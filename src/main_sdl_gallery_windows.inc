/* Native shells reuse the Gallery editors and their live sequence transactions.
   The saved project contains no window geometry or transient ownership. */
typedef struct {
    SDL_Window *window;SDL_Renderer *renderer;SDL_Texture *texture;TsFramebuffer *frame;
    Uint32 id,painted;int visible,top;
} GalleryWindow;
static GalleryWindow gallery_windows[2];
static Uint32 gallery_return_id;
static SDL_Rect gallery_window_content(int sequence)
{return sequence?(SDL_Rect){8,8,624,384}:(SDL_Rect){114,72,512,322};}
static void gallery_windows_close(void)
{
    for(int i=0;i<2;++i){GalleryWindow *w=&gallery_windows[i];
        if(w->texture)SDL_DestroyTexture(w->texture);if(w->renderer)SDL_DestroyRenderer(w->renderer);
        if(w->window)SDL_DestroyWindow(w->window);free(w->frame);memset(w,0,sizeof(*w));}
}
static int gallery_window_create(int sequence)
{
    GalleryWindow *w=&gallery_windows[sequence];if(w->window)return 1;
    SDL_Rect content=gallery_window_content(sequence);
    w->window=SDL_CreateWindow(sequence?"TapeSister - Gallery Sequencer":"TapeSister - Gallery",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,
        content.w*2,(content.h+32)*2,TS_WORKSPACE_WINDOW_FLAGS);
    if(w->window){w->renderer=SDL_CreateRenderer(w->window,-1,SDL_RENDERER_ACCELERATED);
        if(!w->renderer)w->renderer=SDL_CreateRenderer(w->window,-1,SDL_RENDERER_SOFTWARE);}
    if(w->renderer)w->texture=SDL_CreateTexture(w->renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,TS_UI_WIDTH,TS_UI_HEIGHT);
    w->frame=calloc(1,sizeof(*w->frame));
    if(!w->window || !w->renderer || !w->texture || !w->frame){gallery_windows_close();return 0;}
    SDL_SetWindowMinimumSize(w->window,content.w,content.h+32);w->id=SDL_GetWindowID(w->window);return 1;
}
static void gallery_windows_sync(TsUiState *ui)
{
    int wanted=ui->mosaic_open && ui->mosaic_gallery_open && ui->browser.mode==TS_BROWSER_CLOSED;
    int sequence=ui->gallery_sequence_open!=0;
    if(wanted && !ui->mosaic_gallery_detached){SDL_Window *origin=SDL_GetKeyboardFocus();if(origin)gallery_return_id=SDL_GetWindowID(origin);}
    if(wanted && !gallery_window_create(sequence)){ui->mosaic_gallery_detached=0;return;}
    ui->mosaic_gallery_detached=ui->mosaic_gallery_open!=0;
    for(int i=0;i<2;++i){GalleryWindow *w=&gallery_windows[i];int visible=wanted && i==sequence;
        if(visible && !w->visible){SDL_ShowWindow(w->window);SDL_RaiseWindow(w->window);w->painted=SDL_GetTicks()-40;}
        if(!visible && w->visible){SDL_HideWindow(w->window);
            if(!wanted)editor_return_window(gallery_return_id);}
        w->visible=visible;
    }
}
static void gallery_window_raise(TsUiState *ui)
{
    gallery_windows_sync(ui);GalleryWindow *w=&gallery_windows[ui->gallery_sequence_open!=0];
    if(w->visible){SDL_ShowWindow(w->window);SDL_RaiseWindow(w->window);}
}
static void gallery_window_text(GalleryWindow *w,int x,int y,const char *text,uint32_t ink)
{
    SDL_SetRenderDrawColor(w->renderer,(ink>>16)&255,(ink>>8)&255,ink&255,255);
    for(;*text;++text,x+=6){const char *g=ts_ui_glyph(*text);
        for(int r=0;r<7;++r)for(int c=0;c<5;++c)if(g[r*5+c]=='1')SDL_RenderDrawPoint(w->renderer,x+c,y+r);}
}
static void gallery_windows_paint(TsUiState *ui)
{
    gallery_windows_sync(ui);
    for(int i=0;i<2;++i){GalleryWindow *w=&gallery_windows[i];
        if(!w->visible || !window_drawable(w->window) || SDL_GetTicks()-w->painted<33)continue;
        SDL_Rect content=gallery_window_content(i),dest={0,32,content.w,content.h};
        ts_ui_render_gallery(w->frame,ui);
        SDL_UpdateTexture(w->texture,NULL,w->frame->pixels,TS_UI_WIDTH*sizeof(uint32_t));
        int width,height;SDL_GetRendererOutputSize(w->renderer,&width,&height);
        SDL_RenderSetScale(w->renderer,width/(float)content.w,height/(float)(content.h+32));
        SDL_SetRenderDrawColor(w->renderer,40,32,48,255);SDL_RenderClear(w->renderer);
        const char *labels[]={w->top?"ON TOP: ON":"ON TOP: OFF",(SDL_GetWindowFlags(w->window)&SDL_WINDOW_MAXIMIZED)?"RESTORE":"MAXIMIZE","FULLSCREEN"};
        for(int b=0;b<3;++b){SDL_Rect button={8+b*112,5,104,22};SDL_SetRenderDrawColor(w->renderer,68,54,77,255);SDL_RenderFillRect(w->renderer,&button);gallery_window_text(w,button.x+8,13,labels[b],0xffe5d9e9);}
        gallery_window_text(w,352,13,"ALT+ENTER: FULLSCREEN",0xffc4adc9);
        SDL_RenderCopy(w->renderer,w->texture,&content,&dest);SDL_RenderPresent(w->renderer);w->painted=SDL_GetTicks();
    }
}
static int gallery_windows_event(const SDL_Event *event,SDL_AudioDeviceID device,AudioState *a,TsUiState *ui,TsInstrument *bank,MosaicController *c)
{
    gallery_windows_sync(ui);int index=-1;
    for(int i=0;i<2;++i)if(gallery_windows[i].id && event_window_id(event)==gallery_windows[i].id)index=i;
    if(index<0)return 0;
    GalleryWindow *w=&gallery_windows[index];if(!w->visible)return 1;
    if(event->type==SDL_WINDOWEVENT){
        if(event->window.event==SDL_WINDOWEVENT_CLOSE){
            if(index){ui->gallery_sequence_open=ui->gallery_sequence_edit=0;}
            else {ui->mosaic_gallery_open=ui->mosaic_gallery_rename=0;}
            SDL_StopTextInput();gallery_windows_sync(ui);
        }
        return 1;
    }
    SDL_Rect content=gallery_window_content(index);int width,height,x=0,y=0;
    SDL_GetWindowSize(w->window,&width,&height);
    if(event->type==SDL_MOUSEMOTION){x=event->motion.x;y=event->motion.y;}
    else if(event->type==SDL_MOUSEBUTTONDOWN || event->type==SDL_MOUSEBUTTONUP){x=event->button.x;y=event->button.y;}
    else if(event->type==SDL_MOUSEWHEEL){
#if SDL_VERSION_ATLEAST(2,26,0)
        x=event->wheel.mouseX;y=event->wheel.mouseY;
#else
        SDL_GetMouseState(&x,&y);
#endif
    }
    x=x*content.w/SDL_max(1,width);y=y*(content.h+32)/SDL_max(1,height);
    int pointer=event->type==SDL_MOUSEBUTTONDOWN || event->type==SDL_MOUSEBUTTONUP || event->type==SDL_MOUSEMOTION || event->type==SDL_MOUSEWHEEL;
    if(pointer && y<32){
        if(event->type==SDL_MOUSEBUTTONDOWN && event->button.button==SDL_BUTTON_LEFT && y>=5 && y<27){
            if(x>=8 && x<112){w->top=!w->top;SDL_SetWindowAlwaysOnTop(w->window,w->top?SDL_TRUE:SDL_FALSE);}
            if(x>=120 && x<224){if(SDL_GetWindowFlags(w->window)&SDL_WINDOW_MAXIMIZED)SDL_RestoreWindow(w->window);else SDL_MaximizeWindow(w->window);}
            if(x>=232 && x<336)ts_window_fullscreen(w->window,!(SDL_GetWindowFlags(w->window)&SDL_WINDOW_FULLSCREEN));
        }return 1;
    }
    SDL_Keymod mod=event->type==SDL_KEYDOWN || event->type==SDL_KEYUP?event->key.keysym.mod:SDL_GetModState();
    int detached=ui->mosaic_gallery_detached;ui->mosaic_gallery_detached=0;
    mosaic_gallery_event(event,device,a,ui,bank,c,x+content.x,y-32+content.y,mod);
    ui->mosaic_gallery_detached=detached;gallery_windows_sync(ui);return 1;
}
