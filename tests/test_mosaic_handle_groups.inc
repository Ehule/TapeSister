static void test_mosaic_handle_groups(SDL_Window *window)
{
    TsMosaic *saved=ui.mosaic,*scene=ts_mosaic_create();assert(scene);
    ui.mosaic=audio.mosaic=scene;ui.mosaic_open=1;ui.mosaic_follow=0;
    ui.mosaic_scale=24;ui.mosaic_hscale=1;ui.mosaic_scroll=ui.mosaic_xscroll=0;
    ui.mosaic_live_open=0;ui.mosaic_source_keys=0;
    char error[160];TsMosaicSource *source=ts_mosaic_source(scene,&instrument.current,error,sizeof(error));assert(source);
    TsMosaicEvent *a=ts_mosaic_add(scene,source,1,20),*b=ts_mosaic_add(scene,source,2,150);
    TsMosaicEvent *other=ts_mosaic_add(scene,source,3,300);assert(a && b && other);
    a->duration=8;b->duration=6;other->duration=4;a->width=b->width=100;
    a->fade_in=1;b->fade_in=2;a->fade_out=2;b->fade_out=1;other->fade_in=.25;
    mosaic_poll(0,&ui,&instrument,&mosaic);mosaic_select_only(&ui,a->id);mosaic_select_add(&ui,b->id);
    int x,y;fade_handle(b,1,&x,&y);uint64_t initial=ts_mosaic_hash(scene);int history=scene->history_cursor;
    /* Dragging a secondary selection preserves the primary and each offset. */
    click(window,x,y,1);assert(ui.mosaic_selected==a->id && ui.mosaic_ghost_count==2 && ui.mosaic_card_mix_group);
    motion(window,x,y+24);assert(a->fade_in==2 && b->fade_in==3 && other->fade_in==.25);
    resize_alt(window,1);assert(a->fade_in==1 && b->fade_in==1);
    resize_alt(window,0);assert(a->fade_in==2 && b->fade_in==3);
    motion(window,x,y+48);resize_alt(window,1);assert(a->fade_in==2 && b->fade_in==2);
    release(window,x,y+48);resize_alt(window,0);
    assert(scene->history_cursor==history+1 && mosaic_chosen(&ui,0) && mosaic_chosen(&ui,1));
    assert(a->fade_out==2 && b->fade_out==1);
    key(window,SDLK_z,KMOD_CTRL);assert(ts_mosaic_hash(scene)==initial);
    key(window,SDLK_y,KMOD_CTRL);assert(a->fade_in==2 && b->fade_in==2);
    key(window,SDLK_z,KMOD_CTRL);
    /* Alt before mouse-down resets the chosen end; one common legal ceiling
       keeps different-duration cards equal without crossing opposite fades. */
    fade_handle(b,1,&x,&y);resize_alt(window,1);click(window,x,y,1);
    assert(a->fade_in==0 && b->fade_in==0);
    motion(window,x,y+480);assert(a->fade_in==5 && b->fade_in==5);
    key(window,SDLK_ESCAPE,KMOD_ALT);resize_alt(window,0);assert(ts_mosaic_hash(scene)==initial);
    fade_handle(a,2,&x,&y);click(window,x,y,1);motion(window,x,y-24);
    assert(a->fade_out==3 && b->fade_out==2);key(window,SDLK_ESCAPE,KMOD_NONE);
    assert(ts_mosaic_hash(scene)==initial);
    /* Pan uses the clicked handle's scale, even with different card widths. */
    a->pan=-.25f;b->pan=.5f;b->width=140;double span;
    pan_handle(b,&x,&y,&span);initial=ts_mosaic_hash(scene);history=scene->history_cursor;
    click(window,x,y,1);motion(window,x+8,y);
    assert(fabs(a->pan-(-.25+8/span))<1e-6 && fabs(b->pan-(.5+8/span))<1e-6);
    resize_alt(window,1);assert(fabs(a->pan-8/span)<1e-6 && a->pan==b->pan);
    resize_alt(window,0);assert(fabs(a->pan-(-.25+8/span))<1e-6);
    key(window,SDLK_ESCAPE,KMOD_NONE);assert(ts_mosaic_hash(scene)==initial && scene->history_cursor==history);
    pan_handle(a,&x,&y,&span);resize_alt(window,1);click(window,x,y,1);
    assert(a->pan==0 && b->pan==0);motion(window,x-21,y);release(window,x-21,y);resize_alt(window,0);
    assert(a->pan==-.5f && b->pan==-.5f && other->pan==0);
    key(window,SDLK_z,KMOD_CTRL);assert(ts_mosaic_hash(scene)==initial);
    /* The entire retained sample name reaches the info area; moving into a
       gutter or onto another control clears it. Both renderers retain labels. */
    mosaic_sources_refresh(&ui,&instrument,&mosaic);
    TsSample *named=(TsSample *)ui.mosaic_sources[0].sample;assert(named);
    char original_name[128];memcpy(original_name,named->name,sizeof(original_name));
    strcpy(named->name,"Long imported filename - the entire source name stays visible while hovering over this source card - final section.wav");
    motion(window,20,85);assert(!strcmp(ui.mosaic_source_hover,named->name));
    static TsFramebuffer fb;ts_ui_render(&fb,&ui,&instrument);
    const char *shot=getenv("TS_TEST_MOSAIC_HANDLES");live_controller_screenshot_size(&ui,shot,1729,973);
    motion(window,57,85);assert(!ui.mosaic_source_hover[0]);
    motion(window,20,85);assert(ui.mosaic_source_hover[0]);motion(window,180,110);assert(!ui.mosaic_source_hover[0]);
    memcpy(named->name,original_name,sizeof(original_name));
    ui.mosaic=audio.mosaic=saved;mosaic_poll(0,&ui,&instrument,&mosaic);ts_mosaic_free(scene);
}

