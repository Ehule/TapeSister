/* Names and colors are canvas presentation metadata, keyed by stable identity. */
static uint32_t fx_swatch(int i)
{
    static const uint32_t colors[]={0xfff27078,0xffff9855,0xffffc76b,0xffe6e285,0xffb7da77,0xff7cce91,
        0xff6ed7bd,0xff72cddb,0xff7aafe7,0xff939be9,0xffbb92df,0xffdd90c4,
        0xffe8d7bf,0xffc9cdd2,0xff929ba6,0xff606874};
    return colors[SDL_clamp(i,0,15)];
}
static void fx_properties_close(void){fx_canvas.properties=0;SDL_StopTextInput();}
static void fx_properties_begin(MatrixRow row)
{
    TsFxCanvasPosition *p=fx_position(row,0);fx_canvas.properties_row=row;
    fx_canvas.properties=1;fx_canvas.properties_all=1;fx_canvas.text_blink=SDL_GetTicks();
    snprintf(fx_canvas.properties_name,sizeof(fx_canvas.properties_name),"%s",p?p->name:"");
    fx_canvas.properties_cursor=(int)strlen(fx_canvas.properties_name);fx_canvas.properties_color=p?p->color:0;
    fx_canvas.popup=0;matrix_window.drag=0;matrix_window.dirty=1;SDL_StartTextInput();
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
static int fx_properties_event(SDL_Event *e)
{
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
    int width,height;SDL_GetWindowSize(matrix_window.window,&width,&height);
    int x=px*MATRIX_W/SDL_max(1,width),y=py*MATRIX_H/SDL_max(1,height);
    int click=e->type==SDL_MOUSEBUTTONDOWN && e->button.button==SDL_BUTTON_LEFT;
    int drag=e->type==SDL_MOUSEMOTION && (e->motion.state&SDL_BUTTON_LMASK);
    if(click && x>=324 && x<948 && y>=308 && y<344) {
        FxFontMetrics font=fx_font(2);
        fx_canvas.properties_cursor=SDL_clamp((px-mx_px(334)+font.advance/2)/font.advance,0,(int)strlen(fx_canvas.properties_name));
        fx_canvas.properties_all=0;fx_canvas.text_blink=SDL_GetTicks();return 1;
    }
    if(click && y>=578 && y<606) {
        if(x>=324 && x<494){fx_properties_save();return 1;}
        if(x>=516 && x<686){fx_properties_close();return 1;}
        if(x>=750 && x<940){fx_canvas.properties_name[0]=0;fx_canvas.properties_cursor=0;fx_canvas.properties_color=0;return 1;}
    }
    if(click && x>=324 && x<948 && y>=370 && y<404)fx_canvas.properties_color=fx_swatch((x-324)/39);
    if((click || drag) && x>=410 && x<=920 && y>=430 && y<538) {
        int component=(y-430)/36,shift=(2-component)*8,value=SDL_clamp((x-410)*255/510,0,255);
        uint32_t color=fx_canvas.properties_color?fx_canvas.properties_color:fx_swatch(6);
        fx_canvas.properties_color=(color&~(255u<<shift))|((uint32_t)value<<shift);matrix_window.dirty=1;
    }
    return 1;
}
static void fx_properties_paint(void)
{
    mx_rect(300,220,680,410,FX_FOCUS);mx_rect(302,222,676,406,FX_PANEL);
    fx_panel_text(324,240,620,28,"CARD PROPERTIES",FX_FOCUS,2,1);
    fx_panel_text(324,278,620,22,"NAME  /  EMPTY USES ORIGINAL",FX_DIM,1,1);
    mx_rect(324,308,624,36,fx_canvas.properties_all?FX_EDGE:FX_BG);
    fx_edit_text((SDL_Rect){334,316,602,24},fx_canvas.properties_name,fx_canvas.properties_cursor);
    for(int i=0;i<16;++i){mx_rect(324+i*39,370,37,34,fx_swatch(i));}
    uint32_t c=fx_canvas.properties_color?fx_canvas.properties_color:fx_swatch(6);
    for(int i=0;i<3;++i) {
        char label[24];int v=(c>>((2-i)*8))&255;snprintf(label,sizeof(label),"%c %03d","RGB"[i],v);
        fx_panel_text(324,430+i*36,80,22,label,FX_TEXT,1,1);
        mx_rect(410,440+i*36,510,4,FX_EDGE);mx_rect(410+v*2,432+i*36,3,20,c);
    }
    fx_panel_text(324,545,620,22,fx_canvas.properties_color?"CUSTOM COLOR":"AUTOMATIC TYPE COLOR",c,1,1);
    fx_button(324,578,170,"APPLY",0);fx_button(516,578,170,"CANCEL",0);fx_button(750,578,190,"RESET",0);
}
