/* F9: one modeless patch matrix. No DSP or project export in paint/input. */
enum { MATRIX_W=1280,MATRIX_H=840,MATRIX_X=264,MATRIX_Y=176,
       MATRIX_CW=98,MATRIX_RH=28,MATRIX_VISIBLE=17,MATRIX_COLS=10,
       MATRIX_CAPACITY=TS_TRACKER_LANES+TS_SISTER_RUNTIME_PAGE_LIMIT*TS_BANK_SLOT_COUNT+TS_MATRIX_ROWS+TS_MOSAIC_EVENTS,
       MATRIX_ACTIVITY_VOICES=TS_NOTE_BANK_VOICE_CAPACITY+3*TS_PERFORMANCE_VOICE_LIMIT+TS_BANK_SLOT_COUNT+1 };
typedef struct {int kind,index;TsTileId tile;char name[48];} MatrixRow;
typedef struct MatrixSnapshot MatrixSnapshot;
typedef struct {
    SDL_Window *window;SDL_Renderer *renderer;SDL_Texture *texture;uint32_t *pixels;
    Uint32 id,presented;int visible,dirty,scroll,column,filter,selected,destination,drag;
    MatrixRow rows[MATRIX_CAPACITY],selection[MATRIX_CAPACITY];int count,selection_count,anchor;
    TsMosaic *mosaic;uint64_t epoch;
    MatrixSnapshot *history[32],*pending,*original,*edited;int history_count,history_cursor,original_active,history_skip;
    TsMatrixControls controls;TsSourceRoute route;
    TsRouterControls legacy;TsSpatialControls array;
    int rename;Uint32 rename_blink_started;char name[TS_MATRIX_NAME],status[150];
    float peaks[TS_MATRIX_ROWS];unsigned inputs,outputs;
    float sends[TS_MATRIX_ROWS],receiving[TS_MATRIX_DESTINATIONS];
    float card_source[TS_MOSAIC_EVENTS],card_send[TS_MOSAIC_EVENTS];
    float row_source[MATRIX_VISIBLE],row_send[MATRIX_VISIBLE];
    MatrixRow activity_rows[MATRIX_VISIBLE];
    TsRouteActivity activity[MATRIX_ACTIVITY_VOICES],tracks[TS_TRACKER_LANES],tiles[TS_TAPEHEAD_ROUTE_ACTIVITY_COUNT];
    int activity_count,activity_ready;unsigned processors;
    uint64_t activity_frame;
} MatrixWindow;
static MatrixWindow matrix_window={.selected=-1,.rename=-1};
static void matrix_history_clear(void);
static int mx_same(MatrixRow a,MatrixRow b)
{return a.kind==b.kind && (a.kind==1 || a.kind==3?a.tile==b.tile:a.index==b.index);}
static int mx_chosen(int at)
{
    MatrixWindow *w=&matrix_window;
    for(int i=0;i<w->selection_count;++i)if(mx_same(w->rows[at],w->selection[i]))return 1;
    return !w->selection_count && at==w->selected;
}
static void mx_select(int at,int modifiers)
{
    MatrixWindow *w=&matrix_window;if(at<0 || at>=w->count)return;
    if(modifiers&KMOD_SHIFT) {
        if(!(modifiers&KMOD_CTRL))w->selection_count=0;
        int start=SDL_clamp(w->anchor,0,w->count-1);
        for(int i=SDL_min(start,at);i<=SDL_max(start,at);++i) {
            int found=0;for(int j=0;j<w->selection_count;++j)found|=mx_same(w->rows[i],w->selection[j]);
            if(!found)w->selection[w->selection_count++]=w->rows[i];
        }
    } else if(modifiers&KMOD_CTRL) {
        int i=0;for(;i<w->selection_count && !mx_same(w->rows[at],w->selection[i]);++i);
        if(i<w->selection_count)w->selection[i]=w->selection[--w->selection_count];
        else w->selection[w->selection_count++]=w->rows[at];
        w->anchor=at;
    } else {w->selection[0]=w->rows[at];w->selection_count=1;w->anchor=at;}
    w->selected=at;
}
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
/* Small, quiet meters: rapid attack with a 150 ms release. No timed blinking. */
static void mx_signal(int x,int y,int width,int height,float peak,uint32_t color)
{
    if(!isfinite(peak) || peak<=.0001f)return;
    float light=SDL_min(1.f,(20*log10f(peak)+80)/80);
    uint32_t ink=0xff000000u;
    for(int shift=0;shift<=16;shift+=8) {
        int from=(MX_PANEL>>shift)&255,to=(color>>shift)&255;
        ink|=(uint32_t)(from+(to-from)*light*.85f)<<shift;
    }
    mx_rect(x,y,width,height,ink);
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
        else snprintf(r->name,sizeof(r->name),"%s",i==TS_MATRIX_FM?"FM SYNTH":i==TS_MATRIX_MOSAIC?"MOSAIC BUS":"TAPEHEAD LIVE LINK");
    }
    if(!f || f==4)for(int i=0;i<TS_MATRIX_NODES;++i) {
        MatrixRow *r=&w->rows[w->count++];*r=(MatrixRow){.kind=2,.index=i};
        snprintf(r->name,sizeof(r->name),"%s%s",ts_matrix_node_name(i),i>=1 && i<=4?(w->controls.serial_mask&(1u<<i)?" MIX":" WET"):"");
    }
    if((!f || f==5) && w->mosaic)for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(w->mosaic->events[i].id) {
        MatrixRow *r=&w->rows[w->count++];*r=(MatrixRow){.kind=3,.index=i,.tile=w->mosaic->events[i].id};
        snprintf(r->name,sizeof(r->name),"CARD %03d %.12s",i+1,w->mosaic->events[i].name);
    }
    w->selected=-1;
    for(int i=0;i<w->count;++i)if(mx_same(w->rows[i],old))w->selected=i;
    if(w->selected<0 && w->count)w->selected=0;
    w->scroll=SDL_max(0,SDL_min(w->scroll,w->count-MATRIX_VISIBLE));
}
static TsSourceRoute mx_route(MatrixRow row,TsSamplePages *pages,TsInstrument *bank)
{
    if(row.kind==2)return matrix_window.controls.row[row.index];
    if(row.kind==3){TsMosaicEvent *e=ts_mosaic_find(matrix_window.mosaic,row.tile);return e?e->output_route:(TsSourceRoute){0};}
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
    matrix_history_clear();free(w->pixels);memset(w,0,sizeof(*w));w->selected=w->rename=-1;
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
    if(w->mosaic!=ui->mosaic || (ui->mosaic && w->epoch!=ui->mosaic->epoch))matrix_history_clear();
    w->mosaic=ui->mosaic;w->epoch=ui->mosaic?ui->mosaic->epoch:0;
    if(device)SDL_LockAudioDevice(device);
    w->controls=audio->sister.matrix.controls;w->legacy=audio->sister.router.controls;w->array=audio->sister.spatial.controls;
    w->activity_ready=0;w->activity_frame=audio->sister.processed_frames;
    memset(w->row_source,0,sizeof(w->row_source));memset(w->row_send,0,sizeof(w->row_send));
    memset(w->receiving,0,sizeof(w->receiving));
    if(device)SDL_UnlockAudioDevice(device);
    w->selection_count=0;w->filter=track>=0?1:slot>=0?2:0;w->scroll=0;mx_rows(pages,bank);
    for(int i=0;i<w->count;++i)if((track>=0 && w->rows[i].kind==0 && w->rows[i].index==track) ||
        (slot>=0 && w->rows[i].kind==1 && w->rows[i].tile==bank->bank[slot].tile_id)){w->selected=i;w->scroll=SDL_max(0,i-MATRIX_VISIBLE+1);}
    mx_select(w->selected,0);w->visible=w->dirty=1;w->rename=-1;w->status[0]=0;ui->router_open=0;
    source_route_hide();SDL_ShowWindow(w->window);SDL_RaiseWindow(w->window);return 1;
}
#include "main_sdl_matrix_history.inc"
static void mx_destination(char *name,size_t size,int d,int full)
{
    const char *shorts[]={"MAIN","SISTER","PRISM","PEDALS","FALLOUT","EQ","LIMITER"};
    if(d<TS_MATRIX_NODES)snprintf(name,size,"%s",full?ts_matrix_node_name(d):shorts[d]);
    else {int p=d-TS_MATRIX_NODES;snprintf(name,size,full?"OUT %d/%d %s":"OUT %d/%d",p*2+1,p*2+2,matrix_window.controls.output_name[p]);}
}
static void mx_paint(AudioState *audio,TsSamplePages *pages,TsInstrument *bank)
{
    (void)audio;
    MatrixWindow *w=&matrix_window;char label[160];mx_rect(0,0,MATRIX_W,MATRIX_H,MX_BG);
    mx_text(22,22,"ROUTING MATRIX",MX_GREEN,3);
    mx_button(300,20,90,"UNDO",w->history_cursor>0);mx_button(396,20,90,"REDO",w->history_cursor<w->history_count);
    mx_button(492,20,108,w->original_active?"EDITED":"ORIGINAL",w->original_active);mx_button(606,20,132,"RESTORE",0);
    mx_button(750,20,152,"CLEAR ALL",0);
    mx_button(914,20,240,"RESET TO DEFAULT",0);
    mx_text(22,57,"FROM A ROW > TO A COLUMN. ONE SHARED INSTANCE OF EACH PROCESSOR.",MX_DIM,2);
    const char *filters[]={"ALL","TRACKS","TILES","INPUTS","RETURNS","MOSAIC"};
    for(int i=0;i<6;++i)mx_button(22+i*110,88,104,filters[i],w->filter==i);
    if(w->original_active || w->controls.enabled)
        mx_text(700,95,w->original_active?"HEARING ORIGINAL":"MATRIX LIVE",MX_GREEN,w->original_active?1:2);
    else mx_button(690,88,174,"USE MATRIX",0);
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
        mx_signal(x+5,165,MATRIX_CW-12,3,w->receiving[d],MX_GREEN);
    }
    for(int line=0;line<MATRIX_VISIBLE && line+w->scroll<w->count;++line) {
        int at=line+w->scroll,y=MATRIX_Y+line*MATRIX_RH;MatrixRow row=w->rows[at];
        TsSourceRoute raw=mx_route(row,pages,bank),r=mx_display_route(raw);int inherit=raw.mode==TS_SOURCE_INHERIT || mx_legacy_direct(raw) || (row.kind==3 && !raw.matrix_enabled);
        mx_rect(20,y,1234,MATRIX_RH-2,mx_chosen(at)?0xff2a393bu:line&1?0xff1c262bu:0xff192227u);
        snprintf(label,sizeof(label),"%.19s",row.name);mx_text(28,y+6,label,mx_chosen(at)?MX_YELLOW:row.kind==2?MX_GREEN:MX_TEXT,2);
        if(inherit)mx_text(225,y+19,mx_legacy_direct(raw)?"OLD":row.kind==3?"BUS":"TILE",MX_YELLOW,1);
        mx_signal(21,y+5,4,17,w->row_source[line],MX_GREEN);
        if(row.kind==2) {
            int off=row.index>=TS_MATRIX_SISTER && row.index<=TS_MATRIX_FALLOUT && !(w->processors&(1u<<row.index));
            if(off)mx_text(225,y+19,"OFF",MX_YELLOW,1);
            if(row.index>=TS_MATRIX_NODES && row.index<TS_MATRIX_FM && (unsigned)(row.index-TS_MATRIX_NODES)*2>=w->inputs)mx_text(225,y+19,"N/A",MX_YELLOW,1);
        }
        for(int col=0;col<MATRIX_COLS && col+w->column<TS_MATRIX_DESTINATIONS;++col) {
            int d=col+w->column,x=MATRIX_X+col*MATRIX_CW,g=r.matrix_gain[d];
            int selected=mx_chosen(at)&&d==w->destination;
            int delayed=row.kind==2&&row.index<TS_MATRIX_NODES&&(w->controls.delayed[row.index]&(1u<<d));
            mx_rect(x,y,MATRIX_CW-2,MATRIX_RH-2,selected?0xff4a4532u:d==w->destination?0xff303733u:MX_PANEL);
            mx_rect(x+7,y+6,14,14,inherit?MX_DIM:g?(delayed?MX_YELLOW:MX_GREEN):0xff47605du);
            mx_rect(x+9,y+8,10,10,g&&!inherit?(delayed?MX_YELLOW:MX_GREEN):MX_BG);
            if(g&&!inherit){mx_text(x+12,y+9,"X",MX_BG,1);snprintf(label,sizeof(label),"%+.1f",20*log10f(g*.001f));}
            else snprintf(label,sizeof(label),"%s",inherit?(mx_legacy_direct(raw)?"OLD":row.kind==3?"BUS":"TILE"):"--");
            mx_text(x+28,y+6,label,g?MX_TEXT:MX_DIM,2);if(delayed)mx_text(x+79,y+9,"F",MX_YELLOW,1);
            if(g && !inherit && w->controls.enabled &&
               (d<TS_MATRIX_NODES || (unsigned)(d-TS_MATRIX_NODES)*2<w->outputs)) {
                int maximum=1;for(int dest=0;dest<TS_MATRIX_DESTINATIONS;++dest)maximum=SDL_max(maximum,r.matrix_gain[dest]);
                mx_signal(x+28,y+22,60,2,w->row_send[line]*g/maximum,delayed?MX_YELLOW:MX_GREEN);
            }
        }
    }
    int bottom=MATRIX_Y+MATRIX_VISIBLE*MATRIX_RH;
    snprintf(label,sizeof(label),"ROWS %d-%d / %d   CTRL/SHIFT+CLICK: SELECT ROWS | CLICK DESTINATION HEADER: ROUTE SELECTION",w->count?w->scroll+1:0,SDL_min(w->count,w->scroll+MATRIX_VISIBLE),w->count);
    mx_text(24,bottom+6,label,MX_DIM,1);mx_button(1062,bottom+1,90,"< OUT",0);mx_button(1162,bottom+1,90,"OUT >",0);
    mx_rect(20,692,1234,98,MX_PANEL);
    if(w->selected>=0 && w->selected<w->count) {
        MatrixRow row=w->rows[w->selected];TsSourceRoute raw=mx_route(row,pages,bank),r=mx_display_route(raw);
        char dest[60];mx_destination(dest,sizeof(dest),w->destination,1);
        snprintf(label,sizeof(label),mx_legacy_direct(raw)?"%.24s / LEGACY DIRECT - CONVERT ROW TO EDIT":"%.24s > %.38s",row.name,dest);mx_text(32,702,label,MX_YELLOW,2);
        int selected_count=0,mixed=0,g=r.matrix_gain[w->destination];
        for(int i=0;i<w->count;++i)if(mx_chosen(i)) {
            ++selected_count;TsSourceRoute other=mx_display_route(mx_route(w->rows[i],pages,bank));
            mixed|=other.matrix_gain[w->destination]!=g || other.pan!=r.pan;
        }
        if(selected_count>1){snprintf(label,sizeof(label),"%d SELECTED%s",selected_count,mixed?" / MIXED":"");mx_text(700,703,label,MX_GREEN,1);}
        snprintf(label,sizeof(label),"LEVEL %s",g?"":"OFF");
        if(g)snprintf(label,sizeof(label),"LEVEL %+.1f dB",20*log10f(g*.001f));mx_text(32,735,label,MX_TEXT,2);
        mx_rect(252,743,300,3,MX_DIM);int lx=252+(int)(g/2000.f*300);mx_rect(lx-3,735,6,19,MX_GREEN);
        snprintf(label,sizeof(label),"ROW PAN %s%d",r.pan<0?"L":r.pan>0?"R":"",abs(r.pan));mx_text(588,735,label,MX_TEXT,2);
        mx_rect(784,743,260,3,MX_DIM);mx_rect(913,734,2,20,MX_DIM);mx_rect(914,743,r.pan>0?r.pan*130/100:0,3,MX_YELLOW);
        if(r.pan<0)mx_rect(914+r.pan*130/100,743,-r.pan*130/100,3,MX_YELLOW);
        mx_rect(914+r.pan*130/100-3,735,6,19,MX_YELLOW);
        mx_button(1070,731,170,mx_legacy_direct(raw)?"USE MATRIX":row.kind==0?(raw.mode==TS_SOURCE_INHERIT?"USE TRACK":"USE TILE"):row.kind==3?(raw.matrix_enabled?"USE BUS":"CUSTOM ROUTE"):"NAME I/O",0);
        mx_text(32,769,"LEVEL/PAN: DRAG OR WHEEL | SHIFT: FINE | RIGHT-CLICK: 0 dB / CENTER | CELL RIGHT-CLICK: SELECT",MX_DIM,1);
        mx_button(1070,762,170,r.width==-100?"MONO":"STEREO",r.width==-100);
    }
    if(w->rename>=0) {
        mx_rect(252,730,792,51,0xff33443du);snprintf(label,sizeof(label),"NAME: %.23s",w->name);mx_text(266,738,label,MX_YELLOW,2);
        if ((Uint32)(SDL_GetTicks()-w->rename_blink_started)%1000u < 500u)
            mx_rect(266+(6+(int)strlen(w->name))*12,736,2,18,MX_YELLOW);
        mx_text(266,763,"ENTER: SAVE / ESC: CANCEL - CHANNEL NUMBERS STAY VISIBLE",MX_TEXT,1);
    }
    if(w->status[0])mx_text(24,804,w->status,MX_YELLOW,1);
    else if(!w->controls.enabled)mx_text(24,804,"SAVED ROUTING: USE MATRIX IMPORTS THE EXISTING EFFECT CHAIN. ORIGINAL/EDITED COMPARES; RESTORE REPLACES YOUR EDITS.",MX_YELLOW,1);
    else mx_text(24,804,"SIGNAL: ROW = SOURCE / COLUMN = RECEIVING / CELL = SENDING. N/A = PORT NOT OPEN. F9 / ESC CLOSE.",MX_DIM,1);
    mx_text(1180,25,"F9",MX_DIM,2);
}
static int mx_editable(void)
{
    if(!matrix_window.original_active)return 1;
    snprintf(matrix_window.status,sizeof(matrix_window.status),"AUDITIONING ORIGINAL. SELECT EDITED BEFORE MAKING CHANGES.");return 0;
}
static void mx_commit_controls(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui)
{
    if(!mx_editable()){matrix_window.controls=audio->sister.matrix.controls;return;}
    if(device)SDL_LockAudioDevice(device);
    ts_matrix_set(&audio->sister.matrix,&matrix_window.controls,(unsigned)audio->output_rate);
    ui->config.matrix=audio->sister.matrix.controls;
    if(device)SDL_UnlockAudioDevice(device);
    matrix_window.dirty=1;
}
static int mx_importable_source(TsSourceRoute r)
{return !r.matrix_enabled && !r.mix_enabled && (r.mode==TS_SOURCE_MAIN || r.mode==TS_SOURCE_INHERIT);}
static void mx_activate(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,TsSamplePages *pages,TsInstrument *bank)
{
    MatrixWindow *w=&matrix_window;
    if(w->controls.enabled || w->original_active)return;
    /* Do not claim a lossless conversion for source paths the importer does
       not represent yet. Validate the complete project, not just its filter. */
    int supported=audio->mixer.master_gain==1 && !audio->tune_reference_target && !audio->tune_reference_level &&
        !(audio->mixer.monitor_enabled && audio->external_monitor_enabled && atomic_load(audio->external_monitor_enabled));
    for(int i=0;i<TS_TRACKER_LANES;++i)supported&=mx_importable_source(pages->tracker.lanes[i].output_route);
    for(size_t p=0;p<ts_sample_pages_count(pages);++p) {
        const TsInstrument *page=ts_sample_pages_page(pages,bank,p);if(!page)continue;
        for(int i=0;i<TS_BANK_SLOT_COUNT;++i)if(page->bank[i].occupied)supported&=mx_importable_source(page->bank[i].output_route);
    }
    if(w->mosaic)for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(w->mosaic->events[i].id)supported&=!w->mosaic->events[i].output_route.matrix_enabled;
    if(!supported){snprintf(w->status,sizeof(w->status),"UNCHANGED: CUSTOM SOURCE / MONITOR ROUTING CANNOT YET BE IMPORTED WITHOUT CHANGING THE SOUND.");return;}
    TsMatrixControls next;
    if(device)SDL_LockAudioDevice(device);
    int ok=ts_sister_runtime_matrix_import(&audio->sister,&next,w->status,sizeof(w->status));
    if(ok) {
        ok=ts_matrix_set(&audio->sister.matrix,&next,(unsigned)audio->output_rate);
        if(ok)w->controls=ui->config.matrix=next;
    }
    if(device)SDL_UnlockAudioDevice(device);
    if(ok)snprintf(w->status,sizeof(w->status),"SAVED CHAIN IMPORTED: MIX ROWS KEEP THEIR DRY/WET SETTINGS. ORIGINAL/EDITED COMPARES; RESTORE REPLACES EDITS.");
    w->dirty=1;
}
static void mx_commit_route(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,TsSamplePages *pages,TsInstrument *bank,TsSourceRoute route)
{
    MatrixWindow *w=&matrix_window;if(w->selected<0 || w->selected>=w->count || !mx_editable())return;
    MatrixRow row=w->rows[w->selected];
    if(row.kind==2) {w->controls.row[row.index]=route;mx_commit_controls(device,audio,ui);return;}
    if(!ts_source_route_valid(&route,row.kind==0))return;
    if(row.kind==3) {
        if(device)SDL_LockAudioDevice(device);
        TsMosaicEvent *e=ts_mosaic_find(w->mosaic,row.tile);if(e){e->output_route=route;++w->mosaic->revision;}
        if(device)SDL_UnlockAudioDevice(device);w->dirty=1;return;
    }
    if(device)SDL_LockAudioDevice(device);
    if(row.kind==0)pages->tracker.lanes[row.index].output_route=route;
    else {TsBankSlot *slot=mx_tile(pages,bank,row.tile);if(slot){slot->output_route=route;source_route_update_voices(audio,row.tile,route);}}
    if(device)SDL_UnlockAudioDevice(device);
    ts_tapehead_routes_changed(row.kind==1?row.tile:0,route);w->dirty=1;
}
/* Validate every selected row against a staged graph before touching live state. */
static void mx_bulk(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,TsSamplePages *pages,TsInstrument *bank,int value,int pan)
{
    MatrixWindow *w=&matrix_window;TsMatrixControls next=w->controls;
    if(w->original_active){snprintf(w->status,sizeof(w->status),"AUDITIONING ORIGINAL. SELECT EDITED BEFORE MAKING CHANGES.");return;}
    if(!next.enabled){snprintf(w->status,sizeof(w->status),"SELECT USE MATRIX FIRST.");return;}
    int count=0;
    for(int i=0;i<w->count;++i)if(mx_chosen(i)) {
        TsSourceRoute raw=mx_route(w->rows[i],pages,bank);++count;
        if(raw.mode==TS_SOURCE_INHERIT || mx_legacy_direct(raw)) {
            snprintf(w->status,sizeof(w->status),"NO CHANGES: SELECTION INCLUDES INHERITED/LEGACY ROWS. CONVERT THOSE ROWS FIRST.");return;
        }
        if(w->rows[i].kind==2 && !pan && ts_matrix_connect(&next,w->rows[i].index,w->destination,SDL_clamp(value,0,2000))<=0) {
            snprintf(w->status,sizeof(w->status),"NO CHANGES: ROUTING WOULD CREATE A LOOP. REMOVE ITS RETURN OR ENABLE FEEDBACK.");return;
        }
    }
    if(!count)return;
    int selected=w->selected;
    if(device)SDL_LockAudioDevice(device);
    for(int i=0;i<w->count;++i)if(mx_chosen(i)) {
        MatrixRow row=w->rows[i];TsSourceRoute raw=mx_route(row,pages,bank),r=mx_display_route(raw);
        if(row.kind==3 && !raw.matrix_enabled)r=(TsSourceRoute){.matrix_enabled=1};
        if(pan)r.pan=SDL_clamp(value,-100,100);else r.matrix_gain[w->destination]=SDL_clamp(value,0,2000);
        if(row.kind==2){if(pan)next.row[row.index].pan=r.pan;}
        else if(row.kind==0)pages->tracker.lanes[row.index].output_route=r;
        else if(row.kind==1){TsBankSlot *slot=mx_tile(pages,bank,row.tile);if(slot){slot->output_route=r;source_route_update_voices(audio,row.tile,r);}}
        else {TsMosaicEvent *e=ts_mosaic_find(w->mosaic,row.tile);if(e)e->output_route=r;}
    }
    w->controls=next;ts_matrix_set(&audio->sister.matrix,&next,(unsigned)audio->output_rate);ui->config.matrix=next;
    if(device)SDL_UnlockAudioDevice(device);
    mx_refresh_tracker(pages,bank);w->selected=selected;w->dirty=1;
    snprintf(w->status,sizeof(w->status),"%d ROW%s UPDATED - CTRL+Z TO UNDO.",count,count==1?"":"S");
}
static void mx_gain(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,TsSamplePages *pages,TsInstrument *bank,int gain)
{mx_bulk(device,audio,ui,pages,bank,gain,0);}
static void mx_toggle(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,TsSamplePages *pages,TsInstrument *bank)
{
    MatrixWindow *w=&matrix_window;int all=1;
    for(int i=0;i<w->count;++i)if(mx_chosen(i)) {
        TsSourceRoute raw=mx_route(w->rows[i],pages,bank);
        all&=!(w->rows[i].kind==3 && !raw.matrix_enabled) && mx_display_route(raw).matrix_gain[w->destination]!=0;
    }
    mx_gain(device,audio,ui,pages,bank,all?0:1000);
}
/* These are project-wide actions, independent of the current row filter.
   Explicit source routes also replace inherited and legacy direct outputs. */
