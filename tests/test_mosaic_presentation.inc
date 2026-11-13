static void test_mosaic_presentation(void)
{
    TsUiState *saved=malloc(sizeof(ui));assert(saved);*saved=ui;
    ts_ui_init(&ui);ui.mosaic_open=1;ui.mosaic_scale=24;ui.mosaic_hscale=1;
    ui.mosaic=ts_mosaic_create();assert(ui.mosaic);
    char error[160];TsMosaicSource *source=ts_mosaic_source(ui.mosaic,&instrument.current,error,sizeof(error));assert(source);
    TsMosaicEvent *card=ts_mosaic_add(ui.mosaic,source,1,20);assert(card);card->duration=5;
    ui.mosaic_selected=card->id;ui.mosaic->markers[0].stored=1;ui.mosaic->markers[0].time=3;
    SDL_Window *w=SDL_CreateWindow("Layout test",0,0,1280,800,SDL_WINDOW_HIDDEN);assert(w);
    SDL_Renderer *r=SDL_CreateRenderer(w,-1,SDL_RENDERER_SOFTWARE);assert(r);
    SDL_Texture *base=SDL_CreateTexture(r,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,640,400);assert(base);
    main_presentation_window=w;main_presentation_renderer=r;main_presentation_ui=&ui;
    main_presentation=main_workspace_presentation=(TsUiPresentation){0};
    TsUiWaveformDetail details[2]={0};SDL_Texture *textures[2]={0};static TsFramebuffer fb;
    const int sizes[][2]={{1280,800},{1920,1080},{1366,768},{1920,1080}};
    for(int pass=0;pass<4;++pass) {
        SDL_SetWindowSize(w,sizes[pass][0],sizes[pass][1]);main_presentation_prepare();
        TsUiRect canvas=main_presentation.target[4];
        assert(main_presentation.mode==1 && canvas.w>0 && canvas.h>0);
        assert(fabs(ui.mosaic_scale*canvas.h/292.0-48)<.00001);
        assert(fabs(ui.mosaic_hscale*canvas.w/(main_presentation.right-151)-2)<.00001);
        for(int i=0;i<main_presentation.count;++i) {
            TsUiRect a=main_presentation.source[i],b=main_presentation.target[i];int x,y;
            logical_mouse(w,b.x+b.w/2,b.y+b.h/2,&x,&y);assert(ts_ui_rect_contains(a,x,y));
        }
        native_waveform_prepare(r,details,1);ts_ui_waveform_presentation(&main_presentation);
        ts_ui_render(&fb,&ui,&instrument);assert(details[0].valid && details[1].valid);
        assert(details[0].output_x==canvas.x && details[0].output_y==canvas.y && details[0].width==canvas.w && details[0].height==canvas.h);
        assert(details[1].height==details[0].height && details[1].output_y==details[0].output_y);
        assert(!SDL_UpdateTexture(base,NULL,fb.pixels,640*4));main_presentation_copy(r,base,&ui);native_waveform_copy(r,textures,details,&fb);
        if(pass==1) {
            assert(canvas.w==1468 && canvas.h==864);int x,y;logical_mouse(w,1800,20,&x,&y);assert(x==-1 && y==-1);
            const char *dir=getenv("TS_MOSAIC_LAYOUT_DIR");if(dir) {
                SDL_Surface *shot=SDL_CreateRGBSurfaceWithFormat(0,1920,1080,32,SDL_PIXELFORMAT_ARGB8888);assert(shot);
                assert(!SDL_RenderReadPixels(r,NULL,shot->format->format,shot->pixels,shot->pitch));
                char path[1024];snprintf(path,sizeof(path),"%s/mosaic-responsive.bmp",dir);assert(!SDL_SaveBMP(shot,path));SDL_FreeSurface(shot);
            }
        }
    }
    /* Gallery uses the same centered mapping for drawing and pointer input. */
    ui.mosaic_gallery_open=1;main_presentation_prepare();assert(main_presentation.mode==2);
    TsUiRect action=ts_ui_gallery_action(TS_GALLERY_CLOSE),target=ts_ui_presentation_rect(&main_presentation,action);
    int x,y;logical_mouse(w,target.x+target.w/2,target.y+target.h/2,&x,&y);assert(ts_ui_gallery_hit(TS_GALLERY_CLOSE,x,y));
    assert(target.h==32);
    ui.mosaic_open=0;main_presentation_prepare();assert(main_presentation.mode==2);
    ui.mosaic_open=1;ui.mosaic_gallery_open=0;ui.mosaic_live_open=1;main_presentation_prepare();
    TsUiLooperCircle circle={0};assert(native_looper_prepare(r,&circle,&ui));assert(circle.valid);
    int ox,oy;ts_ui_mosaic_live_origin(&ui,&ox,&oy);
    logical_mouse(w,circle.x+circle.width/2,circle.y+circle.height/2,&x,&y);
    assert(abs(x-(ox+83))<=1 && abs(y-(oy+86))<=1);
    assert(ui.mosaic_live_output_width==1280 && ui.mosaic_live_output_height==800);
    double sx,sy;ts_ui_mosaic_live_circle_scale(&ui,&sx,&sy);assert(sx==1 && sy==1);
    ts_ui_mosaic_looper_circle_free(&circle);ui.mosaic_live_open=0;main_presentation_prepare();
    assert(fabs(ui.mosaic_scale*main_presentation.target[4].h/292.0-48)<.00001);
    for(int i=0;i<2;++i){ts_ui_waveform_detail_free(&details[i]);if(textures[i])SDL_DestroyTexture(textures[i]);}
    SDL_DestroyTexture(base);SDL_DestroyRenderer(r);SDL_DestroyWindow(w);
    main_presentation_window=NULL;main_presentation_renderer=NULL;main_presentation_ui=NULL;
    main_presentation=main_workspace_presentation=(TsUiPresentation){0};ts_ui_waveform_presentation(NULL);
    ts_mosaic_free(ui.mosaic);ui=*saved;free(saved);
    puts("Mosaic density, resize invariance, native clipping, ruler alignment and Gallery hitboxes passed.");
}
