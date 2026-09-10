static void native_pixels_shot(const char *name, const uint32_t *pixels, int width, int height)
{
    const char *dir=getenv("TS_TEST_SUSTAIN_SHOTS"); if(!dir)return;
    char path[1024];snprintf(path,sizeof(path),"%s/%s.ppm",dir,name);
    FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n%d %d\n255\n",width,height);
    for(size_t i=0;i<(size_t)width*height;++i) {
        unsigned char rgb[3]={(unsigned char)(pixels[i]>>16),(unsigned char)(pixels[i]>>8),(unsigned char)pixels[i]};
        assert(fwrite(rgb,1,3,f)==3);
    }
    assert(!fclose(f));
}

static void test_native_waveform_and_window(void)
{
    static TsInstrument instrument;static TsUiState ui;static TsFramebuffer fb;
    TsSample sample;char error[160];ts_sample_init(&sample);ts_instrument_init(&instrument);ts_ui_init(&ui);
    sample.frames=8192;sample.sample_rate=48000;sample.channels=2;
    sample.data=malloc(sample.frames*2*sizeof(float));assert(sample.data);
    for(size_t i=0;i<sample.frames;++i) {
        sample.data[2*i]=.23f*sinf(i*.032f)+.12f*sinf(i*.091f)+.045f*sinf(i*.39f);
        sample.data[2*i+1]=.2f*sinf(i*.029f+.7f)+.11f*sinf(i*.089f)+.03f*sinf(i*.45f);
    }
    assert(ts_instrument_import_sample(&instrument,&sample,0,0,sample.frames,TS_LOOP_FORWARD,error,sizeof(error)));
    instrument.view_first=1000;instrument.view_last=3048;
    ts_instrument_set_selection_snapped(&instrument,1450,2350);
    uint64_t hash=ts_sample_hash(&instrument.current);
    SDL_Window *window=SDL_CreateWindow("Native waveform",0,0,2560,1600,SDL_WINDOW_HIDDEN);assert(window);
    SDL_Renderer *renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);assert(renderer);
    SDL_Texture *base=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,640,400);assert(base);
    SDL_Texture *wave=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,2400,536);assert(wave);
    TsUiWaveformDetail detail={0};assert(ts_ui_waveform_detail_resize(&detail,2400,536));
    ts_ui_waveform_detail_begin(&detail);ts_ui_render(&fb,&ui,&instrument);
    ts_ui_render_file_recording(&fb,&ui);
    assert(ts_ui_waveform_detail_finish(&detail,&fb));
    assert(detail.analysis_count==1);
    size_t changes=0;
    for(int x=1;x<2400;++x)changes+=detail.columns[x].first!=detail.columns[x-1].first;
    assert(changes>2000); /* More than 600 independent audio bins. */
    int fractional_detail=0;
    for(int y=20;y<500;++y)for(int x=80;x<2300;x+=4)
        fractional_detail+=detail.pixels[(size_t)y*2400+x]!=detail.pixels[(size_t)y*2400+x+1];
    assert(fractional_detail>1000); /* Physical pixels inside a former UI pixel differ. */
    assert(!SDL_UpdateTexture(base,NULL,fb.pixels,640*sizeof(uint32_t)));
    assert(!SDL_UpdateTexture(wave,NULL,detail.pixels,2400*sizeof(uint32_t)));
    assert(!SDL_RenderCopy(renderer,base,NULL,NULL));SDL_Rect target={80,256,2400,536};
    assert(!SDL_RenderCopy(renderer,wave,NULL,&target));
    uint32_t *pixels=malloc(2560u*1600u*sizeof(uint32_t));assert(pixels);
    assert(!SDL_RenderReadPixels(renderer,NULL,SDL_PIXELFORMAT_ARGB8888,pixels,2560*sizeof(uint32_t)));
    native_pixels_shot("native-waveform-detail",pixels,2560,1600);
    /* Editing labels/buttons remain at their established pixel scale. */
    for(int y=68;y<84;++y)for(int x=567;x<616;++x)
        assert(pixels[(y*4+2)*2560+x*4+2]==fb.pixels[y*640+x]);
    /* Two physical positions within one logical UI column can select different frames. */
    assert(500/4==501/4);
    assert(selection_frame_from_pointer(&instrument,&ui,window,501)>
           selection_frame_from_pointer(&instrument,&ui,window,500));
    int markers=0;
    for(int x=0;x<2400;++x)if(detail.columns[x].has_zero_crossing) {
        size_t boundary=1000+((size_t)x*2048+2399)/2400;
        size_t pointer=selection_frame_from_pointer(&instrument,&ui,window,80+x);
        assert(pointer==boundary);
        ts_instrument_set_selection_snapped(&instrument,1000,pointer);
        assert(instrument.selection_last==boundary);
        ts_instrument_set_playhead_snapped(&instrument,pointer);
        assert(instrument.playhead_frame==boundary);
        ++markers;
    }
    assert(markers>20);
    ts_ui_waveform_detail_begin(&detail);ts_ui_render(&fb,&ui,&instrument);
    assert(ts_ui_waveform_detail_finish(&detail,&fb) && detail.analysis_count==1);
    instrument.view_first++;instrument.view_last++;
    ts_ui_waveform_detail_begin(&detail);ts_ui_render(&fb,&ui,&instrument);
    assert(ts_ui_waveform_detail_finish(&detail,&fb) && detail.analysis_count==2);
    assert(ts_sample_hash(&instrument.current)==hash);
    free(pixels);ts_ui_waveform_detail_free(&detail);SDL_DestroyTexture(wave);SDL_DestroyTexture(base);
    SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);

    static SisterWindow sister;TsConfig config;ts_config_init(&config);config.sister_window_maximized=0;
    ts_sister_ui_model_init(&sister.model,&config);assert(sister_window_ensure(&sister,&config));
    const int sizes[][2]={{1920,1080},{1080,1920},{2560,1600}};
    for(int i=0;i<3;++i) {
        int width=sizes[i][0],height=sizes[i][1];SDL_SetWindowSize(sister.window,width,height);
        int ow,oh;assert(!SDL_GetRendererOutputSize(sister.renderer,&ow,&oh));assert(ow==width && oh==height);
        ts_sister_ui_render(&sister.framebuffer,&sister.model,&ui.palette);
        assert(!SDL_UpdateTexture(sister.texture,NULL,sister.framebuffer.pixels,640*sizeof(uint32_t)));
        assert(!SDL_RenderClear(sister.renderer));assert(!SDL_RenderCopy(sister.renderer,sister.texture,NULL,NULL));
        pixels=malloc((size_t)width*height*sizeof(uint32_t));assert(pixels);
        assert(!SDL_RenderReadPixels(sister.renderer,NULL,SDL_PIXELFORMAT_ARGB8888,pixels,width*sizeof(uint32_t)));
        assert(pixels[0]==sister.framebuffer.pixels[0]);
        assert(pixels[width-1]==sister.framebuffer.pixels[639]);
        assert(pixels[(size_t)(height-1)*width]==sister.framebuffer.pixels[399*640]);
        assert(pixels[(size_t)width*height-1]==sister.framebuffer.pixels[400*640-1]);
        if(!i)native_pixels_shot("sister-filled-window",pixels,width,height);
        free(pixels);
        SDL_Event sent={0},received={0};sent.type=SDL_MOUSEBUTTONDOWN;sent.button.windowID=sister.window_id;
        sent.button.button=SDL_BUTTON_LEFT;sent.button.x=480*width/640;sent.button.y=380*height/400;
        SDL_FlushEvent(SDL_MOUSEBUTTONDOWN);assert(SDL_PushEvent(&sent)==1);
        assert(SDL_PeepEvents(&received,1,SDL_GETEVENT,SDL_MOUSEBUTTONDOWN,SDL_MOUSEBUTTONDOWN)==1);
        assert(received.button.windowID==sister.window_id && received.button.x==sent.button.x && received.button.y==sent.button.y);
        int x,y,wx,wy;assert(sister_event_mouse(sister.window,received.button.x,received.button.y,&x,&y));
        assert(sister_window_mouse(sister.window,sister.renderer,sent.button.x,sent.button.y,&wx,&wy));
        assert(x==wx && y==wy);
        TsSisterUiHit want=ts_sister_ui_hit_test_model(&sister.model,480,380);
        TsSisterUiHit hit=ts_sister_ui_hit_test_model(&sister.model,x,y);assert(hit.action==want.action);
    }
    sister_window_fullscreen(&sister,1);
    assert(SDL_GetWindowFlags(sister.window)&SDL_WINDOW_FULLSCREEN_DESKTOP);
    sister_window_fullscreen(&sister,0);
    assert(!(SDL_GetWindowFlags(sister.window)&SDL_WINDOW_FULLSCREEN_DESKTOP));
    SDL_DestroyTexture(sister.texture);SDL_DestroyRenderer(sister.renderer);SDL_DestroyWindow(sister.window);
    memset(&sister,0,sizeof(sister));ts_sample_free(&sample);ts_instrument_free(&instrument);
    puts("Native waveform detail, precise selection and filled Sister window checks passed");
}
