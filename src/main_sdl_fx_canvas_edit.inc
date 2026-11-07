/* Canvas-only selection and edit transactions. Cable endpoints retain effect
   identities as well as physical ports, so slot reuse cannot retarget a selection. */
static int fx_wire_reserve(FxWireList *list,int count)
{
    if(count<=list->capacity)return 1;
    if(count<0 || count>INT_MAX/2)return 0;
    int capacity=SDL_max(count,SDL_max(32,list->capacity*2));
    if((size_t)capacity>SIZE_MAX/sizeof(*list->items))return 0;
    FxCanvasWire *items=realloc(list->items,(size_t)capacity*sizeof(*items));
    if(!items)return 0;list->items=items;list->capacity=capacity;return 1;
}
static uint32_t fx_wire_target_id(int destination)
{
    int slot=ts_matrix_effect_index(destination);
    if(destination>=TS_MATRIX_EFFECT_SC_BASE && destination<TS_MATRIX_INSTANCE_DESTINATIONS)
        slot=destination-TS_MATRIX_EFFECT_SC_BASE;
    return slot>=0?matrix_window.controls.effect[slot].id:0;
}
static int fx_wire_selected(MatrixRow source,int destination)
{
    for(int i=0;i<fx_canvas.wires.count;++i) {
        FxCanvasWire *wire=&fx_canvas.wires.items[i];
        if(mx_same(wire->source,source) && wire->destination==destination &&
           wire->target_id==fx_wire_target_id(destination))return i;
    }
    return -1;
}
static void fx_primary_wire(void)
{
    fx_canvas.wire=fx_canvas.destination=-1;
    if(fx_canvas.wires.count) {
        FxCanvasWire *wire=&fx_canvas.wires.items[fx_canvas.wires.count-1];
        fx_canvas.source=wire->source;fx_canvas.destination=wire->destination;fx_canvas.wire=0;
    }
}
static void fx_clear_wires(void)
{fx_canvas.wires.count=0;fx_primary_wire();}
static int fx_select_wire(MatrixRow source,int destination,int toggle)
{
    int found=fx_wire_selected(source,destination);
    if(toggle && found>=0) {
        memmove(fx_canvas.wires.items+found,fx_canvas.wires.items+found+1,
            (size_t)(--fx_canvas.wires.count-found)*sizeof(*fx_canvas.wires.items));
    } else if(found<0) {
        if(!fx_wire_reserve(&fx_canvas.wires,fx_canvas.wires.count+1)) {
            snprintf(matrix_window.status,sizeof(matrix_window.status),"CANNOT ALLOCATE CABLE SELECTION.");return 0;
        }
        fx_canvas.wires.items[fx_canvas.wires.count++]=(FxCanvasWire){source,destination,fx_wire_target_id(destination)};
    }
    fx_primary_wire();return 1;
}
static void fx_validate_wires(TsSamplePages *pages,TsInstrument *bank)
{
    for(int i=0;i<fx_canvas.wires.count;) {
        FxCanvasWire *wire=&fx_canvas.wires.items[i];int at=fx_source_at(wire->source),d=wire->destination;
        if(at<0 || d<0 || d>=TS_MATRIX_DESTINATIONS || fx_canvas.dest[d]<0 ||
           wire->target_id!=fx_wire_target_id(d) ||
           !mx_display_route(mx_route(matrix_window.rows[at],pages,bank)).matrix_gain[d]) {
            *wire=fx_canvas.wires.items[--fx_canvas.wires.count];continue;
        }
        wire->source=matrix_window.rows[at];++i;
    }
    if(matrix_window.drag!=FX_DRAG_WIRE && matrix_window.drag!=FX_DRAG_SHIFT_NODE)fx_primary_wire();
}
static int fx_modal(void)
{return matrix_window.drag==FX_DRAG_GRAB || matrix_window.drag==FX_DRAG_SCALE;}
static void fx_cancel_gesture(void)
{
    MatrixWindow *w=&matrix_window;
    if((fx_modal() || w->drag==FX_DRAG_NODE || w->drag==FX_DRAG_PAN) && w->pending && w->layout)
        *w->layout=w->pending->layout;
    if(w->drag==FX_DRAG_BOX || w->drag==FX_DRAG_WIRE_BOX) {
        w->selection_count=fx_canvas.before_count;
        memcpy(w->selection,fx_canvas.before,(size_t)w->selection_count*sizeof(*w->selection));
        /* The backup capacity was reserved before starting the gesture. */
        fx_canvas.wires.count=fx_canvas.before_wires.count;
        if(fx_canvas.wires.count)memcpy(fx_canvas.wires.items,fx_canvas.before_wires.items,
            (size_t)fx_canvas.wires.count*sizeof(*fx_canvas.wires.items));
    }
    w->drag=0;fx_primary_wire();SDL_CaptureMouse(SDL_FALSE);
}
static int fx_begin_transform(int mode)
{
    MatrixWindow *w=&matrix_window;if(!mx_editable() || !w->pending)return 0;
    fx_canvas.move_count=0;
    for(int i=0;i<fx_canvas.count;++i) {
        FxCanvasNode *n=&fx_canvas.node[i];if(!fx_chosen(n->row))continue;
        TsFxCanvasPosition *p=fx_position(n->row,1);
        if(!p || fx_canvas.move_count==TS_FX_CANVAS_POSITIONS) {
            *w->layout=w->pending->layout;fx_canvas.move_count=0;
            snprintf(w->status,sizeof(w->status),"UNCHANGED: POSITION STORAGE IS FULL. SELECT FEWER SOURCE NODES.");return 0;
        }
        p->x=n->x;p->y=n->y;fx_canvas.move[fx_canvas.move_count++]=*p;
    }
    if(!fx_canvas.move_count){snprintf(w->status,sizeof(w->status),"SELECT MODULES TO %s.",mode==FX_DRAG_SCALE?"RESIZE":"MOVE");return 0;}
    fx_canvas.pointer_x=fx_canvas.mouse_x;fx_canvas.pointer_y=fx_canvas.mouse_y;
    fx_canvas.gesture_zoom=w->layout->canvas_zoom;w->drag=mode;SDL_CaptureMouse(SDL_TRUE);
    snprintf(w->status,sizeof(w->status),mode==FX_DRAG_SCALE?
        "RESIZE: MOVE RIGHT/UP TO ENLARGE / CLICK OR ENTER: PLACE / ESC: CANCEL":
        mode==FX_DRAG_NODE?"MOVE: DRAG POINTER / RELEASE: PLACE / ESC: CANCEL":
        "GRAB: MOVE POINTER / CLICK OR ENTER: PLACE / ESC: CANCEL");return 1;
}
static void fx_transform_motion(int x,int y)
{
    float dx=x-fx_canvas.pointer_x,dy=y-fx_canvas.pointer_y;
    float factor=powf(2.f,SDL_clamp((dx-dy)/200.f,-8.f,8.f));
    for(int i=0;i<fx_canvas.move_count;++i) {
        const TsFxCanvasPosition *from=&fx_canvas.move[i];
        TsFxCanvasPosition *p=fx_position((MatrixRow){.kind=from->kind,.tile=from->id,.index=(int)from->id},0);
        if(!p)continue;
        if(matrix_window.drag==FX_DRAG_SCALE) {
            float old=from->scale?from->scale:1;
            p->scale=SDL_clamp(old*factor,.25f,4.f);
            p->x=from->x+FX_NODE_W*(old-p->scale)*.5f;
            p->y=from->y+FX_NODE_H*(old-p->scale)*.5f;
        } else {
            p->x=SDL_clamp(from->x+dx/fx_canvas.gesture_zoom,-100000.f,100000.f);
            p->y=SDL_clamp(from->y+dy/fx_canvas.gesture_zoom,-100000.f,100000.f);
        }
    }
}
static int fx_cable_crosses(int x,int y,int xx,int yy,SDL_Rect box)
{
    int segments=matrix_window.layout->canvas_straight?1:32;
    float bend=fmaxf(40,fabsf((float)xx-x)*.45f);int oldx=x,oldy=y;
    for(int i=1;i<=segments;++i) {
        float t=i/(float)segments,u=1-t;
        int nx=segments==1?xx:(int)lrintf(u*u*u*x+3*u*u*t*(x+bend)+3*u*t*t*(xx-bend)+t*t*t*xx);
        int ny=segments==1?yy:(int)lrintf(u*u*u*y+3*u*u*t*y+3*u*t*t*yy+t*t*t*yy);
        int ax=oldx,ay=oldy,bx=nx,by=ny;
        if(SDL_IntersectRectAndLine(&box,&ax,&ay,&bx,&by))return 1;
        oldx=nx;oldy=ny;
    }
    return 0;
}
static int fx_begin_box(int x,int y,int wires,int add)
{
    MatrixWindow *w=&matrix_window;
    if(!fx_wire_reserve(&fx_canvas.before_wires,fx_canvas.wires.count)) {
        snprintf(w->status,sizeof(w->status),"CANNOT ALLOCATE CABLE SELECTION.");return 0;
    }
    fx_canvas.before_count=w->selection_count;
    memcpy(fx_canvas.before,w->selection,(size_t)w->selection_count*sizeof(*w->selection));
    fx_canvas.before_wires.count=fx_canvas.wires.count;
    if(fx_canvas.wires.count)memcpy(fx_canvas.before_wires.items,fx_canvas.wires.items,
        (size_t)fx_canvas.wires.count*sizeof(*fx_canvas.wires.items));
    fx_canvas.box_add=add;
    if(!add){fx_clear_selection();fx_clear_wires();}
    w->effect_open=w->compressor_open=0;
    fx_canvas.pointer_x=fx_canvas.mouse_x=x;fx_canvas.pointer_y=fx_canvas.mouse_y=y;
    w->drag=wires?FX_DRAG_WIRE_BOX:FX_DRAG_BOX;SDL_CaptureMouse(SDL_TRUE);return 1;
}
static void fx_box_update(int x,int y,TsSamplePages *pages,TsInstrument *bank)
{
    MatrixWindow *w=&matrix_window;int ax=SDL_min((int)fx_canvas.pointer_x,x),ay=SDL_min((int)fx_canvas.pointer_y,y);
    SDL_Rect box={ax,ay,abs(x-(int)fx_canvas.pointer_x)+1,abs(y-(int)fx_canvas.pointer_y)+1};
    w->selection_count=fx_canvas.box_add?fx_canvas.before_count:0;
    memcpy(w->selection,fx_canvas.before,(size_t)w->selection_count*sizeof(*w->selection));
    fx_canvas.wires.count=fx_canvas.box_add?fx_canvas.before_wires.count:0;
    if(fx_canvas.wires.count)memcpy(fx_canvas.wires.items,fx_canvas.before_wires.items,
        (size_t)fx_canvas.wires.count*sizeof(*fx_canvas.wires.items));
    for(int i=0;i<fx_canvas.count;++i) {
        FxCanvasNode *n=&fx_canvas.node[i];
        if(w->drag==FX_DRAG_BOX) {
            SDL_Rect rect={fx_x(n->x),fx_y(n->y),SDL_max(1,(int)(fx_node_w(n)*w->layout->canvas_zoom)),SDL_max(1,(int)(fx_node_h(n)*w->layout->canvas_zoom))};
            if(SDL_HasIntersection(&box,&rect) && !fx_chosen(n->row))w->selection[w->selection_count++]=n->row;
        } else if(n->at>=0) {
            TsSourceRoute route=mx_display_route(mx_route(n->row,pages,bank));
            for(int d=0;d<TS_MATRIX_DESTINATIONS;++d)if(route.matrix_gain[d] && fx_canvas.dest[d]>=0) {
                FxCanvasNode *to=&fx_canvas.node[fx_canvas.dest[d]];
                if(fx_cable_crosses(fx_x(n->x+fx_node_w(n)),fx_y(n->y+fx_node_h(n)*.5f),
                    fx_x(to->x),fx_y(to->y+fx_node_h(to)*(d==to->destination?.5f:.8f)),box) && !fx_select_wire(n->row,d,0)) {
                    fx_cancel_gesture();return;
                }
            }
        }
    }
    fx_primary_wire();fx_canvas.mouse_x=x;fx_canvas.mouse_y=y;
}
static TsSourceRoute *fx_saved_route(MatrixSnapshot *s,MatrixRow row)
{
    if(row.kind==2 || row.kind==4)return &s->controls.row[row.index];
    for(int i=0;i<s->count;++i)if(mx_same(s->routes[i].row,row))return &s->routes[i].route;
    return NULL;
}
/* Stage the whole edit before publishing it. No per-wire commits or partially
   removed branch, and the shared event wrapper creates exactly one undo step. */
