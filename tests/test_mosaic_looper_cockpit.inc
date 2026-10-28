static void cockpit_pointer(SDL_Window *window,SDL_AudioDeviceID device,AudioState *a,ExternalInputState *s,
    TsUiState *u,TsInstrument *bank,MosaicController *c,int type,int x,int y,int clicks)
{
    SDL_Event e;SDL_zero(e);e.type=(Uint32)type;
    if(type==SDL_MOUSEMOTION){e.motion.windowID=SDL_GetWindowID(window);e.motion.x=x;e.motion.y=y;e.motion.state=SDL_BUTTON_LMASK;}
    else {e.button.windowID=SDL_GetWindowID(window);e.button.x=x;e.button.y=y;e.button.button=SDL_BUTTON_LEFT;e.button.clicks=(Uint8)clicks;}
    assert(live_performance_event(&e,window,device,a,s,u,bank,c));
}
static void test_mosaic_looper_cockpit(SDL_Window *window,SDL_AudioDeviceID device)
{
    AudioState *a=routing_audio(0);a->mosaic->playing=0;
    TsUiState *u=mosaic_record_ui_fixture(a);ExternalInputState *s=mosaic_record_input_fixture(a);
    MosaicController *c=calloc(1,sizeof(*c));assert(c);
    ts_loop_capture_init(&s->loop_capture);atomic_init(&s->loop_tap,-1);a->loop_capture=&s->loop_capture;a->loop_tap=&s->loop_tap;
    u->mosaic_live_open=1;u->mosaic_scale=40;u->mosaic_hscale=1;
    mosaic_poll(device,u,&instrument,c);
    TsMosaicEvent *one=&u->mosaic->events[0];uint64_t id=one->id;
    one->looping=0;one->gain=.42f;one->pan=-.3f;one->fade_in=.01;one->fade_out=.03;
    one->notes[0]=67;one->duration=2.4;one->crossfade=17;
    one->output_route.matrix_enabled=1;one->output_route.matrix_gain[TS_MATRIX_MAIN]=750;
    TsMosaicEvent original=*one;mosaic_select_only(u,id);
    int px,py;ts_ui_mosaic_live_origin(u,&px,&py);
    live_performance_click(window,device,a,s,u,&instrument,c,px+110,py+228);
    assert(one->live && !one->looping && one->id==id && !ts_mosaic_live_count(u->mosaic));
    assert(one->gain==original.gain && one->pan==original.pan && one->fade_in==original.fade_in && one->fade_out==original.fade_out);
    assert(one->source==original.source && one->first==original.first && one->last==original.last && one->crossfade==original.crossfade);
    assert(one->notes[0]==67 && !memcmp(&one->output_route,&original.output_route,sizeof(one->output_route)));
    TsMosaicEvent *two=ts_mosaic_copy(u->mosaic,id,20,230),*three=ts_mosaic_copy(u->mosaic,id,1,110);
    assert(two && three);uint64_t second=two->id,third=three->id;
    TsMosaicEvent *ordinary=ts_mosaic_add(u->mosaic,one->source,0,400);assert(ordinary);ordinary->mode=TS_LOOP_REVERSE;
    uint64_t ordinary_id=ordinary->id;
    /* Renumbering existing cards would break old MIDI mappings. Conversion
       preserves IDs; navigation uses their creation order, independent of XY. */
    live_performance_transport(window,device,a,s,u,&instrument,c,2);
    float output[512];audio_callback(a,(Uint8 *)output,sizeof(output));assert(ts_mosaic_live_count(u->mosaic)==1);
    uint64_t generation=u->mosaic->voices[one-u->mosaic->events].generation;
    live_performance_transport(window,device,a,s,u,&instrument,c,2);audio_callback(a,(Uint8 *)output,sizeof(output));
    assert(u->mosaic->voices[one-u->mosaic->events].generation==generation);
    live_performance_click(window,device,a,s,u,&instrument,c,px+30,py+228);assert(u->mosaic_live_play_all);
    live_performance_transport(window,device,a,s,u,&instrument,c,2);audio_callback(a,(Uint8 *)output,sizeof(output));
    assert(ts_mosaic_live_count(u->mosaic)==3 && u->mosaic->voices[one-u->mosaic->events].generation==generation);
    double main_time=u->mosaic->time;
    live_performance_transport(window,device,a,s,u,&instrument,c,6);assert(u->mosaic_selected==third);
    live_performance_transport(window,device,a,s,u,&instrument,c,5);assert(u->mosaic_selected==third);
    live_performance_transport(window,device,a,s,u,&instrument,c,1);assert(u->mosaic_selected==second);
    live_performance_transport(window,device,a,s,u,&instrument,c,0);assert(u->mosaic_selected==id);
    live_performance_transport(window,device,a,s,u,&instrument,c,1);assert(u->mosaic_selected==id);
    assert(u->mosaic->time==main_time && ts_mosaic_live_count(u->mosaic)==3);
    assert(ts_midi_map_assign(&u->config.midi_map,"mosaic.live.next_loop",(TsMidiSource){TS_MIDI_SOURCE_NOTE,9,62}));
    SDL_AudioDeviceID input=0;TsMidiEvent pedal;assert(ts_midi_decode_short_message(0x99,62,100,&pedal));
    assert(handle_midi_mapping(device,a,u,&instrument,&sister,&input,s,&pedal,48000,2));assert(u->mosaic_selected==second);
    assert(ts_midi_decode_short_message(0x89,62,0,&pedal));
    assert(handle_midi_mapping(device,a,u,&instrument,&sister,&input,s,&pedal,48000,2));assert(u->mosaic_selected==second);
    /* Drag, cancel, clamp, focus-loss release and reset without changing any
       selected card or sounding voice. MIDI hit regions travel with the panel. */
    uint64_t selected=u->mosaic_selected;
    cockpit_pointer(window,device,a,s,u,&instrument,c,SDL_MOUSEBUTTONDOWN,px+20,py+10,1);
    cockpit_pointer(window,device,a,s,u,&instrument,c,SDL_MOUSEMOTION,50,70,0);
    cockpit_pointer(window,device,a,s,u,&instrument,c,SDL_MOUSEBUTTONUP,50,70,1);
    ts_ui_mosaic_live_origin(u,&px,&py);assert(px==30 && py==60 && !u->mosaic_live_drag && u->mosaic_selected==selected);
    char target[80];assert(ts_ui_midi_target_from_point(u,px+83,py+86,target,sizeof(target)) && !strcmp(target,"mosaic.live.primary"));
    for(int i=0;i<TS_MOSAIC_LIVE_TRANSPORT_COUNT;++i) {
        assert(ts_ui_midi_target_from_point(u,px+18+i*22,py+193,target,sizeof(target)));
        char expected[80];snprintf(expected,sizeof(expected),"mosaic.live.%s",ts_ui_mosaic_live_transport_command(i));assert(!strcmp(target,expected));
    }
    assert(!ts_ui_midi_target_from_point(u,TS_MOSAIC_LIVE_CX,TS_MOSAIC_LIVE_CY,target,sizeof(target)));
    cockpit_pointer(window,device,a,s,u,&instrument,c,SDL_MOUSEBUTTONDOWN,px+20,py+10,1);
    cockpit_pointer(window,device,a,s,u,&instrument,c,SDL_MOUSEMOTION,160,110,0);
    live_performance_key(window,device,a,s,u,&instrument,c,SDLK_ESCAPE,1,KMOD_NONE);
    ts_ui_mosaic_live_origin(u,&px,&py);assert(px==30 && py==60 && u->mosaic_live_open && !u->mosaic_live_drag);
    cockpit_pointer(window,device,a,s,u,&instrument,c,SDL_MOUSEBUTTONDOWN,px+20,py+10,1);
    cockpit_pointer(window,device,a,s,u,&instrument,c,SDL_MOUSEMOTION,-1000,1000,0);
    cockpit_pointer(window,device,a,s,u,&instrument,c,SDL_MOUSEBUTTONUP,-1000,1000,1);
    ts_ui_mosaic_live_origin(u,&px,&py);assert(px==0 && py==TS_UI_HEIGHT-TS_MOSAIC_LIVE_H);
    mosaic_poll(device,u,&instrument,c);mosaic_live_keyboard_snapshot(a,u,&instrument);
    const char *shot=getenv("TS_TEST_LIVE_LOOPS");char shot_path[1200];
    if(shot){snprintf(shot_path,sizeof(shot_path),"%s-cockpit-moved.ppm",shot);live_controller_screenshot_size(u,shot_path,1366,768);}
    else live_controller_screenshot_size(u,NULL,1366,768);
    live_controller_screenshot_size(u,NULL,1280,1024);
    cockpit_pointer(window,device,a,s,u,&instrument,c,SDL_MOUSEBUTTONDOWN,px+20,py+10,2);
    ts_ui_mosaic_live_origin(u,&px,&py);assert(px==TS_MOSAIC_LIVE_X && py==TS_MOSAIC_LIVE_Y);
    cockpit_pointer(window,device,a,s,u,&instrument,c,SDL_MOUSEBUTTONDOWN,px+20,py+10,1);
    SDL_Event lost;SDL_zero(lost);lost.type=SDL_WINDOWEVENT;lost.window.windowID=SDL_GetWindowID(window);lost.window.event=SDL_WINDOWEVENT_FOCUS_LOST;
    live_performance_event(&lost,window,device,a,s,u,&instrument,c);assert(!u->mosaic_live_drag);
    /* Position is an ordinary saved UI preference; old configs default right. */
    char error[160];u->config.mosaic_looper_x=30;u->config.mosaic_looper_y=60;
    assert(ts_config_save(&u->config,"looper-position-test.ini",error,sizeof(error)));
    TsConfig *loaded=malloc(sizeof(*loaded));assert(loaded && ts_config_load(loaded,"looper-position-test.ini",error,sizeof(error)));
    assert(loaded->mosaic_looper_x==30 && loaded->mosaic_looper_y==60);free(loaded);remove("looper-position-test.ini");
    u->config.mosaic_looper_x=u->config.mosaic_looper_y=-1;
    /* Preserve a mode we cannot yet play, instead of silently reversing its
       sound when the user asks for an independent loop event. */
    mosaic_select_only(u,ordinary_id);int history=u->mosaic->history_count;
    assert(mosaic_live_command(device,&input,a,s,u,"role"));assert(!ordinary->live && ordinary->mode==TS_LOOP_REVERSE && u->mosaic->history_count==history);
    assert(strstr(u->mosaic_live_status,"FORWARD"));
    char folder[160];snprintf(folder,sizeof(folder),"looper-cockpit-%lu",(unsigned long)SDL_GetTicks());SDL_setenv("TAPESISTER_CAPTURES",folder,1);
    u->mosaic->live_capture.source=1;u->mosaic->live_capture.length=0;
    live_performance_key(window,device,a,s,u,&instrument,c,SDLK_z,1,KMOD_NONE);
    assert(mosaic_live_command(device,&input,a,s,u,"primary"));
    for(int i=0;i<32;++i)audio_callback(a,(Uint8 *)output,sizeof(output));
    for(int tries=0;tries<1000 && atomic_load(&s->loop_capture.preview_frames)<8192;++tries)SDL_Delay(1);
    mosaic_live_poll(device,&input,a,s,u,0);
    assert(u->mosaic_live_preview.frames==8192 && u->mosaic_live_preview.epoch==u->mosaic->epoch);
    float peak=0;for(int i=0;i<TS_MOSAIC_RECORD_PEAKS;++i)peak=fmaxf(peak,u->mosaic_live_preview.peaks[i]);assert(peak>0);
    mosaic_poll(device,u,&instrument,c);mosaic_live_keyboard_snapshot(a,u,&instrument);
    if(shot) {char path[1200];snprintf(path,sizeof(path),"%s-cockpit-recording.ppm",shot);live_controller_screenshot_size(u,path,1280,1024);}
    uint64_t next=u->mosaic->next_id;
    live_performance_transport(window,device,a,s,u,&instrument,c,3);live_controller_wait(device,&input,a,s,u,next+1);
    assert(!s->loop_session && !u->mosaic_live_recording && ts_mosaic_live_count(u->mosaic)>=1);
    double elapsed=u->mosaic->live[one-u->mosaic->events].elapsed;
    for(int i=0;i<300;++i)audio_callback(a,(Uint8 *)output,sizeof(output));
    assert(!s->loop_session && u->mosaic->next_id==next+1 && u->mosaic->live[one-u->mosaic->events].elapsed>elapsed);
    /* Punch in later without restarting any earlier loop. Stop must preserve
       queued complete/partial takes without launching them after publication. */
    assert(mosaic_live_command(device,&input,a,s,u,"primary"));
    for(int i=0;i<8;++i)audio_callback(a,(Uint8 *)output,sizeof(output));
    assert(mosaic_live_command(device,&input,a,s,u,"stop_all") && atomic_load(&s->loop_capture.active)); /* Legacy MIDI target. */
    assert(mosaic_live_command(device,&input,a,s,u,"next"));
    for(int i=0;i<8;++i)audio_callback(a,(Uint8 *)output,sizeof(output));
    next=u->mosaic->next_id;
    live_performance_transport(window,device,a,s,u,&instrument,c,4);live_controller_wait(device,&input,a,s,u,next+2);
    for(int i=0;i<3;++i)audio_callback(a,(Uint8 *)output,sizeof(output));
    assert(!s->loop_session && !ts_mosaic_live_count(u->mosaic) && strstr(u->mosaic_live_status,"LOOPS STOPPED"));
    assert(ts_mosaic_find(u->mosaic,next)->source->sample.frames==2048 && ts_mosaic_find(u->mosaic,next+1)->source->sample.frames==2048);
    assert(ts_mosaic_save(u->mosaic,folder,error,sizeof(error)));
    TsMosaic *reopened=ts_mosaic_create();assert(reopened && ts_mosaic_load(reopened,folder,error,sizeof(error)));
    TsMosaicEvent *kept=ts_mosaic_find(reopened,id);assert(kept && kept->live && !kept->looping && kept->notes[0]==67 && kept->gain==original.gain);
    assert(!memcmp(&kept->output_route,&original.output_route,sizeof(kept->output_route)) && !ts_mosaic_live_count(reopened));ts_mosaic_free(reopened);
    mosaic_select_only(u,id);assert(mosaic_live_command(device,&input,a,s,u,"role"));assert(!one->live && !one->looping && one->id==id);
    ts_mosaic_undo(u->mosaic,0);assert(ts_mosaic_find(u->mosaic,id)->live);ts_mosaic_undo(u->mosaic,1);assert(!ts_mosaic_find(u->mosaic,id)->live);
    live_performance_key(window,device,a,s,u,&instrument,c,SDLK_z,0,KMOD_NONE);
    ts_loop_capture_free(&s->loop_capture);ts_external_recorder_free(&s->recorder);free(s);free(u);mosaic_controller_free(c);free(c);routing_free(a);
    SDL_setenv("TAPESISTER_CAPTURES","",1);
}
