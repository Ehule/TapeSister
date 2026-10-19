/* UI-thread-only project appearance editor. The render child receives these
   bounded values through the existing mailbox; no audio processing is edited. */
enum { MW_W=680,MW_H=620,MW_HISTORY=32 };
static struct {
    SDL_Window *host; int video_tab; Uint32 video_painted;
    SDL_Window *window;SDL_Renderer *renderer;SDL_Texture *texture;Uint32 id;
    uint32_t pixels[MW_W*MW_H];
    TsMandalaConfig original,last,history[MW_HISTORY];
    int visible,dirty,swatch,drag,hex_focus,hex_replace,cursor,count;
    char hex[7],message[108];
    const char *hint;
} mandala_editor;
static void mw_rect(int x,int y,int width,int height,uint32_t color)
{
    for(int r=SDL_max(0,y);r<SDL_min(MW_H,y+height);++r)
        for(int c=SDL_max(0,x);c<SDL_min(MW_W,x+width);++c)mandala_editor.pixels[r*MW_W+c]=color|0xff000000u;
}
static void mw_text(int x,int y,const char *text,uint32_t color,int scale)
{
    for(;*text;++text,x+=6*scale){const char *g=ts_ui_glyph(*text);
        for(int r=0;r<7;++r)for(int c=0;c<5;++c)if(g[r*5+c]=='1')mw_rect(x+c*scale,y+r*scale,scale,scale,color);}
}
static void mw_button(int x,int y,int width,const char *label,int selected)
{
    mw_rect(x,y,width,25,selected?0x604568:0x39313F);mw_rect(x,y,width,1,selected?0xE6BD7F:0x75617B);
    mw_text(x+8,y+9,label,selected?0xFFE1A1:0xDDD3E2,1);
}
static void mw_hex(const TsMandalaConfig *c)
{snprintf(mandala_editor.hex,sizeof(mandala_editor.hex),"%06X",ts_mandala_color(c,(unsigned)mandala_editor.swatch));}
static void mw_changed(const TsMandalaConfig *c)
{mandala_editor.last=*c;mandala_editor.dirty=1;mandala_editor.message[0]=0;if(!mandala_editor.hex_focus)mw_hex(c);}
static void mw_commit(const TsMandalaConfig *c)
{
    if(!memcmp(&mandala_editor.history[mandala_editor.cursor],c,sizeof(*c)))return;
    mandala_editor.count=mandala_editor.cursor+1;
    if(mandala_editor.count==MW_HISTORY){memmove(mandala_editor.history,mandala_editor.history+1,(MW_HISTORY-1)*sizeof(*c));--mandala_editor.count;}
    mandala_editor.cursor=mandala_editor.count;mandala_editor.history[mandala_editor.count++]=*c;
    mw_changed(c);
}
static void mw_sync(const TsMandalaConfig *c)
{
    /* Loading another project while this modeless window is open replaces its
       revert/undo origin too, so it cannot restore the previous project's look. */
    if(memcmp(c,&mandala_editor.last,sizeof(*c))) {
        mandala_editor.original=mandala_editor.last=mandala_editor.history[0]=*c;
        mandala_editor.cursor=0;mandala_editor.count=1;mandala_editor.drag=0;
        mandala_editor.hex_focus=0;SDL_CaptureMouse(SDL_FALSE);mw_hex(c);mandala_editor.dirty=1;
    }
}
static void mw_history(TsMandalaConfig *c,int direction)
{
    int at=mandala_editor.cursor+direction;
    if(at>=0 && at<mandala_editor.count){mandala_editor.cursor=at;*c=mandala_editor.history[at];mw_changed(c);}
}
static void mandala_editor_close(void)
{
    if(mandala_editor.texture)SDL_DestroyTexture(mandala_editor.texture);
    if(mandala_editor.renderer)SDL_DestroyRenderer(mandala_editor.renderer);
    if(mandala_editor.window)SDL_DestroyWindow(mandala_editor.window);
    memset(&mandala_editor,0,sizeof(mandala_editor));
}
static void mandala_editor_show(TsUiState *ui,const TsMandalaConfig *c)
{
    if(!mandala_editor.window) {
        mandala_editor.window=SDL_CreateWindow("TapeSister - Visualizer Editor",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,MW_W,MW_H,SDL_WINDOW_ALLOW_HIGHDPI);
        if(mandala_editor.window)mandala_editor.renderer=SDL_CreateRenderer(mandala_editor.window,-1,SDL_RENDERER_SOFTWARE);
        if(mandala_editor.renderer)mandala_editor.texture=SDL_CreateTexture(mandala_editor.renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,MW_W,MW_H);
        if(!mandala_editor.texture){mandala_editor_close();snprintf(ui->status,sizeof(ui->status),"VISUALIZER EDITOR: %.120s",SDL_GetError());return;}
        SDL_RenderSetLogicalSize(mandala_editor.renderer,MW_W,MW_H);mandala_editor.id=SDL_GetWindowID(mandala_editor.window);
    }
    if(!mandala_editor.visible){mandala_editor.last=*c;mandala_editor.original=*c;mandala_editor.history[0]=*c;mandala_editor.count=1;mandala_editor.cursor=0;}
    mandala_editor.visible=mandala_editor.dirty=1;mandala_editor.hex_focus=0;mandala_editor.message[0]=0;mw_hex(c);
    SDL_ShowWindow(mandala_editor.window);SDL_RaiseWindow(mandala_editor.window);
}
static void mandala_editor_video_loaded(TsUiState *ui,const TsMandalaConfig *c)
{mw_commit(c);mandala_editor.video_tab=1;mandala_editor_show(ui,c);}
static void mw_hide(TsMandalaConfig *c)
{
    if(mandala_editor.drag)mw_commit(c);
    mandala_editor.visible=mandala_editor.drag=mandala_editor.hex_focus=0;
    SDL_CaptureMouse(SDL_FALSE);SDL_StopTextInput();SDL_HideWindow(mandala_editor.window);
}
static void mw_visual(TsUiState *ui,const TsMandalaConfig *c)
{
    if(ts_mandala_active()){if(ts_mandala_frozen())ts_mandala_freeze();else ts_mandala_close();}
    else {
        TsMandalaPreferences prefs={ui->config.visual_quality,ui->config.visual_display,ui->config.visual_fullscreen};char error[160];
        if(!ts_mandala_open(c,&prefs,error,sizeof(error)))snprintf(mandala_editor.message,sizeof(mandala_editor.message),"%.107s",error);
    }
    mandala_editor.dirty=1;
}
static uint32_t *mw_parameter(TsMandalaConfig *c,int index)
{
    switch(index){case 0:return &c->density;case 1:return &c->scale;case 2:return &c->motion;case 3:return &c->trails;
        case 4:return &c->video_mix;case 5:return &c->video_scale;case 6:return &c->video_rotation;default:return &c->video_response;}
}
static int mw_min(int index){return index==0?10:index==1?50:0;}
static int mw_max(int index){return index==1?150:100;}
static int mw_default(int index){return index<2?100:index==4?75:50;}
static void mw_slider(TsMandalaConfig *c,int drag,int x)
{
    if(drag<=3) {
        unsigned shift=(unsigned)(3-drag)*8;uint32_t rgb=ts_mandala_color(c,(unsigned)mandala_editor.swatch);
        rgb=(rgb&~(255u<<shift))|((uint32_t)SDL_clamp((x-100)*255/270,0,255)<<shift);
        ts_mandala_set_color(c,(unsigned)mandala_editor.swatch,rgb);
    } else {
        int index=drag-4,origin=20+(index%2)*330;
        *mw_parameter(c,index)=(uint32_t)(mw_min(index)+SDL_clamp(x-origin,0,300)*(mw_max(index)-mw_min(index))/300);
    }
    mw_changed(c);
}
static void mw_shape_icon(int x,int y,int shape,uint32_t color)
{
    /* Tiny selectable silhouettes, not a second live renderer. */
    if(shape==0){mw_shape_icon(x-12,y,1,color);mw_shape_icon(x+12,y,2,color);return;}
    int sides=shape==1?3:shape==2?4:shape==3?24:6;
    float rotation=shape==2?-.78539816f:-1.57079633f;
    for(int j=0;j<sides;++j) {
        float a=rotation+j*6.2831853f/sides,b=rotation+(j+1)*6.2831853f/sides;
        float x0=x+cosf(a)*11,y0=y+sinf(a)*11,x1=x+cosf(b)*11,y1=y+sinf(b)*11;
        for(int k=0;k<=24;++k)mw_rect((int)lrintf(x0+(x1-x0)*k/24),(int)lrintf(y0+(y1-y0)*k/24),1,1,color);
    }
}
static void mw_video_paint(const TsMandalaConfig *c)
{
    char line[110];TsVideoStatus status=ts_mandala_video_status();
    mw_button(20,64,200,c->video_path[0]?"REPLACE VIDEO FILE":"ADD VIDEO FILE",0);
    mw_button(234,64,130,c->video_enabled?"VIDEO ON":"VIDEO OFF",c->video_enabled);
    mw_button(378,64,130,"REMOVE VIDEO",0);
    const char *name=c->video_path;
    for(const char *p=name;*p;++p)if(*p=='/' || *p=='\\')name=p+1;
    mw_rect(20,106,640,83,0x110F15);
    snprintf(line,sizeof(line),"%.100s",*name?name:"CHOOSE A CLIP TO FOLD INTO THE MANDALA");mw_text(32,119,line,0xFFE1A1,1);
    mw_text(32,143,!ts_mandala_active()?"OPEN THE VISUALIZER TO PLAY YOUR VIDEO":!c->video_enabled?"VIDEO OFF / MANDALA CONTINUES":ts_mandala_frozen()?"VISUALIZER FROZEN":ts_mandala_video_paused()?"VIDEO PAUSED / MANDALA CONTINUES":status.message,0xC4B6CB,1);
    snprintf(line,sizeof(line),"%02u:%02u / %02u:%02u   |   VIDEO SOUND OFF",status.position_ms/60000,status.position_ms/1000%60,status.duration_ms/60000,status.duration_ms/1000%60);mw_text(32,167,line,0xBCAFBC,1);
    mw_button(20,205,146,ts_mandala_video_paused()?"PLAY VIDEO":"PAUSE VIDEO",ts_mandala_video_paused());
    mw_button(180,205,146,"RESTART",0);
    mw_button(340,205,146,c->video_loop?"LOOP ON":"LOOP OFF",c->video_loop);
    TsMandalaConfig values=*c;
    for(int i=0;i<4;++i) {
        int x=20+(i%2)*330,y=263+(i/2)*70,value=(int)*mw_parameter(&values,i+4);
        snprintf(line,sizeof(line),"%s  %d",(const char*[]){"VIDEO BLEND","VIDEO ZOOM","ROTATION","AUDIO RESPONSE"}[i],value);mw_text(x,y,line,0xD9CFDE,1);
        mw_rect(x,y+19,300,6,0x100D15);mw_rect(x,y+19,value*3,6,0x9776A5);mw_rect(x+value*3-2,y+14,4,16,0xE9C58C);
    }
    mw_text(20,402,"THE CLIP KEEPS ITS OWN RHYTHM. YOUR MUSIC SHAPES THE LENS.",0xE9C58C,1);
    mw_text(20,425,"PAUSE HOLDS A FRAME WHILE THE MANDALA KEEPS MOVING.",0xBCAFBC,1);
    mw_text(20,448,"ROTATION: 50 IS STILL. LOWER / HIGHER REVERSES DIRECTION.",0xBCAFBC,1);
    mw_text(20,471,"SAVE YOUR PROJECT TO RECALL THE FILE AND THIS LOOK.",0xBCAFBC,1);
}
static void mw_paint(const TsMandalaConfig *c)
{
    char line[100];mw_rect(0,0,MW_W,MW_H,0x201B24);mw_rect(0,0,MW_W,42,0x110F15);
    mw_text(20,16,"VISUALIZER",0xE9C58C,2);
    mw_button(230,9,96,"FORM",!mandala_editor.video_tab);mw_button(334,9,96,"VIDEO",mandala_editor.video_tab);
    mw_button(444,9,216,ts_mandala_active()?(ts_mandala_frozen()?"RESUME VISUALIZER":"CLOSE VISUALIZER"):"OPEN VISUALIZER",0);
    if(mandala_editor.video_tab){mw_video_paint(c);goto footer;}
    for(unsigned i=0;i<=TS_MANDALA_CUSTOM;++i)mw_button(20+(i%3)*216,54+(i/3)*30,208,ts_mandala_palette_name(i),c->palette==i);
    mw_text(20,121,"PALETTE / SELECT A COLOR TO EDIT",0xBCAFBC,1);
    for(unsigned i=0;i<TS_MANDALA_COLORS;++i) {
        int x=20+i*80;mw_rect(x,140,72,30,mandala_editor.swatch==(int)i?0xFFE1A1:0x514456);
        mw_rect(x+2,142,68,26,ts_mandala_color(c,i));
        snprintf(line,sizeof(line),i==TS_MANDALA_BACKGROUND?"BACKDROP":"COLOR %u",i+1);mw_text(x+3,176,line,0xC4B6CB,1);
    }
    uint32_t rgb=ts_mandala_color(c,(unsigned)mandala_editor.swatch);
    for(int i=0;i<3;++i) {
        int value=(rgb>>((2-i)*8))&255,y=202+i*29;mw_text(23,y,(const char*[]){"RED","GREEN","BLUE"}[i],0xD9CFDE,1);
        for(int x=0;x<270;++x)mw_rect(100+x,y-2,1,12,(rgb&~(255u<<((2-i)*8)))|((unsigned)(x*255/269)<<((2-i)*8)));
        mw_rect(100+value*270/255-2,y-5,4,18,0xFFE1A1);snprintf(line,sizeof(line),"%3d",value);mw_text(390,y,line,0xE5D7EB,1);
    }
    mw_text(466,204,"HEX COLOR",0xBCAFBC,1);mw_rect(466,221,194,26,mandala_editor.hex_focus?0x594261:0x110F15);
    snprintf(line,sizeof(line),"#%s%s",mandala_editor.hex,mandala_editor.hex_focus?"_":"");mw_text(478,229,line,0xFFE1A1,1);
    mw_text(466,260,"ENTER TO APPLY / ESC CANCEL",0xBCAFBC,1);
    mw_text(20,295,"BASE SHAPES",0xE9C58C,1);
    for(unsigned i=0;i<TS_MANDALA_SHAPES;++i) {
        int x=20+i*130;mw_rect(x,315,120,55,c->shape==i?0x604568:0x302936);mw_rect(x,315,120,1,c->shape==i?0xFFE1A1:0x64516E);
        mw_shape_icon(x+60,333,(int)i,c->shape==i?0xFFE1A1:0xB99BC5);mw_text(x+10,356,ts_mandala_shape_name(i),0xE5D7EB,1);
    }
    TsMandalaConfig values=*c;
    for(int i=0;i<4;++i) {
        int x=20+(i%2)*330,y=393+(i/2)*45,value=(int)*mw_parameter(&values,i);
        snprintf(line,sizeof(line),"%s  %d%s",(const char*[]){"DENSITY","SIZE","MOTION","TRAILS"}[i],value,i<2?"%":"");mw_text(x,y,line,0xD9CFDE,1);
        int pos=(value-mw_min(i))*300/(mw_max(i)-mw_min(i));mw_rect(x,y+19,300,6,0x100D15);mw_rect(x,y+19,pos,6,0x9776A5);mw_rect(x+pos-2,y+14,4,16,0xE9C58C);
    }
footer:
    snprintf(line,sizeof(line),"SYMMETRY: %u   - / +",c->symmetry);mw_button(20,490,210,line,0);
    snprintf(line,sizeof(line),"EVOLUTION: %u:%02u   - / +",c->cycle_seconds/60,c->cycle_seconds%60);mw_button(240,490,250,line,0);
    mw_button(500,490,160,"RESET FORM",0);
    mw_text(20,525,"LIVE CHANGES / SAVE YOUR PROJECT TO KEEP THIS LOOK",0xBCAFBC,1);
    mw_button(20,545,86,"UNDO",0);mw_button(114,545,86,"REDO",0);mw_button(208,545,160,"REVERT OPENING",0);
    mw_button(376,545,150,"NEW SEED",0);mw_button(534,545,126,"DONE",0);
    const char *help=mandala_editor.message[0]?mandala_editor.message:mandala_editor.hint?mandala_editor.hint:
        ts_mandala_frozen()?"RESUME THE VISUALIZER TO SEE YOUR EDITS.":"DRAG / WHEEL TO EDIT. RIGHT-CLICK FORM SLIDERS TO RESET.";
    mw_text(20,590,help,0xD7C5A6,1);
}
static void mandala_editor_update(const TsMandalaConfig *c)
{
    if(!mandala_editor.visible || !window_drawable(mandala_editor.window))return;
    mw_sync(c);
    /* Transport/freeze can also change in the visual window. */
    static int active=-1,frozen=-1;
    if(active!=ts_mandala_active() || frozen!=ts_mandala_frozen()){active=ts_mandala_active();frozen=ts_mandala_frozen();mandala_editor.dirty=1;}
    if(mandala_editor.video_tab && SDL_GetTicks()-mandala_editor.video_painted>=200) {mandala_editor.video_painted=SDL_GetTicks();mandala_editor.dirty=1;}
    if(!mandala_editor.dirty)return;
    mw_paint(c);SDL_UpdateTexture(mandala_editor.texture,NULL,mandala_editor.pixels,MW_W*4);
    SDL_RenderCopy(mandala_editor.renderer,mandala_editor.texture,NULL,NULL);SDL_RenderPresent(mandala_editor.renderer);mandala_editor.dirty=0;
}
static int mw_slider_hit(int x,int y)
{
    if(mandala_editor.video_tab) {
        if(x>=16 && x<=654 && y>=271 && y<=372) {int row=(y-271)/70,col=(x-16)/330;
            if(row<2 && col<2 && (y-271)%70<32 && (x-16)%330<=308)return 8+row*2+col;}
        return 0;
    }
    if(x>=94 && x<=376 && y>=194 && y<=274) {int i=(y-194)/29;if(i<3 && (y-194)%29<22)return i+1;}
    if(x>=16 && x<=654 && y>=401 && y<=477) {int row=(y-401)/45,col=(x-16)/330;
        if(row<2 && col<2 && (y-401)%45<32 && (x-16)%330<=308)return 4+row*2+col;}
    return 0;
}
static int mandala_editor_event(SDL_Event *e,TsUiState *ui,TsMandalaConfig *c)
{
    if(!mandala_editor.window)return 0;
    if(e->type==SDL_RENDER_DEVICE_RESET) {
        if(mandala_editor.texture)SDL_DestroyTexture(mandala_editor.texture);
        mandala_editor.texture=SDL_CreateTexture(mandala_editor.renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,MW_W,MW_H);
        if(!mandala_editor.texture){mandala_editor_close();return 0;}mandala_editor.dirty=1;return 0;
    }
    if(event_window_id(e)!=mandala_editor.id)return 0;
    if(!mandala_editor.visible)return 1;
    mw_sync(c);
    if(e->type==SDL_WINDOWEVENT) {
        if(e->window.event==SDL_WINDOWEVENT_CLOSE){mw_hide(c);return 1;}
        if(e->window.event==SDL_WINDOWEVENT_FOCUS_LOST){if(mandala_editor.drag)mw_commit(c);mandala_editor.drag=0;SDL_CaptureMouse(SDL_FALSE);SDL_StopTextInput();}
        if(e->window.event==SDL_WINDOWEVENT_FOCUS_GAINED && mandala_editor.hex_focus)SDL_StartTextInput();
        mandala_editor.dirty=1;
    }
    if(e->type==SDL_MOUSEBUTTONUP && mandala_editor.drag){mw_commit(c);mandala_editor.drag=0;SDL_CaptureMouse(SDL_FALSE);}
    if(e->type==SDL_MOUSEMOTION) {
        int x=e->motion.x,y=e->motion.y;
        if(mandala_editor.drag)mw_slider(c,mandala_editor.drag,x);
        mandala_editor.hint=mandala_editor.video_tab?NULL:y>=140 && y<185?"SEVEN INK COLORS AND A BACKDROP. EDITING CREATES A CUSTOM PALETTE.":
            y>=315 && y<370?"CHOOSE THE REPEATING FORMS. AUDIO AND PEDALS KEEP SHAPING THEM.":
            y>=490 && y<515?"SYMMETRY / EVOLUTION: LEFT-CLICK +, RIGHT-CLICK -, OR USE THE WHEEL.":NULL;
        mandala_editor.dirty=1;
    }
    if(e->type==SDL_KEYDOWN) {
        SDL_Keycode key=e->key.keysym.sym;
        if(mandala_editor.hex_focus) {
            if(key==SDLK_ESCAPE){mandala_editor.hex_focus=0;mw_hex(c);SDL_StopTextInput();}
            else if(key==SDLK_a && (e->key.keysym.mod&KMOD_CTRL))mandala_editor.hex_replace=1;
            else if(key==SDLK_BACKSPACE){size_t n=strlen(mandala_editor.hex);if(mandala_editor.hex_replace){n=0;mandala_editor.hex_replace=0;}else if(n)--n;mandala_editor.hex[n]=0;}
            else if(key==SDLK_RETURN || key==SDLK_KP_ENTER) {
                if(strlen(mandala_editor.hex)==6){ts_mandala_set_color(c,(unsigned)mandala_editor.swatch,(uint32_t)strtoul(mandala_editor.hex,NULL,16));mandala_editor.hex_focus=0;SDL_StopTextInput();mw_commit(c);}
                else snprintf(mandala_editor.message,sizeof(mandala_editor.message),"ENTER SIX HEX DIGITS, FOR EXAMPLE FF5424.");
            }
        } else if(key==SDLK_ESCAPE)mw_hide(c);
        else if((e->key.keysym.mod&KMOD_CTRL) && (key==SDLK_z || key==SDLK_y))mw_history(c,key==SDLK_y || (e->key.keysym.mod&KMOD_SHIFT)?1:-1);
        mandala_editor.dirty=1;
    }
    if(e->type==SDL_TEXTINPUT && mandala_editor.hex_focus) {
        for(const char *p=e->text.text;*p;++p)if(isxdigit((unsigned char)*p)) {
            if(mandala_editor.hex_replace){mandala_editor.hex[0]=0;mandala_editor.hex_replace=0;}
            size_t n=strlen(mandala_editor.hex);if(n<6){mandala_editor.hex[n]=(char)toupper((unsigned char)*p);mandala_editor.hex[n+1]=0;}
        }
        mandala_editor.dirty=1;
    }
    int wheel=e->type==SDL_MOUSEWHEEL;
    if(e->type==SDL_MOUSEBUTTONDOWN || wheel) {
        int x,y,delta=1;
        if(wheel){wheel_event_mouse(e,&x,&y);delta=e->wheel.y;if(e->wheel.direction==SDL_MOUSEWHEEL_FLIPPED)delta=-delta;if(!delta)return 1;}
        else {x=e->button.x;y=e->button.y;if(e->button.button!=SDL_BUTTON_LEFT && e->button.button!=SDL_BUTTON_RIGHT)return 1;delta=e->button.button==SDL_BUTTON_RIGHT?-1:1;}
        if(!wheel && delta>0 && y>=9 && y<34 && x>=230 && x<430) {
            mandala_editor.video_tab=x>=334;mandala_editor.hex_focus=0;mandala_editor.hint=NULL;SDL_StopTextInput();mandala_editor.dirty=1;return 1;
        }
        if(mandala_editor.video_tab && y>=42 && y<250) {
            if(!wheel && delta>0) {
                if(y>=64 && y<89 && x>=20 && x<220) {
                    browser_open(ui,TS_BROWSER_LOAD_VIDEO);
                    if(mandala_editor.host){SDL_ShowWindow(mandala_editor.host);SDL_RaiseWindow(mandala_editor.host);}
                } else if(y>=64 && y<89 && x>=234 && x<364 && c->video_path[0]) {c->video_enabled=!c->video_enabled;mw_commit(c);}
                else if(y>=64 && y<89 && x>=378 && x<508) {c->video_enabled=0;memset(c->video_path,0,sizeof(c->video_path));mw_commit(c);}
                else if(y>=205 && y<230 && x>=20 && x<166)ts_mandala_video_pause();
                else if(y>=205 && y<230 && x>=180 && x<326)ts_mandala_video_restart();
                else if(y>=205 && y<230 && x>=340 && x<486) {c->video_loop=!c->video_loop;mw_commit(c);}
            }
            mandala_editor.dirty=1;return 1;
        }
        int slider=mw_slider_hit(x,y);
        mandala_editor.hex_focus=0;SDL_StopTextInput();mw_hex(c);
        if(slider) {
            if(wheel) {
                if(slider<=3){unsigned shift=(unsigned)(3-slider)*8;uint32_t rgb=ts_mandala_color(c,(unsigned)mandala_editor.swatch);int value=(rgb>>shift)&255;
                    ts_mandala_set_color(c,(unsigned)mandala_editor.swatch,(rgb&~(255u<<shift))|((uint32_t)SDL_clamp(value+delta,0,255)<<shift));}
                else {int i=slider-4;*mw_parameter(c,i)=(uint32_t)SDL_clamp((int)*mw_parameter(c,i)+delta,mw_min(i),mw_max(i));}mw_commit(c);
            } else if(delta>0){mandala_editor.drag=slider;SDL_CaptureMouse(SDL_TRUE);mw_slider(c,slider,x);}
            else if(slider>=4){*mw_parameter(c,slider-4)=(uint32_t)mw_default(slider-4);mw_commit(c);}
        } else if(y>=490 && y<515 && x>=20 && x<490) {
            if(x<230)c->symmetry=(uint32_t)SDL_clamp((int)c->symmetry+delta,4,12);
            else if(x>=240)c->cycle_seconds=(uint32_t)SDL_clamp((int)c->cycle_seconds+30*delta,30,1800);
            mw_commit(c);
        } else if(!wheel && delta>0) {
            if(mandala_editor.video_tab && y>=42 && y<490) {}
            else if(y>=54 && y<109 && x>=20 && x<660 && (x-20)%216<208 && (y-54)%30<25) {
                c->palette=(uint32_t)(((y-54)/30)*3+(x-20)/216);mw_commit(c);
            } else if(y>=140 && y<185 && x>=20 && x<652 && (x-20)%80<72){mandala_editor.swatch=(x-20)/80;mw_hex(c);}
            else if(y>=221 && y<247 && x>=466 && x<660){mandala_editor.hex_focus=mandala_editor.hex_replace=1;SDL_StartTextInput();}
            else if(y>=315 && y<370 && x>=20 && x<660 && (x-20)%130<120){c->shape=(uint32_t)((x-20)/130);mw_commit(c);}
            else if(y>=490 && y<515 && x>=500 && x<660){ts_mandala_reset_geometry(c);mw_commit(c);}
            else if(y>=9 && y<34 && x>=444 && x<660)mw_visual(ui,c);
            else if(y>=545 && y<570) {
                if(x>=20 && x<106)mw_history(c,-1);else if(x>=114 && x<200)mw_history(c,1);
                else if(x>=208 && x<368){*c=mandala_editor.original;mw_commit(c);}
                else if(x>=376 && x<526){ts_mandala_reseed(c);mw_commit(c);}
                else if(x>=534 && x<660)mw_hide(c);
            }
        }
        mandala_editor.dirty=1;
    }
    return 1; /* Hex letters/shortcuts never become notes or transport commands. */
}