static void fx_edit_selection(int remove,int wheel,SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,TsSamplePages *pages,TsInstrument *bank)
{
    MatrixWindow *w=&matrix_window;if(!mx_editable() || !w->controls.enabled)return;
    fx_validate_wires(pages,bank);
    MatrixSnapshot *s=mx_snapshot(device,audio,pages,bank);if(!s)return;
    for(int i=0;i<fx_canvas.wires.count;++i) {
        FxCanvasWire wire=fx_canvas.wires.items[i];TsSourceRoute *r=fx_saved_route(s,wire.source);if(!r)continue;
        if(r->mode==TS_SOURCE_INHERIT || mx_legacy_direct(*r)) {
            free(s);snprintf(w->status,sizeof(w->status),"UNCHANGED: CONVERT INHERITED / LEGACY SOURCE ROWS IN THE MATRIX FIRST.");return;
        }
        if(wire.source.kind!=2 && wire.source.kind!=4)*r=mx_display_route(*r);
    }
    int changed=0;float factor=powf(10.f,SDL_clamp(wheel*.025f,-6.f,6.f)),low=0,high=1e9f;
    if(!remove)for(int i=0;i<fx_canvas.wires.count;++i) {
        FxCanvasWire wire=fx_canvas.wires.items[i];TsSourceRoute *r=fx_saved_route(s,wire.source);if(!r)continue;
        int gain=r->matrix_gain[wire.destination];if(!gain)continue;
        int delayed=(wire.source.kind==2 || wire.source.kind==4) && (s->controls.delayed[wire.source.index]&ts_matrix_feedback_bit(wire.destination));
        low=fmaxf(low,1.f/gain);high=fminf(high,(delayed?500.f:2000.f)/gain);
    }
    factor=SDL_clamp(factor,low,high);
    for(int i=0;i<fx_canvas.wires.count;++i) {
        FxCanvasWire wire=fx_canvas.wires.items[i];TsSourceRoute *r=fx_saved_route(s,wire.source);if(!r)continue;
        int old=r->matrix_gain[wire.destination];if(!old)continue;
        int gain=remove?0:SDL_clamp((int)lroundf(old*factor),1,2000);
        if(wire.source.kind==2 || wire.source.kind==4) {
            if(ts_matrix_connect(&s->controls,wire.source.index,wire.destination,gain)!=1){free(s);return;}
        } else r->matrix_gain[wire.destination]=gain;
        changed|=old!=gain;
    }
    if(remove)for(int slot=0;slot<TS_MATRIX_EFFECT_CAPACITY;++slot) {
        uint32_t id=s->controls.effect[slot].id;
        if(!id || !fx_chosen((MatrixRow){.kind=4,.tile=id}))continue;
        ts_matrix_effect_remove(&s->controls,slot);changed=1;
        for(int i=0;i<s->count;++i)mx_clear_effect_route(&s->routes[i].route,slot);
    }
    if(changed && mx_apply(s,device,audio,ui,pages,bank)) {
        if(remove){fx_clear_wires();fx_clear_selection();w->effect_open=w->compressor_open=0;}
        snprintf(w->status,sizeof(w->status),remove?"DELETED EXPLICIT SELECTION / CTRL+Z: UNDO":"SELECTED CABLE LEVELS UPDATED / SHIFT+WHEEL: GAIN / WHEEL: ZOOM");
    }
    free(s);
}
static struct {
    int count,pastes;
    TsMatrixEffectControls effect[TS_MATRIX_EFFECT_CAPACITY];
    TsSourceRoute route[TS_MATRIX_EFFECT_CAPACITY];
    uint64_t delayed[TS_MATRIX_EFFECT_CAPACITY];
    TsFxCanvasPosition position[TS_MATRIX_EFFECT_CAPACITY];
    int slot[TS_MATRIX_EFFECT_CAPACITY];
} fx_clipboard;
static void fx_copy(void)
{
    MatrixWindow *w=&matrix_window;fx_clipboard.count=fx_clipboard.pastes=0;
    for(int i=0;i<fx_canvas.count;++i) {
        FxCanvasNode *n=&fx_canvas.node[i];if(n->row.kind!=4 || !fx_chosen(n->row))continue;
        int slot=ts_matrix_effect_index(n->row.index),at=fx_clipboard.count++;
        fx_clipboard.effect[at]=w->controls.effect[slot];fx_clipboard.route[at]=w->controls.row[n->row.index];
        fx_clipboard.delayed[at]=w->controls.delayed[n->row.index];fx_clipboard.slot[at]=slot;
        fx_clipboard.position[at]=(TsFxCanvasPosition){n->row.tile,4,n->x,n->y,n->scale};
    }
    snprintf(w->status,sizeof(w->status),fx_clipboard.count?"COPIED %d EFFECTS WITH INTERNAL WIRING / CTRL+V: PASTE":"SELECT ADDED EFFECTS TO COPY. FIXED BUSES AND ENDPOINTS ARE NOT COPIED.",fx_clipboard.count);
}
static void fx_paste(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,TsSamplePages *pages,TsInstrument *bank)
{
    MatrixWindow *w=&matrix_window;if(!mx_editable() || !fx_clipboard.count || !w->controls.enabled)return;
    MatrixSnapshot *s=mx_snapshot(device,audio,pages,bank);if(!s)return;
    int slots[TS_MATRIX_EFFECT_CAPACITY];uint32_t ids[TS_MATRIX_EFFECT_CAPACITY];
    for(int i=0;i<fx_clipboard.count;++i) {
        int slot=ts_matrix_effect_add(&s->controls,fx_clipboard.effect[i].type);
        if(slot<0){free(s);snprintf(w->status,sizeof(w->status),"NOTHING PASTED: NEED %d FREE EFFECT SLOTS.",fx_clipboard.count);return;}
        slots[i]=slot;ids[i]=s->controls.effect[slot].id;
        /* Even a legacy project with sends to an unused slot must not feed a
           newly pasted branch through those old physical ports. */
        ts_matrix_effect_remove(&s->controls,slot);
        for(int r=0;r<s->count;++r)mx_clear_effect_route(&s->routes[r].route,slot);
        s->controls.effect[slot]=fx_clipboard.effect[i];s->controls.effect[slot].id=ids[i];
    }
    for(int i=0;i<fx_clipboard.count;++i) {
        int row=TS_MATRIX_EFFECT_BASE+slots[i];TsSourceRoute *r=&s->controls.row[row];
        *r=fx_clipboard.route[i];memset(r->matrix_gain,0,sizeof(r->matrix_gain));s->controls.delayed[row]=0;
        for(int j=0;j<fx_clipboard.count;++j)for(int side=0;side<2;++side) {
            int old=(side?TS_MATRIX_EFFECT_SC_BASE:TS_MATRIX_EFFECT_BASE)+fx_clipboard.slot[j];
            int dest=(side?TS_MATRIX_EFFECT_SC_BASE:TS_MATRIX_EFFECT_BASE)+slots[j];
            r->matrix_gain[dest]=fx_clipboard.route[i].matrix_gain[old];
            if(fx_clipboard.delayed[i]&ts_matrix_feedback_bit(old))s->controls.delayed[row]|=ts_matrix_feedback_bit(dest);
        }
    }
    if(!ts_matrix_valid(&s->controls)){free(s);snprintf(w->status,sizeof(w->status),"NOTHING PASTED: INTERNAL FEEDBACK NEEDS MATRIX FEEDBACK ENABLED.");return;}
    /* Reclaim deleted-instance layout records before reserving the entire paste. */
    for(int i=s->layout.canvas_count-1;i>=0;--i)if(s->layout.canvas[i].kind==4) {
        int live=0;for(int j=0;j<TS_MATRIX_EFFECT_CAPACITY;++j)live|=s->controls.effect[j].id==s->layout.canvas[i].id;
        if(!live){s->layout.canvas[i]=s->layout.canvas[--s->layout.canvas_count];memset(&s->layout.canvas[s->layout.canvas_count],0,sizeof(s->layout.canvas[0]));}
    }
    if(s->layout.canvas_count+fx_clipboard.count>TS_FX_CANVAS_POSITIONS){free(s);snprintf(w->status,sizeof(w->status),"NOTHING PASTED: CANVAS POSITION STORAGE IS FULL.");return;}
    for(int i=0;i<fx_clipboard.count;++i) {
        TsFxCanvasPosition p=fx_clipboard.position[i];p.id=ids[i];p.x=SDL_clamp(p.x+40*(fx_clipboard.pastes+1),-100000.f,100000.f);p.y=SDL_clamp(p.y+40*(fx_clipboard.pastes+1),-100000.f,100000.f);
        s->layout.canvas[s->layout.canvas_count++]=p;
        mx_append_port(&s->layout,TS_MATRIX_EFFECT_BASE+slots[i]);
        if(fx_clipboard.effect[i].type==TS_MATRIX_FX_COMPRESSOR)mx_append_port(&s->layout,TS_MATRIX_EFFECT_SC_BASE+slots[i]);
    }
    if(mx_apply(s,device,audio,ui,pages,bank)) {
        ++fx_clipboard.pastes;fx_clear_selection();fx_clear_wires();fx_build(pages,bank);
        for(int i=0;i<fx_canvas.count;++i)for(int j=0;j<fx_clipboard.count;++j)
            if(fx_canvas.node[i].row.kind==4 && fx_canvas.node[i].row.tile==ids[j])fx_select(&fx_canvas.node[i],1);
        w->effect_open=ids[0];w->compressor_open=0;
        snprintf(w->status,sizeof(w->status),"PASTED %d INDEPENDENT EFFECTS / EXTERNAL PORTS FREE / G: MOVE / CTRL+Z: UNDO",fx_clipboard.count);
    }
    free(s);
}
