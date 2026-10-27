static void mosaic_nav_mouse(SDL_Window *window,Uint32 type,int button,int x,int y)
{
    SDL_Event e;SDL_zero(e);e.type=type;
    if(type==SDL_MOUSEMOTION) {
        e.motion.windowID=SDL_GetWindowID(window);e.motion.state=SDL_BUTTON(button);e.motion.x=x;e.motion.y=y;
    } else {
        e.button.windowID=SDL_GetWindowID(window);e.button.button=button;e.button.x=x;e.button.y=y;
    }
    dispatch(&e,window);
}
static void test_mosaic_navigation(SDL_Window *window)
{
    TsMosaic *saved=ui.mosaic,*scene=ts_mosaic_create();assert(scene);
    ui.mosaic=audio.mosaic=scene;ui.mosaic_open=1;ui.mosaic_follow=0;
    mosaic_poll(0,&ui,&instrument,&mosaic);
    key(window,SDLK_z,KMOD_SHIFT);assert(ui.mosaic_scale==1200 && ui.mosaic_hscale==1);
    char error[160];TsMosaicSource *source=ts_mosaic_source(scene,&instrument.current,error,sizeof(error));assert(source);
    TsMosaicEvent *a=ts_mosaic_add(scene,source,0,20);a->duration=720;
    TsMosaicEvent *b=ts_mosaic_add(scene,source,600,130);b->duration=840;
    mosaic_select_only(&ui,a->id);
    TsMosaicEvent before_a=*a,before_b=*b;
    scene->playing=1;scene->time=600;int history=scene->history_count;
    key(window,SDLK_z,KMOD_SHIFT);assert(ui.mosaic_scroll==0 && fabs(ui.mosaic_scale-(TS_MOSAIC_CANVAS_HEIGHT-10.)/1440)<1e-9);
    assert(!ui.mosaic_follow && ui.mosaic_hscale==1);
    /* Empty space before offset cards must not squash them against the right.
       Fit preserves all timing, placement, selection and history. */
    a->x=6000;b->x=6110;
    TsMosaicEvent offset_a=*a,offset_b=*b;
    click(window,TS_MOSAIC_FIT_X+4,45,1);
    double center=(151+TS_MOSAIC_CANVAS_RIGHT)*.5;
    assert(fabs(151+((a->x+b->x+b->width)*.5-ui.mosaic_xscroll)*ui.mosaic_hscale-center)<1e-9);
    assert(ui.mosaic_hscale==1 && scene->time==600 && scene->playing);
    assert(scene->history_count==history && ui.mosaic_selected==a->id);
    assert(!memcmp(a,&offset_a,sizeof(*a)) && !memcmp(b,&offset_b,sizeof(*b)));
    b->x=15000;key(window,SDLK_z,KMOD_SHIFT);
    assert(fabs(151+(a->x-ui.mosaic_xscroll)*ui.mosaic_hscale-163)<1e-9);
    assert(fabs(151+(b->x+b->width-ui.mosaic_xscroll)*ui.mosaic_hscale-(TS_MOSAIC_CANVAS_RIGHT-12))<1e-9);
    *a=before_a;*b=before_b;
    key(window,SDLK_f,KMOD_NONE);assert(ui.mosaic_follow);
    key(window,SDLK_f,KMOD_NONE);assert(!ui.mosaic_follow);
    ui.mosaic_scale=2;ui.mosaic_hscale=.5;ui.mosaic_xscroll=300;
    key(window,SDLK_h,KMOD_NONE);assert(ui.mosaic_scroll==527 && ui.mosaic_xscroll==300);
    key(window,SDLK_PAGEDOWN,KMOD_NONE);assert(ui.mosaic_scroll==658);
    key(window,SDLK_PAGEUP,KMOD_NONE);assert(ui.mosaic_scroll==527);
    SDL_Event repeated;SDL_zero(repeated);repeated.type=SDL_KEYDOWN;repeated.key.windowID=SDL_GetWindowID(window);
    repeated.key.repeat=1;repeated.key.keysym.sym=SDLK_PAGEUP;dispatch(&repeated,window);assert(ui.mosaic_scroll==396);
    repeated.key.keysym.sym=SDLK_f;dispatch(&repeated,window);assert(!ui.mosaic_follow);
    ui.mosaic_scroll=0;key(window,SDLK_PAGEUP,KMOD_NONE);assert(!ui.mosaic_scroll);
    /* Follow is paused during the undecided press; a drag cancels Follow and
       leaves playback, selection, card parameters and undo history intact. */
    ui.mosaic_scroll=500;ui.mosaic_follow=1;
    mosaic_nav_mouse(window,SDL_MOUSEBUTTONDOWN,SDL_BUTTON_MIDDLE,260,200);
    mosaic_poll(0,&ui,&instrument,&mosaic);assert(ui.mosaic_scroll==500 && scene->time==600);
    mosaic_nav_mouse(window,SDL_MOUSEMOTION,SDL_BUTTON_MIDDLE,212,224);
    assert(ui.mosaic_scroll==488 && ui.mosaic_xscroll==396 && !ui.mosaic_follow);
    mosaic_nav_mouse(window,SDL_MOUSEBUTTONUP,SDL_BUTTON_MIDDLE,212,224);
    assert(!mosaic.pan_active && scene->time==600 && scene->playing && ui.mosaic_selected==a->id);
    /* Escape and focus loss abandon a pending click without seeking/stopping. */
    mosaic_nav_mouse(window,SDL_MOUSEBUTTONDOWN,SDL_BUTTON_MIDDLE,260,200);
    key(window,SDLK_ESCAPE,KMOD_NONE);assert(!mosaic.pan_active && scene->playing && scene->time==600);
    mosaic_nav_mouse(window,SDL_MOUSEBUTTONDOWN,SDL_BUTTON_MIDDLE,260,200);
    SDL_Event lost;SDL_zero(lost);lost.type=SDL_WINDOWEVENT;lost.window.windowID=SDL_GetWindowID(window);lost.window.event=SDL_WINDOWEVENT_FOCUS_LOST;
    dispatch(&lost,window);mosaic_nav_mouse(window,SDL_MOUSEBUTTONUP,SDL_BUTTON_MIDDLE,260,200);
    assert(!mosaic.pan_active && scene->time==600);
    /* An ordinary click with tiny pointer jitter still seeks on release. */
    ui.mosaic_scroll=500;
    mosaic_nav_mouse(window,SDL_MOUSEBUTTONDOWN,SDL_BUTTON_MIDDLE,260,200);assert(scene->time==600);
    mosaic_nav_mouse(window,SDL_MOUSEMOTION,SDL_BUTTON_MIDDLE,261,201);assert(scene->time==600);
    mosaic_nav_mouse(window,SDL_MOUSEBUTTONUP,SDL_BUTTON_MIDDLE,261,201);assert(scene->time==567);
    scene->time=600;
    click(window,20,TS_MOSAIC_MARKER_Y+5,1);assert(ui.mosaic_views_open);
    click(window,225,209,1);assert(scene->views[0].stored && scene->views[0].scroll==500);
    ui.mosaic_scroll=800;ui.mosaic_scale=.4;ui.mosaic_xscroll=0;
    click(window,385,209,1);assert(scene->views[4].stored);
    ui.mosaic_follow=1;click(window,225,209,1);
    assert(ui.mosaic_scroll==500 && ui.mosaic_scale==2 && ui.mosaic_xscroll==396 && !ui.mosaic_follow);
    ui.mosaic_scroll=900;SDL_SetModState(KMOD_SHIFT);click(window,225,209,1);SDL_SetModState(KMOD_NONE);
    assert(scene->views[0].scroll==900);
    mosaic_nav_mouse(window,SDL_MOUSEBUTTONDOWN,SDL_BUTTON_MIDDLE,225,209);
    assert(!scene->views[0].stored && scene->views[4].stored && scene->time==600);
    assert(!memcmp(a,&before_a,sizeof(*a)) && !memcmp(b,&before_b,sizeof(*b)) && scene->history_count==history);
    assert(scene->playing && ui.mosaic_selected==a->id);
    key(window,SDLK_ESCAPE,KMOD_NONE);assert(!ui.mosaic_views_open);
    /* View captures are independent of arrangement undo/redo. */
    ts_mosaic_checkpoint(scene);a->start=10;assert(ts_mosaic_undo(scene,0));assert(scene->views[4].stored);
    /* Modal keyboard ownership remains intact with the new navigation keys. */
    key(window,SDLK_r,KMOD_CTRL);assert(ui.mosaic_scatter_open);
    double scroll=ui.mosaic_scroll;key(window,SDLK_PAGEDOWN,KMOD_NONE);assert(ui.mosaic_scroll==scroll);
    key(window,SDLK_ESCAPE,KMOD_NONE);assert(!ui.mosaic_scatter_open && scene->playing);
    key(window,SDLK_z,KMOD_SHIFT);
    if(getenv("TS_MOSAIC_NAV_SCREENSHOT")) {
        snprintf(ui.status,sizeof(ui.status),"VIEWS: CLICK RECALL / SHIFT SAVE / MIDDLE CLEAR");
        static TsFramebuffer fb;ts_ui_render(&fb,&ui,&instrument);
        assert(ts_ui_write_ppm(&fb,getenv("TS_MOSAIC_NAV_SCREENSHOT")));
    }
    mosaic_drag_end(&ui,&mosaic);ui.mosaic=audio.mosaic=saved;ts_mosaic_free(scene);
    ui.mosaic_scroll=ui.mosaic_xscroll=0;ui.mosaic_scale=24;ui.mosaic_hscale=1;ui.mosaic_follow=0;
    mosaic_poll(0,&ui,&instrument,&mosaic);
}
