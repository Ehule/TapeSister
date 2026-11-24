/* One action table serves mouse discovery and keyboard navigation. Context
   commands reuse the same Matrix transactions as their existing shortcuts. */
enum { FX_CONTEXT_MENU=3,FX_CONTEXT_W=438,FX_CONTEXT_ROW=22,FX_CONTEXT_CAPACITY=32,
    FX_CONTEXT_ADD,FX_CONTEXT_INPUTS,FX_CONTEXT_OUTPUTS,FX_CONTEXT_CONSOLIDATE,
    FX_CONTEXT_WIRE_SOURCES,FX_CONTEXT_WIRE_DESTINATIONS,FX_CONTEXT_WIRE_UPSTREAM,FX_CONTEXT_WIRE_DOWNSTREAM,
    FX_CONTEXT_GROUP,FX_CONTEXT_UNGROUP,FX_CONTEXT_NAMES,FX_CONTEXT_TYPES,
    FX_CONTEXT_PROPERTIES,FX_CONTEXT_PALETTE,FX_CONTEXT_COPY,FX_CONTEXT_PASTE,
    FX_CONTEXT_NETWORK,FX_CONTEXT_FOCUS,FX_CONTEXT_BUNDLE,
    FX_CONTEXT_MUTE,FX_CONTEXT_SOLO,FX_CONTEXT_DELETE,FX_CONTEXT_FIT,FX_CONTEXT_UNDO,FX_CONTEXT_REDO,FX_CONTEXT_FLOW_COLOR };
