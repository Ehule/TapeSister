static void test_mosaic_volume_controls(SDL_Window *window)
{
    TsMosaic *saved=ui.mosaic,*scene=ts_mosaic_create();assert(scene);ui.mosaic=audio.mosaic=scene;
    char error[160];TsMosaicSource *source=ts_mosaic_source(scene,&instrument.current,error,sizeof(error));assert(source);
    TsMosaicEvent *a=ts_mosaic_add(scene,source,0,20);a->duration=20;
    ui.mosaic_open=1;ui.mosaic_scale=10;ui.mosaic_hscale=1;ui.mosaic_scroll=ui.mosaic_xscroll=0;
    mosaic_poll(0,&ui,&instrument,&mosaic);mosaic_select_only(&ui,a->id);
    int history=scene->history_count;
    click(window,599,116,1);motion(window,581,216);release(window,581,216);
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
    click(window,605,166,1);release(window,605,166);
    assert(fabsf(ts_mosaic_volume_at(scene,.75)-.6f)<.02f);
    click(window,575,75,1);assert(ts_mosaic_volume_at(scene,.75)==1);
    ts_mosaic_volume_draw(scene,0,.2f,1,.7f);
    click(window,598,75,1);assert(scene->volume[0]==scene->volume[256]);
    ts_mosaic_volume_draw(scene,0,.2f,1,.7f);
    click(window,620,366,1);assert(scene->volume[0]==scene->volume[256]);
    click(window,290,45,1);assert(scene->repeat && scene->volume[0]==scene->volume[256]);
    ui.mosaic=audio.mosaic=saved;mosaic_poll(0,&ui,&instrument,&mosaic);ts_mosaic_free(scene);
}
