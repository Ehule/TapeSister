/* F9: one modeless patch matrix. No DSP or project export in paint/input. */
enum { MATRIX_W=1280,MATRIX_H=840,MATRIX_X=264,MATRIX_Y=176,
       MATRIX_CW=98,MATRIX_RH=28,MATRIX_VISIBLE=17,MATRIX_COLS=10 };
typedef struct {int kind,index;TsTileId tile;char name[48];} MatrixRow;
typedef struct {
    SDL_Window *window;SDL_Renderer *renderer;SDL_Texture *texture;uint32_t *pixels;
    Uint32 id,presented;int visible,dirty,scroll,column,filter,selected,destination,drag;
    MatrixRow rows[TS_TRACKER_LANES+TS_SISTER_RUNTIME_PAGE_LIMIT*TS_BANK_SLOT_COUNT+TS_MATRIX_ROWS];int count;
    TsMatrixControls controls;TsSourceRoute route;
    TsRouterControls legacy;TsSpatialControls array;
    int rename;char name[TS_MATRIX_NAME],status[150];
    float peaks[TS_MATRIX_ROWS];unsigned inputs,outputs;
} MatrixWindow;
static MatrixWindow matrix_window={.selected=-1,.rename=-1};
#define MX_BG 0xff151c20u
#define MX_PANEL 0xff202b30u
#define MX_TEXT 0xffdbe8e3u
#define MX_DIM 0xff94aaa7u
#define MX_GREEN 0xff8addb4u
#define MX_YELLOW 0xffffce69u
static void mx_rect(int x,int y,int width,int height,uint32_t color)
{
    MatrixWindow *w=&matrix_window;
    for(int yy=SDL_max(0,y);yy<SDL_min(MATRIX_H,y+height);++yy)
        for(int xx=SDL_max(0,x);xx<SDL_min(MATRIX_W,x+width);++xx)w->pixels[yy*MATRIX_W+xx]=color;
}
static void mx_text(int x,int y,const char *text,uint32_t color,int scale)
{
    for(;*text;++text,x+=6*scale) {
        const char *glyph=ts_ui_glyph(*text);
        for(int r=0;r<7;++r)for(int c=0;c<5;++c)if(glyph[r*5+c]=='1')mx_rect(x+c*scale,y+r*scale,scale,scale,color);
    }
}
static void mx_button(int x,int y,int width,const char *label,int selected)
{
    mx_rect(x,y,width,28,selected?0xff365746u:MX_PANEL);
    mx_text(x+10,y+7,label,selected?MX_GREEN:MX_TEXT,2);
}
static TsBankSlot *mx_tile(TsSamplePages *pages,TsInstrument *bank,TsTileId id)
{ return (TsBankSlot *)ts_sample_pages_find_tile(pages,bank,id,NULL); }
static void mx_rows(TsSamplePages *pages,TsInstrument *bank)
{
    MatrixWindow *w=&matrix_window;MatrixRow old={0};if(w->selected>=0 && w->selected<w->count)old=w->rows[w->selected];
    w->count=0;int f=w->filter;
    if(!f || f==1)for(int i=0;i<pages->tracker.channel_count;++i) {
        MatrixRow *r=&w->rows[w->count++];*r=(MatrixRow){.kind=0,.index=i};
        snprintf(r->name,sizeof(r->name),"TRK %02d %.12s",i+1,pages->tracker.lanes[i].name);
    }
    if(!f || f==2)for(size_t page=0;page<ts_sample_pages_count(pages) && page<TS_SISTER_RUNTIME_PAGE_LIMIT;++page) {
        const TsInstrument *source=ts_sample_pages_page(pages,bank,page);if(!source)continue;
        for(int i=0;i<TS_BANK_SLOT_COUNT;++i)if(source->bank[i].occupied) {
            MatrixRow *r=&w->rows[w->count++];*r=(MatrixRow){.kind=1,.index=(int)page*TS_BANK_SLOT_COUNT+i,.tile=source->bank[i].tile_id};
            snprintf(r->name,sizeof(r->name),"P%02zu:%02d %.12s",page+1,i+1,source->bank[i].sample.name);
        }
    }
    if(!f || f==3)for(int i=TS_MATRIX_NODES;i<TS_MATRIX_ROWS;++i) {
        MatrixRow *r=&w->rows[w->count++];*r=(MatrixRow){.kind=2,.index=i};
        if(i<TS_MATRIX_FM)snprintf(r->name,sizeof(r->name),"IN %d/%d %.12s",(i-TS_MATRIX_NODES)*2+1,(i-TS_MATRIX_NODES)*2+2,w->controls.input_name[i-TS_MATRIX_NODES]);
        else snprintf(r->name,sizeof(r->name),"%s",i==TS_MATRIX_FM?"FM SYNTH":"TAPEHEAD LIVE LINK");
    }
    if(!f || f==4)for(int i=0;i<TS_MATRIX_NODES;++i) {
        MatrixRow *r=&w->rows[w->count++];*r=(MatrixRow){.kind=2,.index=i};
        snprintf(r->name,sizeof(r->name),"%s%s",ts_matrix_node_name(i),i>=1 && i<=4?" WET":"");
    }
    w->selected=-1;
    for(int i=0;i<w->count;++i)if(w->rows[i].kind==old.kind && w->rows[i].index==old.index && w->rows[i].tile==old.tile)w->selected=i;
    if(w->selected<0 && w->count)w->selected=0;
    w->scroll=SDL_max(0,SDL_min(w->scroll,w->count-MATRIX_VISIBLE));
}
static TsSourceRoute mx_route(MatrixRow row,TsSamplePages *pages,TsInstrument *bank)
{
    if(row.kind==2)return matrix_window.controls.row[row.index];
    TsBankSlot *slot=row.kind==1?mx_tile(pages,bank,row.tile):NULL;
    return row.kind==0?pages->tracker.lanes[row.index].output_route:slot?slot->output_route:(TsSourceRoute){0};
}
/* Legacy clean routes use speaker-array indices, and Master Mix may collect
   their clean branch. Show that actual destination until the row is edited. */
