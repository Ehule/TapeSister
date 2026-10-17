#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "tapesister/session_log.h"
#include "tapesister/version.h"
#include <SDL2/SDL.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#else
#include <dirent.h>
#include <unistd.h>
#include <signal.h>
#include <sys/resource.h>
#endif

enum { AUDIO_CAP=8192, EVENT_CAP=32, TEXT_CAP=32768, PATH_CAP=2048 };
typedef struct { uint64_t start, elapsed, serial; uint32_t rate, frames, config, epoch; } AudioPacket;
typedef struct { uint64_t at; unsigned length; char type[40], text[TEXT_CAP]; } EventPacket;
static struct {
    atomic_uint state, stop, read, write, epoch, audio_lost, event_lost, export_request, export_result;
    atomic_uint_least64_t bytes;
    atomic_flag gate;
    unsigned event_read, event_write;
    AudioPacket audio[AUDIO_CAP]; EventPacket events[EVENT_CAP];
    SDL_Thread *thread;
    uint64_t origin, frequency, limit, audio_serial; /* serial: callback-owned */
    char path[PATH_CAP], saved[PATH_CAP], directory[PATH_CAP-300];
} log_state = { .gate=ATOMIC_FLAG_INIT };

static double milliseconds(uint64_t tick)
{ return tick >= log_state.origin ? (tick-log_state.origin)*1000.0/log_state.frequency : 0; }
int ts_session_log_enabled(void)
{ unsigned s=atomic_load_explicit(&log_state.state,memory_order_relaxed);return s==TS_SESSION_STARTING || s==TS_SESSION_RECORDING; }
const char *ts_session_log_state_name(int s)
{ static const char *names[]={"OFF","STARTING","RECORDING","FULL","WRITE ERROR","STOPPED"};return s>=0 && s<6?names[s]:"UNKNOWN"; }
void ts_session_log_status(TsSessionLogStatus *s)
{
    s->state=atomic_load(&log_state.state);s->export_result=atomic_load(&log_state.export_result);
    s->dropped_audio=atomic_load(&log_state.audio_lost);s->dropped_events=atomic_load(&log_state.event_lost);
    s->bytes=atomic_load(&log_state.bytes);s->path=log_state.path;s->export_path=log_state.saved;
}
static int event_at(uint64_t at,const char *type,const char *text)
{
    if(!ts_session_log_enabled())return 0;
    if(atomic_flag_test_and_set_explicit(&log_state.gate,memory_order_acquire)) {
        atomic_fetch_add(&log_state.event_lost,1);return 0;
    }
    int queued=0;
    if(log_state.event_write-log_state.event_read>=EVENT_CAP)atomic_fetch_add(&log_state.event_lost,1);
    else {
        EventPacket *p=&log_state.events[log_state.event_write%EVENT_CAP];p->at=at;
        snprintf(p->type,sizeof(p->type),"%s",type);
        p->length=(unsigned)snprintf(p->text,sizeof(p->text),"%s",text);
        if(p->length>=sizeof(p->text)) {
            p->length=sizeof(p->text)-1;atomic_fetch_add(&log_state.event_lost,1);
            memcpy(p->text+p->length-13," [TRUNCATED]",13);
        }
        ++log_state.event_write;
        queued=1;
    }
    atomic_flag_clear_explicit(&log_state.gate,memory_order_release);
    return queued;
}
int ts_session_log_text(const char *type,const char *text)
{ return ts_session_log_enabled()?event_at(SDL_GetPerformanceCounter(),type,text):0; }
void ts_session_log_event(const char *type,const char *format,...)
{
    if(!ts_session_log_enabled())return;
    uint64_t at=SDL_GetPerformanceCounter();char text[512];va_list ap;va_start(ap,format);
    vsnprintf(text,sizeof(text),format,ap);va_end(ap);event_at(at,type,text);
}
void ts_session_log_discontinuity(const char *reason)
{ atomic_fetch_add(&log_state.epoch,1);ts_session_log_text("audio_discontinuity",reason); }
void ts_session_log_control(uint64_t at,uint64_t hold,uint64_t wait,uint64_t frequency,const char *site)
{
    if(!ts_session_log_enabled() || !frequency || (hold<frequency/1000 && wait<frequency/200))return;
    char text[256];snprintf(text,sizeof(text),"site=%s hold_ms=%.6f ui_wait_ms=%.6f (wait does not exclude playback)",
        site?site:"unknown",hold*1000.0/frequency,wait*1000.0/frequency);event_at(at,"control_timing",text);
}
void ts_session_log_audio(uint64_t start,uint64_t elapsed,uint32_t rate,uint32_t frames,uint32_t config)
{
    if(!ts_session_log_enabled())return;
    uint64_t serial=++log_state.audio_serial;
    unsigned w=atomic_load_explicit(&log_state.write,memory_order_relaxed);
    unsigned r=atomic_load_explicit(&log_state.read,memory_order_acquire);
    if(w-r>=AUDIO_CAP){atomic_fetch_add_explicit(&log_state.audio_lost,1,memory_order_relaxed);return;}
    log_state.audio[w%AUDIO_CAP]=(AudioPacket){start,elapsed,serial,rate,frames,config,atomic_load_explicit(&log_state.epoch,memory_order_relaxed)};
    atomic_store_explicit(&log_state.write,w+1,memory_order_release);
}

