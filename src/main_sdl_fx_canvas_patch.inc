/* Route surgery is staged as one transaction, including parked Gallery cards. */
static int fx_bridge_route(TsSourceRoute *r,int slot,const TsSourceRoute *out)
{
    int n=TS_MATRIX_EFFECT_BASE+slot,sc=TS_MATRIX_EFFECT_SC_BASE+slot,gain=r->matrix_gain[n];
    r->matrix_gain[n]=r->matrix_gain[sc]=0;
    if(!gain)return 1;
    for(int d=0;d<TS_MATRIX_DESTINATIONS;++d)if(out->matrix_gain[d]) {
        int v=r->matrix_gain[d]+(gain*out->matrix_gain[d]+500)/1000;
        if(d==n || d==sc || v>2000)return 0;
        r->matrix_gain[d]=(uint16_t)v;
    }
    return 1;
}
static int fx_delete_effects(int retain,SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,TsSamplePages *pages,TsInstrument *bank)
{
    MatrixWindow *w=&matrix_window;if(!mx_editable())return 0;
    MatrixSnapshot *s=mx_snapshot(device,audio,pages,bank);if(!s)return 0;
    int removed=0,ok=1;
    fx_validate_wires(pages,bank);
    for(int i=0;i<fx_canvas.wires.count;++i) {
        FxCanvasWire wire=fx_canvas.wires.items[i];TsSourceRoute *route=fx_saved_route(s,wire.source);
        if(route){route->matrix_gain[wire.destination]=0;if(wire.source.kind==2 || wire.source.kind==4)s->controls.delayed[wire.source.index]&=~ts_matrix_feedback_bit(wire.destination);}
    }
    for(int slot=0;slot<TS_MATRIX_EFFECT_CAPACITY && ok;++slot) {
        uint32_t id=s->controls.effect[slot].id;
        if(!id || !fx_chosen((MatrixRow){.kind=4,.tile=id}))continue;
        int n=TS_MATRIX_EFFECT_BASE+slot;
        TsSourceRoute out=s->controls.row[n];
        if(retain) {
            /* Retiming an explicit feedback delay would change the patch. */
            if(s->controls.delayed[n])ok=0;
            for(int row=0;row<TS_MATRIX_ROWS;++row)
                if(s->controls.delayed[row]&ts_matrix_feedback_bit(n))ok=0;
            for(int row=0;row<TS_MATRIX_ROWS && ok;++row)if(row!=n)
                ok=fx_bridge_route(&s->controls.row[row],slot,&out);
            for(int i=0;i<s->count && ok;++i)ok=fx_bridge_route(&s->routes[i].route,slot,&out);
        } else for(int i=0;i<s->count;++i)mx_clear_effect_route(&s->routes[i].route,slot);
        ts_matrix_effect_remove(&s->controls,slot);++removed;
    }
    if(ok)ok=ts_matrix_valid(&s->controls);
    if(ok && removed)ok=mx_apply(s,device,audio,ui,pages,bank);
    free(s);
    if(!ok) {
        snprintf(w->status,sizeof(w->status),"UNCHANGED: RETAINED LINKS WOULD EXCEED GAIN LIMITS OR ALTER A FEEDBACK LOOP. CHOOSE NO OR CANCEL.");return 0;
    }
    if(removed){fx_clear_wires();fx_clear_selection();w->effect_open=w->compressor_open=0;}
    snprintf(w->status,sizeof(w->status),"REMOVED %d EFFECTS / %s / CTRL+Z: UNDO",removed,retain?"LINKS RETAINED":"INPUTS DISCONNECTED");return 1;
}
static void fx_delete_request(int slot)
{
    MatrixWindow *w=&matrix_window;
    if(slot>=0) {
        fx_clear_selection();fx_clear_wires();
        w->selection[w->selection_count++]=(MatrixRow){.kind=4,.index=TS_MATRIX_EFFECT_BASE+slot,.tile=w->controls.effect[slot].id};
    }
    fx_canvas.confirm_delete=1;w->drag=0;w->dirty=1;
}
static int fx_selected_effects(void)
{
    for(int i=0;i<matrix_window.selection_count;++i)if(matrix_window.selection[i].kind==4)return 1;
    return 0;
}
static void fx_connect_selected(int destination,SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,TsSamplePages *pages,TsInstrument *bank)
{
    MatrixWindow *w=&matrix_window;
    if(destination<0 || !w->selection_count)return;
    for(int i=0;i<w->selection_count;++i) {
        MatrixRow r=w->selection[i];
        if(r.kind==5 || ((r.kind==2 || r.kind==4) && r.index==destination)) {
            snprintf(w->status,sizeof(w->status),"UNCHANGED: SELECT SOURCE CARDS, THEN CTRL+SHIFT-CLICK A SEPARATE DESTINATION.");return;
        }
    }
    w->destination=destination;fx_clear_wires();mx_gain(device,audio,ui,pages,bank,1000);
}
static int fx_replace_effect(int type,const TsSunVoxControls *patch,SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,TsSamplePages *pages,TsInstrument *bank)
{
    MatrixWindow *w=&matrix_window;if(!mx_editable())return 0;
    MatrixSnapshot *s=mx_snapshot(device,audio,pages,bank);if(!s)return 0;
    int slot=-1;for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)if(s->controls.effect[i].id==fx_canvas.replace_id)slot=i;
    if(slot<0){free(s);return 0;}
    TsMatrixEffectControls old=s->controls.effect[slot];
    ts_matrix_effect_default(&s->controls.effect[slot],type,old.id);
    TsMatrixEffectControls *c=&s->controls.effect[slot];
    c->enabled=old.enabled;c->mix=old.mix;c->gain_db=old.gain_db;c->transition=old.transition;
    if(patch)c->sunvox=*patch;
    if(type!=TS_MATRIX_FX_COMPRESSOR) {
        int sc=TS_MATRIX_EFFECT_SC_BASE+slot;
        for(int i=0;i<TS_MATRIX_ROWS;++i){s->controls.row[i].matrix_gain[sc]=0;s->controls.delayed[i]&=~ts_matrix_feedback_bit(sc);}
        for(int i=0;i<s->count;++i)s->routes[i].route.matrix_gain[sc]=0;
    }
    int ok=ts_matrix_valid(&s->controls) && mx_apply(s,device,audio,ui,pages,bank);free(s);
    if(ok){w->effect_open=old.id;snprintf(w->status,sizeof(w->status),"REPLACED EFFECT / AUDIO LINKS, NAME, COLOR AND POSITION KEPT / CTRL+Z: UNDO");}
    return ok;
}
static void fx_replace_request(uint32_t id)
{
    fx_popup_open(710,250);fx_canvas.replace_id=id;
}
