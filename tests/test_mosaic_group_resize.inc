static void resize_alt(SDL_Window *window,int held)
{
    SDL_SetModState(held?KMOD_ALT:KMOD_NONE);
    SDL_Event e;SDL_zero(e);e.type=held?SDL_KEYDOWN:SDL_KEYUP;e.key.windowID=SDL_GetWindowID(window);
    e.key.keysym.sym=SDLK_LALT;e.key.keysym.mod=held?KMOD_ALT:KMOD_NONE;dispatch(&e,window);
}
static void resize_select_three(TsMosaic *scene)
{
    mosaic_select_only(&ui,scene->events[0].id);
    mosaic_select_add(&ui,scene->events[1].id);mosaic_select_add(&ui,scene->events[2].id);
}
static void test_mosaic_group_resize(SDL_Window *window)
{
    TsMosaic *saved=ui.mosaic,*scene=ts_mosaic_create();assert(scene);
    ui.mosaic=audio.mosaic=scene;ui.mosaic_open=1;ui.mosaic_scale=24;ui.mosaic_hscale=1;
    ui.mosaic_scroll=ui.mosaic_xscroll=0;ui.mosaic_follow=1;
    char error[160];TsMosaicSource *source=ts_mosaic_source(scene,&instrument.current,error,sizeof(error));assert(source);
    TsMosaicEvent *a=ts_mosaic_add(scene,source,1,20),*b=ts_mosaic_add(scene,source,2,130),*c=ts_mosaic_add(scene,source,1.5,240);
    TsMosaicEvent *other=ts_mosaic_add(scene,source,5.2,20);assert(a && b && c && other);
    a->duration=4;b->duration=2;c->duration=6;other->duration=1;
    a->fade_in=.4;b->fade_out=.6;c->pan=.4f;b->looping=0;
    b->gain=.31f;b->notes[0]=67;c->output_route.mode=TS_SOURCE_PAIR;c->output_route.speaker=1;
    TsMosaicEvent before[4];memcpy(before,scene->events,sizeof(before));
    mosaic_poll(0,&ui,&instrument,&mosaic);resize_select_three(scene);
    uint64_t hash=ts_mosaic_hash(scene);int history=scene->history_count;
    /* Grab the secondary card: resize must not silently use the primary. */
    click(window,301,160,1);assert(mosaic.drag==2 && mosaic.drag_event==b->id && ui.mosaic_ghost_count==3);
    release(window,301,160);assert(scene->history_count==history && ts_mosaic_hash(scene)==hash);
    scene->playing=1;ts_mosaic_seek(scene,1.5);ts_mosaic_begin_block(scene,256);ts_mosaic_read(scene,48000);
    uint64_t generation=scene->voices[0].generation;double position=scene->voices[0].position[0];
    click(window,301,160,1);motion(window,301,208);
    assert(a->duration==6 && b->duration==4 && c->duration==8 && ui.mosaic_selected==a->id);
    assert(a->start==1 && b->start==2 && c->start==1.5 && scene->history_count==history+1);
    ts_mosaic_begin_block(scene,256);ts_mosaic_read(scene,48000);
    assert(scene->voices[0].generation==generation && scene->voices[0].position[0]>position);
    double scroll=ui.mosaic_scroll;mosaic_poll(0,&ui,&instrument,&mosaic);assert(ui.mosaic_scroll==scroll);
    /* Modifier-only events update immediately at the last pointer position. */
    resize_alt(window,1);assert(a->duration==4 && b->duration==4 && c->duration==4);
    resize_alt(window,0);assert(a->duration==6 && b->duration==4 && c->duration==8);
    resize_alt(window,1);motion(window,301,232);
    assert(a->duration==5 && b->duration==5 && c->duration==5);
    if(getenv("TS_GROUP_RESIZE_SCREENSHOT")) {
        static TsFramebuffer fb;ts_ui_render(&fb,&ui,&instrument);assert(ts_ui_write_ppm(&fb,getenv("TS_GROUP_RESIZE_SCREENSHOT")));
    }
    release(window,301,232);resize_alt(window,0);assert(!mosaic.drag && scene->history_count==history+1);
    for(int i=0;i<3;++i) {TsMosaicEvent expected=before[i];expected.duration=5;assert(!memcmp(&scene->events[i],&expected,sizeof(expected)));}
    assert(!memcmp(other,&before[3],sizeof(*other)));
    uint64_t resized=ts_mosaic_hash(scene);key(window,SDLK_z,KMOD_CTRL);assert(ts_mosaic_hash(scene)==hash);
    key(window,SDLK_y,KMOD_CTRL);assert(ts_mosaic_hash(scene)==resized);key(window,SDLK_z,KMOD_CTRL);
    scene->playing=0;
    /* Top drags anchor each end. Shared limits prevent negative times and
       retain length differences, including after switching away from Alt. */
    resize_select_three(scene);click(window,301,115,1);assert(mosaic.drag==6);
    motion(window,301,91);assert(a->start==0 && b->start==1 && c->start==.5);
    assert(a->duration==5 && b->duration==3 && c->duration==7);
    resize_alt(window,1);assert(a->duration==3 && b->duration==3 && c->duration==3);
    assert(a->start==2 && b->start==1 && c->start==4.5);
    motion(window,301,-1000);assert(a->duration==4 && b->duration==4 && c->duration==4);
    assert(a->start==1 && b->start==0 && c->start==3.5);
    resize_alt(window,0);assert(a->duration==5 && b->duration==3 && c->duration==7);
    key(window,SDLK_ESCAPE,KMOD_NONE);assert(ts_mosaic_hash(scene)==hash && !mosaic.drag);
    for(int i=0;i<3;++i)assert(mosaic_chosen(&ui,i));
    /* Shortest card stops a relative shortening; releasing never adds history. */
    click(window,301,160,1);motion(window,301,-1000);
    assert(fabs(a->duration-2.005)<1e-9 && fabs(b->duration-.005)<1e-9 && fabs(c->duration-4.005)<1e-9);
    release(window,301,-1000);key(window,SDLK_z,KMOD_CTRL);assert(ts_mosaic_hash(scene)==hash);
    /* The upper bound likewise applies to the whole delta, with no no-op undo. */
    a->duration=86400;resize_select_three(scene);history=scene->history_cursor;
    click(window,301,160,1);motion(window,301,208);release(window,301,208);
    assert(a->duration==86400 && b->duration==2 && c->duration==6 && scene->history_cursor==history);a->duration=4;
    /* Spacing reuses the rigid group planner and returns to opening geometry. */
    scene->auto_space=1;resize_select_three(scene);click(window,201,184,1);motion(window,201,208);
    assert(a->duration==5 && b->duration==3 && c->duration==7);
    assert(a->x==114 && b->x==224 && c->x==334 && other->x==20);
    motion(window,201,184);assert(a->x==20 && b->x==130 && c->x==240);
    key(window,SDLK_ESCAPE,KMOD_NONE);scene->auto_space=0;assert(ts_mosaic_hash(scene)==hash);
    /* Alt held before grabbing a selected, overlapped edge must not cycle. */
    b->x=a->x;b->start=a->start;b->duration=a->duration;resize_select_three(scene);
    resize_alt(window,1);click(window,201,184,1);assert(mosaic.drag==2 && ui.mosaic_ghost_count==3);
    motion(window,201,208);assert(a->duration==5 && b->duration==5 && c->duration==5);
    key(window,SDLK_ESCAPE,KMOD_ALT);resize_alt(window,0);
    resize_alt(window,1);click(window,201,150,1);assert(ui.mosaic_selected==b->id);release(window,201,150);resize_alt(window,0);
    *b=before[1];resize_select_three(scene);
    /* Shift still copies once even when the pointer starts on an edge. */
    SDL_SetModState(KMOD_SHIFT);click(window,301,160,1);assert(mosaic.drag==1 && mosaic.drag_copy);
    motion(window,320,184);motion(window,330,196);assert(mosaic_event_count(scene)==7);
    release(window,330,196);SDL_SetModState(KMOD_NONE);key(window,SDLK_z,KMOD_CTRL);assert(ts_mosaic_hash(scene)==hash);
    /* Focus loss ends one completed edit. Later pointer motion does nothing. */
    resize_select_three(scene);click(window,301,160,1);motion(window,301,184);
    SDL_Event lost;SDL_zero(lost);lost.type=SDL_WINDOWEVENT;lost.window.windowID=SDL_GetWindowID(window);
    lost.window.event=SDL_WINDOWEVENT_FOCUS_LOST;dispatch(&lost,window);assert(!mosaic.drag);
    resized=ts_mosaic_hash(scene);motion(window,301,210);assert(ts_mosaic_hash(scene)==resized);
    key(window,SDLK_z,KMOD_CTRL);assert(ts_mosaic_hash(scene)==hash);
    /* Fractional canvas and window scaling still convert to seconds once. */
    for(int scale=0;scale<2;++scale) {
        SDL_SetWindowSize(window,scale?960:640,scale?600:400);double factor=scale?1.5:1;
        ui.mosaic_scale=37.3;ui.mosaic_hscale=.83;ui.mosaic_xscroll=3.1;ui.mosaic_scroll=.17;
        resize_select_three(scene);
        int x=(int)round(151+(b->x-ui.mosaic_xscroll+20)*ui.mosaic_hscale);
        int y=(int)floor(66+(b->start+b->duration-ui.mosaic_scroll)*ui.mosaic_scale)-2;
        click(window,(int)(x*factor),(int)(y*factor),1);assert(mosaic.drag==2);
        motion(window,(int)(x*factor),(int)((y+20)*factor));release(window,(int)(x*factor),(int)((y+20)*factor));
        assert(fabs(b->duration-2-20/37.3)<1e-9 && fabs(a->duration-b->duration-2)<1e-9);
        key(window,SDLK_z,KMOD_CTRL);assert(ts_mosaic_hash(scene)==hash);
    }
    SDL_SetWindowSize(window,640,400);mosaic_drag_end(&ui,&mosaic);
    ui.mosaic=audio.mosaic=saved;ts_mosaic_free(scene);mosaic_poll(0,&ui,&instrument,&mosaic);
}
