/* Move selected cable endpoints in one staged edit. Other sends and downstream
   chains stay intact; no disconnect is published before the new routes exist. */
static void fx_reroute_wires(int destination,SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,TsSamplePages *pages,TsInstrument *bank)
{
    MatrixWindow *w=&matrix_window;
    if(!mx_editable() || !w->controls.enabled || !fx_canvas.wires.count)return;
    if(!ts_matrix_destination_active(&w->controls,destination)) {
        snprintf(w->status,sizeof(w->status),"UNCHANGED: CTRL+SHIFT-CLICK A DESTINATION WITH AN INPUT.");return;
    }
    int selected=fx_canvas.wires.count;
    fx_validate_wires(pages,bank);
    if(selected!=fx_canvas.wires.count) {
        snprintf(w->status,sizeof(w->status),"UNCHANGED: SOME SELECTED CABLES NO LONGER EXIST. CHECK THE SELECTION AND TRY AGAIN.");return;
    }
    MatrixSnapshot *s=mx_snapshot(device,audio,pages,bank);
    if(!s){snprintf(w->status,sizeof(w->status),"UNCHANGED: CANNOT ALLOCATE ROUTING SNAPSHOT.");return;}
    const char *error=NULL;int moved=0;
    for(int i=0;i<selected && !error;++i) {
        FxCanvasWire wire=fx_canvas.wires.items[i];TsSourceRoute *r=fx_saved_route(s,wire.source);
        for(int j=0;j<i;++j)if(mx_same(wire.source,fx_canvas.wires.items[j].source))
            error="UNCHANGED: SELECT ONLY ONE CABLE PER SOURCE TO MOVE TO A SINGLE INPUT.";
        if(error)break;
        if(fx_control_source(wire.source)>=0){error="CONTROL LINK: CHANGE ITS DESTINATION IN THE MODULATOR EDITOR.";break;}
        int visual_pair=wire.source.kind==0 && mx_recall_destination(wire.destination) && mx_recall_destination(destination);
        if(!r || (!visual_pair && wire.destination!=TS_FX_MOSAIC_BUS && (r->mode==TS_SOURCE_INHERIT || mx_legacy_direct(*r)))) {
            error="UNCHANGED: CONVERT INHERITED / LEGACY SOURCE ROWS IN THE MATRIX FIRST.";break;
        }
        if(wire.destination==destination)continue;
        int inherited=wire.destination==TS_FX_MOSAIC_BUS;
        if(inherited)*r=(TsSourceRoute){.matrix_enabled=1,.pan=r->pan,.width=r->width};
        else if(!visual_pair && wire.source.kind!=2 && wire.source.kind!=4)*r=mx_display_route(*r);
        int gain=inherited?1000:r->matrix_gain[wire.destination];
        if(!gain){error="UNCHANGED: A SELECTED CABLE NO LONGER EXISTS. RESELECT IT AND TRY AGAIN.";break;}
        if(r->matrix_gain[destination]) {
            error="UNCHANGED: A SOURCE ALREADY CONNECTS TO THIS INPUT. ITS EXISTING CABLE WAS KEPT.";break;
        }
        if(!inherited)r->matrix_gain[wire.destination]=0;r->matrix_gain[destination]=gain;
        if(wire.source.kind==2 || wire.source.kind==4) {
            uint64_t *delayed=&s->controls.delayed[wire.source.index];
            int feedback=(*delayed&ts_matrix_feedback_bit(wire.destination))!=0;
            *delayed&=~ts_matrix_feedback_bit(wire.destination);
            if(feedback) {
                /* Outputs have no delayed-feedback port. Do not silently turn
                   a delayed connection into an ordinary one. */
                int node=destination==TS_MATRIX_SIDECHAIN?TS_MATRIX_COMPRESSOR:
                    destination>=TS_MATRIX_EFFECT_SC_BASE && destination<TS_MATRIX_INSTANCE_DESTINATIONS?
                    TS_MATRIX_EFFECT_BASE+destination-TS_MATRIX_EFFECT_SC_BASE:destination;
                if(!ts_matrix_is_processor(node)){error="UNCHANGED: DELAYED FEEDBACK NEEDS A PROCESSOR INPUT.";break;}
                *delayed|=ts_matrix_feedback_bit(destination);
            }
        }
        if(!ts_source_route_valid(r,wire.source.kind==0)){error="UNCHANGED: INVALID SOURCE ROUTING.";break;}
        /* A stale style at the new endpoint is not a new cable's color. Moving
           an existing entry needs no additional storage, even at capacity. */
        TsFxCableStyle old=fx_wire_style_key(wire.source,wire.destination),next=fx_wire_style_key(wire.source,destination);
        fx_wire_style_remove(&s->layout,fx_layout_wire_style_at(&s->layout,next));
        int at=fx_layout_wire_style_at(&s->layout,old);
        if(at>=0){next.color=s->layout.cables[at].color;s->layout.cables[at]=next;}
        ++moved;
    }
    /* Validate the final graph, after every old endpoint has been removed. No
       automatic feedback insertion or gain clamping during a bulk reroute. */
    if(!error && moved && !ts_matrix_valid(&s->controls))
        error="UNCHANGED: ROUTING WOULD CREATE A LOOP OR ALTER DELAYED FEEDBACK.";
    if(!error && moved && !mx_apply(s,device,audio,ui,pages,bank))
        error="UNCHANGED: THE NEW ROUTING COULD NOT BE APPLIED.";
    free(s);
    if(error){snprintf(w->status,sizeof(w->status),"%s",error);return;}
    if(!moved){snprintf(w->status,sizeof(w->status),"SELECTED CABLES ALREADY CONNECT TO THIS INPUT.");return;}
    for(int i=0;i<selected;++i) {
        fx_canvas.wires.items[i].destination=destination;
        fx_canvas.wires.items[i].target_id=fx_wire_target_id(destination);
    }
    w->destination=destination;w->effect_open=w->compressor_open=0;fx_primary_wire();
    snprintf(w->status,sizeof(w->status),"MOVED %d CABLE%s / GAINS AND COLORS KEPT / CTRL+Z: UNDO",moved,moved==1?"":"S");
}