/* Filesystem and system counters are exclusively writer-thread work. */
static unsigned process_id(void)
{
#ifdef _WIN32
    return (unsigned)GetCurrentProcessId();
#else
    return (unsigned)getpid();
#endif
}
static int process_alive(unsigned pid)
{
#ifdef _WIN32
    HANDLE p=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);DWORD code=0;
    if(!p)return GetLastError()==ERROR_ACCESS_DENIED;
    int alive=GetExitCodeProcess(p,&code) && code==STILL_ACTIVE;CloseHandle(p);return alive;
#else
    return kill((pid_t)pid,0)==0 || errno==EPERM;
#endif
}
static void remove_utf8(const char *path)
{
#ifdef _WIN32
    WCHAR w[PATH_CAP];if(MultiByteToWideChar(CP_UTF8,0,path,-1,w,PATH_CAP))DeleteFileW(w);
#else
    remove(path);
#endif
}
/* Retain the eight newest inactive auto logs and their saved copies. Never
   touch another live process. Interrupted logs remain readable JSONL. */
static void prune_candidate(const char *name,char keep[8][256],unsigned *count)
{
    unsigned pid=0;size_t n=strlen(name);
    if(n>=256 || n<40 || strncmp(name,"health-session-",15) || strstr(name,".saved.") ||
       strcmp(name+n-6,".jsonl") || sscanf(name+31,"%u-",&pid)!=1 ||
       (pid!=process_id() && process_alive(pid)))return;
    char discard[256]="";
    unsigned at=0;while(at<*count && strcmp(keep[at],name)>0)++at;
    if(at>=8)snprintf(discard,sizeof(discard),"%s",name);
    else {
        if(*count==8)snprintf(discard,sizeof(discard),"%s",keep[7]);else ++*count;
        for(unsigned i=*count-1;i>at;--i)memcpy(keep[i],keep[i-1],256);
        snprintf(keep[at],256,"%s",name);
    }
    if(*discard) {
        char path[PATH_CAP];snprintf(path,sizeof(path),"%s/%s",log_state.directory,discard);remove_utf8(path);
        discard[strlen(discard)-6]=0;snprintf(path,sizeof(path),"%s/%s.saved.jsonl",log_state.directory,discard);remove_utf8(path);
    }
}
static void prune_logs(void)
{
    char keep[8][256]={{0}};unsigned count=0;
#ifdef _WIN32
    WCHAR pattern[PATH_CAP];char utf[PATH_CAP];snprintf(utf,sizeof(utf),"%s/health-session-*.jsonl",log_state.directory);
    if(!MultiByteToWideChar(CP_UTF8,0,utf,-1,pattern,PATH_CAP))return;
    WIN32_FIND_DATAW data;HANDLE h=FindFirstFileW(pattern,&data);if(h==INVALID_HANDLE_VALUE)return;
    do { char name[256];if(WideCharToMultiByte(CP_UTF8,0,data.cFileName,-1,name,sizeof(name),NULL,NULL))prune_candidate(name,keep,&count); }while(FindNextFileW(h,&data));FindClose(h);
#else
    DIR *d=opendir(log_state.directory);if(!d)return;struct dirent *e;
    while((e=readdir(d)))prune_candidate(e->d_name,keep,&count);
    closedir(d);
#endif
}
static int write_bytes(SDL_RWops *file,const char *text,size_t length)
{
    if(atomic_load(&log_state.state)!=TS_SESSION_RECORDING)return 0;
    uint64_t used=atomic_load(&log_state.bytes);
    if(used+length>log_state.limit) {
        const char end[]="{\"type\":\"recording_limit\",\"detail\":\"64 MiB/default session cap reached; later history not recorded\"}\n";
        if(SDL_RWwrite(file,end,1,sizeof(end)-1)!=sizeof(end)-1)atomic_store(&log_state.state,TS_SESSION_ERROR);
        else { atomic_fetch_add(&log_state.bytes,sizeof(end)-1);atomic_store(&log_state.state,TS_SESSION_FULL); }
        return 0;
    }
    if(SDL_RWwrite(file,text,1,length)!=length){atomic_store(&log_state.state,TS_SESSION_ERROR);return 0;}
    atomic_fetch_add(&log_state.bytes,length);return 1;
}
static void record(SDL_RWops *file,uint64_t at,const char *type,const char *fields)
{
    char line[4096];int n=snprintf(line,sizeof(line),"{\"t_ms\":%.6f,\"type\":\"%s\",%s}\n",milliseconds(at),type,fields);
    if(n>0 && n<(int)sizeof(line))write_bytes(file,line,(size_t)n);
}
static void text_record(SDL_RWops *file,const EventPacket *p)
{
    char line[TEXT_CAP*6+256];size_t n=(size_t)snprintf(line,sizeof(line),"{\"t_ms\":%.6f,\"type\":\"%s\",\"detail\":\"",milliseconds(p->at),p->type);
    for(unsigned i=0;i<p->length;++i) {
        unsigned char c=(unsigned char)p->text[i];
        if(c=='"' || c=='\\'){line[n++]='\\';line[n++]=(char)c;}
        else if(c<32)n+=(size_t)snprintf(line+n,sizeof(line)-n,"\\u%04x",c);
        else line[n++]=(char)c;
    }
    memcpy(line+n,"\"}\n",3);write_bytes(file,line,n+3);
}

