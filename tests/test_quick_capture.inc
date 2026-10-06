static void test_quick_capture(SDL_Window *window,SDL_AudioDeviceID device)
{
    AudioState *a=routing_audio(0);ts_keyboard_sequence_init(&a->keyboard_sequence);
    ExternalInputState *input=mosaic_record_input_fixture(a);
    TsUiState *u=mosaic_record_ui_fixture(a);u->mosaic_open=0;u->show_keyboard=0;
    TsInstrument *bank=malloc(sizeof(*bank));TsSamplePages *pages=malloc(sizeof(*pages));assert(bank&&pages);
    char error[160];ts_instrument_init(bank);assert(ts_sample_pages_init(pages,error,sizeof(error)));
    assert(ts_instrument_select_bank(bank,0,error,sizeof(error)));
    assert(ts_instrument_activate_silence_channels(bank,2048,48000,2,error,sizeof(error)));
    for(size_t i=0;i<2048;++i){bank->current.data[i*2]=.07f;bank->current.data[i*2+1]=-.09f;}
    bank->has_loop=1;bank->loop_first=0;bank->loop_last=2048;
    assert(ts_instrument_sync_selected(bank,error,sizeof(error)));
    assert(ts_instrument_bank_capture(bank,1,TS_BANK_CAPTURE_CURRENT,error,sizeof(error)));
    assert(ts_sister_runtime_shift_sample_tile(&a->sister,bank,0,error,sizeof(error))==TS_SISTER_TILE_SHIFT_SOURCE_ADDED);
    assert(ts_sister_runtime_shift_sample_tile(&a->sister,bank,1,error,sizeof(error))==TS_SISTER_TILE_SHIFT_SOURCE_ADDED);
    uint16_t mask=ts_sister_runtime_source_mask(&a->sister);assert(mask==3 && !a->sister.enabled);
    TsNoteEvent note;assert(ts_note_event_midi(&note,60,127,0));
    assert(runtime_note_start_event(a,bank,&bank->tuning,TS_AUDITION_CURRENT,&note,0,48000)==TS_NOTE_STARTED);
    assert(ts_performance_count(&a->sister.performance)==2 && !ts_note_bank_count(&a->notes));
    u->keyboard_sequence.count=2;u->keyboard_sequence.notes[0]=60;u->keyboard_sequence.notes[1]=64;
    u->keyboard_sequence.seconds=.03;u->keyboard_sequence.gate=.8;u->keyboard_sequence.volume=1;
    ts_keyboard_sequence_set(&a->keyboard_sequence,&u->keyboard_sequence);
    keyboard_sequence_toggle_playback(device,a,u,bank,NULL,48000);
    assert(ts_keyboard_sequence_active(&a->keyboard_sequence));
    assert(a->keyboard_sequence.source->count==2);
    uint64_t hash=ts_sample_hash(&bank->current);const float *current=bank->current.data;
    TsKeyboardSequenceSource *arp=a->keyboard_sequence.source;
    SDL_AudioDeviceID input_device=0;
    char directory[160];snprintf(directory,sizeof(directory),"quick-capture-%lu",(unsigned long)SDL_GetTicks());
    SDL_setenv("TAPESISTER_CAPTURES",directory,1);
    SDL_Event e={0};e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=SDL_GetWindowID(window);
    e.button.x=580;e.button.y=320;e.button.button=SDL_BUTTON_LEFT;
    assert(quick_capture_event(&e,window,device,&input_device,a,input,u,bank,pages));
    assert(input->quick_recording && !input->mosaic_recording && !input_device);
    assert(input->recorder.state==TS_EXTERNAL_CAPTURE_RECORDING && input->recorder.channels==2);
    assert(input->quick_slot==2 && input->quick_page==0 && bank->selected_slot==0);
    assert(a->mosaic->playing && ts_keyboard_sequence_active(&a->keyboard_sequence));
    /* Merely changing workspace must not block or stop a running take. */
    MosaicController *workspace=calloc(1,sizeof(*workspace));
    PortalController *portal_view=calloc(1,sizeof(*portal_view));
    SisterWindow *sister_view=calloc(1,sizeof(*sister_view));
    TransformController *transform_view=calloc(1,sizeof(*transform_view));
    assert(workspace && portal_view && sister_view && transform_view);
    TsSample preview={0};SDL_Event navigation=e;navigation.button.x=325;navigation.button.y=15;
    assert(workspace_event(&navigation,window,device,a,u,bank,workspace,portal_view,sister_view,transform_view,&preview));
    assert(u->mosaic_open && input->quick_recording && ts_keyboard_sequence_active(&a->keyboard_sequence));
    assert(workspace_event(&navigation,window,device,a,u,bank,workspace,portal_view,sister_view,transform_view,&preview));
    assert(!u->mosaic_open && input->quick_recording);
    free(workspace);free(portal_view);free(sister_view);free(transform_view);
    float expected[4096];double before=a->mosaic->time;
    for(int i=0;i<8;++i)audio_callback(a,(Uint8*)(expected+i*512),512*sizeof(float));
    sync_external_capture_ui(device,0,input,u);
    assert(u->capture_recorded_frames==2048 && a->mosaic->time>before);
    assert(u->input_level>0 && u->input_peak>0);
    u->sister_source_mask=mask;u->text_cursor_visible=1;
    TsFramebuffer *fb=malloc(sizeof(*fb));assert(fb);
    ts_ui_render(fb,u,bank);ts_ui_render_file_recording(fb,u);
    assert(fb->pixels[0]==(u->palette.colors[TS_PALETTE_PATTERN_VOLUME]|0xff000000u));
    assert(ts_ui_write_ppm(fb,"quick-capture.ppm"));free(fb);
    e.button.y=389;assert(quick_capture_event(&e,window,device,&input_device,a,input,u,bank,pages));
    assert(input->recorder.state==TS_EXTERNAL_CAPTURE_COMPLETED);
    finalize_external_recording(device,&input_device,a,input,u,bank,pages);
    const TsSample *take=&bank->bank[2].sample;assert(take->frames==2048 && take->channels==2);
    for(size_t i=0;i<4096;++i)assert(fabsf(take->data[i]-expected[i])<.000001f);
    assert(!input->quick_recording && !u->quick_capture_active && bank->selected_slot==0);
    assert(current==bank->current.data && hash==ts_sample_hash(&bank->current));
    assert(a->keyboard_sequence.source==arp && ts_keyboard_sequence_active(&a->keyboard_sequence));
    assert(ts_sister_runtime_source_mask(&a->sister)==mask && ts_performance_count(&a->sister.performance)==2);
    assert(u->captured_tile_slot==2 && u->captured_tile_until_ms);
    /* Cancel a take without changing the group, live instrument or ARP. */
    u->show_keyboard=1;e.button.x=335;e.button.y=320;
    assert(quick_capture_event(&e,window,device,&input_device,a,input,u,bank,pages));
    char canceled[1200];snprintf(canceled,sizeof(canceled),"%s",input->recorder.stream->path);
    audio_callback(a,(Uint8*)expected,512*sizeof(float));
    cancel_external_capture(device,0,input,u);
    FILE *file=fopen(canceled,"rb");assert(!file);
    assert(!bank->bank[3].occupied && ts_keyboard_sequence_active(&a->keyboard_sequence));
    /* An immediate stop discards the zero-frame take and leaves playback alone. */
    quick_capture_button(device,&input_device,a,input,u,bank,pages);
    snprintf(canceled,sizeof(canceled),"%s",input->recorder.stream->path);
    quick_capture_button(device,&input_device,a,input,u,bank,pages);
    assert(!input->quick_recording && !bank->bank[3].occupied);
    file=fopen(canceled,"rb");assert(!file);
    /* FM's footer exposes the same immediate recorder without closing FM. */
    u->fm_open=1;e.button.x=490;e.button.y=389;
    assert(quick_capture_event(&e,window,device,&input_device,a,input,u,bank,pages));
    assert(input->quick_recording && u->fm_open && ts_keyboard_sequence_active(&a->keyboard_sequence));
    audio_callback(a,(Uint8*)expected,512*sizeof(float));
    cancel_external_capture(device,0,input,u);u->fm_open=0;
    /* DRY has silence when only the arrangement runs; manual starts retain it. */
    ts_keyboard_sequence_stop(&a->keyboard_sequence);ts_sister_runtime_panic(&a->sister);
    u->config.quick_capture_source=TS_RECORD_SOURCE_DRY;
    quick_capture_button(device,&input_device,a,input,u,bank,pages);
    audio_callback(a,(Uint8*)expected,512*sizeof(float)); /* ARP fade settles. */
    audio_callback(a,(Uint8*)expected,512*sizeof(float));
    for(size_t i=512;i<1024;++i)assert(fabsf(input->recorder.buffer[i])<.000001f);
    assert(input->recorder.recorded_frames==512 && a->mosaic->playing);
    cancel_external_capture(device,0,input,u);
    /* A protected destination is relocated; existing content is untouched. */
    u->config.quick_capture_source=TS_RECORD_SOURCE_OUTPUT;
    quick_capture_button(device,&input_device,a,input,u,bank,pages);
    int reserved=input->quick_slot;bank->bank[reserved].locked=1;
    audio_callback(a,(Uint8*)expected,512*sizeof(float));
    quick_capture_button(device,&input_device,a,input,u,bank,pages);
    finalize_external_recording(device,&input_device,a,input,u,bank,pages);
    assert(!bank->bank[reserved].occupied && bank->bank[reserved].locked);
    assert(u->captured_tile_slot!=reserved && hash==ts_sample_hash(&bank->current));
    bank->bank[reserved].locked=0;
    /* A full page gains a destination without switching its current source. */
    for(int slot=3;slot<TS_BANK_SLOT_COUNT;++slot)
        if(!bank->bank[slot].occupied)assert(ts_instrument_bank_capture(bank,slot,TS_BANK_CAPTURE_CURRENT,error,sizeof(error)));
    u->config.quick_capture_source=TS_RECORD_SOURCE_OUTPUT;u->config.quick_capture_seconds=1;
    quick_capture_button(device,&input_device,a,input,u,bank,pages);
    assert(input->quick_page==1 && ts_sample_pages_active(pages)==0 && bank->selected_slot==0);
    int destination=input->quick_slot;
    assert(destination==0);
    for(int block=0;block<188;++block)audio_callback(a,(Uint8*)expected,512*sizeof(float));
    assert(input->recorder.state==TS_EXTERNAL_CAPTURE_COMPLETED && input->recorder.recorded_frames==48000);
    finalize_external_recording(device,&input_device,a,input,u,bank,pages);
    assert(pages->pages[1]->bank[destination].sample.frames==48000 && ts_sample_pages_active(pages)==0);
    assert(pages->pages[1]->current.frames==48000);
    assert(hash==ts_sample_hash(&bank->current));
    /* New settings survive a config roundtrip; ordinary defaults are stereo OUT. */
    assert(ts_config_save(&u->config,"quick-capture.ini",error,sizeof(error)));
    TsConfig config;ts_config_init(&config);
    assert(config.quick_capture_source==TS_RECORD_SOURCE_OUTPUT && config.quick_capture_channels==2 && !config.quick_capture_seconds);
    assert(ts_config_load(&config,"quick-capture.ini",error,sizeof(error)));
    assert(config.quick_capture_seconds==1 && config.quick_capture_channels==2);remove("quick-capture.ini");
    TsBrowser *files=calloc(1,sizeof(*files));assert(files);ts_browser_init(files);
    files->mode=TS_BROWSER_LOAD_WAV;snprintf(files->directory,sizeof(files->directory),"%s",directory);
    assert(ts_browser_refresh(files));int count=0;
    for(int i=0;i<files->entry_count;++i)if(!files->entries[i].is_directory) {
        char path[1200];snprintf(path,sizeof(path),"%s/%s",directory,files->entries[i].name);assert(!remove(path));++count;
    }
    assert(count==3);free(files);
#ifdef _WIN32
    _rmdir(directory);
#else
    rmdir(directory);
#endif
    SDL_setenv("TAPESISTER_CAPTURES","",1);
    ts_keyboard_sequence_source_free(a->keyboard_sequence.source);
    ts_sample_pages_free(pages);ts_instrument_free(bank);free(pages);free(bank);free(input);free(u);routing_free(a);
}
