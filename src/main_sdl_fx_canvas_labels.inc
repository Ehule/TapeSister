/* Free text is presentation metadata. It has no ports or DSP ownership. */
static void fx_label_cancel(void)
{fx_canvas.label_editing=0;SDL_StopTextInput();}
static void fx_label_begin(int index)
{
    TsMatrixLayout *l=matrix_window.layout;
    if(index<0 && l->label_count==TS_FX_LABEL_LIMIT){snprintf(matrix_window.status,sizeof(matrix_window.status),"TEXT LABEL LIMIT REACHED (64).");return;}
    fx_canvas.label_target=index;fx_canvas.label_editing=1;fx_canvas.label_all=1;
    snprintf(fx_canvas.label_text,sizeof(fx_canvas.label_text),"%s",index>=0?l->labels[index].text:"Text label");
    fx_canvas.label_cursor=(int)strlen(fx_canvas.label_text);
    fx_canvas.popup=0;matrix_window.effect_open=matrix_window.compressor_open=0;
    SDL_StartTextInput();matrix_window.dirty=1;
}
static void fx_label_insert(const char *text)
{
    char *s=fx_canvas.label_text;
    if(fx_canvas.label_all){memset(s,0,TS_FX_LABEL_TEXT);fx_canvas.label_cursor=0;fx_canvas.label_all=0;}
    for(;*text;++text) {
        unsigned char c=(unsigned char)*text;size_t n=strlen(s);int at=fx_canvas.label_cursor;
        if((c!='\n' && (c<32 || c>126)) || n+1>=TS_FX_LABEL_TEXT)continue;
        memmove(s+at+1,s+at,n-at+1);s[at]=(char)c;++fx_canvas.label_cursor;
    }
}
static int fx_label_edit_event(SDL_Event *e)
{
    if(e->type==SDL_TEXTINPUT){fx_label_insert(e->text.text);return 1;}
    if(e->type!=SDL_KEYDOWN)return 1;
    SDL_Keycode k=e->key.keysym.sym;int mod=e->key.keysym.mod;char *s=fx_canvas.label_text;
    int n=(int)strlen(s),at=fx_canvas.label_cursor;
    if(k==SDLK_ESCAPE){fx_label_cancel();return 1;}
    if((mod&KMOD_CTRL) && k==SDLK_a){fx_canvas.label_all=1;return 1;}
    if((mod&KMOD_CTRL) && k==SDLK_v){char *text=SDL_GetClipboardText();if(text){fx_label_insert(text);SDL_free(text);}return 1;}
    if(k==SDLK_RETURN || k==SDLK_KP_ENTER) {
        if(mod&KMOD_SHIFT){fx_label_insert("\n");return 1;}
        TsMatrixLayout *l=matrix_window.layout;int i=fx_canvas.label_target;
        if(i<0 && l->label_count<TS_FX_LABEL_LIMIT) {
            i=l->label_count++;l->labels[i]=(TsFxCanvasLabel){.x=fx_canvas.place_x,.y=fx_canvas.place_y,.scale=1};
        }
        if(i>=0 && i<l->label_count) {
            memset(l->labels[i].text,0,TS_FX_LABEL_TEXT);memcpy(l->labels[i].text,s,(size_t)n);
            fx_clear_selection();fx_clear_wires();fx_canvas.label_selected=i;
        }
        fx_label_cancel();return 1;
    }
    if(k==SDLK_BACKSPACE || k==SDLK_DELETE) {
        if(fx_canvas.label_all){memset(s,0,TS_FX_LABEL_TEXT);fx_canvas.label_cursor=0;}
        else if(k==SDLK_BACKSPACE && at){memmove(s+at-1,s+at,(size_t)(n-at+1));--fx_canvas.label_cursor;}
        else if(k==SDLK_DELETE && at<n)memmove(s+at,s+at+1,(size_t)(n-at));
        fx_canvas.label_all=0;return 1;
    }
    if(k==SDLK_LEFT || k==SDLK_RIGHT || k==SDLK_HOME || k==SDLK_END) {
        fx_canvas.label_cursor=k==SDLK_HOME?0:k==SDLK_END?n:SDL_clamp(at+(k==SDLK_LEFT?-1:1),0,n);
        fx_canvas.label_all=0;
    }
    return 1;
}
static SDL_Rect fx_label_rect(const TsFxCanvasLabel *p)
{
    int lines=1,column=0;
    for(const char *s=p->text;*s;++s)if(*s=='\n' || ++column>=25){++lines;column=0;}
    float z=matrix_window.layout->canvas_zoom*p->scale;
    return (SDL_Rect){fx_x(p->x),fx_y(p->y),SDL_max(28,(int)(320*z)),SDL_max(18,(int)((16+24*SDL_min(16,lines))*z))};
}
static void fx_labels_paint(void)
{
    TsMatrixLayout *l=matrix_window.layout;SDL_Rect clip={20,FX_TOP,fx_right()-20,FX_BOTTOM-FX_TOP};
    for(int i=0;i<l->label_count;++i) {
        TsFxCanvasLabel *p=&l->labels[i];SDL_Rect r=fx_label_rect(p),visible;
        if(!SDL_IntersectRect(&r,&clip,&visible))continue;
        fx_rect(r.x,r.y,r.w,r.h,i==fx_canvas.label_selected?FX_FOCUS:FX_EDGE);
        fx_rect(r.x+1,r.y+1,r.w-2,r.h-2,FX_PANEL);
        SDL_Rect text={r.x+5,r.y+4,r.w-10,r.h-8};
        fx_text_box_scaled(text,clip,p->text,FX_TEXT,2,16,fmaxf(.4f,l->canvas_zoom*p->scale));
    }
}
static void fx_label_editor_paint(void)
{
    mx_rect(250,220,780,370,FX_FOCUS);mx_rect(252,222,776,366,FX_PANEL);
    fx_panel_text(270,238,740,28,"TEXT LABEL",FX_FOCUS,2,1);
    mx_rect(270,282,740,230,fx_canvas.label_all?FX_EDGE:FX_BG);
    char text[TS_FX_LABEL_TEXT+2];int at=fx_canvas.label_cursor;
    memcpy(text,fx_canvas.label_text,(size_t)at);text[at]='|';snprintf(text+at+1,sizeof(text)-at-1,"%s",fx_canvas.label_text+at);
    fx_panel_text(280,292,720,210,text,FX_TEXT,2,8);
    fx_panel_text(270,528,740,24,"ENTER: SAVE   SHIFT+ENTER: NEW LINE   ESC: CANCEL",FX_TEXT,1,1);
    fx_panel_text(270,554,740,24,"CTRL+A: SELECT ALL   CTRL+V: PASTE   ARROWS: CURSOR",FX_DIM,1,1);
}
static int fx_label_drag_event(SDL_Event *e,int x,int y)
{
    MatrixWindow *w=&matrix_window;int mode=w->drag;
    if(mode!=FX_DRAG_LABEL && mode!=FX_DRAG_LABEL_GRAB && mode!=FX_DRAG_LABEL_SCALE)return 0;
    int i=fx_canvas.label_selected;
    if(i<0 || i>=w->layout->label_count){w->drag=0;return 1;}
    if(e->type==SDL_MOUSEMOTION) {
        TsFxCanvasLabel *p=&w->layout->labels[i];float z=w->layout->canvas_zoom;
        if(mode==FX_DRAG_LABEL_SCALE)p->scale=SDL_clamp(fx_canvas.label_origin.scale*expf((x-fx_canvas.pointer_x)*.005f),.25f,4.f);
        else {
            p->x=SDL_clamp(fx_canvas.label_origin.x+(x-fx_canvas.pointer_x)/z,-1000000.f,1000000.f);
            p->y=SDL_clamp(fx_canvas.label_origin.y+(y-fx_canvas.pointer_y)/z,-1000000.f,1000000.f);
            if(w->layout->canvas_snap && !(SDL_GetModState()&KMOD_ALT)){p->x=fx_snap(p->x);p->y=fx_snap(p->y);}
        }
        w->dirty=1;
    }
    if((e->type==SDL_KEYDOWN && e->key.keysym.sym==SDLK_ESCAPE) || (e->type==SDL_MOUSEBUTTONDOWN && e->button.button==SDL_BUTTON_RIGHT))fx_cancel_gesture();
    else if((mode==FX_DRAG_LABEL && e->type==SDL_MOUSEBUTTONUP) || (mode!=FX_DRAG_LABEL &&
        ((e->type==SDL_KEYDOWN && e->key.keysym.sym==SDLK_RETURN) || (e->type==SDL_MOUSEBUTTONDOWN && e->button.button==SDL_BUTTON_LEFT)))) {
        w->drag=0;SDL_CaptureMouse(SDL_FALSE);
    }
    return 1;
}
static void fx_label_drag(int mode,int x,int y)
{
    fx_canvas.label_origin=matrix_window.layout->labels[fx_canvas.label_selected];
    fx_canvas.pointer_x=x;fx_canvas.pointer_y=y;matrix_window.drag=mode;SDL_CaptureMouse(SDL_TRUE);
}
