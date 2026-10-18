/* One isolated native-engine process per export. Only the parent publishes a
   completed WAV. IPC files are confined to a private, uniquely created job. */
#ifndef _WIN32
#include <dirent.h>
#include <spawn.h>
#include <signal.h>
#include <fcntl.h>
extern char **environ;
#endif

enum { OFFLINE_TRACKER=2, OFFLINE_MOSAIC=4, OFFLINE_PATH=1024 };
typedef struct {
    unsigned rate, bits, sources, channels;
    double seconds, tail;
    int overwrite, sister_enabled, sister_rolling;
    float program_gain, master_gain, output_gain;
    int limiter_enabled;
    float limiter_ceiling, limiter_lookahead, limiter_release;
    int eq_run, router_run, prism_run, spatial_morph, spatial_rise;
    int router_timer[TS_ROUTER_COUNT];
    TsSpeakerSetup speakers;
    TsAudioChannelRange output_range;
    TsSisterFxSlotState fx_slot[TS_SISTER_FX_SLOT_COUNT];
    TsSisterFxRamp fx_master;
    int fx_snapshot;
    char project[OFFLINE_PATH], output[OFFLINE_PATH], partial[OFFLINE_PATH];
} OfflineOptions;
typedef struct { uint64_t frames, total; int state; char message[192]; } OfflineProgress;
static struct {
    OfflineOptions options;
    OfflineProgress progress;
    char directory[OFFLINE_PATH];
    int active, cancelling;
    Uint32 poll, cancel_time;
#ifdef _WIN32
    HANDLE process;
#else
    pid_t pid;
#endif
} offline_job;

