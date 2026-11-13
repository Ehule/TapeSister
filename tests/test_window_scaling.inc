/* Main editors must occupy the window and grow with it. Exercise the real
   presentation and inverse pointer paths, including the areas left blank by
   the rejected fixed-density toolbar/palette and centered-dialog layouts. */
static void test_window_scaling(void)
{
    TsUiState *saved=malloc(sizeof(ui));assert(saved);*saved=ui;
    SDL_Window *w=SDL_CreateWindow("Scaling test",0,0,1280,800,SDL_WINDOW_HIDDEN);assert(w);
    SDL_Renderer *r=SDL_CreateRenderer(w,-1,SDL_RENDERER_SOFTWARE);assert(r);
    SDL_Texture *texture=SDL_CreateTexture(r,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,640,400);assert(texture);
    TsFramebuffer *fb=calloc(1,sizeof(*fb));assert(fb);
    const uint32_t background=0xff101010,header=0xff25475b,palette=0xff735139,button=0xff42d38a;
    for(int y=0;y<400;++y)for(int x=0;x<640;++x)
        fb->pixels[y*640+x]=y<66?header:(x<113 && y<358)?palette:background;
    TsUiRect action=ts_ui_gallery_action(TS_GALLERY_CLOSE);
    for(int y=action.y;y<action.y+action.h;++y)for(int x=action.x;x<action.x+action.w;++x)fb->pixels[y*640+x]=button;
    assert(!SDL_UpdateTexture(texture,NULL,fb->pixels,640*4));
    const int sizes[][2]={{1280,800},{1920,1080},{2560,1440},{1366,768}};
    for(int mode=0;mode<4;++mode) {
        ts_ui_init(&ui);ui.mosaic_open=mode<2;ui.mosaic_gallery_open=mode==1;
        ui.fm_open=mode==2;ui.tracker_open=mode==3;
        int first_width=0;
        for(int pass=0;pass<4;++pass) {
            int width=sizes[pass][0],height=sizes[pass][1];
            SDL_SetWindowSize(w,width,height);SDL_SetRenderDrawColor(r,0,0,0,255);SDL_RenderClear(r);
            main_surface_copy(r,texture);
            SDL_Surface *shot=SDL_CreateRGBSurfaceWithFormat(0,width,height,32,SDL_PIXELFORMAT_ARGB8888);assert(shot);
            assert(!SDL_RenderReadPixels(r,NULL,shot->format->format,shot->pixels,shot->pitch));
            uint32_t *pixels=shot->pixels;int stride=shot->pitch/4;
            assert(pixels[10*stride+width-2]==header);
            assert(pixels[(height*350/400)*stride+10]==palette);
            int py=(action.y+action.h/2)*height/400,actual=0;
            for(int px=0;px<width;++px)actual+=pixels[py*stride+px]==button;
            assert(abs(actual-action.w*width/640)<=1);
            if(!pass)first_width=actual;
            if(pass==1 || pass==2)assert(actual>first_width);
            int x,y;logical_mouse(w,(action.x+action.w/2)*width/640,py,&x,&y);
            assert(ts_ui_gallery_hit(TS_GALLERY_CLOSE,x,y));
            logical_mouse(w,width-1,height-1,&x,&y);assert(x==639 && y==399);
            logical_mouse(w,width-1,10,&x,&y);assert(x==639 && y>=0 && y<66);
            SDL_FreeSurface(shot);
        }
    }
    free(fb);SDL_DestroyTexture(texture);SDL_DestroyRenderer(r);SDL_DestroyWindow(w);
    ui=*saved;free(saved);
    puts("Full-window scaling, enlarged controls, occupied margins and matching hitboxes passed.");
}
