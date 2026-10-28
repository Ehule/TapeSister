/* Exercise actual pointer/key dispatch: the former tests called Canvas's
   note handler directly and could not detect Mosaic swallowing QWERTY. */
static int live_performance_event(SDL_Event *event,SDL_Window *window,SDL_AudioDeviceID device,
    AudioState *a,ExternalInputState *s,TsUiState *u,TsInstrument *bank,MosaicController *c)
{
    SDL_AudioDeviceID input=0;
    if(mosaic_live_event(event,window,device,&input,a,s,u))return 1;
    return mosaic_event(event,window,device,a,u,bank,c,&portal,&sister,&transform,48000);
}
static void live_performance_click(SDL_Window *window,SDL_AudioDeviceID device,AudioState *a,
    ExternalInputState *s,TsUiState *u,TsInstrument *bank,MosaicController *c,int x,int y)
{
    SDL_Event event;SDL_zero(event);event.type=SDL_MOUSEBUTTONDOWN;event.button.windowID=SDL_GetWindowID(window);
    event.button.button=SDL_BUTTON_LEFT;event.button.clicks=1;event.button.x=x;event.button.y=y;
    assert(live_performance_event(&event,window,device,a,s,u,bank,c));
    event.type=SDL_MOUSEBUTTONUP;live_performance_event(&event,window,device,a,s,u,bank,c);
}
static void live_performance_key(SDL_Window *window,SDL_AudioDeviceID device,AudioState *a,
    ExternalInputState *s,TsUiState *u,TsInstrument *bank,MosaicController *c,SDL_Keycode key,int down,SDL_Keymod mod)
{
    SDL_Event event;SDL_zero(event);event.type=down?SDL_KEYDOWN:SDL_KEYUP;
    event.key.windowID=SDL_GetWindowID(window);event.key.keysym.sym=key;event.key.keysym.mod=mod;
    assert(live_performance_event(&event,window,device,a,s,u,bank,c));
}
static void live_performance_transport(SDL_Window *window,SDL_AudioDeviceID device,AudioState *a,
    ExternalInputState *s,TsUiState *u,TsInstrument *bank,MosaicController *c,int index)
{
    int x,y;ts_ui_mosaic_live_origin(u,&x,&y);
    live_performance_click(window,device,a,s,u,bank,c,x+8+index*22+10,y+TS_MOSAIC_LIVE_TRANSPORT_Y+10);
}
static void test_mosaic_live_performance_mode(SDL_Window *window,SDL_AudioDeviceID device,int powered)
{
    AudioState *a=routing_audio(powered);a->mosaic->playing=0;
    TsUiState *u=mosaic_record_ui_fixture(a);ExternalInputState *s=mosaic_record_input_fixture(a);
    ts_loop_capture_init(&s->loop_capture);atomic_init(&s->loop_tap,-1);
    a->loop_capture=&s->loop_capture;a->loop_tap=&s->loop_tap;
    TsInstrument *bank=malloc(sizeof(*bank));MosaicController *c=calloc(1,sizeof(*c));assert(bank && c);
    ts_instrument_init(bank);char error[160],folder[160],target[80];SDL_AudioDeviceID input=0;
    TsSample sample={.frames=4800,.sample_rate=48000,.channels=1};sample.data=malloc(sample.frames*sizeof(float));assert(sample.data);
    for(size_t i=0;i<sample.frames;++i)sample.data[i]=.25f;
    assert(ts_instrument_import_sample(bank,&sample,1,0,sample.frames,TS_LOOP_FORWARD,error,sizeof(error)));
    assert(ts_instrument_copy_selected(bank,1,error,sizeof(error)));
    assert(ts_instrument_select_bank(bank,1,error,sizeof(error)));
    for(size_t i=0;i<sample.frames;++i)sample.data[i]=-.5f;
    assert(ts_instrument_import_sample(bank,&sample,1,0,sample.frames,TS_LOOP_FORWARD,error,sizeof(error)));ts_sample_free(&sample);
    bank->bank[1].output_route=(TsSourceRoute){.mode=TS_SOURCE_PAIR,.speaker=0,.second=1,.mix_enabled=1,.clean_level=75,.pan=20};
    assert(ts_instrument_select_bank(bank,0,error,sizeof(error)));
    uint64_t parent=ts_sample_hash(&bank->parent),current=ts_sample_hash(&bank->current);
    snprintf(folder,sizeof(folder),"loop-performance-%lu",(unsigned long)SDL_GetTicks());SDL_setenv("TAPESISTER_CAPTURES",folder,1);
    u->mosaic->live_capture.source=1;u->mosaic->live_capture.length=0;u->mosaic->live_capture.continuous=0;
    mosaic_poll(device,u,bank,c);
    /* Header button is contained in the logo row, with no transport/ruler overlap. */
    live_performance_click(window,device,a,s,u,bank,c,TS_MOSAIC_LIVE_BUTTON_X+10,TS_MOSAIC_LIVE_BUTTON_Y+5);
    assert(u->mosaic_live_open && !u->mosaic_live_advanced && u->mosaic_live_keys);
    assert(!ts_ui_mosaic_live_contains(u,150,100) && ts_ui_mosaic_live_contains(u,600,100));
    assert(ts_ui_midi_target_from_point(u,TS_MOSAIC_LIVE_CX,TS_MOSAIC_LIVE_CY,target,sizeof(target)) && !strcmp(target,"mosaic.live.primary"));
    u->midi_learn_active=1;SDL_Event learn;SDL_zero(learn);learn.type=SDL_MOUSEBUTTONDOWN;
    learn.button.windowID=SDL_GetWindowID(window);learn.button.button=SDL_BUTTON_LEFT;
    learn.button.x=TS_MOSAIC_LIVE_CX;learn.button.y=TS_MOSAIC_LIVE_CY;
    assert(!live_performance_event(&learn,window,device,a,s,u,bank,c) && !s->loop_session);
    learn.type=SDL_KEYDOWN;learn.key.windowID=SDL_GetWindowID(window);learn.key.keysym.sym=SDLK_ESCAPE;
    assert(!live_performance_event(&learn,window,device,a,s,u,bank,c) && u->mosaic_live_open);
    u->midi_learn_active=0;
    /* Every big-button hit means another recording; a selected/running live
       card must never turn this same button into a playback toggle. */
    live_performance_click(window,device,a,s,u,bank,c,TS_MOSAIC_LIVE_CX,TS_MOSAIC_LIVE_CY);
    assert(s->loop_session && s->loop_capture.continuous && u->mosaic->live_capture.continuous);
    live_performance_click(window,device,a,s,u,bank,c,20,85);
    assert(u->mosaic_live_key_valid && u->mosaic_live_key.slot==0 && a->mosaic_keyboard_sample);
    const TsSample *old=a->mosaic_keyboard_sample;
    live_performance_key(window,device,a,s,u,bank,c,SDLK_z,1,KMOD_NONE);
    assert(ts_note_bank_count(&a->notes)==1);
    assert(a->notes.voices[0].sample==old && !a->notes.voices[0].synth && a->notes.voices[0].detached);
    float output[512];for(int block=0;block<8;++block)audio_callback(a,(Uint8 *)output,sizeof(output));
    for(int tries=0;tries<1000 && atomic_load(&s->loop_capture.preview_frames)<2048;++tries)SDL_Delay(1);
    mosaic_live_poll(device,&input,a,s,u,0);
    assert(u->mosaic_live_preview.frames==2048 && u->mosaic_live_rate==48000 && u->mosaic->next_id==2);
    float peak=0;for(int i=0;i<TS_MOSAIC_RECORD_PEAKS;++i)peak=fmaxf(peak,u->mosaic_live_preview.peaks[i]);
    assert(peak>.2f && peak<.3f);
    live_performance_click(window,device,a,s,u,bank,c,TS_MOSAIC_LIVE_CX,TS_MOSAIC_LIVE_CY);
    live_controller_wait(device,&input,a,s,u,3);
    TsMosaicEvent *first=ts_mosaic_find(u->mosaic,2);assert(first && first->live && ts_mosaic_live_count(u->mosaic)==1);
    assert(first->source->sample.data[(first->source->sample.frames-10)*2]>.2f);
    assert(atomic_load(&s->loop_capture.active) && u->mosaic_live_key.slot==0);
    /* New source affects new notes. Existing held notes and the first loop
       keep their exact PCM, tuning, route and playhead. */
    uint64_t source=first->source->hash;double elapsed=u->mosaic->live[first-u->mosaic->events].elapsed;
    live_performance_click(window,device,a,s,u,bank,c,69,85);
    assert(u->mosaic_live_key.slot==1 && a->mosaic_keyboard_sample!=old);
    assert(a->notes.voices[0].sample==old && old->data[0]==.25f);
    assert(first->source->hash==source && u->mosaic->live[first-u->mosaic->events].elapsed==elapsed);
    live_performance_key(window,device,a,s,u,bank,c,SDLK_z,0,KMOD_NONE);
    live_performance_key(window,device,a,s,u,bank,c,SDLK_x,1,KMOD_NONE);
    const TsNoteVoice *voice=ts_note_bank_display_voice(&a->notes);assert(voice && voice->sample==a->mosaic_keyboard_sample && voice->sample->data[0]==-.5f);
    assert(voice->route_tile==bank->bank[1].tile_id && voice->output_route.route.mode==TS_SOURCE_PAIR);
    assert(voice->output_route.route.clean_level==75 && voice->output_route.route.pan==20);
    runtime_note_collect_snapshots(device,a);assert(!old->data && a->mosaic_keyboard_sample->data);
    for(int block=0;block<12;++block)audio_callback(a,(Uint8 *)output,sizeof(output));
    live_performance_click(window,device,a,s,u,bank,c,TS_MOSAIC_LIVE_CX,TS_MOSAIC_LIVE_CY);
    live_controller_wait(device,&input,a,s,u,4);
    TsMosaicEvent *second=ts_mosaic_find(u->mosaic,3);assert(second && second->source!=first->source);
    /* The first loop is audible but not added to DRY KEYS. Route pan/mix does
       not alter its dry reference. Allow the normal note/crossfade handoff. */
    assert(fabsf(second->source->sample.data[(second->source->sample.frames-10)*2]+.5f)<.005f);
    assert(atomic_load(&s->loop_capture.active) && ts_mosaic_live_count(u->mosaic)>=2);
    audio_callback(a,(Uint8 *)output,sizeof(output));
    live_performance_transport(window,device,a,s,u,bank,c,3);
    live_controller_wait(device,&input,a,s,u,5);
    assert(!s->loop_session && ts_mosaic_live_count(u->mosaic)>=3);
    /* The same central action is a stable foot-controller target. Release is
       not another layer boundary, even while an earlier loop is selected. */
    assert(ts_midi_map_assign(&u->config.midi_map,"mosaic.live.primary",(TsMidiSource){TS_MIDI_SOURCE_NOTE,9,61}));
    TsMidiEvent pedal;assert(ts_midi_decode_short_message(0x99,61,100,&pedal));
    assert(handle_midi_mapping(device,a,u,bank,&sister,&input,s,&pedal,48000,2));
    assert(s->loop_session && atomic_load(&s->loop_capture.active));
    audio_callback(a,(Uint8 *)output,sizeof(output));
    assert(ts_midi_decode_short_message(0x89,61,0,&pedal));
    assert(handle_midi_mapping(device,a,u,bank,&sister,&input,s,&pedal,48000,2));
    assert(!atomic_load(&s->loop_capture.produced));
    assert(ts_midi_decode_short_message(0x99,61,100,&pedal));
    assert(handle_midi_mapping(device,a,u,bank,&sister,&input,s,&pedal,48000,2));
    assert(atomic_load(&s->loop_capture.produced)==1 && atomic_load(&s->loop_capture.active));
    mosaic_live_capture_end(device,input,s,1);live_controller_wait(device,&input,a,s,u,6);
    assert(!s->loop_session && ts_mosaic_live_count(u->mosaic)>=4);
    /* Playback and recording remain separate MIDI targets. */
    int px,py;ts_ui_mosaic_live_origin(u,&px,&py);
    assert(ts_ui_midi_target_from_point(u,px+62,py+193,target,sizeof(target)) && !strcmp(target,"mosaic.live.play"));
    assert(ts_note_bank_count(&a->notes)==1);
    live_performance_transport(window,device,a,s,u,bank,c,4);
    for(int block=0;block<3;++block)audio_callback(a,(Uint8 *)output,sizeof(output));
    assert(!ts_mosaic_live_count(u->mosaic) && ts_note_bank_count(&a->notes)==1);
    /* Mode changes and hiding the panel still release the source that began
       a note. Edit shortcuts continue to edit instead of starting new notes. */
    live_performance_click(window,device,a,s,u,bank,c,px+30,py+252);assert(!u->mosaic_live_keys);
    live_performance_key(window,device,a,s,u,bank,c,SDLK_x,0,KMOD_NONE);assert(!ts_note_bank_count(&a->notes));
    live_performance_key(window,device,a,s,u,bank,c,SDLK_z,1,KMOD_NONE);assert(!ts_note_bank_count(&a->notes));
    live_performance_click(window,device,a,s,u,bank,c,px+110,py+252);assert(u->mosaic_live_advanced);
    assert(!main_waveform_detail_allowed(u));
    live_performance_key(window,device,a,s,u,bank,c,SDLK_ESCAPE,1,KMOD_NONE);assert(!u->mosaic_live_advanced && u->mosaic_live_open);
    assert(main_waveform_detail_allowed(u));
    live_performance_key(window,device,a,s,u,bank,c,SDLK_ESCAPE,1,KMOD_NONE);assert(!u->mosaic_live_open);
    assert(bank->selected_slot==0 && ts_sample_hash(&bank->current)==current && ts_sample_hash(&bank->parent)==parent);
    /* Scene replacement clears the selected-source identity; snapshots remain
       owned until any old voices release, never by a deleted Mosaic source. */
    ++u->mosaic->epoch;mosaic_live_keyboard_snapshot(a,u,bank);assert(!u->mosaic_live_key_valid && !a->mosaic_keyboard_sample);
    runtime_note_collect_snapshots(device,a);
    for(int i=0;i<TS_NOTE_BANK_VOICE_CAPACITY;++i)assert(!a->note_snapshots[i].data);
    ts_loop_capture_free(&s->loop_capture);ts_external_recorder_free(&s->recorder);free(s);free(u);
    mosaic_controller_free(c);free(c);ts_instrument_free(bank);free(bank);routing_free(a);SDL_setenv("TAPESISTER_CAPTURES","",1);
}
static void test_mosaic_live_performance(SDL_Window *window,SDL_AudioDeviceID device)
{
    test_mosaic_live_performance_mode(window,device,0);
    test_mosaic_live_performance_mode(window,device,1);
}
