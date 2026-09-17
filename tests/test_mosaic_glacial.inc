static void test_mosaic_glacial(SDL_Window *window,SDL_AudioDeviceID device)
{
    AudioState *a=routing_audio(0);TsUiState *u=mosaic_record_ui_fixture(a);
    ExternalInputState *input=mosaic_record_input_fixture(a);SDL_AudioDeviceID input_device=0;
    SDL_Event e={0};e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=SDL_GetWindowID(window);
    e.button.button=SDL_BUTTON_LEFT;e.button.x=TS_MOSAIC_LENGTH_X;e.button.y=60;
    assert(mosaic_record_tile_event(&e,window,device,&input_device,a,input,u,&instrument));
    assert(u->config.mosaic_record_seconds==10 && u->config.record_max_seconds==2);
    SDL_Event escape={0};escape.type=SDL_KEYDOWN;escape.key.windowID=SDL_GetWindowID(window);escape.key.keysym.sym=SDLK_ESCAPE;
    assert(mosaic_record_tile_event(&escape,window,device,&input_device,a,input,u,&instrument));
    assert(!u->mosaic_record_length_drag && u->config.mosaic_record_seconds==0);
    assert(mosaic_record_tile_event(&e,window,device,&input_device,a,input,u,&instrument));

    e.type=SDL_MOUSEMOTION;e.motion.windowID=SDL_GetWindowID(window);e.motion.x=TS_MOSAIC_LENGTH_X+TS_MOSAIC_LENGTH_W-1;e.motion.y=60;
    assert(mosaic_record_tile_event(&e,window,device,&input_device,a,input,u,&instrument));
    assert(u->config.mosaic_record_seconds==0);
    e.type=SDL_MOUSEBUTTONUP;e.button.windowID=SDL_GetWindowID(window);e.button.button=SDL_BUTTON_LEFT;
    assert(mosaic_record_tile_event(&e,window,device,&input_device,a,input,u,&instrument));assert(!u->mosaic_record_length_drag);
    e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=SDL_GetWindowID(window);e.button.x=375;e.button.y=26;
    assert(mosaic_record_tile_event(&e,window,device,&input_device,a,input,u,&instrument));
    assert(u->mosaic_open && !u->mosaic_record_length_drag); /* Status is not SAVE. */
    /* Same finite duration survives restarting the recorder, including silence.
       Callback blocks straddle the exact endpoint; no extra samples are saved. */
    a->output_rate=1000;u->config.mosaic_record_seconds=10;
    u->mosaic_record_source=TS_RECORD_SOURCE_OUTPUT;
    const char *shot=getenv("TS_MOSAIC_LENGTH_SCREENSHOT");
    if(shot) {
        static TsFramebuffer frame;u->config.mosaic_record_seconds=240;
        snprintf(u->status,sizeof(u->status),"LENGTH: 10S-60MIN / INF; WHEEL 10S; SHIFT 1S");
        ts_ui_render(&frame,u,&instrument);assert(ts_ui_write_ppm(&frame,shot));
        u->config.mosaic_record_seconds=10;
    }
    for(int take=0;take<2;++take) {
        assert(arm_external_capture_to(device,&input_device,a,input,u,&instrument,1));
        assert(input->recorder.stream && input->recorder.capacity_frames==10000);
        char path[1200];snprintf(path,sizeof(path),"%s",input->recorder.stream->path);
        e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=SDL_GetWindowID(window);e.button.button=SDL_BUTTON_RIGHT;e.button.x=240;e.button.y=60;
        assert(mosaic_record_tile_event(&e,window,device,&input_device,a,input,u,&instrument));
        assert(u->config.mosaic_record_seconds==10 && strstr(u->status,"FINISH"));
        float output[512];
        for(int i=0;i<40;++i){audio_callback(a,(Uint8 *)output,sizeof(output));SDL_Delay(1);}
        assert(input->recorder.state==TS_EXTERNAL_CAPTURE_COMPLETED && input->recorder.recorded_frames==10000);
        assert(!input->recorder.silence_stopped);
        uint64_t next=a->mosaic->next_id;
        finalize_external_recording(device,&input_device,a,input,u,&instrument,NULL);
        TsMosaicEvent *card=ts_mosaic_find(a->mosaic,next);assert(card && card->source->sample.frames==10000);
        assert(card->source->sample.sample_rate==1000 && card->duration==10);
        assert(!u->mosaic_recording && !input->mosaic_writer);
        assert(!remove(path));
    }
    /* Both the standard parameter action and Matrix Step can reach an hour. */
    TsSisterParameters p=a->sister.parameters;
    sister_set_parameter(&p,TS_SISTER_UI_PARAM_PRISM_TIME,1);
    assert(p.prism.morph_seconds==3600 && sister_parameter_normalized(&p,TS_SISTER_UI_PARAM_PRISM_TIME)==1);
    TsConfig config;ts_config_init(&config);config.prism_morph_seconds=3600;config.prism_step_seconds=1200;
    TsSisterUiModel *model=malloc(sizeof(*model));assert(model);ts_sister_ui_model_init(model,&config);
    assert(model->parameters.prism.morph_seconds==3600 && model->parameters.prism.matrix.step_seconds==1200);free(model);
    free(input);free(u);routing_free(a);
}
