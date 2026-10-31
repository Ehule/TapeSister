static void gallery_pointer(SDL_Window *window,SDL_AudioDeviceID device,AudioState *a,ExternalInputState *s,
    TsUiState *u,MosaicController *c,int x,int y)
{live_performance_click(window,device,a,s,u,&instrument,c,x,y);}
static void gallery_key(SDL_Window *window,SDL_AudioDeviceID device,AudioState *a,ExternalInputState *s,
    TsUiState *u,MosaicController *c,SDL_Keycode key,SDL_Keymod mod)
{live_performance_key(window,device,a,s,u,&instrument,c,key,1,mod);}
static void test_mosaic_gallery_controller(SDL_Window *window,SDL_AudioDeviceID device)
{
    SDL_SetWindowSize(window,640,400);
    AudioState *a=routing_audio(0);TsUiState *u=mosaic_record_ui_fixture(a);
    ExternalInputState *s=mosaic_record_input_fixture(a);MosaicController *c=calloc(1,sizeof(*c));assert(c);
    TsSamplePages pages;char error[256];assert(ts_sample_pages_init(&pages,error,sizeof(error)));
    pages.mosaic=a->mosaic;assert(ts_sample_pages_enable_gallery(&pages,error,sizeof(error)));
    TsMosaicGallery *g=pages.gallery;a->gallery=g;u->mosaic_gallery=g;c->pages=&pages;
    g->fade_seconds=.1;u->mosaic_follow=0;u->mosaic_scroll=3;u->mosaic_xscroll=22;u->mosaic_scale=31;u->mosaic_hscale=1.2;
    TsMosaic *first=a->mosaic;
    TsMosaicEvent *bg=ts_mosaic_copy(first,first->events[0].id,0,150);assert(bg);bg->live=1;bg->live_order=-1;
    assert(ts_mosaic_live_launch(first,bg->id,0));
    mosaic_poll(device,u,&instrument,c);
    /* A held selected-source layer stays owned until physical key-up. */
    source_keys_click(window,device,a,s,u,&instrument,c,instrument.selected_slot,KMOD_NONE);
    gallery_pointer(window,device,a,s,u,c,TS_MOSAIC_SOURCE_KEYS_X+5,TS_MOSAIC_SOURCE_KEYS_Y+5);assert(u->mosaic_source_keys);
    gallery_key(window,device,a,s,u,c,SDLK_z,KMOD_NONE);assert(ts_performance_count(&a->mosaic_source_performance)>0);
    gallery_pointer(window,device,a,s,u,c,184,12);assert(u->mosaic_gallery_open && ts_ui_foreground_panel_open(u));
    gallery_pointer(window,device,a,s,u,c,156,330);assert(g->count==2 && g->active==0 && u->mosaic_gallery_selected==1);
    gallery_pointer(window,device,a,s,u,c,245,330);assert(u->mosaic_gallery_rename);
    gallery_key(window,device,a,s,u,c,SDLK_a,KMOD_CTRL);
    SDL_Event text={0};text.type=SDL_TEXTINPUT;snprintf(text.text.text,sizeof(text.text.text),"Blue room");
    assert(live_performance_event(&text,window,device,a,s,u,&instrument,c));
    gallery_key(window,device,a,s,u,c,SDLK_RETURN,KMOD_NONE);
    assert(!u->mosaic_gallery_rename && !strcmp(g->canvas[1].name,"Blue room"));
    TsMosaic *second=g->canvas[1].scene;
    TsMosaicSource *source=ts_mosaic_source(second,&instrument.current,error,sizeof(error));assert(source);
    TsMosaicEvent *card=ts_mosaic_add(second,source,0,200);assert(card);card->live=1;card->duration=60;
    second->live_envelope=1;second->markers[3]=(TsMosaicMarker){.stored=1,.time=.5};
    mosaic_poll(device,u,&instrument,c);
    assert(!main_waveform_detail_allowed(u));
    TsUiLooperCircle hidden_circle={0};u->mosaic_live_open=1;
    assert(!ts_ui_mosaic_looper_circle(&hidden_circle,u,1280,800));
    ts_ui_mosaic_looper_circle_free(&hidden_circle);
    live_controller_screenshot_size(u,NULL,1637,947);u->mosaic_live_open=0;
    const char *shot=getenv("TS_TEST_GALLERY");if(shot){char path[1200];snprintf(path,sizeof(path),"%s-gallery.ppm",shot);live_controller_screenshot_size(u,path,1280,800);}
    u->mosaic_recording=1;gallery_pointer(window,device,a,s,u,c,432,330);
    assert(g->active==0 && u->mosaic_gallery_open && strstr(u->mosaic_gallery_status,"TAKE"));u->mosaic_recording=0;
    /* The actual file recorder remains active across the crossfade. */
    SisterWindow *rec=calloc(1,sizeof(*rec));assert(rec);ts_performance_recorder_init(&rec->performance_recorder);
    a->sister_file_recorder=&rec->performance_recorder;
    char folder[128],recorded[1200];snprintf(folder,sizeof(folder),"gallery-record-%lu",(unsigned long)SDL_GetTicks());SDL_setenv("TAPESISTER_CAPTURES",folder,1);
    assert(begin_file_capture(a,rec,48000,TS_SISTER_TAP_MIX,2));poll_file_capture_ui(u,rec);
    snprintf(recorded,sizeof(recorded),"%s",rec->performance_recorder.path);
    gallery_pointer(window,device,a,s,u,c,432,330);
    assert(!u->mosaic_gallery_open && g->active==1 && a->mosaic==second && u->mosaic==second && pages.mosaic==second);
    assert(g->outgoing==0 && second->time==0 && !u->mosaic_source_keys && ts_performance_count(&a->mosaic_source_performance)>0);
    float output[512];int callbacks=0;
    audio_callback(a,(Uint8 *)output,sizeof(output));++callbacks;
    assert(first->time>.1 && second->time>0 && second->live[0].elapsed>0);
    /* Mosaic and F11 controls reach both audible canvases during a fade. */
    mosaic_transport(device,u,0);double old_time=first->time,new_time=second->time;
    audio_callback(a,(Uint8 *)output,sizeof(output));++callbacks;
    assert(first->time==old_time && second->time==new_time);
    assert(performance_command_for(PERF_LOOPS,0,window,device,a,u,&pages,&instrument,NULL,48000));
    assert(first->live_paused && second->live_paused);
    assert(performance_command_for(PERF_LOOPS,1,window,device,a,u,&pages,&instrument,NULL,48000));
    mosaic_transport(device,u,0);assert(first->playing && second->playing);
    for(int i=0;i<24;++i){audio_callback(a,(Uint8 *)output,sizeof(output));++callbacks;}
    assert(g->outgoing<0 && first->live_paused && !first->playing);
    double parked=first->time,loop=first->live[bg-first->events].elapsed;
    for(int i=0;i<4;++i){audio_callback(a,(Uint8 *)output,sizeof(output));++callbacks;}
    assert(first->time==parked && first->live[bg-first->events].elapsed==loop);
    live_performance_key(window,device,a,s,u,&instrument,c,SDLK_z,0,KMOD_NONE);
    assert(!ts_performance_count(&a->mosaic_source_performance));
    gallery_key(window,device,a,s,u,c,SDLK_g,KMOD_CTRL);assert(u->mosaic_gallery_open);
    gallery_pointer(window,device,a,s,u,c,185,113);gallery_pointer(window,device,a,s,u,c,190,284);
    assert(g->canvas[0].entry==TS_GALLERY_RESUME);
    gallery_pointer(window,device,a,s,u,c,430,330);
    assert(g->active==0 && first->time==parked && first->live[bg-first->events].elapsed==loop);
    assert(u->mosaic_scroll==3 && u->mosaic_xscroll==22 && u->mosaic_scale==31 && u->mosaic_hscale==1.2);
    for(int i=0;i<24;++i){audio_callback(a,(Uint8 *)output,sizeof(output));++callbacks;}
    assert(g->outgoing<0 && first->time>parked);
    g->canvas[1].entry=TS_GALLERY_MARKER;g->canvas[1].marker=3;
    gallery_key(window,device,a,s,u,c,SDLK_PAGEDOWN,KMOD_CTRL);
    assert(g->active==1 && second->time==.5 && second->marker_active==4);
    for(int i=0;i<24;++i){audio_callback(a,(Uint8 *)output,sizeof(output));++callbacks;}
    assert(g->outgoing<0);
    /* Deletion confirmation cannot remove the active canvas. */
    gallery_key(window,device,a,s,u,c,SDLK_g,KMOD_CTRL);
    gallery_pointer(window,device,a,s,u,c,337,330);gallery_pointer(window,device,a,s,u,c,337,330);
    assert(g->count==2 && strstr(u->mosaic_gallery_status,"SWITCH AWAY"));
    mosaic_gallery_snapshot(u,g);
    if(shot){char path[1200];snprintf(path,sizeof(path),"%s-marker-entry.ppm",shot);live_controller_screenshot_size(u,path,1280,800);}
    gallery_key(window,device,a,s,u,c,SDLK_ESCAPE,KMOD_NONE);
    assert(ts_performance_recorder_state(&rec->performance_recorder)==TS_PERFORMANCE_FILE_RECORDING);
    ts_performance_recorder_request_stop(&rec->performance_recorder);
    SDL_WaitThread(rec->performance_writer,NULL);rec->performance_writer=NULL;poll_file_capture_ui(u,rec);
    TsSample captured={0};assert(ts_sample_load_wav(&captured,recorded,error,sizeof(error)));
    assert(captured.frames==(size_t)callbacks*256 && captured.channels==2);
    double energy=0;for(size_t i=0;i<captured.frames*2;++i)energy+=fabsf(captured.data[i]);assert(energy>1);
    ts_sample_free(&captured);ts_performance_recorder_free(&rec->performance_recorder);free(rec);a->sister_file_recorder=NULL;
    remove(recorded);SDL_setenv("TAPESISTER_CAPTURES","",1);
    /* Real Gallery buttons and browser/pending-file pipeline: export a page,
       then import it without changing the running canvas, banks or effects. */
    uint64_t scene_hash=ts_mosaic_hash(second),bank_hash=ts_sample_hash(&instrument.current);
    TsSisterParameters parameters=a->sister.parameters;char project_path[1024];strcpy(project_path,u->project_path);
    char directory[128];snprintf(directory,sizeof(directory),"canvas-transfer-ui-%llu",(unsigned long long)SDL_GetPerformanceCounter());
#ifdef _WIN32
    assert(!_mkdir(directory));
#else
    assert(!mkdir(directory,0700));
#endif
    gallery_key(window,device,a,s,u,c,SDLK_g,KMOD_CTRL);
    gallery_pointer(window,device,a,s,u,c,550,90);assert(u->browser.mode==TS_BROWSER_EXPORT_CANVAS && !u->mosaic_gallery_open);
    ui_sync_dialog_workspaces(u);assert(u->mosaic_open);
    snprintf(u->browser.directory,sizeof(u->browser.directory),"%s",directory);ts_browser_set_filename(&u->browser,"Room.tsr");
    PendingFileOperation pending={0};
    browser_action(device,a,u,&instrument,NULL,&pages,NULL,0,&pending,0);assert(pending.active && u->file_busy);
    run_pending_file_operation(device,a,u,&instrument,NULL,NULL,&pages,NULL,0,&pending);
    assert(!u->file_busy && u->mosaic_gallery_open && strstr(u->status,"EXPORTED CANVAS"));
    assert(a->mosaic==second && !strcmp(project_path,u->project_path));
    gallery_pointer(window,device,a,s,u,c,465,90);assert(u->browser.mode==TS_BROWSER_IMPORT_CANVAS);
    snprintf(u->browser.directory,sizeof(u->browser.directory),"%s/Room",directory);assert(ts_browser_refresh(&u->browser));
    int found=-1;for(int i=0;i<u->browser.entry_count;++i)if(!strcmp(u->browser.entries[i].name,"Room.tsr"))found=i;
    assert(found>=0);ts_browser_select(&u->browser,found);
    browser_action(device,a,u,&instrument,NULL,&pages,NULL,0,&pending,0);assert(pending.active);
    run_pending_file_operation(device,a,u,&instrument,NULL,NULL,&pages,NULL,0,&pending);
    assert(u->mosaic_gallery_open && u->mosaic_canvas_import_count==1 && g->count==2);
    if(shot){char path[1200];snprintf(path,sizeof(path),"%s-import.ppm",shot);live_controller_screenshot_size(u,path,1280,800);}
    gallery_pointer(window,device,a,s,u,c,430,330);assert(u->mosaic_canvas_import_pending && u->file_busy);
    mosaic_canvas_queue_import(u,&pending);assert(pending.active && !pending.presented);
    run_pending_file_operation(device,a,u,&instrument,NULL,NULL,&pages,NULL,0,&pending);
    assert(!u->file_busy && !u->mosaic_canvas_import_count && g->count==3 && g->active==1);
    assert(ts_mosaic_hash(g->canvas[2].scene)==scene_hash && !strcmp(g->canvas[2].name,"Blue room"));
    assert(ts_sample_hash(&instrument.current)==bank_hash && !memcmp(&parameters,&a->sister.parameters,sizeof(parameters)));
    assert(a->mosaic==second && !strcmp(project_path,u->project_path));
    assert(g->canvas[2].entry==TS_GALLERY_MARKER && g->canvas[2].marker==3);
    gallery_pointer(window,device,a,s,u,c,190,284);assert(g->canvas[2].entry==TS_GALLERY_START && g->canvas[1].entry==TS_GALLERY_MARKER);
    gallery_pointer(window,device,a,s,u,c,430,330);assert(g->active==2 && !u->mosaic_gallery_open);
    for(int i=0;i<24;++i)audio_callback(a,(Uint8 *)output,sizeof(output));
    assert(g->outgoing<0 && a->mosaic==g->canvas[2].scene && a->mosaic->time>0 && ts_mosaic_live_count(a->mosaic)>0);
    mosaic_controller_free(c);free(c);ts_external_recorder_free(&s->recorder);free(s);free(u);
    a->gallery=NULL;a->mosaic=ts_sample_pages_mosaic_root(&pages);ts_sample_pages_free(&pages);routing_free(a);
}
