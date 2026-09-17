static double power_audio_energy(void)
{
    float output[512];double energy=0;
    for(int block=0;block<24;++block) {
        audio_callback(&audio,(Uint8 *)output,sizeof(output));
        for(int n=0;n<512;++n) {
            assert(isfinite(output[n]));
            if(block>=8)energy+=output[n]*output[n];
        }
    }
    return energy;
}

static void test_keyboard_power_audio(void)
{
    static SisterWindow panel;
    ts_sister_ui_model_init(&panel.model,&ui.config);
    ts_performance_recorder_init(&panel.performance_recorder);
    ui.config.sister_buffer_seconds=1;
    for(int route=0;route<4;++route) {
        reset_route(route==3?0:route);
        ts_sister_runtime_disable(&audio.sister);
        ts_sister_runtime_set_sources(&audio.sister,route==1?TS_SISTER_SOURCE_FM:TS_SISTER_SOURCE_TILES);
        ts_sister_runtime_set_monitor(&audio.sister,1);
        ts_sister_runtime_set_rolling(&audio.sister,1);
        ts_sister_runtime_set_hold(&audio.sister,0);
        TsSisterParameters p=audio.sister.parameters;
        p.monitor_dry=1;p.monitor_wet=0;p.fx.enabled=0;p.prism.enabled=0;
        p.fx.master_transition=ts_sister_fx_transition_normalized(10);
        ts_sister_runtime_set_parameters(&audio.sister,&p);
        panel.model.parameters=audio.sister.parameters;
        TsSisterUiHit power=ts_sister_ui_hit_test_model(&panel.model,20,12);
        assert(power.action==TS_SISTER_UI_ACTION_POWER);
        if(route==3)sister_apply_action(device,&audio,&ui,&instrument,&panel,NULL,NULL,power,44100,2);
        toggle_fm_hold(device,&audio,&ui,&instrument);
        click_note(0,0);click_note(4,1);
        assert(voices()==2 && ui.keyboard_hold);
        double before=power_audio_energy();assert(before>1);
        if(!audio.sister.enabled) {
            ui.config.sister_buffer_seconds=TS_SISTER_MAX_SECONDS+1;
            sister_apply_action(device,&audio,&ui,&instrument,&panel,NULL,NULL,power,44100,2);
            assert(!audio.sister.enabled && ui.keyboard_hold && voices()==2);
            assert(power_audio_energy()>before*.25);
            ui.config.sister_buffer_seconds=1;
        }
        for(int cycle=0;cycle<4;++cycle) {
            TsNoteBank notes=audio.notes;
            TsPerformanceVoice group[TS_PERFORMANCE_VOICE_LIMIT];
            memcpy(group,audio.sister.performance.voices,sizeof(group));
            sister_apply_action(device,&audio,&ui,&instrument,&panel,NULL,NULL,power,44100,2);
            assert(ui.keyboard_hold && voices()==2);
            assert(!memcmp(&notes,&audio.notes,sizeof(notes)));
            assert(!memcmp(group,audio.sister.performance.voices,sizeof(group)));
            double after=power_audio_energy();
            fprintf(stderr,"Power audio: route=%d enabled=%d energy=%.3f\n",route,audio.sister.enabled,after);
            assert(after>before*.25); /* A note count alone missed the silent route. */
        }
        click_note(0,1);click_note(4,0);assert(!voices() && ui.keyboard_hold);
        assert(power_audio_energy()<.0001);
    }
}
