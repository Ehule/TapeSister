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
static TsSample workspace_preview;

static int dispatch(const SDL_Event *event,SDL_Window *window)
{
    if(main_file_capture_event(event,window,&audio,&ui,&sister,48000))return 1;
    if(workspace_event(event,window,0,&audio,&ui,&instrument,&mosaic,&portal,&sister,&transform,&workspace_preview))return 1;
    if(mosaic_event(event,window,0,&audio,&ui,&instrument,&mosaic,&portal,&sister,&transform,48000))return 1;
    return main_file_capture_event(event,window,&audio,&ui,&sister,48000);
}

static void click(SDL_Window *window,int x,int y,int clicks)
{
    SDL_Event event={0};event.type=SDL_MOUSEBUTTONDOWN;event.button.windowID=SDL_GetWindowID(window);
    event.button.button=SDL_BUTTON_LEFT;event.button.x=x;event.button.y=y;event.button.clicks=(Uint8)clicks;
    (void)dispatch(&event,window);
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
static void motion(SDL_Window *window,int x,int y)
{
    SDL_Event e={0};e.type=SDL_MOUSEMOTION;e.motion.windowID=SDL_GetWindowID(window);e.motion.x=x;e.motion.y=y;
    dispatch(&e,window);
}
static void release(SDL_Window *window,int x,int y)
{
    SDL_Event e={0};e.type=SDL_MOUSEBUTTONUP;e.button.windowID=SDL_GetWindowID(window);
    e.button.button=SDL_BUTTON_LEFT;e.button.x=x;e.button.y=y;
    mosaic_event(&e,window,0,&audio,&ui,&instrument,&mosaic,&portal,&sister,&transform,48000);
}
static void key(SDL_Window *window,SDL_Keycode key,SDL_Keymod mod)
{
    SDL_Event e={0};e.type=SDL_KEYDOWN;e.key.windowID=SDL_GetWindowID(window);e.key.keysym.sym=key;e.key.keysym.mod=mod;
    dispatch(&e,window);
}
static void wait_render(void);
#include "test_mosaic_native.inc"
#include "test_mosaic_arranging.inc"
#include "test_mosaic_routing.inc"
static void test_canvas_feedback(SDL_Window *window)
{
    TsMosaic *saved=ui.mosaic,*scene=ts_mosaic_create();assert(scene);ui.mosaic=audio.mosaic=scene;
    char error[160];TsMosaicSource *source=ts_mosaic_source(scene,&instrument.current,error,sizeof(error));assert(source);
    TsMosaicEvent *a=ts_mosaic_add(scene,source,.5,20);a->duration=2;
    TsMosaicEvent *b=ts_mosaic_add(scene,source,1,130);b->duration=3;
    TsMosaicEvent *obstacle=ts_mosaic_add(scene,source,1,250);obstacle->duration=3;
    ui.mosaic_open=1;ui.mosaic_scale=24;ui.mosaic_hscale=1;ui.mosaic_scroll=ui.mosaic_xscroll=0;
    mosaic_poll(0,&ui,&instrument,&mosaic);assert(ui.mosaic_source_count==17 && ui.mosaic_source_pages==2);
    /* Source click, waveform preview and drag cancellation keep the pin safe. */
    click(window,20,85,1);assert(ui.mosaic_source_selected==0 && source->pins==1);
    motion(window,530,190);assert(ui.mosaic_ghost_count==1 && ui.mosaic_ghosts[0].source==source);
    release(window,110,190);assert(!ui.mosaic_ghost_count && !source->pins);
    SDL_SetModState(KMOD_SHIFT);click(window,155,70,1);assert(ui.mosaic_box);
    motion(window,385,180);release(window,385,180);SDL_SetModState(KMOD_NONE);
    assert(mosaic_chosen(&ui,0) && mosaic_chosen(&ui,1) && !mosaic_chosen(&ui,2));
    double gap=b->x-a->x,spacing=b->start-a->start,other_time=obstacle->start;
    click(window,180,96,1);assert(ui.mosaic_ghost_count==2 && mosaic.drag==1);
    motion(window,220,120);assert(ui.mosaic_ghosts[0].start==.5);
    assert(fabs(b->x-a->x-gap)<1e-9 && fabs(b->start-a->start-spacing)<1e-9);
    assert(a->start==1.5 && b->start==2 && obstacle->start==other_time);
    assert(a->x>=obstacle->x+obstacle->width+TS_MOSAIC_GUTTER);
    release(window,220,120);assert(!ui.mosaic_ghost_count);
    key(window,SDLK_z,KMOD_CTRL);assert(a->start==.5 && b->start==1 && a->x==20 && b->x==130);
    key(window,SDLK_m,KMOD_NONE);assert(a->muted && b->muted && !obstacle->muted);
    key(window,SDLK_m,KMOD_NONE);assert(!a->muted && !b->muted);
    key(window,SDLK_s,KMOD_NONE);assert(a->solo && b->solo && !obstacle->solo);
    key(window,SDLK_s,KMOD_NONE);assert(!a->solo && !b->solo);
    click(window,430,96,1);release(window,430,96);assert(ui.mosaic_selected==obstacle->id && !mosaic_chosen(&ui,0));
    /* Middle click seeks through a tile without changing the selected event. */
    SDL_Event seek={0};seek.type=SDL_MOUSEBUTTONDOWN;seek.button.windowID=SDL_GetWindowID(window);
    seek.button.button=SDL_BUTTON_MIDDLE;seek.button.x=190;seek.button.y=110;
    mosaic_event(&seek,window,0,&audio,&ui,&instrument,&mosaic,&portal,&sister,&transform,48000);
    assert(fabs(scene->time-44.0/24)<1e-9 && ui.mosaic_selected==obstacle->id);
    click(window,430,46,1);assert(ui.mosaic_follow);scene->playing=1;scene->time=20;
    mosaic_poll(0,&ui,&instrument,&mosaic);assert(fabs(ui.mosaic_scroll-(20-220.0/24))<1e-9);
    scene->time=0;mosaic_poll(0,&ui,&instrument,&mosaic);assert(ui.mosaic_scroll==0);
    click(window,430,46,1);scene->time=20;mosaic_poll(0,&ui,&instrument,&mosaic);assert(ui.mosaic_scroll==0);
    scene->playing=0;
    /* Edited source appears immediately, while the shared original stays. */
    assert(mosaic_enter(0,&audio,&ui,&instrument,&mosaic,a->id));
    for(size_t i=0;i<instrument.current.frames*instrument.current.channels;++i)instrument.current.data[i]*=.1f;
    ++instrument.current.visual_revision;assert(mosaic_commit(0,&ui,&instrument,&mosaic) && !ui.mosaic_edit_choice);
    assert(!mosaic_leave(0,&audio,&ui,&instrument,&mosaic));
    assert(ui.mosaic_edit_choice && a->source==source && b->source==source);
    key(window,SDLK_u,KMOD_NONE);assert(!ui.mosaic_edit_choice);
    assert(mosaic_leave(0,&audio,&ui,&instrument,&mosaic));mosaic_poll(0,&ui,&instrument,&mosaic);
    assert(ui.mosaic_source_count==18 && ui.mosaic_sources[16].source==a->source);
    assert(ui.mosaic_sources[17].source==source && b->source==source);
    double peaks=0,original=0;for(int i=0;i<TS_MOSAIC_PEAKS;++i){peaks+=a->source->peaks[i];original+=source->peaks[i];}
    assert(peaks<original*.101 && peaks>original*.099);
    /* Main-bank overlays must not touch any Mosaic pixel, even with routed,
       selected, or locked bank tiles. Borders and mute state remain visible. */
    static TsFramebuffer fb,copy;
    ui.show_keyboard=0;ui.sister_source_mask=0xffff;ui.mosaic_time=0;ui.mosaic_playing=0;
    ts_ui_render(&fb,&ui,&instrument);copy=fb;ts_overlay_tile_states(&fb,&ui,&instrument);
    assert(memcmp(&fb,&copy,sizeof(fb))==0);
    mosaic_select_only(&ui,a->id);ts_ui_render(&fb,&ui,&instrument);uint32_t border=fb.pixels[80*640+171];
    assert(border==0xffff3131u);
    TsPalette palette=ui.palette;
    ui.palette.colors[TS_PALETTE_MOSAIC_HIGHLIGHT]=0xff01fefeu;
    ts_ui_render(&fb,&ui,&instrument);assert(fb.pixels[80*640+171]==0xff01fefeu);
    ui.palette=palette;mosaic_select_only(&ui,0);ts_ui_render(&fb,&ui,&instrument);assert(border!=fb.pixels[80*640+171]);
    mosaic_native_check(&instrument,NULL);
    /* Warp waits until gesture completion; NEW TILE leaves the owner and all
       siblings intact and gives the edited document a fresh event ID. */
    assert(mosaic_enter(0,&audio,&ui,&instrument,&mosaic,a->id));
    uint64_t original_id=a->id;TsMosaicSource *original_source=a->source;
    scene->playing=1;scene->time=1;
    int background=getenv("TS_TEST_CDP_BIN")!=NULL;
    if(background) {
        ui.portal.open=1;assert(portal_source(0,&audio,&ui,&instrument,&portal));
        ts_portal_recipe_default(&ui.portal.recipe,ts_portal_process_find("modify.speed.1"));ui.portal.recipe.values[0]=.5;
        portal_preview(0,&audio,&ui,&portal);assert(portal.worker);portal.quick_apply=1;portal_close(0,&audio,&ui,&portal);
    }
    assert(ts_instrument_warp_gesture_begin(&instrument,&ui.warp_gesture,error,sizeof(error)));
    assert(ts_instrument_warp_gesture_preview(&instrument,&ui.warp_gesture,.7f,error,sizeof(error)));
    assert(mosaic_commit(0,&ui,&instrument,&mosaic) && !ui.mosaic_edit_choice);
    assert(ts_instrument_warp_gesture_commit(&instrument,&ui.warp_gesture,error,sizeof(error)));
    assert(mosaic_commit(0,&ui,&instrument,&mosaic) && !ui.mosaic_edit_choice);
    assert(!mosaic_leave(0,&audio,&ui,&instrument,&mosaic) && ui.mosaic_edit_choice);
    assert(a->source==original_source && b->source==source && scene->playing);
    const char *dialog=getenv("TS_MOSAIC_EDIT_SCREENSHOT");
    if(dialog){ts_audio_ui_render(&fb,&ui,&instrument);assert(ts_ui_write_ppm(&fb,dialog));}
    key(window,SDLK_RETURN,KMOD_NONE);assert(!ui.mosaic_edit_choice && !mosaic.active && ui.mosaic_open);
    TsMosaicEvent *variant=ts_mosaic_find(scene,ui.mosaic_selected);assert(variant && variant->source!=original_source);
    assert(a->source==original_source && b->source==source && a->id==original_id);
    assert(variant->start==a->start && variant->x>=a->x+a->width+TS_MOSAIC_GUTTER);
    TsInstrument *original_document=mosaic_document(&mosaic,&ui,&instrument,a->id);
    assert(original_document && original_document!=&instrument && mosaic_same_audio(&original_document->current,&original_source->sample));
    assert(mosaic_enter(0,&audio,&ui,&instrument,&mosaic,variant->id));
    if(background) {
        TsMosaicSource *new_audio=variant->source;wait_render();
        assert(strstr(ui.portal.message,"APPLIED TO EVENT") && a->source!=original_source);
        assert(variant->source==new_audio && mosaic_same_audio(&instrument.current,&new_audio->sample) && b->source==source);
        original_source=a->source;
    }
    int original_loop=a->looping,other_loop=b->looping;
    click(window,570,320,1);assert(variant->looping!=original_loop && a->looping==original_loop && b->looping==other_loop);
    int original_notes=a->note_count;click(window,200,370,1);assert(a->note_count==original_notes && variant->note_count!=original_notes);
    TsMosaicSource *warped=variant->source;
    assert(ts_instrument_apply_smear(&instrument,.6f,error,sizeof(error)));
    assert(mosaic_commit(0,&ui,&instrument,&mosaic) && !ui.mosaic_edit_choice);
    uint64_t smear=ts_sample_hash(&instrument.current);
    key(window,SDLK_ESCAPE,KMOD_NONE);assert(ui.mosaic_edit_choice);
    key(window,SDLK_ESCAPE,KMOD_NONE); /* Keep editing, not undo. */
    assert(!ui.mosaic_edit_choice && mosaic.active==variant->id && ts_sample_hash(&instrument.current)==smear && variant->source==warped);
    assert(ts_instrument_apply_smear(&instrument,.8f,error,sizeof(error)));
    assert(mosaic_commit(0,&ui,&instrument,&mosaic) && !ui.mosaic_edit_choice);
    assert(variant->source==warped);key(window,SDLK_BACKQUOTE,KMOD_SHIFT);assert(ui.mosaic_edit_choice);
    key(window,SDLK_u,KMOD_NONE);
    assert(!ui.mosaic_edit_choice && !mosaic.active && ui.mosaic_open && variant->source!=warped && a->source==original_source && b->source==source && scene->playing);
    /* Undoing every working edit removes the exit question. */
    assert(mosaic_enter(0,&audio,&ui,&instrument,&mosaic,variant->id));
    assert(ts_instrument_apply_smear(&instrument,.4f,error,sizeof(error)));
    assert(mosaic_commit(0,&ui,&instrument,&mosaic) && mosaic.audio_dirty);
    assert(ts_instrument_undo(&instrument,error,sizeof(error)));
    assert(mosaic_leave(0,&audio,&ui,&instrument,&mosaic) && !ui.mosaic_edit_choice);
    if(background) {
        /* FM is staged in an event, then its exact CDP input is published on
           exit. Completion must still reach that owner while B is selected. */
        TsFmSeedSequence seeds;ts_fm_seed_sequence_init(&seeds,0x43524541);portal.create_seeds=&seeds;
        assert(mosaic_enter(0,&audio,&ui,&instrument,&mosaic,variant->id));
        SDL_Event create={0};create.type=SDL_MOUSEBUTTONDOWN;create.button.button=SDL_BUTTON_LEFT;
        assert(portal_create_event(&create,100,214,0,&audio,&ui,&instrument,&portal,&transform));
        uint64_t fm=ts_sample_hash(&instrument.current),sibling=b->source->hash;
        uint64_t root=1;for(;;++root){TsFmSeedSequence probe;ts_fm_seed_sequence_init(&probe,root);if(ts_fm_seed_sequence_next(&probe)%12==1)break;}
        ts_fm_seed_sequence_init(&portal.create_dice,root);create.button.button=SDL_BUTTON_RIGHT;
        assert(portal_create_event(&create,100,214,0,&audio,&ui,&instrument,&portal,&transform) && portal.worker);
        key(window,SDLK_BACKQUOTE,KMOD_SHIFT);assert(ui.mosaic_edit_choice);key(window,SDLK_u,KMOD_NONE);
        assert(!mosaic.active && variant->source->hash==fm);
        assert(portal.source_event_revision==variant->revision);
        assert(mosaic_enter(0,&audio,&ui,&instrument,&mosaic,b->id));wait_render();
        assert(variant->source->hash!=fm && b->source->hash==sibling && ts_sample_hash(&instrument.current)==sibling);
        assert(mosaic_leave(0,&audio,&ui,&instrument,&mosaic));
        assert(mosaic_enter(0,&audio,&ui,&instrument,&mosaic,variant->id));
        create.button.button=SDL_BUTTON_MIDDLE;
        assert(portal_create_event(&create,100,214,0,&audio,&ui,&instrument,&portal,&transform));
        assert(ts_sample_hash(&instrument.current)==fm && variant->source->hash!=fm);
        /* Save waits for the destination choice, then replays exactly once. */
        SDL_FlushEvent(SDL_KEYDOWN);key(window,SDLK_s,KMOD_CTRL);assert(ui.mosaic_edit_choice && mosaic.resume_exit);
        key(window,SDLK_u,KMOD_NONE);assert(variant->source->hash==fm && !mosaic.resume_exit);
        SDL_Event resumed;assert(SDL_PeepEvents(&resumed,1,SDL_GETEVENT,SDL_KEYDOWN,SDL_KEYDOWN)==1);
        assert(resumed.key.keysym.sym==SDLK_s && (resumed.key.keysym.mod&KMOD_CTRL));
        ui.mosaic_open=1;portal.create_seeds=NULL;
    }
    /* Palette pagination includes edited versions, with usable sources on
       page two and no spill into the arrangement. */
    TsSample extra={0};assert(ts_sample_clone(&extra,&source->sample,error,sizeof(error)));
    for(int i=0;i<18;++i) {
        extra.data[0]=.01f*(i+1);TsMosaicSource *version=ts_mosaic_source(scene,&extra,error,sizeof(error));assert(version);
        assert(ts_mosaic_add(scene,version,20+i*4,0));
    }
    ts_sample_free(&extra);mosaic_poll(0,&ui,&instrument,&mosaic);assert(ui.mosaic_source_count>16);
    click(window,97,359,1);assert(ui.mosaic_source_page==1);
    click(window,20,85,1);assert(ui.mosaic_source_selected==16 && mosaic.source_drag.source==ui.mosaic_sources[16].source);
    release(window,110,190);click(window,20,359,1);assert(ui.mosaic_source_page==0);
    ui.mosaic=audio.mosaic=saved;mosaic_poll(0,&ui,&instrument,&mosaic);ts_mosaic_free(scene);
}
static void test_fallout_without_sister(void)
{
    char error[160];assert(ts_sister_runtime_reconfigure(&audio.sister,48000,2,error,sizeof(error)));
    sister.model.parameters=audio.sister.parameters;
    TsSisterUiHit hit={.action=TS_SISTER_UI_ACTION_FALLOUT_TOGGLE,.index=TS_SISTER_UI_FALLOUT_POWER};
    sister_apply_action(0,&audio,&ui,&instrument,&sister,NULL,NULL,hit,48000,2);
    assert(!audio.sister.enabled && audio.sister.parameters.fx.fallout.enabled);
    hit.action=TS_SISTER_UI_ACTION_PARAMETER;hit.index=TS_SISTER_UI_PARAM_FALLOUT_MIX;hit.normalized=1;
    sister_apply_action(0,&audio,&ui,&instrument,&sister,NULL,NULL,hit,48000,2);
    assert(audio.sister.parameters.fx.fallout.mix==1);
    ts_mosaic_seek(ui.mosaic,0);ui.mosaic->playing=1;
    float output[512];for(int i=0;i<8;++i)audio_callback(&audio,(Uint8 *)output,sizeof(output));
    assert(audio.sister.fallout.write_clock>0 && !audio.sister.enabled);
    hit.action=TS_SISTER_UI_ACTION_FALLOUT_TOGGLE;hit.index=TS_SISTER_UI_FALLOUT_POWER;
    sister_apply_action(0,&audio,&ui,&instrument,&sister,NULL,NULL,hit,48000,2);
    assert(!audio.sister.parameters.fx.fallout.enabled);
}

static void wait_render(void)
{
    Uint32 start=SDL_GetTicks();
    while(portal.worker && SDL_GetTicks()-start<15000){portal_poll(0,&audio,&ui,&instrument,&portal);SDL_Delay(1);}
    assert(!portal.worker && !ui.portal.busy);
}
static void test_record_file(SDL_Window *window,int x,int y)
{
    char folder[160],path[1200],error[160];
    snprintf(folder,sizeof(folder),"mosaic-record-%lu",(unsigned long)SDL_GetTicks());
    SDL_setenv("TAPESISTER_CAPTURES",folder,1);
    ts_audio_mixer_init(&audio.mixer);ts_performance_recorder_init(&sister.performance_recorder);
    audio.sister_file_recorder=&sister.performance_recorder;
    ui.mosaic_open=1;ts_mosaic_seek(ui.mosaic,0);ui.mosaic->playing=1;
    if(x>=0)click(window,x,y,1);else key(window,SDLK_f,KMOD_CTRL|KMOD_SHIFT);
    assert(ui.file_record_state==TS_PERFORMANCE_FILE_RECORDING);
    snprintf(path,sizeof(path),"%s",sister.performance_recorder.path);
    float output[4096];
    for(int i=0;i<8;++i)audio_callback(&audio,(Uint8 *)(output+i*512),512*sizeof(float));
    poll_file_capture_ui(&ui,&sister);assert(ui.file_record_frames==2048);
    /* The same recorder survives opening an event editor. */
    assert(mosaic_enter(0,&audio,&ui,&instrument,&mosaic,ui.mosaic->events[0].id));
    assert(ui.file_record_state==TS_PERFORMANCE_FILE_RECORDING && ui.mosaic->playing);
    click(window,580,389,1);
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
    test_workspace_routes(window);
    test_mosaic_master_routes();
    test_record_file(window,580,389);
    test_record_file(window,240,45);
    test_record_file(window,-1,0);
    test_canvas_gestures(window);
    test_mosaic_copy_drag(window);
    test_mosaic_external_banks(window);
    test_canvas_feedback(window);
    test_fallout_without_sister();
    test_mosaic_palette_controls();

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
        mosaic_sources_refresh(&ui,bank,&mosaic);ui.mosaic_selected=events[3]->id;
        mosaic_source_select_event(&ui,events[3]);ui.mosaic_follow=1;
        static TsFramebuffer fb;ts_ui_render(&fb,&ui,bank);assert(ts_ui_write_ppm(&fb,shot));
        mosaic_native_check(bank,getenv("TS_MOSAIC_NATIVE_SCREENSHOT"));
        ui.mosaic=saved;ts_mosaic_free(scene);ts_instrument_free(bank);free(bank);
    }
    portal_free(&portal);mosaic_controller_free(&mosaic);ts_mosaic_free(ui.mosaic);
    ts_performance_free(&audio.performance);ts_performance_free(&audio.tile_launchers);ts_sister_runtime_free(&audio.sister);
    ts_capture_free(&audio.capture);ts_instrument_free(&instrument);SDL_DestroyWindow(window);
    SDL_CloseAudioDevice(device);ts_real_output=0;SDL_Quit();
    puts("Mosaic editor, background CDP ownership, shared routing and exact output WAV capture passed");return 0;
}