static int mx_legacy_direct(TsSourceRoute r)
{
    if(r.matrix_enabled || r.mode==TS_SOURCE_MAIN || r.mode==TS_SOURCE_INHERIT ||
       (r.mix_enabled && matrix_window.legacy.master_mix))return 0;
    int left=matrix_window.array.output[r.speaker],right=matrix_window.array.output[r.second];
    return left<0 || left>=TS_MATRIX_OUTPUTS*2 ||
        (r.mode==TS_SOURCE_PAIR && ((left&1) || right!=left+1));
}
static TsSourceRoute mx_display_route(TsSourceRoute raw)
{
    if(raw.matrix_enabled || raw.mode==TS_SOURCE_MAIN || raw.mode==TS_SOURCE_INHERIT)return ts_matrix_source_route(raw);
    if(raw.mix_enabled && matrix_window.legacy.master_mix) {
        TsSourceRoute r=ts_matrix_source_route(raw);
        for(int d=TS_MATRIX_NODES;d<TS_MATRIX_DESTINATIONS;++d)r.matrix_gain[d]=0;
        r.matrix_gain[TS_MATRIX_MAIN]=raw.clean_level*10;return r;
    }
    if(!mx_legacy_direct(raw)) {
        raw.speaker=matrix_window.array.output[raw.speaker];
        raw.second=matrix_window.array.output[raw.second];
    }
    return ts_matrix_source_route(raw);
}
static void matrix_hide(void)
{
    matrix_window.visible=0;matrix_window.drag=0;matrix_window.rename=-1;
    SDL_StopTextInput();if(matrix_window.window)SDL_HideWindow(matrix_window.window);
}
static void matrix_close(void)
{
    MatrixWindow *w=&matrix_window;
    if(w->texture)SDL_DestroyTexture(w->texture);
    if(w->renderer)SDL_DestroyRenderer(w->renderer);
    if(w->window)SDL_DestroyWindow(w->window);
    free(w->pixels);memset(w,0,sizeof(*w));w->selected=w->rename=-1;
}
static int matrix_show(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,
    TsSamplePages *pages,TsInstrument *bank,int track,int slot)
{
    MatrixWindow *w=&matrix_window;
    if(!w->window) {
        w->window=SDL_CreateWindow("TapeSister - Routing Matrix (F9)",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,
            MATRIX_W,MATRIX_H,SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI);
        if(w->window){SDL_SetWindowMinimumSize(w->window,960,630);w->renderer=SDL_CreateRenderer(w->window,-1,SDL_RENDERER_SOFTWARE);}
        if(w->renderer)w->texture=SDL_CreateTexture(w->renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,MATRIX_W,MATRIX_H);
        w->pixels=calloc(MATRIX_W*MATRIX_H,sizeof(uint32_t));
        if(!w->window || !w->renderer || !w->texture || !w->pixels){matrix_close();snprintf(ui->status,sizeof(ui->status),"MATRIX WINDOW FAILED: %.110s",SDL_GetError());return 0;}
        w->id=SDL_GetWindowID(w->window);
    }
    if(device)SDL_LockAudioDevice(device);
    w->controls=audio->sister.matrix.controls;w->legacy=audio->sister.router.controls;w->array=audio->sister.spatial.controls;
    if(device)SDL_UnlockAudioDevice(device);
    w->filter=track>=0?1:slot>=0?2:0;w->scroll=0;mx_rows(pages,bank);
    for(int i=0;i<w->count;++i)if((track>=0 && w->rows[i].kind==0 && w->rows[i].index==track) ||
        (slot>=0 && w->rows[i].kind==1 && w->rows[i].tile==bank->bank[slot].tile_id)){w->selected=i;w->scroll=SDL_max(0,i-MATRIX_VISIBLE+1);}
    w->visible=w->dirty=1;w->rename=-1;w->status[0]=0;ui->router_open=0;
    source_route_hide();SDL_ShowWindow(w->window);SDL_RaiseWindow(w->window);return 1;
}
static void mx_destination(char *name,size_t size,int d,int full)
{
    const char *shorts[]={"MAIN","SISTER","PRISM","PEDALS","FALLOUT","EQ","LIMITER"};
    if(d<TS_MATRIX_NODES)snprintf(name,size,"%s",full?ts_matrix_node_name(d):shorts[d]);
    else {int p=d-TS_MATRIX_NODES;snprintf(name,size,full?"OUT %d/%d %s":"OUT %d/%d",p*2+1,p*2+2,matrix_window.controls.output_name[p]);}
}
static void mx_paint(AudioState *audio,TsSamplePages *pages,TsInstrument *bank)
{
    MatrixWindow *w=&matrix_window;char label[160];mx_rect(0,0,MATRIX_W,MATRIX_H,MX_BG);
    mx_text(22,22,"TAPESISTER / ROUTING MATRIX",MX_GREEN,3);
    mx_text(22,57,"FROM A ROW > TO A COLUMN. ONE SHARED INSTANCE OF EACH PROCESSOR.",MX_DIM,2);
    mx_button(22,88,102,"ALL",w->filter==0);mx_button(134,88,126,"TRACKS",w->filter==1);
    mx_button(270,88,114,"TILES",w->filter==2);mx_button(394,88,126,"INPUTS",w->filter==3);
    mx_button(530,88,138,"RETURNS",w->filter==4);
    mx_button(690,88,174,w->controls.enabled?"MATRIX LIVE":"USE MATRIX",w->controls.enabled);
    mx_button(876,88,174,"CUT FEEDBACK",0);mx_button(1062,88,192,w->controls.feedback?"FEEDBACK ON":"FEEDBACK OFF",w->controls.feedback);
    mx_text(22,137,"SOURCE / OUTPUT",MX_DIM,2);
    const char *shorts[]={"MAIN","SISTER","PRISM","PEDALS","FALLOUT","EQ","LIMITER"};
    for(int col=0;col<MATRIX_COLS && col+w->column<TS_MATRIX_DESTINATIONS;++col) {
        int d=col+w->column,x=MATRIX_X+col*MATRIX_CW;
        mx_rect(x,128,MATRIX_CW-2,44,d==w->destination?0xff4a4532u:MX_PANEL);
        if(d<TS_MATRIX_NODES)snprintf(label,sizeof(label),"%s",shorts[d]);
        else snprintf(label,sizeof(label),"OUT%d/%d",(d-TS_MATRIX_NODES)*2+1,(d-TS_MATRIX_NODES)*2+2);
        mx_text(x+5,133,label,d==w->destination?MX_YELLOW:MX_TEXT,strlen(label)>7?1:2);
        const char *detail=d<TS_MATRIX_NODES?(d==0?"SUM":d==1?"TAPE IN":d<=4?"FX IN":"IN"):w->controls.output_name[d-TS_MATRIX_NODES];
        snprintf(label,sizeof(label),"%.14s",detail);mx_text(x+5,151,label,MX_DIM,1);
        if(d>=TS_MATRIX_NODES && (unsigned)(d-TS_MATRIX_NODES)*2>=w->outputs)mx_text(x+68,153,"N/A",MX_YELLOW,1);
    }
    for(int line=0;line<MATRIX_VISIBLE && line+w->scroll<w->count;++line) {
        int at=line+w->scroll,y=MATRIX_Y+line*MATRIX_RH;MatrixRow row=w->rows[at];
        TsSourceRoute raw=mx_route(row,pages,bank),r=mx_display_route(raw);int inherit=raw.mode==TS_SOURCE_INHERIT || mx_legacy_direct(raw);
        mx_rect(20,y,1234,MATRIX_RH-2,at==w->selected?0xff2a393bu:line&1?0xff1c262bu:0xff192227u);
        snprintf(label,sizeof(label),"%.19s",row.name);mx_text(28,y+6,label,at==w->selected?MX_YELLOW:row.kind==2?MX_GREEN:MX_TEXT,2);
        if(inherit)mx_text(225,y+19,mx_legacy_direct(raw)?"OLD":"TILE",MX_YELLOW,1);
        if(row.kind==2) {
            float peak=w->peaks[row.index];mx_rect(22,y+2,2,24,peak>.0001f?MX_GREEN:MX_PANEL);
            int off=(row.index==TS_MATRIX_SISTER&&!audio->sister.enabled) ||
                (row.index==TS_MATRIX_PRISM&&!audio->sister.parameters.prism.enabled) ||
                (row.index==TS_MATRIX_BOARD&&!audio->sister.parameters.fx.enabled) ||
                (row.index==TS_MATRIX_FALLOUT&&(!audio->sister.parameters.fx.enabled||!audio->sister.parameters.fx.fallout.enabled));
            if(off)mx_text(225,y+19,"OFF",MX_YELLOW,1);
            if(row.index>=TS_MATRIX_NODES && row.index<TS_MATRIX_FM && (unsigned)(row.index-TS_MATRIX_NODES)*2>=w->inputs)mx_text(225,y+19,"N/A",MX_YELLOW,1);
        }
        for(int col=0;col<MATRIX_COLS && col+w->column<TS_MATRIX_DESTINATIONS;++col) {
            int d=col+w->column,x=MATRIX_X+col*MATRIX_CW,g=r.matrix_gain[d];
            int selected=at==w->selected&&d==w->destination;
            int delayed=row.kind==2&&row.index<TS_MATRIX_NODES&&(w->controls.delayed[row.index]&(1u<<d));
            mx_rect(x,y,MATRIX_CW-2,MATRIX_RH-2,selected?0xff4a4532u:d==w->destination?0xff303733u:MX_PANEL);
            mx_rect(x+7,y+6,14,14,inherit?MX_DIM:g?(delayed?MX_YELLOW:MX_GREEN):0xff47605du);
            mx_rect(x+9,y+8,10,10,g&&!inherit?(delayed?MX_YELLOW:MX_GREEN):MX_BG);
            if(g&&!inherit){mx_text(x+12,y+9,"X",MX_BG,1);snprintf(label,sizeof(label),"%+.1f",20*log10f(g*.001f));}
            else snprintf(label,sizeof(label),"%s",inherit?(mx_legacy_direct(raw)?"OLD":"TILE"):"--");
            mx_text(x+28,y+6,label,g?MX_TEXT:MX_DIM,2);if(delayed)mx_text(x+79,y+9,"F",MX_YELLOW,1);
        }
    }
    int bottom=MATRIX_Y+MATRIX_VISIBLE*MATRIX_RH;
    snprintf(label,sizeof(label),"ROWS %d-%d / %d   WHEEL: SCROLL | CTRL+WHEEL ON CELL: LEVEL",w->count?w->scroll+1:0,SDL_min(w->count,w->scroll+MATRIX_VISIBLE),w->count);
    mx_text(24,bottom+6,label,MX_DIM,1);mx_button(1062,bottom+1,90,"< OUT",0);mx_button(1162,bottom+1,90,"OUT >",0);
    mx_rect(20,692,1234,98,MX_PANEL);
    if(w->selected>=0 && w->selected<w->count) {
        MatrixRow row=w->rows[w->selected];TsSourceRoute raw=mx_route(row,pages,bank),r=mx_display_route(raw);
        char dest[60];mx_destination(dest,sizeof(dest),w->destination,1);
        snprintf(label,sizeof(label),mx_legacy_direct(raw)?"%.24s / LEGACY DIRECT - CONVERT ROW TO EDIT":"%.24s > %.38s",row.name,dest);mx_text(32,702,label,MX_YELLOW,2);
        int g=r.matrix_gain[w->destination];snprintf(label,sizeof(label),"LEVEL %s",g?"":"OFF");
        if(g)snprintf(label,sizeof(label),"LEVEL %+.1f dB",20*log10f(g*.001f));mx_text(32,735,label,MX_TEXT,2);
        mx_rect(252,743,300,3,MX_DIM);int lx=252+(int)(g/2000.f*300);mx_rect(lx-3,735,6,19,MX_GREEN);
        snprintf(label,sizeof(label),"ROW PAN %s%d",r.pan<0?"L":r.pan>0?"R":"",abs(r.pan));mx_text(588,735,label,MX_TEXT,2);
        mx_rect(784,743,260,3,MX_DIM);mx_rect(913,734,2,20,MX_DIM);mx_rect(914,743,r.pan>0?r.pan*130/100:0,3,MX_YELLOW);
        if(r.pan<0)mx_rect(914+r.pan*130/100,743,-r.pan*130/100,3,MX_YELLOW);
        mx_rect(914+r.pan*130/100-3,735,6,19,MX_YELLOW);
        mx_button(1070,731,170,mx_legacy_direct(raw)?"USE MATRIX":row.kind==0?(raw.mode==TS_SOURCE_INHERIT?"USE TRACK":"USE TILE"):"NAME I/O",0);
        mx_text(32,769,"CHECKBOX: CONNECT | RIGHT-CLICK: SELECT | MIDDLE-CLICK I/O: NAME | RIGHT-CLICK PAN: CENTER",MX_DIM,1);
        mx_button(1070,762,170,r.width==-100?"MONO":"STEREO",r.width==-100);
    }
    if(w->rename>=0) {
        mx_rect(252,730,792,51,0xff33443du);snprintf(label,sizeof(label),"NAME: %.23s_",w->name);mx_text(266,738,label,MX_YELLOW,2);
        mx_text(266,763,"ENTER: SAVE / ESC: CANCEL - CHANNEL NUMBERS STAY VISIBLE",MX_TEXT,1);
    }
    if(!w->controls.enabled)mx_text(24,804,"LEGACY PROJECT: USE MATRIX STARTS A NEW MAIN > EQ > LIMITER PATH. EXISTING SOURCE SENDS ARE RETAINED.",MX_YELLOW,1);
    else if(w->status[0])mx_text(24,804,w->status,MX_YELLOW,1);
    else mx_text(24,804,"CHANGES ARE LIVE. INPUTS/OUTPUTS USE THE ACTIVE AUDIO DEVICE. N/A = PORT NOT OPEN. F9 / ESC CLOSE.",MX_DIM,1);
    mx_text(1180,25,"F9",MX_DIM,2);
}
static void mx_commit_controls(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui)
{
    if(device)SDL_LockAudioDevice(device);
    ts_matrix_set(&audio->sister.matrix,&matrix_window.controls,(unsigned)audio->output_rate);
    ui->config.matrix=audio->sister.matrix.controls;
    if(device)SDL_UnlockAudioDevice(device);
    matrix_window.dirty=1;
}
static void mx_commit_route(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,TsSamplePages *pages,TsInstrument *bank,TsSourceRoute route)
{
    MatrixWindow *w=&matrix_window;if(w->selected<0 || w->selected>=w->count)return;
    MatrixRow row=w->rows[w->selected];
    if(row.kind==2) {w->controls.row[row.index]=route;mx_commit_controls(device,audio,ui);return;}
    if(!ts_source_route_valid(&route,row.kind==0))return;
    if(device)SDL_LockAudioDevice(device);
    if(row.kind==0)pages->tracker.lanes[row.index].output_route=route;
    else {TsBankSlot *slot=mx_tile(pages,bank,row.tile);if(slot){slot->output_route=route;source_route_update_voices(audio,row.tile,route);}}
    if(device)SDL_UnlockAudioDevice(device);
    ts_tapehead_routes_changed(row.kind==1?row.tile:0,route);w->dirty=1;
}
static void mx_gain(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,TsSamplePages *pages,TsInstrument *bank,int gain)
{
    MatrixWindow *w=&matrix_window;MatrixRow row=w->rows[w->selected];TsSourceRoute raw=mx_route(row,pages,bank);
    if(!w->controls.enabled){snprintf(w->status,sizeof(w->status),"SELECT USE MATRIX FIRST; LEGACY ROUTING IS STILL PLAYING.");return;}
    if(mx_legacy_direct(raw)){snprintf(w->status,sizeof(w->status),"THIS ROW USES A LEGACY CROSS-PAIR SPEAKER MAP. USE MATRIX REPLACES IT WITH MAIN MIX.");return;}
    if(raw.mode==TS_SOURCE_INHERIT){snprintf(w->status,sizeof(w->status),"THIS TRACK USES ITS TILES. SELECT USE TRACK TO OVERRIDE THEM.");return;}
    gain=SDL_max(0,SDL_min(gain,2000));
    if(row.kind==2) {
        int result=ts_matrix_connect(&w->controls,row.index,w->destination,gain);
        if(result<=0){snprintf(w->status,sizeof(w->status),"LOOP: REMOVE THE RETURN PATH FOR A SERIES CHAIN, OR ENABLE FEEDBACK FOR A DELAYED LOOP.");return;}
        mx_commit_controls(device,audio,ui);
        if(row.index<TS_MATRIX_NODES && w->controls.delayed[row.index]&(1u<<w->destination))
            snprintf(w->status,sizeof(w->status),"FEEDBACK: 64-SAMPLE DELAY / MAX -6 dB / BOUNDED RETURN. CUT FEEDBACK REMOVES ALL LOOP CONNECTIONS.");
    } else {TsSourceRoute r=mx_display_route(raw);r.matrix_gain[w->destination]=gain;mx_commit_route(device,audio,ui,pages,bank,r);}
}
static void matrix_update(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,TsSamplePages *pages,TsInstrument *bank)
{
    MatrixWindow *w=&matrix_window;(void)ui;if(!w->visible || !window_drawable(w->window) || SDL_GetTicks()-w->presented<50)return;
    if(device)SDL_LockAudioDevice(device);
    w->controls=audio->sister.matrix.controls;w->legacy=audio->sister.router.controls;w->array=audio->sister.spatial.controls;memcpy(w->peaks,audio->sister.matrix.output_peak,sizeof(w->peaks));
    w->inputs=audio->sister.insert.duplex?atomic_load(&audio->sister.insert.input_channels):atomic_load(&audio->sister.insert.matrix_channels);
    w->outputs=audio->output_device_channels;
    if(device)SDL_UnlockAudioDevice(device);
    mx_rows(pages,bank);mx_paint(audio,pages,bank);
    SDL_UpdateTexture(w->texture,NULL,w->pixels,MATRIX_W*4);SDL_RenderClear(w->renderer);
    SDL_RenderCopy(w->renderer,w->texture,NULL,NULL);SDL_RenderPresent(w->renderer);w->presented=SDL_GetTicks();w->dirty=0;
}
static void mx_name(int port)
{
    MatrixWindow *w=&matrix_window;w->rename=port;
    snprintf(w->name,sizeof(w->name),"%s",port<TS_MATRIX_INPUTS?w->controls.input_name[port]:w->controls.output_name[port-TS_MATRIX_INPUTS]);
    SDL_StartTextInput();w->dirty=1;
}
static int matrix_event(SDL_Event *e,SDL_Window *main,SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,TsSamplePages *pages,TsInstrument *bank)
{
    MatrixWindow *w=&matrix_window;int own=w->window&&event_window_id(e)==w->id;
    if(e->type==SDL_KEYDOWN && e->key.keysym.sym==SDLK_F9 && !(e->key.keysym.mod&(KMOD_CTRL|KMOD_ALT|KMOD_GUI))) {
        if(!e->key.repeat){if(w->visible)matrix_hide();else matrix_show(device,audio,ui,pages,bank,-1,-1);}return 1;
    }
    if(!own && ui->master_eq_open && event_window_id(e)==SDL_GetWindowID(main) &&
       e->type==SDL_MOUSEBUTTONDOWN && e->button.button==SDL_BUTTON_LEFT) {
        int x,y;logical_mouse(main,e->button.x,e->button.y,&x,&y);
        if(x>=528 && x<616 && y>=340 && y<365) {
            ui->master_eq_open=0;matrix_show(device,audio,ui,pages,bank,-1,-1);return 1;
        }
    }
    if(!own)return 0;
    if(e->type==SDL_WINDOWEVENT) {
        if(e->window.event==SDL_WINDOWEVENT_CLOSE)matrix_hide();
        if(e->window.event==SDL_WINDOWEVENT_FOCUS_LOST)w->drag=0;
        w->dirty=1;return 1;
    }
    if(!w->visible)return 1;
    if(w->rename>=0) {
        if(e->type==SDL_TEXTINPUT)for(const char *s=e->text.text;*s;++s) {
            size_t n=strlen(w->name);if(n+1<TS_MATRIX_NAME && *s>=32 && *s<=126 && *s!='='){w->name[n]=*s;w->name[n+1]=0;}
        }
        if(e->type==SDL_KEYDOWN) {
            if(e->key.keysym.sym==SDLK_BACKSPACE){size_t n=strlen(w->name);if(n)w->name[n-1]=0;}
            if(e->key.keysym.sym==SDLK_RETURN) {
                snprintf(w->rename<TS_MATRIX_INPUTS?w->controls.input_name[w->rename]:w->controls.output_name[w->rename-TS_MATRIX_INPUTS],TS_MATRIX_NAME,"%s",w->name);
                mx_commit_controls(device,audio,ui);w->rename=-1;SDL_StopTextInput();
            } else if(e->key.keysym.sym==SDLK_ESCAPE){w->rename=-1;SDL_StopTextInput();}
        }
        w->dirty=1;return 1;
    }
    if(e->type==SDL_KEYDOWN && e->key.keysym.sym==SDLK_ESCAPE){matrix_hide();return 1;}
    if(e->type==SDL_KEYDOWN || e->type==SDL_KEYUP){e->key.windowID=SDL_GetWindowID(main);return 0;}
    int x,y,rx,ry,ww,hh;SDL_GetWindowSize(w->window,&ww,&hh);
    if(e->type==SDL_MOUSEBUTTONDOWN || e->type==SDL_MOUSEBUTTONUP){rx=e->button.x;ry=e->button.y;}
    else if(e->type==SDL_MOUSEMOTION){rx=e->motion.x;ry=e->motion.y;}
    else if(e->type==SDL_MOUSEWHEEL)wheel_event_mouse(e,&rx,&ry);else return 1;
    x=rx*MATRIX_W/SDL_max(1,ww);y=ry*MATRIX_H/SDL_max(1,hh);w->dirty=1;
    if(e->type==SDL_MOUSEBUTTONUP){w->drag=0;return 1;}
    int click=e->type==SDL_MOUSEBUTTONDOWN,left=click&&e->button.button==SDL_BUTTON_LEFT,middle=click&&e->button.button==SDL_BUTTON_MIDDLE;
    if(left && y>=88 && y<116) {
        int f=x<124?0:x<260?1:x<384?2:x<520?3:x<668?4:-1;
        if(f>=0){w->filter=f;w->scroll=0;mx_rows(pages,bank);return 1;}
        if(x>=690 && x<864 && !w->controls.enabled){w->controls.enabled=1;mx_commit_controls(device,audio,ui);return 1;}
        if(x>=876 && x<1050){ts_matrix_cut_feedback(&w->controls);mx_commit_controls(device,audio,ui);return 1;}
        if(x>=1062){if(w->controls.feedback)ts_matrix_cut_feedback(&w->controls);else w->controls.feedback=1;mx_commit_controls(device,audio,ui);return 1;}
    }
    if(click && y>=128 && y<MATRIX_Y && x>=MATRIX_X) {
        int d=(x-MATRIX_X)/MATRIX_CW+w->column;
        if(d<TS_MATRIX_DESTINATIONS){w->destination=d;if(middle && d>=TS_MATRIX_NODES)mx_name(TS_MATRIX_INPUTS+d-TS_MATRIX_NODES);}return 1;
    }
    int at=(y-MATRIX_Y)/MATRIX_RH+w->scroll;
    if(y>=MATRIX_Y && y<MATRIX_Y+MATRIX_RH*MATRIX_VISIBLE) {
        if(e->type==SDL_MOUSEWHEEL) {
            if((SDL_GetModState()&KMOD_CTRL) && x>=MATRIX_X && at<w->count) {
                w->selected=at;w->destination=SDL_min(TS_MATRIX_DESTINATIONS-1,(x-MATRIX_X)/MATRIX_CW+w->column);
                TsSourceRoute r=mx_display_route(mx_route(w->rows[at],pages,bank));int g=r.matrix_gain[w->destination];
                if(g)mx_gain(device,audio,ui,pages,bank,(int)lrintf(g*powf(10,e->wheel.y*.05f)));
            } else w->scroll=SDL_max(0,SDL_min(w->count-MATRIX_VISIBLE,w->scroll-e->wheel.y*3));return 1;
        }
        if(click && at<w->count) {
            w->selected=at;w->status[0]=0;MatrixRow row=w->rows[at];
            if(x<MATRIX_X) {if(middle && row.kind==2 && row.index>=TS_MATRIX_NODES && row.index<TS_MATRIX_FM)mx_name(row.index-TS_MATRIX_NODES);}
            else {int d=(x-MATRIX_X)/MATRIX_CW+w->column;if(d<TS_MATRIX_DESTINATIONS){w->destination=d;TsSourceRoute r=mx_display_route(mx_route(row,pages,bank));if(left)mx_gain(device,audio,ui,pages,bank,r.matrix_gain[d]?0:1000);}}
        }return 1;
    }
    if(left && y>=653 && y<683 && x>=1062){w->column=SDL_max(0,SDL_min(TS_MATRIX_DESTINATIONS-MATRIX_COLS,w->column+(x<1152?-1:1)));return 1;}
    if(w->selected<0 || w->selected>=w->count)return 1;
    MatrixRow row=w->rows[w->selected];TsSourceRoute raw=mx_route(row,pages,bank),r=mx_display_route(raw);
    if(left && x>=1070 && y>=731 && y<759) {
        if(mx_legacy_direct(raw)){mx_commit_route(device,audio,ui,pages,bank,(TsSourceRoute){0});return 1;}
        if(row.kind==0) {if(raw.mode==TS_SOURCE_INHERIT)raw=(TsSourceRoute){0};else raw.mode=TS_SOURCE_INHERIT;mx_commit_route(device,audio,ui,pages,bank,raw);}
        else if(row.kind==2 && row.index>=TS_MATRIX_NODES && row.index<TS_MATRIX_FM)mx_name(row.index-TS_MATRIX_NODES);
        else if(w->destination>=TS_MATRIX_NODES)mx_name(TS_MATRIX_INPUTS+w->destination-TS_MATRIX_NODES);
        else snprintf(w->status,sizeof(w->status),"SELECT AN INPUT ROW OR OUTPUT COLUMN TO NAME ITS HARDWARE.");return 1;
    }
    if(left && x>=1070 && y>=762 && y<790 && raw.mode!=TS_SOURCE_INHERIT && !mx_legacy_direct(raw) && w->controls.enabled) {
        r.width=r.width==-100?0:-100;mx_commit_route(device,audio,ui,pages,bank,r);return 1;
    }
    if(click && y>=730 && y<760) {
        if(x>=252 && x<=552)w->drag=1;
        if(x>=784 && x<=1044)w->drag=2;
    }
    if(w->drag && (click || e->type==SDL_MOUSEMOTION)) {
        if(w->drag==1)mx_gain(device,audio,ui,pages,bank,(x-252)*2000/300);
        else if(raw.mode!=TS_SOURCE_INHERIT && !mx_legacy_direct(raw) && w->controls.enabled) {
            r.pan=click&&e->button.button==SDL_BUTTON_RIGHT?0:SDL_max(-100,SDL_min(100,(x-914)*100/130));
            mx_commit_route(device,audio,ui,pages,bank,r);
        }
    }
    return 1;
}
#undef MX_BG
#undef MX_PANEL
#undef MX_TEXT
#undef MX_DIM
#undef MX_GREEN
#undef MX_YELLOW
