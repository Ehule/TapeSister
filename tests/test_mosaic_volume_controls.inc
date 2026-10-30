static void test_mosaic_volume_controls(SDL_Window *window)
{
    TsMosaic *saved=ui.mosaic,*scene=ts_mosaic_create();assert(scene);ui.mosaic=audio.mosaic=scene;
    char error[160];TsMosaicSource *source=ts_mosaic_source(scene,&instrument.current,error,sizeof(error));assert(source);
    TsMosaicEvent *a=ts_mosaic_add(scene,source,0,20);a->duration=20;
    ui.mosaic_open=1;ui.mosaic_scale=10;ui.mosaic_hscale=1;ui.mosaic_scroll=ui.mosaic_xscroll=0;
    mosaic_poll(0,&ui,&instrument,&mosaic);mosaic_select_only(&ui,a->id);
    int history=scene->history_count;
    click(window,599,130,1);motion(window,582,214);release(window,582,214);
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
    click(window,605,172,1);release(window,605,172);
    assert(fabsf(ts_mosaic_volume_at(scene,.75)-.6f)<.02f);
    click(window,575,75,1);assert(ts_mosaic_volume_at(scene,.75)==1);
    /* Fit All exposes both handles inside the frame, below/above the tools. */
    click(window,510,45,1);assert(ui.mosaic_scroll==0 && ui.mosaic_scale==(TS_MOSAIC_CANVAS_HEIGHT-10.)/20);
    click(window,582,89,1);assert(mosaic.volume_drag==2);
    motion(window,585,75);release(window,585,75); /* Crossing a toolbar cannot press it. */
    assert(scene->volume[0]==.25f && scene->volume[128]==1);
    click(window,613,324,1);assert(mosaic.volume_drag==3);release(window,613,324);
    assert(scene->volume[256]==.75f && scene->volume[128]==1);
    hash=ts_mosaic_hash(scene);
    click(window,575,346,1);assert(!mosaic.volume_drag);
    for(int i=0;i<257;++i)assert(fabsf(scene->volume[i]-(.25f+.5f*i/256))<1e-6f);
    key(window,SDLK_z,KMOD_CTRL);assert(ts_mosaic_hash(scene)==hash);
    click(window,598,75,1);assert(scene->volume[0]==.75f && scene->volume[256]==.75f && scene->volume[128]==1);
    key(window,SDLK_z,KMOD_CTRL);assert(ts_mosaic_hash(scene)==hash);
    click(window,620,346,1);assert(scene->volume[0]==.25f && scene->volume[256]==.25f && scene->volume[128]==1);
    key(window,SDLK_z,KMOD_CTRL);assert(ts_mosaic_hash(scene)==hash);
    click(window,290,45,1);assert(scene->repeat && scene->volume[0]==scene->volume[256]);
    click(window,575,346,1);
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
    click(window,585,89,1);motion(window,613,206);release(window,613,206);
    assert(!mosaic.volume_drag && fabsf(ts_mosaic_volume_at(scene,.5)-.75f)<.02f);
    /* Hiding is a view change: envelope, history, selection and geometry stay.
       The reclaimed strip supports cards, not invisible envelope drawing. */
    hash=ts_mosaic_hash(scene);history=scene->history_count;
    ui.mosaic_source_keys=1; /* Shortcut takes priority over the V piano key. */
    key(window,SDLK_v,KMOD_SHIFT);assert(ui.config.mosaic_volume_hidden && !mosaic.volume_drag);
    ui.mosaic_source_keys=0;
    assert(ts_mosaic_hash(scene)==hash && scene->history_count==history && ui.mosaic_selected==a->id);
    double old_x=a->x;a->x=430;
    TsUiWaveformDetail details[2]={0};
    for(int mode=0;mode<2;++mode) {
        ts_ui_waveform_details_begin(details,1729,973);ts_ui_render(&after,&ui,&instrument);
        assert(ts_ui_waveform_detail_finish(&details[0],&after));
        assert(details[0].logical_width==(mode?414:480));
        if(!mode) {
            TsUiWaveformDetail *d=&details[0];int y=84*d->height/292;
            assert(d->pixels[(size_t)y*d->width+449*d->width/480]!=d->pixels[(size_t)y*d->width+249*d->width/480]);
        }
        if(!mode){click(window,TS_MOSAIC_ENV_TOGGLE_X+3,TS_MOSAIC_ENV_TOGGLE_Y+4,1);release(window,635,72);}
    }
    a->x=old_x;ts_ui_waveform_detail_begin(NULL);
    for(int i=0;i<2;++i)ts_ui_waveform_detail_free(&details[i]);
    assert(!ui.config.mosaic_volume_hidden && ts_mosaic_hash(scene)==hash && scene->history_count==history);
    key(window,SDLK_v,KMOD_SHIFT);
    a->x=430;a->width=86;ui.mosaic_xscroll=0;ui.mosaic_hscale=1;ui.mosaic_scroll=0;ui.mosaic_scale=10;
    click(window,600,150,1);release(window,600,150);
    assert(ui.mosaic_selected==a->id && !mosaic.volume_drag);
    const char *shot=getenv("TS_TEST_MOSAIC_VOLUME");
    if(shot){ts_ui_render(&after,&ui,&instrument);assert(ts_ui_write_ppm(&after,shot));}
    key(window,SDLK_v,KMOD_SHIFT);assert(!ui.config.mosaic_volume_hidden);
    ui.mosaic=audio.mosaic=saved;mosaic_poll(0,&ui,&instrument,&mosaic);ts_mosaic_free(scene);
}
