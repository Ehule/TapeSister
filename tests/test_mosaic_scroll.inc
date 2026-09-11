/* A moving view must translate a waveform rather than reslice its transients.
   Exercise the same native surfaces used in the window at integer and fractional
   UI scales, then check the fixed content margins at both canvas edges. */
static void test_mosaic_scroll_waveforms(void)
{
    static TsUiState view;static TsFramebuffer fb;static TsInstrument doc;
    ts_instrument_init(&doc);
    const int sizes[][2]={{2560,1600},{1920,1200},{1729,973}};
    for(int pass=0;pass<3;++pass) {
        ts_ui_init(&view);view.mosaic=ts_mosaic_create();assert(view.mosaic);
        view.mosaic_open=1;view.mosaic_scale=24;view.mosaic_hscale=1;view.mosaic_time=-1;
        TsSample sample;ts_sample_init(&sample);sample.sample_rate=7200;
        sample.channels=1;sample.frames=144000;sample.data=calloc(sample.frames,sizeof(float));assert(sample.data);
        int width=sizes[pass][0],height=sizes[pass][1];
        int native_height=376*height/400-66*height/400;
        double rate=24.0*native_height/310;
        /* A sparse transient makes width-morphing visible. A scroll must keep
           the same raster profile after accounting for whole-pixel movement. */
        sample.data[(size_t)(121.5*sample.sample_rate/rate)]=1;sample.data[2141]=.8f;
        char error[160];TsMosaicSource *source=ts_mosaic_source(view.mosaic,&sample,error,sizeof(error));assert(source);
        TsMosaicEvent *event=ts_mosaic_add(view.mosaic,source,.137,20);assert(event);
        event->width=100;event->duration=20;event->looping=0;
        TsUiWaveformDetail detail[2]={0};uint32_t *reference=NULL;
        for(int mode=0;mode<2;++mode) {
            event->looping=mode;
            if(mode){event->first=19;event->last=5157;}
            unsigned changed=0;Uint64 started=SDL_GetPerformanceCounter();
            for(int step=0;step<40;++step) {
                double travel=step*.173; /* Includes many frames between raster steps. */
                view.mosaic_scroll=event->start+(100+travel)/rate;
                ts_ui_waveform_details_begin(detail,width,height);ts_ui_render(&fb,&view,&doc);
                assert(ts_ui_waveform_detail_finish(&detail[0],&fb));
                TsUiWaveformDetail *d=&detail[0];
                if(!step) {
                    reference=malloc((size_t)d->width*d->height*sizeof(*reference));assert(reference);
                    memcpy(reference,d->pixels,(size_t)d->width*d->height*sizeof(*reference));
                } else {
                    int shift=(int)floor(travel+.5);
                    for(int y=4;y<210;++y)for(int x=25*d->width/480;x<115*d->width/480;++x)
                        changed+=reference[(size_t)(y+shift)*d->width+x]!=d->pixels[(size_t)y*d->width+x];
                }
            }
            printf("Mosaic scroll %dx%d %s: translated profile changes=%u, render_ms=%.3f\n",width,height,mode?"loop":"one-shot",changed,
                1000.0*(SDL_GetPerformanceCounter()-started)/SDL_GetPerformanceFrequency()/40);
            if(!getenv("TS_MOSAIC_SCROLL_COMPARE"))assert(changed==0);
            free(reference);
        }
        /* A constant signal exposes a waveform jumping into a clipped header
           or footer. These margins belong to the tile, even while offscreen. */
        for(size_t i=0;i<sample.frames;++i)sample.data[i]=.6f;
        TsMosaicSource *constant=ts_mosaic_source(view.mosaic,&sample,error,sizeof(error));assert(constant);
        event->source=constant;event->first=0;event->last=sample.frames;event->looping=1;event->start=0;
        for(int edge=0;edge<2;++edge) {
            view.mosaic_scroll=edge?0:2.0/24;
            event->duration=edge?(381.0-66)/24:20;
            ts_ui_waveform_details_begin(detail,width,height);ts_ui_render(&fb,&view,&doc);
            assert(ts_ui_waveform_detail_finish(&detail[0],&fb));
            int y=edge?370:70,x=221;
            assert(fb.pixels[y*640+x]==0xff201f27u);
            TsUiWaveformDetail *d=&detail[0];
            int px=(int)((x-151+.5)*d->width/480),py=(int)((y-66+.5)*d->height/310);
            assert(d->pixels[(size_t)py*d->width+px]==0xff201f27u);
        }
        for(int i=0;i<2;++i)ts_ui_waveform_detail_free(&detail[i]);
        ts_sample_free(&sample);ts_mosaic_free(view.mosaic);view.mosaic=NULL;
    }
    ts_instrument_free(&doc);
    puts("Mosaic scrolling translation and clipped content margins passed");
}
