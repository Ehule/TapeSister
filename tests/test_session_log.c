#ifdef NDEBUG
#undef NDEBUG
#endif
#define SDL_MAIN_HANDLED
#include "tapesister/session_log.h"
#include "tapesister/config.h"
#include <SDL2/SDL.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

static TsSessionLogStatus status(void)
{TsSessionLogStatus s;ts_session_log_status(&s);return s;}
static void await_state(int state)
{for(int i=0;i<500 && status().state!=state;++i)SDL_Delay(10);assert(status().state==state);}
static void await_export(void)
{for(int i=0;i<500 && status().export_result==1;++i)SDL_Delay(10);assert(status().export_result==2);}
static char *read_log(const char *path)
{
    SDL_RWops *file=SDL_RWFromFile(path,"rb");assert(file);Sint64 n=SDL_RWsize(file);assert(n>=0);
    char *text=malloc((size_t)n+1);assert(text);assert(SDL_RWread(file,text,1,(size_t)n)==(size_t)n);
    text[n]=0;SDL_RWclose(file);return text;
}
static int controls(void *arg)
{for(int i=0;i<2000;++i)ts_session_log_event("concurrent_edit","producer=%d index=%d",*(int *)arg,i);return 0;}
static void temporary_directory(const char *path,int create)
{
#ifdef _WIN32
    WCHAR wide[2048];assert(MultiByteToWideChar(CP_UTF8,0,path,-1,wide,2048));
    assert(create?CreateDirectoryW(wide,NULL):RemoveDirectoryW(wide));
#else
    assert(!(create?mkdir(path,0775):rmdir(path)));
#endif
}
static void remove_project_log(const char *path)
{
#ifdef _WIN32
    WCHAR wide[2048];assert(MultiByteToWideChar(CP_UTF8,0,path,-1,wide,2048));assert(DeleteFileW(wide));
#else
    assert(!remove(path));
#endif
}