static void mx_reset_routes(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,
    TsSamplePages *pages,TsInstrument *bank,int defaults)
{
    MatrixWindow *w=&matrix_window;TsMatrixControls controls;
    if(!mx_editable())return;
    TsSourceRoute route={.matrix_enabled=1};
    if(defaults)route.matrix_gain[TS_MATRIX_MAIN]=1000;
    ts_matrix_default(&controls);controls.enabled=1;
    if(!defaults)for(int i=0;i<TS_MATRIX_ROWS;++i)controls.row[i]=route;
    if(device)SDL_LockAudioDevice(device);
    memcpy(controls.input_name,audio->sister.matrix.controls.input_name,sizeof(controls.input_name));
    memcpy(controls.output_name,audio->sister.matrix.controls.output_name,sizeof(controls.output_name));
    for(int lane=0;lane<TS_TRACKER_LANES;++lane)pages->tracker.lanes[lane].output_route=route;
    for(size_t p=0;p<ts_sample_pages_count(pages);++p) {
        TsInstrument *page=ts_sample_pages_page_mut(pages,bank,p);if(!page)continue;
        for(int slot=0;slot<TS_BANK_SLOT_COUNT;++slot)if(page->bank[slot].occupied)
            page->bank[slot].output_route=route;
    }
    if(w->mosaic)for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(w->mosaic->events[i].id)
        w->mosaic->events[i].output_route=defaults?(TsSourceRoute){0}:route;
    source_route_update_voices(audio,0,route);
    ts_matrix_set(&audio->sister.matrix,&controls,(unsigned)audio->output_rate);
    ui->config.matrix=w->controls=audio->sister.matrix.controls;
    if(device)SDL_UnlockAudioDevice(device);
    /* One cache refresh, not a score hash and host lock for every tile. */
    ts_tapehead_all_routes_changed(route);
    w->drag=0;w->dirty=1;
    snprintf(w->status,sizeof(w->status),defaults?
        "DEFAULT: ALL TILES/TRACKS + FM/LINK > MAIN > EQ > LIMITER > OUT 1/2. EXTERNAL INPUTS OFF. I/O NAMES KEPT.":
        "ALL CONNECTIONS CLEARED, INCLUDING HIDDEN TRACKS/TILES AND FEEDBACK. I/O NAMES KEPT. PATCH A NEW PATH TO HEAR AUDIO.");
}
static void mx_take_voice(TsSourceRouteVoice *voice,TsTileId tile,float gain)
{
    MatrixWindow *w=&matrix_window;
    if(tile && voice->activity_source>0 && w->activity_count<MATRIX_ACTIVITY_VOICES)
        w->activity[w->activity_count++]=(TsRouteActivity){tile,voice->activity_source*gain,voice->activity_send*gain};
    voice->activity_source=voice->activity_send=0;
}
/* Fixed-size copies and bounded voice reads under the existing exclusion.
   Matching rows, smoothing, logs and painting all happen after unlocking. */
