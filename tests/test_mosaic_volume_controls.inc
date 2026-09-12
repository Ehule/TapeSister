static void test_mosaic_volume_controls(SDL_Window *window)
{
    TsMosaic *saved=ui.mosaic,*scene=ts_mosaic_create();assert(scene);ui.mosaic=audio.mosaic=scene;
    char error[160];TsMosaicSource *source=ts_mosaic_source(scene,&instrument.current,error,sizeof(error));assert(source);
    TsMosaicEvent *a=ts_mosaic_add(scene,source,0,20);a->duration=20;
    ui.mosaic_open=1;ui.mosaic_scale=10;ui.mosaic_hscale=1;ui.mosaic_scroll=ui.mosaic_xscroll=0;
    mosaic_poll(0,&ui,&instrument,&mosaic);mosaic_select_only(&ui,a->id);
    int history=scene->history_count;
    click(window,599,131,1);motion(window,582,216);release(window,582,216);
    assert(scene->history_count==history+1 && !mosaic.volume_drag);
    assert(fabsf(ts_mosaic_volume_at(scene,.25)-.5f)<.02f);
    assert(fabsf(ts_mosaic_volume_at(scene,.75)-.2f)<.02f);
    assert(a->gain==1 && a->start==0 && ui.mosaic_selected==a->id);
    key(window,SDLK_z,KMOD_CTRL);assert(ts_mosaic_volume_at(scene,.25)==1);
    key(window,SDLK_y,KMOD_CTRL);assert(ts_mosaic_volume_at(scene,.25)<.6f);
    uint64_t hash=ts_mosaic_hash(scene);
    click(window,570,186,1);motion(window,610,206);key(window,SDLK_ESCAPE,KMOD_NONE);
    assert(!mosaic.volume_drag && ts_mosaic_hash(scene)==hash);
    /* Scroll/zoom remaps the same time coordinates, without moving events. */
    ui.mosaic_scroll=10;ui.mosaic_scale=20;
    click(window,605,174,1);release(window,605,174);
    assert(fabsf(ts_mosaic_volume_at(scene,.75)-.6f)<.02f);
    click(window,575,75,1);assert(ts_mosaic_volume_at(scene,.75)==1);
    /* Fit All exposes both handles inside the frame, below/above the tools. */
    click(window,510,45,1);assert(ui.mosaic_scroll==0 && ui.mosaic_scale==15);
    click(window,582,89,1);assert(mosaic.volume_drag==2);
    motion(window,585,75);release(window,585,75); /* Crossing a toolbar cannot press it. */
    assert(scene->volume[0]==.25f && scene->volume[128]==1);
    click(window,613,344,1);assert(mosaic.volume_drag==3);release(window,613,344);
    assert(scene->volume[256]==.75f && scene->volume[128]==1);
    hash=ts_mosaic_hash(scene);
    click(window,575,366,1);assert(!mosaic.volume_drag);
    for(int i=0;i<257;++i)assert(fabsf(scene->volume[i]-(.25f+.5f*i/256))<1e-6f);
    key(window,SDLK_z,KMOD_CTRL);assert(ts_mosaic_hash(scene)==hash);
    click(window,598,75,1);assert(scene->volume[0]==.75f && scene->volume[256]==.75f && scene->volume[128]==1);
    key(window,SDLK_z,KMOD_CTRL);assert(ts_mosaic_hash(scene)==hash);
    click(window,620,366,1);assert(scene->volume[0]==.25f && scene->volume[256]==.25f && scene->volume[128]==1);
    key(window,SDLK_z,KMOD_CTRL);assert(ts_mosaic_hash(scene)==hash);
    click(window,290,45,1);assert(scene->repeat && scene->volume[0]==scene->volume[256]);
    click(window,575,366,1);
    for(int i=0;i<257;++i)assert(scene->volume[i]==.25f);
    assert(strstr(ui.status,"FLAT RAMP"));
    click(window,598,75,1);assert(strstr(ui.status,"REPEAT ALREADY LINKS"));
    click(window,613,89,1);release(window,613,89);
    assert(scene->volume[0]==.75f && scene->volume[256]==.75f);
    /* Toolbar gaps don't draw; playhead and handles never cover the tools. */
    hash=ts_mosaic_hash(scene);history=scene->history_count;
    click(window,590,85,1);assert(!mosaic.volume_drag && ts_mosaic_hash(scene)==hash && scene->history_count==history);
    static TsFramebuffer before,after;
    ui.mosaic_time=0;ts_ui_render(&before,&ui,&instrument);
    ui.mosaic_time=.6;ts_ui_render(&after,&ui,&instrument);
    for(int y=68;y<84;++y)for(int x=568;x<=630;++x)
        assert(before.pixels[y*TS_UI_WIDTH+x]==after.pixels[y*TS_UI_WIDTH+x]);
    click(window,585,89,1);motion(window,613,216);release(window,613,216);
    assert(!mosaic.volume_drag && fabsf(ts_mosaic_volume_at(scene,.5)-.75f)<.02f);
    ui.mosaic=audio.mosaic=saved;mosaic_poll(0,&ui,&instrument,&mosaic);ts_mosaic_free(scene);
}
