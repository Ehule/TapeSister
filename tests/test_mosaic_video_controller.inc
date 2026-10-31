static void test_mosaic_video_controller(SDL_Window *window)
{
    TsMosaicSource *audio_source=ts_mosaic_source(ui.mosaic,&instrument.current,ui.status,sizeof(ui.status));assert(audio_source);
    TsMosaicEvent *audio_card=ts_mosaic_add(ui.mosaic,audio_source,8,250);assert(audio_card);uint64_t audio_id=audio_card->id;
    mosaic_sources_refresh(&ui,&instrument,&mosaic);mosaic_select_only(&ui,audio_id);
    key(window,SDLK_v,(SDL_Keymod)(KMOD_CTRL|KMOD_SHIFT));assert(ui.mosaic_video_open && ts_ui_foreground_panel_open(&ui));
    click(window,180,118,1);assert(ui.browser.mode==TS_BROWSER_MOSAIC_VIDEO);
    ui_sync_dialog_workspaces(&ui);assert(ui.mosaic_open);
    gallery_browser_visible(&ui,getenv("TS_TEST_VIDEO"),"video-file");
    snprintf(ui.browser.directory,sizeof(ui.browser.directory),"%s",TS_VIDEO_FIXTURE);
    char *slash=strrchr(ui.browser.directory,'/');assert(slash);*slash=0;assert(ts_browser_refresh(&ui.browser));
    int found=0;for(int i=0;i<ui.browser.entry_count;++i)if(!strcmp(ui.browser.entries[i].name,"mandala-video.mp4")){ts_browser_select(&ui.browser,i);found=1;break;}assert(found);
    PendingFileOperation pending={0};browser_action(0,&audio,&ui,&instrument,NULL,NULL,NULL,0,&pending,0);
    assert(ui.browser.mode==TS_BROWSER_CLOSED && ui.mosaic_video_open && ui.mosaic_open);
    TsMosaicEvent *e=ts_mosaic_find(ui.mosaic,ui.mosaic_video_id);assert(e && e->source->video_path[0] && !e->source->sample.data);
    assert(ui.mosaic_selected==e->id && !mosaic_chosen(&ui,(int)(audio_card-ui.mosaic->events)));
    ts_mosaic_delete(ui.mosaic,audio_id);
    click(window,190,165,1);assert(e->video_random_start);
    click(window,390,165,1);assert(e->video_bw);
    click(window,190,195,1);assert(e->video_transition==1);
    click(window,390,195,1);assert(e->video_fade==2.25);
    const char *shot=getenv("TS_TEST_VIDEO");if(shot){char path[1200];snprintf(path,sizeof(path),"%s-dialog.ppm",shot);live_controller_screenshot_size(&ui,path,1280,800);}
    click(window,450,315,1);assert(!ui.mosaic_video_open);
    uint64_t id=e->id;ui.mosaic->loop_range=(TsMosaicRange){2,4};
    key(window,SDLK_r,KMOD_CTRL);assert(ui.mosaic_scatter_open && ui.mosaic_scatter_start==2 && ui.mosaic_scatter_end==4);
    ui.mosaic_scatter_copies=1;ui.mosaic_scatter_count=20;mosaic_scatter_apply(0,&ui,&mosaic);
    assert(!ui.mosaic_scatter_open);int count=0;
    for(int i=0;i<TS_MOSAIC_EVENTS;++i){e=&ui.mosaic->events[i];if(!e->id)continue;++count;
        assert(e->source->video_path[0] && e->video_random_start && e->video_bw && e->video_fade==2.25);
        if(e->id!=id)assert(e->start>=2 && e->start<=4);}
    assert(count==21);
    if(shot){char path[1200];snprintf(path,sizeof(path),"%s-scatter.ppm",shot);live_controller_screenshot_size(&ui,path,1729,973);}
    key(window,SDLK_z,KMOD_CTRL);count=0;for(int i=0;i<TS_MOSAIC_EVENTS;++i)count+=ui.mosaic->events[i].id!=0;assert(count==1);
    e=ts_mosaic_find(ui.mosaic,id);assert(e);mosaic_select_only(&ui,id);
    SDL_Event right={0};right.type=SDL_MOUSEBUTTONDOWN;right.button.windowID=SDL_GetWindowID(window);right.button.button=SDL_BUTTON_RIGHT;right.button.x=160;right.button.y=74;
    assert(dispatch(&right,window));assert(ui.mosaic_video_open && ui.mosaic_video_id==id);
    click(window,450,315,1);assert(!ui.mosaic_video_open);
    right.button.x=500;right.button.y=300;assert(dispatch(&right,window));
    assert(ui.mosaic_video_open && !ui.mosaic_video_id && ui.mosaic_video_time==234.0/24);click(window,450,315,1);
    assert(!mosaic_enter(0,&audio,&ui,&instrument,&mosaic,0));
    /* Folder picking and cancellation must also preserve the originating workspace. */
    key(window,SDLK_v,(SDL_Keymod)(KMOD_CTRL|KMOD_SHIFT));click(window,400,118,1);
    assert(ui.browser.mode==TS_BROWSER_MOSAIC_VIDEO_POOL);ui_sync_dialog_workspaces(&ui);assert(ui.mosaic_open);
    gallery_browser_visible(&ui,getenv("TS_TEST_VIDEO"),"video-pool");
    snprintf(ui.browser.directory,sizeof(ui.browser.directory),"%s",TS_VIDEO_FIXTURE);
    slash=strrchr(ui.browser.directory,'/');assert(slash);*slash=0;assert(ts_browser_refresh(&ui.browser));
    browser_action(0,&audio,&ui,&instrument,NULL,NULL,NULL,0,&pending,0);ui_sync_dialog_workspaces(&ui);
    e=ts_mosaic_find(ui.mosaic,ui.mosaic_video_id);assert(e && e->source->video_pool && ui.mosaic_open && ui.mosaic_video_open);
    click(window,450,315,1);assert(!ui.mosaic_video_open);
    key(window,SDLK_v,(SDL_Keymod)(KMOD_CTRL|KMOD_SHIFT));click(window,180,118,1);
    ui_sync_dialog_workspaces(&ui);assert(ui.mosaic_open);ts_browser_close(&ui.browser);ui_sync_dialog_workspaces(&ui);
    assert(ui.mosaic_open && !ui_dialog_open(&ui));
    /* The visualizer's older manual file picker keeps Mosaic visible too. */
    browser_open(&ui,TS_BROWSER_LOAD_VIDEO);ui_sync_dialog_workspaces(&ui);assert(ui.mosaic_open);
    gallery_browser_visible(&ui,getenv("TS_TEST_VIDEO"),"visualizer-video");ts_browser_close(&ui.browser);
    ui_sync_dialog_workspaces(&ui);assert(ui.mosaic_open && !ui_dialog_open(&ui));
    puts("video canvas: file picker, right-click, settings, native render, range scatter, copies, undo and picker workspace retention PASS");
}
