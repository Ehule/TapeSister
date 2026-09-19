static SisterWindow router_panel;
static int router_mouse(int type,int x,int y)
{
    SDL_Event e={0};e.type=type;
    if(type==SDL_MOUSEMOTION){e.motion.windowID=SDL_GetWindowID(window);e.motion.x=x;e.motion.y=y;e.motion.state=SDL_BUTTON_LMASK;}
    else {e.button.windowID=SDL_GetWindowID(window);e.button.button=SDL_BUTTON_LEFT;e.button.x=x;e.button.y=y;}
    return router_event(&e,window,device,&audio,&ui,&router_panel);
}
static void router_shot(const char *key)
{
    const char *path=getenv(key);if(!path)return;
    TsSisterRoutingSnapshot s;assert(ts_sister_runtime_get_snapshot(&audio.sister,&s));
    ui.router_enabled=s.router_enabled;memcpy(ui.router_peaks,s.router_peaks,sizeof(ui.router_peaks));
    static TsFramebuffer fb;ts_ui_render(&fb,&ui,&instrument);assert(ts_ui_write_ppm(&fb,path));
}
static void test_router_controller(void)
{
    ts_sister_ui_model_init(&router_panel.model,&ui.config);ui.master_eq_open=0;
    ui.performance_tooltip[0]=0;
    TsSisterParameters saved=audio.sister.parameters;char error[160];
    ui.mosaic=audio.mosaic=ts_mosaic_create();assert(ui.mosaic);
    TsMosaicSource *source=ts_mosaic_source(ui.mosaic,&instrument.current,error,sizeof(error));assert(source);
    assert(ts_mosaic_add(ui.mosaic,source,0,0));ui.mosaic->playing=ui.mosaic->repeat=1;
    for(int route=0;route<4;++route) {
        reset_route(route);ui.router_open=0;ts_router_default(&ui.config.router);router_commit(device,&audio,&ui);
        if(route==3)assert(ts_sister_runtime_enable(&audio.sister,44100,2,2,1,error,sizeof(error)));
        else ts_sister_runtime_disable(&audio.sister);
        TsSisterParameters p=audio.sister.parameters;p.prism.enabled=1;p.prism.lenses=24;
        p.fx.enabled=1;p.fx.fallout.enabled=1;p.fx.fallout.mix=.2f;ts_sister_runtime_set_parameters(&audio.sister,&p);
        toggle_fm_hold(device,&audio,&ui,&instrument);click_note(0,0);assert(voices()==1);
        TsKeyboardSequenceSettings seq=audio.keyboard_sequence.settings;seq.count=2;seq.notes[0]=60;seq.notes[1]=67;seq.seconds=.04f;
        ts_keyboard_sequence_set(&audio.keyboard_sequence,&seq);
        assert(keyboard_sequence_prepare(device,&audio,&ui,&instrument,&fm,44100,1));assert(ts_keyboard_sequence_play(&audio.keyboard_sequence));
        assert(power_audio_energy()>.01);
        SDL_Event key={0};key.type=SDL_KEYDOWN;key.key.windowID=SDL_GetWindowID(window);key.key.keysym.sym=SDLK_F9;
        assert(router_event(&key,window,device,&audio,&ui,&router_panel) && ui.router_open);
        assert(voices()==1 && audio.keyboard_sequence.running);
        key.type=SDL_KEYUP;key.key.keysym.sym=SDLK_z;
        assert(!router_event(&key,window,device,&audio,&ui,&router_panel));
        /* Dragging is a preview until release; Escape cancels without audio changes. */
        assert(router_mouse(SDL_MOUSEBUTTONDOWN,160,135));assert(router_mouse(SDL_MOUSEMOTION,160,280));
        assert(audio.sister.router.controls.order[0]==TS_ROUTER_PRISM);
        key.type=SDL_KEYDOWN;key.key.keysym.sym=SDLK_ESCAPE;
        assert(router_event(&key,window,device,&audio,&ui,&router_panel) && !ui.router_drag && ui.router_open);
        assert(router_mouse(SDL_MOUSEBUTTONDOWN,160,135));assert(router_mouse(SDL_MOUSEMOTION,160,184));
        if(route==3)router_shot("TS_TEST_ROUTER_DRAG_PPM");
        assert(router_mouse(SDL_MOUSEBUTTONUP,160,184));
        assert(audio.sister.router.controls.order[0]==TS_ROUTER_SISTER && ui.config.router.order[1]==TS_ROUTER_PRISM);
        TsPerformanceRecorder recorder;ts_performance_recorder_init(&recorder);
        assert(ts_performance_recorder_start(&recorder,"test-router-output.wav",44100,2,16384,error,sizeof(error)));
        audio.sister_file_recorder=&recorder;atomic_store(&audio.sister_file_tap,TS_SISTER_TAP_MIX);
        for(int action=0;action<12;++action) {
            int row=action%4,y=136+48*row,x=action%2?540:440;
            assert(router_mouse(SDL_MOUSEBUTTONDOWN,x,y));assert(router_mouse(SDL_MOUSEBUTTONUP,x,y));
            float block[512];size_t before=atomic_load(&recorder.write_cursor);
            audio_callback(&audio,(Uint8*)block,sizeof(block));
            assert(voices()==1 && audio.keyboard_sequence.running);
            assert(atomic_load(&recorder.write_cursor)==before+256);
            for(int i=0;i<256;++i) {
                assert(isfinite(block[i*2]+block[i*2+1]));
                assert(recorder.ring[before+i].l==block[i*2] && recorder.ring[before+i].r==block[i*2+1]);
            }
        }
        audio.sister_file_recorder=NULL;ts_performance_recorder_request_stop(&recorder);
        while(ts_performance_recorder_pump(&recorder,16384)){}
        ts_performance_recorder_free(&recorder);remove("test-router-output.wav");
        ui.config.router.bypass_mask=0;ui.config.router.solo=0;router_commit(device,&audio,&ui);
        assert(power_audio_energy()>.01);
        ts_ui_performance_hover(&ui,180,180,100,0);ts_ui_performance_hover(&ui,180,180,700,0);
        assert(strstr(ui.performance_tooltip,"DRAG"));ui.performance_tooltip[0]=0;
        if(route==3)router_shot("TS_TEST_ROUTER_PPM");
        key.key.keysym.sym=SDLK_SPACE;
        assert(router_event(&key,window,device,&audio,&ui,&router_panel));assert(!voices() && !audio.keyboard_sequence.running);
        key.key.keysym.sym=SDLK_F9;assert(router_event(&key,window,device,&audio,&ui,&router_panel) && !ui.router_open);
    }
    /* Browser text and modal confirmations retain F9/Space ownership. */
    SDL_Event key={0};key.type=SDL_KEYDOWN;key.key.windowID=SDL_GetWindowID(window);key.key.keysym.sym=SDLK_F9;
    ui.browser.mode=TS_BROWSER_LOAD_WAV;assert(!router_event(&key,window,device,&audio,&ui,&router_panel));ts_browser_close(&ui.browser);
    ui.exit_confirm_open=1;assert(!router_event(&key,window,device,&audio,&ui,&router_panel));ui.exit_confirm_open=0;
    ui.fm_open=0;ui.master_eq_open=1;
    assert(router_mouse(SDL_MOUSEBUTTONDOWN,390,370) && ui.router_open && !ui.master_eq_open);ui.router_open=0;
    ts_mosaic_free(ui.mosaic);ui.mosaic=audio.mosaic=NULL;
    ts_sister_runtime_set_parameters(&audio.sister,&saved);ts_router_default(&ui.config.router);router_commit(device,&audio,&ui);
    reset_route(0);
}
