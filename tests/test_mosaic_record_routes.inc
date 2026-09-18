static ExternalInputState *mosaic_record_input_fixture(AudioState *a)
{
    ExternalInputState *input=calloc(1,sizeof(*input));assert(input);
    ts_external_recorder_init(&input->recorder);ts_input_monitor_init(&input->monitor);
    ts_input_activity_init(&input->activity);ts_input_ownership_init(&input->ownership);
    atomic_init(&input->record_source,TS_RECORD_SOURCE_EXT);
    a->record_bank_recorder=&input->recorder;a->record_source=&input->record_source;
    a->fm_output_gain=1;
    return input;
}

static TsUiState *mosaic_record_ui_fixture(AudioState *a)
{
    TsUiState *u=calloc(1,sizeof(*u));assert(u);ts_ui_init(u);
    u->mosaic=a->mosaic;u->mosaic_open=1;
    /* Neither a high threshold nor a very short silence timeout should gate
       either internal mode. These settings still belong to EXT recording. */
    u->config.record_threshold_db=0;u->config.record_silence_ms=1;
    u->config.record_preroll_ms=50;u->config.record_tail_ms=0;u->config.record_max_seconds=2;
    return u;
}

static void mosaic_record_play_keyboard(SDL_AudioDeviceID device,AudioState *a,TsUiState *u,int kind)
{
    if(kind==0) {
        SDL_Event key={0};key.type=SDL_KEYDOWN;key.key.keysym.sym=SDLK_z;
        assert(canvas_qwerty_event(&key,device,a,u,&instrument,48000));
    } else {
        TsNoteEvent note;
        if(kind==1) {
            assert(ts_note_event_qwerty(&note,0,60));
            begin_fm_note_event(device,a,u,&instrument,&instrument.current,&note,48000,0);
        } else {
            assert(ts_note_event_midi(&note,60,96,1));
            if(!a->sister.enabled) {
                a->performance_group_latched=1;
                a->performance_source_mask=(uint16_t)(1u<<instrument.selected_slot);
            }
            begin_note_event(device,a,u,&instrument,&note,48000,0);
        }
    }
    assert(ts_note_bank_count(&a->notes)+ts_performance_count(&a->performance)+
           ts_performance_count(&a->sister.performance)>0);
}

