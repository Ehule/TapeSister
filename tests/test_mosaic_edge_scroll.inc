static void test_mosaic_edge_scroll(SDL_Window *window)
{
    TsMosaic *saved=ui.mosaic;
    const double scales[]={.25,1,2};
    for(int scenario=0;scenario<3;++scenario) {
        TsMosaic *scene=ts_mosaic_create();assert(scene);ui.mosaic=audio.mosaic=scene;
        ui.mosaic_open=1;ui.mosaic_follow=0;ui.mosaic_scroll=0;ui.mosaic_scale=24;
        double horizontal=scales[scenario];ui.mosaic_hscale=horizontal;
        ui.mosaic_xscroll=500-40/horizontal;mosaic_poll(0,&ui,&instrument,&mosaic);
        char error[160];TsMosaicSource *source=ts_mosaic_source(scene,&instrument.current,error,sizeof(error));assert(source);
        TsMosaicEvent *a=ts_mosaic_add(scene,source,2,500),*b=ts_mosaic_add(scene,source,3,650);
        a->duration=b->duration=8;a->gain=.42f;b->notes[0]=67;
        scene->auto_space=0;scene->playing=1;scene->time=4;
        mosaic_select_only(&ui,a->id);mosaic_select_add(&ui,b->id);
        uint64_t before=ts_mosaic_hash(scene);int history=scene->history_count;
        TsMosaicEvent original_a=*a,original_b=*b;
        SDL_SetModState(scenario==2?KMOD_SHIFT:KMOD_NONE);
        click(window,199,150,1);assert(mosaic.drag==1);
        double scroll=ui.mosaic_xscroll;
        mosaic_drag_autoscroll(0,&ui,&mosaic,mosaic.drag_scroll_ms+50);
        assert(scroll==ui.mosaic_xscroll && ts_mosaic_hash(scene)==before);
        motion(window,TS_MOSAIC_CANVAS_RIGHT,150);assert(mosaic.drag_moved);
        TsMosaicEvent *moved=ts_mosaic_find(scene,ui.mosaic_selected);assert(moved);
        double screen=151+(moved->x-ui.mosaic_xscroll)*horizontal;
        for(int i=0;i<20;++i)mosaic_drag_autoscroll(0,&ui,&mosaic,mosaic.drag_scroll_ms+20);
        assert(ui.mosaic_xscroll>scroll && fabs(151+(moved->x-ui.mosaic_xscroll)*horizontal-screen)<1e-8);
        TsMosaicEvent group[TS_MOSAIC_EVENTS];assert(mosaic_selected_events(&ui,group)==2);
        assert(fabs(group[1].x-group[0].x-150)<1e-8 && group[1].start-group[0].start==1);
        assert(group[0].gain==.42f && group[1].notes[0]==67 && group[0].duration==8);
        assert(scene->time==4 && scene->playing && scene->history_count==history+1);
        if(scenario==2) {
            assert(mosaic_event_count(scene)==4);
            assert(!memcmp(a,&original_a,sizeof(*a)) && !memcmp(b,&original_b,sizeof(*b)));
        }
        /* Return to the middle: no extra scroll, and no jump on the next motion. */
        motion(window,350,150);scroll=ui.mosaic_xscroll;
        mosaic_drag_autoscroll(0,&ui,&mosaic,mosaic.drag_scroll_ms+50);assert(ui.mosaic_xscroll==scroll);
        assert(fabs(151+(moved->x-ui.mosaic_xscroll)*horizontal-(350-8))<1e-8);
        motion(window,151,150);scroll=ui.mosaic_xscroll;
        for(int i=0;i<10;++i)mosaic_drag_autoscroll(0,&ui,&mosaic,mosaic.drag_scroll_ms+20);
        assert(ui.mosaic_xscroll<scroll);
        /* Release ends scrolling; undo and redo cover the entire gesture. */
        release(window,151,150);scroll=ui.mosaic_xscroll;
        mosaic_drag_autoscroll(0,&ui,&mosaic,mosaic.drag_scroll_ms+50);assert(ui.mosaic_xscroll==scroll);
        uint64_t after=ts_mosaic_hash(scene);SDL_SetModState(KMOD_NONE);
        key(window,SDLK_z,KMOD_CTRL);assert(ts_mosaic_hash(scene)==before);
        key(window,SDLK_y,KMOD_CTRL);assert(ts_mosaic_hash(scene)==after);
        key(window,SDLK_z,KMOD_CTRL);
        /* Escape cancels a held-edge move. Focus loss commits once and stops. */
        ui.mosaic_xscroll=500-40/horizontal;mosaic_select_only(&ui,a->id);mosaic_select_add(&ui,b->id);
        click(window,199,150,1);motion(window,TS_MOSAIC_CANVAS_RIGHT,150);
        mosaic_drag_autoscroll(0,&ui,&mosaic,mosaic.drag_scroll_ms+20);
        key(window,SDLK_ESCAPE,KMOD_NONE);assert(!mosaic.drag && ts_mosaic_hash(scene)==before && scene->playing);
        ui.mosaic_xscroll=500-40/horizontal;mosaic_select_only(&ui,a->id);mosaic_select_add(&ui,b->id);
        click(window,199,150,1);motion(window,151,150);
        SDL_Event lost;SDL_zero(lost);lost.type=SDL_WINDOWEVENT;lost.window.windowID=SDL_GetWindowID(window);
        lost.window.event=SDL_WINDOWEVENT_FOCUS_LOST;dispatch(&lost,window);scroll=ui.mosaic_xscroll;
        mosaic_drag_autoscroll(0,&ui,&mosaic,mosaic.drag_scroll_ms+50);assert(!mosaic.drag && ui.mosaic_xscroll==scroll);
        /* At the origin both the view and the moved selection stay nonnegative. */
        key(window,SDLK_z,KMOD_CTRL);a->x=0;b->x=150;ui.mosaic_xscroll=0;
        mosaic_select_only(&ui,a->id);mosaic_select_add(&ui,b->id);
        click(window,159,150,1);motion(window,151,150);
        mosaic_drag_autoscroll(0,&ui,&mosaic,mosaic.drag_scroll_ms+50);
        assert(ui.mosaic_xscroll==0 && a->x==0 && b->x==150);
        release(window,151,150);
        mosaic_drag_end(&ui,&mosaic);ui.mosaic=audio.mosaic=saved;ts_mosaic_free(scene);
    }
    ui.mosaic_scroll=ui.mosaic_xscroll=0;ui.mosaic_scale=24;ui.mosaic_hscale=1;
    mosaic_poll(0,&ui,&instrument,&mosaic);
}

