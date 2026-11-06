/* Exercise the overview through real picks and both renderers. Geometry,
   history and transport must stay untouched by hover and presentation. */
static void test_mosaic_overview(SDL_Window *window)
{
    TsMosaic *saved=ui.mosaic,*scene=ts_mosaic_create();assert(scene);
    ui.mosaic=audio.mosaic=scene;
    char error[160];TsMosaicSource *source=ts_mosaic_source(scene,&instrument.current,error,sizeof(error));assert(source);
    TsMosaicEvent *a=ts_mosaic_add(scene,source,20,20),*b=ts_mosaic_add(scene,source,70,20);assert(a && b);
    a->duration=b->duration=12;a->width=b->width=86;
    snprintf(a->name,sizeof(a->name),"ELECTRICAL MASS / OVERLAPPING SMALL IGNITIONS");
    ui.mosaic_open=1;ui.mosaic_scale=1;ui.mosaic_hscale=.55;
    ui.mosaic_scroll=ui.mosaic_xscroll=0;ui.mosaic_time=-1;ui.mosaic_follow=0;
    mosaic_poll(0,&ui,&instrument,&mosaic);mosaic_select_only(&ui,0);
    TsMosaicEvent before=*a;int history=scene->history_count;
    static TsFramebuffer plain,hover;
    ts_ui_render(&plain,&ui,&instrument);
    /* This formerly blank 12px body now contains a readable compact name. */
    unsigned glyphs=0;
    for(int y=88;y<95;++y)for(int x=165;x<205;++x)
        glyphs+=plain.pixels[y*640+x]==ts_theme_color(&ui.palette,TS_THEME_MOSAIC,TS_COLOR_TEXT,0xffe0d6e9);
    assert(glyphs>12);
    motion(window,178,92);assert(ui.mosaic_hover_event==a->id && !ui.mosaic_selected);
    ts_ui_render(&hover,&ui,&instrument);
    assert(plain.pixels[86*640+162]!=hover.pixels[86*640+162]);
    assert(!memcmp(a,&before,sizeof(before)) && scene->history_count==history && scene->time==0);
    /* A stationary pointer follows the new visible card after scrolling. */
    ui.mosaic_scroll=50;mosaic_poll(0,&ui,&instrument,&mosaic);assert(ui.mosaic_hover_event==b->id);
    motion(window,400,200);assert(!ui.mosaic_hover_event);
    ui.mosaic_scroll=0;motion(window,178,92);assert(ui.mosaic_hover_event==a->id);
    SDL_Event leave;SDL_zero(leave);leave.type=SDL_WINDOWEVENT;leave.window.windowID=SDL_GetWindowID(window);
    leave.window.event=SDL_WINDOWEVENT_LEAVE;dispatch(&leave,window);mosaic_poll(0,&ui,&instrument,&mosaic);
    assert(!ui.mosaic_hover_event);
    motion(window,178,92);ui.mosaic_scatter_open=1;mosaic_poll(0,&ui,&instrument,&mosaic);assert(!ui.mosaic_hover_event);
    ui.mosaic_scatter_open=0;
    /* Hover uses the existing front-card ordering and tiny-card hit padding. */
    b->start=a->start;b->duration=2;
    motion(window,178,87);assert(ui.mosaic_hover_event==b->id);
    mosaic_select_only(&ui,a->id);motion(window,178,87);assert(ui.mosaic_hover_event==a->id);
    assert(scene->history_count==history);
    mosaic_drag_end(&ui,&mosaic);ui.mosaic=audio.mosaic=saved;ts_mosaic_free(scene);

    /* A long dense arrangement at the screenshot's horizontal zoom. */
    TsInstrument *doc=calloc(1,sizeof(*doc));assert(doc);ts_instrument_init(doc);
    scene=ts_mosaic_create();assert(scene);ui.mosaic=audio.mosaic=scene;
    TsMosaicSource *sources[5];
    const char *names[]={"ELECTRICAL MASS","SMALL IGNITIONS","BOWED SIGNAL","CHOKING TAILS","CURRENT FRACTURE"};
    TsInstrument *generator=calloc(1,sizeof(*generator));assert(generator);ts_instrument_init(generator);
    unsigned colors=0;
    for(int i=0;i<5;++i) {
        for(int trial=0;trial<40;++trial) {
            assert(ts_instrument_generate(generator,TS_GENERATOR_METALLIC,720+i*73+trial,error,sizeof(error)));
            unsigned color=(unsigned)(ts_sample_hash(&generator->current)%5);
            if(!(colors&(1u<<color)) || trial==39){colors|=1u<<color;break;}
        }
        assert(ts_instrument_select_bank(doc,i,error,sizeof(error)));
        assert(ts_instrument_import_sample(doc,&generator->current,0,0,0,TS_LOOP_FORWARD,error,sizeof(error)));
        snprintf(doc->current.name,sizeof(doc->current.name),"%s",names[i]);
        sources[i]=ts_mosaic_source(scene,&doc->current,error,sizeof(error));assert(sources[i]);
    }
    ts_instrument_free(generator);free(generator);
    const double durations[]={12,38,68,90,130};
    for(int row=0;row<18;++row)for(int col=0;col<7;++col) {
        int kind=(row+col)%5;
        TsMosaicEvent *e=ts_mosaic_add(scene,sources[kind],row*80+col*3,col*98);assert(e);
        e->duration=durations[(row*3+col)%5];e->width=86;
        e->muted=row==5 && col==3;e->fade_in=e->duration*.12;e->fade_out=e->duration*.18;
    }
    ui.mosaic_scale=.19;ui.mosaic_hscale=.65;ui.mosaic_scroll=ui.mosaic_xscroll=0;
    scene->time=974.87;ui.mosaic_playing=0;ui.config.mosaic_volume_hidden=1;
    mosaic_poll(0,&ui,doc,&mosaic);mosaic_select_only(&ui,scene->events[58].id);
    ui.mosaic_hover_event=scene->events[14].id;
    ts_ui_render(&plain,&ui,doc);
    const char *shot=getenv("TS_MOSAIC_OVERVIEW_SCREENSHOT");
    if(shot)assert(ts_ui_write_ppm(&plain,shot));
    mosaic_native_check(doc,getenv("TS_MOSAIC_OVERVIEW_NATIVE_SCREENSHOT"));
    if(getenv("TS_MOSAIC_OVERVIEW_COMPACT_SCREENSHOT")) {
        ui.mosaic_scale=.4;ui.mosaic_scroll=400;
        mosaic_native_check(doc,getenv("TS_MOSAIC_OVERVIEW_COMPACT_SCREENSHOT"));
        ui.mosaic_scale=.19;ui.mosaic_scroll=0;
    }
    /* Worst-case layout remains bounded, even when most cards are offscreen. */
    for(int i=18*7;i<TS_MOSAIC_EVENTS;++i) {
        TsMosaicEvent *e=ts_mosaic_add(scene,sources[i%5],i*30,1000+i*20);assert(e);e->duration=10;
    }
    ts_ui_render(&plain,&ui,doc);
    mosaic_drag_end(&ui,&mosaic);ui.mosaic=audio.mosaic=saved;ts_mosaic_free(scene);ts_instrument_free(doc);free(doc);
    ui.mosaic_scroll=ui.mosaic_xscroll=0;ui.mosaic_scale=24;ui.mosaic_hscale=1;ui.mosaic_follow=0;
    mosaic_poll(0,&ui,&instrument,&mosaic);
}
