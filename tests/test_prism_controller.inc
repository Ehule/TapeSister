/* Runs through the actual SDL application callback and control dispatcher. */
static void test_prism_points(void)
{
    AudioState *a=routing_audio(0);
    SisterWindow *panel=calloc(1,sizeof(*panel)); assert(panel);
    ts_sister_ui_model_init(&panel->model,&ui.config);
    panel->model.parameters=a->sister.parameters;
    panel->model.parameters.prism.enabled=1;
    panel->model.parameters.prism.drift=0;
    ts_sister_runtime_set_parameters(&a->sister,&panel->model.parameters);
    panel->model.fx_page=3;
    ts_sister_runtime_set_selected_preset(&a->sister,"PRISM TEST");
    panel->window=SDL_CreateWindow("Prism points",0,0,1280,800,SDL_WINDOW_HIDDEN); assert(panel->window);
    panel->window_id=SDL_GetWindowID(panel->window);
    TsPrismView v=ts_prism_control_view(&panel->model.parameters.prism);
    int x,y; ts_sister_ui_prism_point(v.lens[8],&x,&y);
    assert(ts_sister_ui_prism_hit(&panel->model,x,y)==8);
    SDL_Event e={0}; e.type=SDL_MOUSEBUTTONDOWN; e.button.button=SDL_BUTTON_LEFT;
    e.button.x=x*2; e.button.y=y*2;
    assert(sister_prism_event(0,a,panel,&e) && panel->prism_drag_lens==9);
    assert(!a->sister.parameters.prism.pitch_offset[8]); /* No click jump. */
    e.type=SDL_MOUSEMOTION; e.motion.state=SDL_BUTTON_LMASK;
    e.motion.x=(x+80)*2; e.motion.y=(y-70)*2;
    assert(sister_prism_event(0,a,panel,&e));
    assert(a->sister.parameters.prism.pitch_offset[8]>600);
    assert(fabsf(a->sister.parameters.prism.pan_offset[8]-.8f)<.001f);
    assert(!a->sister.parameters.prism.pitch_offset[7]);
    assert(a->sister.selected_preset_modified);
    e.type=SDL_KEYDOWN; e.key.windowID=panel->window_id; e.key.keysym.sym=SDLK_ESCAPE;
    assert(workspace_event(&e,panel->window,0,a,&ui,&instrument,&mosaic,&portal,panel,&transform,&workspace_preview));
    assert(!panel->prism_drag_lens);
    assert(!a->sister.parameters.prism.pitch_offset[8] && !a->sister.parameters.prism.pan_offset[8]);
    e.type=SDL_MOUSEBUTTONDOWN; e.button.button=SDL_BUTTON_LEFT; e.button.x=x*2; e.button.y=y*2;
    assert(sister_prism_event(0,a,panel,&e));
    e.type=SDL_MOUSEMOTION; e.motion.state=SDL_BUTTON_LMASK; e.motion.x=1800; e.motion.y=-100;
    assert(sister_prism_event(0,a,panel,&e)); /* Captured out-of-window drag clamps. */
    assert(a->sister.parameters.prism.pitch_offset[8]==1200);
    e.type=SDL_MOUSEBUTTONUP; e.button.button=SDL_BUTTON_LEFT;
    assert(sister_prism_event(0,a,panel,&e) && !panel->prism_drag_lens);
    float w[512]; for (int i=0;i<200;++i) audio_callback(a,(Uint8 *)w,sizeof(w));
    TsSisterRoutingSnapshot snapshot; assert(ts_sister_runtime_get_snapshot(&a->sister,&snapshot));
    assert(fabsf(snapshot.prism.lens[8].cents-1226)<.1f && snapshot.prism.lens[8].pan>.99f);
    panel->model.routing.prism=snapshot.prism;
    ts_sister_ui_prism_point(snapshot.prism.lens[8],&x,&y);
    e.type=SDL_MOUSEBUTTONDOWN; e.button.button=SDL_BUTTON_RIGHT; e.button.x=x*2; e.button.y=y*2;
    assert(sister_prism_event(0,a,panel,&e));
    assert(!a->sister.parameters.prism.pitch_offset[8] && !a->sister.parameters.prism.pan_offset[8]);
    /* The body anchor is not advertised as a pitch-shifted lens. */
    panel->model.routing.prism.valid=0;
    ts_sister_ui_prism_point(v.lens[0],&x,&y);
    assert(ts_sister_ui_prism_hit(&panel->model,x,y)==-1);
    panel->model.fx_page=0;
    assert(ts_sister_ui_prism_hit(&panel->model,308,167)==-1);
    SDL_DestroyWindow(panel->window); free(panel); routing_free(a);
}

