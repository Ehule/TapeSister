/* Exercise the same two-surface compositor as a real enlarged SDL window. */
static void mosaic_native_check(const TsInstrument *doc,const char *shot)
{
    SDL_Window *w=SDL_CreateWindow("Mosaic native detail",0,0,1920,1200,SDL_WINDOW_HIDDEN);assert(w);
    SDL_Renderer *r=SDL_CreateRenderer(w,-1,SDL_RENDERER_SOFTWARE);assert(r);
    SDL_Texture *base=SDL_CreateTexture(r,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,640,400);assert(base);
    TsUiWaveformDetail details[2]={0};SDL_Texture *textures[2]={0};static TsFramebuffer fb;
    for(int pass=0;pass<2;++pass) {
        int width=pass?1729:1920,height=pass?973:1200;SDL_SetWindowSize(w,width,height);
        assert(main_waveform_detail_allowed(&ui));
        native_waveform_prepare(r,details,1);ts_ui_render(&fb,&ui,doc);
        assert(details[0].valid && details[1].valid);
        assert(!SDL_UpdateTexture(base,NULL,fb.pixels,640*sizeof(uint32_t)));
        assert(!SDL_RenderCopy(r,base,NULL,NULL));native_waveform_copy(r,textures,details,&fb);
        uint32_t *pixels=malloc((size_t)width*height*sizeof(*pixels));assert(pixels);
        assert(!SDL_RenderReadPixels(r,NULL,SDL_PIXELFORMAT_ARGB8888,pixels,width*sizeof(uint32_t)));
        if(!pass) {
            int independent[2]={0};
            for(int i=0;i<2;++i) {
                TsUiWaveformDetail *d=&details[i];
                for(int y=0;y<d->height;++y)for(int x=0;x+1<d->width;x+=3)
                    independent[i]+=d->pixels[(size_t)y*d->width+x]!=d->pixels[(size_t)y*d->width+x+1];
            }
            assert(independent[0]>100 && independent[1]>20);
            int py=66+(int)((ui.mosaic_time-ui.mosaic_scroll)*ui.mosaic_scale);
            if(py>=66 && py<376)for(int x=147;x<631;++x)
                assert(pixels[(py*3+1)*width+x*3+1]==fb.pixels[py*640+x]);
            for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
                int lx=x/3,ly=y/3;
                if(ly<66 || ly>=376 || (lx>=103 && lx<151) || lx>=631)
                    assert(pixels[(size_t)y*width+x]==fb.pixels[ly*640+lx]);
            }
            if(shot) {
                FILE *f=fopen(shot,"wb");assert(f);fprintf(f,"P6\n%d %d\n255\n",width,height);
                for(size_t i=0;i<(size_t)width*height;++i) {
                    unsigned char rgb[3]={(unsigned char)(pixels[i]>>16),(unsigned char)(pixels[i]>>8),(unsigned char)pixels[i]};
                    assert(fwrite(rgb,1,3,f)==3);
                }
                assert(!fclose(f));
            }
        }
        free(pixels);
        /* Recording border and modal ink are never covered by native signals. */
        ui.mosaic_edit_choice=1;assert(!main_waveform_detail_allowed(&ui));
        native_waveform_prepare(r,details,main_waveform_detail_allowed(&ui));ts_audio_ui_render(&fb,&ui,doc);
        assert(!details[0].valid && !details[1].valid);ui.mosaic_edit_choice=0;
    }
    for(int i=0;i<2;++i){ts_ui_waveform_detail_free(&details[i]);SDL_DestroyTexture(textures[i]);}
    SDL_DestroyTexture(base);SDL_DestroyRenderer(r);SDL_DestroyWindow(w);
}
