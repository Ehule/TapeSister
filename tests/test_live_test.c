#ifdef NDEBUG
#undef NDEBUG
#endif
#define SDL_MAIN_HANDLED
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
int main(int argc,char **argv)
{
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
    assert(ts_session_log_start(NULL,0));SDL_Delay(30);
    a.sister.matrix.controls=u.config.matrix;
    u.config.diagnostic_live_test=1;live_test_loaded(0,&a,&u,&pages,u.project_path);
    assert(live_test.state==LT_ARMED && live_test_save_blocked() && !u.mosaic->playing && u.mosaic->time==300);
    assert(live_test.count==240 && !strcmp(live_test_scenario(),"tree04-live-v1") && live_test.steps[live_test.count-1].ms==216000);
    for(int i=1;i<live_test.count;++i)assert(live_test.steps[i].ms>live_test.steps[i-1].ms);
    live_test_poll(window,0,&a,&u,&pages,&bank,&mosaic,&portal,&sister,&transform,SDL_GetPerformanceCounter());
    assert(live_test.state==LT_ARMED && !live_test.next);
    /* A press of the real transport arms the monotonic schedule. */
    ts_profile_enable(1);
    mosaic_transport(0,&a,&u,0);assert(live_test.state==LT_PENDING && u.mosaic->playing && !ts_profile_enabled());
    live_test_poll(window,0,&a,&u,&pages,&bank,&mosaic,&portal,&sister,&transform,live_test.started);
    assert(live_test.state==LT_RUNNING && live_test.next==1);
    Uint64 origin=live_test.started;
    unsigned scheduled=live_test.steps[1].ms;
    live_test_poll(window,0,&a,&u,&pages,&bank,&mosaic,&portal,&sister,&transform,
        origin+(Uint64)((scheduled+25)*live_test.frequency/1000));
    assert(live_test.next==2 && live_test.started==origin && live_test.steps[1].ms==scheduled);
    mosaic_sources_refresh(&u,&bank,&mosaic);
    LiveTestStep mute={0,LT_MUTE,0,1},solo={0,LT_SOLO,0,1};
    assert(live_test_action(&mute,window,0,&a,&u,&pages,&bank,&mosaic,&portal,&sister,&transform));
    assert(ts_mosaic_find(u.mosaic,live_test.card[2])->muted);
    mute.value=0;assert(live_test_action(&mute,window,0,&a,&u,&pages,&bank,&mosaic,&portal,&sister,&transform));
    assert(live_test_action(&solo,window,0,&a,&u,&pages,&bank,&mosaic,&portal,&sister,&transform));
    solo.value=0;assert(live_test_action(&solo,window,0,&a,&u,&pages,&bank,&mosaic,&portal,&sister,&transform));
    SDL_Event escape=live_test_key(window,SDLK_ESCAPE,KMOD_NONE);
    assert(live_test_input(&escape,0,&a,&u) && live_test.state==LT_ABORTED && !u.mosaic->playing && ts_profile_enabled());
    mosaic_transport(0,&a,&u,0);assert(live_test.state==LT_ABORTED);mosaic_transport(0,&a,&u,1);
    /* Saving a diagnostic edit must stop before allocating/writing a journal. */
    assert(!project_save_start(0,&a,&u,&pages,&bank,NULL,NULL,"must-not-exist.tsr",error,sizeof(error)));
    assert(strstr(error,"temporary"));
    live_test.state=LT_RUNNING;live_test.generation=a.audio_health_generation;
    ++a.audio_health_generation;
    live_test_poll(window,0,&a,&u,&pages,&bank,&mosaic,&portal,&sister,&transform,SDL_GetPerformanceCounter());
    assert(live_test.state==LT_ABORTED);
    /* The second mode starts Keter at zero through the real Gallery controller.
       It never edits parameters or windows during the measurement. */
    u.config.diagnostic_live_test=2;
    live_test_loaded(0,&a,&u,&pages,u.project_path);
    assert(live_test.state==LT_PREPARING && live_test.duration_ms==608000);
    assert(live_test.count==22 && !strcmp(live_test_scenario(),"tree04-keter-full-v1"));
    for(int i=0;i<live_test.count-1;++i)assert(live_test.steps[i].action==LT_CHECK);
    assert(live_test.steps[live_test.count-1].action==LT_END);
    live_test_poll(window,0,&a,&u,&pages,&bank,&mosaic,&portal,&sister,&transform,SDL_GetPerformanceCounter());
    assert(live_test.state==LT_ARMED && pages.gallery->active==0 && pages.mosaic==u.mosaic && a.mosaic==u.mosaic);
    assert(u.mosaic->time==0 && !u.mosaic->playing && pages.gallery->outgoing<0);
    mosaic_transport(0,&a,&u,0);assert(live_test.state==LT_PENDING && u.mosaic->playing);
    origin=live_test.started;
    for(int step=0;step<live_test.count;++step) {
        unsigned ms=live_test.steps[step].ms;
        /* Virtual time jumps 30 seconds, but the real log writer still wakes
           every 25 ms. Let it drain between steps; do not defeat the production
           dropped-record abort by flooding its 32-event queue in this test. */
        SDL_Delay(50);
        live_test_poll(window,0,&a,&u,&pages,&bank,&mosaic,&portal,&sister,&transform,
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
    live_test_poll(window,0,&a,&u,&pages,&bank,&mosaic,&portal,&sister,&transform,SDL_GetPerformanceCounter());
    assert(strstr(u.status,"KETER PROFILE TEST ARMED") && u.mosaic->time==0);
    mosaic_transport(0,&a,&u,0);assert(ts_profile_enabled());
    live_test_poll(window,0,&a,&u,&pages,&bank,&mosaic,&portal,&sister,&transform,live_test.started);
    ts_profile_lane_begin(1);
    uint64_t timer=ts_profile_begin(TS_PROF_AUDIO);SDL_Delay(1);
    ts_profile_end(TS_PROF_AUDIO,timer);ts_profile_lane_end(1);
    SDL_Delay(50);
    live_test_poll(window,0,&a,&u,&pages,&bank,&mosaic,&portal,&sister,&transform,
        live_test.started+30*live_test.frequency);
    assert(live_test.next==2 && u.mosaic->playing && u.mosaic->time==0);
    for(int step=live_test.next;step<live_test.count;++step) {
        SDL_Delay(50);
        live_test_poll(window,0,&a,&u,&pages,&bank,&mosaic,&portal,&sister,&transform,
            live_test.started+(Uint64)ceil(live_test.steps[step].ms*live_test.frequency/1000.0));
        assert(live_test.next==step+1);
    }
    assert(live_test.state==LT_DONE && !u.mosaic->playing && !ts_profile_enabled());
    pages.gallery->active=2;a.mosaic=u.mosaic=pages.mosaic=ts_gallery_active(pages.gallery);
    live_test_loaded(0,&a,&u,&pages,u.project_path);
    live_test_poll(window,0,&a,&u,&pages,&bank,&mosaic,&portal,&sister,&transform,SDL_GetPerformanceCounter());
    mosaic_transport(0,&a,&u,0);assert(ts_profile_enabled());
    SDL_Delay(50);
    assert(live_test_input(&escape,0,&a,&u) && live_test.state==LT_ABORTED && !ts_profile_enabled());
    /* The new composition uses option 1, ten canvases, interval profiling,
       and the real Space/Escape/seek controllers, while legacy schedules stay fixed. */
    static const char *angelic_names[]={"KETER","CHOKHMAH","BINAH","CHESED","GEVURAH","TIFERET","NETZACH","HOD","YESOD","MALKHUT"};
    for(int c=0;c<10;++c) {
        TsMosaic *m=pages.gallery->canvas[c].scene;
        snprintf(pages.gallery->canvas[c].name,TS_GALLERY_NAME,"%s",angelic_names[c]);
        if(c>=3)m->events[0]=(TsMosaicEvent){.id=100+c,.source=&source,.duration=600,.width=50,.gain=1};
        m->playing=0;m->audition_id=0;m->markers[0]=(TsMosaicMarker){.stored=1,.time=210};
    }
    snprintf(u.project_path,sizeof(u.project_path),"Tree-of-Life-Angelic-Noise.tsr");
    assert(live_test_angelic_matches(&u.config.matrix,&pages,u.project_path));
    pages.gallery->canvas[9].scene->events[0].solo=1;
    assert(!live_test_angelic_matches(&u.config.matrix,&pages,u.project_path));
    pages.gallery->canvas[9].scene->events[0].solo=0;
    u.config.diagnostic_live_test=1;live_test_loaded(0,&a,&u,&pages,u.project_path);
    assert(live_test.angelic && live_test.state==LT_PREPARING && live_test.duration_ms==792100);
    assert(!strcmp(live_test_scenario(),"angelic-noise-live-v1") && live_test.count>500);
    int visited[10]={1},actions[LT_ASSERT+1]={0};
    for(int i=0;i<live_test.count;++i){LiveTestStep step=live_test.steps[i];++actions[step.action];
        if(i)assert(step.ms>live_test.steps[i-1].ms);
        if(step.action==LT_GALLERY)visited[step.target]=1;
    }
    for(int c=0;c<10;++c)assert(visited[c]);
    assert(actions[LT_ESCAPE]>=30 && actions[LT_SPACE]>=30 && actions[LT_ASSERT]>=60);
    live_test_poll(window,0,&a,&u,&pages,&bank,&mosaic,&portal,&sister,&transform,SDL_GetPerformanceCounter());
    assert(live_test.state==LT_ARMED && pages.gallery->active==0 && u.mosaic->time==0);
    mosaic_transport(0,&a,&u,0);assert(ts_profile_enabled());
    LiveTestStep steps[]={ {0,LT_SPACE,0,0},{0,LT_ASSERT,1,0},{0,LT_ESCAPE,0,0},{0,LT_ASSERT,2,0},
        {0,LT_ESCAPE,1,0},{0,LT_ASSERT,3,0},{0,LT_SPACE,1,0},{0,LT_SEEK,0,.5f},{0,LT_SEEK,1,.35f} };
    for(size_t i=0;i<sizeof(steps)/sizeof(*steps);++i){SDL_Delay(30);assert(live_test_action(&steps[i],window,0,&a,&u,&pages,&bank,&mosaic,&portal,&sister,&transform));}
    assert(u.mosaic->time==210 && !a.engine_stopped && u.mosaic->playing);
    assert(live_test_input(&escape,0,&a,&u) && live_test.state==LT_ABORTED && !ts_profile_enabled());
    ts_session_log_stop();
    TsSessionLogStatus log;ts_session_log_status(&log);
    assert(log.export_result==2 && log.export_serial>=1);
    FILE *saved=fopen(log.export_path,"rb");assert(saved);
    char line[1024];int found_abort=0,found_start=0,found_keter=0,found_complete=0,found_profile=0,found_interval=0;
    while(fgets(line,sizeof(line),saved)) {
        found_profile|=strstr(line,"scenario=tree04-keter-profile-v1")!=NULL;
        found_interval|=strstr(line,"\"type\":\"live_test_profile\"")!=NULL;
        found_abort|=strstr(line,"\"type\":\"live_test_abort\"")!=NULL;
        found_start|=strstr(line,"\"type\":\"live_test_start\"")!=NULL;
        found_keter|=strstr(line,"scenario=tree04-keter-full-v1")!=NULL;
        found_complete|=strstr(line,"\"type\":\"live_test_complete\"")!=NULL;
    }
    fclose(saved);assert(found_abort && found_start && found_keter && found_complete && found_profile && found_interval);
    TsMosaic *root=ts_sample_pages_mosaic_root(&pages);ts_sample_pages_free(&pages);ts_mosaic_free(root);
    ts_sister_runtime_free(&a.sister);ts_instrument_free(&bank);
    SDL_DestroyWindow(window);SDL_Quit();
    puts("Live diagnostic gating, schedule, real mute/solo, abort and save protection passed");
    return 0;
}
