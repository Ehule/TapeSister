#include "tapesister/movement_name.h"
static void movement_navigator_open(TsUiState *,TsSamplePages *);
static void begin_mosaic_layout(TsUiState *);
/* A spatial view of the Matrix's exact routes. Only the existing Matrix
   transactions may change audio; positions, pan and zoom are view metadata. */
enum { FX_GRID=32, FX_NODE_W=210, FX_NODE_H=86, FX_RIGHT=1000, FX_DRAG_NODE=100, FX_DRAG_WIRE, FX_DRAG_PAN, FX_DRAG_BOX, FX_DRAG_GRAB, FX_DRAG_SCALE, FX_DRAG_SHIFT_NODE, FX_DRAG_WIRE_BOX, FX_DRAG_DIVIDER, FX_DRAG_CONTROL=120, FX_DRAG_MACRO=140, FX_DRAG_LABEL=151, FX_DRAG_LABEL_GRAB, FX_DRAG_LABEL_SCALE, FX_DRAG_GROUP };
typedef struct { MatrixRow row; int at,destination,rank,outside_filter; float x,y,scale,level; } FxCanvasNode;
typedef struct { MatrixRow source; int destination; uint32_t target_id; } FxCanvasWire;
typedef struct { FxCanvasWire *items; int count,capacity; } FxWireList;
static struct {
    FxCanvasNode node[MATRIX_CAPACITY+TS_MATRIX_DESTINATIONS];
    int label_selected,label_editing,label_target,label_cursor,label_all;
    Uint32 text_blink;int text_phase,confirm_delete;uint32_t replace_id;
    int properties,properties_count;MatrixRow properties_rows[MATRIX_CAPACITY+TS_MATRIX_DESTINATIONS];FxWireList properties_wires;MatrixRow properties_row;char properties_name[64];uint32_t properties_color;int properties_cursor,properties_all;
    char label_text[TS_FX_LABEL_TEXT];
    TsFxCanvasLabel label_origin,label_move[TS_FX_LABEL_LIMIT+TS_FX_HEADING_LIMIT];
    unsigned char labels[TS_FX_LABEL_LIMIT+TS_FX_HEADING_LIMIT],before_labels[TS_FX_LABEL_LIMIT+TS_FX_HEADING_LIMIT],moving_labels[TS_FX_LABEL_LIMIT+TS_FX_HEADING_LIMIT];
    int count,dest[TS_FX_DESTINATIONS],fit,wire,destination,mouse_x,mouse_y;
    MatrixRow source,moving;
    float origin_x,origin_y,pointer_x,pointer_y;
    uint32_t meta;
    TsFxCanvasPosition move[TS_FX_CANVAS_POSITIONS];int move_count;
    MatrixRow before[MATRIX_CAPACITY+TS_MATRIX_DESTINATIONS];int before_count;
    int popup,popup_x,popup_y,popup_category,popup_scroll,popup_selected,confirm_clear;
    int context_x,context_y,context_has_card;MatrixRow context_card;
    int inspector_page,inspector_drag;uint64_t inspector_key;float place_x,place_y;
    FxWireList wires,before_wires;
    SDL_Cursor *divider_cursor;int divider_hover,divider_start,divider_pointer;
    int bundles,focus_count;MatrixRow focus_rows[MATRIX_CAPACITY+TS_MATRIX_DESTINATIONS];
    int help;uint32_t group_selected,properties_group;float properties_opacity;
    int properties_flow_tab,properties_flow_only;
    uint32_t properties_flow_color;
    uint32_t revealed[TS_MATRIX_EFFECT_CAPACITY];
    int box_add,shift_moved;float gesture_zoom;uint64_t movement;int movement_popup,active_view;
} fx_canvas={.wire=-1,.destination=-1,.label_selected=-1};
#define FX_TOP (fx_canvas.meta?116:96)
#define FX_BOTTOM (fx_canvas.meta?682:MATRIX_H-(fx_canvas.help?144:48))
enum { FX_TOOL_MATRIX,FX_TOOL_UNDO,FX_TOOL_REDO,FX_TOOL_ADD,FX_TOOL_FIT,
       FX_TOOL_CLOSE,FX_TOOL_HELP,FX_TOOL_EDITOR,FX_TOOL_MOVEMENT,
       FX_TOOL_CURVES,FX_TOOL_SNAP,FX_TOOL_CLEAR,FX_TOOL_CPU,FX_TOOL_NAVIGATOR,FX_TOOL_PROJECT,FX_TOOL_LAYOUT,FX_TOOL_FLOW,FX_TOOL_COUNT };
static TsUiRect fx_toolbar_rect(int tool)
{
    static const char *labels[]={"MATRIX F9","UNDO","REDO","+ EFFECT","FIT","MOSAIC","HELP","EDITOR"};
    if(tool<FX_TOOL_MOVEMENT) {
        int x=220;
        for(int i=0;i<tool;++i)x+=ts_ui_control_width(labels[i],2,0)+TS_UI_CONTROL_GAP;
        return ts_ui_control_rect(x,16,labels[tool],2,0);
    }
    if(tool==FX_TOOL_MOVEMENT)return (TsUiRect){20,56,300,TS_UI_TOOLBAR_HEIGHT};
    if(tool==FX_TOOL_CURVES)return ts_ui_control_rect(326,56,"STRAIGHT",2,0);
    if(tool==FX_TOOL_SNAP)return ts_ui_control_rect(448,56,"SNAP OFF",2,0);
    if(tool==FX_TOOL_CLEAR)return ts_ui_control_rect(570,56,"CLEAR CABLES",2,0);
    if(tool==FX_TOOL_NAVIGATOR)return ts_ui_control_rect(744,56,"MOVEMENTS",2,0);
    if(tool==FX_TOOL_PROJECT)return ts_ui_control_rect(880,56,"PROJECT",2,0);
    if(tool==FX_TOOL_LAYOUT)return ts_ui_control_rect(992,56,"VIEW SIZES",2,0);
    if(tool==FX_TOOL_FLOW)return (TsUiRect){1132,56,128,TS_UI_TOOLBAR_HEIGHT};
    return (TsUiRect){MATRIX_W-200,MATRIX_H-36,180,TS_UI_TOOLBAR_HEIGHT};
}
static int fx_toolbar_hit(int tool,int x,int y)
{return ts_ui_rect_contains(fx_toolbar_rect(tool),x,y);}
static void fx_divider_cursor(int active)
{
    if(active && !fx_canvas.divider_cursor)fx_canvas.divider_cursor=SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_SIZEWE);
    if(active && fx_canvas.divider_cursor)SDL_SetCursor(fx_canvas.divider_cursor);
    else if(fx_canvas.divider_hover)SDL_SetCursor(SDL_GetDefaultCursor());
    fx_canvas.divider_hover=active;
}
static void fx_navigation_free(void);
static void fx_context_reset(void)
{fx_navigation_free();fx_properties_close();memset(fx_canvas.labels,0,sizeof(fx_canvas.labels));fx_canvas.label_selected=-1;fx_canvas.label_editing=fx_canvas.properties=fx_canvas.confirm_delete=0;fx_canvas.group_selected=fx_canvas.properties_group=0;fx_canvas.replace_id=0;fx_divider_cursor(0);if(fx_canvas.divider_cursor)SDL_FreeCursor(fx_canvas.divider_cursor);fx_canvas.divider_cursor=NULL;svfx_close();fx_canvas.meta=0;fx_canvas.wire=fx_canvas.destination=-1;fx_canvas.fit=fx_canvas.popup=fx_canvas.confirm_clear=0;fx_canvas.inspector_key=0;fx_canvas.movement=0;fx_canvas.movement_popup=0;fx_canvas.active_view=0;fx_canvas.focus_count=0;fx_canvas.bundles=0;
 memset(fx_canvas.revealed,0,sizeof(fx_canvas.revealed));
 free(fx_canvas.properties_wires.items);fx_canvas.properties_wires=(FxWireList){0};
 free(fx_canvas.wires.items);free(fx_canvas.before_wires.items);fx_canvas.wires=(FxWireList){0};fx_canvas.before_wires=(FxWireList){0};}
