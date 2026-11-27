#ifdef NDEBUG
#undef NDEBUG
#endif
#define SDL_MAIN_HANDLED
static void live_log_writer_phase(int phase);
#define TS_SESSION_LOG_WRITER_PHASE(phase) live_log_writer_phase(phase)
#include "../src/ts_session_log.c"
#define TS_LIVE_TEST_HARNESS
#define main tapesister_application_main
#include "../src/main_sdl.c"
#undef main
#include <assert.h>
static AudioState a;
static TsUiState u;
static TsSamplePages pages;
static TsInstrument bank;
static MosaicController mosaic;
static PortalController portal;
static SisterWindow sister;
static TransformController transform;
static TsMosaicSource source;
/* The controller replays minutes of virtual time in seconds. Park the real
   writer after each drain so its nonblocking event gate cannot collide with a
   synthetic burst. The --run integration mode leaves this hook disabled. */
static SDL_sem *live_log_reached,*live_log_release;
static atomic_int live_log_controlled;
static void live_log_writer_phase(int phase)
{
    if(phase==2 && atomic_load(&live_log_controlled)) {
        assert(!SDL_SemPost(live_log_reached));
        assert(!SDL_SemWaitTimeout(live_log_release,10000));
    }
}
static void live_log_drain(void)
{
    assert(!SDL_SemPost(live_log_release));
    assert(!SDL_SemWaitTimeout(live_log_reached,10000));
}
static void poll_at(SDL_Window *window,Uint64 now)
{
    live_log_drain();
    live_test_poll(window,0,&a,&u,&pages,&bank,&mosaic,&portal,&sister,&transform,now);
}
static int live_fixture(const char *path)
{
    char error[256];ts_instrument_init(&bank);
    assert(ts_sample_pages_init(&pages,error,sizeof(error)));
    pages.mosaic=ts_mosaic_create();assert(pages.mosaic);
    assert(ts_sample_pages_enable_gallery(&pages,error,sizeof(error)));
    TsSample sample={.frames=4800,.channels=1,.sample_rate=48000};
    sample.data=malloc(sample.frames*sizeof(float));assert(sample.data);
    for(size_t i=0;i<sample.frames;++i)sample.data[i]=.01f*sinf((float)i*.04f);
    snprintf(sample.name,sizeof(sample.name),"SHORT DIAGNOSTIC TONE");
    assert(ts_instrument_import_sample(&bank,&sample,0,0,0,TS_LOOP_FORWARD,error,sizeof(error)));
    TsMosaicSource *s=ts_mosaic_source(pages.mosaic,&sample,error,sizeof(error));assert(s);
    assert(ts_mosaic_add(pages.mosaic,s,0,50));
    pages.mosaic->markers[0]=(TsMosaicMarker){.stored=1,.time=.05};
    pages.mosaic->grid=(TsMosaicGrid){1,0,1};
    assert(ts_gallery_rename(pages.gallery,0,"A short movement",error,sizeof(error)));
    assert(ts_gallery_add(pages.gallery,error,sizeof(error)));
    assert(ts_gallery_rename(pages.gallery,1,"Empty movement",error,sizeof(error)));
    int saved=ts_sample_pages_save_project(&pages,&bank,NULL,NULL,path,error,sizeof(error));
    if(!saved)fprintf(stderr,"Diagnostic fixture: %s\n",error);assert(saved);
    ts_sample_free(&sample);
    TsMosaic *root=ts_sample_pages_mosaic_root(&pages);ts_sample_pages_free(&pages);ts_mosaic_free(root);
    ts_instrument_free(&bank);return 0;
}
#include "test_gallery_stress_diagnostic.inc"
int main(int argc,char **argv)
{
    if(argc==3 && !strcmp(argv[1],"--make-fixture"))return live_fixture(argv[2]);
    /* Optional full application integration run, normal device and UI loop.
       Caller supplies an isolated INI with diagnostic_live_test=1. */
    if(argc==3 && !strcmp(argv[1],"--run")) {
        char *args[]={argv[0],argv[2],NULL};
        return tapesister_application_main(2,args);
    }
    SDL_setenv("SDL_VIDEODRIVER","dummy",1);SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    assert(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER));
    SDL_Window *window=SDL_CreateWindow("Live diagnostic",0,0,640,400,0);assert(window);
    assert(ts_profile_init(SDL_GetPerformanceCounter,SDL_GetPerformanceFrequency()));
    ts_ui_init(&u);ts_instrument_init(&bank);
    char error[256];assert(ts_sample_pages_init(&pages,error,sizeof(error)));
    pages.mosaic=ts_mosaic_create();assert(pages.mosaic);
    assert(ts_sample_pages_enable_gallery(&pages,error,sizeof(error)));
    for(int i=1;i<10;++i)assert(ts_gallery_add(pages.gallery,error,sizeof(error)));
    static const char *names[]={"01 KETER","02 CHOKHMAH","03 BINAH"};
    const int counts[]={53,48,43};
    for(int p=0;p<3;++p) {
        snprintf(pages.gallery->canvas[p].name,TS_GALLERY_NAME,"%s",names[p]);
        for(int i=0;i<counts[p];++i)pages.gallery->canvas[p].scene->events[i]=
            (TsMosaicEvent){.id=i+1,.source=&source,.duration=600,.width=50,.gain=1};
    }
    pages.gallery->active=2;
    a.gallery=u.mosaic_gallery=pages.gallery;
    a.mosaic=u.mosaic=pages.mosaic=ts_gallery_active(pages.gallery);mosaic.pages=&pages;
    ts_sister_runtime_init(&a.sister);
    ts_realtime_diagnostics_init(&a.realtime_diagnostics);a.realtime_diagnostics_enabled=1;
    a.output_rate=48000;a.output_device_channels=4;
    ts_matrix_default(&u.config.matrix);u.config.matrix.enabled=1;
    for(int i=0;i<14;++i)ts_matrix_effect_default(&u.config.matrix.effect[i],TS_SISTER_FX_DISTORTION,i+1);
    ts_matrix_effect_default(&u.config.matrix.effect[3],TS_SISTER_FX_GRAIN,4);
    ts_matrix_effect_default(&u.config.matrix.effect[11],TS_MATRIX_FX_SUNVOX,12);
    int spring=-1;
    for(int i=0;i<ts_sunvox_factory_count();++i)if(!strcmp(ts_sunvox_factory_name(i),"Taneb Spring")){spring=i;break;}
    assert(spring>=0 && ts_sunvox_select(&u.config.matrix.effect[11].sunvox,spring));
    snprintf(u.project_path,sizeof(u.project_path),"Tree-of-Life-Live-FX-04.tsr");
    assert(live_test_matches(&u.config.matrix,&pages,u.project_path));
    assert(!live_test_matches(&u.config.matrix,&pages,"unrelated.tsr"));
    pages.gallery->canvas[1].scene->events[0].solo=1;
    assert(!live_test_matches(&u.config.matrix,&pages,u.project_path));
    pages.gallery->canvas[1].scene->events[0].solo=0;
    /* Disabled is inert, including on transport and in the regular poll. */
    live_test_loaded(0,&a,&u,&pages,u.project_path);
    assert(live_test.state==LT_OFF && !live_test.touched && u.mosaic->time==0);
    live_test_transport(&u,1);assert(live_test.state==LT_OFF);
    live_log_reached=SDL_CreateSemaphore(0);live_log_release=SDL_CreateSemaphore(0);
    assert(live_log_reached && live_log_release);atomic_store(&live_log_controlled,1);
    assert(ts_session_log_start(NULL,0));
    assert(!SDL_SemWaitTimeout(live_log_reached,10000));
    a.sister.matrix.controls=u.config.matrix;
    u.config.diagnostic_live_test=1;live_test_loaded(0,&a,&u,&pages,u.project_path);
    assert(live_test.state==LT_PREPARING && live_test_save_blocked() && !u.mosaic->playing);
    assert(live_test.movements==10 && !strcmp(live_test_scenario(),"gallery-live-v1"));
    assert(live_test.duration_ms==8100+3*67000+7*12000);
    for(int i=1;i<live_test.count;++i)assert(live_test.steps[i].ms>live_test.steps[i-1].ms);
    poll_at(window,SDL_GetPerformanceCounter());
    assert(live_test.state==LT_ARMED && pages.gallery->active==0 && !live_test.next);
    /* Play starts a measured run; delayed actions retain the original schedule. */
    ts_profile_enable(1);
    mosaic_transport(0,&a,&u,0);assert(live_test.state==LT_PENDING && u.mosaic->playing && ts_profile_enabled());
    poll_at(window,live_test.started);
    assert(live_test.state==LT_RUNNING && live_test.next==1);
    Uint64 origin=live_test.started;
    unsigned scheduled=live_test.steps[1].ms;
    poll_at(window,origin+(Uint64)((scheduled+25)*live_test.frequency/1000));
    assert(live_test.next==2 && live_test.started==origin && live_test.steps[1].ms==scheduled);
    live_log_drain();
    SDL_Event escape=live_test_key(window,SDLK_ESCAPE,KMOD_NONE);
    assert(live_test_input(&escape,0,&a,&u) && live_test.state==LT_ABORTED && !u.mosaic->playing && ts_profile_enabled());
    mosaic_transport(0,&a,&u,0);assert(live_test.state==LT_ABORTED);mosaic_transport(0,&a,&u,1);
    /* Saving a diagnostic edit must stop before allocating/writing a journal. */
    assert(!project_save_start(0,&a,&u,&pages,&bank,NULL,NULL,"must-not-exist.tsr",error,sizeof(error)));
    assert(strstr(error,"temporary"));
    live_test.state=LT_RUNNING;live_test.generation=a.audio_health_generation;
    ++a.audio_health_generation;
    poll_at(window,SDL_GetPerformanceCounter());
    assert(live_test.state==LT_ABORTED);
    /* The second mode starts Keter at zero through the real Gallery controller.
       It never edits parameters or windows during the measurement. */
    live_log_drain();
    pages.gallery->active=2;a.mosaic=u.mosaic=pages.mosaic=ts_gallery_active(pages.gallery);
    u.config.diagnostic_live_test=2;
    live_test_loaded(0,&a,&u,&pages,u.project_path);
    assert(live_test.state==LT_PREPARING && live_test.duration_ms==608000);
    assert(live_test.count==22 && !strcmp(live_test_scenario(),"tree04-keter-full-v1"));
    for(int i=0;i<live_test.count-1;++i)assert(live_test.steps[i].action==LT_CHECK);
    assert(live_test.steps[live_test.count-1].action==LT_END);
    poll_at(window,SDL_GetPerformanceCounter());
    assert(live_test.state==LT_ARMED && pages.gallery->active==0 && pages.mosaic==u.mosaic && a.mosaic==u.mosaic);
    assert(u.mosaic->time==0 && !u.mosaic->playing && pages.gallery->outgoing<0);
    mosaic_transport(0,&a,&u,0);assert(live_test.state==LT_PENDING && u.mosaic->playing);
    origin=live_test.started;
    for(int step=0;step<live_test.count;++step) {
        unsigned ms=live_test.steps[step].ms;
        poll_at(window,
            origin+(Uint64)ceil(ms*live_test.frequency/1000.0));
        if(live_test.next!=step+1) {
            TsSessionLogStatus failed;ts_session_log_status(&failed);
            fprintf(stderr,"Keter step %d: next=%d state=%d lost=%u/%u status=%s\n",
                step,live_test.next,live_test.state,failed.dropped_audio,failed.dropped_events,u.status);
        }
        assert(live_test.next==step+1);
    }
    assert(live_test.state==LT_DONE && !u.mosaic->playing);
    /* Mode 3 must retain full playback history and log interval profiles without
       changing mode 2 or leaving profiling on/off differently after abort. */
    pages.gallery->active=2;a.mosaic=u.mosaic=pages.mosaic=ts_gallery_active(pages.gallery);
    u.config.diagnostic_live_test=3;ts_profile_enable(0);
    live_test_loaded(0,&a,&u,&pages,u.project_path);
    assert(live_test.state==LT_PREPARING && live_test.count==22);
    assert(!strcmp(live_test_scenario(),"tree04-keter-profile-v1"));
    poll_at(window,SDL_GetPerformanceCounter());
    assert(strstr(u.status,"KETER PROFILE TEST ARMED") && u.mosaic->time==0);
    mosaic_transport(0,&a,&u,0);assert(ts_profile_enabled());
    poll_at(window,live_test.started);
    ts_profile_lane_begin(1);
    uint64_t timer=ts_profile_begin(TS_PROF_AUDIO);SDL_Delay(1);
    ts_profile_end(TS_PROF_AUDIO,timer);ts_profile_lane_end(1);
    poll_at(window,
        live_test.started+30*live_test.frequency);
    assert(live_test.next==2 && u.mosaic->playing && u.mosaic->time==0);
    for(int step=live_test.next;step<live_test.count;++step) {
        poll_at(window,
            live_test.started+(Uint64)ceil(live_test.steps[step].ms*live_test.frequency/1000.0));
        assert(live_test.next==step+1);
    }
    assert(live_test.state==LT_DONE && !u.mosaic->playing && !ts_profile_enabled());
    pages.gallery->active=2;a.mosaic=u.mosaic=pages.mosaic=ts_gallery_active(pages.gallery);
    live_test_loaded(0,&a,&u,&pages,u.project_path);
    poll_at(window,SDL_GetPerformanceCounter());
    mosaic_transport(0,&a,&u,0);assert(ts_profile_enabled());
    live_log_drain();
    assert(live_test_input(&escape,0,&a,&u) && live_test.state==LT_ABORTED && !ts_profile_enabled());
    /* Option 1 accepts arbitrary names, saved mute/solo, disabled Matrix,
       subsecond movements, empty canvases, and the maximum gallery size. */
    for(int c=10;c<TS_GALLERY_LIMIT;++c)assert(ts_gallery_add(pages.gallery,error,sizeof(error)));
    for(int c=0;c<TS_GALLERY_LIMIT;++c) {
        TsMosaic *m=pages.gallery->canvas[c].scene;
        snprintf(pages.gallery->canvas[c].name,TS_GALLERY_NAME,"Custom movement %d",c+1);
        if(c>=3 && c!=7)m->events[0]=(TsMosaicEvent){.id=100+c,.source=&source,.duration=.1+c,.width=50,.gain=1};
        m->playing=0;m->audition_id=0;
        m->markers[0]=(TsMosaicMarker){.stored=1,.time=210}; /* Outside short scenes: skip. */
        m->grid=(TsMosaicGrid){1,0,1};
    }
    pages.gallery->canvas[0].scene->events[0].muted=1;
    pages.gallery->canvas[1].scene->events[0].solo=1;
    pages.gallery->canvas[2].scene->speed=.75;
    u.config.matrix.enabled=a.sister.matrix.controls.enabled=0;
    snprintf(u.project_path,sizeof(u.project_path),"Renamed Project With Spaces.tsr");
    u.config.diagnostic_live_test=1;live_test_loaded(0,&a,&u,&pages,u.project_path);
    assert(live_test.gallery_test && live_test.state==LT_PREPARING && live_test.movements==TS_GALLERY_LIMIT);
    assert(live_test.movement_markers[0]==1 && live_test.movement_markers[3]==0);
    assert(live_test.movement_cards[7]==0 && pages.gallery->canvas[2].scene->speed==.75);
    int visited[TS_GALLERY_LIMIT]={0},inventories=0;
    for(int i=0;i<live_test.count;++i) {
        LiveTestStep step=live_test.steps[i];
        if(i)assert(step.ms>live_test.steps[i-1].ms);
        if(step.action==LT_GALLERY)visited[step.target]=1;
        if(step.action==LT_INVENTORY)++inventories;
        assert(step.action!=LT_MIX && step.action!=LT_MUTE && step.action!=LT_SOLO && step.action!=LT_BYPASS);
    }
    assert(inventories==TS_GALLERY_LIMIT && live_test.count<1024);
    for(int c=0;c<TS_GALLERY_LIMIT;++c)assert(visited[c]);
    poll_at(window,SDL_GetPerformanceCounter());
    assert(live_test.state==LT_ARMED && pages.gallery->active==0 && u.mosaic->time==0);
    mosaic_transport(0,&a,&u,0);assert(ts_profile_enabled());
    for(int step=0;step<live_test.count;++step) {
        poll_at(window,live_test.started+(Uint64)ceil(live_test.steps[step].ms*live_test.frequency/1000.0));
        if(live_test.next!=step+1 || live_test.state==LT_ABORTED)
            fprintf(stderr,"Gallery step %d action=%s: next=%d state=%d status=%s\n",step,
                live_test_action_name(live_test.steps[step].action),live_test.next,live_test.state,u.status);
        assert(live_test.next==step+1 && live_test.state!=LT_ABORTED);
    }
    assert(live_test.state==LT_DONE && !u.mosaic->playing && !ts_profile_enabled());
    assert(pages.gallery->canvas[0].scene->events[0].muted && pages.gallery->canvas[1].scene->events[0].solo);
    assert(!a.sister.matrix.controls.enabled && pages.gallery->canvas[2].scene->speed==.75);
    test_gallery_stress_diagnostic(window);
    /* Loading another project discards the old schedule and save guard, even
       when diagnostics are switched off or the next Gallery is unavailable. */
    u.config.diagnostic_live_test=0;live_test_loaded(0,&a,&u,&pages,"another.tsr");
    assert(!live_test_save_blocked() && live_test.state==LT_OFF && live_test.count==0);
    u.config.diagnostic_live_test=1;
    TsSamplePages empty={0};live_test_loaded(0,&a,&u,&empty,"empty.tsr");
    assert(live_test.state==LT_OFF && !live_test_save_blocked());
    live_test_loaded(0,&a,&u,&pages,u.project_path);
    poll_at(window,SDL_GetPerformanceCounter());
    live_log_drain();
    TsSessionLogStatus before_loss;ts_session_log_status(&before_loss);
    assert(!before_loss.dropped_audio && !before_loss.dropped_events);
    /* A real dropped record must still abort: deliberately overflow the same
       bounded queue while the writer is parked, then run the next checkpoint. */
    live_test.state=LT_ARMED;mosaic_transport(0,&a,&u,0);
    poll_at(window,live_test.started);
    assert(live_test.state==LT_RUNNING);
    live_log_drain();
    for(int i=0;i<EVENT_CAP+1;++i)ts_session_log_event("forced_overflow","index=%d",i);
    TsSessionLogStatus overflow;ts_session_log_status(&overflow);
    assert(overflow.dropped_events==1);
    poll_at(window,live_test.started+(Uint64)ceil(live_test.steps[live_test.next].ms*live_test.frequency/1000.0));
    assert(live_test.state==LT_ABORTED && !u.mosaic->playing && !ts_profile_enabled());
    atomic_store(&live_log_controlled,0);assert(!SDL_SemPost(live_log_release));
    ts_session_log_stop();
    SDL_DestroySemaphore(live_log_reached);SDL_DestroySemaphore(live_log_release);
    TsSessionLogStatus log;ts_session_log_status(&log);
    assert(log.export_result==2 && log.export_serial>=1);
    FILE *saved=fopen(log.export_path,"rb");assert(saved);
    char line[1024];int found_abort=0,found_start=0,found_keter=0,found_complete=0,found_profile=0,found_interval=0,found_incomplete=0;
    while(fgets(line,sizeof(line),saved)) {
        found_incomplete|=strstr(line,"reason=incomplete_telemetry")!=NULL;
        found_profile|=strstr(line,"scenario=tree04-keter-profile-v1")!=NULL;
        found_interval|=strstr(line,"\"type\":\"live_test_profile\"")!=NULL;
        found_abort|=strstr(line,"\"type\":\"live_test_abort\"")!=NULL;
        found_start|=strstr(line,"\"type\":\"live_test_start\"")!=NULL;
        found_keter|=strstr(line,"scenario=tree04-keter-full-v1")!=NULL;
        found_complete|=strstr(line,"\"type\":\"live_test_complete\"")!=NULL;
    }
    fclose(saved);assert(found_abort && found_start && found_keter && found_complete && found_profile && found_interval && found_incomplete);
    TsMosaic *root=ts_sample_pages_mosaic_root(&pages);ts_sample_pages_free(&pages);ts_mosaic_free(root);
    ts_sister_runtime_free(&a.sister);ts_instrument_free(&bank);
    SDL_DestroyWindow(window);SDL_Quit();
    puts("Live diagnostic gating, schedule, real mute/solo, abort and save protection passed");
    return 0;
}