typedef struct { int action,enabled;const char *name,*shortcut; } FxContextItem;
static int fx_context_items(FxContextItem items[FX_CONTEXT_CAPACITY])
{
    MatrixWindow *w=&matrix_window;int objects=0,effects=0,labels=0,grouped=fx_canvas.group_selected!=0;
    for(int i=0;i<fx_canvas.count;++i) {
        FxCanvasNode *n=&fx_canvas.node[i];if(fx_object_selected(n)){++objects;effects+=n->row.kind==4;}
        TsFxCanvasPosition *p=fx_position(n->row,0);if(fx_chosen(n->row) && p && p->group)grouped=1;
    }
    for(int i=0;i<fx_label_limit();++i)if(fx_label_chosen(i) && fx_label_visible(i)){++labels;if(fx_label_at(i)->group)grouped=1;}
    int edit=mx_editable(),n=0;
#define ITEM(a,ok,title,key) items[n++]=(FxContextItem){a,!!(ok),title,key}
    ITEM(FX_CONTEXT_ADD,edit,"Add effect...","Ctrl+N");
    ITEM(FX_CONTEXT_INPUTS,fx_canvas.context_has_card,"Select input cards","");
    ITEM(FX_CONTEXT_OUTPUTS,fx_canvas.context_has_card,"Select output cards","");
    ITEM(FX_CONTEXT_WIRE_SOURCES,fx_canvas.wires.count,"Select cable sources","");
    ITEM(FX_CONTEXT_WIRE_DESTINATIONS,fx_canvas.wires.count,"Select cable destinations","");
    ITEM(FX_CONTEXT_WIRE_UPSTREAM,fx_canvas.wires.count,"Trace cables upstream","");
    ITEM(FX_CONTEXT_WIRE_DOWNSTREAM,fx_canvas.wires.count,"Trace cables downstream","");
    ITEM(FX_CONTEXT_NETWORK,fx_canvas.context_has_card || w->selection_count || fx_canvas.wires.count,"Select connected network","");
    ITEM(FX_CONTEXT_FOCUS,fx_canvas.focus_count || w->selection_count,fx_canvas.focus_count?"Show all paths":"Focus selection","");
    ITEM(FX_CONTEXT_BUNDLE,1,fx_canvas.bundles?"Expand all cables":"Bundle source cables","");
    ITEM(FX_CONTEXT_CONSOLIDATE,edit && effects>=2,"Consolidate...","Ctrl+Shift+P");
    ITEM(FX_CONTEXT_GROUP,edit && (w->selection_count || labels),"Group selection","Ctrl+G");
    ITEM(FX_CONTEXT_UNGROUP,edit && grouped,"Ungroup","Alt+G");
    ITEM(FX_CONTEXT_NAMES,edit && objects>1,"Arrange by name","Ctrl+R");
    ITEM(FX_CONTEXT_TYPES,edit && objects>1,"Arrange by type","Ctrl+Shift+R");
    ITEM(FX_CONTEXT_PROPERTIES,objects || labels || fx_canvas.wires.count || grouped,
        fx_canvas.wires.count?"Cable color...":labels && !objects?"Edit text...":"Name / color...","F2");
    ITEM(FX_CONTEXT_FLOW_COLOR,1,"Flow color...","");
    int sources=0;for(int i=0;i<w->selection_count;++i)sources|=w->selection[i].kind==3;
    if(sources){ITEM(FX_CONTEXT_MUTE,edit,"Mute source cards","M");ITEM(FX_CONTEXT_SOLO,edit,"Solo source cards","Alt+S");}
    ITEM(FX_CONTEXT_PALETTE,1,"Palette editor...","Ctrl+Alt+P");
    ITEM(FX_CONTEXT_COPY,fx_selected_effects(),"Copy effects","Ctrl+C");
    ITEM(FX_CONTEXT_PASTE,edit && fx_clipboard.count && w->controls.enabled,"Paste effects","Ctrl+V");
    ITEM(FX_CONTEXT_DELETE,edit && (fx_selected_effects() || labels || fx_canvas.wires.count),"Delete selection...","Delete");
    ITEM(FX_CONTEXT_FIT,objects || labels || fx_canvas.wires.count,"Frame selection","Z");
    ITEM(FX_CONTEXT_UNDO,!w->original_active && w->history_cursor>0,"Undo","Ctrl+Z");
    ITEM(FX_CONTEXT_REDO,!w->original_active && w->history_cursor<w->history_count,"Redo","Ctrl+Y");
#undef ITEM
    return n;
}
static void fx_context_open(int x,int y,const MatrixRow *card)
{
    fx_canvas.context_x=x;fx_canvas.context_y=y;fx_canvas.context_has_card=card!=NULL;
    if(card)fx_canvas.context_card=*card;
    FxContextItem items[FX_CONTEXT_CAPACITY];int count=fx_context_items(items);
    fx_canvas.popup=FX_CONTEXT_MENU;fx_canvas.popup_selected=-1;
    fx_canvas.popup_x=SDL_clamp(x,24,MATRIX_W-FX_CONTEXT_W-24);
    fx_canvas.popup_y=SDL_clamp(y,FX_TOP+4,MATRIX_H-44-count*FX_CONTEXT_ROW);
    matrix_window.drag=0;matrix_window.dirty=1;
    snprintf(matrix_window.status,sizeof(matrix_window.status),"CANVAS ACTIONS / UP-DOWN: CHOOSE / ENTER: APPLY / ESC: CLOSE");
}
static void fx_context_paint(void)
{
    FxContextItem items[FX_CONTEXT_CAPACITY];int count=fx_context_items(items),x=fx_canvas.popup_x,y=fx_canvas.popup_y;
    mx_rect(x,y,FX_CONTEXT_W,40+count*FX_CONTEXT_ROW,FX_CABLE);
    mx_rect(x+2,y+2,FX_CONTEXT_W-4,36+count*FX_CONTEXT_ROW,FX_PANEL);
    fx_panel_text(x+12,y+9,FX_CONTEXT_W-100,24,"CANVAS ACTIONS",FX_FOCUS,2,1);
    fx_panel_text(x+FX_CONTEXT_W-42,y+12,32,20,"Esc",FX_DIM,1,1);
    for(int i=0;i<count;++i) {
        int yy=y+32+i*FX_CONTEXT_ROW,hot=fx_canvas.popup_selected==i;
        if(hot)mx_rect(x+6,yy,FX_CONTEXT_W-12,FX_CONTEXT_ROW,FX_BG);
        uint32_t ink=!items[i].enabled?FX_DIM:hot?FX_FOCUS:FX_TEXT;
        fx_panel_text(x+12,yy+6,*items[i].shortcut?266:FX_CONTEXT_W-24,22,items[i].name,ink,2,1);
        fx_panel_text(x+FX_CONTEXT_W-152,yy+10,140,18,items[i].shortcut,FX_DIM,1,1);
    }
}
/* Store outgoing destination bits once, then follow only that direction. The
   finite visited sets prevent feedback loops and reconverging branches from
   selecting a card twice. Implicit Mosaic bus and sidechain links count too. */