typedef struct {
    uint64_t count,frames,ticks,peak,over,near,gaps,first,last;
    uint32_t rate,buffer,epoch;
} Window;
static void window_record(SDL_RWops *file,Window *w)
{
    if(!w->count)return;
    char fields[768];
    snprintf(fields,sizeof(fields),"\"from_ms\":%.6f,\"callbacks\":%llu,\"frames\":%llu,\"rate\":%u,\"buffer\":%u,\"epoch\":%u,\"average_ms\":%.6f,\"peak_ms\":%.6f,\"load_percent\":%.4f,\"over_budget\":%llu,\"near_budget\":%llu,\"start_gaps\":%llu",
        milliseconds(w->first),(unsigned long long)w->count,(unsigned long long)w->frames,w->rate,w->buffer,w->epoch,
        w->ticks*1000.0/log_state.frequency/w->count,w->peak*1000.0/log_state.frequency,
        w->frames?w->ticks*100.0/log_state.frequency*w->rate/w->frames:0,
        (unsigned long long)w->over,(unsigned long long)w->near,(unsigned long long)w->gaps);
    record(file,w->last,"audio_window",fields);memset(w,0,sizeof(*w));
}
static void audio_record(SDL_RWops *file,const AudioPacket *p,AudioPacket *previous,Window *w)
{
    if(!p->rate || !p->frames)return;
    if(w->count && (w->rate!=p->rate || w->buffer!=p->frames || w->epoch!=p->epoch || p->start-w->first>=log_state.frequency))window_record(file,w);
    if(!w->count){w->first=p->start;w->rate=p->rate;w->buffer=p->frames;w->epoch=p->epoch;}
    double budget=(double)p->frames/p->rate,elapsed=(double)p->elapsed/log_state.frequency,gap=0;
    if(previous->serial+1==p->serial && previous->rate==p->rate && previous->frames==p->frames && previous->epoch==p->epoch && p->start>=previous->start) {
        double interval=(p->start-previous->start)/(double)log_state.frequency;
        double tolerance=budget*.25;if(tolerance<.001)tolerance=.001;
        if(interval>budget+tolerance){gap=interval-budget;++w->gaps;}
    }
    ++w->count;w->frames+=p->frames;w->ticks+=p->elapsed;w->last=p->start;
    if(p->elapsed>w->peak)w->peak=p->elapsed;
    if(elapsed>=budget)++w->over;else if(elapsed>=budget*.9)++w->near;
    if(elapsed>=budget*.9 || gap>0) {
        char fields[512];snprintf(fields,sizeof(fields),"\"duration_ms\":%.6f,\"budget_ms\":%.6f,\"extra_start_gap_ms\":%.6f,\"over_budget\":%s,\"rate\":%u,\"buffer\":%u,\"configuration\":%u,\"epoch\":%u",
            elapsed*1000,budget*1000,gap*1000,elapsed>=budget?"true":"false",p->rate,p->frames,p->config,p->epoch);
        record(file,p->start,"audio_timing",fields);
    }
    *previous=*p;
}