static int fx_source_at(MatrixRow key)
{
    for(int i=0;i<matrix_window.count;++i)if(mx_same(matrix_window.rows[i],key))return i;
    return -1;
}
static TsMatrixEffectControls *fx_meta(void)
{
    if(fx_canvas.meta)for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)
        if(matrix_window.controls.effect[i].id==fx_canvas.meta &&
           matrix_window.controls.effect[i].type==TS_MATRIX_FX_META)return &matrix_window.controls.effect[i];
    fx_canvas.meta=0;return NULL;
}
static TsFxCanvasPosition *fx_position_source(TsFxCanvasPosition *p,MatrixRow row)
{
    TsMosaicEvent *e=row.kind==3?mx_card(row):NULL;
    if(e){snprintf(p->name,sizeof(p->name),"%s",e->name);p->color=e->color;}
    return p;
}
static TsFxCanvasPosition *fx_position(MatrixRow row,int create)
{
    TsMatrixLayout *l=matrix_window.layout;int kind=row.kind;uint64_t id=mx_row_id(row);
    if(kind==5)id=(uint64_t)row.index;
    for(int i=0;i<l->canvas_count;++i)if(l->canvas[i].kind==kind && l->canvas[i].id==id && l->canvas[i].canvas==row.canvas)return fx_position_source(&l->canvas[i],row);
    /* Adopt legacy unqualified card positions once. Same-numbered cards in
       another movement get their own positions, rather than stealing this one. */
    if(kind==3 && row.canvas)for(int i=0;i<l->canvas_count;++i)
        if(l->canvas[i].kind==kind && l->canvas[i].id==id && !l->canvas[i].canvas) {
            l->canvas[i].canvas=row.canvas;return fx_position_source(&l->canvas[i],row);
        }
    if(!create)return NULL;
    if(kind==0 || kind==1 || kind==3) {
        int sources=0;
        for(int i=0;i<l->canvas_count;++i) {
            int k=l->canvas[i].kind;
            if(k==0 || k==1 || k==3)++sources;
        }
        if(sources>=TS_FX_CANVAS_SOURCE_POSITIONS)return NULL;
    }
    if(l->canvas_count==TS_FX_CANVAS_POSITIONS) {
        /* Reclaim positions of deleted instances before reusing a view slot. */
        for(int i=l->canvas_count-1;i>=0;--i)if(l->canvas[i].kind==4) {
            int alive=0;for(int j=0;j<TS_MATRIX_EFFECT_CAPACITY;++j)alive|=matrix_window.controls.effect[j].id==l->canvas[i].id;
            if(!alive) {
                l->canvas[i]=l->canvas[--l->canvas_count];
                memset(&l->canvas[l->canvas_count],0,sizeof(l->canvas[0]));
            }
        }
    }
    if(l->canvas_count==TS_FX_CANVAS_POSITIONS)return NULL;
    TsFxCanvasPosition *p=&l->canvas[l->canvas_count++];memset(p,0,sizeof(*p));p->kind=kind;p->id=id;p->canvas=row.canvas;return fx_position_source(p,row);
}
/* A newly visible module can enter a column already occupied by a hand-placed
   node. Find an open spot once, then save it with the other world positions. */
static void fx_store_initial_position(TsFxCanvasPosition *position,float x,float y)
{
    TsMatrixLayout *layout=matrix_window.layout;
    if(layout->canvas_snap){x=roundf(x/FX_GRID)*FX_GRID;y=roundf(y/FX_GRID)*FX_GRID;}
    for(int i=0;i<layout->canvas_count;) {
        TsFxCanvasPosition *other=&layout->canvas[i++];
        float scale=other->scale?other->scale:1;
        if(other==position || x+FX_NODE_W+12<=other->x || x>=other->x+FX_NODE_W*scale+12 ||
           y+FX_NODE_H+12<=other->y || y>=other->y+FX_NODE_H*scale+12)continue;
        y+=layout->canvas_snap?128.f:120.f;
        i=0;
    }
    position->x=x;
    position->y=y;
}

static int fx_row_playing(MatrixRow row)
{
    MatrixWindow *w=&matrix_window;
    if(row.kind==3 && row.index>=0 && row.index<TS_MOSAIC_EVENTS) {
        TsMosaic *m=mx_card_scene(row);
        for(int i=0;m && i<2;++i)if(w->playing[i].canvas==row.canvas && w->playing[i].epoch==m->epoch && w->playing[i].card[row.index]==row.tile)return 1;
    }
    if(row.kind==1)for(int i=0;i<w->activity_count;++i)if(w->activity[i].tile==row.tile && w->activity[i].source>0)return 1;
    if(row.kind==0 && row.index>=0 && row.index<TS_TRACKER_LANES)return w->tracks[row.index].source>0;
    return 0;
}
static int fx_filter_source(MatrixRow row)
{return fx_canvas.active_view?fx_row_playing(row):row.kind==3 && row.canvas==fx_canvas.movement;}
static float fx_row_level(MatrixRow row)
{
    MatrixWindow *w=&matrix_window;
    if(row.kind==3 && row.index>=0 && row.index<TS_MOSAIC_EVENTS) {
        TsMosaic *m=mx_card_scene(row);
        for(int i=0;m && i<2;++i) {
            const MatrixPlayingCards *p=&w->playing[i];
            if(p->canvas==row.canvas && p->epoch==m->epoch && p->signal_card[row.index]==row.tile)
                return p->peak[row.index];
        }
    } else if(row.kind==0 && row.index>=0 && row.index<TS_TRACKER_LANES) {
        return w->tracks[row.index].source*w->activity_program*w->activity_tracker_gain;
    } else if(row.kind==1) {
        float peak=0;
        for(int i=0;i<w->activity_count;++i)if(w->activity[i].tile==row.tile)
            peak=fmaxf(peak,w->activity[i].source);
        for(int i=0;i<TS_TAPEHEAD_ROUTE_ACTIVITY_COUNT;++i)if(w->tiles[i].tile==row.tile)
            peak=fmaxf(peak,w->tiles[i].source*w->activity_program);
        return peak;
    } else if(row.kind==2 || row.kind==4)return w->peaks[row.index];
    return 0;
}
/* This mask is UI-only reachability. Feedback is bounded by the finite set
   of destinations; it never drives DSP activation or transport. */