static void test_prism_controller(void)
{
    test_prism_points();
    for (int powered=0; powered<2; ++powered) {
        AudioState *a=routing_audio(powered), *dry=routing_audio(powered);
        SisterWindow *panel=calloc(1,sizeof(*panel)); assert(panel);
        ts_sister_ui_model_init(&panel->model,&ui.config);
        panel->model.parameters=a->sister.parameters;
        panel->model.parameters.monitor_dry=1;
        panel->model.parameters.monitor_wet=0;
        ts_sister_runtime_set_parameters(&a->sister,&panel->model.parameters);
        ts_sister_runtime_set_parameters(&dry->sister,&panel->model.parameters);
        TsSisterUiHit hit={.action=TS_SISTER_UI_ACTION_PAGE};
        for (int page=1; page<=4; ++page) {
            sister_apply_action(0,a,&ui,&instrument,panel,NULL,NULL,hit,48000,2);
            assert(panel->model.fx_page==page%4);
        }
        panel->model.fx_page=3;
        hit=ts_sister_ui_hit_test_model(&panel->model,24,55);
        assert(hit.action==TS_SISTER_UI_ACTION_PRISM_TOGGLE);
        char target[80];
        assert(ts_sister_ui_midi_target(hit,target,sizeof(target)));
        assert(!strcmp(target,"sister.prism.toggle"));
        TsSisterUiHit midi;
        assert(midi_sister_hit_from_target(target,1,&midi));
        sister_apply_action(0,a,&ui,&instrument,panel,NULL,NULL,midi,48000,2);
        assert(a->sister.parameters.prism.enabled);
        for (int i=0; i<8; ++i) {
            hit=ts_sister_ui_hit_test_model(&panel->model,16+(i%4)*154+71,280+(i/4)*28+8);
            assert(hit.action==TS_SISTER_UI_ACTION_PARAMETER && hit.index==TS_SISTER_UI_PARAM_PRISM_LENSES+i);
            assert(ts_sister_ui_midi_target(hit,target,sizeof(target)));
            assert(midi_sister_hit_from_target(target,.75f,&midi));
            sister_apply_action(0,a,&ui,&instrument,panel,NULL,NULL,midi,48000,2);
            float normalized=sister_parameter_normalized(&a->sister.parameters,midi.index);
            assert(fabsf(normalized-.75f)<.051f);
        }
        assert(a->sister.parameters.prism.lenses==10);
        float wheel=sister_parameter_wheel_normalized(&a->sister.parameters,TS_SISTER_UI_PARAM_PRISM_LENSES,1,1);
        sister_set_parameter(&panel->model.parameters,TS_SISTER_UI_PARAM_PRISM_LENSES,wheel);
        assert(panel->model.parameters.prism.lenses==11);
        hit=ts_sister_ui_hit_test_model(&panel->model,145,55);
        assert(hit.action==TS_SISTER_UI_ACTION_PRISM_MODE);
        assert(ts_sister_ui_midi_target(hit,target,sizeof(target)));
        assert(midi_sister_hit_from_target(target,1,&midi));
        sister_apply_action(0,a,&ui,&instrument,panel,NULL,NULL,midi,48000,2);
        assert(a->sister.parameters.prism.mode==TS_PRISM_ENSEMBLE);
        panel->model.parameters.prism.mix=1;
        panel->model.parameters.prism.output_db=0;
        ts_sister_runtime_set_parameters(&a->sister,&panel->model.parameters);
        float w[512],d[512]; double changed=0;
        for (int b=0; b<200; ++b) {
            audio_callback(a,(Uint8 *)w,sizeof(w)); audio_callback(dry,(Uint8 *)d,sizeof(d));
            if (b>100) for (int i=0; i<512; ++i) changed+=fabsf(w[i]-d[i]);
        }
        assert(changed>1);
        TsSisterRoutingSnapshot snapshot;
        assert(ts_sister_runtime_get_snapshot(&a->sister,&snapshot) && snapshot.prism.valid);
        assert(snapshot.prism.wet>.99f && snapshot.prism.lens[10].level>.5f);
        /* REC FILE must capture exact final samples while pages change. */
        ts_performance_recorder_init(&panel->performance_recorder);
        a->sister_file_recorder=&panel->performance_recorder;
        char folder[128],path[1200],error[160];
        snprintf(folder,sizeof(folder),"prism-record-%d-%u",powered,SDL_GetTicks());
        SDL_setenv("TAPESISTER_CAPTURES",folder,1);
        hit=ts_sister_ui_hit_test_model(&panel->model,580,339);
        assert(hit.action==TS_SISTER_UI_ACTION_RECORD_FILE);
        sister_apply_action(0,a,&ui,&instrument,panel,NULL,NULL,hit,48000,2);
        assert(ts_performance_recorder_state(&panel->performance_recorder)==TS_PERFORMANCE_FILE_RECORDING);
        snprintf(path,sizeof(path),"%s",panel->performance_recorder.path);
        static float expected[64*512];
        for (int b=0; b<64; ++b) {
            if (b%8==0) {
                TsSisterUiHit page={.action=TS_SISTER_UI_ACTION_PAGE};
                sister_apply_action(0,a,&ui,&instrument,panel,NULL,NULL,page,48000,2);
            }
            audio_callback(a,(Uint8 *)(expected+b*512),512*sizeof(float));
        }
        sister_apply_action(0,a,&ui,&instrument,panel,NULL,NULL,hit,48000,2);
        Uint32 start=SDL_GetTicks();
        while (ts_performance_recorder_state(&panel->performance_recorder)==TS_PERFORMANCE_FILE_STOPPING &&
               SDL_GetTicks()-start<5000) SDL_Delay(1);
        poll_file_capture_ui(&ui,panel);
        assert(ui.file_record_state==TS_PERFORMANCE_FILE_IDLE);
        TsSample saved={0}; assert(ts_sample_load_wav(&saved,path,error,sizeof(error)));
        assert(saved.frames==64*256 && saved.channels==2);
        for (int i=0; i<64*512; ++i) assert(fabsf(saved.data[i]-expected[i])<1e-6f);
        assert(!panel->model.file_capture_dropped_frames);
        ts_sample_free(&saved); remove(path);
#ifdef _WIN32
        _rmdir(folder);
#else
        rmdir(folder);
#endif
        SDL_setenv("TAPESISTER_CAPTURES","",1);
        ts_performance_recorder_free(&panel->performance_recorder); a->sister_file_recorder=NULL;
        routing_free(a); routing_free(dry); free(panel);
    }
    puts("Prism native controls, MIDI and exact REC FILE capture passed with tape on/off.");
}
