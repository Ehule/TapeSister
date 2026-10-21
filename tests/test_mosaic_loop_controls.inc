static void mosaic_loop_text(SDL_Window *window,const char *value)
{
    key(window,SDLK_a,KMOD_CTRL);
    SDL_Event e;SDL_zero(e);e.type=SDL_TEXTINPUT;e.text.windowID=SDL_GetWindowID(window);
    snprintf(e.text.text,sizeof(e.text.text),"%s",value);dispatch(&e,window);
}
static void test_mosaic_loop_controls(SDL_Window *window)
{
    TsMosaic *saved=ui.mosaic,*scene=ts_mosaic_create();assert(scene);
    ui.mosaic=audio.mosaic=scene;ui.mosaic_open=1;ui.mosaic_follow=0;
    mosaic_poll(0,&ui,&instrument,&mosaic);
    char error[160];TsMosaicSource *source=ts_mosaic_source(scene,&instrument.current,error,sizeof(error));assert(source);
    TsMosaicEvent *a=ts_mosaic_add(scene,source,1,20);a->duration=4;
    TsMosaicEvent *b=ts_mosaic_add(scene,source,4,130);b->duration=3;
    mosaic_select_only(&ui,a->id);ui.mosaic_selection[b-scene->events]=b->id;
    TsMosaicEvent original_a=*a,original_b=*b;
    ui.mosaic_scroll=ui.mosaic_xscroll=0;ui.mosaic_scale=20;ui.mosaic_hscale=1;
    scene->time=3;scene->playing=1;int history=scene->history_count;
    /* Upward selection previews without seeking, editing cards or adding undo. */
    SDL_SetModState(KMOD_SHIFT);click(window,130,226,1);SDL_SetModState(KMOD_NONE);
    assert(ui.mosaic_loop_drag && !scene->loop_range.end);
    mosaic_nav_mouse(window,SDL_MOUSEMOTION,SDL_BUTTON_LEFT,130,106);
    assert(ui.mosaic_loop_preview.start==2 && ui.mosaic_loop_preview.end==8);
    assert(!scene->loop_range.end && scene->time==3 && scene->history_count==history);
    release(window,130,106);assert(scene->loop_range.start==2 && scene->loop_range.end==8);
    assert(!ui.mosaic_loop_drag && scene->history_count==history+1 && ui.mosaic_selected==a->id);
    /* Both handles adjust independently, with no accidental seek on release. */
    click(window,130,106,1);assert(ui.mosaic_loop_drag==2);
    mosaic_nav_mouse(window,SDL_MOUSEMOTION,SDL_BUTTON_LEFT,130,86);release(window,130,86);
    assert(scene->loop_range.start==1 && scene->loop_range.end==8 && scene->time==3);
    click(window,130,226,1);assert(ui.mosaic_loop_drag==3);release(window,130,206);
    assert(scene->loop_range.end==7);
    int edits=scene->history_count;
    click(window,130,206,1);mosaic_nav_mouse(window,SDL_MOUSEMOTION,SDL_BUTTON_LEFT,130,186);
    key(window,SDLK_ESCAPE,KMOD_NONE);assert(!ui.mosaic_loop_drag && scene->loop_range.end==7 && scene->playing);
    click(window,130,206,1);
    SDL_Event lost;SDL_zero(lost);lost.type=SDL_WINDOWEVENT;lost.window.windowID=SDL_GetWindowID(window);lost.window.event=SDL_WINDOWEVENT_FOCUS_LOST;
    dispatch(&lost,window);release(window,130,166);assert(!ui.mosaic_loop_drag && scene->loop_range.end==7);
    assert(scene->history_count==edits);
    key(window,SDLK_l,KMOD_NONE);assert(scene->repeat);
    click(window,290,44,1);assert(!scene->repeat);
    key(window,SDLK_l,KMOD_SHIFT);assert(scene->loop_range.start==1 && scene->loop_range.end==7 && !scene->repeat);
    mosaic_nav_mouse(window,SDL_MOUSEBUTTONDOWN,SDL_BUTTON_RIGHT,130,146);
    assert(!scene->loop_range.end && scene->time==3);
    key(window,SDLK_z,KMOD_CTRL);assert(scene->loop_range.start==1 && scene->loop_range.end==7);
    key(window,SDLK_y,KMOD_CTRL);assert(!scene->loop_range.end);
    key(window,SDLK_l,KMOD_SHIFT);key(window,SDLK_l,KMOD_NONE);assert(scene->repeat);
    /* Play starts at the range when outside it, but pause never moves time. */
    scene->playing=0;scene->time=8;key(window,SDLK_SPACE,KMOD_NONE);assert(scene->playing && scene->time==1);
    scene->time=8;key(window,SDLK_SPACE,KMOD_NONE);assert(!scene->playing && scene->time==8);
    scene->time=3;scene->playing=1;
    /* Exact entry stays inside Mosaic through frame/dialog synchronization. */
    click(window,130,146,2);assert(ui.mosaic_loop_open);
    ui_sync_dialog_workspaces(&ui);assert(ui.mosaic_open && ui_dialog_open(&ui));
    assert(!main_waveform_detail_allowed(&ui) && ts_ui_foreground_panel_open(&ui));
    double scroll=ui.mosaic_scroll;key(window,SDLK_PAGEDOWN,KMOD_NONE);assert(ui.mosaic_scroll==scroll);
    mosaic_loop_text(window,"nan");key(window,SDLK_RETURN,KMOD_NONE);
    assert(ui.mosaic_loop_open && scene->loop_range.start==1 && scene->playing);
    mosaic_loop_text(window,"2.125");key(window,SDLK_TAB,KMOD_NONE);mosaic_loop_text(window,"2");
    key(window,SDLK_RETURN,KMOD_NONE);assert(ui.mosaic_loop_open && scene->loop_range.end==7);
    mosaic_loop_text(window,"6.875");key(window,SDLK_RETURN,KMOD_NONE);
    assert(!ui.mosaic_loop_open && scene->loop_range.start==2.125 && scene->loop_range.end==6.875);
    click(window,130,146,2);mosaic_loop_text(window,"4");key(window,SDLK_ESCAPE,KMOD_NONE);
    assert(!ui.mosaic_loop_open && scene->loop_range.start==2.125);
    assert(!memcmp(a,&original_a,sizeof(*a)) && !memcmp(b,&original_b,sizeof(*b)));
    assert(ui.mosaic_selected==a->id && mosaic_chosen(&ui,b-scene->events));
    if(getenv("TS_MOSAIC_LOOP_SCREENSHOT")) {
        mosaic_loop_status(&ui,scene->loop_range);mosaic_poll(0,&ui,&instrument,&mosaic);
        static TsFramebuffer fb;ts_ui_render(&fb,&ui,&instrument);
        assert(ts_ui_write_ppm(&fb,getenv("TS_MOSAIC_LOOP_SCREENSHOT")));
        click(window,130,146,2);ts_ui_render(&fb,&ui,&instrument);
        assert(ts_ui_write_ppm(&fb,"mosaic-loop-dialog.ppm"));key(window,SDLK_ESCAPE,KMOD_NONE);
    }
    mosaic_native_check(&instrument,getenv("TS_MOSAIC_LOOP_NATIVE_SCREENSHOT"));
    /* Scatter and spacing modals must keep ownership of L/Shift+L. */
    key(window,SDLK_r,KMOD_CTRL);assert(ui.mosaic_scatter_open);
    key(window,SDLK_l,KMOD_NONE);assert(scene->repeat);key(window,SDLK_ESCAPE,KMOD_NONE);
    mosaic_drag_end(&ui,&mosaic);ui.mosaic=audio.mosaic=saved;ts_mosaic_free(scene);
    ui.mosaic_scroll=ui.mosaic_xscroll=0;ui.mosaic_scale=24;ui.mosaic_hscale=1;ui.mosaic_follow=0;
    mosaic_poll(0,&ui,&instrument,&mosaic);
}
