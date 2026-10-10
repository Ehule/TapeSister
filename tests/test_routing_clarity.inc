/* Exercise the actual track editor, audio callback and pending audio settings. */
static void routing_clarity(const char *directory)
{
    a.output_device_channels=4;
    TsRouterControls old=a.sister.router.controls,main=old;main.master_mix=1;
    ts_sister_runtime_set_router(&a.sister,&main);
    assert(source_route_show(0,&a,&u,&pages,&bank,0,0));
    click(100,89,SDL_BUTTON_LEFT);
    assert(pages.tracker.lanes[0].output_route.mode==TS_SOURCE_MAIN);
    assert(!source_route_window.model.advanced);
    click(32,224,SDL_BUTTON_LEFT);
    assert(pages.tracker.lanes[0].output_route.pan==-100);
    double energy[4];startPlaying(PLAYMODE_SONG,0);render(energy);
    assert(energy[0]>1 && energy[1]==0 && energy[2]==0 && energy[3]==0);
    assert(ts_tapehead_scope_pan(0,128)==0);
    click(344,224,SDL_BUTTON_LEFT);render(energy);
    assert(energy[0]==0 && energy[1]>1 && energy[2]==0 && energy[3]==0);
    assert(ts_tapehead_scope_pan(0,128)==255);
    /* Pan is retained across actual score save/reopen, including the embedded editor. */
    assert(ts_tapehead_export(&pages.tracker,error,sizeof(error)));
    assert(ts_sister_tracker_save_file(&pages.tracker,"clarity-score.tst",error,sizeof(error)));
    ts_tapehead_stop();ts_tapehead_close();
    assert(ts_sister_tracker_load_file(&pages.tracker,"clarity-score.tst",error,sizeof(error)));
    assert(embedded_open(window,0,&a,&u,&pages,&bank,48000));startPlaying(PLAYMODE_SONG,0);
    render(energy);assert(energy[0]==0 && energy[1]>1 && energy[2]==0 && energy[3]==0);
    FILE *legacy=fopen("clarity-score.tst","r+b");assert(legacy && !fseek(legacy,6,SEEK_SET));
    assert(fputc(5,legacy)!=EOF && !fclose(legacy));
    TsSisterTracker *old_score=malloc(sizeof(*old_score));assert(old_score);ts_sister_tracker_init(old_score);
    assert(ts_sister_tracker_load_file(old_score,"clarity-score.tst",error,sizeof(error)));
    assert(old_score->lanes[0].output_route.mode==TS_SOURCE_MAIN && old_score->lanes[0].output_route.pan==0);
    ts_sister_tracker_free(old_score);free(old_score);
    assert(source_route_show(0,&a,&u,&pages,&bank,0,0));
    click(110,224,SDL_BUTTON_LEFT);shot(directory,"track-main-pan");
    click(300,89,SDL_BUTTON_LEFT); /* Reveal does not change the audio route. */
    assert(source_route_window.model.advanced && pages.tracker.lanes[0].output_route.mode==TS_SOURCE_MAIN);
    click(300,89,SDL_BUTTON_LEFT);assert(pages.tracker.lanes[0].output_route.mode==TS_SOURCE_PAIR);
    shot(directory,"advanced-direct-output");
    click(100,89,SDL_BUTTON_LEFT);click(399,20,SDL_BUTTON_LEFT);
    click(100,89,SDL_BUTTON_LEFT);assert(pages.tracker.lanes[0].output_route.mix_enabled);
    click(100,89,SDL_BUTTON_LEFT); /* Disabling sends returns to main, never a dry hardware bypass. */
    assert(pages.tracker.lanes[0].output_route.mode==TS_SOURCE_MAIN);
    click(300,375,SDL_BUTTON_LEFT);assert(source_route_window.model.page==3);
    shot(directory,"main-mix-path");
    /* The master insert is presented last even if an old project stored it first. */
    ts_router_move(&main,4,0);
    for(int i=0;i<4;++i)assert(ts_router_display_stage(&main,i)!=TS_ROUTER_INSERT);
    assert(ts_router_display_stage(&main,4)==TS_ROUTER_INSERT);

    TsConfig config=u.config;
    u.config.audio_backend=ts_started_backend;
    assert(!ts_audio_restart_required(&u.config));
    u.config.insert.send_pair=1;u.config.insert.return_pair=2;
    assert(!ts_audio_restart_required(&u.config)); /* Port choices alone stay live. */
    u.config.audio_backend=ts_started_backend==TS_AUDIO_BACKEND_AUTO?TS_AUDIO_BACKEND_JACK:TS_AUDIO_BACKEND_AUTO;
    assert(ts_audio_restart_required(&u.config));
    ExternalInputState input={0};SDL_AudioDeviceID capture=0;
    u.insert_apply_pending=1;TsInsertControls before=a.sister.insert.controls;
    insert_sync(0,&capture,&a,&input,&u);
    assert(u.insert_restart_required && !memcmp(&before,&a.sister.insert.controls,sizeof(before)));
    assert(strstr(u.status,"RESTART"));
    int asio=ts_asio_selected;ts_asio_selected=1;
    u.config.audio_backend=ts_started_backend;u.config.audio_buffer_frames=ts_output_desired.samples;
    snprintf(u.config.audio_output_device,sizeof(u.config.audio_output_device),"%s",ts_active_output);
    assert(!ts_audio_restart_required(&u.config));
    ++u.config.audio_buffer_frames;assert(ts_audio_restart_required(&u.config));
    u.config.audio_buffer_frames=ts_output_desired.samples;
    snprintf(u.config.audio_output_device,sizeof(u.config.audio_output_device),"Other ASIO driver");
    assert(ts_audio_restart_required(&u.config));ts_asio_selected=asio;
    u.config=config;u.insert_restart_required=0;
    ts_sister_runtime_set_router(&a.sister,&old);
    ts_tapehead_stop();remove("clarity-score.tst");
}