static int offline_join(char *out,size_t size,const char *dir,const char *name)
{ int n=snprintf(out,size,"%s/%s",dir,name);return n>0 && (size_t)n<size; }
static int offline_replace(const char *from,const char *to,int overwrite)
{
#ifdef _WIN32
    return MoveFileExA(from,to,overwrite?MOVEFILE_REPLACE_EXISTING:0)!=0;
#else
    if(overwrite)return rename(from,to)==0;
    if(link(from,to))return 0;
    unlink(from);return 1;
#endif
}
static int offline_write_blob(const char *dir,const char *name,const void *data,size_t bytes)
{
    char path[OFFLINE_PATH],temp[OFFLINE_PATH];
    if(!offline_join(path,sizeof(path),dir,name) || snprintf(temp,sizeof(temp),"%s.tmp",path)>=(int)sizeof(temp))return 0;
    FILE *f=fopen(temp,"wb");if(!f)return 0;
    int ok=fwrite(data,1,bytes,f)==bytes;if(fclose(f))ok=0;
    if(ok)ok=offline_replace(temp,path,1);
    if(!ok)remove(temp);return ok;
}
static int offline_read_blob(const char *dir,const char *name,void *data,size_t bytes)
{
    char path[OFFLINE_PATH];if(!offline_join(path,sizeof(path),dir,name))return 0;
    FILE *f=fopen(path,"rb");if(!f)return 0;
    int ok=fread(data,1,bytes,f)==bytes && fgetc(f)==EOF && !ferror(f);
    fclose(f);return ok;
}
/* Never traverse a symlink/reparse point during private snapshot cleanup. */
static void offline_remove_tree(const char *path)
{
#ifdef _WIN32
    DWORD a=GetFileAttributesA(path);if(a==INVALID_FILE_ATTRIBUTES)return;
    if(!(a&FILE_ATTRIBUTE_DIRECTORY)){DeleteFileA(path);return;}
    if(!(a&FILE_ATTRIBUTE_REPARSE_POINT)) {
        char glob[OFFLINE_PATH];WIN32_FIND_DATAA e;
        if(!offline_join(glob,sizeof(glob),path,"*"))return;
        HANDLE h=FindFirstFileA(glob,&e);
        if(h!=INVALID_HANDLE_VALUE) {
            do {char child[OFFLINE_PATH];if(strcmp(e.cFileName,".") && strcmp(e.cFileName,"..") &&
                offline_join(child,sizeof(child),path,e.cFileName))offline_remove_tree(child);
            }while(FindNextFileA(h,&e));FindClose(h);
        }
    }
    RemoveDirectoryA(path);
#else
    struct stat s;if(lstat(path,&s))return;
    if(!S_ISDIR(s.st_mode)){unlink(path);return;}
    DIR *d=opendir(path);if(!d)return;struct dirent *e;
    while((e=readdir(d))) {char child[OFFLINE_PATH];if(strcmp(e->d_name,".") && strcmp(e->d_name,"..") &&
        offline_join(child,sizeof(child),path,e->d_name))offline_remove_tree(child);}
    closedir(d);rmdir(path);
#endif
}
static int offline_valid(const OfflineOptions *o,char *error,size_t size)
{
    if(!o || !isfinite(o->seconds) || !isfinite(o->tail) || o->seconds<=0 || o->seconds*o->rate<1 || o->seconds>14400 ||
        o->tail<0 || o->tail>600 || o->rate<8000 || o->rate>192000 ||
        (o->bits!=24 && o->bits!=32) || !(o->sources&6u) || (o->sources&~6u) ||
        o->channels<2 || o->channels>255) {
        snprintf(error,size,"Choose Mosaic/TrackSister, 0-4 hours, and a tail up to 600 seconds.");return 0;
    }
    if((ceil(o->seconds*o->rate)+ceil(o->tail*o->rate))*2*(o->bits/8)>UINT32_MAX-80u) {
        snprintf(error,size,"WAV exceeds 4 GB. Shorten the duration or choose 24-bit.");return 0;
    }
    return 1;
}
static void offline_job_release(void)
{
#ifdef _WIN32
    if(offline_job.process)CloseHandle(offline_job.process);offline_job.process=NULL;
#endif
    if(*offline_job.options.partial)remove(offline_job.options.partial);
    if(*offline_job.directory)offline_remove_tree(offline_job.directory);
    *offline_job.directory=0;offline_job.active=offline_job.cancelling=0;
}
static int offline_job_prepare(OfflineOptions *options,char *error,size_t size)
{
    if(offline_job.active){snprintf(error,size,"An offline render is already running.");return 0;}
    if(!offline_valid(options,error,size))return 0;
    memset(&offline_job,0,sizeof(offline_job));
    char *base=SDL_GetPrefPath("TapeSister","RenderJobs");
    if(!base){snprintf(error,size,"Cannot create render snapshot folder.");return 0;}
    int n=snprintf(offline_job.directory,sizeof(offline_job.directory),"%sjob-%llu",base,(unsigned long long)SDL_GetPerformanceCounter());SDL_free(base);
    if(n<0 || n>=(int)sizeof(offline_job.directory))goto failed;
#ifdef _WIN32
    if(!CreateDirectoryA(offline_job.directory,NULL))goto failed;
#else
    if(mkdir(offline_job.directory,0700))goto failed;
#endif
    n=snprintf(options->project,sizeof(options->project),"%s/Snapshot/Snapshot.tsr",offline_job.directory);
    if(n<0 || n>=(int)sizeof(options->project))goto owned_failed;
    n=snprintf(options->partial,sizeof(options->partial),"%s.tapesister-%llu.part",options->output,(unsigned long long)SDL_GetPerformanceCounter());
    if(n<0 || n>=(int)sizeof(options->partial))goto owned_failed;
    offline_job.options=*options;return 1;
owned_failed:
    offline_remove_tree(offline_job.directory);
failed:
    *offline_job.directory=0;snprintf(error,size,"Cannot create render snapshot (path too long or folder unavailable).");return 0;
}
static int offline_job_launch(char *error,size_t size)
{
    if(!offline_write_blob(offline_job.directory,"options",&offline_job.options,sizeof(offline_job.options)))goto failed;
    char executable[OFFLINE_PATH];char *base=SDL_GetBasePath();if(!base)goto failed;
    int n=snprintf(executable,sizeof(executable),"%s%s",base,
#ifdef _WIN32
        "TapeSister.exe"
#else
        "tapesister"
#endif
    );SDL_free(base);if(n<0 || n>=(int)sizeof(executable))goto failed;
#ifdef _WIN32
    char command[OFFLINE_PATH*2+100];STARTUPINFOA s={0};PROCESS_INFORMATION p;
    s.cb=sizeof(s);
    snprintf(command,sizeof(command),"\"%s\" --offline-worker \"%s\" %lu",executable,offline_job.directory,(unsigned long)GetCurrentProcessId());
    if(!CreateProcessA(executable,command,NULL,NULL,FALSE,CREATE_NO_WINDOW|BELOW_NORMAL_PRIORITY_CLASS,NULL,NULL,&s,&p))goto failed;
    offline_job.process=p.hProcess;CloseHandle(p.hThread);
#else
    char parent[32];snprintf(parent,sizeof(parent),"%ld",(long)getpid());
    char *args[]={executable,"--offline-worker",offline_job.directory,parent,NULL};
    if(posix_spawn(&offline_job.pid,executable,NULL,NULL,args,environ))goto failed;
#endif
    offline_job.active=1;offline_job.progress=(OfflineProgress){.state=0};
    snprintf(offline_job.progress.message,sizeof(offline_job.progress.message),"Loading project snapshot...");
    return 1;
failed:
    snprintf(error,size,"Could not launch offline renderer.");offline_job_release();return 0;
}
static void offline_job_cancel(void)
{
    if(!offline_job.active || offline_job.cancelling)return;
    char flag=1;offline_write_blob(offline_job.directory,"cancel",&flag,1);
    offline_job.cancelling=1;offline_job.cancel_time=SDL_GetTicks();
}
static void offline_job_kill(void)
{
#ifdef _WIN32
    if(offline_job.process)TerminateProcess(offline_job.process,1);
#else
    if(offline_job.pid>0)kill(offline_job.pid,SIGKILL);
#endif
}
static void offline_job_poll(void)
{
    if(!offline_job.active || SDL_GetTicks()-offline_job.poll<100)return;
    offline_job.poll=SDL_GetTicks();OfflineProgress p;
    if(offline_read_blob(offline_job.directory,"progress",&p,sizeof(p)))offline_job.progress=p;
    if(offline_job.cancelling && SDL_GetTicks()-offline_job.cancel_time>3000)offline_job_kill();
    int exited=0,success=0;
#ifdef _WIN32
    DWORD code;if(GetExitCodeProcess(offline_job.process,&code) && code!=STILL_ACTIVE){exited=1;success=code==0;}
#else
    int status;pid_t result=waitpid(offline_job.pid,&status,WNOHANG);
    if(result>0){exited=1;success=WIFEXITED(status) && WEXITSTATUS(status)==0;}
    else if(result<0 && errno!=EINTR)exited=1;
#endif
    if(!exited)return;
    if(offline_read_blob(offline_job.directory,"progress",&p,sizeof(p)))offline_job.progress=p;
    if(offline_job.cancelling){offline_job.progress.state=3;snprintf(offline_job.progress.message,sizeof(offline_job.progress.message),"Cancelled. Output file unchanged.");}
    else if(success && offline_job.progress.state==2) {
        if(!offline_replace(offline_job.options.partial,offline_job.options.output,offline_job.options.overwrite)) {
            offline_job.progress.state=-1;snprintf(offline_job.progress.message,sizeof(offline_job.progress.message),"Could not publish WAV. Check destination permissions or choose a new name.");
        } else snprintf(offline_job.progress.message,sizeof(offline_job.progress.message),"Render complete: %.160s",path_basename(offline_job.options.output));
    } else {
        if(offline_job.progress.state>=0 || !*offline_job.progress.message)
            snprintf(offline_job.progress.message,sizeof(offline_job.progress.message),"Renderer exited before completing the WAV.");
        offline_job.progress.state=-1;
    }
    offline_job_release();
}
static void offline_job_shutdown(void)
{
    if(offline_job.active) {
        offline_job_kill();
#ifdef _WIN32
        WaitForSingleObject(offline_job.process,1000);
#else
        while(waitpid(offline_job.pid,NULL,0)<0 && errno==EINTR){}
#endif
    }
    offline_job_release();
}