static void fx_context_trace(int outputs,TsSamplePages *pages,TsInstrument *bank)
{
    MatrixWindow *w=&matrix_window;MatrixRow origin=fx_canvas.context_card;
    fx_canvas.movement=0;fx_canvas.active_view=0;fx_build(pages,bank);
    enum { WORDS=(TS_FX_DESTINATIONS+63)/64 };
    uint64_t (*links)[WORDS]=calloc((size_t)fx_canvas.count,sizeof(*links));
    unsigned char *seen=calloc((size_t)fx_canvas.count,1);uint64_t reach[WORDS]={0};
    if(!links || !seen){free(links);free(seen);snprintf(w->status,sizeof(w->status),"CANNOT ALLOCATE CONNECTION SELECTION.");return;}
    int root=-1;
    for(int i=0;i<fx_canvas.count;++i) {
        FxCanvasNode *node=&fx_canvas.node[i];if(mx_same(node->row,origin))root=i;
        if(node->at<0)continue;
        TsSourceRoute raw=mx_route(node->row,pages,bank),route=mx_display_route(raw);
        int control=fx_control_source(node->row);
        for(int d=0;d<TS_FX_DESTINATIONS;++d) {
            int to=fx_canvas.dest[d];if(to<0)continue;
            int connected=control>=0?fx_visible_gain(node->row,d,pages,bank):
                node->row.kind==3 && !raw.matrix_enabled?d==TS_FX_MOSAIC_BUS:
                d<TS_MATRIX_DESTINATIONS && route.matrix_gain[d];
            if(!connected)continue;
            int target=fx_canvas.node[to].destination;
            if(target<0)target=TS_FX_MOSAIC_BUS;
            links[i][target/64]|=UINT64_C(1)<<(target%64);
        }
    }
    if(root>=0) {
        seen[root]=1;int dest=fx_canvas.node[root].destination;
        if(dest<0 && origin.kind==2 && origin.index==TS_MATRIX_MOSAIC)dest=TS_FX_MOSAIC_BUS;
        if(dest>=0)reach[dest/64]|=UINT64_C(1)<<(dest%64);
        for(int changed=1;changed;) {
            changed=0;
            for(int i=0;i<fx_canvas.count;++i) {
                int dest=fx_canvas.node[i].destination;
                if(dest<0 && fx_canvas.node[i].row.kind==2 && fx_canvas.node[i].row.index==TS_MATRIX_MOSAIC)dest=TS_FX_MOSAIC_BUS;
                int match=0;
                if(outputs)match=dest>=0 && (reach[dest/64]&(UINT64_C(1)<<(dest%64)));
                else for(int k=0;k<WORDS;++k)match|=(links[i][k]&reach[k])!=0;
                if(match && !seen[i]){seen[i]=1;changed=1;}
                if(!seen[i])continue;
                for(int k=0;outputs && k<WORDS;++k){uint64_t next=reach[k]|links[i][k];changed|=next!=reach[k];reach[k]=next;}
                if(!outputs && dest>=0){uint64_t bit=UINT64_C(1)<<(dest%64);changed|=!(reach[dest/64]&bit);reach[dest/64]|=bit;}
            }
        }
    }
    fx_clear_selection();fx_clear_wires();w->effect_open=w->compressor_open=0;
    for(int i=0;i<fx_canvas.count;++i)if(seen[i] && i!=root)w->selection[w->selection_count++]=fx_canvas.node[i].row;
    free(links);free(seen);
    snprintf(w->status,sizeof(w->status),"SELECTED %d %s CARDS / ALL MOVEMENTS / Z: FRAME SELECTION",w->selection_count,outputs?"OUTPUT":"INPUT");
}
static void fx_context_trace_wires(int outputs,int recursive,TsSamplePages *pages,TsInstrument *bank)
{
    MatrixWindow *w=&matrix_window;int wire_count=fx_canvas.wires.count;
    FxCanvasWire *wires=malloc((size_t)wire_count*sizeof(*wires));
    if(!wires)return;
    memcpy(wires,fx_canvas.wires.items,(size_t)wire_count*sizeof(*wires));
    fx_canvas.movement=0;fx_canvas.active_view=0;fx_build(pages,bank);
    enum { WORDS=(TS_FX_DESTINATIONS+63)/64 };
    uint64_t (*links)[WORDS]=calloc((size_t)fx_canvas.count,sizeof(*links));
    unsigned char *seen=calloc((size_t)fx_canvas.count,1);uint64_t reach[WORDS]={0};
    if(!links || !seen){free(wires);free(links);free(seen);snprintf(w->status,sizeof(w->status),"CANNOT ALLOCATE CONNECTION SELECTION.");return;}
    for(int i=0;i<fx_canvas.count;++i) {
        FxCanvasNode *node=&fx_canvas.node[i];
        if(node->at<0)continue;
        TsSourceRoute raw=mx_route(node->row,pages,bank),route=mx_display_route(raw);
        int control=fx_control_source(node->row);
        for(int d=0;d<TS_FX_DESTINATIONS;++d) {
            int to=fx_canvas.dest[d];if(to<0)continue;
            int connected=control>=0?fx_visible_gain(node->row,d,pages,bank):
                node->row.kind==3 && !raw.matrix_enabled?d==TS_FX_MOSAIC_BUS:
                d<TS_MATRIX_DESTINATIONS && route.matrix_gain[d];
            if(!connected)continue;
            int target=fx_canvas.node[to].destination;
            if(target<0)target=TS_FX_MOSAIC_BUS;
            links[i][target/64]|=UINT64_C(1)<<(target%64);
        }
    }
    /* Start from the exact selected cable endpoints, never the nearest card.
       Keep a visited set: cycles and reconverging paths terminate safely. */
    for(int j=0;j<wire_count;++j) {
        MatrixRow source=wires[j].source;
        int destination=wires[j].destination;
        for(int i=0;i<fx_canvas.count;++i) {
            FxCanvasNode *node=&fx_canvas.node[i];
            int match=outputs ? destination>=0 && destination<TS_FX_DESTINATIONS && fx_canvas.dest[destination]==i : mx_same(node->row,source);
            if(match) {
                seen[i]=1;
                int d=node->destination;
                if(d<0 && node->row.kind==2 && node->row.index==TS_MATRIX_MOSAIC)d=TS_FX_MOSAIC_BUS;
                if(d>=0)reach[d/64]|=UINT64_C(1)<<(d%64);
            }
        }
    }
    if(recursive)for(int changed=1;changed;) {
        changed=0;
        for(int i=0;i<fx_canvas.count;++i) {
            int dest=fx_canvas.node[i].destination;
            if(dest<0 && fx_canvas.node[i].row.kind==2 && fx_canvas.node[i].row.index==TS_MATRIX_MOSAIC)dest=TS_FX_MOSAIC_BUS;
            int match=0;
            if(outputs)match=dest>=0 && (reach[dest/64]&(UINT64_C(1)<<(dest%64)));
            else for(int k=0;k<WORDS;++k)match|=(links[i][k]&reach[k])!=0;
            if(match && !seen[i]){seen[i]=1;changed=1;}
            if(!seen[i])continue;
            for(int k=0;outputs && k<WORDS;++k){uint64_t next=reach[k]|links[i][k];changed|=next!=reach[k];reach[k]=next;}
            if(!outputs && dest>=0){uint64_t bit=UINT64_C(1)<<(dest%64);changed|=!(reach[dest/64]&bit);reach[dest/64]|=bit;}
        }
    }
    free(wires);
    fx_clear_selection();fx_clear_wires();w->effect_open=w->compressor_open=0;
    for(int i=0;i<fx_canvas.count;++i)if(seen[i])w->selection[w->selection_count++]=fx_canvas.node[i].row;
    free(links);free(seen);
    snprintf(w->status,sizeof(w->status),"SELECTED %d %s CARDS / ALL MOVEMENTS / Z: FRAME SELECTION",w->selection_count,outputs?"DOWNSTREAM":"UPSTREAM");
}
static void fx_context_network(TsSamplePages *pages,TsInstrument *bank)
{
    MatrixWindow *w=&matrix_window;
    fx_canvas.movement=0;fx_canvas.active_view=0;fx_build(pages,bank);
    unsigned char *seen=calloc((size_t)fx_canvas.count,1);
    if(!seen){snprintf(w->status,sizeof(w->status),"CANNOT ALLOCATE NETWORK SELECTION.");return;}
    for(int i=0;i<fx_canvas.count;++i) {
        FxCanvasNode *n=&fx_canvas.node[i];seen[i]=fx_chosen(n->row) || (fx_canvas.context_has_card && mx_same(n->row,fx_canvas.context_card));
        for(int j=0;j<fx_canvas.wires.count;++j) {
            FxCanvasWire wire=fx_canvas.wires.items[j];
            if(mx_same(wire.source,n->row) || (wire.destination>=0 && fx_canvas.dest[wire.destination]==i))seen[i]=1;
        }
    }
    for(int changed=1;changed;) {
        changed=0;
        for(int i=0;i<fx_canvas.count;++i)if(fx_canvas.node[i].at>=0)
            for(int d=0;d<TS_FX_DESTINATIONS;++d) {
                int to=fx_canvas.dest[d];if(to<0 || seen[i]==seen[to])continue;
                if(fx_visible_gain(fx_canvas.node[i].row,d,pages,bank)){seen[i]=seen[to]=1;changed=1;}
            }
    }
    fx_clear_selection();fx_clear_wires();w->effect_open=w->compressor_open=0;
    for(int i=0;i<fx_canvas.count;++i)if(seen[i])w->selection[w->selection_count++]=fx_canvas.node[i].row;
    free(seen);snprintf(w->status,sizeof(w->status),"SELECTED %d CONNECTED CARDS / Z: FRAME / FOCUS SELECTION IN MENU",w->selection_count);
}
static void fx_context_run(int action,SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,TsSamplePages *pages,TsInstrument *bank)
{
    MatrixWindow *w=&matrix_window;fx_canvas.popup=0;
    switch(action) {
    case FX_CONTEXT_NETWORK:fx_context_network(pages,bank);break;
    case FX_CONTEXT_FOCUS:
        if(fx_canvas.focus_count)fx_canvas.focus_count=0;
        else {fx_canvas.focus_count=w->selection_count;memcpy(fx_canvas.focus_rows,w->selection,(size_t)w->selection_count*sizeof(w->selection[0]));}
        snprintf(w->status,sizeof(w->status),"%s / VIEW ONLY",fx_canvas.focus_count?"FOCUS SELECTION / MENU: SHOW ALL PATHS":"ALL PATHS SHOWN");break;
    case FX_CONTEXT_BUNDLE:fx_canvas.bundles=!fx_canvas.bundles;
        snprintf(w->status,sizeof(w->status),"%s / SELECT A BUNDLE TO EXPAND ITS CABLES",fx_canvas.bundles?"SOURCE CABLES BUNDLED":"ALL CABLES EXPANDED");break;
    case FX_CONTEXT_ADD:fx_popup_open(fx_canvas.context_x,fx_canvas.context_y);break;
    case FX_CONTEXT_INPUTS:case FX_CONTEXT_OUTPUTS:fx_context_trace(action==FX_CONTEXT_OUTPUTS,pages,bank);break;
    case FX_CONTEXT_WIRE_SOURCES:case FX_CONTEXT_WIRE_DESTINATIONS:
    case FX_CONTEXT_WIRE_UPSTREAM:case FX_CONTEXT_WIRE_DOWNSTREAM:
        fx_context_trace_wires(action==FX_CONTEXT_WIRE_DESTINATIONS || action==FX_CONTEXT_WIRE_DOWNSTREAM,
            action==FX_CONTEXT_WIRE_UPSTREAM || action==FX_CONTEXT_WIRE_DOWNSTREAM,pages,bank);break;
    case FX_CONTEXT_CONSOLIDATE:fx_consolidate_choose(device,audio,ui,pages,bank);break;
    case FX_CONTEXT_GROUP:fx_group_create();break;
    case FX_CONTEXT_UNGROUP:fx_group_ungroup();break;
    case FX_CONTEXT_NAMES:case FX_CONTEXT_TYPES:fx_arrange(action==FX_CONTEXT_TYPES,pages,bank);break;
    case FX_CONTEXT_PROPERTIES:
        if(fx_canvas.group_selected)fx_properties_begin_group(fx_canvas.group_selected);
        else if(fx_canvas.wires.count)fx_properties_begin_cables();
        else if(w->selection_count)fx_properties_begin(w->selection[0]);
        else if(fx_canvas.label_selected>=0)fx_label_begin(fx_canvas.label_selected);
        break;
    case FX_CONTEXT_MUTE:case FX_CONTEXT_SOLO:fx_source_toggle(device,ui,action==FX_CONTEXT_SOLO,0);break;
    case FX_CONTEXT_PALETTE:palette_window_show(ui);break;
    case FX_CONTEXT_FLOW_COLOR:fx_properties_begin_flow();break;
    case FX_CONTEXT_COPY:fx_copy();break;
    case FX_CONTEXT_PASTE:fx_paste(device,audio,ui,pages,bank);break;
    case FX_CONTEXT_DELETE:if(fx_selected_effects())fx_delete_request(-1);else fx_edit_selection(1,0,device,audio,ui,pages,bank);break;
    case FX_CONTEXT_FIT:fx_fit_selection(1);break;
    case FX_CONTEXT_UNDO:case FX_CONTEXT_REDO:mx_undo(device,audio,ui,pages,bank,action==FX_CONTEXT_REDO);break;
    }
}
static int fx_context_event(SDL_Event *e,int x,int y,SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,TsSamplePages *pages,TsInstrument *bank)
{
    FxContextItem items[FX_CONTEXT_CAPACITY];int count=fx_context_items(items),at=-1;
    if(e->type==SDL_KEYDOWN) {
        if(e->key.repeat)return 1;
        SDL_Keycode key=e->key.keysym.sym;
        if(key==SDLK_ESCAPE || key==SDLK_BACKSPACE){fx_canvas.popup=0;return 1;}
        if(key==SDLK_UP || key==SDLK_DOWN) {
            int step=key==SDLK_UP?-1:1,index=fx_canvas.popup_selected;
            if(index<0)index=step>0?-1:0;
            for(int i=0;i<count;++i){index=(index+step+count)%count;if(items[index].enabled)break;}
            fx_canvas.popup_selected=index;
        }
        if(key==SDLK_RETURN || key==SDLK_KP_ENTER)at=fx_canvas.popup_selected;
    } else if(e->type==SDL_MOUSEMOTION || e->type==SDL_MOUSEBUTTONDOWN) {
        if(x>=fx_canvas.popup_x && x<fx_canvas.popup_x+FX_CONTEXT_W && y>=fx_canvas.popup_y+32 && y<fx_canvas.popup_y+32+count*FX_CONTEXT_ROW)
            at=(y-fx_canvas.popup_y-32)/FX_CONTEXT_ROW;
        if(e->type==SDL_MOUSEMOTION){if(fx_canvas.popup_selected!=at)matrix_window.dirty=1;fx_canvas.popup_selected=at;return 1;}
        if(e->button.button!=SDL_BUTTON_LEFT || at<0){fx_canvas.popup=0;return 1;}
    }
    if(at>=0 && at<count && items[at].enabled)fx_context_run(items[at].action,device,audio,ui,pages,bank);
    return 1;
}
