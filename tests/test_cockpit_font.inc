/* Actual renderer pixels, including fractional display scaling. A complete
   native image scaled once is the reference; scaling every tiny stroke is not. */
static SDL_Surface *perf_font_frame(int width,int height)
{
    SDL_SetWindowSize(performance_transport.window,width,height);
    assert(!SDL_RenderSetLogicalSize(performance_transport.renderer,PERF_W,PERF_H));
    perf_rect(0,0,PERF_W,PERF_H,0xff151c20);
    perf_small(24,32,"MOSAIC TRACKSISTER ARP LIVE TILES / PLAYING STOPPED",0xffdbe8e3);
    perf_small(24,60,"ABCDEFGHIJKLMNOPQRSTUVWXYZ 0123456789 .,:;+-/%",0xff94aaa7);
    perf_small(24,88,"GROUP / MUTE / SOLO / 00:00.000 / +0.0 / 125 BPM",0xff8addb4);
    perf_rect(20,116,760,36,0xff8addb4);
    perf_small(24,132,"OVERVIEW / CUES / INPUTS / CLOCK ROUTING",0xff151c20);
    perf_text(24,176,"TAPESISTER / CONTROL CENTER",0xffdbe8e3,2);
    perf_small(PERF_W-4,PERF_H-6,"EDGE",0xff94aaa7); /* Clip, never wrap rows. */
    perf_present();
    int w,h;assert(!SDL_GetRendererOutputSize(performance_transport.renderer,&w,&h));
    assert(w==width && h==height);
    SDL_Surface *s=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_ARGB8888);assert(s);
    assert(!SDL_RenderReadPixels(performance_transport.renderer,NULL,s->format->format,s->pixels,s->pitch));
    return s;
}
static void perf_font_save(SDL_Surface *s,const char *directory,int percent)
{
    if(!directory)return;
    char path[1024];snprintf(path,sizeof(path),"%s/font-grid-%d.bmp",directory,percent);
    assert(!SDL_SaveBMP(s,path));
}
static void test_cockpit_font(const char *directory)
{
    SDL_Surface *native=perf_font_frame(PERF_W,PERF_H);perf_font_save(native,directory,100);
    /* The visible small E has a solid two-pixel top bar and left stem. This
       guards the thin/broken strokes without prescribing the whole alphabet. */
    int ex=24+4*9,ey=60;
    for(int y=0;y<12;++y)for(int x=0;x<(y<2?8:2);++x)
        assert(((const Uint32*)((const Uint8*)native->pixels+(ey+y)*native->pitch))[ex+x]==0xff94aaa7);
    const int scales[]={75,125,150};
    for(unsigned i=0;i<sizeof(scales)/sizeof(*scales);++i) {
        int percent=scales[i];
        int w=PERF_W*percent/100,h=PERF_H*percent/100;
        SDL_Surface *actual=perf_font_frame(w,h);perf_font_save(actual,directory,percent);
        SDL_Surface *expected=SDL_CreateRGBSurfaceWithFormat(0,w,h,32,SDL_PIXELFORMAT_ARGB8888);assert(expected);
        SDL_Renderer *r=SDL_CreateSoftwareRenderer(expected);assert(r);
        SDL_Texture *t=SDL_CreateTextureFromSurface(r,native);assert(t);
        assert(!SDL_SetTextureBlendMode(t,SDL_BLENDMODE_NONE));
        assert(!SDL_SetTextureScaleMode(t,SDL_ScaleModeNearest));
        assert(!SDL_RenderCopy(r,t,NULL,NULL));SDL_RenderPresent(r);
        unsigned different=0;
        for(int y=0;y<h;++y) {
            const Uint32 *a=(const Uint32*)((const Uint8*)actual->pixels+y*actual->pitch);
            const Uint32 *e=(const Uint32*)((const Uint8*)expected->pixels+y*expected->pitch);
            for(int x=0;x<w;++x)different+=a[x]!=e[x];
        }
        if(different)fprintf(stderr,"F11 %d%% text grid differs from a single bitmap scale at %u pixels\n",percent,different);
        assert(!different);
        SDL_DestroyTexture(t);SDL_DestroyRenderer(r);SDL_FreeSurface(expected);SDL_FreeSurface(actual);
    }
    SDL_FreeSurface(native);SDL_SetWindowSize(performance_transport.window,PERF_W,PERF_H);
    assert(!SDL_RenderSetLogicalSize(performance_transport.renderer,PERF_W,PERF_H));
}