static int fx_movement_destinations(TsSamplePages *pages,TsInstrument *bank,unsigned char *keep)
{
    MatrixWindow *w=&matrix_window;int bus=0;memset(keep,0,TS_MATRIX_DESTINATIONS);
    for(int i=0;i<w->count;++i)if(fx_filter_source(w->rows[i])) {
        TsSourceRoute raw=mx_route(w->rows[i],pages,bank);
        if(!raw.matrix_enabled) {
            int root=w->rows[i].kind==3?TS_MATRIX_MOSAIC:w->rows[i].kind==0?TS_MATRIX_TRACKER:TS_MATRIX_TILES;
            bus=1;keep[root]=1;
            for(int d=0;d<TS_MATRIX_DESTINATIONS;++d)if(w->controls.row[root].matrix_gain[d])keep[d]=1;
            continue;
        }
        TsSourceRoute route=mx_display_route(raw);
        for(int d=0;d<TS_MATRIX_DESTINATIONS;++d)if(route.matrix_gain[d])keep[d]=1;
    }
    for(int pass=0;pass<TS_MATRIX_PROCESSORS;++pass) {
        int changed=0;
        for(int k=0;k<TS_MATRIX_PROCESSORS;++k) {
            int n=ts_matrix_processor(k),sc=ts_matrix_sidechain_for(n);
            if(!keep[n] && !(sc>=0 && keep[sc]))continue;
            if(!keep[n]){keep[n]=1;changed=1;}
            for(int d=0;d<TS_MATRIX_DESTINATIONS;++d)if(w->controls.row[n].matrix_gain[d] && !keep[d]){keep[d]=1;changed=1;}
        }
        if(!changed)break;
    }
    return bus;
}
#include "main_sdl_fx_canvas_headings.inc"
#include "main_sdl_fx_canvas_cache.inc"
static void fx_build(TsSamplePages *pages,TsInstrument *bank)
{
    MatrixWindow *w=&matrix_window;int wanted[TS_MATRIX_DESTINATIONS]={0};
    if(w->filter){w->filter=0;mx_rows(pages,bank);}
    if(fx_canvas.movement) {
        int alive=0;for(int c=0;c<mx_scene_count();++c)alive|=mx_scene_id(c)==fx_canvas.movement;
        if(!alive)fx_canvas.movement=0;
    }
    if(!fx_graph_changed(pages,bank))return;
    ++fx_graph_cache.builds;w->dirty=1;
    fx_canvas.count=0;for(int d=0;d<TS_FX_DESTINATIONS;++d)fx_canvas.dest[d]=-1;
    int show_tracker_sends=0;
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)show_tracker_sends|=w->controls.effect[i].type==TS_MATRIX_FX_RECALL;
    /* Sources are endpoints, not copies of sample/generator editors. Ordinary
       tile/card/track membership remains represented by its source bus. */
    for(int i=0;i<w->count;++i) {
        MatrixRow row=w->rows[i];TsSourceRoute raw=mx_route(row,pages,bank);
        int processor=(row.kind==2 || row.kind==4) && ts_matrix_is_processor(row.index);
        if(processor)continue;
        int keep=row.kind==2?(row.index>=TS_MATRIX_FM && row.index<=TS_MATRIX_TRACKER):
            row.kind==3 || raw.matrix_enabled || raw.mix_enabled;
        if(row.kind==2 && ts_matrix_input_pair(row.index)>=0) {
            keep=1; /* mx_rows already enumerated the physical/named input pairs. */
        }
        /* A new send must be patchable before a track owns any explicit audio routes. */
        int recall_track=show_tracker_sends && row.kind==0;
        if(!recall_track && (!keep || (row.kind!=3 && raw.mode==TS_SOURCE_INHERIT && !raw.matrix_enabled)))continue;
        FxCanvasNode *n=&fx_canvas.node[fx_canvas.count++];*n=(FxCanvasNode){.row=row,.at=i,.destination=-1,.scale=1};
        TsSourceRoute route=mx_display_route(raw);
        for(int d=0;d<TS_MATRIX_DESTINATIONS;++d)wanted[d]|=route.matrix_gain[d]!=0;
    }
    for(int i=0;i<fx_canvas.count;++i)if(fx_canvas.node[i].row.kind==2 && fx_canvas.node[i].row.index==TS_MATRIX_MOSAIC) {
        FxCanvasNode first=fx_canvas.node[0];fx_canvas.node[0]=fx_canvas.node[i];fx_canvas.node[i]=first;break;
    }
    for(int i=0;i<w->count;++i) {
        MatrixRow row=w->rows[i];
        if((row.kind!=2 && row.kind!=4) || !ts_matrix_is_processor(row.index))continue;
        /* Membership comes from the Matrix inventory, even for isolated chains. */
        for(int d=0;d<TS_MATRIX_DESTINATIONS;++d)
            if(w->controls.row[row.index].matrix_gain[d])wanted[d]=1;
        FxCanvasNode *n=&fx_canvas.node[fx_canvas.count];*n=(FxCanvasNode){.row=row,.at=i,.destination=row.index,.scale=1};
        fx_canvas.dest[row.index]=fx_canvas.count++;
        int sc=ts_matrix_sidechain_for(row.index);if(sc>=0 && ts_matrix_destination_active(&w->controls,sc))fx_canvas.dest[sc]=fx_canvas.count-1;
    }
    for(int d=0;d<TS_MATRIX_DESTINATIONS;++d) {
        if(fx_canvas.dest[d]>=0 || !ts_matrix_destination_active(&w->controls,d))continue;
        if(!ts_matrix_is_output(d) && d<TS_MATRIX_AMBISONICS)continue;
        MatrixRow sink={.kind=5,.index=d};
        int pair=ts_matrix_output_pair(d);
        int named=pair>=0 && w->controls.output_name[pair][0];
        if(!wanted[d] && !named && d!=TS_MATRIX_NODES && !mx_destination_available(d) && !fx_position(sink,0))continue;
        FxCanvasNode *n=&fx_canvas.node[fx_canvas.count];
        *n=(FxCanvasNode){.row={.kind=5,.index=d},.at=-1,.destination=d,.scale=1};
        mx_destination(n->row.name,sizeof(n->row.name),d,1);fx_canvas.dest[d]=fx_canvas.count++;
    }
    /* Seed a new patch from its signal flow once. Keep each module's position
       in the project thereafter, including when the last incident wire goes. */
    int arrange=0;
    for(int i=0;i<fx_canvas.count;++i) {
        FxCanvasNode *n=&fx_canvas.node[i];
        if(n->row.kind<2 || n->row.kind==3)continue;
        if(!fx_position(n->row,0)){arrange=1;break;}
    }
    for(int pass=0;arrange && pass<TS_MATRIX_PROCESSORS+1;++pass) {
        int changed=0;
        for(int i=0;i<fx_canvas.count;++i) {
            FxCanvasNode *n=&fx_canvas.node[i];if(n->at<0)continue;
            TsSourceRoute route=mx_display_route(mx_route(n->row,pages,bank));
            for(int d=0;d<TS_MATRIX_DESTINATIONS;++d)if(route.matrix_gain[d] && fx_canvas.dest[d]>=0) {
                if((n->row.kind==2 || n->row.kind==4) && (w->controls.delayed[n->row.index]&ts_matrix_feedback_bit(d)))continue;
                FxCanvasNode *to=&fx_canvas.node[fx_canvas.dest[d]];
                int rank=SDL_min(TS_MATRIX_PROCESSORS+1,n->rank+1);
                if(to->rank<rank){to->rank=rank;changed=1;}
            }
        }
        if(!changed)break;
    }
    int rows[TS_MATRIX_PROCESSORS+3]={0},source_row=0;
    for(int i=0;i<fx_canvas.count;++i) {
        FxCanvasNode *n=&fx_canvas.node[i];
        if(n->row.kind==0 || n->row.kind==1 || n->row.kind==3) {
            TsFxCanvasPosition *saved=fx_position(n->row,0);
            n->x=saved?saved->x:-260.f;
            n->y=saved?saved->y:source_row*120.f;
            n->scale=saved && saved->scale?saved->scale:1;
            ++source_row;
            continue;
        }
        n->x=n->rank*260.f;
        n->y=rows[n->rank]++*120.f;
        TsFxCanvasPosition *p=fx_position(n->row,0);
        if(!p) {
            p=fx_position(n->row,1);
            if(p)fx_store_initial_position(p,n->x,n->y);
            else {
                /* A project can use every saved position. Give excess nodes
                   deterministic slots that are independent of their wires. */
                n->x=(n->row.kind==5?7:1+n->row.index%6)*260.f;
                n->y=(1+n->row.index/6)*120.f;
            }
        }
        if(p){n->x=p->x;n->y=p->y;n->scale=p->scale?p->scale:1;}

    }
    fx_migrate_headings();
    if(fx_canvas.movement || fx_canvas.active_view) {
        unsigned char keep[TS_MATRIX_DESTINATIONS];int bus=fx_movement_destinations(pages,bank,keep);
        int count=0;
        for(int i=0;i<fx_canvas.count;++i) {
            FxCanvasNode n=fx_canvas.node[i];
            int visible=n.row.kind==3 || n.row.kind==0 || n.row.kind==1?fx_filter_source(n.row):
                n.destination>=0?keep[n.destination]:n.row.kind==2 && keep[n.row.index] && bus;
            if(!visible && n.row.kind==4) {
                int slot=ts_matrix_effect_index(n.row.index);
                visible=(slot>=0 && fx_canvas.revealed[slot]==n.row.tile) || w->effect_open==n.row.tile;
                for(int j=0;!visible && j<w->selection_count;++j)visible=mx_same(n.row,w->selection[j]);
                n.outside_filter=visible;
            }
            if(visible)fx_canvas.node[count++]=n;
        }
        fx_canvas.count=count;
        for(int d=0;d<TS_FX_DESTINATIONS;++d)fx_canvas.dest[d]=-1;
        for(int i=0;i<count;++i)if(fx_canvas.node[i].destination>=0) {
            int n=fx_canvas.node[i].destination;fx_canvas.dest[n]=i;
            int sc=ts_matrix_sidechain_for(n);if(sc>=0 && ts_matrix_destination_active(&w->controls,sc))fx_canvas.dest[sc]=i;
        }
    }
    for(int i=0;i<fx_canvas.count;++i)if(fx_canvas.node[i].row.kind==2 && fx_canvas.node[i].row.index==TS_MATRIX_MOSAIC)fx_canvas.dest[TS_FX_MOSAIC_BUS]=i;
    /* Initial placement can create saved positions during this build. */
    memcpy(&fx_graph_cache.layout,w->layout,offsetof(TsMatrixLayout,canvas)+(size_t)w->layout->canvas_count*sizeof(w->layout->canvas[0]));
    fx_graph_cache.layout.heading_count=w->layout->heading_count;
    memcpy(fx_graph_cache.layout.headings,w->layout->headings,sizeof(w->layout->headings));
}
static int fx_chosen(MatrixRow row)
{
    MatrixWindow *w=&matrix_window;
    for(int i=0;i<w->selection_count;++i)if(mx_same(row,w->selection[i]))return 1;
    return 0;
}
static int fx_label_chosen(int i)
{return i>=0 && i<fx_label_limit() && (fx_canvas.labels[i] || fx_canvas.label_selected==i);}
static void fx_delete_labels(TsMatrixLayout *l)
{
    for(int i=fx_label_limit()-1;i>=0;--i)if(fx_label_chosen(i)) {
        if(i>=TS_FX_LABEL_LIMIT)memset(l->headings[i-TS_FX_LABEL_LIMIT].label.text,0,TS_FX_LABEL_TEXT);
        else if(i<l->label_count){memmove(l->labels+i,l->labels+i+1,(size_t)(l->label_count-i-1)*sizeof(l->labels[0]));memset(l->labels+--l->label_count,0,sizeof(l->labels[0]));}
    }
}
static void fx_clear_selection(void)
{fx_canvas.group_selected=0;memset(fx_canvas.labels,0,sizeof(fx_canvas.labels));fx_canvas.label_selected=-1;matrix_window.selection_count=0;matrix_window.selected=-1;}
static void fx_select(FxCanvasNode *n,int add)
{
    MatrixWindow *w=&matrix_window;
    if(!add)fx_clear_selection();
    int i=0;while(i<w->selection_count && !mx_same(n->row,w->selection[i]))++i;
    if(i==w->selection_count)w->selection[w->selection_count++]=n->row;
    else if(add)w->selection[i]=w->selection[--w->selection_count];
    w->selected=n->at;
}
static float fx_node_w(const FxCanvasNode *n){return FX_NODE_W*n->scale;}
static float fx_node_h(const FxCanvasNode *n){return FX_NODE_H*n->scale;}
static float fx_snap(float value) {return roundf(value/FX_GRID)*FX_GRID;}
static int fx_split(void){return SDL_max(360,MATRIX_W-MATRIX_BASE_W+SDL_clamp(matrix_window.layout->canvas_split,TS_FX_CANVAS_SPLIT_MIN,TS_FX_CANVAS_SPLIT_MAX));}
static int fx_right(void){return fx_canvas.meta?1260:fx_split();}
static int fx_inspector_x(void){return fx_split()+28;}
static int fx_inspector_width(void){return MATRIX_W-44-fx_inspector_x();}
static int fx_group_visible(uint32_t id);
static void fx_fit_selection(int selected)
{
    float left=1e9f,top=1e9f,right=-1e9f,bottom=-1e9f;int count=0;
    for(int i=0;i<fx_canvas.count;++i){FxCanvasNode *n=&fx_canvas.node[i];
        if(selected && !fx_chosen(n->row))continue;
        left=fminf(left,n->x);top=fminf(top,n->y);right=fmaxf(right,n->x+fx_node_w(n));bottom=fmaxf(bottom,n->y+fx_node_h(n));++count;}
    for(int i=0;i<fx_label_limit();++i) {
        if(selected && !fx_label_chosen(i))continue;
        if(!fx_label_visible(i) || (i<TS_FX_LABEL_LIMIT && fx_canvas.movement))continue;
        const TsFxCanvasLabel *p=fx_label_at(i);int heading=i>=TS_FX_LABEL_LIMIT;
        left=fminf(left,p->x);top=fminf(top,p->y-(heading?40*p->scale:0));
        right=fmaxf(right,p->x+320*p->scale);bottom=fmaxf(bottom,p->y+(heading?0:160*p->scale));++count;
    }
    for(int i=0;i<matrix_window.layout->group_count;++i){const TsFxCanvasGroup *g=&matrix_window.layout->groups[i];
        if((selected && g->id!=fx_canvas.group_selected) || !fx_group_visible(g->id))continue;
        left=fminf(left,g->x);top=fminf(top,g->y);right=fmaxf(right,g->x+g->w);bottom=fmaxf(bottom,g->y+g->h);++count;}
    fx_canvas.fit=0;if(!count)return;
    TsMatrixLayout *l=matrix_window.layout;float width=fx_split()-20,height=FX_BOTTOM-FX_TOP;
    l->canvas_zoom=SDL_clamp(fminf((width-48)/(right-left),(height-48)/(bottom-top)),.001f,selected?2.f:1.f);
    l->canvas_x=-left+(width/l->canvas_zoom-(right-left))*.5f;
    l->canvas_y=-top+(height/l->canvas_zoom-(bottom-top))*.5f;l->canvas_view=1;
}
static void fx_fit(void){fx_fit_selection(0);}
/* Reveal is a view edit. Move the viewport only as far as necessary; preserve
   the user's zoom unless the selected card itself cannot fit. */