static void test_mosaic_save_view(SDL_Window *window)
{
    ui.mosaic_open=1;ui.mosaic_follow=0;ui.mosaic_scroll=7;ui.mosaic_xscroll=850;
    ui.mosaic_scale=12;ui.mosaic_hscale=.5;
    uint64_t selected=ui.mosaic_selected,hash=ts_mosaic_hash(ui.mosaic);
    /* Toggle while transport is running: labels change, playback/view do not. */
    int old_playing=ui.mosaic->playing;double old_time=ui.mosaic->time;
    ui.mosaic->playing=1;ui.mosaic->time=ui.mosaic_time=125.5;
    ui.mosaic_scroll=120;ui.mosaic_seconds_only=0;
    static TsFramebuffer minutes,seconds;
    ts_ui_render(&minutes,&ui,&instrument);
    /* The compact second row must neither paint over nor intercept the master
       output fader/EQ. The right-channel label previously lost its lower rows. */
    const int buttons[][4]={{TS_MOSAIC_FX_X,TS_MOSAIC_FX_Y,TS_MOSAIC_FX_W,TS_MOSAIC_FX_H},
        {TS_MOSAIC_TIME_X,TS_MOSAIC_TIME_Y,TS_MOSAIC_TIME_W,TS_MOSAIC_TIME_H}};
    for(int b=0;b<2;++b)for(int y=buttons[b][1];y<buttons[b][1]+buttons[b][3];++y)
        for(int x=buttons[b][0];x<buttons[b][0]+buttons[b][2];++x)
            assert(!ts_ui_master_output_contains(x,y) && !ts_ui_master_limiter_contains(x,y));
    const char *glyph=ts_ui_glyph('R');
    uint32_t ink=ts_theme_legacy(&ui.palette,TS_THEME_MOSAIC,TS_COLOR_FOCUS,TS_PALETTE_MOUSE);
    for(int r=0;r<7;++r)for(int c=0;c<5;++c)if(glyph[r*5+c]=='1')
        assert(minutes.pixels[(TS_UI_MASTER_METER_Y+11+r)*640+TS_UI_MASTER_METER_X+c]==ink);
    click(window,TS_MOSAIC_TIME_X+5,TS_MOSAIC_TIME_Y+5,1);
    assert(ui.mosaic_seconds_only && ui.mosaic->playing && ui.mosaic->time==125.5);
    assert(ui.mosaic_scroll==120 && ui.mosaic_scale==12 && ui.mosaic_hscale==.5);
    assert(ui.mosaic_selected==selected && ts_mosaic_hash(ui.mosaic)==hash);
    ts_ui_render(&seconds,&ui,&instrument);
    int different=0;
    for(int y=66;y<TS_MOSAIC_CANVAS_BOTTOM;++y)for(int x=115;x<151;++x)
        different+=minutes.pixels[y*640+x]!=seconds.pixels[y*640+x];
    assert(different>0);
    if(getenv("TS_MOSAIC_TIME_MINUTES"))assert(ts_ui_write_ppm(&minutes,getenv("TS_MOSAIC_TIME_MINUTES")));
    if(getenv("TS_MOSAIC_TIME_SECONDS"))assert(ts_ui_write_ppm(&seconds,getenv("TS_MOSAIC_TIME_SECONDS")));
    click(window,TS_MOSAIC_TIME_X+5,TS_MOSAIC_TIME_Y+5,1);
    assert(!ui.mosaic_seconds_only && ui.mosaic->playing && ui.mosaic->time==125.5);
    ui.mosaic->playing=old_playing;ui.mosaic->time=ui.mosaic_time=old_time;ui.mosaic_scroll=7;
    SDL_Event save;SDL_zero(save);save.type=SDL_KEYDOWN;save.key.windowID=SDL_GetWindowID(window);
    save.key.keysym.sym=SDLK_s;save.key.keysym.mod=KMOD_CTRL;
    assert(!dispatch(&save,window) && ui.mosaic_open); /* Falls through to global Save. */
    click(window,370,14,1);assert(ui.mosaic_open); /* Header Save also falls through. */
    char path[sizeof(ui.project_path)];memcpy(path,ui.project_path,sizeof(path));ui.project_path[0]=0;
    begin_active_project_save(&ui);ui_sync_dialog_workspaces(&ui);
    assert(ui.mosaic_open && ui.browser.mode==TS_BROWSER_SAVE_RECIPE && !main_waveform_detail_allowed(&ui));
    static TsFramebuffer browser,plain,confirm,busy;
    ts_ui_render(&browser,&ui,&instrument);
    int playing=ui.mosaic->playing;double time=ui.mosaic->time;
    ui.mosaic_follow=1;ui.mosaic->playing=1;ui.mosaic->time=100;
    mosaic_poll(0,&ui,&instrument,&mosaic);assert(ui.mosaic_scroll==7 && ui.mosaic_xscroll==850);
    ui.mosaic_follow=0;ui.mosaic->playing=playing;ui.mosaic->time=time;
    /* Switching the Save browser's TSR/XM tabs also keeps Mosaic underneath. */
    browser_open(&ui,TS_BROWSER_SAVE_XM);ui_sync_dialog_workspaces(&ui);assert(ui.mosaic_open);
    ts_ui_render(&confirm,&ui,&instrument);assert(memcmp(&browser,&confirm,sizeof(browser)));
    browser_cancel(&ui);SDL_StopTextInput();
    ui_sync_dialog_workspaces(&ui);ts_ui_render(&plain,&ui,&instrument);assert(ui.mosaic_open);
    /* Dialogs are actually painted, and input cannot edit the cards underneath. */
    snprintf(ui.project_path,sizeof(ui.project_path),"test-mosaic-view.tsr");
    begin_active_project_save(&ui);ui_sync_dialog_workspaces(&ui);
    assert(ui.mosaic_open && ui.project_overwrite_confirm_open);
    assert(!dispatch(&save,window));key(window,SDLK_DELETE,KMOD_NONE);assert(ts_mosaic_hash(ui.mosaic)==hash);
    ts_ui_render(&confirm,&ui,&instrument);
    assert(memcmp(&browser,&plain,sizeof(plain)) && memcmp(&confirm,&plain,sizeof(plain)));
    /* Top-left workspace identity survives the overlay. */
    assert(!memcmp(plain.pixels+640*32,confirm.pixels+640*32,640*33*sizeof(uint32_t)));
    PendingFileOperation pending={0};queue_active_project_save(&ui,&pending);ui_sync_dialog_workspaces(&ui);
    assert(ui.mosaic_open && ui.file_busy && pending.active && !ui.project_overwrite_confirm_open);
    ts_ui_render(&busy,&ui,&instrument);assert(memcmp(&busy,&plain,sizeof(plain)));
    assert(!memcmp(plain.pixels+640*32,busy.pixels+640*32,640*33*sizeof(uint32_t)));
    if(getenv("TS_MOSAIC_SAVE_SCREENSHOT"))assert(ts_ui_write_ppm(&confirm,getenv("TS_MOSAIC_SAVE_SCREENSHOT")));
    ui.file_busy=0;ui_sync_dialog_workspaces(&ui);
    assert(ui.mosaic_open && ui.mosaic_selected==selected && ui.mosaic_scroll==7 && ui.mosaic_xscroll==850);
    assert(ui.mosaic_scale==12 && ui.mosaic_hscale==.5 && ts_mosaic_hash(ui.mosaic)==hash);
    memcpy(ui.project_path,path,sizeof(path));ui.mosaic_scroll=ui.mosaic_xscroll=0;ui.mosaic_scale=24;ui.mosaic_hscale=1;
}
