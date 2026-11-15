static void test_gallery_sequence_controls(SDL_Window *window,SDL_AudioDeviceID device,AudioState *a,
    ExternalInputState *input,TsUiState *u,MosaicController *c,const char *shot)
{
    TsMosaicGallery *g=c->pages->gallery;char error[160];
    TsMosaic *m=ts_gallery_active(g);m->playing=0;
    ts_gallery_sequence_default(&g->sequence,g->canvas[g->active].id);
    m->markers[2]=(TsMosaicMarker){.stored=1,.time=.1};
    m->markers[3]=(TsMosaicMarker){.stored=1,.time=.2};
    g->sequence.step[0]=(TsGalleryStep){.canvas_id=g->canvas[g->active].id,.in_marker=2,.out_marker=3,.fade_seconds=.02,.repeats=2};
    u->mosaic_gallery_open=1;u->mosaic_canvas_import_count=0;u->mosaic_gallery_rename=0;
    gallery_pointer(window,device,a,input,u,c,510,360);assert(u->gallery_sequence_open);
    gallery_pointer(window,device,a,input,u,c,120,326);assert(g->sequence.length==2 && u->gallery_sequence_selected==1);
    gallery_pointer(window,device,a,input,u,c,170,58);assert(g->sequence.loop);
    /* Enter an exact timestamp through the actual text-edit handler. */
    SDL_Event e={0};e.type=SDL_MOUSEBUTTONDOWN;e.button.button=SDL_BUTTON_RIGHT;
    assert(gallery_sequence_event(&e,device,a,u,&instrument,c,460,265,KMOD_NONE));assert(u->gallery_sequence_edit==2);
    e.type=SDL_KEYDOWN;e.key.keysym.sym=SDLK_a;
    gallery_sequence_event(&e,device,a,u,&instrument,c,0,0,KMOD_CTRL);
    e.type=SDL_TEXTINPUT;strcpy(e.text.text,"0:00.300");gallery_sequence_event(&e,device,a,u,&instrument,c,0,0,KMOD_NONE);
    e.type=SDL_KEYDOWN;e.key.keysym.sym=SDLK_RETURN;gallery_sequence_event(&e,device,a,u,&instrument,c,0,0,KMOD_NONE);
    assert(!u->gallery_sequence_edit && g->sequence.step[1].out_marker==-1 && g->sequence.step[1].out_seconds==.3);
    gallery_pointer(window,device,a,input,u,c,30,90);assert(u->gallery_sequence_selected==0);
    gallery_pointer(window,device,a,input,u,c,40,58);assert(g->player.running && g->player.step==0 && !a->engine_stopped);
    /* Selection is editing, never an implicit launch. */
    gallery_pointer(window,device,a,input,u,c,185,90);assert(u->gallery_sequence_selected==1 && g->player.step==0);
    float output[512*2];
    for(int i=0;i<9;++i)audio_callback(a,(Uint8 *)output,sizeof(output));
    assert(g->player.visit==2 && g->spare_active);
    gallery_key(window,device,a,input,u,c,SDLK_SPACE,KMOD_NONE);assert(g->player.paused);
    double time=ts_gallery_active(g)->time,old=ts_gallery_outgoing(g)->time;
    audio_callback(a,(Uint8 *)output,sizeof(output));assert(ts_gallery_active(g)->time==time && ts_gallery_outgoing(g)->time==old);
    gallery_key(window,device,a,input,u,c,SDLK_SPACE,KMOD_NONE);assert(!g->player.paused);
    for(int i=0;i<11;++i)audio_callback(a,(Uint8 *)output,sizeof(output));
    assert(g->player.step==1);
    gallery_pointer(window,device,a,input,u,c,490,295);assert(g->player.queued==1);
    mosaic_poll(device,u,&instrument,c);
    if(shot) {
        char path[1200];snprintf(path,sizeof(path),"%s-sequencer.ppm",shot);
        live_controller_screenshot_size(u,path,1280,800);
    }
    gallery_key(window,device,a,input,u,c,SDLK_ESCAPE,KMOD_NONE);assert(!u->gallery_sequence_open && u->mosaic_gallery_open && g->player.running);
    gallery_key(window,device,a,input,u,c,SDLK_ESCAPE,KMOD_NONE);assert(!u->mosaic_gallery_open && g->player.running);
    mosaic_transport(device,a,u,0);assert(g->player.paused);mosaic_transport(device,a,u,0);assert(!g->player.paused);
    mosaic_transport(device,a,u,2);assert(!g->player.running && a->engine_stopped);
    /* Audio switches independently of UI polling, then the editor follows. */
    a->engine_stopped=0;
    int next=(g->active+1)%g->count;
    g->sequence.step[0]=(TsGalleryStep){.canvas_id=g->canvas[g->active].id,.in_marker=-1,.out_marker=-1,.out_seconds=.01,.repeats=1};
    g->sequence.step[1]=(TsGalleryStep){.canvas_id=g->canvas[next].id,.in_marker=-1,.out_marker=-1,.out_seconds=.05,.repeats=1};
    assert(gallery_sequence_start(device,a,u,&instrument,c,0));
    audio_callback(a,(Uint8 *)output,sizeof(output));assert(g->active==next && a->mosaic==ts_gallery_active(g) && u->mosaic!=a->mosaic);
    gallery_sequence_follow(device,a,u,&instrument,c);assert(u->mosaic==a->mosaic && c->pages->mosaic==a->mosaic);
    ts_gallery_sequence_stop(g);u->mosaic_gallery_open=u->gallery_sequence_open=0;
    (void)error;
}