static void test_mosaic_scale_snap(SDL_Window *window)
{
    TsMosaic *saved=ui.mosaic,*scene=ts_mosaic_create();assert(scene);
    ui.mosaic=audio.mosaic=scene;ui.mosaic_open=1;ui.mosaic_follow=0;
    ui.mosaic_scale=24;ui.mosaic_hscale=1;ui.mosaic_scroll=ui.mosaic_xscroll=0;
    ui.mosaic_live_open=0;ui.mosaic_source_keys=0;
    char error[160];TsMosaicSource *source=ts_mosaic_source(scene,&instrument.current,error,sizeof(error));assert(source);
    TsMosaicEvent *a=ts_mosaic_add(scene,source,1,20),*b=ts_mosaic_add(scene,source,2,150);
    TsMosaicEvent *target=ts_mosaic_add(scene,source,7,300);assert(a && b && target);
    a->duration=4;b->duration=2;target->duration=2;
    mosaic_poll(0,&ui,&instrument,&mosaic);mosaic_select_only(&ui,a->id);mosaic_select_add(&ui,b->id);
    uint64_t initial=ts_mosaic_hash(scene);int history=scene->history_cursor;
    grab_pointer(window,350,180);key(window,SDLK_s,KMOD_NONE);motion(window,350,227);
    TsMosaicEvent p=scale_preview(a);assert(fabs(p.start+p.duration-7)>1e-4);
    resize_alt(window,1);p=scale_preview(a);assert(p.start==1 && p.start+p.duration==7);
    resize_alt(window,0);p=scale_preview(a);assert(fabs(p.start+p.duration-7)>1e-4);
    resize_alt(window,1);key(window,SDLK_RETURN,KMOD_ALT);resize_alt(window,0);
    assert(a->duration==6 && b->duration==4 && scene->history_cursor==history+1);
    key(window,SDLK_z,KMOD_CTRL);assert(ts_mosaic_hash(scene)==initial);
    /* Bottom anchoring snaps the moving start and keeps the original end. */
    target->start=1.55;grab_pointer(window,350,180);key(window,SDLK_s,KMOD_NONE);key(window,SDLK_b,KMOD_NONE);
    motion(window,350,192);resize_alt(window,1);p=scale_preview(a);
    assert(fabs(p.start-1.55)<1e-9 && fabs(p.start+p.duration-5)<1e-9);
    key(window,SDLK_ESCAPE,KMOD_ALT);resize_alt(window,0);
    /* Center anchoring changes both edges symmetrically; constraints reject a
       false snap which would require a negative start. */
    target->start=6;grab_pointer(window,350,180);key(window,SDLK_s,KMOD_NONE);key(window,SDLK_c,KMOD_NONE);
    motion(window,350,203);resize_alt(window,1);p=scale_preview(a);
    assert(fabs(p.start)<1e-9 && fabs(p.duration-6)<1e-9);key(window,SDLK_ESCAPE,KMOD_ALT);resize_alt(window,0);
    target->start=6.2;grab_pointer(window,350,180);key(window,SDLK_s,KMOD_NONE);key(window,SDLK_c,KMOD_NONE);
    motion(window,350,203);resize_alt(window,1);p=scale_preview(a);
    assert(p.start==0 && p.start+p.duration==6);key(window,SDLK_ESCAPE,KMOD_ALT);resize_alt(window,0);
    /* A selected card pinned to time zero must not mask another card's snap. */
    b->start=0;b->duration=.001;target->start=6;
    grab_pointer(window,350,180);key(window,SDLK_s,KMOD_NONE);key(window,SDLK_c,KMOD_NONE);
    motion(window,350,203);resize_alt(window,1);p=scale_preview(a);
    assert(fabs(p.start)<1e-9 && fabs(p.duration-6)<1e-9);
    key(window,SDLK_ESCAPE,KMOD_ALT);resize_alt(window,0);
    ui.mosaic=audio.mosaic=saved;mosaic_poll(0,&ui,&instrument,&mosaic);ts_mosaic_free(scene);
}
