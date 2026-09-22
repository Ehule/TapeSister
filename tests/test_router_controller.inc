static void router_commit(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui)
{
    ts_router_sanitize(&ui->config.router);
    if(device)SDL_LockAudioDevice(device);
    ts_sister_runtime_set_router(&audio->sister,&ui->config.router);router_sync(audio,ui);
    if(device)SDL_UnlockAudioDevice(device);
}
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
    router_sync(&audio,&ui);
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
        assert(router_mouse(SDL_MOUSEBUTTONDOWN,160,135));assert(router_mouse(SDL_MOUSEMOTION,160,164));
        if(route==3)router_shot("TS_TEST_ROUTER_DRAG_PPM");
        assert(router_mouse(SDL_MOUSEBUTTONUP,160,164));
        assert(audio.sister.router.controls.order[0]==TS_ROUTER_SISTER && ui.config.router.order[1]==TS_ROUTER_PRISM);
        TsPerformanceRecorder recorder;ts_performance_recorder_init(&recorder);
        assert(ts_performance_recorder_start(&recorder,"test-router-output.wav",44100,2,16384,error,sizeof(error)));
        audio.sister_file_recorder=&recorder;atomic_store(&audio.sister_file_tap,TS_SISTER_TAP_MIX);
        for(int action=0;action<12;++action) {
            int row=action%4,y=122+42*row,x=action%2?540:440;
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
        ui.config.router.bypass_mask=1u<<TS_ROUTER_INSERT;ui.config.router.solo=0;router_commit(device,&audio,&ui);
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
    assert(router_mouse(SDL_MOUSEBUTTONDOWN,565,350) && ui.router_open && !ui.master_eq_open);ui.router_open=0;
    ts_mosaic_free(ui.mosaic);ui.mosaic=audio.mosaic=NULL;
    ts_sister_runtime_set_parameters(&audio.sister,&saved);ts_router_default(&ui.config.router);router_commit(device,&audio,&ui);
    reset_route(0);
}

