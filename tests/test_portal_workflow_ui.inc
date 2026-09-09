static void test_portal_workflow_ui(SDL_Window *window,SDL_AudioDeviceID device)
{
    static TsInstrument instrument;static TsUiState ui;static AudioState audio;
    static SisterWindow sister;PortalController c;TransformController transform;
    char error[160];ts_instrument_init(&instrument);ts_ui_init(&ui);
    ts_sister_runtime_init(&audio.sister);ts_note_bank_init(&audio.notes);
    ts_performance_init(&audio.performance);ts_performance_init(&audio.tile_launchers);
    ts_capture_init(&audio.capture);ts_audio_mixer_init(&audio.mixer);audio.bank_slot=-1;audio.output_rate=44100;
    ts_performance_recorder_init(&sister.performance_recorder);
    audio.sister_file_recorder=&sister.performance_recorder;transform_controller_init(&transform);
    assert(ts_instrument_generate(&instrument,TS_GENERATOR_METALLIC,999,error,sizeof(error)));
    portal_init(&c,&ui.portal);TsPortalUi *p=&ui.portal;
    snprintf(ui.config.cdp_bin_path,sizeof(ui.config.cdp_bin_path),"%s",getenv("TS_TEST_CDP_BIN"));
    SDL_Event click={0};click.type=SDL_MOUSEBUTTONDOWN;click.button.button=SDL_BUTTON_LEFT;
    click.button.windowID=SDL_GetWindowID(window);click.button.x=478;click.button.y=12;
#define WORK_CLICK(X,Y) do {click.button.x=(X);click.button.y=(Y);assert(portal_event(&click,window,device,&audio,&ui,&instrument,&c,&sister,44100,&transform));}while(0)
    assert(!ui.show_recipes && !p->open);
    selection_screenshot("TS_TEST_PORTAL_MAIN_ACCESS_SCREENSHOT",&ui,&instrument);
    WORK_CLICK(478,12);assert(p->open && p->source && !c.worker && !p->auto_preview);
    ts_portal_recipe_default(&p->recipe,ts_portal_process_find("modify.loudness.1"));
    WORK_CLICK(500,314);assert(p->auto_preview && c.auto_pending);
    Uint32 due=c.auto_due;portal_auto_poll(device,&audio,&ui,&c,due-1);assert(!c.worker);
    p->dragging_parameter=0;portal_parameter_drag(device,&audio,p,&c,0,280);
    portal_auto_poll(device,&audio,&ui,&c,c.auto_due+1000);assert(!c.worker);
    p->dragging_parameter=-1;p->number_focus=0;
    portal_auto_poll(device,&audio,&ui,&c,c.auto_due+1000);assert(!c.worker);
    p->number_focus=-1;p->recipe.values[0]=.5;
    portal_auto_poll(device,&audio,&ui,&c,c.auto_due);assert(c.worker && !c.auto_pending);
    /* New edits cancel the old generation and wait for its worker to exit. */
    PortalWorker *old=c.worker;portal_invalidate(device,&audio,p,&c);p->recipe.values[0]=.25;
    c.auto_due=SDL_GetTicks()+60000;
    portal_auto_poll(device,&audio,&ui,&c,c.auto_due);assert(c.worker==old && SDL_AtomicGet(&old->cancel));
    wait_portal(&audio,&ui,&instrument,&c);assert(!p->valid && c.auto_pending);
    portal_auto_poll(device,&audio,&ui,&c,c.auto_due);assert(c.worker);
    wait_portal(&audio,&ui,&instrument,&c);assert(p->valid && !c.auto_pending);
    assert(fabs(ts_sample_peak(p->result)-.25*ts_sample_peak(p->source))<.0001);
    portal_auto_poll(device,&audio,&ui,&c,SDL_GetTicks()+10000);assert(!c.worker);
    /* Text is committed only on Enter; long entries retain a visible caret. */
    WORK_CLICK(400,270);assert(p->number_focus==0);
    snprintf(p->number_text,sizeof(p->number_text),"0.123456789");
    static TsFramebuffer off,on;ui.text_cursor_visible=0;ts_ui_render(&off,&ui,&instrument);
    ui.text_cursor_visible=1;ts_ui_render(&on,&ui,&instrument);
    int changed=0;for(int y=263;y<276;++y)for(int x=388;x<437;++x)changed+=off.pixels[y*640+x]!=on.pixels[y*640+x];
    assert(changed==9);
    selection_screenshot("TS_TEST_PORTAL_CURSOR_SCREENSHOT",&ui,&instrument);
    SDL_Event key={0};key.type=SDL_KEYDOWN;key.key.windowID=SDL_GetWindowID(window);key.key.keysym.sym=SDLK_RETURN;
    assert(portal_event(&key,window,device,&audio,&ui,&instrument,&c,&sister,44100,&transform));
    assert(p->number_focus<0 && c.auto_pending && fabs(p->recipe.values[0]-.123456789)<1e-9);
    /* Explicit Preview/Cancel takes precedence over queued automatic work. */
    portal_preview(device,&audio,&ui,&c);assert(c.worker && !c.auto_pending);
    portal_preview(device,&audio,&ui,&c);wait_portal(&audio,&ui,&instrument,&c);
    portal_auto_poll(device,&audio,&ui,&c,SDL_GetTicks()+10000);assert(!c.worker && !c.auto_pending);
    /* Choosing processes schedules a new render; filtering alone does not. */
    p->query[0]=0;WORK_CLICK(25,TS_PORTAL_LIST_Y+4);assert(c.auto_pending && !strcmp(p->recipe.process_id,"distort.reverse"));
    portal_auto_poll(device,&audio,&ui,&c,c.auto_due);assert(c.worker);
    wait_portal(&audio,&ui,&instrument,&c);assert(p->valid && !c.auto_pending);
    snprintf(p->query,sizeof(p->query),"LINEAR GAIN");assert(!c.auto_pending);
    WORK_CLICK(25,TS_PORTAL_LIST_Y+4);assert(c.auto_pending && !strcmp(p->recipe.process_id,"modify.loudness.1"));p->query[0]=0;
    /* Invalid recipes make one attempt, not an automatic retry loop. */
    portal_invalidate(device,&audio,p,&c);p->recipe.values[0]=-1;
    portal_auto_poll(device,&audio,&ui,&c,c.auto_due);assert(!c.worker && !c.auto_pending);
    p->recipe.values[0]=.5;portal_invalidate(device,&audio,p,&c);
    WORK_CLICK(500,314);assert(!p->auto_preview && !c.auto_pending);
    portal_auto_poll(device,&audio,&ui,&c,SDL_GetTicks()+10000);assert(!c.worker);
    /* REC captures the actual Portal chord at the final output, without a tile edit. */
    portal_preview(device,&audio,&ui,&c);wait_portal(&audio,&ui,&instrument,&c);assert(p->valid);
    p->listen_result=1;p->loop=1;ts_ui_keyboard_set_octave(&ui,4);
    portal_note_on(device,&audio,&ui,0,44100);portal_note_on(device,&audio,&ui,4,44100);
    assert(ts_note_bank_count(&audio.notes)==2);
    uint64_t original=ts_sample_hash(&instrument.current);
    char folder[160],path[1200];snprintf(folder,sizeof(folder),"/tmp/tapesister-portal-record-%lu",(unsigned long)SDL_GetTicks());
    SDL_setenv("TAPESISTER_CAPTURES",folder,1);
    click.button.x=478;click.button.y=12;
    assert(main_file_capture_event(&click,window,&audio,&ui,&sister,44100));
    assert(ui.file_record_state==TS_PERFORMANCE_FILE_RECORDING && p->open && !audio.sister.enabled);
    snprintf(path,sizeof(path),"%s",sister.performance_recorder.path);
    float output[4096];for(int i=0;i<8;++i)audio_callback(&audio,(Uint8*)(output+i*512),512*sizeof(float));
    poll_file_capture_ui(&ui,&sister);assert(ui.file_record_frames==2048 && ts_note_bank_count(&audio.notes)==2);
    portal_poll(device,&audio,&ui,&instrument,&c);
    ts_ui_render(&on,&ui,&instrument);ts_ui_render_file_recording(&on,&ui);
    assert(on.pixels[0]==ui.palette.colors[TS_PALETTE_PATTERN_VOLUME]);
    const char *shot=getenv("TS_TEST_PORTAL_RECORD_SCREENSHOT");if(shot)assert(ts_ui_write_ppm(&on,shot));
    key.key.keysym.sym=SDLK_f;key.key.keysym.mod=KMOD_CTRL|KMOD_SHIFT;
    assert(main_file_capture_event(&key,window,&audio,&ui,&sister,44100));
    Uint32 start=SDL_GetTicks();while(ts_performance_recorder_state(&sister.performance_recorder)==TS_PERFORMANCE_FILE_STOPPING && SDL_GetTicks()-start<5000)SDL_Delay(1);
    poll_file_capture_ui(&ui,&sister);assert(ui.file_record_state==TS_PERFORMANCE_FILE_IDLE && strstr(p->message,"SAVED"));
    TsSample saved;ts_sample_init(&saved);assert(ts_sample_load_wav(&saved,path,error,sizeof(error)));
    assert(saved.channels==2 && saved.frames==2048);double energy=0;
    for(int i=0;i<4096;++i) {assert(fabs(saved.data[i]-output[i])<.000001);energy+=fabs(output[i]);}assert(energy>0);
    assert(ts_sample_hash(&instrument.current)==original && ts_note_bank_count(&audio.notes)==2);
    ts_sample_free(&saved);remove(path);
#ifdef _WIN32
    _rmdir(folder);
#else
    rmdir(folder);
#endif
    SDL_setenv("TAPESISTER_CAPTURES","",1);
    p->auto_preview=1;portal_auto_schedule(p,&c);portal_close(device,&audio,&ui,&c);
    portal_auto_poll(device,&audio,&ui,&c,SDL_GetTicks()+10000);assert(!c.worker && !c.auto_pending);
    portal_free(&c);ts_instrument_free(&instrument);ts_performance_recorder_free(&sister.performance_recorder);
    ts_sister_runtime_free(&audio.sister);ts_performance_free(&audio.performance);
    ts_performance_free(&audio.tile_launchers);ts_capture_free(&audio.capture);
#undef WORK_CLICK
}