static void fx_ensure_visible(FxCanvasNode *n)
{
    TsMatrixLayout *l=matrix_window.layout;float zoom=l->canvas_zoom;
    float width=fx_right()-20,height=FX_BOTTOM-FX_TOP;
    if(fx_node_w(n)*zoom>width-48 || fx_node_h(n)*zoom>height-48){fx_fit_selection(1);return;}
    float x=(n->x+l->canvas_x)*zoom,y=(n->y+l->canvas_y)*zoom;
    if(x<24)l->canvas_x+=(24-x)/zoom;
    else if(x+fx_node_w(n)*zoom>width-24)l->canvas_x+=(width-24-x-fx_node_w(n)*zoom)/zoom;
    if(y<24)l->canvas_y+=(24-y)/zoom;
    else if(y+fx_node_h(n)*zoom>height-24)l->canvas_y+=(height-24-y-fx_node_h(n)*zoom)/zoom;
    l->canvas_view=1;
}
/* Use Mosaic's palette roles; cables have a dedicated, brighter data color. */
#define FX_THEME(role,fallback) ts_theme_color(&matrix_window.palette,TS_THEME_MOSAIC,role,fallback)
#define FX_BG FX_THEME(TS_COLOR_BACKGROUND,0xff0b0d10u)
#define FX_PANEL FX_THEME(TS_COLOR_PANEL,0xff201f27u)
#define FX_EDGE FX_THEME(TS_COLOR_EDGE,0xff48414du)
#define FX_TEXT FX_THEME(TS_COLOR_TEXT,0xffe0d6e9u)
#define FX_DIM FX_THEME(TS_COLOR_DIM,0xffa79ab3u)
#define FX_FOCUS FX_THEME(TS_COLOR_FOCUS,0xffffc563u)
#define FX_MUTED FX_THEME(TS_COLOR_MUTED,0xff77727bu)
#define FX_CABLE FX_THEME(TS_COLOR_DATA2,0xff81cec8u)
static uint32_t fx_color(int type)
{
    static const int roles[]={TS_COLOR_DATA4,TS_COLOR_DATA3,TS_COLOR_DATA1,TS_COLOR_DATA1,
        TS_COLOR_DATA5,TS_COLOR_DATA3,TS_COLOR_DATA4,TS_COLOR_DATA5,TS_COLOR_DATA3,TS_COLOR_DATA4,TS_COLOR_DATA5,TS_COLOR_DATA3,TS_COLOR_DATA5,TS_COLOR_DATA5,TS_COLOR_DATA3,TS_COLOR_DATA2};
    static const uint32_t colors[]={0xffc9b3d4u,0xff9ed2b2u,0xffe6c17du,0xffe5b36bu,
        0xffb990c6u,0xff95c6dfu,0xffc294d2u,0xff9fcea7u,0xffb6d38du,0xffd19ebdu,0xffc499c9u,0xff9ed2b2u,0xffc499c9u,0xffc9a0e6u,0xffbba8dcu,0xff6ed7bdu};
    if(type==TS_MATRIX_FX_RECALL)return FX_CABLE;
    type=SDL_clamp(type,0,(int)SDL_arraysize(roles)-1);
    return FX_THEME(roles[type],colors[type]);
}
static uint32_t fx_source_color(MatrixRow row)
{
    TsMosaicEvent *e=row.kind==3?mx_card(row):NULL;
    if(e)return ts_ui_mosaic_card_color(&matrix_window.palette,e);
    TsFxCanvasPosition *p=fx_position(row,0);if(p && p->color)return p->color;
    int slot=row.kind==4?ts_matrix_effect_index(row.index):-1;
    return fx_color(slot>=0?matrix_window.controls.effect[slot].type:0);
}
static int fx_line_height(FxFontMetrics font)
{return (font.rows[7]*MATRIX_H+mx_pixel_h()-1)/mx_pixel_h()+2;}

/* Wrap at a word boundary when possible. Return the start of the next line. */
static const char *fx_label_line(const char *text,int capacity,char line[128])
{
    const char *newline=strchr(text,'\n');
    int count=newline?(int)(newline-text):(int)strlen(text);
    capacity=SDL_clamp(capacity,0,127);
    if(count>capacity) {
        count=capacity;
        while(count>0 && text[count]!=' ')--count;
        if(!count)count=capacity;
    }
    memcpy(line,text,count);line[count]=0;
    text+=count;if(*text=='\n')++text;
    while(*text==' ')++text;
    return text;
}
static void fx_text_box_scaled(SDL_Rect area,SDL_Rect clip,const char *text,uint32_t ink,int scale,int max_lines,float factor)
{
    FxFontMetrics font=fx_font_scaled(scale,factor);
    int stride=fx_line_height(font);
    int capacity=(mx_px(area.x+area.w)-mx_px(area.x))/font.advance;
    int lines=SDL_min(max_lines,(area.h+2)/stride);
    SDL_Rect visible;
    if(capacity<1 || lines<1 || !SDL_IntersectRect(&area,&clip,&visible))return;
    for(int i=0;i<lines && *text;++i) {
        char line[128];
        text=fx_label_line(text,capacity,line);
        if(i==lines-1 && *text && capacity>=3) {
            size_t keep=SDL_min(strlen(line),(size_t)SDL_min(capacity-3,124));
            memcpy(line+keep,"...",4);
        }
        fx_text_clipped(area.x,area.y+i*stride,line,ink,font,visible);
    }
}
static void fx_text_box(SDL_Rect area,SDL_Rect clip,const char *text,uint32_t ink,int scale,int lines)
{fx_text_box_scaled(area,clip,text,ink,scale,lines,1);}
static int fx_name_fits(const char *name,SDL_Rect area,int scale,float factor)
{
    FxFontMetrics font=fx_font_scaled(scale,factor);
    int capacity=(mx_px(area.x+area.w)-mx_px(area.x))/font.advance;
    int lines=SDL_min(2,(area.h+2)/fx_line_height(font));
    char line[128];
    if(capacity<1 || lines<1)return 0;
    while(lines-->0 && *name)name=fx_label_line(name,capacity,line);
    return !*name;
}
static void fx_panel_text(int x,int y,int width,int height,const char *text,uint32_t ink,int scale,int lines)
{
    SDL_Rect box={x,y,width,height};
    fx_text_box(box,box,text,ink,scale,lines);
}
static void fx_button(int x,int y,int width,const char *text,int active)
{
    mx_rect(x,y,width,28,active?FX_FOCUS:FX_EDGE);mx_rect(x+2,y+2,width-4,24,FX_PANEL);
    FxFontMetrics f=fx_font(2);
    int logical_width=(int)strlen(text)*f.advance*MATRIX_W/mx_pixel_w();
    mx_text(x+(width-logical_width)/2,y+(28-fx_line_height(f)+2)/2,text,active?FX_FOCUS:FX_TEXT,2);
}
static float fx_xf(float x){TsMatrixLayout *l=matrix_window.layout;return 20+(x+l->canvas_x)*l->canvas_zoom;}
static int fx_x(float x){return (int)lrintf(fx_xf(x));}
static float fx_yf(float y){TsMatrixLayout *l=matrix_window.layout;return FX_TOP+(y+l->canvas_y)*l->canvas_zoom;}
static int fx_y(float y){return (int)lrintf(fx_yf(y));}
static void fx_rect(int x,int y,int width,int height,uint32_t color)
{
    int r=SDL_min(fx_right(),x+width),b=SDL_min(FX_BOTTOM,y+height);x=SDL_max(20,x);y=SDL_max(FX_TOP,y);
    if(r>x && b>y)mx_rect(x,y,r-x,b-y,color);
}
#include "main_sdl_fx_canvas_draw.inc"
#include "ts_canvas_tiny_text.inc"
static uint32_t fx_tint(uint32_t a,uint32_t b)
{return 0xff000000u | (((a&0x00fefefeu)>>1)+((b&0x00fefefeu)>>1));}