static void system_record(SDL_RWops *file,uint64_t at)
{
    static double last_cpu;static uint64_t last_at,last_total,last_idle;
    double cpu=-1,system=-1;uint64_t rss=0,total=0,idle=0,available=0;
#ifdef _WIN32
    FILETIME created,exited,kernel,user,si,sk,su;
    if(GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user))
        cpu=((double)(((uint64_t)kernel.dwHighDateTime<<32)|kernel.dwLowDateTime)+(((uint64_t)user.dwHighDateTime<<32)|user.dwLowDateTime))/1e7;
    if(GetSystemTimes(&si,&sk,&su)){idle=((uint64_t)si.dwHighDateTime<<32)|si.dwLowDateTime;total=(((uint64_t)sk.dwHighDateTime<<32)|sk.dwLowDateTime)+(((uint64_t)su.dwHighDateTime<<32)|su.dwLowDateTime);}
    PROCESS_MEMORY_COUNTERS memory;memset(&memory,0,sizeof(memory));memory.cb=sizeof(memory);
    if(GetProcessMemoryInfo(GetCurrentProcess(),&memory,sizeof(memory)))rss=memory.WorkingSetSize;
    MEMORYSTATUSEX memstatus;memset(&memstatus,0,sizeof(memstatus));memstatus.dwLength=sizeof(memstatus);
    if(GlobalMemoryStatusEx(&memstatus))available=memstatus.ullAvailPhys;
#else
    struct rusage usage;if(!getrusage(RUSAGE_SELF,&usage))cpu=usage.ru_utime.tv_sec+usage.ru_utime.tv_usec/1e6+usage.ru_stime.tv_sec+usage.ru_stime.tv_usec/1e6;
    FILE *stat=fopen("/proc/stat","r");unsigned long long u,n,s,i,io,irq,soft,steal;
    if(stat){if(fscanf(stat,"cpu %llu %llu %llu %llu %llu %llu %llu %llu",&u,&n,&s,&i,&io,&irq,&soft,&steal)==8){total=u+n+s+i+io+irq+soft+steal;idle=i+io;}fclose(stat);}
    FILE *mem=fopen("/proc/self/statm","r");unsigned long pages,resident;
    if(mem){if(fscanf(mem,"%lu %lu",&pages,&resident)==2)rss=(uint64_t)resident*(uint64_t)sysconf(_SC_PAGESIZE);fclose(mem);}
    mem=fopen("/proc/meminfo","r");
    if(mem){char line[160];while(fgets(line,sizeof(line),mem)){unsigned long long kb;if(sscanf(line,"MemAvailable: %llu kB",&kb)==1){available=kb*1024;break;}}fclose(mem);}
