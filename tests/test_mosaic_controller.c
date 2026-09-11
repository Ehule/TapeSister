#ifdef NDEBUG
#undef NDEBUG
#endif
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#define main tapesister_application_main
#include "../src/main_sdl.c"
#undef main
#include <assert.h>

static TsInstrument instrument;
static TsUiState ui;
static AudioState audio;
static SisterWindow sister;
static MosaicController mosaic;
static PortalController portal;
static TransformController transform;

static void click(SDL_Window *window,int x,int y,int clicks)
{
    SDL_Event event={0};event.type=SDL_MOUSEBUTTONDOWN;event.button.windowID=SDL_GetWindowID(window);
    event.button.button=SDL_BUTTON_LEFT;event.button.x=x;event.button.y=y;event.button.clicks=(Uint8)clicks;
    (void)mosaic_event(&event,window,0,&audio,&ui,&instrument,&mosaic,&portal,&sister,&transform,48000);
}
static void test_canvas_gestures(SDL_Window *window)
{
    ui.mosaic_open=1;ui.mosaic_scroll=ui.mosaic_xscroll=0;ui.mosaic_scale=24;ui.mosaic_hscale=1;
    click(window,20,85,1);assert(mosaic.drag==3);
    SDL_Event event={0};event.type=SDL_MOUSEBUTTONUP;event.button.windowID=SDL_GetWindowID(window);
    event.button.button=SDL_BUTTON_LEFT;event.button.x=430;event.button.y=100;
    assert(mosaic_event(&event,window,0,&audio,&ui,&instrument,&mosaic,&portal,&sister,&transform,48000));
    TsMosaicEvent *e=ts_mosaic_find(ui.mosaic,ui.mosaic_selected);assert(e);
    uint64_t id=e->id;double start=e->start;
    assert(fabs(start-34.0/24)<1e-9);
    click(window,450,109,1);assert(mosaic.drag==1);
    event.type=SDL_MOUSEMOTION;event.motion.windowID=SDL_GetWindowID(window);event.motion.x=460;event.motion.y=145;
    assert(mosaic_event(&event,window,0,&audio,&ui,&instrument,&mosaic,&portal,&sister,&transform,48000));
    assert(fabs(e->start-start-36.0/24)<1e-9);
    event.type=SDL_MOUSEBUTTONUP;event.button.windowID=SDL_GetWindowID(window);event.button.x=460;event.button.y=145;
    assert(mosaic_event(&event,window,0,&audio,&ui,&instrument,&mosaic,&portal,&sister,&transform,48000));
    event.type=SDL_KEYDOWN;event.key.windowID=SDL_GetWindowID(window);event.key.keysym.mod=KMOD_CTRL;event.key.keysym.sym=SDLK_c;
    assert(mosaic_event(&event,window,0,&audio,&ui,&instrument,&mosaic,&portal,&sister,&transform,48000));
    TsMosaicSource *source=e->source;assert(source->pins==1);
    event.key.keysym.mod=0;event.key.keysym.sym=SDLK_DELETE;
    assert(mosaic_event(&event,window,0,&audio,&ui,&instrument,&mosaic,&portal,&sister,&transform,48000));
    assert(!ts_mosaic_find(ui.mosaic,id));
    event.key.keysym.mod=KMOD_CTRL;event.key.keysym.sym=SDLK_v;
    assert(mosaic_event(&event,window,0,&audio,&ui,&instrument,&mosaic,&portal,&sister,&transform,48000));
    e=ts_mosaic_find(ui.mosaic,ui.mosaic_selected);assert(e && e->id!=id && e->source==source);
    ts_mosaic_delete(ui.mosaic,e->id);
}
static void wait_render(void)
{
    Uint32 start=SDL_GetTicks();
    while(portal.worker && SDL_GetTicks()-start<15000){portal_poll(0,&audio,&ui,&instrument,&portal);SDL_Delay(1);}
    assert(!portal.worker && !ui.portal.busy);
}
static void test_record_file(SDL_Window *window)
{
    char folder[160],path[1200],error[160];
    snprintf(folder,sizeof(folder),"mosaic-record-%lu",(unsigned long)SDL_GetTicks());
    SDL_setenv("TAPESISTER_CAPTURES",folder,1);
    ts_audio_mixer_init(&audio.mixer);ts_performance_recorder_init(&sister.performance_recorder);
    audio.sister_file_recorder=&sister.performance_recorder;
    ui.mosaic_open=1;ts_mosaic_seek(ui.mosaic,0);ui.mosaic->playing=1;
    click(window,240,45,1);assert(ui.file_record_state==TS_PERFORMANCE_FILE_RECORDING);
    snprintf(path,sizeof(path),"%s",sister.performance_recorder.path);
    float output[4096];
    for(int i=0;i<8;++i)audio_callback(&audio,(Uint8 *)(output+i*512),512*sizeof(float));
    poll_file_capture_ui(&ui,&sister);assert(ui.file_record_frames==2048);
    /* The same recorder survives opening an event editor. */
    assert(mosaic_enter(0,&audio,&ui,&instrument,&mosaic,ui.mosaic->events[0].id));
    assert(ui.file_record_state==TS_PERFORMANCE_FILE_RECORDING && ui.mosaic->playing);
    main_file_capture_toggle(&audio,&ui,&sister,48000);
    Uint32 start=SDL_GetTicks();
    while(ts_performance_recorder_state(&sister.performance_recorder)==TS_PERFORMANCE_FILE_STOPPING && SDL_GetTicks()-start<5000)SDL_Delay(1);
    poll_file_capture_ui(&ui,&sister);assert(ui.file_record_state==TS_PERFORMANCE_FILE_IDLE);
    TsSample saved={0};assert(ts_sample_load_wav(&saved,path,error,sizeof(error)));
    assert(saved.channels==2 && saved.frames==2048);double energy=0;
    for(int i=0;i<4096;++i){assert(fabsf(saved.data[i]-output[i])<1e-6f);energy+=fabs(output[i]);}
    assert(energy>0);ts_sample_free(&saved);remove(path);
#ifdef _WIN32
    _rmdir(folder);
#else
    rmdir(folder);
#endif
    SDL_setenv("TAPESISTER_CAPTURES","",1);mosaic_leave(0,&audio,&ui,&instrument,&mosaic);
    ts_performance_recorder_free(&sister.performance_recorder);audio.sister_file_recorder=NULL;
}
static void test_async_ownership(uint64_t a,uint64_t b,uint64_t original)
{
    const char *bin=getenv("TS_TEST_CDP_BIN");if(!bin){puts("Set TS_TEST_CDP_BIN for native CDP ownership checks");return;}
    snprintf(ui.config.cdp_bin_path,sizeof(ui.config.cdp_bin_path),"%s",bin);
    assert(mosaic_enter(0,&audio,&ui,&instrument,&mosaic,a));ui.portal.open=1;
    assert(portal_source(0,&audio,&ui,&instrument,&portal));
    assert(portal.source_event==a);
    ts_portal_recipe_default(&ui.portal.recipe,ts_portal_process_find("modify.speed.1"));ui.portal.recipe.values[0]=.5;
    portal_preview(0,&audio,&ui,&portal);assert(portal.worker);portal.quick_apply=1;
    portal_close(0,&audio,&ui,&portal);assert(!SDL_AtomicGet(&portal.worker->cancel));
    mosaic_leave(0,&audio,&ui,&instrument,&mosaic);
    assert(mosaic_enter(0,&audio,&ui,&instrument,&mosaic,b));
    uint64_t other=ts_sample_hash(&instrument.current);wait_render();
    assert(strstr(ui.portal.message,"APPLIED TO EVENT"));
    assert(ts_sample_hash(&instrument.current)==other);
    assert(ts_mosaic_find(ui.mosaic,b)->source->hash==original);
    assert(ts_mosaic_find(ui.mosaic,a)->source!=ts_mosaic_find(ui.mosaic,b)->source);
    assert(ts_sample_hash(&mosaic.bank->current)==original);

    /* Same ID, edited document: preserve the result instead of overwriting. */
    mosaic_leave(0,&audio,&ui,&instrument,&mosaic);
    assert(mosaic_enter(0,&audio,&ui,&instrument,&mosaic,a));ui.portal.open=1;
    assert(portal_source(0,&audio,&ui,&instrument,&portal));
    portal_preview(0,&audio,&ui,&portal);assert(portal.worker);portal.quick_apply=1;
    instrument.has_loop=1;instrument.loop_first=100;instrument.loop_last=instrument.current.frames-100;
    mosaic_commit(0,&ui,&instrument,&mosaic);wait_render();
    assert(ui.portal.valid && strstr(ui.portal.message,"EVENT CHANGED"));
    ts_mosaic_delete(ui.mosaic,a);
    assert(!portal_apply(0,&audio,&ui,&instrument,&portal,0));
    assert(ui.portal.valid && strstr(ui.portal.message,"EVENT REMOVED"));
    /* Restore the deleted definition before returning its open document. */
    TsMosaicEvent *restored=ts_mosaic_add(ui.mosaic,ts_mosaic_find(ui.mosaic,b)->source,0,0);assert(restored);restored->id=a;
    portal_close(0,&audio,&ui,&portal);mosaic_leave(0,&audio,&ui,&instrument,&mosaic);
}
int main(void)
{
    SDL_SetMainReady();SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    SDL_setenv("SDL_VIDEODRIVER","dummy",1);assert(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER|SDL_INIT_AUDIO)==0);
    SDL_AudioSpec desired={0};desired.freq=48000;desired.format=AUDIO_F32SYS;desired.channels=2;desired.samples=256;
    SDL_AudioDeviceID device=SDL_OpenAudioDevice(NULL,0,&desired,NULL,0);assert(device);ts_real_output=device;
    SDL_Window *window=SDL_CreateWindow("Mosaic test",0,0,640,400,SDL_WINDOW_HIDDEN);assert(window);
    char error[160];ts_instrument_init(&instrument);ts_ui_init(&ui);
    ts_note_bank_init(&audio.notes);ts_performance_init(&audio.performance);ts_performance_init(&audio.tile_launchers);
    ts_sister_runtime_init(&audio.sister);ts_capture_init(&audio.capture);audio.output_rate=48000;audio.bank_slot=-1;
    transform_controller_init(&transform);portal_init(&portal,&ui.portal);portal.mosaic=&mosaic;
    assert(ts_instrument_generate(&instrument,TS_GENERATOR_METALLIC,777,error,sizeof(error)));
    uint64_t original=ts_sample_hash(&instrument.current);
    ui.mosaic=audio.mosaic=ts_mosaic_create();ui.mosaic_scale=24;ui.mosaic_open=1;
    TsMosaicSource *source=ts_mosaic_source(ui.mosaic,&instrument.current,error,sizeof(error));assert(source);
    TsMosaicEvent *a=ts_mosaic_add(ui.mosaic,source,0,0);a->duration=6;
    TsMosaicEvent *b=ts_mosaic_copy(ui.mosaic,a->id,.25,105);b->duration=9;
    uint64_t aid=a->id,bid=b->id;
    ui.mosaic->playing=1;
    TsAudioBuses buses;TsStereoFrame capture;
    for(int i=0;i<24000;++i){ts_audio_buses_clear(&buses);runtime_note_render(&audio,&buses,&capture);}
    double before=ui.mosaic->time;
    assert(mosaic_enter(0,&audio,&ui,&instrument,&mosaic,aid));
    assert(ui.mosaic->playing && ui.mosaic->time==before);
    click(window,200,370,1);mosaic_poll(0,&ui,&instrument,&mosaic);
    assert(a->note_count==2 && ui.staged_notes);
    click(window,570,320,1);assert(!a->looping && b->looping);
    mosaic_leave(0,&audio,&ui,&instrument,&mosaic);assert(ts_sample_hash(&instrument.current)==original);
    assert(ui.mosaic->playing);
    test_async_ownership(aid,bid,original);
    test_record_file(window);
    test_canvas_gestures(window);

    /* Route a single render to the direct bus and Sister input without
       advancing its clock twice. Different left/right data stays stereo. */
    ts_mosaic_seek(ui.mosaic,.5);ui.mosaic->playing=1;
    ts_audio_buses_clear(&buses);before=ui.mosaic->time;
    runtime_note_render(&audio,&buses,&capture);
    assert(fabs(ui.mosaic->time-before-1.0/48000)<1e-10);
    assert(buses.tile_performance.l==audio.tile_launcher_mix.l);
    assert(buses.tile_performance.r==audio.tile_launcher_mix.r);
    for(int i=0;i<300;++i){ts_audio_buses_clear(&buses);runtime_note_render(&audio,&buses,&capture);}
    assert(fabsf(buses.tile_performance.l)+fabsf(buses.tile_performance.r)>0);

    /* A reproducible scene rendered by the actual application UI. */
    const char *shot=getenv("TS_MOSAIC_SCREENSHOT");
    if(shot) {
        TsMosaic *scene=ts_mosaic_create();TsInstrument *bank=malloc(sizeof(*bank));assert(scene && bank);ts_instrument_init(bank);
        const char *names[]={"METAL BLOOM","DUST PULSE","WARM DRIFT","GLASS CHOIR"};
        TsMosaicSource *sources[4];unsigned colors=0;
        TsInstrument *generator=malloc(sizeof(*generator));assert(generator);ts_instrument_init(generator);
        for(int i=0;i<4;++i) {
            for(int trial=0;trial<30;++trial) {
                assert(ts_instrument_generate(generator,(TsGeneratorKind)i,(uint32_t)(900+i*50+trial),error,sizeof(error)));
                uint64_t hash=ts_sample_hash(&generator->current);if(!(colors&(1u<<(hash%5))) || trial==29){colors|=1u<<(hash%5);break;}
            }
            assert(ts_instrument_select_bank(bank,i,error,sizeof(error)));
            assert(ts_instrument_import_sample(bank,&generator->current,0,0,0,TS_LOOP_FORWARD,error,sizeof(error)));
            snprintf(bank->current.name,sizeof(bank->current.name),"%s",names[i]);
            sources[i]=ts_mosaic_source(scene,&bank->current,error,sizeof(error));assert(sources[i]);
        }
        ts_instrument_free(generator);free(generator);
        TsMosaicEvent *events[6];
        events[0]=ts_mosaic_add(scene,sources[0],0,0);events[0]->duration=8.2;
        events[1]=ts_mosaic_add(scene,sources[1],.7,106);events[1]->duration=2.4;events[1]->looping=0;
        events[2]=ts_mosaic_add(scene,sources[2],3.9,106);events[2]->duration=5.3;
        events[3]=ts_mosaic_add(scene,sources[3],2.1,214);events[3]->duration=8.5;
        events[4]=ts_mosaic_add(scene,sources[1],9.1,0);events[4]->duration=2.3;events[4]->looping=0;
        events[5]=ts_mosaic_add(scene,sources[2],10.1,320);events[5]->duration=1.6;
        events[0]->note_count=3;events[0]->notes[1]=64;events[0]->notes[2]=67;
        events[3]->note_count=2;events[3]->notes[1]=72;
        TsMosaic *saved=ui.mosaic;ui.mosaic=scene;
        ui.mosaic_open=1;ui.mosaic_scale=24;ui.mosaic_hscale=1;ui.mosaic_time=4.35;ui.mosaic_playing=1;ui.mosaic_selected=events[3]->id;
        snprintf(ui.status,sizeof(ui.status),"FREE PLACEMENT / SHARED SOURCE AUDIO / INDEPENDENT LOOP CLOCKS");
        static TsFramebuffer fb;ts_ui_render(&fb,&ui,bank);assert(ts_ui_write_ppm(&fb,shot));
        ui.mosaic=saved;ts_mosaic_free(scene);ts_instrument_free(bank);free(bank);
    }
    portal_free(&portal);mosaic_controller_free(&mosaic);ts_mosaic_free(ui.mosaic);
    ts_performance_free(&audio.performance);ts_performance_free(&audio.tile_launchers);ts_sister_runtime_free(&audio.sister);
    ts_capture_free(&audio.capture);ts_instrument_free(&instrument);SDL_DestroyWindow(window);
    SDL_CloseAudioDevice(device);ts_real_output=0;SDL_Quit();
    puts("Mosaic editor, background CDP ownership, shared routing and exact output WAV capture passed");return 0;
}
