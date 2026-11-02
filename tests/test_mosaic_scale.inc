static TsMosaicEvent scale_preview(const TsMosaicEvent *e)
{
    TsMosaicEvent out;ts_ui_mosaic_transform_event(&ui,e,&out);return out;
}
static void scale_release_key(SDL_Window *window)
{
    SDL_Event e;SDL_zero(e);e.type=SDL_KEYUP;e.key.windowID=SDL_GetWindowID(window);
    e.key.keysym.sym=SDLK_s;dispatch(&e,window);grab_frame();
}
static void test_mosaic_scale(SDL_Window *window)
{
    TsMosaic *saved=ui.mosaic,*scene=ts_mosaic_create();assert(scene);
    ui.mosaic=audio.mosaic=scene;ui.mosaic_open=1;ui.mosaic_follow=0;
    ui.mosaic_scroll=ui.mosaic_xscroll=0;ui.mosaic_scale=.5;ui.mosaic_hscale=.1;
    mosaic_poll(0,&ui,&instrument,&mosaic);scene->auto_space=0;
    ui.mosaic_live_open=1;ui.mosaic_live_keys=ui.mosaic_source_keys=0;
    char error[160];TsMosaicSource *source=ts_mosaic_source(scene,&instrument.current,error,sizeof(error));assert(source);
    TsMosaicEvent *a=ts_mosaic_add(scene,source,2,20),*b=ts_mosaic_add(scene,source,8,150);assert(a && b);
    a->duration=4;b->duration=2;a->pan=.25f;a->fade_in=.3;a->fade_out=.5;
    /* Existing sub-5ms cards remain unchanged on a no-op and retain anchors. */
    TsMosaicEvent tiny=*a,tiny_preview;tiny.start=0;tiny.duration=.001;
    ui.mosaic_grab=2;ui.mosaic_scale_delta=0;
    ts_ui_mosaic_transform_event(&ui,&tiny,&tiny_preview);assert(tiny_preview.duration==tiny.duration);
    for(int anchor=1;anchor<=2;++anchor) {
        ui.mosaic_scale_anchor=anchor;ui.mosaic_scale_delta=1;
        ts_ui_mosaic_transform_event(&ui,&tiny,&tiny_preview);
        assert(tiny_preview.start==0 && tiny_preview.duration==tiny.duration);
    }
    ui.mosaic_grab=0;ui.mosaic_scale_anchor=0;ui.mosaic_scale_delta=0;
    mosaic_select_only(&ui,a->id);mosaic_select_add(&ui,b->id);
    scene->playing=1;scene->time=3;
    uint64_t before=ts_mosaic_hash(scene),pcm=ts_sample_hash(&source->sample);
    int history=scene->history_count,cursor=scene->history_cursor;
    /* S works even when the cards have no useful on-screen resize edge. */
    double hx,hy,radius;assert(!ts_ui_mosaic_resize_handle(&ui,a,2,&hx,&hy,&radius));
    grab_pointer(window,350,180);key(window,SDLK_s,KMOD_NONE);
    assert(ui.mosaic_grab==2 && !ui.mosaic_scale_anchor && !a->solo && !b->solo);
    scale_release_key(window);for(int i=0;i<3;++i)grab_frame();
    assert(ui.mosaic_grab==2 && ui.mosaic_ghost_count==2);
    motion(window,350,190);grab_frame();TsMosaicEvent pa=scale_preview(a),pb=scale_preview(b);
    assert(pa.start==2 && pa.duration==24 && pb.start==8 && pb.duration==22);
    assert(ts_mosaic_hash(scene)==before && scene->history_count==history);
    /* Anchor changes always recompute from the opening geometry. */
    key(window,SDLK_b,KMOD_NONE);pa=scale_preview(a);pb=scale_preview(b);
    assert(fabs(pa.start+pa.duration-6)<1e-9 && fabs(pb.start+pb.duration-10)<1e-9);
    assert(pa.duration==.005 && pb.duration==.005);
    key(window,SDLK_c,KMOD_NONE);pa=scale_preview(a);pb=scale_preview(b);
    assert(pa.start==0 && pa.duration==8 && pb.start==0 && pb.duration==18);
    key(window,SDLK_t,KMOD_NONE);pa=scale_preview(a);pb=scale_preview(b);
    assert(pa.start==2 && pa.duration==24 && pb.start==8 && pb.duration==22);
    for(int i=0;i<256;++i)(void)ts_mosaic_read(scene,48000);
    assert(scene->playing && scene->time>3 && ts_mosaic_hash(scene)==before);
    key(window,SDLK_ESCAPE,KMOD_NONE);
    assert(!ui.mosaic_grab && !mosaic.drag && !ui.mosaic_ghost_count);
    assert(ts_mosaic_hash(scene)==before && scene->history_count==history && scene->history_cursor==cursor);
    /* A group publication is exactly one undoable edit, preserving source/mix. */
    grab_pointer(window,350,180);key(window,SDLK_s,KMOD_NONE);motion(window,350,190);grab_frame();
    key(window,SDLK_RETURN,KMOD_NONE);
    assert(a->start==2 && a->duration==24 && b->start==8 && b->duration==22);
    assert(a->pan==.25f && a->fade_in==.3 && a->fade_out==.5 && a->source==source && b->source==source);
    assert(scene->history_count==history+1 && ts_sample_hash(&source->sample)==pcm);
    key(window,SDLK_z,KMOD_CTRL);assert(a->duration==4 && b->duration==2);
    key(window,SDLK_y,KMOD_CTRL);assert(a->duration==24 && b->duration==22);
    key(window,SDLK_z,KMOD_CTRL);before=ts_mosaic_hash(scene);
    history=scene->history_count;cursor=scene->history_cursor;
    grab_pointer(window,350,180);key(window,SDLK_s,KMOD_NONE);key(window,SDLK_b,KMOD_NONE);
    key(window,SDLK_c,KMOD_NONE);key(window,SDLK_t,KMOD_NONE);key(window,SDLK_RETURN,KMOD_NONE);
    assert(ts_mosaic_hash(scene)==before && scene->history_count==history && scene->history_cursor==cursor);
    /* Bottom-up growth clamps each card independently at zero. */
    ui.mosaic_scale=1;
    grab_pointer(window,350,180);key(window,SDLK_s,KMOD_NONE);key(window,SDLK_b,KMOD_NONE);
    motion(window,350,176);grab_frame();pa=scale_preview(a);pb=scale_preview(b);
    assert(pa.start==0 && pa.duration==6 && pb.start==4 && pb.duration==6);
    click(window,350,176,1);release(window,350,176);
    assert(!ui.mosaic_grab && !mosaic.drag && !ui.mosaic_edit_choice);
    assert(a->start==0 && a->duration==6 && b->start==4 && b->duration==6);
    key(window,SDLK_z,KMOD_CTRL);assert(a->start==2 && a->duration==4 && b->start==8 && b->duration==2);
    /* Center growth keeps each midpoint, with independent zero boundaries. */
    grab_pointer(window,350,180);key(window,SDLK_s,KMOD_NONE);key(window,SDLK_c,KMOD_NONE);
    motion(window,350,184);grab_frame();pa=scale_preview(a);pb=scale_preview(b);
    assert(pa.start==0 && pa.duration==8 && pb.start==4 && pb.duration==10);
    assert(pa.start+pa.duration*.5==4 && pb.start+pb.duration*.5==9);
    key(window,SDLK_RETURN,KMOD_NONE);key(window,SDLK_z,KMOD_CTRL);
    assert(a->start==2 && a->duration==4 && b->start==8 && b->duration==2);
    /* Minimum duration, right-click cancel and sampled focus loss leave history. */
    before=ts_mosaic_hash(scene);history=scene->history_count;cursor=scene->history_cursor;
    grab_pointer(window,350,180);key(window,SDLK_s,KMOD_NONE);motion(window,350,-10000);grab_frame();
    pa=scale_preview(a);assert(pa.start==2 && pa.duration==.005);
    grab_right_click(window,350,180);assert(ts_mosaic_hash(scene)==before && !ui.mosaic_grab);
    grab_pointer(window,350,180);key(window,SDLK_s,KMOD_NONE);motion(window,350,190);grab_frame();
    mosaic_drag_service(0,&ui,&mosaic,SDL_GetTicks(),0,0);
    assert(!ui.mosaic_grab && !mosaic.drag && ts_mosaic_hash(scene)==before);
    assert(scene->history_count==history && scene->history_cursor==cursor);
    /* AUTO-SPACE only adds one rigid horizontal shift to the duration preview. */
    TsMosaicEvent *obstacle=ts_mosaic_add(scene,source,7,20);assert(obstacle);obstacle->duration=1;scene->auto_space=1;
    grab_pointer(window,350,180);key(window,SDLK_s,KMOD_NONE);motion(window,350,184);grab_frame();
    pa=scale_preview(a);pb=scale_preview(b);
    assert(pa.x>=obstacle->x+obstacle->width+TS_MOSAIC_GUTTER && pb.x-pa.x==b->x-a->x);
    assert(a->start==2 && a->duration==4);key(window,SDLK_ESCAPE,KMOD_NONE);scene->auto_space=0;
    /* Solo remains available, including every instance of a selected source. */
    mosaic_select_only(&ui,a->id);key(window,SDLK_s,KMOD_ALT);assert(a->solo && !b->solo && !obstacle->solo);
    key(window,SDLK_s,KMOD_ALT);assert(!a->solo);
    key(window,SDLK_s,KMOD_ALT|KMOD_SHIFT);assert(a->solo && b->solo && obstacle->solo);
    key(window,SDLK_s,KMOD_ALT|KMOD_SHIFT);assert(!a->solo && !b->solo && !obstacle->solo);
    ts_mosaic_delete(scene,obstacle->id);
    /* Clearly marked grips work below the old 24-pixel resize threshold. */
    ui.mosaic_live_open=0;ui.mosaic_scale=5;ui.mosaic_hscale=.5;
    assert(ts_ui_mosaic_resize_handle(&ui,a,2,&hx,&hy,&radius));
    motion(window,(int)hx,(int)hy);assert(ui.mosaic_resize_event==a->id && ui.mosaic_resize_part==2);
    click(window,(int)hx,(int)hy,1);assert(mosaic.drag==2);
    motion(window,(int)hx,(int)hy+5);release(window,(int)hx,(int)hy+5);assert(a->duration==5);
    key(window,SDLK_z,KMOD_CTRL);assert(a->duration==4);
    /* A video instruction also scales without opening its editor. */
    TsMosaicSource *video=ts_mosaic_video_source(scene,TS_VIDEO_FIXTURE,0);assert(video);
    TsMosaicEvent *v=ts_mosaic_add(scene,video,5,280);assert(v);v->duration=2;
    mosaic_select_only(&ui,v->id);ui.mosaic_scale=1;
    grab_pointer(window,350,180);key(window,SDLK_s,KMOD_NONE);motion(window,350,184);grab_frame();
    key(window,SDLK_RETURN,KMOD_NONE);assert(v->duration==6 && v->start==5 && !ui.mosaic_video_open);
    key(window,SDLK_z,KMOD_CTRL);assert(v->duration==2);
    /* Native and logical renderers share the same anchored preview and grips. */
    ui.mosaic_scale=24;ui.mosaic_hscale=1;mosaic_select_only(&ui,a->id);mosaic_select_add(&ui,b->id);
    grab_pointer(window,350,220);key(window,SDLK_s,KMOD_NONE);key(window,SDLK_c,KMOD_NONE);
    motion(window,350,244);grab_frame();
    const char *shot=getenv("TS_TEST_MOSAIC_SCALE");
    if(shot)live_controller_screenshot_size(&ui,shot,1729,973);
    else live_controller_screenshot_size(&ui,NULL,1729,973);
    assert(ui.mosaic_grab==2 && a->duration==4 && b->duration==2);
    key(window,SDLK_ESCAPE,KMOD_NONE);
    /* QWERTY still owns S when explicitly enabled, just as it owns G. */
    ui.mosaic_source_keys=1;key(window,SDLK_s,KMOD_NONE);assert(!ui.mosaic_grab);
    scale_release_key(window);ui.mosaic_source_keys=0;
    /* Scene replacement cancels a pending scale before reused IDs can move. */
    key(window,SDLK_s,KMOD_NONE);assert(ui.mosaic_grab==2);
    TsMosaic *other=ts_mosaic_create();assert(other);ui.mosaic=audio.mosaic=other;
    mosaic_poll(0,&ui,&instrument,&mosaic);assert(!ui.mosaic_grab && !mosaic.drag);
    ts_mosaic_free(other);ui.mosaic=audio.mosaic=saved;ts_mosaic_free(scene);
    ui.mosaic_scale=24;ui.mosaic_hscale=1;ui.mosaic_follow=0;ui.mosaic_live_open=0;ui.mosaic_live_keys=1;
    mosaic_poll(0,&ui,&instrument,&mosaic);
}