static void fx_card_pixels(SDL_Rect clip,int x,int y,int right,int bottom,uint32_t ink)
{
    mx_fill_pixels(SDL_max(x,clip.x),SDL_max(y,clip.y),
                   SDL_min(right,clip.x+clip.w),SDL_min(bottom,clip.y+clip.h),ink);
}

static void fx_node_rect(float left,float top,float right,float bottom,const char *name,const char *compact,
    const char *detail,uint32_t color,int selected,float factor)
{
    if(right<=20 || left>=fx_right() || bottom<=FX_TOP || top>=FX_BOTTOM)return;
    FxPoint a=fx_device_point((FxPoint){left,top}),b=fx_device_point((FxPoint){right,bottom});
    int l=(int)lroundf(a.x),t=(int)lroundf(a.y),r=(int)lroundf(b.x),bt=(int)lroundf(b.y);
    if(r<=l || bt<=t)return;
    int stroke=SDL_max(1,(int)lroundf(fx_stroke.scale*(selected?1.5f:1.0f)));
    int x=(int)lroundf(left),y=(int)lroundf(top);
    int width=(int)lroundf(right)-x,height=(int)lroundf(bottom)-y;
    SDL_Rect clip=fx_cables_area();
    uint32_t panel=FX_PANEL;
    uint32_t edge=selected?FX_FOCUS:fx_tint(color,panel);
    uint32_t accent=selected?FX_FOCUS:color;
    if(r-l<=2*stroke || bt-t<=2*stroke) {
        fx_card_pixels(clip,l,t,r,bt,accent);
        return;
    }
    fx_card_pixels(clip,l+stroke,t+stroke,r-stroke,bt-stroke,panel);
    fx_card_pixels(clip,l,t,r,t+stroke,accent);
    fx_card_pixels(clip,l,bt-stroke,r,bt,edge);
    fx_card_pixels(clip,l,t+stroke,l+stroke,bt-stroke,edge);
    fx_card_pixels(clip,r-stroke,t+stroke,r,bt-stroke,edge);
    SDL_Rect graph={20,FX_TOP,fx_right()-20,FX_BOTTOM-FX_TOP};
    int pad=SDL_max(2,(int)lroundf((height<44*factor?4:7)*factor));
    int show_detail=width>=180*factor && height>=76*factor && detail && detail[0];
    SDL_Rect title={x+pad,y+pad,width-2*pad,height-pad-(int)((show_detail?30:4)*factor)};
    uint32_t title_ink=color==FX_MUTED?FX_DIM:FX_TEXT;
    if(width<118*factor || height<34*factor) {
        int available_width=r-l-4,available_height=bt-t-5;
        int readable=SDL_min(available_height/5,available_width/20);
        int pixel=SDL_clamp((int)lroundf(1.5f*fx_stroke.scale),1,SDL_max(1,readable));
        int capacity=SDL_min(63,available_width/(4*pixel));
        int lines=SDL_min(2,(available_height+pixel)/(6*pixel));
        if(lines>0 && capacity>0) {
            CanvasTextSurface surface={matrix_window.pixels,mx_pixel_w(),clip.x,clip.y,clip.x+clip.w,clip.y+clip.h};
            if(strlen(name)>(size_t)(capacity*lines) && compact[0] && strlen(compact)<strlen(name))name=compact;
            int y=t+2;
            if(lines==2 && strlen(name)>(size_t)capacity) {
                char first[128];
                name=fx_label_line(name,capacity,first);
                canvas_tiny_text(&surface,l+2,y,pixel,capacity,first,title_ink);
                y+=6*pixel;
            }
            canvas_tiny_text(&surface,l+2,y,pixel,capacity,name,title_ink);
        }
    } else {
        if(!fx_name_fits(name,title,1,factor))name=compact;
        fx_text_box_scaled(title,graph,name,title_ink,1,2,factor);
    }
    if(show_detail)fx_text_box_scaled((SDL_Rect){x+pad,y+height-(int)(27*factor),width-2*pad,(int)(18*factor)},graph,detail,FX_DIM,1,1,factor);
}