static void test_mosaic_record_routes(SDL_Window *window,SDL_AudioDeviceID device)
{
    for(int powered=0;powered<2;++powered)for(int kind=0;kind<3;++kind) {
        AudioState *wet=routing_audio(powered),*plain=routing_audio(powered);
        TsUiState *wu=mosaic_record_ui_fixture(wet),*pu=mosaic_record_ui_fixture(plain);
        ExternalInputState *wi=mosaic_record_input_fixture(wet),*pi=mosaic_record_input_fixture(plain);
        SDL_AudioDeviceID input_device=0;
        TsSisterParameters p=wet->sister.parameters;
        p.prism.enabled=1;p.prism.lenses=12;p.prism.spread=.8f;p.prism.mix=.7f;p.prism.output_db=6;
        ts_sister_runtime_set_parameters(&wet->sister,&p);
        ts_sister_runtime_set_master_output_gain(&wet->sister,.3f);
        /* The recording must exclude Mosaic even with Sister/Prism processing
           it, while still including sample, FM and grouped MIDI notes. */
        plain->mosaic->playing=0;
        if(powered) {
            assert(ts_sister_runtime_set_source_slot(&wet->sister,&instrument,instrument.selected_slot,1));
            assert(ts_sister_runtime_set_source_slot(&plain->sister,&instrument,instrument.selected_slot,1));
        }
        assert(arm_external_capture_to(device,&input_device,wet,wi,wu,&instrument,1));
        assert(arm_external_capture_to(device,&input_device,plain,pi,pu,&instrument,1));
        assert(wi->recorder.state==TS_EXTERNAL_CAPTURE_RECORDING && wi->recorder.channels==2);
        assert(!input_device && !wi->ownership.requests && !wi->recorder.pre_roll_capacity);
        float out[512],other[512];
        audio_callback(wet,(Uint8 *)out,sizeof(out));audio_callback(plain,(Uint8 *)other,sizeof(other));
        assert(wi->recorder.recorded_frames==256 && wi->recorder.state==TS_EXTERNAL_CAPTURE_RECORDING);
        for(int i=0;i<512;++i)assert(wi->recorder.buffer[i]==0 && pi->recorder.buffer[i]==0);
        mosaic_record_play_keyboard(device,wet,wu,kind);mosaic_record_play_keyboard(device,plain,pu,kind);
        for(int i=0;i<12;++i) {
            audio_callback(wet,(Uint8 *)out,sizeof(out));audio_callback(plain,(Uint8 *)other,sizeof(other));
        }
        assert(wi->recorder.recorded_frames==pi->recorder.recorded_frames);
        double energy=0;
        for(size_t i=0;i<wi->recorder.recorded_frames*2;++i) {
            assert(fabsf(wi->recorder.buffer[i]-pi->recorder.buffer[i])<1e-6f);
            energy+=fabsf(wi->recorder.buffer[i]);
        }
        assert(energy>1);
        cancel_external_capture(device,0,wi,wu);cancel_external_capture(device,0,pi,pu);
        free(wi);free(pi);free(wu);free(pu);routing_free(wet);routing_free(plain);
    }
    /* Record a processed QWERTY performance together with the arrangement.
       Compare every stereo sample with the actual final output buffer. */
    AudioState *a=routing_audio(1);TsUiState *u=mosaic_record_ui_fixture(a);
    ExternalInputState *input=mosaic_record_input_fixture(a);SDL_AudioDeviceID input_device=0;
    assert(ts_sister_runtime_set_source_slot(&a->sister,&instrument,instrument.selected_slot,1));
    TsSisterParameters p=a->sister.parameters;p.prism.enabled=1;p.prism.mix=.8f;p.prism.output_db=12;
    ts_sister_runtime_set_parameters(&a->sister,&p);ts_sister_runtime_set_master_output_gain(&a->sister,.45f);
    u->mosaic_record_source=TS_RECORD_SOURCE_OUTPUT;
    char folder[160],error[160];snprintf(folder,sizeof(folder),"mosaic-output-%lu",(unsigned long)SDL_GetTicks());
    SDL_setenv("TAPESISTER_CAPTURES",folder,1);

    SDL_Event click={0};click.type=SDL_MOUSEBUTTONDOWN;click.button.windowID=SDL_GetWindowID(window);
    click.button.button=SDL_BUTTON_LEFT;click.button.x=TS_MOSAIC_REC_TILE_X+10;click.button.y=45;
    assert(mosaic_record_tile_event(&click,window,device,&input_device,a,input,u,&instrument));
    assert(input->recorder.state==TS_EXTERNAL_CAPTURE_RECORDING && input->recorder.channels==2);
    const int blocks=32;float expected[32*512];double energy=0;uint64_t next=a->mosaic->next_id;
    for(int i=0;i<blocks;++i) {
        if(i==8)mosaic_record_play_keyboard(device,a,u,0);
        audio_callback(a,(Uint8 *)(expected+i*512),512*sizeof(float));
        assert(a->mosaic->next_id==next); /* No card contributes to its own take. */
    }
    assert(input->recorder.recorded_frames==blocks*256);
    for(int i=0;i<blocks*512;++i)assert(input->recorder.buffer[i]==expected[i]);
    for(int i=0;i<8*512;++i)energy+=fabsf(expected[i]);
    assert(energy>1); /* The arrangement alone can bounce, before any note-on. */
    assert(mosaic_record_tile_event(&click,window,device,&input_device,a,input,u,&instrument));
    assert(input->recorder.state==TS_EXTERNAL_CAPTURE_COMPLETED);
    float after[512];audio_callback(a,(Uint8 *)after,sizeof(after));
    assert(input->recorder.recorded_frames==blocks*256 && a->mosaic->next_id==next);
    uint64_t current_hash=ts_sample_hash(&instrument.current);int current_slot=instrument.selected_slot;
    finalize_external_recording(device,&input_device,a,input,u,&instrument,NULL);
    TsMosaicEvent *take=ts_mosaic_find(a->mosaic,u->mosaic_selected);assert(take && take->id==next);
    assert(take->source->sample.frames==blocks*256 && take->source->sample.channels==2 && !take->looping);
    assert(!memcmp(take->source->sample.data,expected,sizeof(expected)));
    int published=0;
    for(int i=0;i<TS_BANK_SLOT_COUNT;++i)if(instrument.bank[i].occupied &&
        ts_sample_hash(&instrument.bank[i].sample)==take->source->hash)++published;
    assert(published==1 && instrument.selected_slot==current_slot && ts_sample_hash(&instrument.current)==current_hash);
    assert(a->mosaic->playing && !u->mosaic_recording);
    TsBrowser *files=calloc(1,sizeof(*files));assert(files);ts_browser_init(files);
    files->mode=TS_BROWSER_LOAD_WAV;snprintf(files->directory,sizeof(files->directory),"%s",folder);
    assert(ts_browser_refresh(files));int found=0;
    for(int i=0;i<files->entry_count;++i)if(!files->entries[i].is_directory) {
        char path[1200];snprintf(path,sizeof(path),"%s/%s",folder,files->entries[i].name);
        assert(strstr(files->entries[i].name,"OUTPUT_"));TsSample saved={0};
        assert(ts_sample_load_wav(&saved,path,error,sizeof(error)));
        assert(saved.frames==blocks*256 && saved.channels==2 && !memcmp(saved.data,expected,sizeof(expected)));
        ts_sample_free(&saved);assert(!remove(path));++found;
    }
    assert(found==1);free(files);
#ifdef _WIN32
    _rmdir(folder);
#else
    rmdir(folder);
#endif
    SDL_setenv("TAPESISTER_CAPTURES","",1);free(input);free(u);routing_free(a);
}
