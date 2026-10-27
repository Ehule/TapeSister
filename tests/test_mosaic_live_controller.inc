static void live_controller_wait(SDL_AudioDeviceID device,SDL_AudioDeviceID *input_device,AudioState *a,ExternalInputState *s,TsUiState *u,uint64_t next)
{
    for(int tries=0;tries<2000 && (u->mosaic->next_id<next || (!atomic_load(&s->loop_capture.active) && s->loop_session));++tries) {
        mosaic_live_poll(device,input_device,a,s,u,0);SDL_Delay(1);
    }
    assert(u->mosaic->next_id>=next);
}
static void live_controller_screenshot(TsUiState *u,const char *path)
{
    SDL_Window *window=SDL_CreateWindow("Live loop native fixture",0,0,1280,800,SDL_WINDOW_HIDDEN);assert(window);
    SDL_Renderer *renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);assert(renderer);
    SDL_Texture *base=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,640,400);assert(base);
    TsUiWaveformDetail details[2]={0};SDL_Texture *textures[2]={0};TsFramebuffer *fb=calloc(1,sizeof(*fb));assert(fb);
    native_waveform_prepare(renderer,details,main_waveform_detail_allowed(u));ts_audio_ui_render(fb,u,&instrument);
    assert(!SDL_UpdateTexture(base,NULL,fb->pixels,640*sizeof(uint32_t)));
    assert(!SDL_RenderCopy(renderer,base,NULL,NULL));native_waveform_copy(renderer,textures,details,fb);
    uint32_t *pixels=malloc(1280*800*sizeof(*pixels));assert(pixels);
    assert(!SDL_RenderReadPixels(renderer,NULL,SDL_PIXELFORMAT_ARGB8888,pixels,1280*sizeof(*pixels)));
    FILE *file=fopen(path,"wb");assert(file);fprintf(file,"P6\n1280 800\n255\n");
    for(int i=0;i<1280*800;++i){unsigned char rgb[]={(unsigned char)(pixels[i]>>16),(unsigned char)(pixels[i]>>8),(unsigned char)pixels[i]};assert(fwrite(rgb,1,3,file)==3);}
    assert(!fclose(file));free(pixels);free(fb);
    for(int i=0;i<2;++i){ts_ui_waveform_detail_free(&details[i]);SDL_DestroyTexture(textures[i]);}
    SDL_DestroyTexture(base);SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);
}
static void test_mosaic_live_controller(SDL_Window *window,SDL_AudioDeviceID device)
{
    AudioState *a=routing_audio(0);TsUiState *u=mosaic_record_ui_fixture(a);
    ExternalInputState *s=mosaic_record_input_fixture(a);ts_loop_capture_init(&s->loop_capture);atomic_init(&s->loop_tap,-1);
    a->loop_capture=&s->loop_capture;a->loop_tap=&s->loop_tap;
    SDL_AudioDeviceID input_device=0;char folder[160],target[80];
    snprintf(folder,sizeof(folder),"live-loops-%lu",(unsigned long)SDL_GetTicks());SDL_setenv("TAPESISTER_CAPTURES",folder,1);
    u->mosaic->events[0].live=1;u->mosaic->playing=0;ts_mosaic_live_launch(u->mosaic,u->mosaic->events[0].id,0);
    u->mosaic->live_capture.source=1;u->mosaic->live_capture.length=0;u->mosaic->live_capture.continuous=1;
    u->mosaic_live_open=1;
    /* Every performance command is a learnable trigger; learning a pedal does
       not start recording, and release does not finish/advance a take. */
    assert(ts_ui_midi_target_from_point(u,140,112,target,sizeof(target)) && !strcmp(target,"mosaic.live.record"));
    assert(ts_midi_map_assign(&u->config.midi_map,target,(TsMidiSource){TS_MIDI_SOURCE_NOTE,9,60}));
    TsMidiEvent pedal;assert(ts_midi_decode_short_message(0x99,60,100,&pedal));
    assert(handle_midi_mapping(device,a,u,&instrument,&sister,&input_device,s,&pedal,48000,2));
    assert(s->loop_session && atomic_load(&s->loop_capture.active));
    assert(atomic_load(&s->loop_tap)==1);
    SDL_Event source_change;SDL_zero(source_change);source_change.type=SDL_MOUSEBUTTONDOWN;
    source_change.button.windowID=SDL_GetWindowID(window);source_change.button.button=SDL_BUTTON_LEFT;
    source_change.button.x=140;source_change.button.y=145;
    assert(mosaic_live_event(&source_change,window,device,&input_device,a,s,u) && u->mosaic->live_capture.source==1);
    float out[512];audio_callback(a,(Uint8 *)out,sizeof(out));audio_callback(a,(Uint8 *)out,sizeof(out));
    double audible=0;for(int i=0;i<512;++i)audible+=fabsf(out[i]);assert(audible>1);
    assert(ts_midi_decode_short_message(0x89,60,0,&pedal));
    assert(handle_midi_mapping(device,a,u,&instrument,&sister,&input_device,s,&pedal,48000,2));
    assert(!atomic_load(&s->loop_capture.produced));
    assert(ts_midi_decode_short_message(0x99,60,100,&pedal));
    assert(handle_midi_mapping(device,a,u,&instrument,&sister,&input_device,s,&pedal,48000,2));
    for(int tries=0;tries<100 && atomic_load(&s->loop_capture.file.flushed_frames)<512;++tries)SDL_Delay(1);
    mosaic_live_poll(device,&input_device,a,s,u,1);assert(u->mosaic->next_id==2); /* Drag defers publication, not capture. */
    live_controller_wait(device,&input_device,a,s,u,3);
    TsMosaicEvent *first=ts_mosaic_find(u->mosaic,2);assert(first && first->live && first->source->sample.frames==512);
    for(int i=0;i<1024;++i)assert(first->source->sample.data[i]==0); /* Dry never recaptures running loops. */
    assert(atomic_load(&s->loop_capture.active));
    mosaic_record_play_keyboard(device,a,u,0);
    for(int i=0;i<12;++i)audio_callback(a,(Uint8 *)out,sizeof(out));
    assert(mosaic_live_command(device,&input_device,a,s,u,"next"));live_controller_wait(device,&input_device,a,s,u,4);
    TsMosaicEvent *second=ts_mosaic_find(u->mosaic,3);assert(second && second->source->sample.frames==3072 && second->source!=first->source);
    double energy=0;for(int i=0;i<6144;++i)energy+=fabsf(second->source->sample.data[i]);assert(energy>1);
    audio_callback(a,(Uint8 *)out,sizeof(out));
    assert(mosaic_live_command(device,&input_device,a,s,u,"stop_capture"));live_controller_wait(device,&input_device,a,s,u,5);
    assert(!s->loop_session && !u->mosaic_live_recording && ts_mosaic_live_count(u->mosaic)>=3);
    TsMosaicEvent *partial=ts_mosaic_find(u->mosaic,4);assert(partial && partial->source->sample.frames==256);
    assert(atomic_load(&s->loop_tap)==-1);
    /* Switching source between sessions publishes exactly one producer only
       after setup. OUTPUT deliberately contains the actual running mix. */
    u->mosaic->live_capture.source=2;u->mosaic->live_capture.continuous=0;
    assert(mosaic_live_command(device,&input_device,a,s,u,"record") && atomic_load(&s->loop_tap)==2);
    audio_callback(a,(Uint8 *)out,sizeof(out));
    for(int tries=0;tries<1000 && atomic_load(&s->loop_capture.file.flushed_frames)<256;++tries)SDL_Delay(1);
    TsSample captured={0};char read_error[160];
    assert(ts_performance_recorder_load_range(&s->loop_capture.file,0,256,&captured,read_error,sizeof(read_error)));
    for(int i=0;i<512;++i)assert(captured.data[i]==out[i]);ts_sample_free(&captured);
    mosaic_live_capture_end(device,input_device,s,1);live_controller_wait(device,&input_device,a,s,u,5);
    assert(!s->loop_session && atomic_load(&s->loop_tap)==-1 && u->mosaic->next_id==5);
    u->mosaic->live_capture.source=1;u->mosaic->live_capture.continuous=1;
    /* The real callback binds master timing even with Mosaic paused. Tempo
       following changes rate without restarting the player's generation. */
    TsMosaicEvent *base=&u->mosaic->events[0];base->live_tempo=1;base->live_beats=ts_mosaic_live_cycle(base)/.5;
    a->sister.clock_controls.enabled=1;a->sister.clock_controls.bpm=120;
    ts_mosaic_live_restart(u->mosaic,base->id);audio_callback(a,(Uint8 *)out,sizeof(out));
    double elapsed=u->mosaic->live[0].elapsed;uint64_t generation=u->mosaic->voices[0].generation;
    a->sister.clock_controls.bpm=240;audio_callback(a,(Uint8 *)out,sizeof(out));
    assert(fabs(u->mosaic->live[0].elapsed-elapsed-512.0/48000)<1e-9 && u->mosaic->voices[0].generation==generation);
    /* The same pedal target works from another workspace. Stop all is a
       separate action and does not stop note playing or the main timeline. */
    u->mosaic_open=0;assert(main_waveform_detail_allowed(u));
    assert(mosaic_live_command(device,&input_device,a,s,u,"stop_all"));
    for(int i=0;i<3;++i)audio_callback(a,(Uint8 *)out,sizeof(out));assert(!ts_mosaic_live_count(u->mosaic));
    assert(ts_note_bank_count(&a->notes)>0);
    u->mosaic_open=1;u->mosaic_live_open=0;u->mosaic_scale=24;u->mosaic_hscale=1;
    u->mosaic_scroll=u->mosaic_xscroll=0;mosaic_select_only(u,second->id);
    snprintf(target,sizeof(target),"mosaic.live.%llu.toggle",(unsigned long long)second->id);
    assert(midi_apply_target(device,a,u,&instrument,&sister,&input_device,s,target,1,48000,2));
    assert(u->mosaic->live[second-u->mosaic->events].state==TS_MOSAIC_LIVE_PLAYING);
    /* Native rendered fixture uses the actual app UI and snapshot, with both
       the panel and each loop's independent state available for visual QA. */
    MosaicController controller={0};controller.ui_epoch=u->mosaic->epoch;mosaic_poll(device,u,&instrument,&controller);
    const char *shot=getenv("TS_TEST_LIVE_LOOPS");
    if(shot) {
        u->mosaic_scale=100;for(int i=0;i<4;++i)u->mosaic->events[i].duration=2;
        u->mosaic_live_open=0;char path[1200];
        snprintf(path,sizeof(path),"%s-cards.ppm",shot);live_controller_screenshot(u,path);
        u->mosaic_live_open=1;snprintf(path,sizeof(path),"%s-controls.ppm",shot);live_controller_screenshot(u,path);
    }
    ts_loop_capture_free(&s->loop_capture);ts_external_recorder_free(&s->recorder);
    free(s);free(u);routing_free(a);
    /* EXT uses the selected raw input rather than monitor/output. Exercise the
       real input callback at 44.1 kHz stereo, splitting inside an input block. */
    a=routing_audio(1);u=mosaic_record_ui_fixture(a);s=mosaic_record_input_fixture(a);
    ts_loop_capture_init(&s->loop_capture);atomic_init(&s->loop_tap,-1);ts_performance_inputs_init(&s->performance);
    a->loop_capture=&s->loop_capture;a->loop_tap=&s->loop_tap;
    s->channels=2;s->input_channel=TS_INPUT_CHANNEL_STEREO;s->sample_rate=44100;s->range=(TsAudioChannelRange){1,2};
    char path[200],error[160];snprintf(path,sizeof(path),"%s/external.wav",folder);
    assert(ts_loop_capture_start(&s->loop_capture,path,44100,2,100,1,error,sizeof(error)));
    s->loop_epoch=u->mosaic->epoch;s->loop_session=1;s->loop_quarter=.5;
    s->loop_writer=SDL_CreateThread(sister_performance_writer_main,"test-loop-writer",&s->loop_capture.file);assert(s->loop_writer);
    atomic_store_explicit(&s->loop_tap,0,memory_order_release);
    float input[500];for(int i=0;i<250;++i){input[i*2]=.25f;input[i*2+1]=-.5f;}
    external_input_callback(s,(Uint8 *)input,sizeof(input));assert(s->loop_capture.frames==250);
    mosaic_live_capture_end(device,0,s,1);live_controller_wait(device,&input_device,a,s,u,4);
    assert(!s->loop_session && u->mosaic->next_id==4); /* 2 complete layers; discarded 50-frame partial. */
    for(int card=1;card<=2;++card) {
        TsSample *sample=&u->mosaic->events[card].source->sample;assert(sample->frames==100 && sample->sample_rate==44100 && sample->channels==2);
        for(int i=0;i<100;++i){assert(sample->data[i*2]==.25f && sample->data[i*2+1]==-.5f);}
    }
    ts_loop_capture_free(&s->loop_capture);ts_external_recorder_free(&s->recorder);free(s);free(u);routing_free(a);
    SDL_setenv("TAPESISTER_CAPTURES","",1);
}