static void mx_capture_activity(SDL_AudioDeviceID device,AudioState *audio,TsSamplePages *pages,TsInstrument *bank,Uint32 elapsed)
{
    MatrixWindow *w=&matrix_window;float destinations[TS_MATRIX_DESTINATIONS];
    MatrixRow old_rows[MATRIX_VISIBLE];float old_source[MATRIX_VISIBLE],old_send[MATRIX_VISIBLE];
    memcpy(old_rows,w->activity_rows,sizeof(old_rows));memcpy(old_source,w->row_source,sizeof(old_source));memcpy(old_send,w->row_send,sizeof(old_send));
    if(device)SDL_LockAudioDevice(device);
    w->controls=audio->sister.matrix.controls;w->legacy=audio->sister.router.controls;w->array=audio->sister.spatial.controls;memcpy(w->peaks,audio->sister.matrix.output_peak,sizeof(w->peaks));
    int fresh=w->activity_ready && audio->sister.processed_frames!=w->activity_frame;
    w->activity_frame=audio->sister.processed_frames;w->activity_ready=1;
    memcpy(destinations,audio->sister.matrix.input_peak,sizeof(audio->sister.matrix.input_peak));
    memcpy(destinations+TS_MATRIX_NODES,audio->sister.matrix.hardware_peak,sizeof(audio->sister.matrix.hardware_peak));
    memset(audio->sister.matrix.hardware_peak,0,sizeof(audio->sister.matrix.hardware_peak));
    for(int i=0;i<TS_MATRIX_ROWS;++i) {
        TsSourceRouteVoice *v=&audio->sister.matrix.voice[i];w->sends[i]=v->activity_send;
        v->activity_source=v->activity_send=0;
    }
    w->processors=(audio->sister.enabled?1u<<TS_MATRIX_SISTER:0)|
        (audio->sister.parameters.prism.enabled?1u<<TS_MATRIX_PRISM:0)|
        (audio->sister.parameters.fx.enabled?1u<<TS_MATRIX_BOARD:0)|
        (audio->sister.parameters.fx.enabled && audio->sister.parameters.fx.fallout.enabled?1u<<TS_MATRIX_FALLOUT:0);
    w->activity_count=0;
    float program=audio->mixer.program_gain;
    for(int i=0;i<TS_NOTE_BANK_VOICE_CAPACITY;++i)
        mx_take_voice(&audio->notes.voices[i].output_route,audio->notes.voices[i].route_tile,program*audio->notes.normalization_gain);
    TsPerformanceBank *voices[]={&audio->performance,&audio->tile_launchers,&audio->sister.performance};
    for(int b=0;b<3;++b) {
        for(int i=0;i<TS_PERFORMANCE_VOICE_LIMIT;++i) {
            TsRouteActivity value=voices[b]->activity[i];
            if(value.tile && value.source>0 && w->activity_count<MATRIX_ACTIVITY_VOICES) {
                value.source*=program;value.send*=program;w->activity[w->activity_count++]=value;
            }
        }
        memset(voices[b]->activity,0,sizeof(voices[b]->activity));
    }
    for(int i=0;i<TS_BANK_SLOT_COUNT;++i)
        mx_take_voice(&audio->keyboard_sequence.voices[i].output_route,audio->keyboard_sequence.voices[i].route_tile,program);
    mx_take_voice(&audio->audition_route,audio->audition_route_tile,program);
    ts_tapehead_take_route_activity(w->tracks,w->tiles);
    if(w->mosaic)for(int i=0;i<TS_MOSAIC_EVENTS;++i) {
        TsSourceRouteVoice *v=&w->mosaic->voices[i].output_route;
        w->card_source[i]=v->activity_source*program;w->card_send[i]=v->activity_send*program;
        v->activity_source=v->activity_send=0;
    }
    w->inputs=audio->sister.insert.duplex?atomic_load(&audio->sister.insert.input_channels):atomic_load(&audio->sister.insert.matrix_channels);
    w->outputs=audio->output_device_channels;
    if(device)SDL_UnlockAudioDevice(device);
    mx_rows(pages,bank);
    float release=expf(-(float)SDL_min(elapsed,1000u)/150.f);
    for(int d=0;d<TS_MATRIX_DESTINATIONS;++d) {
        float value=fresh && w->controls.enabled?destinations[d]:0;
        w->receiving[d]=w->controls.enabled?fmaxf(value,w->receiving[d]*release):0;
        if(d>=TS_MATRIX_NODES && (unsigned)(d-TS_MATRIX_NODES)*2>=w->outputs)w->receiving[d]=0;
    }
    for(int line=0;line<MATRIX_VISIBLE;++line) {
        int at=w->scroll+line;float source=0,send=0;MatrixRow row={.kind=-1};
        if(at<w->count) {
            row=w->rows[at];
            if(fresh && w->controls.enabled) {
                if(row.kind==2){source=w->peaks[row.index];send=w->sends[row.index];}
                else if(row.kind==3) {
                    source=w->card_source[row.index];send=w->card_send[row.index];
                }
                else if(row.kind==0){source=w->tracks[row.index].source*program;send=w->tracks[row.index].send*program;}
                else {
                    for(int i=0;i<w->activity_count;++i)if(w->activity[i].tile==row.tile) {
                        source=fmaxf(source,w->activity[i].source);send=fmaxf(send,w->activity[i].send);
                    }
                    for(int i=0;i<TS_TAPEHEAD_ROUTE_ACTIVITY_COUNT;++i)if(w->tiles[i].tile==row.tile) {
                        source=fmaxf(source,w->tiles[i].source*program);send=fmaxf(send,w->tiles[i].send*program);
                    }
                }
            }
            if(w->controls.enabled)for(int i=0;i<MATRIX_VISIBLE;++i)
                if(row.kind==old_rows[i].kind && row.index==old_rows[i].index && row.tile==old_rows[i].tile) {
                    source=fmaxf(source,old_source[i]*release);send=fmaxf(send,old_send[i]*release);break;
                }
        }
        w->activity_rows[line]=row;w->row_source[line]=source;w->row_send[line]=send;
    }
}
static void matrix_update(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,TsSamplePages *pages,TsInstrument *bank)
{
    MatrixWindow *w=&matrix_window;(void)ui;Uint32 elapsed=SDL_GetTicks()-w->presented;
    if(!w->visible || !window_drawable(w->window) || elapsed<50)return;
    mx_capture_activity(device,audio,pages,bank,elapsed);mx_paint(audio,pages,bank);
    SDL_UpdateTexture(w->texture,NULL,w->pixels,MATRIX_W*4);SDL_RenderClear(w->renderer);
    SDL_RenderCopy(w->renderer,w->texture,NULL,NULL);SDL_RenderPresent(w->renderer);w->presented=SDL_GetTicks();w->dirty=0;
}
static void mx_name(int port)
{
    MatrixWindow *w=&matrix_window;w->rename=port;w->rename_blink_started=SDL_GetTicks();
    snprintf(w->name,sizeof(w->name),"%s",port<TS_MATRIX_INPUTS?w->controls.input_name[port]:w->controls.output_name[port-TS_MATRIX_INPUTS]);
    SDL_StartTextInput();w->dirty=1;
}
static int matrix_event_inner(SDL_Event *e,SDL_Window *main,SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,TsSamplePages *pages,TsInstrument *bank)
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
        if(e->type==SDL_TEXTINPUT || (e->type==SDL_KEYDOWN && e->key.keysym.sym==SDLK_BACKSPACE))
            w->rename_blink_started=SDL_GetTicks();
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
    if(e->type==SDL_KEYDOWN && (e->key.keysym.mod&KMOD_CTRL) && (e->key.keysym.sym==SDLK_z || e->key.keysym.sym==SDLK_y)) {
        if(!e->key.repeat)mx_undo(device,audio,ui,pages,bank,e->key.keysym.sym==SDLK_y || (e->key.keysym.mod&KMOD_SHIFT));return 1;
    }
    if(e->type==SDL_KEYDOWN && e->key.keysym.sym==SDLK_ESCAPE){matrix_hide();return 1;}
    if(e->type==SDL_KEYDOWN || e->type==SDL_KEYUP){e->key.windowID=SDL_GetWindowID(main);return 0;}
    int x,y,rx,ry,ww,hh;SDL_GetWindowSize(w->window,&ww,&hh);
    if(e->type==SDL_MOUSEBUTTONDOWN || e->type==SDL_MOUSEBUTTONUP){rx=e->button.x;ry=e->button.y;}
    else if(e->type==SDL_MOUSEMOTION){rx=e->motion.x;ry=e->motion.y;}
    else if(e->type==SDL_MOUSEWHEEL)wheel_event_mouse(e,&rx,&ry);else return 1;
    x=rx*MATRIX_W/SDL_max(1,ww);y=ry*MATRIX_H/SDL_max(1,hh);w->dirty=1;
    int wheel=e->type==SDL_MOUSEWHEEL?wheel_event_detents(e,0):0;
    int fine=(SDL_GetModState()&KMOD_SHIFT)!=0;
    if(e->type==SDL_MOUSEBUTTONUP){w->drag=0;return 1;}
    int click=e->type==SDL_MOUSEBUTTONDOWN,left=click&&e->button.button==SDL_BUTTON_LEFT,middle=click&&e->button.button==SDL_BUTTON_MIDDLE;
    if(left && y>=20 && y<48) {
        if(x>=300 && x<486){mx_undo(device,audio,ui,pages,bank,x>=396);return 1;}
        if(x>=492 && x<600){mx_ab(device,audio,ui,pages,bank);return 1;}
        if(x>=606 && x<738){mx_restore(device,audio,ui,pages,bank);return 1;}
        if(x>=750 && x<902){mx_reset_routes(device,audio,ui,pages,bank,0);return 1;}
        if(x>=914 && x<1154){mx_reset_routes(device,audio,ui,pages,bank,1);return 1;}
    }
    if(left && y>=88 && y<116) {
        int f=x>=22 && x<682?(x-22)/110:-1;
        if(f>=0){w->selection_count=0;w->filter=f;w->scroll=0;mx_rows(pages,bank);return 1;}
        if(x>=690 && x<864) {
            mx_activate(device,audio,ui,pages,bank);return 1;
        }
        if(x>=876 && x<1050){ts_matrix_cut_feedback(&w->controls);mx_commit_controls(device,audio,ui);return 1;}
        if(x>=1062){if(w->controls.feedback)ts_matrix_cut_feedback(&w->controls);else w->controls.feedback=1;mx_commit_controls(device,audio,ui);return 1;}
    }
    if(click && y>=128 && y<MATRIX_Y && x>=MATRIX_X) {
        int d=(x-MATRIX_X)/MATRIX_CW+w->column;
        if(d<TS_MATRIX_DESTINATIONS){w->destination=d;if(left && w->selection_count>1)mx_toggle(device,audio,ui,pages,bank);if(middle && d>=TS_MATRIX_NODES)mx_name(TS_MATRIX_INPUTS+d-TS_MATRIX_NODES);}return 1;
    }
    int at=(y-MATRIX_Y)/MATRIX_RH+w->scroll;
    if(y>=MATRIX_Y && y<MATRIX_Y+MATRIX_RH*MATRIX_VISIBLE) {
        if(e->type==SDL_MOUSEWHEEL) {
            if((SDL_GetModState()&KMOD_CTRL) && x>=MATRIX_X && at<w->count) {
                if(!mx_chosen(at))mx_select(at,0);w->selected=at;w->destination=SDL_min(TS_MATRIX_DESTINATIONS-1,(x-MATRIX_X)/MATRIX_CW+w->column);
                TsSourceRoute r=mx_display_route(mx_route(w->rows[at],pages,bank));int g=r.matrix_gain[w->destination];
                if(g && wheel)mx_gain(device,audio,ui,pages,bank,(int)lrintf(fminf(2000.f,g*powf(10,wheel*(fine?.005f:.05f)))));
            } else w->scroll=SDL_max(0,SDL_min(w->count-MATRIX_VISIBLE,w->scroll-wheel*3));return 1;
        }
        if(click && at<w->count) {
            if(x<MATRIX_X)mx_select(at,SDL_GetModState());else if(!mx_chosen(at))mx_select(at,0);
            w->selected=at;w->status[0]=0;MatrixRow row=w->rows[at];
            if(x<MATRIX_X) {if(middle && row.kind==2 && row.index>=TS_MATRIX_NODES && row.index<TS_MATRIX_FM)mx_name(row.index-TS_MATRIX_NODES);}
            else {int d=(x-MATRIX_X)/MATRIX_CW+w->column;if(d<TS_MATRIX_DESTINATIONS){w->destination=d;TsSourceRoute r=mx_display_route(mx_route(row,pages,bank));if(left)mx_toggle(device,audio,ui,pages,bank);}}
        }return 1;
    }
    if(left && y>=653 && y<683 && x>=1062){w->column=SDL_max(0,SDL_min(TS_MATRIX_DESTINATIONS-MATRIX_COLS,w->column+(x<1152?-1:1)));return 1;}
    if(w->selected<0 || w->selected>=w->count)return 1;
    MatrixRow row=w->rows[w->selected];TsSourceRoute raw=mx_route(row,pages,bank),r=mx_display_route(raw);
    if(left && x>=1070 && y>=731 && y<759) {
        if(mx_legacy_direct(raw)){mx_commit_route(device,audio,ui,pages,bank,(TsSourceRoute){0});return 1;}
        if(row.kind==3){raw=raw.matrix_enabled?(TsSourceRoute){0}:(TsSourceRoute){.matrix_enabled=1};mx_commit_route(device,audio,ui,pages,bank,raw);}
        else if(row.kind==0) {if(raw.mode==TS_SOURCE_INHERIT)raw=(TsSourceRoute){0};else raw.mode=TS_SOURCE_INHERIT;mx_commit_route(device,audio,ui,pages,bank,raw);}
        else if(row.kind==2 && row.index>=TS_MATRIX_NODES && row.index<TS_MATRIX_FM)mx_name(row.index-TS_MATRIX_NODES);
        else if(w->destination>=TS_MATRIX_NODES)mx_name(TS_MATRIX_INPUTS+w->destination-TS_MATRIX_NODES);
        else snprintf(w->status,sizeof(w->status),"SELECT AN INPUT ROW OR OUTPUT COLUMN TO NAME ITS HARDWARE.");return 1;
    }
    if(left && x>=1070 && y>=762 && y<790 && raw.mode!=TS_SOURCE_INHERIT && !mx_legacy_direct(raw) && w->controls.enabled) {
        r.width=r.width==-100?0:-100;mx_commit_route(device,audio,ui,pages,bank,r);return 1;
    }
    if(y>=730 && y<760) {
        int level=x>=32 && x<=552,pan=x>=588 && x<=1044;
        if(e->type==SDL_MOUSEWHEEL && (level || pan)) {
            if(wheel && level) {
                int g=r.matrix_gain[w->destination];
                if(g || wheel>0)mx_gain(device,audio,ui,pages,bank,g?
                    (int)lrintf(fminf(2000.f,g*powf(10,wheel*(fine?.005f:.05f)))):1000);
            } else if(wheel && raw.mode!=TS_SOURCE_INHERIT && !mx_legacy_direct(raw) && w->controls.enabled) {
                mx_bulk(device,audio,ui,pages,bank,SDL_clamp(r.pan+wheel*(fine?1:5),-100,100),1);
            }
            return 1;
        }
        if(click && e->button.button==SDL_BUTTON_RIGHT && (level || pan)) {
            w->drag=0;
            if(level)mx_gain(device,audio,ui,pages,bank,1000);
            else if(raw.mode!=TS_SOURCE_INHERIT && !mx_legacy_direct(raw) && w->controls.enabled) {
                mx_bulk(device,audio,ui,pages,bank,0,1);
            }
            return 1;
        }
        if(left && x>=252 && x<=552)w->drag=1;
        if(left && x>=784 && x<=1044)w->drag=2;
    }
    if(w->drag && (click || e->type==SDL_MOUSEMOTION)) {
        if(w->drag==1)mx_gain(device,audio,ui,pages,bank,(x-252)*2000/300);
        else if(raw.mode!=TS_SOURCE_INHERIT && !mx_legacy_direct(raw) && w->controls.enabled) {
            r.pan=SDL_max(-100,SDL_min(100,(x-914)*100/130));
            mx_bulk(device,audio,ui,pages,bank,r.pan,1);
        }
    }
    return 1;
}
/* One pointer gesture is one transaction. Unchanged clicks never enter history. */
static int matrix_event(SDL_Event *e,SDL_Window *main,SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,TsSamplePages *pages,TsInstrument *bank)
{
    MatrixWindow *w=&matrix_window;
    if(w->mosaic!=ui->mosaic || (ui->mosaic && w->epoch!=ui->mosaic->epoch))matrix_history_clear();
    w->mosaic=ui->mosaic;w->epoch=ui->mosaic?ui->mosaic->epoch:0;
    int own=w->visible && event_window_id(e)==w->id;
    int edit=own && (e->type==SDL_MOUSEBUTTONDOWN || e->type==SDL_MOUSEWHEEL ||
        (w->rename>=0 && e->type==SDL_KEYDOWN && e->key.keysym.sym==SDLK_RETURN));
    if(edit && !w->pending){mx_original(device,audio,pages,bank);w->pending=mx_snapshot(device,audio,pages,bank);}
    w->history_skip=0;
    if(edit && !w->pending){snprintf(w->status,sizeof(w->status),"EDIT NOT APPLIED: CANNOT ALLOCATE UNDO SNAPSHOT.");return 1;}
    int original=w->original_active;
    int result=matrix_event_inner(e,main,device,audio,ui,pages,bank);
    if(w->pending && (!w->drag || e->type==SDL_MOUSEBUTTONUP)) {
        MatrixSnapshot *after=mx_snapshot(device,audio,pages,bank);
        if(!after && !w->history_skip) {
            mx_apply(w->pending,device,audio,ui,pages,bank);w->history_skip=1;
            snprintf(w->status,sizeof(w->status),"EDIT NOT APPLIED: CANNOT ALLOCATE UNDO SNAPSHOT.");
        }
        if(original && !w->history_skip && after && !mx_equal(w->pending,after)) {
            mx_apply(w->pending,device,audio,ui,pages,bank);w->history_skip=1;
            snprintf(w->status,sizeof(w->status),"AUDITIONING ORIGINAL. SELECT EDITED BEFORE MAKING CHANGES.");
        }
        if(!w->history_skip && after && !mx_equal(w->pending,after))mx_push(w->pending);
        else free(w->pending);
        free(after);w->pending=NULL;
    }
    return result;
}
#undef MX_BG
#undef MX_PANEL
#undef MX_TEXT
#undef MX_DIM
#undef MX_GREEN
#undef MX_YELLOW
