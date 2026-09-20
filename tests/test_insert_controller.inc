static ExternalInputState insert_input;
static float insert_previous[256*8];

static void insert_callback_block(float *out)
{
    float capture[256*8]={0};
    for(int i=0;i<256;++i) {
        capture[i*8+2]=-insert_previous[i*8+3]*.5f;
        capture[i*8+3]=insert_previous[i*8+2]*.75f;
    }
    external_input_callback(&insert_input,(Uint8 *)capture,sizeof(capture));
    audio_callback(&audio,(Uint8 *)out,256*8*sizeof(float));
    memcpy(insert_previous,out,sizeof(insert_previous));
    for(int i=0;i<256;++i) {
        for(int ch=0;ch<8;++ch)assert(isfinite(out[i*8+ch]));
        for(int ch=4;ch<8;++ch)assert(out[i*8+ch]==0);
    }
}

static void insert_controller_shot(const char *key)
{
    TsSisterRoutingSnapshot s;assert(ts_sister_runtime_get_snapshot(&audio.sister,&s));
    ui.config.router=s.router;ui.config.insert=s.insert;ui.router_enabled=s.router_enabled;
    ui.insert_send_peak=s.insert_send_peak;ui.insert_return_peak=s.insert_return_peak;
    ui.insert_inputs=s.insert_inputs;ui.insert_outputs=s.insert_outputs;
    memcpy(ui.router_peaks,s.router_peaks,sizeof(ui.router_peaks));ui.performance_tooltip[0]=0;
    snprintf(ui.status,sizeof(ui.status),"INSERT: SERIAL SEND / RETURN - ROUTER ORDER IS LIVE");
    const char *path=getenv(key);if(!path)return;
    static TsFramebuffer fb;ts_ui_render(&fb,&ui,&instrument);ts_ui_render_file_recording(&fb,&ui);
    assert(ts_ui_write_ppm(&fb,path));
}

