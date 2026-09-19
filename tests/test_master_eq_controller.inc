static SisterWindow eq_panel;
static int eq_click(int x,int y,int button)
{
    SDL_Event e={0};e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=SDL_GetWindowID(window);
    e.button.button=button;e.button.x=x;e.button.y=y;
    int result=master_eq_event(&e,window,device,&audio,&ui,&eq_panel);
    e.type=SDL_MOUSEBUTTONUP;master_eq_event(&e,window,device,&audio,&ui,&eq_panel);
    return result;
}
static void test_master_eq_controller(void)
{
    ts_sister_ui_model_init(&eq_panel.model,&ui.config);
    reset_route(0);ts_sister_runtime_disable(&audio.sister);
    int lim=audio.sister.limiter.enabled;
    assert(eq_click(TS_UI_MASTER_LIMITER_X+5,10,SDL_BUTTON_LEFT));
    assert(ui.master_eq_open);
    assert(audio.sister.limiter.enabled==lim);
    eq_click(360,55,SDL_BUTTON_LEFT);assert(audio.sister.master_eq.controls.enabled);
    uint64_t hash=paged_project_state_hash(NULL,&instrument,NULL,&audio.sister);
    eq_click(330,311,SDL_BUTTON_LEFT);
    assert(audio.sister.master_eq.controls.band[0].gain_db>0);
    assert(paged_project_state_hash(NULL,&instrument,NULL,&audio.sister)!=hash);
    /* The row changes audible state; graph selection remains independent. */
    TsMasterEqControls saved=ui.config.master_eq;
    eq_click(160,260,SDL_BUTTON_LEFT);
    assert(!audio.sister.master_eq.controls.band[1].enabled && ui.master_eq_band==0);
    for(int i=0;i<5;++i) {
        TsEqBand expected=saved.band[i];if(i==1)expected.enabled=0;
        assert(!memcmp(&expected,&ui.config.master_eq.band[i],sizeof(expected)));
    }
    saved=ui.config.master_eq;
    eq_click(135,260,SDL_BUTTON_LEFT); /* Gap between buttons is inert. */
    assert(!memcmp(&saved,&ui.config.master_eq,sizeof(saved)));
    SDL_SetModState(KMOD_CTRL);eq_click(160,260,SDL_BUTTON_LEFT);
    assert(audio.sister.master_eq.controls.solo_band==2 && ui.master_eq_band==0);
    assert(!ui.config.master_eq.band[1].enabled && ts_master_eq_band_active(&ui.config.master_eq,1));
    eq_click(160,260,SDL_BUTTON_LEFT);
    assert(!memcmp(&saved,&ui.config.master_eq,sizeof(saved)));
    eq_click(285,260,SDL_BUTTON_LEFT);eq_click(405,260,SDL_BUTTON_LEFT);
    assert(audio.sister.master_eq.controls.solo_band==4);
    SDL_SetModState(KMOD_NONE);eq_click(405,260,SDL_BUTTON_LEFT);
    assert(!ui.config.master_eq.solo_band && !ui.config.master_eq.band[3].enabled);
    /* Learn selection never changes the sound. */
    saved=ui.config.master_eq;ui.midi_learn_active=1;
    eq_click(285,260,SDL_BUTTON_LEFT);
    assert(ui.master_eq_band==2 && !memcmp(&saved,&ui.config.master_eq,sizeof(saved)));
    ui.midi_learn_active=0;ui.master_eq_band=0;
    /* Reset clears solo along with the bands. */
    SDL_SetModState(KMOD_CTRL);eq_click(160,260,SDL_BUTTON_LEFT);SDL_SetModState(KMOD_NONE);
    eq_click(500,55,SDL_BUTTON_LEFT);assert(ui.master_eq_reset_pending);
    assert(audio.sister.master_eq.controls.band[0].gain_db>0);
    eq_click(500,55,SDL_BUTTON_LEFT);assert(!audio.sister.master_eq.controls.enabled);
    assert(!audio.sister.master_eq.controls.solo_band);
    for(int i=0;i<5;++i)assert(audio.sister.master_eq.controls.band[i].gain_db==0);
    /* Modal dialogs own their mouse/key input; no edits pass through. */
    ui.project_overwrite_confirm_open=1;
    assert(!eq_click(360,55,SDL_BUTTON_LEFT));assert(!audio.sister.master_eq.controls.enabled);
    ui.project_overwrite_confirm_open=0;
    assert(!ui.master_eq_open);master_eq_open(device,&audio,&ui);
    /* Drag a node while scaled 2x; the absolute musical pitch is irrelevant. */
    int nx=44+(int)lrintf(552*ts_master_eq_normalized(&ui.config.master_eq.band[0],0,44100));
    SDL_SetWindowSize(window,1280,800);
    SDL_Event e={0};e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=SDL_GetWindowID(window);
    e.button.button=SDL_BUTTON_LEFT;e.button.x=nx*2;e.button.y=310;
    assert(master_eq_event(&e,window,device,&audio,&ui,&eq_panel));assert(ui.master_eq_drag==4);
    float old_hz=ui.config.master_eq.band[0].frequency;
    e.type=SDL_MOUSEMOTION;e.motion.windowID=SDL_GetWindowID(window);e.motion.state=SDL_BUTTON_LMASK;
    e.motion.x=nx*2+100;e.motion.y=274;e.motion.xrel=100;e.motion.yrel=-36;
    assert(master_eq_event(&e,window,device,&audio,&ui,&eq_panel));
    assert(ui.config.master_eq.band[0].frequency>old_hz && fabs(ui.config.master_eq.band[0].gain_db-4)<.01);
    e.type=SDL_MOUSEBUTTONUP;e.button.windowID=SDL_GetWindowID(window);e.button.button=SDL_BUTTON_LEFT;
    master_eq_event(&e,window,device,&audio,&ui,&eq_panel);assert(!ui.master_eq_drag);
    SDL_SetWindowSize(window,640,400);
    /* Existing MIDI target selection, pickup, and dispatch are reused. */
    char target[TS_MIDI_TARGET_ID_MAX];
    assert(ts_ui_midi_target_from_point(&ui,240,312,target,sizeof(target)) && !strcmp(target,"main.eq.band.1.1"));
    assert(ts_midi_target_is_continuous(target));
    TsMidiSource cc={TS_MIDI_SOURCE_CC,0,21};assert(ts_midi_map_assign(&ui.config.midi_map,target,cc));
    TsMidiEvent midi;assert(ts_midi_decode_short_message(0xb0,21,0,&midi));
    assert(handle_midi_mapping(device,&audio,&ui,&instrument,&eq_panel,NULL,NULL,&midi,44100,2));
    assert(fabs(ui.config.master_eq.band[0].gain_db-4)<.01); /* Pickup waits. */
    assert(ts_midi_decode_short_message(0xb0,21,127,&midi));
    assert(handle_midi_mapping(device,&audio,&ui,&instrument,&eq_panel,NULL,NULL,&midi,44100,2));
    assert(ui.config.master_eq.band[0].gain_db==12);
    assert(midi_apply_target(device,&audio,&ui,&instrument,&eq_panel,NULL,NULL,"main.eq.band.2.0",.5f,44100,2));
    assert(midi_apply_target(device,&audio,&ui,&instrument,&eq_panel,NULL,NULL,"main.eq.band.2.2",1,44100,2));
    assert(audio.sister.master_eq.controls.band[1].q==8);
    int enabled=ui.config.master_eq.enabled;
    assert(midi_apply_target(device,&audio,&ui,&instrument,&eq_panel,NULL,NULL,"main.eq.bypass",1,44100,2));
    assert(ui.config.master_eq.enabled!=enabled);
    ts_midi_map_init(&ui.config.midi_map);
    /* EQ is live above all source routes and does not release notes or ARP. */
    for(int route=0;route<4;++route) {
        reset_route(route);
        if(route==3)assert(ts_sister_runtime_enable(&audio.sister,44100,2,2,1,NULL,0));
        else ts_sister_runtime_disable(&audio.sister);
        toggle_fm_hold(device,&audio,&ui,&instrument);click_note(0,0);
        TsKeyboardSequenceSettings seq=audio.keyboard_sequence.settings;
        seq.count=2;seq.notes[0]=60;seq.notes[1]=67;seq.seconds=.04;
        ts_keyboard_sequence_set(&audio.keyboard_sequence,&seq);
        assert(keyboard_sequence_prepare(device,&audio,&ui,&instrument,&fm,44100,1));
        assert(ts_keyboard_sequence_play(&audio.keyboard_sequence));
        ui.config.master_eq.band[0]=(TsEqBand){1,TS_EQ_BELL,300,-3,1};
        ui.config.master_eq.enabled=1;master_eq_commit(device,&audio,&ui);
        assert(voices()==1 && audio.keyboard_sequence.running);
        assert(power_audio_energy()>.01);
        e=(SDL_Event){0};e.type=SDL_KEYUP;e.key.windowID=SDL_GetWindowID(window);e.key.keysym.sym=SDLK_z;
        assert(!master_eq_event(&e,window,device,&audio,&ui,&eq_panel));
        e.type=SDL_KEYDOWN;e.key.keysym.sym=SDLK_SPACE;
        assert(master_eq_event(&e,window,device,&audio,&ui,&eq_panel));
        assert(!audio.keyboard_sequence.running && !voices());
    }
    /* FILE OUT sees exactly the post-EQ/limiter/fader hardware frames. */
    reset_route(0);ts_sister_runtime_disable(&audio.sister);
    toggle_fm_hold(device,&audio,&ui,&instrument);click_note(0,0);assert(power_audio_energy()>.01);
    TsPerformanceRecorder recorder;ts_performance_recorder_init(&recorder);char error[160];
    assert(ts_performance_recorder_start(&recorder,"test-master-eq-output.wav",44100,2,1024,error,sizeof(error)));
    audio.sister_file_recorder=&recorder;atomic_store(&audio.sister_file_tap,TS_SISTER_TAP_MIX);
    float out[512];audio_callback(&audio,(Uint8 *)out,sizeof(out));
    assert(atomic_load(&recorder.write_cursor)==256);
    for(int i=0;i<256;++i)assert(recorder.ring[i].l==out[i*2] && recorder.ring[i].r==out[i*2+1]);
    audio.sister_file_recorder=NULL;ts_performance_recorder_request_stop(&recorder);
    while(ts_performance_recorder_pump(&recorder,1024)){}
    ts_performance_recorder_free(&recorder);remove("test-master-eq-output.wav");
    stop_all_force(device,&audio,&ui);
    /* Native screenshot: modest correction, all six filter choices are available. */
    ui.fm_open=0;ui.master_eq_open=1;ui.master_eq_band=2;ui.master_eq_rate=44100;
    ts_master_eq_default(&ui.config.master_eq);ui.config.master_eq.enabled=1;
    ui.config.master_eq.band[0]=(TsEqBand){1,TS_EQ_HIGH_PASS,40,0,.70710678f};
    ui.config.master_eq.band[1]=(TsEqBand){1,TS_EQ_LOW_SHELF,150,-2.5f,.70710678f};
    ui.config.master_eq.band[2]=(TsEqBand){1,TS_EQ_BELL,850,-4,1.4f};
    ui.config.master_eq.band[3]=(TsEqBand){1,TS_EQ_BELL,3400,-2,2};
    ui.config.master_eq.band[4]=(TsEqBand){1,TS_EQ_HIGH_SHELF,10000,1.5f,.70710678f};
    master_eq_commit(device,&audio,&ui);ui.master_output.limiter_enabled=1;
    ui.master_output.limiter_ceiling_db=-1;ui.master_output.limiter_gain_reduction_db=0;ui.master_output.gain=1;
    const char *shot=getenv("TS_TEST_MASTER_EQ_PPM");
    if(shot){static TsFramebuffer fb;ts_ui_render(&fb,&ui,&instrument);assert(ts_ui_write_ppm(&fb,shot));}
    const char *solo_shot=getenv("TS_TEST_MASTER_EQ_SOLO_PPM");
    if(solo_shot) {
        ui.config.master_eq.band[1].enabled=0;ui.config.master_eq.solo_band=3;
        master_eq_commit(device,&audio,&ui);
        static TsFramebuffer fb;ts_ui_render(&fb,&ui,&instrument);assert(ts_ui_write_ppm(&fb,solo_shot));
    }
    ui.master_eq_open=0;ts_master_eq_default(&ui.config.master_eq);master_eq_commit(device,&audio,&ui);
    ts_keyboard_sequence_source_free(ts_keyboard_sequence_source(&audio.keyboard_sequence,NULL));
}
