static void test_mosaic_record_feed(ExternalInputState *input,int stereo)
{
    for(int i=0;i<1024;++i)
        ts_external_recorder_write_frame(&input->recorder,(TsStereoFrame){.25f,stereo?-.5f:.25f});
    assert(input->recorder.state==TS_EXTERNAL_CAPTURE_RECORDING);
}

static void test_mosaic_record_tile(SDL_Window *window,SDL_AudioDeviceID device)
{
    AudioState *a=routing_audio(0);TsUiState *u=calloc(1,sizeof(*u));
    ExternalInputState *input=calloc(1,sizeof(*input));assert(u && input);
    ts_ui_init(u);u->mosaic=a->mosaic;u->mosaic_open=1;u->mosaic_scale=24;
    u->config.record_threshold_db=-24;u->config.record_preroll_ms=0;
    u->config.record_silence_ms=30;u->config.record_tail_ms=5;u->config.record_max_seconds=2;
    ts_external_recorder_init(&input->recorder);ts_input_monitor_init(&input->monitor);
    ts_input_activity_init(&input->activity);ts_input_ownership_init(&input->ownership);
    atomic_init(&input->record_source,TS_RECORD_SOURCE_EXT);
    a->record_bank_recorder=&input->recorder;a->record_source=&input->record_source;
    SDL_AudioDeviceID input_device=0;
    uint64_t bank_hash=ts_sample_hash(&instrument.current);
    int bank_slot=instrument.selected_slot;
    uint64_t first_id=u->mosaic->events[0].id;
    double start=u->mosaic->time;
    SDL_Event e={0};e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=SDL_GetWindowID(window);
    e.button.x=TS_MOSAIC_REC_TILE_X+10;e.button.y=45;e.button.button=SDL_BUTTON_RIGHT;
    assert(u->mosaic_record_source==TS_RECORD_SOURCE_DRY);
    assert(mosaic_record_tile_event(&e,window,device,&input_device,a,input,u,&instrument));
    assert(u->mosaic_record_source==TS_RECORD_SOURCE_OUTPUT && u->record_source==TS_RECORD_SOURCE_EXT);
    assert(mosaic_record_tile_event(&e,window,device,&input_device,a,input,u,&instrument));
    assert(u->mosaic_record_source==TS_RECORD_SOURCE_EXT);
    assert(mosaic_record_tile_event(&e,window,device,&input_device,a,input,u,&instrument));
    assert(u->mosaic_record_source==TS_RECORD_SOURCE_DRY);
    /* Retain coverage of the legacy threshold-triggered synth recorder shared
       with REC BANK; the Mosaic UI now offers DRY/OUT/EXT instead. */
    u->mosaic_record_source=TS_RECORD_SOURCE_SYNTH;
    e.button.button=SDL_BUTTON_LEFT;
    u->config_open=1;
    assert(!mosaic_record_tile_event(&e,window,device,&input_device,a,input,u,&instrument));
    u->config_open=0;
    assert(mosaic_record_tile_event(&e,window,device,&input_device,a,input,u,&instrument));
    assert(input->recorder.state==TS_EXTERNAL_CAPTURE_ARMED && u->mosaic_recording);
    assert(u->mosaic->playing && input->mosaic_start==start);
    /* The arrangement recorder can visit FM and return while it is armed. */
    MosaicController navigation={0};TsSample preview=instrument.current;
    navigation.fm_parked=1;navigation.fm_epoch=u->mosaic->epoch;
    u->fm_preview_sample=&preview;u->fm_source_slot=instrument.selected_slot;
    u->fm_source_page=u->sample_page;u->fm_source_hash=bank_hash;
    SDL_Event visit={0};visit.type=SDL_KEYDOWN;visit.key.windowID=SDL_GetWindowID(window);
    visit.key.keysym.sym=SDLK_BACKQUOTE;visit.key.keysym.mod=KMOD_SHIFT;
    assert(workspace_event(&visit,window,device,a,u,&instrument,&navigation,&portal,&sister,&transform,&preview));
    assert(u->fm_open && !u->mosaic_open && u->mosaic_recording);
    visit.key.keysym.mod=KMOD_NONE;
    assert(workspace_event(&visit,window,device,a,u,&instrument,&navigation,&portal,&sister,&transform,&preview));
    assert(u->mosaic_open && !u->fm_open && u->mosaic_recording);
    u->fm_preview_sample=NULL;
    arm_capture(device,a,u,&instrument,48000);
    assert(a->capture.state==TS_CAPTURE_IDLE && strstr(u->status,"MOSAIC REC TILE"));
    assert(!sister_begin_capture(device,a,u,&instrument,&sister,0,48000));
    assert(a->sister.capture.state==TS_CAPTURE_IDLE);
    /* Mosaic's backing track is audible but excluded from SYNTH capture. */
    float output[512];audio_callback(a,(Uint8 *)output,sizeof(output));
    assert(u->mosaic->time>start && input->recorder.state==TS_EXTERNAL_CAPTURE_ARMED);
    assert(!arm_external_capture(device,&input_device,a,input,u,&instrument));
    assert(input->mosaic_recording); /* REC bank cannot steal an armed take. */
    test_mosaic_record_feed(input,0);
    sync_external_capture_ui(device,0,input,u);
    const char *shot=getenv("TS_TEST_MOSAIC_REC_TILE");
    if(shot) {TsFramebuffer *fb=calloc(1,sizeof(*fb));assert(fb);ts_ui_render(fb,u,&instrument);assert(ts_ui_write_ppm(fb,shot));free(fb);}
    char folder[160],error[160];
    snprintf(folder,sizeof(folder),"mosaic-tile-record-%lu",(unsigned long)SDL_GetTicks());
    SDL_setenv("TAPESISTER_CAPTURES",folder,1);
    assert(mosaic_record_tile_event(&e,window,device,&input_device,a,input,u,&instrument));
    assert(input->recorder.state==TS_EXTERNAL_CAPTURE_COMPLETED);
    finalize_external_recording(device,&input_device,a,input,u,&instrument,NULL);
    assert(!input->mosaic_recording && !u->mosaic_recording && strstr(u->status,"ARCHIVED"));
    TsMosaicEvent *take=ts_mosaic_find(u->mosaic,u->mosaic_selected);assert(take && take->id!=first_id);
    uint64_t take_id=take->id;
    assert(take->start==start && !take->looping && take->source->sample.frames==1024);
    assert(take->source->sample.sample_rate==48000 && take->source->sample.channels==1);
    for(int i=0;i<1024;++i)assert(take->source->sample.data[i]==.25f);
    assert(u->mosaic->playing && ts_mosaic_find(u->mosaic,first_id));
    assert(instrument.selected_slot==bank_slot && ts_sample_hash(&instrument.current)==bank_hash);
    assert(ts_mosaic_undo(u->mosaic,0) && !ts_mosaic_find(u->mosaic,take_id));
    assert(ts_mosaic_undo(u->mosaic,1) && ts_mosaic_find(u->mosaic,take_id));
    /* Cancel while armed leaves the scene and its undo history untouched. */
    uint64_t revision=u->mosaic->revision;
    assert(mosaic_record_tile_event(&e,window,device,&input_device,a,input,u,&instrument));
    assert(mosaic_record_tile_event(&e,window,device,&input_device,a,input,u,&instrument));
    assert(!u->mosaic_recording && u->mosaic->revision==revision && !external_capture_busy(input));
    assert(mosaic_record_tile_event(&e,window,device,&input_device,a,input,u,&instrument));
    test_mosaic_record_feed(input,0);
    SDL_Event cancel={0};cancel.type=SDL_KEYDOWN;cancel.key.windowID=SDL_GetWindowID(window);
    cancel.key.keysym.sym=SDLK_ESCAPE;
    assert(mosaic_record_tile_event(&cancel,window,device,&input_device,a,input,u,&instrument));
    assert(!u->mosaic_recording && u->mosaic->revision==revision && !external_capture_busy(input));
    /* Stereo input uses its native rate/channels, completes on silence, and
       preserves an open editor's selection. No real capture device is needed. */
    u->mosaic_record_source=TS_RECORD_SOURCE_EXT;atomic_store(&input->record_source,TS_RECORD_SOURCE_EXT);
    assert(ts_external_recorder_arm_channels(&input->recorder,0,44100,2,-24,0,1,0,2,error,sizeof(error)));
    input->mosaic_recording=u->mosaic_recording=1;input->mosaic_epoch=u->mosaic->epoch;
    input->mosaic_start=2;u->mosaic_editing=u->mosaic_selected=first_id;
    test_mosaic_record_feed(input,1);
    for(int i=0;i<100;++i)ts_external_recorder_write_frame(&input->recorder,(TsStereoFrame){0});
    assert(input->recorder.state==TS_EXTERNAL_CAPTURE_COMPLETED);
    finalize_external_recording(device,&input_device,a,input,u,&instrument,NULL);
    take=ts_mosaic_find(u->mosaic,u->mosaic->next_id-1);assert(take && take->start==2);
    assert(take->source->sample.sample_rate==44100 && take->source->sample.channels==2);
    for(int i=0;i<1024;++i) {
        assert(take->source->sample.data[i*2]==.25f);assert(take->source->sample.data[i*2+1]==-.5f);
    }
    assert(u->mosaic_selected==first_id && u->mosaic_editing==first_id);
    u->mosaic_editing=0;
    /* Replacement scene and full scene both retain the archive without
       inserting into the wrong destination or overwriting another card. */
    for(int full=0;full<2;++full) {
        assert(ts_external_recorder_arm_channels(&input->recorder,0,44100,2,-24,0,1,0,2,error,sizeof(error)));
        input->mosaic_recording=u->mosaic_recording=1;input->mosaic_epoch=u->mosaic->epoch;
        test_mosaic_record_feed(input,1);assert(ts_external_recorder_stop(&input->recorder,error,sizeof(error)));
        if(!full)++u->mosaic->epoch;
        else while(mosaic_record_has_space(u->mosaic))assert(ts_mosaic_add(u->mosaic,take->source,0,0));
        uint64_t next=u->mosaic->next_id;
        finalize_external_recording(device,&input_device,a,input,u,&instrument,NULL);
        assert(!u->mosaic_recording && u->mosaic->next_id==next && strstr(u->status,"ARCHIVED"));
    }
    assert(!arm_external_capture_to(device,&input_device,a,input,u,&instrument,1));
    assert(!external_capture_busy(input) && strstr(u->status,"MOSAIC FULL"));
    TsBrowser *files=calloc(1,sizeof(*files));assert(files);ts_browser_init(files);
    files->mode=TS_BROWSER_LOAD_WAV;snprintf(files->directory,sizeof(files->directory),"%s",folder);
    assert(ts_browser_refresh(files));int archived=0;
    for(int i=0;i<files->entry_count;++i)if(!files->entries[i].is_directory) {
        char path[1200];snprintf(path,sizeof(path),"%s/%s",folder,files->entries[i].name);
        TsSample sample={0};assert(ts_sample_load_wav(&sample,path,error,sizeof(error)));
        assert(sample.frames>=1024);ts_sample_free(&sample);assert(!remove(path));++archived;
    }
    assert(archived==4);free(files);
#ifdef _WIN32
    _rmdir(folder);
#else
    rmdir(folder);
#endif
    SDL_setenv("TAPESISTER_CAPTURES","",1);
    ts_external_recorder_free(&input->recorder);free(input);free(u);routing_free(a);
}