static void test_insert_controller(void)
{
    TsSisterParameters saved=audio.sister.parameters;char error[160];
    ts_input_monitor_init(&insert_input.monitor);ts_input_activity_init(&insert_input.activity);
    ts_input_ownership_init(&insert_input.ownership);ts_external_recorder_init(&insert_input.recorder);
    atomic_init(&insert_input.direct_monitor,1);atomic_init(&insert_input.record_source,TS_RECORD_SOURCE_EXT);
    insert_input.channels=8;insert_input.sample_rate=44100;insert_input.input_channel=TS_INPUT_CHANNEL_STEREO;
    insert_input.insert=&audio.sister.insert;
    ts_input_ownership_request(&insert_input.ownership,TS_INPUT_CONSUMER_INSERT);
    audio.output_device_channels=8;audio.sister.insert.output_channels=8;
    TsInsertControls ports={1,1,0,0};ts_sister_runtime_set_insert(&audio.sister,&ports);ui.config.insert=ports;
    ts_insert_capture_prepare(&audio.sister.insert,8,44100,256);
    for(int route=0;route<4;++route) {
        reset_route(route);ui.router_open=ui.insert_open=0;
        if(route==3)assert(ts_sister_runtime_enable(&audio.sister,44100,2,2,1,error,sizeof(error)));
        else ts_sister_runtime_disable(&audio.sister);
        TsSisterParameters p=audio.sister.parameters;p.prism.enabled=1;p.prism.lenses=24;
        p.fx.enabled=1;p.fx.fallout.enabled=1;p.fx.fallout.mix=.2f;
        ts_sister_runtime_set_parameters(&audio.sister,&p);
        ui.mosaic=audio.mosaic=ts_mosaic_create();assert(ui.mosaic);
        TsMosaicSource *source=ts_mosaic_source(ui.mosaic,&instrument.current,error,sizeof(error));assert(source);
        assert(ts_mosaic_add(ui.mosaic,source,0,0));ui.mosaic->playing=ui.mosaic->repeat=1;
        toggle_fm_hold(device,&audio,&ui,&instrument);click_note(0,0);
        TsKeyboardSequenceSettings seq=audio.keyboard_sequence.settings;seq.count=2;seq.notes[0]=60;seq.notes[1]=67;seq.seconds=.04f;
        ts_keyboard_sequence_set(&audio.keyboard_sequence,&seq);
        assert(keyboard_sequence_prepare(device,&audio,&ui,&instrument,&fm,44100,1));assert(ts_keyboard_sequence_play(&audio.keyboard_sequence));
        ts_router_default(&ui.config.router);ui.config.router.bypass_mask=0;router_commit(device,&audio,&ui);
        float block[256*8];double send_energy=0,master_energy=0;
        for(int n=0;n<24;++n) {
            insert_callback_block(block);
            for(int i=0;i<256;++i){send_energy+=fabs(block[i*8+2])+fabs(block[i*8+3]);master_energy+=fabs(block[i*8])+fabs(block[i*8+1]);}
        }
        assert(send_energy>.01 && master_energy>.01);
        TsPerformanceRecorder recorder;ts_performance_recorder_init(&recorder);
        assert(ts_performance_recorder_start(&recorder,"test-insert-out.wav",44100,2,16384,error,sizeof(error)));
        audio.sister_file_recorder=&recorder;atomic_store(&audio.sister_file_tap,TS_SISTER_TAP_MIX);
        TsExternalRecorder mosaic_take;ts_external_recorder_init(&mosaic_take);
        assert(ts_external_recorder_arm_channels(&mosaic_take,1,44100,2,-60,0,5000,0,2,error,sizeof(error)));
        assert(ts_external_recorder_start_manual(&mosaic_take));
        _Atomic int record_source;atomic_init(&record_source,TS_RECORD_SOURCE_OUTPUT);
        audio.record_bank_recorder=&mosaic_take;audio.record_source=&record_source;
        for(int action=0;action<12;++action) {
            int from=0;while(ui.config.router.order[from]!=TS_ROUTER_INSERT)++from;
            ts_router_move(&ui.config.router,from,action%5);
            if(action%3==0)ts_router_toggle_bypass(&ui.config.router,TS_ROUTER_INSERT);
            if(action%3==1)ts_router_toggle_solo(&ui.config.router,TS_ROUTER_INSERT);
            router_commit(device,&audio,&ui);
            size_t before=atomic_load(&recorder.write_cursor);insert_callback_block(block);
            assert(voices()==1 && audio.keyboard_sequence.running && ui.mosaic->playing);
            assert(atomic_load(&recorder.write_cursor)==before+256);
            for(int i=0;i<256;++i)assert(recorder.ring[before+i].l==block[i*8] && recorder.ring[before+i].r==block[i*8+1]);
            for(int i=0;i<256;++i)assert(mosaic_take.buffer[(before+i)*2]==block[i*8] && mosaic_take.buffer[(before+i)*2+1]==block[i*8+1]);
        }
        /* Apply is deferred and refused during recording; gain reset remains usable. */
        ui.router_open=1;insert_panel_open(&ui);ui.insert_offered_outputs=8;ui.insert_inputs=8;
        ui.insert_send_choice=3;ui.insert_apply_pending=1;SDL_AudioDeviceID capture_device=0;
        insert_sync(device,&capture_device,&audio,&insert_input,&ui);
        assert(audio.sister.insert.controls.send_pair==1 && strstr(ui.status,"FINISH RECORDING"));
        ui.insert_send_choice=1;assert(router_mouse(SDL_MOUSEBUTTONDOWN,36,236));assert(router_mouse(SDL_MOUSEBUTTONUP,36,236));
        assert(audio.sister.insert.controls.send_db==-24);
        SDL_Event right={0};right.type=SDL_MOUSEBUTTONDOWN;right.button.windowID=SDL_GetWindowID(window);
        right.button.button=SDL_BUTTON_RIGHT;right.button.x=80;right.button.y=236;
        assert(router_event(&right,window,device,&audio,&ui,&router_panel));assert(audio.sister.insert.controls.send_db==0);
        audio.sister_file_recorder=NULL;ts_performance_recorder_request_stop(&recorder);
        while(ts_performance_recorder_pump(&recorder,16384)){}
        ts_performance_recorder_free(&recorder);remove("test-insert-out.wav");
        audio.record_bank_recorder=NULL;audio.record_source=NULL;ts_external_recorder_free(&mosaic_take);
        if(route==3) {
            ts_router_default(&ui.config.router);ts_router_move(&ui.config.router,4,2);ui.config.router.bypass_mask=0;router_commit(device,&audio,&ui);
            for(int n=0;n<24;++n)insert_callback_block(block);
            ui.fm_open=0;ui.insert_open=0;insert_controller_shot("TS_TEST_INSERT_ROUTER_PPM");
            insert_panel_open(&ui);ui.insert_offered_outputs=8;insert_controller_shot("TS_TEST_INSERT_PANEL_PPM");
        }
        ui.insert_open=ui.router_open=0;ts_mosaic_free(ui.mosaic);ui.mosaic=audio.mosaic=NULL;
    }
    reset_route(0);audio.output_device_channels=2;audio.sister.insert.output_channels=2;
    ts_insert_capture_prepare(&audio.sister.insert,0,44100,0);
    ts_insert_default(&ui.config.insert);ts_sister_runtime_set_insert(&audio.sister,&ui.config.insert);
    ts_router_default(&ui.config.router);router_commit(device,&audio,&ui);
    ts_sister_runtime_set_parameters(&audio.sister,&saved);ts_external_recorder_free(&insert_input.recorder);
}