int main(int argc,char **argv)
{
    assert(SDL_Init(SDL_INIT_TIMER)==0);
    char *directory=SDL_GetPrefPath("TapeSisterTests","Health");assert(directory);
    TsConfig config,loaded;ts_config_init(&config);assert(config.session_log==1);
    char ini[2048],error[160];snprintf(ini,sizeof(ini),"%ssession-test.ini",directory);
    config.session_log=0;assert(ts_config_save(&config,ini,error,sizeof(error)));
    assert(ts_config_load(&loaded,ini,error,sizeof(error)));assert(loaded.session_log==0);remove(ini);

    uint64_t frequency=SDL_GetPerformanceFrequency(),start=SDL_GetPerformanceCounter();
    for(int i=0;i<100000;++i)ts_session_log_audio(start,frequency/1000,48000,512,0);
    double off=(SDL_GetPerformanceCounter()-start)*1e9/frequency/100000;
    assert(ts_session_log_start(directory,0));await_state(TS_SESSION_RECORDING);
    ts_session_log_text("escaping","quoted \"text\"\\path\nnext\tline");
    ts_session_log_text("heard_click","manual marker");
    start=SDL_GetPerformanceCounter();
    ts_session_log_audio(start,frequency/1000,48000,512,1);
    ts_session_log_audio(start+frequency*512/48000,frequency/80,48000,512,1); /* 12.5 ms overrun */
    ts_session_log_control(start,frequency/500,frequency/100,frequency,"test_control");
    ts_session_log_text("health_reset","retained history");
    ts_session_log_discontinuity("device changed");
    ts_session_log_audio(start+frequency,frequency/1000,48000,1024,2);
    assert(ts_session_log_export());await_export();
    char path[2048],saved[2048];snprintf(path,sizeof(path),"%s",status().path);snprintf(saved,sizeof(saved),"%s",status().export_path);
    char *text=read_log(saved);
    assert(strstr(text,"session_start") && strstr(text,"heard_click") && strstr(text,"health_reset"));
    assert(strstr(text,"\"over_budget\":true") && strstr(text,"control_timing"));
    assert(strstr(text,"\\\"text\\\"\\\\path\\u000anext\\u0009line"));
    assert(strstr(text,"\"epoch\":1") && strstr(text,"\"buffer\":1024"));
    free(text);
    uint64_t accepted_ticks=0;
    for(int batch=0;batch<20;++batch) {
        uint64_t before=SDL_GetPerformanceCounter();
        for(int i=0;i<1000;++i)ts_session_log_audio(start,frequency/1000,48000,512,0);
        accepted_ticks+=SDL_GetPerformanceCounter()-before;SDL_Delay(10);
    }
    assert(status().dropped_audio==0);
    printf("Session accepted enqueue: %.1f ns/call over 20000 calls\n",accepted_ticks*1e9/frequency/20000);
    ts_session_log_text("after_export","recording continues");
    ts_session_log_stop();text=read_log(path);
    assert(strstr(text,"after_export") && strstr(text,"session_end"));free(text);
    text=read_log(saved);assert(!strstr(text,"after_export"));free(text);
    if(argc>1)printf("SESSION_LOG=%s\n",path);else remove(path);
    remove(saved);

    /* A slow writer/full queue drops diagnostics, never stalls the producer. */
    assert(ts_session_log_start(directory,0));await_state(TS_SESSION_RECORDING);
    int ids[]={1,2};SDL_Thread *a=SDL_CreateThread(controls,"log events A",&ids[0]);
    SDL_Thread *b=SDL_CreateThread(controls,"log events B",&ids[1]);assert(a && b);
    start=SDL_GetPerformanceCounter();
    for(int i=0;i<1000000;++i)ts_session_log_audio(start,frequency/1000,48000,512,0);
    double on=(SDL_GetPerformanceCounter()-start)*1e9/frequency/1000000;
    SDL_WaitThread(a,NULL);SDL_WaitThread(b,NULL);
    assert(status().dropped_audio>0);snprintf(path,sizeof(path),"%s",status().path);
    ts_session_log_stop();text=read_log(path);assert(strstr(text,"dropped_audio_records") && strstr(text,"session_end"));free(text);remove(path);
    printf("Session producer: disabled %.1f ns/call; enabled/overflow stress %.1f ns/call\n",off,on);

    assert(ts_session_log_start(directory,1024));await_state(TS_SESSION_RECORDING);
    for(int i=0;i<20;++i)ts_session_log_text("large","A bounded recording stops safely when its disk budget is reached.");
    await_state(TS_SESSION_FULL);assert(!ts_session_log_enabled());assert(status().bytes<1280);
    assert(ts_session_log_export());await_export();snprintf(path,sizeof(path),"%s",status().path);snprintf(saved,sizeof(saved),"%s",status().export_path);
    ts_session_log_stop();text=read_log(path);assert(strstr(text,"recording_limit"));free(text);remove(path);remove(saved);

    /* Project snapshots: spaces/UTF-8, Save As, failed replacement preserving
       the earlier snapshot, and queued saves completed by immediate shutdown. */
    char app_project[96];snprintf(app_project,sizeof(app_project),"Session Project-\xc3\xa9-%llu",(unsigned long long)SDL_GetPerformanceCounter());
    char *project_directory=SDL_GetPrefPath("TapeSisterTests",app_project);assert(project_directory);
    char project[2048],project_saved[2048],other_saved[2048];
    snprintf(project,sizeof(project),"%sPerformance.tsr",project_directory);
    assert(ts_session_log_start(directory,0));await_state(TS_SESSION_RECORDING);
    assert(ts_session_log_export_destination(project,project_saved,sizeof(project_saved)));
    assert(strstr(project_saved,"/logs/health-session-") || strstr(project_saved,"\\logs/health-session-"));
    char windows_path[2048];
    assert(ts_session_log_export_destination("C:\\Music Sessions\\Show.tsr",windows_path,sizeof(windows_path)));
    assert(!strncmp(windows_path,"C:\\Music Sessions\\logs/",23));
    assert(ts_session_log_export_destination("\\\\server\\share\\Show.tsr",windows_path,sizeof(windows_path)));
    assert(!strncmp(windows_path,"\\\\server\\share\\logs/",20));
    assert(ts_session_log_export_destination("Show.tsr",windows_path,sizeof(windows_path)));
    assert(!strncmp(windows_path,"./logs/",7));
    char tiny[8];assert(!ts_session_log_export_destination(project,tiny,sizeof(tiny)) && !*tiny);
    assert(ts_session_log_text("before_project_save","retained with project"));
    assert(ts_session_log_export_project(project,0));await_export();
    assert(!status().export_copy_path && !strcmp(status().export_path,project_saved));
    text=read_log(project_saved);assert(strstr(text,"before_project_save"));free(text);
    /* A directory in place of the temporary file forces copy failure without
       relying on permissions (the suite may run as root). */
    char blocked[2052];snprintf(blocked,sizeof(blocked),"%s.tmp",project_saved);
    temporary_directory(blocked,1);
    assert(ts_session_log_text("after_project_save","new live history"));
    assert(ts_session_log_export_project(project,1));
    for(int i=0;i<500 && status().export_result==1;++i)SDL_Delay(10);
    assert(status().export_result==3 && status().state==TS_SESSION_RECORDING);
    text=read_log(project_saved);assert(strstr(text,"before_project_save") && !strstr(text,"after_project_save"));free(text);
    temporary_directory(blocked,0);
    char *other_directory=SDL_GetPrefPath("TapeSisterTests","Session Save As");assert(other_directory);
    char other[2048];snprintf(other,sizeof(other),"%sOther.tsr",other_directory);
    assert(ts_session_log_export_destination(other,other_saved,sizeof(other_saved)));
    assert(ts_session_log_export_project(project,0));
    assert(ts_session_log_export_project(other,0));
    ts_session_log_stop();assert(status().export_result==2 && status().export_serial==4);
    text=read_log(project_saved);assert(strstr(text,"after_project_save"));free(text);
    text=read_log(other_saved);assert(strstr(text,"after_project_save"));free(text);
    /* Pruning the recovery directory must not remove project-side snapshots. */
    remove(status().path);assert(ts_session_log_start(directory,0));await_state(TS_SESSION_RECORDING);
    text=read_log(project_saved);assert(strstr(text,"before_project_save"));free(text);
    ts_session_log_stop();remove(status().path);remove_project_log(project_saved);remove_project_log(other_saved);
    SDL_free(project_directory);SDL_free(other_directory);

    char invalid[2048];snprintf(invalid,sizeof(invalid),"%snonexistent/subdirectory",directory);
    assert(ts_session_log_start(invalid,0));await_state(TS_SESSION_ERROR);
    ts_session_log_audio(start,frequency,48000,512,0);assert(!ts_session_log_export());ts_session_log_stop();
    char app[80];snprintf(app,sizeof(app),"HealthRetention-%llu",(unsigned long long)SDL_GetPerformanceCounter());
    char *retention=SDL_GetPrefPath("TapeSisterTests",app);assert(retention);
    for(int i=1;i<=12;++i) {
        snprintf(path,sizeof(path),"%shealth-session-200001%02d-000000-99999999-1.jsonl",retention,i);
        SDL_RWops *file=SDL_RWFromFile(path,"wb");assert(file);SDL_RWclose(file);
    }
    snprintf(saved,sizeof(saved),"%sunrelated.txt",retention);SDL_RWops *file=SDL_RWFromFile(saved,"wb");assert(file);SDL_RWclose(file);
    assert(ts_session_log_start(retention,0));await_state(TS_SESSION_RECORDING);
    for(int i=1;i<=12;++i) {
        snprintf(path,sizeof(path),"%shealth-session-200001%02d-000000-99999999-1.jsonl",retention,i);
        file=SDL_RWFromFile(path,"rb");assert((file!=NULL)==(i>4));if(file)SDL_RWclose(file);remove(path);
    }
    file=SDL_RWFromFile(saved,"rb");assert(file);SDL_RWclose(file);remove(saved);
    ts_session_log_stop();remove(status().path);SDL_free(retention);
    SDL_free(directory);SDL_Quit();puts("Session recording: events, timing, reset history, asynchronous export, concurrent producers, overflow, storage cap and path failure passed");
    (void)argv;return 0;
}