typedef enum {FX_PORT_AUDIO,FX_PORT_SIDECHAIN,FX_PORT_MODULATION} FxPortShape;
static void fx_draw_port(float x,float y,uint32_t ink,FxPortShape shape)
{
    SDL_Rect clip=fx_cables_area();
    FxPoint center=fx_device_point((FxPoint){x,y});
    int cx=(int)lroundf(center.x),cy=(int)lroundf(center.y);
    int unit=SDL_max(1,(int)lroundf(fx_stroke.scale));
    int radius=unit*(shape==FX_PORT_AUDIO && matrix_window.layout->canvas_zoom<.45f?1:2);
    if(shape==FX_PORT_SIDECHAIN) {
        for(int dy=-radius;dy<=radius;++dy) {
            int span=radius-abs(dy);
            fx_card_pixels(clip,cx-span,cy+dy,cx+span+1,cy+dy+1,ink);
        }
    } else if(shape==FX_PORT_MODULATION) {
        fx_card_pixels(clip,cx-radius,cy,cx+radius+1,cy+unit,ink);
        fx_card_pixels(clip,cx,cy-radius,cx+unit,cy+radius+1,ink);
    } else {
        fx_card_pixels(clip,cx-radius,cy-radius,cx+radius+1,cy+radius+1,ink);
    }
}
static void fx_chain_cable(int x,int y,int xx,int yy)
{
    if(!fx_stroke.coverage)return;
    fx_stroke_begin();
    if(abs(y-yy)<4) {
        fx_stroke_segment(fx_device_point((FxPoint){x,y}),fx_device_point((FxPoint){xx,yy}));
        fx_stroke_arrow(fx_device_point((FxPoint){(x+xx)*.5f,(y+yy)*.5f}),fx_device_point((FxPoint){xx-x,yy-y}));
    } else {
        int side=SDL_min(1244,SDL_max(x,xx)+18);
        FxPoint a=fx_device_point((FxPoint){x,y}),b=fx_device_point((FxPoint){side,y});
        FxPoint c=fx_device_point((FxPoint){side,yy}),d=fx_device_point((FxPoint){xx,yy});
        fx_stroke_segment(a,b);fx_stroke_segment(b,c);fx_stroke_segment(c,d);
        fx_stroke_arrow(fx_device_point((FxPoint){side,(y+yy)*.5f}),(FxPoint){0,yy-y});
    }
    fx_stroke_end(FX_CABLE);
}
static void fx_meta_paint(TsMatrixEffectControls *c)
{
    TsMetaControls *rack=&c->meta;char label[80];
    snprintf(label,sizeof(label),"FX CANVAS / %.31s",rack->name);mx_text(24,78,label,MX_AMBER,2);
    int previous_x=108,previous_y=210;
    fx_node_rect(26,180,108,240,"IN","IN","",MX_GREEN,0,1);
    for(int i=0;i<rack->count;++i) {
        int col=i<4?i:7-i,x=146+col*270,y=i<4?174:418;
        int inx=i<4?x:x+FX_NODE_W,outx=i<4?x+FX_NODE_W:x;
        fx_chain_cable(previous_x,previous_y,inx,y+FX_NODE_H/2);
        const TsMetaStage *s=&rack->stage[i];snprintf(label,sizeof(label),"STAGE %d / %s",i+1,s->enabled?"ACTIVE":"BYPASS");
        fx_node_rect(x,y,x+FX_NODE_W,y+FX_NODE_H,ts_sister_fx_type_name(s->type),ts_matrix_effect_short_name(s->type),label,s->enabled?fx_color(s->type):FX_MUTED,0,1);
        previous_x=outx;previous_y=y+FX_NODE_H/2;
    }
    int reverse=rack->count>4 || rack->count==4;
    int out_x=rack->count>4?SDL_max(26,previous_x-130):rack->count==4?1090:previous_x+40;
    int out_y=rack->count==4?300:previous_y-30;
    fx_chain_cable(previous_x,previous_y,reverse?out_x+82:out_x,out_y+30);
    fx_node_rect(out_x,out_y,out_x+82,out_y+60,"OUT","OUT","",MX_GREEN,0,1);
    mx_text(26,600,"Double-click a stage to edit its parameters and mappings.",FX_TEXT,2);
    mx_text(26,628,"Patch parallel branches on the outer canvas.",FX_DIM,2);
    for(int i=0;i<4;++i) {
        float v=rack->scene_mode && rack->captured==3?rack->scene[0][i]+(rack->scene[1][i]-rack->scene[0][i])*rack->morph:rack->macro[i];
        snprintf(label,sizeof(label),"%s %.0F%%",rack->label[i],v*100);int x=24+i*310;
        mx_text(x,710,label,MX_TEXT,2);mx_rect(x,746,286,6,MX_DIM);mx_rect(x+(int)(282*v),740,4,18,MX_GREEN);
    }
    mx_button(24,784,212,"RACK EDITOR",0);mx_text(260,790,"Drag or scroll to adjust a macro.",FX_TEXT,2);
}
static int fx_control_source(MatrixRow row)
{int slot=row.kind==4?ts_matrix_effect_index(row.index):-1;return slot>=0 && matrix_window.controls.effect[slot].type==TS_MATRIX_FX_MODULATOR?slot:-1;}
static int fx_visible_gain(MatrixRow row,int destination,TsSamplePages *pages,TsInstrument *bank)
{
    int slot=fx_control_source(row);
    if(slot>=0){const TsModControls *c=&matrix_window.controls.effect[slot].modulation;
        for(unsigned i=0;i<c->map_count;++i)if(ts_mod_target(&matrix_window.controls,&c->map[i])>=0 && TS_MATRIX_EFFECT_BASE+ts_mod_target(&matrix_window.controls,&c->map[i])==destination)return 1000;return 0;}
    TsSourceRoute raw=mx_route(row,pages,bank);
    if(row.kind==3 && !raw.matrix_enabled)return destination==TS_FX_MOSAIC_BUS?1000:0;
    return destination<TS_MATRIX_DESTINATIONS?mx_display_route(raw).matrix_gain[destination]:0;
}
#include "main_sdl_fx_inspector.inc"
static void fx_inspector_paint(TsSamplePages *pages,TsInstrument *bank)
{
    MatrixWindow *w=&matrix_window;char label[80];int slot=mx_effect_slot();
    int x=fx_inspector_x(),width=fx_inspector_width();
    mx_rect(fx_split()+8,FX_TOP-4,MATRIX_W-28-fx_split(),FX_BOTTOM-FX_TOP+8,FX_EDGE);mx_rect(fx_split()+11,FX_TOP-1,MATRIX_W-34-fx_split(),FX_BOTTOM-FX_TOP+2,FX_PANEL);
    if(fx_inspector_build()){fx_inspector_controls_paint();return;}
    if(slot<0) {
        if(fx_canvas.wire>=0 && fx_canvas.destination>=0 && fx_canvas.dest[fx_canvas.destination]>=0) {
            snprintf(label,sizeof(label),"%d CABLE%s",fx_canvas.wires.count,fx_canvas.wires.count==1?"":"S");mx_text(x,136,label,FX_FOCUS,2);
            mx_text(x,194,"FROM",FX_CABLE,2);
            fx_panel_text(x,221,width+5,65,fx_canvas.source.name,FX_TEXT,2,3);
            mx_text(x,302,"TO",FX_CABLE,2);
            fx_panel_text(x,329,width+5,65,fx_canvas.node[fx_canvas.dest[fx_canvas.destination]].row.name,FX_TEXT,2,3);
            int control=fx_control_source(fx_canvas.source)>=0,bus=fx_canvas.destination==TS_FX_MOSAIC_BUS;
            int gain=control || bus?0:fx_visible_gain(fx_canvas.source,fx_canvas.destination,pages,bank);
            if(gain>0) {
                snprintf(label,sizeof(label),"%+.1f dB",20*log10f(gain/1000.f));
                mx_text(x,414,label,FX_FOCUS,2);
            }
            fx_panel_text(x,465,width+5,100,control?"Control link. Right-click: color. X/Delete: disconnect assignments.":bus?"Mosaic bus link. X/Delete: disconnect. Ctrl+Shift-click: reroute.":"Shift+wheel: level. Wheel: zoom. X/Delete: disconnect.",FX_TEXT,2,4);
        } else if(w->selection_count) {
            if(w->selection_count==1)snprintf(label,sizeof(label),"%s",w->selection[0].name);
            else snprintf(label,sizeof(label),"%d NODES",w->selection_count);
            fx_panel_text(x,136,width+5,80,label,FX_FOCUS,2,3);
            fx_panel_text(x,235,width+5,100,"Z frames this selection.",FX_TEXT,2,3);
            int kind=w->selection[0].kind,sources=kind==0 || kind==1 || kind==3;
            for(int i=1;i<w->selection_count;++i)sources&=w->selection[i].kind==kind;
            if(sources){mx_button(x,345,width,kind==3?"TO MOSAIC BUS":kind==0?"TO TRACK BUS":"TO TILE BUS",0);
                fx_panel_text(x,386,width,90,"Return selected sources to their bus routing. Ctrl+Z: undo.",FX_DIM,1,4);}
        } else {
            mx_text(x,136,"FX INSPECTOR",FX_FOCUS,2);
            fx_panel_text(x,197,width+5,100,"Select a module to inspect it.",FX_TEXT,2,4);
            fx_panel_text(x,310,width+5,100,"Double-click a Meta FX to open its chain.",FX_TEXT,2,4);
        }
        return;
    }
}
static SDL_Rect fx_text_object_rect(int index);
static void fx_groups_clamp(void);
static int fx_source_toggle(SDL_AudioDeviceID,TsUiState *,int,int);
static uint32_t fx_wire_color(MatrixRow source,int destination);
static int fx_wire_selected(MatrixRow source,int destination);
static int fx_select_wire(MatrixRow source,int destination,int toggle);
#include "main_sdl_fx_canvas_navigation.inc"
#include "main_sdl_fx_canvas_edit.inc"
#include "main_sdl_fx_canvas_labels.inc"
#include "main_sdl_fx_canvas_patch.inc"
#include "main_sdl_fx_canvas_style.inc"
#include "main_sdl_fx_canvas_arrange.inc"
#include "main_sdl_fx_canvas_consolidate.inc"
#include "main_sdl_fx_canvas_reroute.inc"
#include "main_sdl_fx_canvas_groups.inc"
#include "main_sdl_fx_canvas_properties.inc"
#include "main_sdl_fx_canvas_popup.inc"
static void fx_paint_cables(TsSamplePages *pages,TsInstrument *bank)
{
    fx_groups_cache_background();
    fx_navigation_prepare(pages,bank);
    fx_cables_begin();
    MatrixWindow *w=&matrix_window;
    fx_validate_wires(pages,bank);
    FxCanvasNode *bus=NULL;
    for(int i=0;i<fx_canvas.count;++i)if(fx_canvas.node[i].row.kind==2 && fx_canvas.node[i].row.index==TS_MATRIX_MOSAIC)bus=&fx_canvas.node[i];
    int cable_selected=fx_canvas.wires.count>0;
    int node_selected=0;
    if(w->selection_count==1)for(int i=0;i<fx_canvas.count;++i)
        if(mx_same(fx_canvas.node[i].row,w->selection[0])){node_selected=1;break;}
    int focus=cable_selected || node_selected;
    /* Draw the focused path last so parallel paths cannot cover it. */
    for(int layer=0;layer<=focus;++layer) {
        for(int i=0;i<fx_canvas.count;++i) {
            FxCanvasNode *n=&fx_canvas.node[i];if(n->at<0)continue;
            TsSourceRoute raw=mx_route(n->row,pages,bank),r=mx_display_route(raw);
            if(n->row.kind==3 && !raw.matrix_enabled) {
                int selected=fx_wire_selected(n->row,TS_FX_MOSAIC_BUS)>=0;uint32_t custom=fx_wire_color(n->row,TS_FX_MOSAIC_BUS);
                if(bus && layer==(focus && selected)) {
                    uint32_t color=selected?MX_YELLOW:custom?custom:FX_DIM;
                    float x,y,xx,yy;fx_navigation_endpoints(n,TS_FX_MOSAIC_BUS,&x,&y,&xx,&yy);
                    fx_cables_add_signal(x,y,xx,yy,fx_focus_color(color,n,bus),0,n->level,color);
                }
                continue;
            }
            for(int d=0;d<TS_MATRIX_DESTINATIONS;++d)if(r.matrix_gain[d] && fx_canvas.dest[d]>=0) {
                FxCanvasNode *to=&fx_canvas.node[fx_canvas.dest[d]];int sc=d!=to->destination;
                int delayed=(n->row.kind==2 || n->row.kind==4) && (w->controls.delayed[n->row.index]&ts_matrix_feedback_bit(d));
                int selected=fx_wire_selected(n->row,d)>=0;
                int related=cable_selected?selected:node_selected &&
                    (mx_same(n->row,w->selection[0]) || mx_same(to->row,w->selection[0]));
                if(focus && layer!=related)continue;
                uint32_t custom=fx_wire_color(n->row,d);
                uint32_t flow_color=selected?MX_YELLOW:custom?custom:delayed?MX_AMBER:sc?0xffbc98ccu:FX_CABLE;
                uint32_t color=flow_color;
                if(focus && !related)color=fx_tint(color,FX_BG);
                float x=fx_xf(n->x+fx_node_w(n)),y=fx_yf(n->y+fx_node_h(n)*.5f),xx=fx_xf(to->x),yy=fx_yf(to->y+fx_node_h(to)*(sc?.8f:.5f));
                fx_navigation_endpoints(n,d,&x,&y,&xx,&yy);
                float level=w->layout->canvas_flow && w->controls.enabled && mx_destination_available(d)?
                    n->level*fabsf(r.matrix_gain[d]*.001f):0;
                if(level>.0001f)fx_cables_add_signal(x,y,xx,yy,fx_focus_color(color,n,to),0,level,flow_color);
                else fx_cables_add(x,y,xx,yy,fx_focus_color(color,n,to));
            }
        }
    }
    for(int i=0;i<fx_canvas.count;++i) {
        FxCanvasNode *n=&fx_canvas.node[i];int slot=n->row.kind==4?ts_matrix_effect_index(n->row.index):-1;
        if(slot<0 || w->controls.effect[slot].type!=TS_MATRIX_FX_MODULATOR)continue;
        const TsMatrixEffectControls *source=&w->controls.effect[slot];const TsModControls *c=&source->modulation;
        unsigned drawn=0;
        for(unsigned m=0;m<c->map_count;++m) {
            const TsModMap *map=&c->map[m];int target=ts_mod_target(&w->controls,map);
            if(target<0 || (drawn&(1u<<target)))continue;
            int node=fx_canvas.dest[TS_MATRIX_EFFECT_BASE+target];if(node<0)continue;
            drawn|=1u<<target;FxCanvasNode *to=&fx_canvas.node[node];
            int active=0;
            for(unsigned k=0;k<c->map_count;++k)if(c->map[k].target==map->target && ts_mod_target(&w->controls,&c->map[k])==target)
                active|=source->enabled && c->map[k].enabled && c->lane[c->map[k].lane].enabled;
            uint32_t custom=fx_wire_color(n->row,TS_MATRIX_EFFECT_BASE+target);int selected=fx_wire_selected(n->row,TS_MATRIX_EFFECT_BASE+target)>=0;
            fx_cables_add_kind(fx_xf(n->x+fx_node_w(n)),fx_yf(n->y+fx_node_h(n)*.5f),fx_xf(to->x),fx_yf(to->y+fx_node_h(to)*.5f),fx_focus_color(selected?MX_YELLOW:custom?custom:active?0xffc4a4dcu:FX_DIM,n,to),1);
        }
    }
    fx_cables_end();
    fx_navigation_badges();
}
static void fx_paint(AudioState *audio,TsSamplePages *pages,TsInstrument *bank)
{
    MatrixWindow *w=&matrix_window;mx_rect(0,0,MATRIX_W,MATRIX_H,FX_BG);char label[100];
    if(!fx_stroke_prepare()){fx_stroke_free();snprintf(w->status,sizeof(w->status),"CABLE DISPLAY BUFFER UNAVAILABLE.");}
    fx_build(pages,bank);if(fx_canvas.fit)fx_fit();
    mx_rect(16,FX_TOP-4,fx_right()-12,FX_BOTTOM-FX_TOP+8,FX_EDGE);
    mx_rect(20,FX_TOP,fx_right()-20,FX_BOTTOM-FX_TOP,FX_BG);
    /* A world-space grid: dots remain attached to module coordinates. At
       distant zooms draw only major dots, keeping paint cost bounded. */
    float grid=FX_GRID;while(grid*w->layout->canvas_zoom<16)grid*=2;
    float gx=ceilf(-w->layout->canvas_x/grid)*grid,gy=ceilf(-w->layout->canvas_y/grid)*grid;
    uint32_t grid_ink=fx_tint(FX_EDGE,FX_BG);
    for(float xx=gx;fx_xf(xx)<fx_right();xx+=grid)
        for(float yy=gy;fx_yf(yy)<FX_BOTTOM;yy+=grid)fx_rect(fx_x(xx),fx_y(yy),1,1,grid_ink);
    TsMatrixEffectControls *meta=fx_meta();
    fx_flow_visible=0;
    if(meta)fx_meta_paint(meta);
    else {
        for(int i=0;i<fx_canvas.count;++i)fx_canvas.node[i].level=fx_row_level(fx_canvas.node[i].row);
        fx_groups_paint();
        fx_paint_cables(pages,bank);
        for(int i=0;i<fx_canvas.count;++i) {
            FxCanvasNode *n=&fx_canvas.node[i];
            float l=fx_xf(n->x),t=fx_yf(n->y),r=fx_xf(n->x+fx_node_w(n)),b=fx_yf(n->y+fx_node_h(n));
            if(r<14 || l>fx_right()+6 || b<FX_TOP || t>FX_BOTTOM)continue;
            int slot=n->row.kind==4?ts_matrix_effect_index(n->row.index):-1;
            const TsMatrixEffectControls *c=slot>=0?&w->controls.effect[slot]:NULL;
            const char *detail=n->at<0?(mx_destination_available(n->destination)?"OUTPUT / RECEIVER":"N/A / PORT NOT OPEN"):c?(c->enabled?(c->type==TS_MATRIX_FX_META?"DOUBLE-CLICK TO ENTER":"ACTIVE / SELECT TO EDIT"):"BYPASS"):"BUS / PATCH ENDPOINT";
            if(n->outside_filter)detail="OUTSIDE MOVEMENT / EDITING";
            if(c && c->type==TS_MATRIX_FX_MODULATOR)detail="CONTROL / DOUBLE-CLICK TO EDIT";
            const char *title=c && c->type==TS_MATRIX_FX_SUNVOX?ts_sunvox_name(&c->sunvox):c && c->type==TS_MATRIX_FX_META?c->meta.name:n->row.name;
            char compact[48];
            if(c && c->type==TS_MATRIX_FX_RECALL)detail="HIT + LEVEL / OSC";
            if(c)snprintf(compact,sizeof(compact),"%s %u",ts_matrix_effect_short_name(c->type),(unsigned)c->id);
            else if(n->row.kind==2 && n->row.index==TS_MATRIX_TRACKER)snprintf(compact,sizeof(compact),"TRK BUS");
            else if(n->row.kind==2 && n->row.index==TS_MATRIX_LINK)snprintf(compact,sizeof(compact),"LIVE LINK");
            else snprintf(compact,sizeof(compact),"%s",title);
            TsFxCanvasPosition *style=fx_position(n->row,0);
            if(style && style->name[0]){title=style->name;snprintf(compact,sizeof(compact),"%s",title);}
            TsMosaicEvent *source=n->row.kind==3?mx_card(n->row):NULL;
            if(source){title=source->name;
                const char *layer=title;
                if(w->gallery)for(int c=0;c<w->gallery->count;++c)if(w->gallery->canvas[c].id==n->row.canvas)
                    layer=ts_movement_layer(title,w->gallery->canvas[c].name);
                snprintf(compact,sizeof(compact),"%s",layer);
                detail=source->muted?"MUTED / M: TOGGLE":source->solo?"SOLO / ALT+S: TOGGLE":"M: MUTE / ALT+S: SOLO";}
            uint32_t color=c && !c->enabled?FX_MUTED:fx_source_color(n->row);
            float peak=n->level;
            int sounding=peak>.0001f && n->row.kind<=3 && n->row.kind!=2;
            if(sounding) {
                color=fx_tint(color,MX_GREEN);
                detail="PLAYING";
            }
            color=fx_focus_color(color,n,n);
            fx_node_rect(l,t,r,b,title,compact,detail,color,fx_chosen(n->row),n->scale);
            float middle=fx_yf(n->y+fx_node_h(n)*.5f);
            int modulation=c && c->type==TS_MATRIX_FX_MODULATOR;
            if(n->destination>=0 && !modulation)fx_draw_port(l,middle,color,FX_PORT_AUDIO);
            if(n->at>=0 && (!c || c->type!=TS_MATRIX_FX_RECALL))
                fx_draw_port(r,middle,sounding?MX_GREEN:color,modulation?FX_PORT_MODULATION:FX_PORT_AUDIO);
            int sc=n->destination>=0?ts_matrix_sidechain_for(n->destination):-1;
            if(sc>=0 && ts_matrix_destination_active(&w->controls,sc))
                fx_draw_port(l,fx_yf(n->y+fx_node_h(n)*.8f),color,FX_PORT_SIDECHAIN);
            if(peak>.0001f && r-l>22 && l>20 && r<fx_right() && b<FX_BOTTOM && t>=FX_TOP)
                mx_signal((int)lroundf(l)+8,(int)lroundf(b)-3,(int)lroundf(r-l)-16,2,peak,MX_GREEN);
        }
        fx_labels_paint();fx_groups_titles();
        if((w->drag==FX_DRAG_WIRE || (w->drag==FX_DRAG_SHIFT_NODE && fx_canvas.shift_moved)) && fx_canvas.wire>=0) {
            for(int i=0;i<fx_canvas.count;++i)if(mx_same(fx_canvas.node[i].row,fx_canvas.source))
                fx_cable(fx_xf(fx_canvas.node[i].x+fx_node_w(&fx_canvas.node[i])),fx_yf(fx_canvas.node[i].y+fx_node_h(&fx_canvas.node[i])*.5f),fx_canvas.mouse_x,fx_canvas.mouse_y,MX_YELLOW,1,0,0);
        }
        if(w->drag==FX_DRAG_BOX || w->drag==FX_DRAG_WIRE_BOX){int ax=SDL_min((int)fx_canvas.pointer_x,fx_canvas.mouse_x),ay=SDL_min((int)fx_canvas.pointer_y,fx_canvas.mouse_y);
            int bx=SDL_max((int)fx_canvas.pointer_x,fx_canvas.mouse_x),by=SDL_max((int)fx_canvas.pointer_y,fx_canvas.mouse_y);
            fx_line(ax,ay,bx,ay,FX_FOCUS);fx_line(bx,ay,bx,by,FX_FOCUS);fx_line(bx,by,ax,by,FX_FOCUS);fx_line(ax,by,ax,ay,FX_FOCUS);}
        fx_inspector_paint(pages,bank);
        /* The grip lives between the graph and inspector borders. */
        uint32_t grip=fx_canvas.divider_hover || w->drag==FX_DRAG_DIVIDER?FX_FOCUS:FX_DIM;
        mx_rect(fx_split()+4,FX_TOP,2,FX_BOTTOM-FX_TOP,FX_EDGE);
        for(int y=374;y<418;y+=8)mx_rect(fx_split()+3,y,4,4,grip);
        if(fx_canvas.help) {
            mx_text(24,FX_BOTTOM+16,"G: grab / S: resize / Ctrl+Shift-click: connect sources / move cables",FX_TEXT,2);
            mx_text(24,FX_BOTTOM+44,"Ctrl+R: arrange names / Ctrl+Shift+R: types / Ctrl+Shift+P: consolidate",FX_TEXT,2);
            mx_text(24,FX_BOTTOM+72,"Right-click: actions / F2: name + color / Shift+wheel: gain / Z: frame",FX_TEXT,2);
        }
    }
    mx_text(20,22,"FX CANVAS",FX_CABLE,3);
    const char *names[]={meta?"BACK TO FX":"MATRIX F9","UNDO","REDO","+ EFFECT","FIT","MOSAIC","HELP","EDITOR"};
    for(int i=0;i<FX_TOOL_MOVEMENT;++i) {
        TsUiRect r=fx_toolbar_rect(i);fx_button(r.x,r.y,r.w,names[i],i==FX_TOOL_HELP && fx_canvas.help);
    }
    if(!meta) {
        const char *name=fx_canvas.active_view?"CURRENTLY PLAYING":"ALL MOVEMENTS";
        if(w->gallery)for(int c=0;c<w->gallery->count;++c)if(w->gallery->canvas[c].id==fx_canvas.movement)name=w->gallery->canvas[c].name;
        snprintf(label,sizeof(label),"VIEW: %.17s",name);
        const char *second[]={w->controls.enabled?label:"USE SAVED CHAIN",w->layout->canvas_straight?"STRAIGHT":"SPLINES",w->layout->canvas_snap?"SNAP ON":"SNAP OFF","CLEAR CABLES"};
        for(int i=FX_TOOL_MOVEMENT;i<=FX_TOOL_CLEAR;++i) {
            TsUiRect r=fx_toolbar_rect(i);fx_button(r.x,r.y,r.w,second[i-FX_TOOL_MOVEMENT],i==FX_TOOL_MOVEMENT?(fx_canvas.movement!=0 || fx_canvas.active_view):i==FX_TOOL_SNAP?w->layout->canvas_snap:0);
        }
    }
    if(!meta) {
        const char *tools[]={"MOVEMENTS","PROJECT","VIEW SIZES"};
        for(int i=0;i<3;++i){TsUiRect r=fx_toolbar_rect(FX_TOOL_NAVIGATOR+i);fx_button(r.x,r.y,r.w,tools[i],0);}
        TsUiRect flow=fx_toolbar_rect(FX_TOOL_FLOW);
        fx_button(flow.x,flow.y,flow.w,"FLOW F",w->layout->canvas_flow);
        mx_rect(flow.x+10,flow.y+7,14,14,w->layout->canvas_flow?FX_FOCUS:FX_DIM);
        mx_rect(flow.x+12,flow.y+9,10,10,w->layout->canvas_flow?FX_FOCUS:FX_PANEL);
    }
    if(w->status[0])fx_panel_text(24,MATRIX_H-34,MATRIX_W-240,28,w->status,FX_FOCUS,1,1);
    else {snprintf(label,sizeof(label),"%d NODES / %.0F%% / Z: FRAME SELECTED",fx_canvas.count,w->layout->canvas_zoom*100);mx_text(24,MATRIX_H-28,label,FX_DIM,1);}
    ts_ui_cpu_label(label,sizeof(label),ts_audio_cpu_percent);
    TsUiRect cpu=fx_toolbar_rect(FX_TOOL_CPU);fx_button(cpu.x,cpu.y,cpu.w,"",0);
    mx_text(cpu.x+10,cpu.y+3,label,ts_audio_cpu_percent<0?FX_DIM:
        ts_theme_color(&w->palette,TS_THEME_MOSAIC,
            ts_audio_cpu_percent>=100?TS_COLOR_ERROR:ts_audio_cpu_percent>=90?TS_COLOR_WARNING:TS_COLOR_ACTIVITY,
            ts_audio_cpu_percent>=100?0xffeb777du:ts_audio_cpu_percent>=90?0xffdfbc72u:0xff8addb4u),1);
    mx_text(cpu.x+10,cpu.y+16,ts_audio_activity,FX_DIM,1);
    if(fx_canvas.popup)fx_popup_paint();
    if(fx_canvas.label_editing)fx_label_editor_paint();
    if(fx_canvas.confirm_delete) {
        mx_rect(300,250,680,260,FX_FOCUS);mx_rect(302,252,676,256,FX_PANEL);
        fx_panel_text(324,272,620,28,"REMOVE EFFECTS: RETAIN LINKS?",FX_FOCUS,2,1);
        fx_panel_text(324,315,620,90,"YES connects incoming sources to the next effects. NO disconnects inputs. Downstream effects stay in place. Both choices can be undone.",FX_TEXT,2,4);
        fx_button(324,448,190,"YES: RETAIN",0);fx_button(532,448,190,"NO: DISCONNECT",0);fx_button(740,448,190,"CANCEL",0);
    }
    if(fx_canvas.movement_popup) {
        int count=w->gallery?w->gallery->count:0;
        mx_rect(20,88,500,32*(count+2)+4,FX_EDGE);
        fx_button(22,90,496,"ALL MOVEMENTS",!fx_canvas.movement && !fx_canvas.active_view);
        fx_button(22,122,496,"CURRENTLY PLAYING",fx_canvas.active_view);
        for(int c=0;c<count;++c) {
            snprintf(label,sizeof(label),"C%02d / %.35s",c+1,w->gallery->canvas[c].name);
            fx_button(22,154+c*32,496,label,fx_canvas.movement==w->gallery->canvas[c].id);
        }
    }
    if(fx_canvas.confirm_clear) {
        mx_rect(340,270,600,230,FX_FOCUS);mx_rect(342,272,596,226,FX_PANEL);
        mx_text(362,292,"CLEAR ALL CABLES?",FX_FOCUS,2);
        fx_panel_text(362,333,548,90,"Disconnect all routes, including hidden sources and feedback. Modules stay in place. You can undo this change.",FX_TEXT,2,4);
        fx_button(362,445,180,"CANCEL",0);fx_button(710,445,208,"CLEAR CABLES",0);
    }
    (void)audio;
}
static void fx_view(int canvas,TsSamplePages *pages,TsInstrument *bank)
{
    fx_properties_close();fx_cancel_gesture();svfx_close();fx_clear_wires();MatrixWindow *w=&matrix_window;w->canvas=canvas;w->drag=0;w->effect_menu=0;fx_canvas.popup=fx_canvas.confirm_clear=0;fx_canvas.meta=0;fx_canvas.wire=-1;
    if(canvas){w->filter=0;mx_rows(pages,bank);fx_build(pages,bank);if(!w->layout->canvas_view)fx_canvas.fit=1;}
    if(canvas && fx_canvas.movement && w->effect_open)
        for(int i=0;i<fx_canvas.count;++i)if(fx_canvas.node[i].row.kind==4 && fx_canvas.node[i].row.tile==w->effect_open){fx_ensure_visible(&fx_canvas.node[i]);break;}
    SDL_SetWindowTitle(w->window,canvas?"TapeSister - FX Canvas (Shift+F9)":"TapeSister - Routing Matrix (F9)");w->dirty=1;
}
static void fx_selection_from_mosaic(TsUiState *ui,TsSamplePages *pages,TsInstrument *bank)
{
    fx_clear_selection();fx_clear_wires();
    if(!ui->mosaic)return;
    for(int i=0;i<matrix_window.count;++i) {
        MatrixRow row=matrix_window.rows[i];
        if(row.kind!=3 || mx_card_scene(row)!=ui->mosaic)continue;
        TsMosaicEvent *card=ts_mosaic_find(ui->mosaic,row.tile);if(!card)continue;
        int slot=(int)(card-ui->mosaic->events);
        if(ui->mosaic_selected==card->id || ui->mosaic_selection[slot]==card->id) {
            matrix_window.selection[matrix_window.selection_count++]=row;matrix_window.selected=i;
        }
    }
    /* Imported selections must remain visible even after a movement filter. */
    if(matrix_window.selection_count){fx_canvas.movement=0;fx_canvas.active_view=0;fx_build(pages,bank);fx_fit_selection(1);}
    matrix_window.dirty=1;
}
static void fx_selection_to_mosaic(TsUiState *ui)
{
    if(!ui->mosaic)return;
    mosaic_select_only(ui,0);
    for(int i=0;i<matrix_window.selection_count;++i) {
        MatrixRow row=matrix_window.selection[i];
        if(row.kind==3 && mx_card_scene(row)!=ui->mosaic)return;
    }
    for(int i=0;i<matrix_window.selection_count;++i) {
        MatrixRow row=matrix_window.selection[i];if(row.kind==3)mosaic_select_add(ui,row.tile);
    }
}
static void fx_return_mosaic(SDL_Window *main,TsUiState *ui)
{
    fx_selection_to_mosaic(ui);ui->mosaic_open=1;ui->tracker_open=0;
    matrix_hide();SDL_ShowWindow(main);SDL_RaiseWindow(main);SDL_SetWindowInputFocus(main);
}
static void fx_focus_movement(uint64_t movement,int playing,TsSamplePages *pages,TsInstrument *bank)
{
    fx_canvas.active_view=playing;fx_canvas.movement=movement;
    memset(fx_canvas.revealed,0,sizeof(fx_canvas.revealed));
    fx_clear_selection();fx_clear_wires();matrix_window.effect_open=matrix_window.compressor_open=0;
    fx_build(pages,bank);fx_fit();matrix_window.dirty=1;
    snprintf(matrix_window.status,sizeof(matrix_window.status),"MOVEMENT VIEW / PLAYBACK AND ROUTING UNCHANGED");
}
#include "main_sdl_fx_canvas_input.inc"
