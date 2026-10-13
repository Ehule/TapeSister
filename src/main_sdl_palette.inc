/* Modeless, UI-thread-only palette editor. No DSP, audio locks or project edits. */
enum { PALETTE_W=720, PALETTE_H=648, PALETTE_ROWS=8, PALETTE_HISTORY=32, PALETTE_LEGACY=100, PALETTE_ROLE_CAPACITY=TS_THEME_ROLE_COUNT+TS_PALETTE_COLOR_COUNT };
typedef struct {
    SDL_Window *window; SDL_Renderer *renderer; SDL_Texture *texture;
    uint32_t pixels[PALETTE_W*PALETTE_H];
    Uint32 id; int visible, dirty, page, group, role, scroll, detail, follow, top, drag, hex_focus;
    int compare, history_count, history_cursor, overwrite_shared;
    TsPalette original, edited, history[PALETTE_HISTORY];
    TsBrowser browser;
    char hex[8], message[128];
} PaletteWindow;
static PaletteWindow palette_window;
static void pw_rect(int x,int y,int width,int height,uint32_t color)
{
    for(int row=SDL_max(0,y);row<SDL_min(PALETTE_H,y+height);++row)
        for(int col=SDL_max(0,x);col<SDL_min(PALETTE_W,x+width);++col)
            palette_window.pixels[row*PALETTE_W+col]=color;
}
static void pw_text(int x,int y,const char *s,uint32_t color,int scale)
{
    for(;*s;++s,x+=6*scale){const char *glyph=ts_ui_glyph(*s);for(int row=0;row<7;++row)for(int col=0;col<5;++col)
        if(glyph[row*5+col]=='1')pw_rect(x+col*scale,y+row*scale,scale,scale,color);}
}
/* Editor chrome is deliberately stable so an unreadable experiment can be undone. */
static void pw_button(int x,int y,int width,const char *label,int selected)
{
    pw_rect(x,y,width,24,selected?0xff35253eu:0xff4a3c4au);pw_rect(x,y,width,1,0xff87748bu);
    pw_rect(x,y,1,24,0xff87748bu);pw_text(x+7,y+8,label,selected?0xffa9e6ffu:0xffeee7edu,1);
}
static int pw_roles(int page,int detail,int *roles)
{
    if(page==3){for(int i=0;i<TS_PALETTE_COLOR_COUNT;++i)roles[i]=PALETTE_LEGACY+i;return TS_PALETTE_COLOR_COUNT;}
    static const int basic[]={TS_COLOR_BACKGROUND,TS_COLOR_PANEL,TS_COLOR_CONTROL,TS_COLOR_TEXT,TS_COLOR_SELECTION,TS_COLOR_SELECTEDTEXT,TS_COLOR_FOCUS,TS_COLOR_ACTIVITY};
    static const int musical[TS_THEME_GROUP_COUNT][8]={
      [TS_THEME_CANVAS]={TS_COLOR_DATA1,TS_COLOR_DATA2,TS_COLOR_DATA4,TS_COLOR_DATA3,TS_COLOR_DATA5,TS_COLOR_PLAYHEAD,TS_COLOR_FOCUS,TS_COLOR_SELECTION},
      [TS_THEME_SISTER]={TS_COLOR_DATA1,TS_COLOR_DATA2,TS_COLOR_DATA3,TS_COLOR_DATA4,TS_COLOR_DATA5,TS_COLOR_ACTIVITY,TS_COLOR_CONTROL,TS_COLOR_TEXT},
      [TS_THEME_PEDALBOARD]={TS_COLOR_DATA1,TS_COLOR_DATA2,TS_COLOR_DATA5,TS_COLOR_DATA6,TS_COLOR_DATA3,TS_COLOR_CONTROL,TS_COLOR_FOCUS,TS_COLOR_TEXT},
      [TS_THEME_FALLOUT]={TS_COLOR_DATA1,TS_COLOR_DATA2,TS_COLOR_DATA3,TS_COLOR_DATA4,TS_COLOR_DATA5,TS_COLOR_CONTROL,TS_COLOR_FOCUS,TS_COLOR_TEXT},
      [TS_THEME_PRISM]={TS_COLOR_DATA1,TS_COLOR_DATA2,TS_COLOR_DATA3,TS_COLOR_DATA4,TS_COLOR_DATA5,TS_COLOR_DATA6,TS_COLOR_GRID,TS_COLOR_FOCUS},
      [TS_THEME_MOSAIC]={TS_COLOR_DATA2,TS_COLOR_DATA1,TS_COLOR_DATA5,TS_COLOR_DATA4,TS_COLOR_DATA3,TS_COLOR_FOCUS,TS_COLOR_PLAYHEAD,TS_COLOR_MUTED},
      [TS_THEME_TRACKER]={TS_COLOR_DATA1,TS_COLOR_DATA4,TS_COLOR_DATA5,TS_COLOR_DATA3,TS_COLOR_DATA2,TS_COLOR_DATA6,TS_COLOR_PLAYHEAD,TS_COLOR_FOCUS},
      [TS_THEME_ARP]={TS_COLOR_DATA1,TS_COLOR_DATA2,TS_COLOR_PLAYHEAD,TS_COLOR_SELECTION,TS_COLOR_SELECTEDTEXT,TS_COLOR_PANEL,TS_COLOR_TEXT,TS_COLOR_CONTROL},
      [TS_THEME_ROUTER]={TS_COLOR_DATA2,TS_COLOR_ACTIVITY,TS_COLOR_WARNING,TS_COLOR_SELECTION,TS_COLOR_SELECTEDTEXT,TS_COLOR_GRID,TS_COLOR_MUTED,TS_COLOR_SOLO},
      [TS_THEME_EQ]={TS_COLOR_DATA1,TS_COLOR_DATA2,TS_COLOR_ACTIVITY,TS_COLOR_GRID,TS_COLOR_SOLO,TS_COLOR_MUTED,TS_COLOR_TEXT,TS_COLOR_CONTROL},
      [TS_THEME_SPATIAL]={TS_COLOR_DATA1,TS_COLOR_DATA2,TS_COLOR_DATA4,TS_COLOR_GRID,TS_COLOR_FOCUS,TS_COLOR_CONTROL,TS_COLOR_PANEL,TS_COLOR_TEXT},
      [TS_THEME_PORTAL]={TS_COLOR_DATA1,TS_COLOR_DATA2,TS_COLOR_DATA3,TS_COLOR_DATA4,TS_COLOR_DATA5,TS_COLOR_FOCUS,TS_COLOR_PANEL,TS_COLOR_TEXT},
      [TS_THEME_BROWSER]={TS_COLOR_BACKGROUND,TS_COLOR_PANEL,TS_COLOR_TEXT,TS_COLOR_SELECTION,TS_COLOR_SELECTEDTEXT,TS_COLOR_FOCUS,TS_COLOR_DATA4,TS_COLOR_DATA2},
      [TS_THEME_HEALTH]={TS_COLOR_ACTIVITY,TS_COLOR_WARNING,TS_COLOR_ERROR,TS_COLOR_TEXT,TS_COLOR_DIM,TS_COLOR_FOCUS,TS_COLOR_PANEL,TS_COLOR_EDGE},
      [TS_THEME_MIDI]={TS_COLOR_DATA2,TS_COLOR_DATA3,TS_COLOR_FOCUS,TS_COLOR_TEXT,TS_COLOR_PANEL,TS_COLOR_CONTROL,TS_COLOR_DIM,TS_COLOR_EDGE},
      [TS_THEME_TRANSPORT]={TS_COLOR_DATA4,TS_COLOR_RECORD,TS_COLOR_TEXT,TS_COLOR_DIM,TS_COLOR_BACKGROUND,TS_COLOR_CONTROL,TS_COLOR_SELECTION,TS_COLOR_FOCUS}
    };
    if(detail){for(int i=0;i<TS_THEME_ROLE_COUNT;++i)roles[i]=i;return TS_THEME_ROLE_COUNT;}
    memcpy(roles,page==2?musical[palette_window.group]:basic,sizeof(basic));return 8;
}
static int pw_group(void){return palette_window.page==0?TS_THEME_OVERALL:palette_window.group;}
static uint32_t pw_color(const TsPalette *p)
{
    int g=pw_group(),r=palette_window.role;return r>=PALETTE_LEGACY?p->colors[r-PALETTE_LEGACY]:ts_theme_preview(p,g,r);
}
static void pw_set_color(TsPalette *p,uint32_t c)
{
    int r=palette_window.role;
    if(r>=PALETTE_LEGACY){p->colors[r-PALETTE_LEGACY]=c|0xff000000u;p->defined_colors|=1u<<(r-PALETTE_LEGACY);}
    else ts_theme_override(p,pw_group(),r,c);
}
static void pw_hex(const TsPalette *p)
{snprintf(palette_window.hex,sizeof(palette_window.hex),"%06X",(unsigned)(pw_color(p)&0xffffffu));}
static void pw_edit(TsUiState *ui)
{
    if(palette_window.compare){ui->palette=palette_window.edited;palette_window.compare=0;}
    palette_window.overwrite_shared=0;
}
static void pw_commit(TsUiState *ui)
{
    PaletteWindow *w=&palette_window;
    if(w->history_count && !memcmp(&w->history[w->history_cursor],&ui->palette,sizeof(ui->palette)))return;
    w->history_count=w->history_cursor+1;
    if(w->history_count==PALETTE_HISTORY){memmove(w->history,w->history+1,(PALETTE_HISTORY-1)*sizeof(TsPalette));--w->history_count;}
    w->history_cursor=w->history_count;w->history[w->history_count++]=ui->palette;
    pw_hex(&ui->palette);w->dirty=1;
}
static void pw_history(TsUiState *ui,int direction)
{
    PaletteWindow *w=&palette_window;pw_edit(ui);int at=w->history_cursor+direction;
    if(at>=0&&at<w->history_count){w->history_cursor=at;ui->palette=w->history[at];}
    pw_hex(&ui->palette);w->dirty=1;
}
static void pw_set_channel(TsUiState *ui,int channel,int value)
{
    pw_edit(ui);uint32_t c=pw_color(&ui->palette);int shift=(2-channel)*8;
    c=(c&~(255u<<shift))|((uint32_t)SDL_clamp(value,0,255)<<shift);
    pw_set_color(&ui->palette,c);pw_hex(&ui->palette);palette_window.dirty=1;
}
static void palette_window_hide(TsUiState *ui,int cancel)
{
    PaletteWindow *w=&palette_window;pw_edit(ui);
    if(cancel)ui->palette=w->original;
    w->visible=0;w->drag=0;w->hex_focus=0;ts_browser_close(&w->browser);
    SDL_CaptureMouse(SDL_FALSE);SDL_StopTextInput();if(w->window)SDL_HideWindow(w->window);
}
static void palette_window_close(void)
{
    PaletteWindow *w=&palette_window;
    if(w->texture)SDL_DestroyTexture(w->texture);
    if(w->renderer)SDL_DestroyRenderer(w->renderer);
    if(w->window)SDL_DestroyWindow(w->window);
    memset(w,0,sizeof(*w));
}
static void palette_window_show(TsUiState *ui)
{
    PaletteWindow *w=&palette_window;
    if(!w->window) {
        w->window=SDL_CreateWindow("TapeSister - Palette",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,PALETTE_W,PALETTE_H,SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI);
        if(w->window)w->renderer=SDL_CreateRenderer(w->window,-1,SDL_RENDERER_SOFTWARE);
        if(w->renderer)w->texture=SDL_CreateTexture(w->renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,PALETTE_W,PALETTE_H);
        if(!w->texture){palette_window_close();snprintf(ui->status,sizeof(ui->status),"PALETTE WINDOW: %.120s",SDL_GetError());return;}
        SDL_RenderSetLogicalSize(w->renderer,PALETTE_W,PALETTE_H);SDL_SetWindowMinimumSize(w->window,PALETTE_W,PALETTE_H);
        w->id=SDL_GetWindowID(w->window);w->follow=1;w->group=TS_THEME_CANVAS;w->role=TS_COLOR_BACKGROUND;
        ts_browser_init(&w->browser);
        char path[TS_SHARED_PALETTE_PATH_MAX],directory[TS_BROWSER_PATH_MAX];
        if(shared_palette_path(ui,path,sizeof(path))&&runtime_path_parent(path,directory,sizeof(directory))){
            snprintf(w->browser.directory,sizeof(w->browser.directory),"%s",directory);
            if(!ts_browser_refresh(&w->browser))ts_browser_init(&w->browser);
        }
    }
    if(!w->visible){w->original=ui->palette;w->history[0]=ui->palette;w->history_count=1;w->history_cursor=0;w->compare=0;w->message[0]=0;}
    ui->config_open=0;ui->palette_open=0;w->visible=w->dirty=1;pw_hex(&ui->palette);
    SDL_StopTextInput();SDL_ShowWindow(w->window);SDL_RaiseWindow(w->window);
}
static void pw_file_open(int saving)
{
    PaletteWindow *w=&palette_window;w->hex_focus=0;
    ts_browser_open(&w->browser,saving?TS_BROWSER_TRACKER_PALETTE_EXPORT:TS_BROWSER_TRACKER_PALETTE_IMPORT,"My Palette.pal");
    if(saving)SDL_StartTextInput();else SDL_StopTextInput();w->dirty=1;
}
static void pw_file_action(TsUiState *ui)
{
    PaletteWindow *w=&palette_window;TsBrowser *b=&w->browser;char path[TS_BROWSER_PATH_MAX];
    int saving=b->mode==TS_BROWSER_TRACKER_PALETTE_EXPORT;
    if(!saving&&b->selected>=0&&b->selected<b->entry_count&&b->entries[b->selected].is_directory){ts_browser_enter_selected_directory(b);w->dirty=1;return;}
    if(!(saving?ts_browser_destination_path(b,path,sizeof(path)):ts_browser_selected_path(b,path,sizeof(path))))return;
    if(saving&&ts_browser_path_exists(path)&&!b->overwrite_armed){b->overwrite_armed=1;snprintf(b->message,sizeof(b->message),"FILE EXISTS - SAVE AGAIN TO REPLACE");w->dirty=1;return;}
    TsPalette loaded=ui->palette;int ok;
    if(saving)ok=ts_palette_save(w->compare?&w->edited:&ui->palette,path,b->message,sizeof(b->message));
    else ok=ts_palette_load(&loaded,path,b->message,sizeof(b->message));
    if(ok){if(!saving){pw_edit(ui);ui->palette=loaded;pw_commit(ui);}snprintf(w->message,sizeof(w->message),"%s %.95s",saving?"SAVED":"LOADED",path_basename(path));ts_browser_close(b);SDL_StopTextInput();}
    w->dirty=1;
}
static void pw_paint(const TsUiState *ui)
{
    PaletteWindow *w=&palette_window;const TsPalette *p=&ui->palette;char label[128];
    pw_rect(0,0,PALETTE_W,PALETTE_H,0xff242126u);pw_rect(0,0,PALETTE_W,37,0xff141216u);
    pw_text(16,13,"PALETTE",0xff91d2d0u,2);
    pw_button(350,7,164,w->follow?"FOLLOW VIEW: ON":"PINNED COMPONENT",w->follow);
    pw_button(524,7,178,w->top?"ALWAYS ON TOP: ON":"ALWAYS ON TOP: OFF",w->top);
    if(w->browser.mode!=TS_BROWSER_CLOSED) {
        TsBrowser *b=&w->browser;int saving=b->mode==TS_BROWSER_TRACKER_PALETTE_EXPORT;
        pw_text(16,53,saving?"SAVE AS / EXPORT PALETTE":"LOAD A SAVED PALETTE",0xffe6be7fu,1);
        size_t n=strlen(b->directory);pw_text(16,80,b->directory+(n>108?n-108:0),0xffe2dae5u,1);
        pw_button(16,103,100,"UP FOLDER",0);pw_button(126,103,130,"NEW FOLDER",0);
        for(int i=0;i<16;++i){int at=b->scroll+i,y=141+i*25;if(at>=b->entry_count)break;
            pw_rect(16,y,686,23,at==b->selected?0xff483651u:0xff19171cu);
            snprintf(label,sizeof(label),b->entries[at].is_directory?"[DIR] %.104s":"      %.104s",b->entries[at].name);
            pw_text(22,y+8,label,at==b->selected?0xffb8e6f4u:0xffdfd6e3u,1);}
        if(saving||b->creating_directory){pw_text(16,552,b->creating_directory?"FOLDER":"NAME",0xffb8abbfu,1);pw_rect(76,543,626,26,0xff161319u);
            snprintf(label,sizeof(label),"%.100s",b->filename);pw_text(84,553,label,0xffe9dfeau,1);pw_rect(84+SDL_min(100,b->filename_cursor)*6,550,1,13,0xffe6be7fu);}
        pw_button(16,580,186,b->creating_directory?"CREATE FOLDER":saving?(b->overwrite_armed?"CONFIRM REPLACE":"SAVE PALETTE"):"LOAD PALETTE",0);
        pw_button(214,580,120,"CANCEL",0);pw_text(16,620,b->message,0xffe9c58cu,1);return;
    }
    for(int i=0;i<8;++i){int x=16+(i%4)*174,y=47+(i/4)*29;TsPalette preset;ts_palette_preset(&preset,i);
        pw_button(x,y,164,ts_theme_preset_name(i),p->theme.preset==i);
        for(int c=0;c<5;++c)pw_rect(x+129+c*6,y+8,5,9,ts_theme_preview(&preset,TS_THEME_OVERALL,TS_COLOR_DATA1+c));}
    pw_button(16,113,218,"OVERALL LOOK",w->page==0);pw_button(246,113,218,"READABILITY",w->page==1);pw_button(476,113,226,"COMPONENTS",w->page==2);
    if(w->page==1) {
        const char *names[]={"TEXT CLARITY","GRID VISIBILITY","SELECTION STRENGTH","DESKTOP RELIEF","CONTROL RELIEF"};
        const int values[]={p->theme.text_clarity,p->theme.grid_visibility,p->theme.selection_strength,p->desktop_contrast,p->buttons_contrast};
        for(int i=0;i<5;++i){int y=167+i*62;snprintf(label,sizeof(label),"%s  %d",names[i],values[i]);pw_text(24,y,label,0xffdfd7e4u,1);
            pw_rect(24,y+22,664,7,0xff141218u);pw_rect(24,y+22,values[i]*664/100,7,0xff9bc3c0u);pw_rect(24+values[i]*660/100,y+16,4,19,0xffe6be7fu);}
        pw_text(24,494,"50 = ORIGINAL CLARITY / GRID / SELECTION",0xffbdb0c5u,1);
        pw_text(24,511,"STATE LABELS AND SHAPES REMAIN VISIBLE WITH EVERY PRESET.",0xffbdb0c5u,1);
    } else {
        int roles[PALETTE_ROLE_CAPACITY],count=pw_roles(w->page,w->detail,roles);
        for(int g=1;g<TS_THEME_GROUP_COUNT;++g){int y=157+(g-1)*21;int chosen=w->page==2&&g==w->group;
            pw_rect(16,y,174,20,chosen?0xff483651u:0xff1a171eu);pw_text(22,y+7,ts_theme_group_name(g),chosen?0xffbce6f3u:0xffbfb2c7u,1);
            if(ts_theme_group_custom(p,g))pw_rect(181,y+7,4,4,0xffe3b775u);}
        pw_text(204,151,w->page==3?"SHARED .PAL COLORS":ts_theme_group_name(pw_group()),0xffdfb783u,1);
        if(w->page!=3)pw_button(555,143,147,w->detail?"DETAIL: ON":"DETAIL: OFF",w->detail);
        for(int i=0;i<PALETTE_ROWS;++i){int at=w->scroll+i,y=176+i*25;if(at>=count)break;int role=roles[at];uint32_t c=role>=PALETTE_LEGACY?p->colors[role-PALETTE_LEGACY]:ts_theme_preview(p,pw_group(),role);
            pw_rect(204,y,498,23,w->role==role?0xff483651u:0xff19161du);pw_rect(210,y+4,28,15,c);
            pw_text(248,y+8,role>=PALETTE_LEGACY?ts_palette_color_name((TsPaletteColor)(role-PALETTE_LEGACY)):ts_theme_role_name(pw_group(),role),0xffe8dfeau,1);
            snprintf(label,sizeof(label),"#%06X %s",(unsigned)(c&0xffffffu),role>=PALETTE_LEGACY?"SHARED":p->theme.overrides[pw_group()][role]?"CUSTOM":"LINKED");pw_text(581,y+8,label,0xffbcb0c6u,1);}
        snprintf(label,sizeof(label),"%d-%d / %d  WHEEL TO SCROLL",w->scroll+1,SDL_min(count,w->scroll+PALETTE_ROWS),count);pw_text(204,384,label,0xffb8a9c1u,1);
        uint32_t c=pw_color(p);pw_rect(204,409,80,77,c);pw_text(304,407,"HEX",0xffb8a9c1u,1);pw_button(340,399,130,w->hex,w->hex_focus);
        if(w->hex_focus)pw_text(480,408,"ENTER TO APPLY",0xffd4bd86u,1);
        for(int channel=0;channel<3;++channel){int y=439+channel*23,value=(c>>((2-channel)*8))&255;
            pw_text(304,y,(const char*[]){"R","G","B"}[channel],0xffded4e6u,1);pw_rect(326,y+1,318,6,0xff141218u);
            pw_rect(326,y+1,value*318/255,6,(const uint32_t[]){0xffd7959du,0xffa9c99au,0xff96b9d7u}[channel]);pw_rect(326+value*314/255,y-3,4,14,0xffe6be7fu);
            snprintf(label,sizeof(label),"%03d",value);pw_text(666,y,label,0xffded4e6u,1);}
        pw_button(204,512,236,w->page==3?"RESET SHARED COLOR":"USE OVERALL / RESET COLOR",0);if(w->page!=3)pw_button(450,512,252,"RESET THIS COMPONENT",0);
    }
    pw_rect(16,545,686,1,0xff65526du);
    pw_button(16,555,68,"UNDO",0);pw_button(91,555,68,"REDO",0);
    pw_button(169,555,142,w->compare?"SHOW EDITED":"SHOW BEFORE",w->compare);
    pw_button(321,555,133,"REVERT SESSION",0);pw_button(464,555,110,"DONE",0);pw_button(584,555,118,"SHARED COLORS",w->page==3);
    pw_button(16,588,148,"LOAD / IMPORT",0);pw_button(174,588,150,"SAVE AS / EXPORT",0);
    pw_button(334,588,176,w->overwrite_shared?"CONFIRM SHARED SAVE":"SAVE SHARED",0);
    pw_text(16,628,w->message[0]?w->message:"LIVE CHANGES / DONE KEEPS THEM / SAVE SHARED RECALLS AT STARTUP",0xffc4b8cbu,1);
}
static int palette_active_group(SDL_Window *main,const TsUiState *ui,const SisterWindow *sister,Uint32 id)
{
    if(id==sister->window_id)return sister->model.fx_page==1?TS_THEME_PEDALBOARD:sister->model.fx_page==2?TS_THEME_FALLOUT:sister->model.fx_page==3?TS_THEME_PRISM:TS_THEME_SISTER;
    if(id==matrix_window.id)return matrix_window.compressor_open?TS_THEME_EQ:TS_THEME_ROUTER;
    if(id==source_route_window.id)return TS_THEME_ROUTER;
    if(id==ts_audio_health_window_id())return TS_THEME_HEALTH;
    if(id==spatial_window.id)return TS_THEME_SPATIAL;
    if(id==performance_transport.id)return TS_THEME_TRANSPORT;
    if(id!=SDL_GetWindowID(main))return -1;
    if(ui->midi_learn_active)return TS_THEME_MIDI;
    if(ui->config_open||ui->browser.mode!=TS_BROWSER_CLOSED)return TS_THEME_BROWSER;
    if(ui->master_eq_open||ui->canvas_eq_open)return TS_THEME_EQ;
    if(ui->router_open)return TS_THEME_ROUTER;
    if(ui->portal.open)return TS_THEME_PORTAL;
    if(ui->tracker_open)return TS_THEME_TRACKER;
    if(ui->mosaic_open)return TS_THEME_MOSAIC;
    if(ui->keyboard_sequence_open)return TS_THEME_ARP;
    return TS_THEME_CANVAS;
}
static void pw_follow(SDL_Window *main,const TsUiState *ui,const SisterWindow *sister,Uint32 id)
{
    PaletteWindow *w=&palette_window;if(!w->visible||!w->follow||id==w->id)return;
    int g=palette_active_group(main,ui,sister,id);if(g>0&&g!=w->group){w->group=g;w->scroll=0;if(w->page==2){int roles[PALETTE_ROLE_CAPACITY];pw_roles(w->page,w->detail,roles);w->role=roles[0];}w->dirty=1;pw_hex(&ui->palette);}
}
static int palette_window_event(const SDL_Event *e,SDL_Window *main,TsUiState *ui,SisterWindow *sister)
{
    PaletteWindow *w=&palette_window;Uint32 id=event_window_id(e);
    if(e->type==SDL_KEYDOWN && e->key.keysym.sym==SDLK_p && (e->key.keysym.mod&KMOD_CTRL) && (e->key.keysym.mod&KMOD_ALT) && !(e->key.keysym.mod&(KMOD_SHIFT|KMOD_GUI))) {
        if(!e->key.repeat)palette_window_show(ui);
        return 1;
    }
    if(!w->visible)return 0;
    if(id!=w->id){if(e->type==SDL_WINDOWEVENT&&e->window.event==SDL_WINDOWEVENT_FOCUS_GAINED)pw_follow(main,ui,sister,id);return 0;}
    if(e->type==SDL_WINDOWEVENT) {
        if(e->window.event==SDL_WINDOWEVENT_CLOSE)palette_window_hide(ui,0);
        if(e->window.event==SDL_WINDOWEVENT_FOCUS_LOST && w->drag){w->drag=0;pw_commit(ui);SDL_CaptureMouse(SDL_FALSE);}
        if(e->window.event==SDL_WINDOWEVENT_FOCUS_GAINED&&(w->hex_focus||ts_browser_edits_text(&w->browser)))SDL_StartTextInput();
        w->dirty=1;return 1;
    }
    int x=0,y=0;float lx,ly;
    if(e->type==SDL_MOUSEBUTTONDOWN||e->type==SDL_MOUSEBUTTONUP)SDL_RenderWindowToLogical(w->renderer,e->button.x,e->button.y,&lx,&ly);
    else if(e->type==SDL_MOUSEMOTION)SDL_RenderWindowToLogical(w->renderer,e->motion.x,e->motion.y,&lx,&ly);
    else {int mx,my;if(e->type==SDL_MOUSEWHEEL)wheel_event_mouse(e,&mx,&my);else SDL_GetMouseState(&mx,&my);SDL_RenderWindowToLogical(w->renderer,mx,my,&lx,&ly);}
    x=(int)lx;y=(int)ly;
    if(w->browser.mode!=TS_BROWSER_CLOSED) {
        TsBrowser *b=&w->browser;
        if(e->type==SDL_TEXTINPUT && ts_browser_edits_text(b))ts_browser_append_filename(b,e->text.text);
        if(e->type==SDL_KEYDOWN){SDL_Keycode k=e->key.keysym.sym;if(k==SDLK_ESCAPE){ts_browser_close(b);SDL_StopTextInput();}
            else if(k==SDLK_RETURN){if(b->creating_directory)ts_browser_create_directory(b);else pw_file_action(ui);}
            else if(k==SDLK_a&&(e->key.keysym.mod&KMOD_CTRL)&&ts_browser_edits_text(b)){b->filename[0]=0;b->filename_cursor=0;b->overwrite_armed=0;}
            else if(k==SDLK_BACKSPACE&&ts_browser_edits_text(b))ts_browser_backspace_filename(b);
            else if(k==SDLK_DELETE&&ts_browser_edits_text(b))ts_browser_delete_filename(b);
            else if(k==SDLK_LEFT)ts_browser_move_filename_cursor(b,-1);else if(k==SDLK_RIGHT)ts_browser_move_filename_cursor(b,1);
            else if(k==SDLK_UP)ts_browser_move_selection(b,-1);else if(k==SDLK_DOWN)ts_browser_move_selection(b,1);}
        if(e->type==SDL_MOUSEWHEEL)ts_browser_scroll(b,-e->wheel.y*3);
        if(e->type==SDL_MOUSEBUTTONDOWN&&e->button.button==SDL_BUTTON_LEFT) {
            if(y>=103&&y<127){if(x<120)ts_browser_parent(b);else if(x<256){ts_browser_begin_create_directory(b);SDL_StartTextInput();}}
            else if(y>=141&&y<541){int at=b->scroll+(y-141)/25;if(at<b->entry_count){ts_browser_select(b,at);if(e->button.clicks>1){if(b->entries[at].is_directory)ts_browser_enter_selected_directory(b);else pw_file_action(ui);}}}
            else if(y>=580&&y<604){if(x<202){if(b->creating_directory)ts_browser_create_directory(b);else pw_file_action(ui);}else if(x<334){ts_browser_close(b);SDL_StopTextInput();}}
        }
        w->dirty=1;return 1;
    }
    if(e->type==SDL_TEXTINPUT&&w->hex_focus){for(const char *c=e->text.text;*c;++c)if(isxdigit((unsigned char)*c)&&strlen(w->hex)<6){size_t n=strlen(w->hex);w->hex[n]=(char)toupper((unsigned char)*c);w->hex[n+1]=0;}w->dirty=1;return 1;}
    if(e->type==SDL_KEYDOWN) {
        SDL_Keycode k=e->key.keysym.sym;int ctrl=e->key.keysym.mod&KMOD_CTRL;
        if(w->hex_focus){if(k==SDLK_BACKSPACE){size_t n=strlen(w->hex);if(n)w->hex[n-1]=0;}
            else if(k==SDLK_a&&ctrl)w->hex[0]=0;
            else if(k==SDLK_RETURN&&strlen(w->hex)==6){pw_edit(ui);pw_set_color(&ui->palette,(uint32_t)strtoul(w->hex,NULL,16));pw_commit(ui);w->hex_focus=0;SDL_StopTextInput();}
            else if(k==SDLK_ESCAPE){w->hex_focus=0;pw_hex(&ui->palette);SDL_StopTextInput();}
        } else if(k==SDLK_ESCAPE)palette_window_hide(ui,0);
        else if(ctrl&&k==SDLK_z)pw_history(ui,e->key.keysym.mod&KMOD_SHIFT?1:-1);
        else if(ctrl&&k==SDLK_y)pw_history(ui,1);
        else if(k==SDLK_UP||k==SDLK_DOWN||k==SDLK_PAGEUP||k==SDLK_PAGEDOWN){int roles[PALETTE_ROLE_CAPACITY],n=pw_roles(w->page,w->detail,roles),at=0;while(at<n-1&&roles[at]!=w->role)++at;at=SDL_clamp(at+(k==SDLK_UP||k==SDLK_PAGEUP?-1:1),0,n-1);w->role=roles[at];w->scroll=SDL_clamp(at-PALETTE_ROWS+1,0,SDL_max(0,n-PALETTE_ROWS));pw_hex(&ui->palette);}
        w->dirty=1;return 1;
    }
    if(e->type==SDL_MOUSEWHEEL){if(y>=427&&y<502&&w->page!=1){int channel=SDL_clamp((y-433)/23,0,2),value=(pw_color(&ui->palette)>>((2-channel)*8))&255;
            pw_set_channel(ui,channel,value+e->wheel.y*((SDL_GetModState()&KMOD_SHIFT)?8:1));pw_commit(ui);
        }else{int roles[PALETTE_ROLE_CAPACITY],n=pw_roles(w->page,w->detail,roles);w->scroll=SDL_clamp(w->scroll-e->wheel.y,0,SDL_max(0,n-PALETTE_ROWS));}w->dirty=1;return 1;}
    if(e->type==SDL_MOUSEBUTTONDOWN&&e->button.button==SDL_BUTTON_LEFT) {
        w->hex_focus=0;SDL_StopTextInput();w->dirty=1;
        if(y>=7&&y<31){if(x>=350&&x<514)w->follow=!w->follow;else if(x>=524){w->top=!w->top;SDL_SetWindowAlwaysOnTop(w->window,w->top?SDL_TRUE:SDL_FALSE);}return 1;}
        if(y>=47&&y<100){int col=(x-16)/174,row=(y-47)/29;if(x>=16&&col<4&&row<2&&(y-47)%29<24){pw_edit(ui);ts_palette_preset(&ui->palette,row*4+col);pw_commit(ui);snprintf(w->message,sizeof(w->message),"%s / UNDO RESTORES CUSTOM COLORS",ts_theme_preset_name(row*4+col));}return 1;}
        if(y>=113&&y<137){w->page=x<240?0:x<470?1:2;w->scroll=0;int roles[PALETTE_ROLE_CAPACITY];pw_roles(w->page,w->detail,roles);w->role=roles[0];pw_hex(&ui->palette);return 1;}
        if(w->page==1&&y>=167&&y<477){int row=(y-167)/62;if(row<5){pw_edit(ui);w->drag=10+row;SDL_CaptureMouse(SDL_TRUE);}}
        else if(w->page!=1) {
            if(x>=16&&x<190&&y>=157&&y<493){w->group=1+(y-157)/21;w->page=2;w->follow=0;w->scroll=0;int roles[PALETTE_ROLE_CAPACITY];pw_roles(w->page,w->detail,roles);w->role=roles[0];pw_hex(&ui->palette);return 1;}
            if(w->page!=3&&x>=555&&y>=143&&y<167){w->detail=!w->detail;w->scroll=0;int roles[PALETTE_ROLE_CAPACITY];pw_roles(w->page,w->detail,roles);w->role=roles[0];pw_hex(&ui->palette);return 1;}
            if(x>=204&&y>=176&&y<376){int roles[PALETTE_ROLE_CAPACITY],n=pw_roles(w->page,w->detail,roles),at=w->scroll+(y-176)/25;if(at<n)w->role=roles[at];pw_hex(&ui->palette);return 1;}
            if(x>=340&&x<470&&y>=399&&y<423){w->hex_focus=1;SDL_StartTextInput();return 1;}
            if(x>=326&&x<648&&y>=430&&y<501){pw_edit(ui);w->drag=1+SDL_clamp((y-433)/23,0,2);SDL_CaptureMouse(SDL_TRUE);}
            if(y>=512&&y<536){pw_edit(ui);if(x>=204&&x<440){if(w->role>=PALETTE_LEGACY){TsPalette defaults;ts_palette_default(&defaults);pw_set_color(&ui->palette,defaults.colors[w->role-PALETTE_LEGACY]);}else ui->palette.theme.overrides[pw_group()][w->role]=0;}else if(w->page!=3&&x>=450)ts_theme_reset_group(&ui->palette,pw_group());pw_commit(ui);return 1;}
        }
        if(y>=555&&y<579){if(x<84)pw_history(ui,-1);else if(x<159)pw_history(ui,1);else if(x>=169&&x<311){if(w->compare)pw_edit(ui);else{w->edited=ui->palette;ui->palette=w->original;w->compare=1;}pw_hex(&ui->palette);}
            else if(x>=321&&x<454){pw_edit(ui);ui->palette=w->original;pw_commit(ui);}else if(x>=464&&x<574)palette_window_hide(ui,0);else if(x>=584){w->page=3;w->role=PALETTE_LEGACY;w->scroll=0;pw_hex(&ui->palette);}return 1;}
        if(y>=588&&y<612){if(x<164)pw_file_open(0);else if(x<324)pw_file_open(1);else if(x<510){char path[TS_SHARED_PALETTE_PATH_MAX];
                if(!shared_palette_path(ui,path,sizeof(path)))snprintf(w->message,sizeof(w->message),"SHARED PALETTE PATH IS TOO LONG");
                else if(ts_browser_path_exists(path)&&!w->overwrite_shared){w->overwrite_shared=1;snprintf(w->message,sizeof(w->message),"SAVE SHARED AGAIN TO REPLACE THE SHARED PALETTE");}
                else{if(ts_palette_save(w->compare?&w->edited:&ui->palette,path,w->message,sizeof(w->message)))snprintf(w->message,sizeof(w->message),"SHARED PALETTE SAVED / RECALLS ON STARTUP");w->overwrite_shared=0;}}
            return 1;}
    }
    if(w->drag&&(e->type==SDL_MOUSEMOTION||e->type==SDL_MOUSEBUTTONDOWN)) {
        if(w->drag<10)pw_set_channel(ui,w->drag-1,(x-326)*255/314);
        else {int value=SDL_clamp((x-24)*100/660,0,100);switch(w->drag-10){case 0:ui->palette.theme.text_clarity=(uint8_t)value;break;case 1:ui->palette.theme.grid_visibility=(uint8_t)value;break;case 2:ui->palette.theme.selection_strength=(uint8_t)value;break;case 3:ui->palette.desktop_contrast=SDL_max(1,value);break;case 4:ui->palette.buttons_contrast=SDL_max(1,value);break;}}
        w->dirty=1;
    }
    if(e->type==SDL_MOUSEBUTTONUP&&w->drag){w->drag=0;SDL_CaptureMouse(SDL_FALSE);pw_commit(ui);w->dirty=1;}
    return 1;
}
static void palette_window_update(SDL_Window *main,TsUiState *ui,SisterWindow *sister)
{
    PaletteWindow *w=&palette_window;if(!w->visible)return;
    SDL_Window *focus=SDL_GetKeyboardFocus();if(focus)pw_follow(main,ui,sister,SDL_GetWindowID(focus));
    if(!w->dirty||!window_drawable(w->window))return;
    pw_paint(ui);SDL_UpdateTexture(w->texture,NULL,w->pixels,PALETTE_W*sizeof(uint32_t));SDL_RenderCopy(w->renderer,w->texture,NULL,NULL);SDL_RenderPresent(w->renderer);w->dirty=0;
}