#endif
    if(last_at<log_state.origin){last_at=last_total=last_idle=0;last_cpu=0;}
    double process=-1;
    if(last_at && at>last_at && cpu>=last_cpu && cpu>=0)process=(cpu-last_cpu)*100.0*log_state.frequency/(at-last_at);
    if(last_at && total>last_total && idle>=last_idle)system=100.0*(1.0-(double)(idle-last_idle)/(total-last_total));
    char fields[512];snprintf(fields,sizeof(fields),"\"process_cpu_one_core_percent\":%.3f,\"system_cpu_percent\":%.3f,\"resident_bytes\":%llu,\"system_available_bytes\":%llu,\"dropped_audio_records\":%u,\"dropped_events\":%u",
        process,system,(unsigned long long)rss,(unsigned long long)available,atomic_load(&log_state.audio_lost),atomic_load(&log_state.event_lost));
    record(file,at,"system",fields);last_cpu=cpu;last_at=at;last_total=total;last_idle=idle;
}
static int copy_saved(SDL_RWops **file)
{
    /* Close flushes SDL stdio before opening the snapshot. Writer is the only
       file owner; audio keeps using its queue while this copy runs. */
    if(*file){if(SDL_RWclose(*file)){*file=NULL;atomic_store(&log_state.state,TS_SESSION_ERROR);return 0;}*file=NULL;}
    SDL_RWops *in=SDL_RWFromFile(log_state.path,"rb"),*out=NULL;int ok=in!=NULL;
    if(ok){out=SDL_RWFromFile(log_state.saved,"wb");ok=out!=NULL;}
    if(ok){char bytes[16384];Sint64 remaining=SDL_RWsize(in);if(remaining<0)ok=0;
        while(ok && remaining>0){size_t n=remaining>(Sint64)sizeof(bytes)?sizeof(bytes):(size_t)remaining;
            if(SDL_RWread(in,bytes,1,n)!=n || SDL_RWwrite(out,bytes,1,n)!=n)ok=0;
            remaining-=(Sint64)n;}}
    if(in)SDL_RWclose(in);
    if(out && SDL_RWclose(out))ok=0;
    *file=SDL_RWFromFile(log_state.path,"ab");if(!*file)atomic_store(&log_state.state,TS_SESSION_ERROR);
    if(!ok)remove_utf8(log_state.saved);
    return ok;
}
static int writer(void *unused)
{
    (void)unused;SDL_SetThreadPriority(SDL_THREAD_PRIORITY_LOW);prune_logs();
    SDL_RWops *file=SDL_RWFromFile(log_state.path,"wb");
    if(!file){atomic_store(&log_state.state,TS_SESSION_ERROR);return 0;}
    atomic_store(&log_state.state,TS_SESSION_RECORDING);
    EventPacket header={0};header.at=log_state.origin;snprintf(header.type,sizeof(header.type),"session_start");
    header.length=(unsigned)snprintf(header.text,sizeof(header.text),"TapeSister %s (%s %s); schema=1; platform=%s; CPUs=%d; RAM_MB=%d; monotonic_frequency=%llu; UTC start encoded in filename; cap_bytes=%llu; timing observations are not proof of audible dropouts; -1 system metrics mean unavailable; record order may differ from event time; no audio samples recorded",
        TAPESISTER_BUILD_MARKER,__DATE__,__TIME__,SDL_GetPlatform(),SDL_GetCPUCount(),SDL_GetSystemRAM(),
        (unsigned long long)log_state.frequency,(unsigned long long)log_state.limit);text_record(file,&header);
    AudioPacket previous={0};Window window={0};uint64_t last_system=0,last_flush=log_state.origin;
    unsigned seen_loss=0;
    for(;;) {
        unsigned r=atomic_load_explicit(&log_state.read,memory_order_relaxed),w=atomic_load_explicit(&log_state.write,memory_order_acquire);
        unsigned loss=atomic_load(&log_state.audio_lost);
        if(loss!=seen_loss){memset(&previous,0,sizeof(previous));seen_loss=loss;}
        /* Snapshot a finite batch; a continuously busy producer cannot starve
           event draining, exporting or shutdown. */
        while(r!=w){AudioPacket p=log_state.audio[r%AUDIO_CAP];++r;atomic_store_explicit(&log_state.read,r,memory_order_release);audio_record(file,&p,&previous,&window);}
        for(unsigned i=0;i<EVENT_CAP;++i) {
            EventPacket event;int have=0;
            if(!atomic_flag_test_and_set_explicit(&log_state.gate,memory_order_acquire)) {
                if(log_state.event_read!=log_state.event_write){EventPacket *p=&log_state.events[log_state.event_read%EVENT_CAP];
                    memcpy(&event,p,offsetof(EventPacket,text)+p->length+1);++log_state.event_read;have=1;}
                atomic_flag_clear_explicit(&log_state.gate,memory_order_release);
            }
            if(!have)break;
            text_record(file,&event);
        }
        uint64_t now=SDL_GetPerformanceCounter();
        if(window.count && now>=window.first && now-window.first>=log_state.frequency)window_record(file,&window);
        if(!last_system || now-last_system>=log_state.frequency){system_record(file,now);last_system=now;}
        if(atomic_exchange(&log_state.export_request,0)) {
            window_record(file,&window);
            record(file,now,"export_boundary","\"detail\":\"Snapshot ends here; live session continues\"");
            atomic_store(&log_state.export_result,copy_saved(&file)?2:3);last_flush=now;
        }
        if(atomic_load(&log_state.stop))break;
        /* Flush at least once a second for recovery after a crash. */
        if(file && now-last_flush>=log_state.frequency) {
            int failed=SDL_RWclose(file);file=SDL_RWFromFile(log_state.path,"ab");
            if(failed || !file)atomic_store(&log_state.state,TS_SESSION_ERROR);
            last_flush=now;
        }
        SDL_Delay(25);
    }
    window_record(file,&window);system_record(file,SDL_GetPerformanceCounter());
    record(file,SDL_GetPerformanceCounter(),"session_end","\"detail\":\"Normal shutdown\"");
    if(file && SDL_RWclose(file))atomic_store(&log_state.state,TS_SESSION_ERROR);
    if(atomic_load(&log_state.state)==TS_SESSION_RECORDING)atomic_store(&log_state.state,TS_SESSION_STOPPED);
    return 0;
}
int ts_session_log_start(const char *directory,uint64_t limit)
{
    if(log_state.thread)return 0;
    atomic_store(&log_state.state,TS_SESSION_OFF);atomic_store(&log_state.stop,0);
    atomic_store(&log_state.read,0);atomic_store(&log_state.write,0);atomic_store(&log_state.epoch,0);
    atomic_store(&log_state.audio_lost,0);atomic_store(&log_state.event_lost,0);atomic_store(&log_state.bytes,0);
    atomic_store(&log_state.export_request,0);atomic_store(&log_state.export_result,0);
    atomic_flag_clear(&log_state.gate);log_state.event_read=log_state.event_write=0;log_state.audio_serial=0;
    log_state.path[0]=log_state.saved[0]=0;
    if(!atomic_is_lock_free(&log_state.state) || !atomic_is_lock_free(&log_state.read) || !atomic_is_lock_free(&log_state.write) ||
       !atomic_is_lock_free(&log_state.audio_lost) || !atomic_is_lock_free(&log_state.epoch)) {
        atomic_store(&log_state.state,TS_SESSION_ERROR);return 0;
    }
    char *allocated=directory?NULL:SDL_GetPrefPath("TapeSister","Health");
    if(!directory)directory=allocated;
    if(!directory || strlen(directory)>=sizeof(log_state.directory)){SDL_free(allocated);atomic_store(&log_state.state,TS_SESSION_ERROR);return 0;}
    snprintf(log_state.directory,sizeof(log_state.directory),"%s",directory);SDL_free(allocated);
    log_state.origin=SDL_GetPerformanceCounter();log_state.frequency=SDL_GetPerformanceFrequency();log_state.limit=limit?limit:64u*1024u*1024u;
    time_t wall=time(NULL);struct tm utc;
#ifdef _WIN32
    gmtime_s(&utc,&wall);
#else
    gmtime_r(&wall,&utc);
#endif
    char date[32];strftime(date,sizeof(date),"%Y%m%d-%H%M%S",&utc);
    snprintf(log_state.path,sizeof(log_state.path),"%s/health-session-%s-%u-%llu.jsonl",log_state.directory,date,process_id(),(unsigned long long)log_state.origin);
    snprintf(log_state.saved,sizeof(log_state.saved),"%.*s.saved.jsonl",(int)strlen(log_state.path)-6,log_state.path);
    atomic_store(&log_state.state,TS_SESSION_STARTING);log_state.thread=SDL_CreateThread(writer,"session health log",NULL);
    if(!log_state.thread){atomic_store(&log_state.state,TS_SESSION_ERROR);return 0;}return 1;
}
int ts_session_log_export(void)
{
    int state=atomic_load(&log_state.state);
    if(!log_state.thread || state==TS_SESSION_ERROR || state==TS_SESSION_OFF || atomic_load(&log_state.export_result)==1)return 0;
    atomic_store(&log_state.export_result,1);atomic_store(&log_state.export_request,1);return 1;
}
void ts_session_log_stop(void)
{
    if(!log_state.thread)return;
    atomic_store(&log_state.stop,1);SDL_WaitThread(log_state.thread,NULL);log_state.thread=NULL;
}