static int router_right(int x,int y)
{
    SDL_Event e={0};e.type=SDL_MOUSEBUTTONDOWN;e.button.button=SDL_BUTTON_RIGHT;
    e.button.windowID=SDL_GetWindowID(window);e.button.x=x;e.button.y=y;
    return router_event(&e,window,device,&audio,&ui,&router_panel);
}
static void test_router_performance_controller(void)
{
    char error[160];TsSisterParameters saved=audio.sister.parameters;
    for(int route=0;route<4;++route) {
        reset_route(route);ui.router_performance_open=0;ui.router_open=1;ui.insert_open=0;
        ts_router_default(&ui.config.router);router_commit(device,&audio,&ui);
        ts_router_performance_default(&audio.sister.router.performance);
        if(route==3)assert(ts_sister_runtime_enable(&audio.sister,44100,2,2,1,error,sizeof(error)));
        else ts_sister_runtime_disable(&audio.sister);
        TsSisterParameters p=audio.sister.parameters;p.prism.enabled=1;p.prism.lenses=24;
        ts_sister_runtime_set_parameters(&audio.sister,&p);
        toggle_fm_hold(device,&audio,&ui,&instrument);click_note(0,0);assert(voices()==1);
        router_sync(&audio,&ui);
        assert(router_mouse(SDL_MOUSEBUTTONDOWN,460,51) && ui.router_performance_open);
        ui.router_slot=0;assert(router_mouse(SDL_MOUSEBUTTONDOWN,40,139)); /* STORE A */
        router_manual(device,&audio,&ui,0,1);
        assert(router_mouse(SDL_MOUSEBUTTONDOWN,80,82));assert(ui.router_slot==1);
        assert(router_mouse(SDL_MOUSEBUTTONDOWN,40,139)); /* STORE B */
        assert(router_mouse(SDL_MOUSEBUTTONDOWN,40,82));
        assert(router_mouse(SDL_MOUSEBUTTONDOWN,125,139)); /* RECALL A */
        assert(audio.sister.router.controls.solo==0);
        assert(router_mouse(SDL_MOUSEBUTTONDOWN,440,300)); /* Step 01 -> A */
        assert(router_mouse(SDL_MOUSEBUTTONDOWN,80,82)); /* select B */
        SDL_SetModState(KMOD_SHIFT);assert(router_mouse(SDL_MOUSEBUTTONDOWN,64,215));SDL_SetModState(KMOD_NONE);
        assert(audio.sister.router.performance.length==2 && ui.router_step==1);
        assert(router_mouse(SDL_MOUSEBUTTONDOWN,440,300)); /* Step 02 -> B */
        audio.sister.router.performance.step[0].seconds=.05f;
        audio.sister.router.performance.step[1].seconds=.05f;
        TsRouterControls before=audio.sister.router.controls;
        TsInsertControls insert_before=audio.sister.insert.controls;
        assert(router_mouse(SDL_MOUSEBUTTONDOWN,40,172));assert(audio.sister.router.transport.running);
        /* UI edits do not restart the current step. */
        uint64_t frames=audio.sister.router.transport.frames;
        assert(router_mouse(SDL_MOUSEBUTTONDOWN,30,215));
        assert(router_mouse(SDL_MOUSEBUTTONDOWN,180,300)); /* A -> B; applies on next visit */
        assert(audio.sister.router.transport.frames==frames && audio.sister.router.controls.solo==0);
        assert(router_right(180,300)); /* Restore future A */
        TsPerformanceRecorder recorder;ts_performance_recorder_init(&recorder);
        assert(ts_performance_recorder_start(&recorder,"test-router-performance-output.wav",44100,2,16384,error,sizeof(error)));
        audio.sister_file_recorder=&recorder;atomic_store(&audio.sister_file_tap,TS_SISTER_TAP_MIX);
        for(int block=0;block<20;++block) {
            float output[512];size_t start=atomic_load(&recorder.write_cursor);
            audio_callback(&audio,(Uint8*)output,sizeof(output));
            assert(atomic_load(&recorder.write_cursor)==start+256 && voices()==1);
            for(int i=0;i<256;++i) {
                assert(isfinite(output[i*2]+output[i*2+1]));
                assert(recorder.ring[start+i].l==output[i*2] && recorder.ring[start+i].r==output[i*2+1]);
            }
        }
        assert(audio.sister.router.transport.step==0 && audio.sister.router.transport.frames==1495);
        TsSisterRoutingSnapshot snapshot;assert(ts_sister_runtime_get_snapshot(&audio.sister,&snapshot));
        assert(snapshot.router_view.running && fabsf(snapshot.router_view.remaining-1495.f/44100)<1e-6f);
        assert(!memcmp(&insert_before,&audio.sister.insert.controls,sizeof(insert_before)));
        router_sync(&audio,&ui);ui.performance_tooltip[0]=0;
        if(route==3)router_shot("TS_TEST_ROUTER_PERFORMANCE_PPM");
        assert(router_mouse(SDL_MOUSEBUTTONDOWN,110,172)); /* STOP holds */
        assert(!audio.sister.router.transport.running && audio.sister.router.controls.solo==0);
        assert(router_mouse(SDL_MOUSEBUTTONDOWN,40,172));
        assert(router_mouse(SDL_MOUSEBUTTONDOWN,540,51) && !ui.router_performance_open);
        assert(router_mouse(SDL_MOUSEBUTTONDOWN,160,135));assert(router_mouse(SDL_MOUSEBUTTONUP,160,164));
        assert(audio.sister.router.transport.running);
        assert(audio.sister.router.controls.order[0]==TS_ROUTER_PRISM && !ui.router_drag); /* Locked. */
        audio.sister.router.performance.timer_seconds[TS_ROUTER_PRISM]=.05f;
        assert(router_right(545,122));assert(audio.sister.router.controls.solo==TS_ROUTER_PRISM+1);
        router_sync(&audio,&ui);if(route==3)router_shot("TS_TEST_ROUTER_TIMERS_PPM");
        assert(router_mouse(SDL_MOUSEBUTTONDOWN,445,122)); /* Live override */
        assert(audio.sister.router.transport.running && audio.sister.router.transport.manual_override && !audio.sister.router.transport.timer_mask);
        assert(router_mouse(SDL_MOUSEBUTTONDOWN,460,51));assert(router_mouse(SDL_MOUSEBUTTONDOWN,260,172));
        assert(audio.sister.router.controls.bypass_mask==before.bypass_mask && audio.sister.router.controls.solo==before.solo);
        assert(audio.sister.router.controls.order[0]==TS_ROUTER_PRISM); /* RESTORE never reorders. */
        assert(router_mouse(SDL_MOUSEBUTTONDOWN,540,51));
        assert(router_mouse(SDL_MOUSEBUTTONDOWN,160,135));assert(router_mouse(SDL_MOUSEBUTTONUP,160,164));
        assert(audio.sister.router.controls.order[0]==TS_ROUTER_SISTER); /* Unlocked after RESTORE. */
        assert(router_mouse(SDL_MOUSEBUTTONDOWN,460,51));
        audio.sister_file_recorder=NULL;ts_performance_recorder_request_stop(&recorder);
        while(ts_performance_recorder_pump(&recorder,16384)){}
        ts_performance_recorder_free(&recorder);remove("test-router-performance-output.wav");
        /* Closing a view does not stop the transport or consume key releases. */
        assert(router_mouse(SDL_MOUSEBUTTONDOWN,40,172));
        SDL_Event e={0};e.type=SDL_KEYDOWN;e.key.windowID=SDL_GetWindowID(window);e.key.keysym.sym=SDLK_F9;
        assert(router_event(&e,window,device,&audio,&ui,&router_panel));assert(!ui.router_open && audio.sister.router.transport.running);
        assert(router_event(&e,window,device,&audio,&ui,&router_panel));
        e.key.keysym.sym=SDLK_SPACE;assert(router_event(&e,window,device,&audio,&ui,&router_panel));
        assert(!voices() && !audio.sister.router.transport.running && !audio.sister.router.transport.timer_mask);
        ui.router_open=ui.router_performance_open=0;
    }
    /* MIDI learn uses the existing target discovery, trigger filtering, and
       dispatcher, including Note Off suppression and stable stage IDs. */
    ui.router_open=1;ui.router_performance_open=0;router_sync(&audio,&ui);
    char target[TS_MIDI_TARGET_ID_MAX];int stage=audio.sister.router.controls.order[0];
    assert(ts_ui_midi_target_from_point(&ui,440,122,target,sizeof(target)));
    char expected[80];snprintf(expected,sizeof(expected),"router.stage.%d.bypass",stage);assert(!strcmp(target,expected));
    ui.midi_learn_active=1;assert(!router_mouse(SDL_MOUSEBUTTONDOWN,440,122) && ui.router_open);ui.midi_learn_active=0;
    TsMidiSource cc={TS_MIDI_SOURCE_CC,0,93};assert(ts_midi_map_assign(&ui.config.midi_map,target,cc));
    TsMidiEvent midi;unsigned bypass=audio.sister.router.controls.bypass_mask;
    assert(ts_midi_decode_short_message(0xb0,93,0,&midi));
    assert(handle_midi_mapping(device,&audio,&ui,&instrument,&router_panel,NULL,NULL,&midi,44100,2));
    assert(audio.sister.router.controls.bypass_mask==bypass);
    assert(ts_midi_decode_short_message(0xb0,93,127,&midi));
    assert(handle_midi_mapping(device,&audio,&ui,&instrument,&router_panel,NULL,NULL,&midi,44100,2));
    assert(audio.sister.router.controls.bypass_mask==(bypass^(1u<<stage)));
    ui.router_performance_open=1;
    assert(ts_ui_midi_target_from_point(&ui,80,82,target,sizeof(target)) && !strcmp(target,"router.state.B.recall"));
    assert(midi_apply_target(device,&audio,&ui,&instrument,&router_panel,NULL,NULL,"router.play",1,44100,2));
    assert(audio.sister.router.transport.running);
    assert(midi_apply_target(device,&audio,&ui,&instrument,&router_panel,NULL,NULL,target,1,44100,2));
    assert(audio.sister.router.transport.running && audio.sister.router.controls.solo==1);
    assert(midi_apply_target(device,&audio,&ui,&instrument,&router_panel,NULL,NULL,"router.stop",1,44100,2));
    assert(!audio.sister.router.transport.running);
    assert(midi_apply_target(device,&audio,&ui,&instrument,&router_panel,NULL,NULL,"router.restore",1,44100,2));
    assert(!router_midi_command(device,&audio,&ui,"router.state.B.nothin"));
    assert(router_midi_command(device,&audio,&ui,"router.timer.4.solo"));assert(audio.sister.router.controls.solo==5);
    assert(router_midi_command(device,&audio,&ui,"router.timer.4.cancel"));assert(!audio.sister.router.transport.timer_mask);
    ts_midi_map_init(&ui.config.midi_map);ui.router_open=ui.router_performance_open=0;
    ts_sister_runtime_set_parameters(&audio.sister,&saved);
    ts_router_performance_default(&audio.sister.router.performance);
    ts_router_default(&ui.config.router);router_commit(device,&audio,&ui);reset_route(0);
}
