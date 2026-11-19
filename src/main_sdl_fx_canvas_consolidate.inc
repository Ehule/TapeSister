/* A rack is serial. Validate its entire boundary (including parked Gallery
   sources) before replacing anything. Reuse the entry identity and input port. */
static void fx_consolidate_notice(const char *reason)
{
    snprintf(matrix_window.status,sizeof(matrix_window.status),"CONSOLIDATION FAILED: %s",reason);
    SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_WARNING,"Cannot consolidate effects",reason,matrix_window.window);
}
static int fx_consolidate_slot_type(int type)
{
    if(type>=TS_SISTER_FX_REVERB && type<=TS_SISTER_FX_TRANSIENT)return type;
    return type==TS_MATRIX_FX_EQ?TS_SISTER_FX_EQ:type==TS_MATRIX_FX_AUTOPAN?TS_SISTER_FX_AUTOPAN:type==TS_MATRIX_FX_FILTER?TS_SISTER_FX_FILTER:type==TS_MATRIX_FX_DISPERSION?TS_SISTER_FX_DISPERSION:
        type==TS_MATRIX_FX_RESONATOR?TS_SISTER_FX_RESONATOR:type==TS_MATRIX_FX_SUNVOX?TS_SISTER_FX_SUNVOX:0;
}
static int fx_consolidate_member(uint64_t id,const uint32_t *ids,int count)
{for(int i=0;i<count;++i)if(ids[i]==id)return 1;return 0;}
static const char *fx_consolidate_plan(MatrixSnapshot *s,const int *slots,int count,int meta)
{
    TsMatrixControls *c=&s->controls;int next[TS_MATRIX_EFFECT_CAPACITY],incoming[TS_MATRIX_EFFECT_CAPACITY]={0};
    uint32_t ids[TS_SISTER_FX_SLOT_COUNT];
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)next[i]=-1;
    for(int i=0;i<count;++i)ids[i]=c->effect[slots[i]].id;
    for(int i=0;i<count;++i) {
        int slot=slots[i],n=TS_MATRIX_EFFECT_BASE+slot;
        if(c->delayed[n])return "The selection contains delayed feedback. Keep its routing on the canvas.";
        for(int row=0;row<TS_MATRIX_ROWS;++row)if(c->delayed[row]&ts_matrix_feedback_bit(n))
            return "A delayed feedback cable enters the selection. Consolidation would change its timing.";
        for(int j=0;j<count;++j)if(c->row[n].matrix_gain[TS_MATRIX_EFFECT_BASE+slots[j]]) {
            int dst=slots[j];if(next[slot]>=0 || ++incoming[dst]>1)
                return "Parallel or merging branches cannot fit a serial board. Select one unbranched chain.";
            if(c->row[n].matrix_gain[TS_MATRIX_EFFECT_BASE+dst]!=1000)
                return "An internal cable has a non-unity gain. Set internal cables to 0 dB before consolidating.";
            next[slot]=dst;
        }
    }
    int first=-1;
    for(int i=0;i<count;++i)if(!incoming[slots[i]]) {
        if(first>=0)return "The effects are not one serial chain. Parallel effects or effects with different inputs must stay separate.";
        first=slots[i];
    }
    if(first<0)return "The selected effects form a loop. A serial board cannot preserve that routing.";
    int order[TS_SISTER_FX_SLOT_COUNT],at=first;
    for(int i=0;i<count;++i) {
        if(at<0)return "The selection contains separate chains or a loop. Select one connected serial chain.";
        order[i]=at;at=next[at];
    }
    if(at>=0)return "The selected effects form a loop. A serial board cannot preserve that routing.";
    int last=order[count-1];
    for(int i=0;i<count;++i) {
        int slot=order[i],n=TS_MATRIX_EFFECT_BASE+slot,sc=TS_MATRIX_EFFECT_SC_BASE+slot;
        for(int row=0;row<TS_MATRIX_ROWS;++row) {
            if(c->row[row].matrix_gain[sc])return "A sidechain enters the selection. Keep this routing on the canvas.";
            if(slot!=first && c->row[row].matrix_gain[n] && row!=TS_MATRIX_EFFECT_BASE+order[i-1])
                return "Different inputs feed the selected effects. Only the first effect may have inputs from outside the chain.";
        }
        for(int r=0;r<s->count;++r) {
            if(s->routes[r].route.matrix_gain[sc])return "A source feeds a sidechain in the selection. Keep this routing on the canvas.";
            if(slot!=first && s->routes[r].route.matrix_gain[n])
                return "Different input cards or tracks feed the selected effects. Only the first effect may have outside inputs.";
        }
        if(slot!=last) {
            if(c->row[n].pan || c->row[n].width)return "An internal effect output has pan or width processing. Keep it on the canvas.";
            for(int d=0;d<TS_MATRIX_DESTINATIONS;++d)if(c->row[n].matrix_gain[d] && d!=TS_MATRIX_EFFECT_BASE+next[slot])
                return "An intermediate effect has an extra output branch. Only the last effect may send outside the chain.";
        }
    }
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)if(c->effect[i].id && c->effect[i].type==TS_MATRIX_FX_MODULATOR)
        for(unsigned j=0;j<c->effect[i].modulation.map_count;++j)
            if(fx_consolidate_member(c->effect[i].modulation.map[j].target,ids,count))
                return "A modulator targets a selected effect. Its parameter mappings cannot yet be moved into a rack.";
    TsMatrixEffectControls *rack=malloc(sizeof(*rack));if(!rack)return "Not enough memory to prepare the consolidated card.";
    ts_matrix_effect_default(rack,meta?TS_MATRIX_FX_META:TS_MATRIX_FX_PEDALBOARD,c->effect[first].id);
    rack->transition=c->effect[first].transition;rack->board.transition=rack->transition;
    if(meta){ts_meta_default(&rack->meta);snprintf(rack->meta.name,sizeof(rack->meta.name),"CONSOLIDATED FX");}
    const char *error=NULL;
    for(int i=0;i<count && !error;++i) {
        const TsMatrixEffectControls *old=&c->effect[order[i]];int type=fx_consolidate_slot_type(old->type);
        if(!type){error="The selection includes a compressor, modulator, or existing rack. These cannot be nested in this container.";break;}
        float transition=meta?ts_sister_fx_transition_normalized(12):rack->board.transition;
        if(fabsf(old->transition-transition)>1e-6f) {
            error=meta?"Meta FX uses 12 ms stage bypass fades. Use a Pedalboard to retain a different shared fade time.":
                "The effects have different bypass fade times. A Pedalboard needs one shared fade time.";break;
        }
        if(meta && type==TS_SISTER_FX_EQ){error="This EQ needs its five-band editor. Choose Pedalboard to preserve the curve.";break;}
        if(meta && type==TS_SISTER_FX_SUNVOX){error="Meta FX cannot hold SunVox effects. Choose Pedalboard in the consolidate popup instead.";break;}
        if(meta && ((type==TS_SISTER_FX_TRANSIENT && old->transient_states.captured) ||
           (type==TS_SISTER_FX_FILTER && (old->filter.captured || old->filter.locks)))) {
            error="Meta FX cannot preserve these captured states or filter locks. Choose Pedalboard in the consolidate popup instead.";break;
        }
        if(meta) {
            TsMetaStage *stage=&rack->meta.stage[i];ts_meta_stage_default(stage,type);stage->enabled=old->enabled;if(type==TS_SISTER_FX_AUTOPAN)stage->autopan=old->autopan;
            stage->value[0]=old->a;stage->value[1]=old->b;stage->value[2]=old->c;stage->value[3]=old->mix;stage->value[4]=(old->gain_db+12)/24;
            stage->power=old->transient_power;memcpy(stage->filter,old->filter.value,sizeof(stage->filter));
        } else {
            TsSisterFxSlotControls *slot=&rack->board.slot[i];
            slot->type=(TsSisterFxType)type;slot->enabled=old->enabled;slot->parameter_a=old->a;slot->parameter_b=old->b;slot->parameter_c=old->c;
            slot->eq=old->eq;slot->mix=old->mix;slot->gain_db=old->gain_db;slot->filter=old->filter;slot->transient_power=old->transient_power;
            if(type==TS_SISTER_FX_AUTOPAN)slot->autopan=old->autopan;slot->transient_states=old->transient_states;slot->sunvox=old->sunvox;
        }
    }
    if(error){free(rack);return error;}
    if(meta)rack->meta.count=count;else rack->board.count=SDL_max(TS_SISTER_FX_SLOT_MIN,count);
    TsSourceRoute output=c->row[TS_MATRIX_EFFECT_BASE+last];
    uint32_t first_id=c->effect[first].id,last_id=c->effect[last].id;
    /* Incoming entry cables keep their port and identity; move only the exit. */
    for(int i=0;i<count;++i)if(order[i]!=first)ts_matrix_effect_remove(c,order[i]);
    c->effect[first]=*rack;free(rack);c->row[TS_MATRIX_EFFECT_BASE+first]=output;
    for(int i=s->layout.canvas_count;i-->0;) {
        TsFxCanvasPosition *p=&s->layout.canvas[i];
        if(p->kind!=4 || !fx_consolidate_member(p->id,ids,count))continue;
        if(p->id==first_id){snprintf(p->name,sizeof(p->name),"%s",meta?"Consolidated Meta FX":"Consolidated Pedalboard");continue;}
        memmove(p,p+1,(size_t)(--s->layout.canvas_count-i)*sizeof(*p));memset(s->layout.canvas+s->layout.canvas_count,0,sizeof(*p));
    }
    for(int i=s->layout.cable_count;i-->0;) {
        TsFxCableStyle *p=&s->layout.cables[i];
        int from=p->kind==4 && fx_consolidate_member(p->source,ids,count),to=fx_consolidate_member(p->target,ids,count);
        if((from && to) || (from && p->source!=last_id) || (to && p->target!=first_id))fx_wire_style_remove(&s->layout,i);
        else if(from)p->source=first_id;
    }
    return ts_matrix_valid(c) && ts_matrix_layout_valid(&s->layout)?NULL:"The resulting routing or layout failed validation. The original selection was kept.";
}
static void fx_consolidate(int meta,SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,TsSamplePages *pages,TsInstrument *bank)
{
    MatrixWindow *w=&matrix_window;if(!mx_editable()){fx_consolidate_notice("Finish the current edit or return from Original audition before consolidating.");return;}
    fx_build(pages,bank);int slots[TS_SISTER_FX_SLOT_COUNT],count=0,limit=meta?TS_META_STAGES:TS_SISTER_FX_SLOT_COUNT;
    for(int i=0;i<fx_canvas.count;++i)if(fx_object_selected(&fx_canvas.node[i])) {
        MatrixRow row=fx_canvas.node[i].row;
        if(row.kind!=4){fx_consolidate_notice("Select added effect cards only. Sources, outputs and fixed processors must stay outside the container.");return;}
        if(count==limit){fx_consolidate_notice(meta?"Meta FX holds at most 8 stages. Select fewer effects or use a Pedalboard (up to 20).":"A Pedalboard holds at most 20 effects. Select a smaller chain.");return;}
        slots[count++]=ts_matrix_effect_index(row.index);
    }
    if(count<2){fx_consolidate_notice("Select at least two effects in one serial chain, or select their group rug.");return;}
    for(size_t i=0;i<ui->config.midi_map.count;++i) {
        unsigned id;int field;char tail;
        if(sscanf(ui->config.midi_map.mappings[i].target,"matrix.filter.%u.%d%c",&id,&field,&tail)!=2)continue;
        for(int j=0;j<count;++j)if(w->controls.effect[slots[j]].id==id) {
            fx_consolidate_notice("A selected filter has MIDI mappings. Remove or reassign those mappings before consolidating.");return;
        }
    }
    MatrixSnapshot *s=mx_snapshot(device,audio,pages,bank);if(!s){fx_consolidate_notice("Not enough memory to prepare a routing snapshot.");return;}
    const char *error=fx_consolidate_plan(s,slots,count,meta);uint32_t id=0;
    if(!error) {
        for(int i=0;i<count;++i)if(s->controls.effect[slots[i]].id)id=s->controls.effect[slots[i]].id;
        if(!mx_apply(s,device,audio,ui,pages,bank))error="The new effect container could not be prepared. The original cards and cables were kept.";
    }
    free(s);if(error){fx_consolidate_notice(error);return;}
    fx_clear_wires();fx_clear_selection();w->effect_open=id;w->compressor_open=0;
    fx_build(pages,bank);for(int i=0;i<fx_canvas.count;++i)if(fx_canvas.node[i].row.kind==4 && fx_canvas.node[i].row.tile==id){fx_select(&fx_canvas.node[i],0);break;}
    snprintf(w->status,sizeof(w->status),"CONSOLIDATED %d EFFECTS INTO %s / DOUBLE-CLICK: EDIT / CTRL+Z: UNDO",count,meta?"META FX":"PEDALBOARD");
}
static void fx_consolidate_choose(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,TsSamplePages *pages,TsInstrument *bank)
{
    const SDL_MessageBoxButtonData buttons[]={
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT,0,"Cancel"},
        {0,2,"Meta FX"},
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT,1,"Pedalboard"}
    };
    const SDL_MessageBoxData dialog={.flags=SDL_MESSAGEBOX_INFORMATION,.window=matrix_window.window,
        .title="Consolidate effects",.message="Choose a container for the selected serial effect chain.\n\n"
        "Pedalboard: native and SunVox effects, up to 20 stages.\n"
        "Meta FX: native effects, up to 8 stages.",.numbuttons=3,.buttons=buttons};
    int choice=0;
    if(SDL_ShowMessageBox(&dialog,&choice)<0) {
        char reason[256];snprintf(reason,sizeof(reason),"Could not open the consolidate popup: %.190s",SDL_GetError());
        fx_consolidate_notice(reason);return;
    }
    if(choice==1 || choice==2)fx_consolidate(choice==2,device,audio,ui,pages,bank);
    else snprintf(matrix_window.status,sizeof(matrix_window.status),"CONSOLIDATION CANCELLED.");
}
