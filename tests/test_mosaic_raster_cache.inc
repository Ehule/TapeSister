static void check_scene_cache(TsUiState *ui,TsInstrument *instrument,
    TsUiWaveformDetail warm[2],int width,int height)
{
    static TsFramebuffer painted,reference;
    TsUiWaveformDetail cold[2]={{0}};
    uint32_t *previous=NULL;int old_w=warm[0].width,old_h=warm[0].height;
    if(warm[0].valid) {
        previous=malloc((size_t)old_w*old_h*4);assert(previous);
        memcpy(previous,warm[0].pixels,(size_t)old_w*old_h*4);
    }
    ts_ui_waveform_details_begin(warm,width,height);ts_ui_render(&painted,ui,instrument);
    for(int i=0;i<2;++i)assert(ts_ui_waveform_detail_finish(&warm[i],&painted));
    ts_ui_waveform_details_begin(cold,width,height);ts_ui_render(&reference,ui,instrument);
    for(int i=0;i<2;++i) {
        assert(ts_ui_waveform_detail_finish(&cold[i],&reference));
        assert(warm[i].width==cold[i].width && warm[i].height==cold[i].height);
        assert(!memcmp(warm[i].pixels,cold[i].pixels,(size_t)warm[i].width*warm[i].height*4));
        ts_ui_waveform_detail_free(&cold[i]);
    }
    assert(!memcmp(painted.pixels,reference.pixels,sizeof(painted.pixels)));
    if(previous && old_w==warm[0].width && old_h==warm[0].height) {
        TsUiWaveformDetail *d=&warm[0];
        for(int y=0;y<old_h;++y)for(int x=0;x<old_w;++x)
            if(x<d->dirty_x || x>=d->dirty_x+d->dirty_w || y<d->dirty_y || y>=d->dirty_y+d->dirty_h)
                assert(previous[(size_t)y*old_w+x]==d->pixels[(size_t)y*old_w+x]);
    }
    free(previous);
}
static void test_mosaic_raster_cache(TsUiState *ui,TsInstrument *instrument)
{
    TsMosaic *m=ts_mosaic_create();assert(m);char error[256];
    TsMosaicSource *source=ts_mosaic_source(m,&instrument->current,error,sizeof(error));assert(source);
    for(int i=0;i<24;++i) {
        TsMosaicEvent *e=ts_mosaic_add(m,source,i*.4,(i%5)*80);assert(e);
        e->duration=3;e->looping=1;e->width=95;e->fade_in=.2;e->fade_out=.3;
    }
    ui->mosaic=m;ui->mosaic_open=1;ui->mosaic_scale=28;ui->mosaic_hscale=1;
    TsUiWaveformDetail warm[2]={{0}};
    const int sizes[][2]={{640,400},{1280,800},{1919,1079},{2560,1440}};
    for(unsigned size=0;size<sizeof(sizes)/sizeof(sizes[0]);++size) {
        int w=sizes[size][0],h=sizes[size][1];check_scene_cache(ui,instrument,warm,w,h);
        uint64_t count=warm[0].raster_count;
        for(int i=0;i<8;++i) {
            ui->mosaic_time=i*.17;check_scene_cache(ui,instrument,warm,w,h);
            assert(warm[0].raster_count==count);
        }
        check_scene_cache(ui,instrument,warm,w,h);assert(!warm[0].dirty_w && !warm[0].dirty_h);
        ui->mosaic_hover_event=m->events[1].id;check_scene_cache(ui,instrument,warm,w,h);
        ui->mosaic_selected=m->events[2].id;ui->mosaic_selection[3]=m->events[3].id;
        check_scene_cache(ui,instrument,warm,w,h);
        m->events[2].pan=.7f;m->events[3].muted=1;check_scene_cache(ui,instrument,warm,w,h);
        m->events[4].solo=1;check_scene_cache(ui,instrument,warm,w,h);
        ui->mosaic_scroll+=.123;ui->mosaic_xscroll+=.35;check_scene_cache(ui,instrument,warm,w,h);
        ui->mosaic_scale*=1.11;ui->mosaic_hscale*=.97;check_scene_cache(ui,instrument,warm,w,h);
        ui->mosaic_grab=1;ui->mosaic_grab_x=7;ui->mosaic_grab_time=.17;
        check_scene_cache(ui,instrument,warm,w,h);ui->mosaic_grab=0;check_scene_cache(ui,instrument,warm,w,h);
        ui->mosaic_box=1;ui->mosaic_box_x=180;ui->mosaic_box_y=88;ui->mosaic_box_end_x=440;ui->mosaic_box_end_y=300;
        check_scene_cache(ui,instrument,warm,w,h);ui->mosaic_box=0;check_scene_cache(ui,instrument,warm,w,h);
        ui->mosaic_live_open=ui->mosaic_live_advanced=1;check_scene_cache(ui,instrument,warm,w,h);
        ui->mosaic_live_open=ui->mosaic_live_advanced=0;check_scene_cache(ui,instrument,warm,w,h);
        ui->palette.colors[TS_PALETTE_MOSAIC_TILE_1]^=0x00202020u;check_scene_cache(ui,instrument,warm,w,h);
        /* A painted but unpresented edit must survive a subsequent cache hit. */
        static TsFramebuffer skipped;m->events[0].name[0]='X';
        ts_ui_waveform_details_begin(warm,w,h);ts_ui_render(&skipped,ui,instrument);
        check_scene_cache(ui,instrument,warm,w,h);
        m->events[0].live=1;check_scene_cache(ui,instrument,warm,w,h);
        m->events[0].live=0;check_scene_cache(ui,instrument,warm,w,h);
    }
    for(int i=0;i<2;++i)ts_ui_waveform_detail_free(&warm[i]);
    ui->mosaic_open=0;ui->mosaic=NULL;ts_mosaic_free(m);
}
