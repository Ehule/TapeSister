/* Names and colors are canvas presentation metadata, keyed by stable identity. */
enum { FX_PROPERTIES_W=560, FX_PROPERTIES_H=326 };
static struct {
    SDL_Window *window;SDL_Renderer *renderer;SDL_Texture *texture;Uint32 id;
    uint32_t pixels[FX_PROPERTIES_W*FX_PROPERTIES_H];int dirty,phase;
} fx_properties_window;
static uint32_t fx_swatch(int i)
{
    static const uint32_t colors[]={0xfff27078,0xffff9855,0xffffc76b,0xffe6e285,0xffb7da77,0xff7cce91,
        0xff6ed7bd,0xff72cddb,0xff7aafe7,0xff939be9,0xffbb92df,0xffdd90c4,
        0xffe8d7bf,0xffc9cdd2,0xff929ba6,0xff606874};
    return colors[SDL_clamp(i,0,15)];
}

static void fx_properties_close(void)
{
    if(fx_canvas.properties)SDL_StopTextInput();
    fx_canvas.properties=0;
    if(fx_properties_window.window)SDL_HideWindow(fx_properties_window.window);
}
static void fx_properties_destroy(void)
{
    fx_properties_close();
    if(fx_properties_window.texture)SDL_DestroyTexture(fx_properties_window.texture);
    if(fx_properties_window.renderer)SDL_DestroyRenderer(fx_properties_window.renderer);
    if(fx_properties_window.window)SDL_DestroyWindow(fx_properties_window.window);
    memset(&fx_properties_window,0,sizeof(fx_properties_window));
}
static void fx_properties_place(MatrixRow row)
{
    SDL_Rect bounds;int display=SDL_GetWindowDisplayIndex(matrix_window.window);
    if(display<0 || SDL_GetDisplayUsableBounds(display,&bounds))return;
    int x,y,width,height;SDL_GetWindowPosition(matrix_window.window,&x,&y);
    SDL_GetWindowSize(matrix_window.window,&width,&height);
    int card_x=width/2;
    for(int i=0;i<fx_canvas.count;++i)if(mx_same(fx_canvas.node[i].row,row)) {
        card_x=fx_x(fx_canvas.node[i].x)*width/MATRIX_W;break;
    }
    /* Start on the opposite side of the card, within this monitor's work area.
       Subsequent openings retain the user's dragged position. */
    x+=card_x<width/2?width-FX_PROPERTIES_W-36:36;y+=80;
    x=SDL_clamp(x,bounds.x,bounds.x+SDL_max(0,bounds.w-FX_PROPERTIES_W-16));
    y=SDL_clamp(y,bounds.y+32,bounds.y+SDL_max(32,bounds.h-FX_PROPERTIES_H-16));
    SDL_SetWindowPosition(fx_properties_window.window,x,y);
}
static void fx_properties_begin(MatrixRow row)
{
    if(!fx_properties_window.window) {
        fx_properties_window.window=SDL_CreateWindow("TapeSister - Card Name / Color",
            SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,FX_PROPERTIES_W,FX_PROPERTIES_H,
            SDL_WINDOW_HIDDEN|SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI|SDL_WINDOW_ALWAYS_ON_TOP);
        if(fx_properties_window.window) {
            SDL_SetWindowMinimumSize(fx_properties_window.window,FX_PROPERTIES_W,FX_PROPERTIES_H);
            fx_properties_window.renderer=SDL_CreateRenderer(fx_properties_window.window,-1,SDL_RENDERER_ACCELERATED);
            if(!fx_properties_window.renderer)fx_properties_window.renderer=SDL_CreateRenderer(fx_properties_window.window,-1,SDL_RENDERER_SOFTWARE);
        }
        if(fx_properties_window.renderer)fx_properties_window.texture=SDL_CreateTexture(fx_properties_window.renderer,
            SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,FX_PROPERTIES_W,FX_PROPERTIES_H);
        if(!fx_properties_window.texture) {
            snprintf(matrix_window.status,sizeof(matrix_window.status),"CARD EDITOR FAILED: %.120s",SDL_GetError());
            fx_properties_destroy();return;
        }
        SDL_SetTextureScaleMode(fx_properties_window.texture,SDL_ScaleModeNearest);
        fx_properties_window.id=SDL_GetWindowID(fx_properties_window.window);
        fx_properties_place(row);
    }
    TsFxCanvasPosition *p=fx_position(row,0);fx_canvas.properties_row=row;
    fx_canvas.properties=1;fx_canvas.properties_all=1;fx_canvas.text_blink=SDL_GetTicks();
    snprintf(fx_canvas.properties_name,sizeof(fx_canvas.properties_name),"%s",p?p->name:"");
    fx_canvas.properties_cursor=(int)strlen(fx_canvas.properties_name);fx_canvas.properties_color=p?p->color:0;
    fx_canvas.popup=0;matrix_window.drag=0;matrix_window.dirty=1;fx_properties_window.dirty=1;
    SDL_ShowWindow(fx_properties_window.window);
    if(SDL_GetWindowFlags(fx_properties_window.window)&SDL_WINDOW_MINIMIZED)SDL_RestoreWindow(fx_properties_window.window);
    SDL_RaiseWindow(fx_properties_window.window);SDL_StartTextInput();
}
static void fx_properties_save(void)
{
    TsFxCanvasPosition *p=fx_position(fx_canvas.properties_row,1);
    if(!p){snprintf(matrix_window.status,sizeof(matrix_window.status),"CANVAS POSITION STORAGE FULL.");return;}
    snprintf(p->name,sizeof(p->name),"%s",fx_canvas.properties_name);p->color=fx_canvas.properties_color;
    fx_properties_close();matrix_window.dirty=1;
}
static void fx_properties_insert(const char *text)
{
    char *s=fx_canvas.properties_name;
    if(fx_canvas.properties_all){memset(s,0,sizeof(fx_canvas.properties_name));fx_canvas.properties_cursor=0;fx_canvas.properties_all=0;}
    for(;*text;++text) {
        size_t n=strlen(s);int at=fx_canvas.properties_cursor;
        if((unsigned char)*text<32 || (unsigned char)*text>126 || n+1>=sizeof(fx_canvas.properties_name))continue;
        memmove(s+at+1,s+at,n-at+1);s[at]=*text;++fx_canvas.properties_cursor;
    }
}

