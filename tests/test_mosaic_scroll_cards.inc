/* Compare the complete SDL-composited canvas after translation, including tile
   edges, labels, margins, repeat marks and ghosts, at fractional window scales.
   An optional local project reproduces a reported session without checking its
   recordings into the repository. */
static void test_mosaic_scroll_cards(void)
{
    static TsUiState view;static TsInstrument doc;static TsFramebuffer fb;
    ts_instrument_init(&doc);
    const char *project=getenv("TS_MOSAIC_SCROLL_PROJECT");
    const int sizes[][2]={{2560,1600},{1920,1200},{1729,973},{2560,1600}};
    for(int scene=0;scene<(project?2:1);++scene) {
        ts_ui_init(&view);view.mosaic=ts_mosaic_create();assert(view.mosaic);
        view.mosaic_open=1;view.mosaic_scale=24;view.mosaic_hscale=1;view.mosaic_time=-1;
        char error[160];
        if(scene)assert(ts_mosaic_load(view.mosaic,project,error,sizeof(error)));
        else {
            TsSample sample;ts_sample_init(&sample);sample.sample_rate=7200;
            sample.channels=1;sample.frames=17213;sample.data=calloc(sample.frames,sizeof(float));assert(sample.data);
            for(size_t i=0;i<sample.frames;++i)sample.data[i]=sinf(i*.31f)*(.1f+.7f*(i%1793)/1793.f);
            TsMosaicSource *s=ts_mosaic_source(view.mosaic,&sample,error,sizeof(error));assert(s);
            TsMosaicEvent *a=ts_mosaic_add(view.mosaic,s,1.137,20.17);assert(a);
            a->duration=4.137;a->width=100.37;a->looping=0;snprintf(a->name,sizeof(a->name),"TRANSIENT");
            view.mosaic_selected=a->id;
            TsMosaicEvent *b=ts_mosaic_add(view.mosaic,s,a->start+a->duration,a->x);assert(b);
            b->duration=9.73;b->width=a->width;b->looping=1;
            TsMosaicEvent *c=ts_mosaic_add(view.mosaic,s,3.71,151.73);assert(c);
            c->duration=6.93;c->muted=1;c->note_count=3;c->notes[1]=67;c->notes[2]=72;
            view.mosaic_ghost_count=1;view.mosaic_ghosts[0]=*b;view.mosaic_ghosts[0].x=270.37;
            ts_sample_free(&sample);
        }
        for(int pass=0;pass<4;++pass) {
            view.mosaic_scale=pass==3?3.17:24;
            int width=sizes[pass][0],height=sizes[pass][1];
            SDL_Window *w=SDL_CreateWindow("Mosaic scroll regression",0,0,width,height,SDL_WINDOW_HIDDEN);assert(w);
            SDL_Renderer *r=SDL_CreateRenderer(w,-1,SDL_RENDERER_SOFTWARE);assert(r);
            SDL_Texture *base=SDL_CreateTexture(r,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,640,400);assert(base);
            SDL_Texture *textures[2]={0};TsUiWaveformDetail details[2]={0};
            uint32_t *pixels=malloc((size_t)width*height*4),*reference=malloc((size_t)width*height*4);assert(pixels && reference);
            double sx=(double)(TS_MOSAIC_CANVAS_RIGHT*width/640-151*width/640)/(TS_MOSAIC_CANVAS_RIGHT-151);
            double sy=(double)(376*height/400-66*height/400)/310,rate=view.mosaic_scale*sy;
            unsigned long long changed=0,compared=0,ruler_changed=0,ruler_compared=0;
            /* Forward and backward, with horizontal movement too. Start near
               FOLLOW's activation point in the supplied arrangement. */
            for(int step=0;step<48;++step) {
                double travel=(step<24?step:47-step)*.613;
                view.mosaic_scroll=(scene?9.37:.37)+travel/rate;
                view.mosaic_xscroll=3.17+travel*.53/sx;
                native_waveform_prepare(r,details,1);ts_ui_render(&fb,&view,&doc);
                assert(!SDL_UpdateTexture(base,NULL,fb.pixels,640*4));
                assert(!SDL_RenderCopy(r,base,NULL,NULL));native_waveform_copy(r,textures,details,&fb);
                assert(!SDL_RenderReadPixels(r,NULL,SDL_PIXELFORMAT_ARGB8888,pixels,width*4));
                TsUiWaveformDetail *d=&details[0];assert(d->valid);
                if(!step)memcpy(reference,pixels,(size_t)width*height*4);
                else {
                    int dx=(int)(floor(view.mosaic_xscroll*sx+.5)-floor(3.17*sx+.5));
                    int dy=(int)(floor(view.mosaic_scroll*rate+.5)-floor((scene?9.37:.37)*rate+.5));
                    for(int y=0;y<d->height-dy;++y)for(int x=0;x<d->width-dx;++x) {
                        size_t at=(size_t)(d->output_y+y)*width+d->output_x+x;
                        changed+=pixels[at]!=reference[at+(size_t)dy*width+dx];++compared;
                    }
                    /* The ruler follows vertical movement only, including
                       partial glyphs crossing either canvas edge. */
                    for(int y=0;y<d->height-dy;++y)for(int x=113*width/640;x<151*width/640;++x) {
                        size_t at=(size_t)(d->output_y+y)*width+x;
                        ruler_changed+=pixels[at]!=reference[at+(size_t)dy*width];++ruler_compared;
                    }
                }
            }
            printf("Mosaic complete canvas %s %dx%d: %llu/%llu translated pixels changed\n",scene?"local project":"synthetic",width,height,changed,compared);
            if(!getenv("TS_MOSAIC_SCROLL_COMPARE"))assert(changed==0);
            printf("Mosaic ruler %s %dx%d scale %.2f: %llu/%llu translated pixels changed\n",scene?"local project":"synthetic",width,height,view.mosaic_scale,ruler_changed,ruler_compared);
            if(!getenv("TS_MOSAIC_SCROLL_COMPARE"))assert(ruler_changed==0);
            if(scene && (pass==1 || pass==3)) {
                /* Run the real audio clock and FOLLOW controller across the
                   scroll threshold, masking only the independent playhead ink. */
                const char *frames=getenv("TS_MOSAIC_SCROLL_FRAMES");
                MosaicController follow={0};view.mosaic_follow=1;view.mosaic_scroll=view.mosaic_xscroll=0;
                int from=pass==3?70:7,pixel_scale=height/400;
                ts_mosaic_seek(view.mosaic,from);view.mosaic->playing=1;
                int previous_pan=0,previous_head=0;changed=compared=ruler_changed=ruler_compared=0;
                for(int frame=0;frame<210;++frame) {
                    for(int n=0;n<1600;++n)(void)ts_mosaic_read(view.mosaic,48000);
                    mosaic_poll(0,&view,&doc,&follow);
                    native_waveform_prepare(r,details,1);ts_ui_render(&fb,&view,&doc);
                    assert(!SDL_UpdateTexture(base,NULL,fb.pixels,640*4));
                    assert(!SDL_RenderCopy(r,base,NULL,NULL));native_waveform_copy(r,textures,details,&fb);
                    assert(!SDL_RenderReadPixels(r,NULL,SDL_PIXELFORMAT_ARGB8888,pixels,width*4));
                    TsUiWaveformDetail *d=&details[0];
                    int pan=(int)floor(view.mosaic_scroll*rate+.5);
                    int head=(int)((view.mosaic_time-view.mosaic_scroll)*view.mosaic_scale)*pixel_scale;
                    if(frame) {
                        int dy=pan-previous_pan;assert(dy>=0);
                        for(int y=0;y<d->height-dy;++y) {
                            if((y>=head && y<head+pixel_scale) || (y+dy>=previous_head && y+dy<previous_head+pixel_scale))continue;
                            for(int x=0;x<d->width;++x) {
                                size_t at=(size_t)(d->output_y+y)*width+d->output_x+x;
                                changed+=pixels[at]!=reference[at+(size_t)dy*width];++compared;
                            }
                        }
                        for(int y=0;y<d->height-dy;++y) {
                            /* Include the ruler and tick stubs, excluding the
                               arrow which is two logical rows above the line. */
                            if((y>=head-2*pixel_scale && y<head+3*pixel_scale) ||
                               (y+dy>=previous_head-2*pixel_scale && y+dy<previous_head+3*pixel_scale))continue;
                            for(int x=113*width/640;x<151*width/640;++x) {
                                size_t at=(size_t)(d->output_y+y)*width+x;
                                ruler_changed+=pixels[at]!=reference[at+(size_t)dy*width];++ruler_compared;
                            }
                        }
                    }
                    if(frames) {
                        char path[4096];snprintf(path,sizeof(path),"%s/%d-%03d.bmp",frames,from,frame);
                        SDL_Surface *surface=SDL_CreateRGBSurfaceWithFormatFrom(pixels,width,height,32,width*4,SDL_PIXELFORMAT_ARGB8888);
                        assert(surface);assert(!SDL_SaveBMP(surface,path));SDL_FreeSurface(surface);
                    }
                    memcpy(reference,pixels,(size_t)width*height*4);previous_pan=pan;previous_head=head;
                }
                printf("Mosaic project FOLLOW %d-%d seconds: canvas %llu/%llu, ruler %llu/%llu translated pixels changed\n",from,from+7,changed,compared,ruler_changed,ruler_compared);
                if(!getenv("TS_MOSAIC_SCROLL_COMPARE")){assert(changed==0);assert(ruler_changed==0);}
                view.mosaic->playing=0;view.mosaic_time=-1;view.mosaic_follow=0;
            }
            free(pixels);free(reference);
            for(int i=0;i<2;++i){SDL_DestroyTexture(textures[i]);ts_ui_waveform_detail_free(&details[i]);}
            SDL_DestroyTexture(base);SDL_DestroyRenderer(r);SDL_DestroyWindow(w);
        }
        ts_mosaic_free(view.mosaic);view.mosaic=NULL;
    }
    ts_instrument_free(&doc);
}
