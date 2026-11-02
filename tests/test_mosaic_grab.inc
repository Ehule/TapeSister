/* Exercise actual event dispatch at overview zoom and the modal G lifecycle. */
static void grab_pointer(SDL_Window *window,int x,int y)
{
    SDL_WarpMouseInWindow(window,x,y);SDL_PumpEvents();
    int px,py;SDL_GetMouseState(&px,&py);assert(px==x && py==y);
}
static void grab_right_click(SDL_Window *window,int x,int y)
{
    SDL_Event e;SDL_zero(e);e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=SDL_GetWindowID(window);
    e.button.button=SDL_BUTTON_RIGHT;e.button.x=x;e.button.y=y;dispatch(&e,window);
}
static void grab_frame(void)
{
    mosaic_poll(0,&ui,&instrument,&mosaic);
    /* The hidden dummy window has no OS focus. Sample a focused desktop
       frame while the physical mouse buttons remain released. */
    assert(!(SDL_GetMouseState(NULL,NULL)&SDL_BUTTON_LMASK));
    mosaic_drag_service(0,&ui,&mosaic,SDL_GetTicks(),1,SDL_GetMouseState(NULL,NULL));
}
static void test_mosaic_grab(SDL_Window *window)
{
    TsMosaic *saved=ui.mosaic,*scene=ts_mosaic_create();assert(scene);
    ui.mosaic=audio.mosaic=scene;ui.mosaic_open=1;ui.mosaic_follow=0;
    ui.mosaic_scroll=ui.mosaic_xscroll=0;ui.mosaic_scale=1;ui.mosaic_hscale=.1;
    mosaic_poll(0,&ui,&instrument,&mosaic);scene->auto_space=0;
    char error[160];TsMosaicSource *source=ts_mosaic_source(scene,&instrument.current,error,sizeof(error));assert(source);
    TsMosaicEvent *a=ts_mosaic_add(scene,source,20,100);assert(a);a->duration=2;a->width=86;
    /* A two-pixel card is a move target, not two overlapping resize grips. */
    click(window,165,86,1);assert(mosaic.drag==1 && ui.mosaic_selected==a->id);
    motion(window,175,96);release(window,175,96);
    assert(a->start==30 && a->x==200 && a->duration==2 && a->width==86);
    key(window,SDLK_z,KMOD_CTRL);assert(a->start==20 && a->x==100);
    /* Padding picks a tiny card near its body, without changing its geometry. */
    click(window,165,84,1);assert(mosaic.drag==1 && ui.mosaic_selected==a->id);release(window,165,84);
    /* An exact body wins over a selected neighbour's padding; nearest padding
       wins over selection order. Alt-click still reaches covered cards. */
    TsMosaicEvent *b=ts_mosaic_add(scene,source,17,100);assert(b);b->duration=2;
    mosaic_select_only(&ui,a->id);mosaic_select_click(window,165,84,KMOD_NONE);assert(ui.mosaic_selected==b->id);
    b->start=14;mosaic_select_only(&ui,a->id);mosaic_select_click(window,165,83,KMOD_NONE);assert(ui.mosaic_selected==b->id);
    b->start=20;mosaic_select_only(&ui,a->id);mosaic_select_click(window,165,86,KMOD_ALT);assert(ui.mosaic_selected==b->id);
    b->start=40;b->x=1200;b->duration=3;
    /* Plain window selection replaces; Shift-window selection adds. */
    click(window,155,78,1);motion(window,180,92);release(window,180,92);
    assert(ui.mosaic_selected==a->id && !mosaic_chosen(&ui,(int)(b-scene->events)));
    SDL_SetModState(KMOD_SHIFT);click(window,263,100,1);motion(window,285,114);release(window,285,114);SDL_SetModState(KMOD_NONE);
    assert(mosaic_chosen(&ui,(int)(a-scene->events)) && mosaic_chosen(&ui,(int)(b-scene->events)));
    /* Group G stages every member; playback, history and sources stay intact. */
    scene->time=7;scene->playing=1;int history=scene->history_count,cursor=scene->history_cursor;
    uint64_t before=ts_mosaic_hash(scene);uint64_t primary=ui.mosaic_selected;
    int live_open=ui.mosaic_live_open,live_keys=ui.mosaic_live_keys;
    ui.mosaic_live_open=1;ui.mosaic_live_keys=0;
    grab_pointer(window,400,180);key(window,SDLK_g,KMOD_NONE);assert(ui.mosaic_grab && mosaic.drag==1 && ui.mosaic_ghost_count==2);
    /* Run the application's between-frame watchdog too. G is a modal move:
       neither the mouse button nor the G key needs to remain held. */
    grab_frame();
    SDL_Event released_g;SDL_zero(released_g);released_g.type=SDL_KEYUP;
    released_g.key.windowID=SDL_GetWindowID(window);released_g.key.keysym.sym=SDLK_g;
    dispatch(&released_g,window);
    for(int frame=0;frame<3;++frame)grab_frame();
    assert(ui.mosaic_grab && mosaic.drag==1 && ui.mosaic_ghost_count==2);
    motion(window,420,192);grab_frame();assert(ui.mosaic_grab_x==200 && ui.mosaic_grab_time==12);
    assert(ts_mosaic_hash(scene)==before && scene->history_count==history && scene->history_cursor==cursor);
    assert(scene->playing && scene->time==7 && a->source==source && b->source==source);
    const char *shot=getenv("TS_TEST_MOSAIC_GRAB");
    if(shot){char path[1200];snprintf(path,sizeof(path),"%s-overview.ppm",shot);live_controller_screenshot_size(&ui,path,1729,973);}
    key(window,SDLK_ESCAPE,KMOD_NONE);assert(!ui.mosaic_grab && !mosaic.drag && !ui.mosaic_ghost_count);
    assert(ts_mosaic_hash(scene)==before && scene->history_count==history && scene->history_cursor==cursor);
    assert(ui.mosaic_selected==primary && scene->playing && scene->time==7);
    ui.mosaic_live_open=live_open;ui.mosaic_live_keys=live_keys;
    /* Confirm one group move and undo/redo it as one operation. */
    grab_pointer(window,400,180);key(window,SDLK_g,KMOD_NONE);grab_frame();motion(window,420,192);grab_frame();key(window,SDLK_RETURN,KMOD_NONE);
    assert(!ui.mosaic_grab && a->start==32 && b->start==52 && a->x==300 && b->x==1400);
    key(window,SDLK_z,KMOD_CTRL);assert(a->start==20 && b->start==40 && a->x==100 && b->x==1200);
    key(window,SDLK_y,KMOD_CTRL);assert(a->start==32 && b->start==52);
    /* Right click cancels instead of opening the video menu. A no-op confirm
       and a focus loss both preserve the current undo/redo stack. */
    before=ts_mosaic_hash(scene);history=scene->history_count;cursor=scene->history_cursor;
    grab_pointer(window,400,180);key(window,SDLK_g,KMOD_NONE);motion(window,440,210);grab_right_click(window,440,210);
    assert(!ui.mosaic_video_open && !ui.mosaic_grab && ts_mosaic_hash(scene)==before);
    grab_pointer(window,400,180);key(window,SDLK_g,KMOD_NONE);key(window,SDLK_RETURN,KMOD_NONE);
    assert(scene->history_count==history && scene->history_cursor==cursor);
    key(window,SDLK_g,KMOD_NONE);motion(window,420,192);
    SDL_Event e;SDL_zero(e);e.type=SDL_WINDOWEVENT;e.window.windowID=SDL_GetWindowID(window);e.window.event=SDL_WINDOWEVENT_FOCUS_LOST;dispatch(&e,window);
    assert(!ui.mosaic_grab && ts_mosaic_hash(scene)==before && scene->history_count==history && scene->history_cursor==cursor);
    /* Single-card G also works for video instructions. Click confirms without
       entering their editor; release cannot create a second gesture. */
    TsMosaicSource *video=ts_mosaic_video_source(scene,TS_VIDEO_FIXTURE,0);assert(video);
    TsMosaicEvent *v=ts_mosaic_add(scene,video,60,2400);assert(v);v->duration=1.5;v->video_random_start=1;
    mosaic_select_only(&ui,v->id);grab_pointer(window,400,180);key(window,SDLK_g,KMOD_NONE);grab_frame();motion(window,410,183);grab_frame();
    click(window,410,183,1);release(window,410,183);
    assert(!ui.mosaic_video_open && !ui.mosaic_grab && !mosaic.drag && v->start==63 && v->x==2500 && v->duration==1.5 && v->video_random_start);
    /* Rigid spacing and non-negative bounds share the normal drag planner. */
    scene->auto_space=1;mosaic_select_only(&ui,a->id);mosaic_select_add(&ui,b->id);
    double relative_x=b->x-a->x,relative_t=b->start-a->start;
    grab_pointer(window,400,180);key(window,SDLK_g,KMOD_NONE);motion(window,0,0);key(window,SDLK_RETURN,KMOD_NONE);
    assert(a->x==0 && a->start==0 && b->x-a->x==relative_x && b->start-a->start==relative_t);
    /* AUTO-SPACE shifts the group rigidly around an unselected obstacle. */
    TsMosaicEvent *obstacle=ts_mosaic_add(scene,source,5,140);assert(obstacle);obstacle->duration=4;
    grab_pointer(window,400,180);key(window,SDLK_g,KMOD_NONE);motion(window,410,185);
    assert(a->x+ui.mosaic_grab_x>=obstacle->x+obstacle->width+TS_MOSAIC_GUTTER);
    assert(a->start==0 && b->x-a->x==relative_x);key(window,SDLK_ESCAPE,KMOD_NONE);
    ts_mosaic_delete(scene,obstacle->id);
    /* G edge scrolling changes only the preview and view until confirmed. */
    scene->auto_space=0;grab_pointer(window,400,180);key(window,SDLK_g,KMOD_NONE);motion(window,560,190);
    double opening_x=a->x;mosaic.drag_scroll_ms=100;mosaic_drag_service(0,&ui,&mosaic,150,1,0);
    assert(ui.mosaic_xscroll>0 && a->x==opening_x && ui.mosaic_grab_x>1600);
    key(window,SDLK_ESCAPE,KMOD_NONE);ui.mosaic_xscroll=0;
    /* Source/live QWERTY ownership keeps G as a note key. */
    ui.mosaic_source_keys=1;key(window,SDLK_g,KMOD_NONE);assert(!ui.mosaic_grab);
    SDL_zero(e);e.type=SDL_KEYUP;e.key.keysym.sym=SDLK_g;dispatch(&e,window);ui.mosaic_source_keys=0;
    /* Close zoom retains duration/width grips and a normal central move area. */
    ui.mosaic_scale=24;ui.mosaic_hscale=1;a->x=20;a->start=2;a->duration=4;
    mosaic_select_only(&ui,a->id);click(window,178,115,1);assert(mosaic.drag==6);release(window,178,115);
    click(window,178,208,1);assert(mosaic.drag==2);release(window,178,208);
    click(window,255,150,1);assert(mosaic.drag==4);release(window,255,150);
    click(window,190,155,1);assert(mosaic.drag==1);
    mosaic_drag_service(0,&ui,&mosaic,SDL_GetTicks(),1,SDL_BUTTON_LMASK);
    assert(mosaic.drag==1 && !ui.mosaic_grab);
    mosaic_drag_service(0,&ui,&mosaic,SDL_GetTicks(),1,0);
    assert(!mosaic.drag);release(window,190,155);
    /* The sampled focus guard still cancels G even without a focus event. */
    before=ts_mosaic_hash(scene);history=scene->history_count;cursor=scene->history_cursor;
    grab_pointer(window,350,220);key(window,SDLK_g,KMOD_NONE);motion(window,370,242);grab_frame();
    mosaic_drag_service(0,&ui,&mosaic,SDL_GetTicks(),0,0);
    assert(!ui.mosaic_grab && !mosaic.drag && ts_mosaic_hash(scene)==before);
    assert(scene->history_count==history && scene->history_cursor==cursor);
    /* Alt can be pressed/released during G without accumulating snap error. */
    obstacle=ts_mosaic_add(scene,source,7,400);assert(obstacle);obstacle->duration=1;
    grab_pointer(window,350,220);key(window,SDLK_g,KMOD_NONE);motion(window,370,242);
    double unsnapped=ui.mosaic_grab_time;assert(fabs(unsnapped-22./24)<1e-9);
    key(window,SDLK_LALT,KMOD_ALT);assert(fabs(ui.mosaic_grab_time-1)<1e-9 && a->start==2);
    SDL_zero(e);e.type=SDL_KEYUP;e.key.keysym.sym=SDLK_LALT;dispatch(&e,window);
    assert(fabs(ui.mosaic_grab_time-unsnapped)<1e-9);key(window,SDLK_ESCAPE,KMOD_NONE);
    ts_mosaic_delete(scene,obstacle->id);
    /* The looper cannot consume a grab cancellation or confirmation. */
    grab_pointer(window,350,220);key(window,SDLK_g,KMOD_NONE);ui.mosaic_live_open=1;
    SDL_zero(e);e.type=SDL_KEYDOWN;e.key.windowID=SDL_GetWindowID(window);e.key.keysym.sym=SDLK_ESCAPE;
    assert(!mosaic_live_event(&e,window,0,NULL,&audio,NULL,&ui));dispatch(&e,window);
    assert(!ui.mosaic_grab && ui.mosaic_live_open);ui.mosaic_live_open=0;
    /* Capture the actual native compositor while a readable group is grabbed. */
    scene->auto_space=0;b->x=150;b->start=3;b->duration=4;v->x=280;v->start=5;v->duration=2;
    mosaic_select_add(&ui,b->id);mosaic_select_add(&ui,v->id);
    grab_pointer(window,350,220);key(window,SDLK_g,KMOD_NONE);motion(window,370,244);
    if(shot){char path[1200];snprintf(path,sizeof(path),"%s-group.ppm",shot);live_controller_screenshot_size(&ui,path,1729,973);}
    else live_controller_screenshot_size(&ui,NULL,1729,973);
    assert(ui.mosaic_grab && a->start==2 && b->start==3 && v->start==5);
    key(window,SDLK_ESCAPE,KMOD_NONE);
    /* Replacing the document invalidates the preview rather than moving IDs
       which happen to be reused in a different canvas. */
    key(window,SDLK_g,KMOD_NONE);TsMosaic *other=ts_mosaic_create();assert(other);
    ui.mosaic=audio.mosaic=other;mosaic_poll(0,&ui,&instrument,&mosaic);assert(!ui.mosaic_grab && !mosaic.drag);
    ts_mosaic_free(other);ui.mosaic=audio.mosaic=saved;ts_mosaic_free(scene);
    ui.mosaic_scroll=ui.mosaic_xscroll=0;ui.mosaic_scale=24;ui.mosaic_hscale=1;ui.mosaic_follow=0;
    mosaic_poll(0,&ui,&instrument,&mosaic);
}