static int fx_properties_owns(const SDL_Event *e)
{return fx_properties_window.id && event_window_id(e)==fx_properties_window.id;}
static int fx_properties_apply_event(const SDL_Event *e)
{
    if(e->type==SDL_KEYDOWN)return !ts_window_fullscreen_shortcut(e) &&
        (e->key.keysym.sym==SDLK_RETURN || e->key.keysym.sym==SDLK_KP_ENTER);
    if(e->type!=SDL_MOUSEBUTTONDOWN || e->button.button!=SDL_BUTTON_LEFT)return 0;
    int width,height;SDL_GetWindowSize(fx_properties_window.window,&width,&height);
    int x=e->button.x*FX_PROPERTIES_W/SDL_max(1,width),y=e->button.y*FX_PROPERTIES_H/SDL_max(1,height);
    return x>=16 && x<156 && y>=280 && y<310;
}
static int fx_properties_event(SDL_Event *e)
{
    if(!fx_properties_owns(e))return 0;
    if(!fx_canvas.properties)return 1; /* Consume releases queued before Apply/Cancel hid the window. */
    fx_properties_window.dirty=1;
    if(e->type==SDL_WINDOWEVENT) {
        if(e->window.event==SDL_WINDOWEVENT_CLOSE)fx_properties_close();
        if(e->window.event==SDL_WINDOWEVENT_FOCUS_GAINED)SDL_StartTextInput();
        return 1;
    }
    /* The global handler normally consumes this first; never Apply on Alt+Enter. */
    if(ts_window_fullscreen_shortcut(e))return 1;
    if(e->type==SDL_TEXTINPUT){fx_properties_insert(e->text.text);fx_canvas.text_blink=SDL_GetTicks();return 1;}
    if(e->type==SDL_KEYDOWN) {
        SDL_Keycode k=e->key.keysym.sym;int mod=e->key.keysym.mod;
        char *s=fx_canvas.properties_name;int n=(int)strlen(s),at=fx_canvas.properties_cursor;fx_canvas.text_blink=SDL_GetTicks();
        if(k==SDLK_ESCAPE){fx_properties_close();return 1;}
        if(k==SDLK_RETURN || k==SDLK_KP_ENTER){fx_properties_save();return 1;}
        if((mod&KMOD_CTRL) && k==SDLK_a){fx_canvas.properties_all=1;return 1;}
        if((mod&KMOD_CTRL) && k==SDLK_v){char *t=SDL_GetClipboardText();if(t){fx_properties_insert(t);SDL_free(t);}return 1;}
        if(k==SDLK_BACKSPACE || k==SDLK_DELETE) {
            if(fx_canvas.properties_all){memset(s,0,sizeof(fx_canvas.properties_name));fx_canvas.properties_cursor=0;}
            else if(k==SDLK_BACKSPACE && at){memmove(s+at-1,s+at,(size_t)(n-at+1));--fx_canvas.properties_cursor;}
            else if(k==SDLK_DELETE && at<n)memmove(s+at,s+at+1,(size_t)(n-at));
            fx_canvas.properties_all=0;
        }
        if(k==SDLK_LEFT || k==SDLK_RIGHT || k==SDLK_HOME || k==SDLK_END) {
            fx_canvas.properties_cursor=k==SDLK_HOME?0:k==SDLK_END?n:SDL_clamp(at+(k==SDLK_LEFT?-1:1),0,n);fx_canvas.properties_all=0;
        }
        return 1;
    }

    if(e->type!=SDL_MOUSEBUTTONDOWN && e->type!=SDL_MOUSEMOTION && e->type!=SDL_MOUSEBUTTONUP)return 1;
    int px=e->type==SDL_MOUSEMOTION?e->motion.x:e->button.x,py=e->type==SDL_MOUSEMOTION?e->motion.y:e->button.y;
    int width,height;SDL_GetWindowSize(fx_properties_window.window,&width,&height);
    int x=px*FX_PROPERTIES_W/SDL_max(1,width),y=py*FX_PROPERTIES_H/SDL_max(1,height);
    int click=e->type==SDL_MOUSEBUTTONDOWN && e->button.button==SDL_BUTTON_LEFT;
    int drag=e->type==SDL_MOUSEMOTION && (e->motion.state&SDL_BUTTON_LMASK);
    if(click && x>=16 && x<544 && y>=46 && y<78) {
        int first=SDL_max(0,fx_canvas.properties_cursor-42);
        fx_canvas.properties_cursor=SDL_clamp(first+(x-24+6)/12,0,(int)strlen(fx_canvas.properties_name));
        fx_canvas.properties_all=0;fx_canvas.text_blink=SDL_GetTicks();return 1;
    }
    if(click && y>=280 && y<310) {
        if(x>=16 && x<156){fx_properties_save();return 1;}
        if(x>=166 && x<306){fx_properties_close();return 1;}
        if(x>=404 && x<544){fx_canvas.properties_name[0]=0;fx_canvas.properties_cursor=0;fx_canvas.properties_color=0;return 1;}
    }
    if(click && x>=16 && x<544 && y>=98 && y<126)fx_canvas.properties_color=fx_swatch((x-16)/33);
    if((click || drag) && x>=96 && x<=528 && y>=146 && y<242) {
        int component=(y-146)/32,shift=(2-component)*8,value=SDL_clamp((x-96)*255/432,0,255);
        uint32_t color=fx_canvas.properties_color?fx_canvas.properties_color:fx_swatch(6);
        fx_canvas.properties_color=(color&~(255u<<shift))|((uint32_t)value<<shift);
    }
    return 1;
}
static void fx_properties_rect(int x,int y,int w,int h,uint32_t color)
{
    int left=SDL_max(0,x),right=SDL_min(FX_PROPERTIES_W,x+w);
    for(int row=SDL_max(0,y);row<SDL_min(FX_PROPERTIES_H,y+h);++row)
        for(int col=left;col<right;++col)fx_properties_window.pixels[row*FX_PROPERTIES_W+col]=color;
}
static void fx_properties_text(int x,int y,const char *text,uint32_t color,int scale,int limit)
{
    for(int i=0;text[i] && i<limit;++i,x+=6*scale) {
        const char *glyph=ts_ui_glyph(text[i]);
        for(int r=0;r<7;++r)for(int c=0;c<5;++c)if(glyph[r*5+c]=='1')
            fx_properties_rect(x+c*scale,y+r*scale,scale,scale,color);
    }
}
static void fx_properties_button(int x,int width,const char *name)
{
    fx_properties_rect(x,280,width,30,FX_EDGE);
    fx_properties_text(x+12,288,name,FX_TEXT,2,12);
}
static void fx_properties_paint(void)
{
    if(!fx_canvas.properties || !window_drawable(fx_properties_window.window))return;
    int phase=(SDL_GetTicks()-fx_canvas.text_blink)/500%2;
    if(!fx_properties_window.dirty && phase==fx_properties_window.phase)return;
    fx_properties_window.phase=phase;fx_properties_window.dirty=0;
    fx_properties_rect(0,0,FX_PROPERTIES_W,FX_PROPERTIES_H,FX_PANEL);
    fx_properties_text(16,18,"NAME / EMPTY USES ORIGINAL",FX_DIM,2,44);
    fx_properties_rect(16,46,528,32,fx_canvas.properties_all?FX_EDGE:FX_BG);
    int first=SDL_max(0,fx_canvas.properties_cursor-42);
    fx_properties_text(24,55,fx_canvas.properties_name+first,FX_TEXT,2,43);
    if(!phase)fx_properties_rect(24+(fx_canvas.properties_cursor-first)*12,52,2,21,FX_FOCUS);
    for(int i=0;i<16;++i)fx_properties_rect(16+i*33,98,31,28,fx_swatch(i));
    uint32_t color=fx_canvas.properties_color?fx_canvas.properties_color:fx_swatch(6);
    for(int i=0;i<3;++i) {
        char label[16];int v=(color>>((2-i)*8))&255;snprintf(label,sizeof(label),"%c %03d","RGB"[i],v);
        fx_properties_text(16,152+i*32,label,FX_TEXT,2,6);
        fx_properties_rect(96,158+i*32,432,4,FX_EDGE);
        fx_properties_rect(96+v*432/255,150+i*32,3,20,color);
    }
    fx_properties_text(16,250,fx_canvas.properties_color?"CUSTOM COLOR":"AUTOMATIC TYPE COLOR",color,2,44);
    fx_properties_button(16,140,"APPLY");fx_properties_button(166,140,"CANCEL");fx_properties_button(404,140,"RESET");
    SDL_UpdateTexture(fx_properties_window.texture,NULL,fx_properties_window.pixels,FX_PROPERTIES_W*sizeof(uint32_t));
    SDL_RenderCopy(fx_properties_window.renderer,fx_properties_window.texture,NULL,NULL);
    SDL_RenderPresent(fx_properties_window.renderer);
}
