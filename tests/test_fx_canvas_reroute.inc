static void reroute_click(int destination,int sidechain)
{
    fx_build(&pages,&bank);assert(fx_canvas.dest[destination]>=0);
    FxCanvasNode *n=&fx_canvas.node[fx_canvas.dest[destination]];
    SDL_SetModState(KMOD_CTRL|KMOD_SHIFT);
    click(fx_x(n->x)+(sidechain?0:35),fx_y(n->y+fx_node_h(n)*(sidechain?.8f:.5f)),1);
    SDL_SetModState(KMOD_NONE);
}
static void reroute_cables(void)
{
    MatrixWindow *w=&matrix_window;TsMosaicGallery *g=pages.gallery;char error[256];
    assert(g && g->count==1);edit_settle();matrix_history_clear();
    MatrixSnapshot *saved=mx_snapshot(0,&audio,&pages,&bank);assert(saved);
    assert(ts_gallery_add(g,error,sizeof(error)));
    float data[16]={.1f};TsSample sample={.data=data,.frames=16,.channels=1,.sample_rate=48000};
    TsMosaicEvent *cards[96];TsSourceRoute original[96];MatrixRow rows[96];
    for(int c=0;c<2;++c) {
        TsMosaicSource *source=ts_mosaic_source(g->canvas[c].scene,&sample,error,sizeof(error));assert(source);
        for(int i=0;i<48;++i){cards[c*48+i]=ts_mosaic_add(g->canvas[c].scene,source,i*2,i*40);assert(cards[c*48+i]);}
    }
    MatrixSnapshot *fixture=mx_snapshot(0,&audio,&pages,&bank);assert(fixture);
    TsMatrixControls *c=&fixture->controls;ts_matrix_default(c);c->enabled=1;c->next_effect_id=w->controls.next_effect_id;
    int comp=ts_matrix_effect_add(c,TS_MATRIX_FX_COMPRESSOR),board=ts_matrix_effect_add(c,TS_MATRIX_FX_PEDALBOARD),delay=ts_matrix_effect_add(c,TS_SISTER_FX_DELAY);
    assert(comp>=0 && board>=0 && delay>=0);
    int dc=TS_MATRIX_EFFECT_BASE+comp,db=TS_MATRIX_EFFECT_BASE+board,dd=TS_MATRIX_EFFECT_BASE+delay;
    int sc=TS_MATRIX_EFFECT_SC_BASE+comp,out=TS_MATRIX_NODES+1;
    uint32_t cid=c->effect[comp].id,bid=c->effect[board].id,did=c->effect[delay].id;
    assert(ts_matrix_connect(c,dc,TS_MATRIX_MAIN,1000)==1 && ts_matrix_connect(c,db,TS_MATRIX_MAIN,1000)==1);
    assert(ts_matrix_connect(c,dd,TS_MATRIX_MAIN,1000)==1);
    for(int i=0;i<fixture->count;++i)fixture->routes[i].route=(TsSourceRoute){.matrix_enabled=1};
    ts_matrix_layout_default(&fixture->layout);fixture->layout.canvas_snap=0;
    assert(mx_apply(fixture,0,&audio,&ui,&pages,&bank));free(fixture);edit_settle();
    for(int i=0;i<96;++i) {
        TsSourceRoute *r=&cards[i]->output_route;*r=(TsSourceRoute){.matrix_enabled=1,.pan=i%41-20,.width=i%31};
        r->matrix_gain[dc]=100+i*8;
        if(i%2==0)r->matrix_gain[out]=333+i;
        if(i%3==0)r->matrix_gain[sc]=222;
        original[i]=*r;
    }
    mx_bind_mosaic(&ui,&pages);mx_rows(&pages,&bank);fx_view(1,&pages,&bank);fx_canvas.fit=0;
    TsMatrixLayout *l=w->layout;l->canvas_x=l->canvas_y=0;l->canvas_zoom=1;l->canvas_view=1;
    for(int i=0;i<fx_canvas.count;++i){TsFxCanvasPosition *p=fx_position(fx_canvas.node[i].row,1);if(p)p->x=p->y=-4000;}
    TsFxCanvasPosition *cp=fx_position(instance(cid)->row,1),*bp=fx_position(instance(bid)->row,1),*dp=fx_position(instance(did)->row,1);
    cp->x=500;cp->y=100;bp->x=700;bp->y=300;dp->x=400;dp->y=450;
    fx_build(&pages,&bank);
    for(int i=0;i<96;++i) {
        rows[i]=gallery_card(g->canvas[i/48].id,cards[i]->id);
        assert(fx_select_wire(rows[i],dc,0));
        if(i<95)fx_wire_set_color(fx_canvas.wires.items[i],fx_swatch(i%16));
    }
    /* A stale destination style must not recolor an uncolored moved cable. */
    fx_wire_set_color((FxCanvasWire){rows[95],db,bid},fx_swatch(5));
    fx_wire_set_color((FxCanvasWire){rows[0],out,0},fx_swatch(9));
    /* At the color-table limit the operation still only relocates entries. */
    while(l->cable_count<TS_FX_CABLE_STYLES) {
        int i=l->cable_count;l->cables[l->cable_count++]=(TsFxCableStyle){.source=100000u+(unsigned)i,.kind=1,.destination=TS_MATRIX_MAIN,.color=0xffaabbcc};
    }
    /* Select the 96 incoming cables through a real Ctrl-drag near the old
       input, without selecting that compressor's parallel sidechain cables. */
    fx_clear_wires();int input_x=fx_x(cp->x),input_y=fx_y(cp->y+FX_NODE_H*.5f);
    SDL_SetModState(KMOD_CTRL);
    pointer(SDL_MOUSEBUTTONDOWN,input_x-30,input_y-6,SDL_BUTTON_LEFT,1);
    pointer(SDL_MOUSEMOTION,input_x-15,input_y+6,0,0);
    pointer(SDL_MOUSEBUTTONUP,input_x-15,input_y+6,SDL_BUTTON_LEFT,1);SDL_SetModState(KMOD_NONE);
    assert(fx_canvas.wires.count==96 && strstr(w->status,"CTRL+SHIFT-CLICK"));
    fx_select(node(TS_MATRIX_MOSAIC),0); /* Mixed selection: cables take precedence. */
    MatrixSnapshot *before=mx_snapshot(0,&audio,&pages,&bank);assert(before);
    TsMatrixEffectNode *live=audio.sister.matrix_effects.current[comp];int undo=w->history_cursor;
    reroute_click(db,0);
    assert(w->history_cursor==undo+1 && fx_canvas.wires.count==96 && w->selection_count==1);
    assert(audio.sister.matrix_effects.current[comp]==live && !memcmp(&before->controls,&w->controls,sizeof(w->controls)));
    for(int i=0;i<96;++i) {
        TsSourceRoute expected=original[i];expected.matrix_gain[db]=expected.matrix_gain[dc];expected.matrix_gain[dc]=0;
        assert(!memcmp(&expected,&cards[i]->output_route,sizeof(expected)));
        assert(fx_wire_selected(rows[i],db)>=0 && !fx_wire_color(rows[i],dc));
        assert(fx_wire_color(rows[i],db)==(i<95?fx_swatch(i%16):0));
    }
    assert(fx_wire_color(rows[0],out)==fx_swatch(9) && l->cable_count==TS_FX_CABLE_STYLES-1);view_codec();
    MatrixSnapshot *after=mx_snapshot(0,&audio,&pages,&bank);assert(after);
    key(SDLK_z,KMOD_CTRL);MatrixSnapshot *check=mx_snapshot(0,&audio,&pages,&bank);assert(mx_equal(before,check));free(check);
    key(SDLK_y,KMOD_CTRL);check=mx_snapshot(0,&audio,&pages,&bank);assert(mx_equal(after,check));free(check);
    /* Clicking the current input is a no-op, without a new history entry. */
    fx_clear_wires();for(int i=0;i<96;++i)assert(fx_select_wire(rows[i],db,0));
    undo=w->history_cursor;reroute_click(db,0);assert(w->history_cursor==undo);
    /* A conflict encountered last rolls back all 95 earlier staged moves. */
    cards[95]->output_route.matrix_gain[dc]=271;
    MatrixSnapshot *conflict=mx_snapshot(0,&audio,&pages,&bank);assert(conflict);
    reroute_click(dc,0);check=mx_snapshot(0,&audio,&pages,&bank);
    assert(mx_equal(conflict,check) && w->history_cursor==undo && strstr(w->status,"ALREADY CONNECTS"));free(conflict);free(check);
    cards[95]->output_route.matrix_gain[dc]=0;
    /* A stale effect identity cannot redirect a cable to a reused slot. */
    fx_clear_wires();assert(fx_select_wire(rows[1],db,0));fx_canvas.wires.items[0].target_id=UINT32_MAX;
    conflict=mx_snapshot(0,&audio,&pages,&bank);assert(conflict);reroute_click(dc,0);
    check=mx_snapshot(0,&audio,&pages,&bank);assert(mx_equal(conflict,check) && strstr(w->status,"NO LONGER EXIST"));free(conflict);free(check);
    /* Two cables from one source cannot silently merge their gains/colors. */
    fx_clear_wires();assert(fx_select_wire(rows[0],db,0) && fx_select_wire(rows[0],out,0));
    conflict=mx_snapshot(0,&audio,&pages,&bank);assert(conflict);reroute_click(dc,0);
    check=mx_snapshot(0,&audio,&pages,&bank);assert(mx_equal(conflict,check) && strstr(w->status,"ONE CABLE PER SOURCE"));free(conflict);free(check);
    /* The lower purple input is explicitly addressable; parallel output stays. */
    fx_clear_selection();fx_clear_wires();assert(fx_select_wire(rows[1],db,0));
    reroute_click(sc,1);assert(!cards[1]->output_route.matrix_gain[db] && cards[1]->output_route.matrix_gain[sc]==original[1].matrix_gain[dc]);
    assert(fx_wire_color(rows[1],sc)==fx_swatch(1));key(SDLK_z,KMOD_CTRL);
    /* Only visible movement cables are selected, and parked movement routes
       retain their original identity even when local card IDs coincide. */
    fx_canvas.movement=g->canvas[0].id;fx_build(&pages,&bank);fx_clear_wires();
    for(int i=0;i<48;++i)assert(fx_select_wire(rows[i],db,0));reroute_click(dc,0);
    for(int i=0;i<96;++i)assert(cards[i]->output_route.matrix_gain[i<48?dc:db]==original[i].matrix_gain[dc]);
    key(SDLK_z,KMOD_CTRL);fx_canvas.movement=0;fx_build(&pages,&bank);fx_clear_wires();
    /* Cycles are refused atomically, even with feedback enabled: bulk moves
       never insert a delay or clamp gain as a side effect. */
    w->controls.row[db].matrix_gain[dc]=700;w->controls.feedback=1;mx_commit_controls(0,&audio,&ui);fx_build(&pages,&bank);
    assert(fx_select_wire(rows[1],db,0) && fx_select_wire(instance(cid)->row,TS_MATRIX_MAIN,0));
    conflict=mx_snapshot(0,&audio,&pages,&bank);assert(conflict);reroute_click(db,0);
    check=mx_snapshot(0,&audio,&pages,&bank);assert(mx_equal(conflict,check) && strstr(w->status,"LOOP"));free(conflict);free(check);
    /* Existing delayed edges keep their delay and sub-unity gain when moved. */
    w->controls.row[dc].matrix_gain[dd]=400;w->controls.delayed[dc]|=ts_matrix_feedback_bit(dd);
    assert(ts_matrix_valid(&w->controls));mx_commit_controls(0,&audio,&ui);fx_build(&pages,&bank);
    fx_clear_wires();assert(fx_select_wire(instance(cid)->row,dd,0));reroute_click(db,0);
    assert(!w->controls.row[dc].matrix_gain[dd] && w->controls.row[dc].matrix_gain[db]==400);
    assert(!(w->controls.delayed[dc]&ts_matrix_feedback_bit(dd)) && (w->controls.delayed[dc]&ts_matrix_feedback_bit(db)));
    key(SDLK_z,KMOD_CTRL);
    /* Output destinations work for ordinary cables. */
    FxCanvasNode *output=&fx_canvas.node[fx_canvas.dest[TS_MATRIX_NODES]];
    TsFxCanvasPosition *op=fx_position(output->row,1);op->x=700;op->y=500;fx_build(&pages,&bank);
    fx_clear_wires();assert(fx_select_wire(instance(cid)->row,dd,0));
    conflict=mx_snapshot(0,&audio,&pages,&bank);assert(conflict);reroute_click(TS_MATRIX_NODES,0);
    check=mx_snapshot(0,&audio,&pages,&bank);assert(mx_equal(conflict,check) && strstr(w->status,"DELAYED FEEDBACK"));free(conflict);free(check);
    fx_clear_wires();assert(fx_select_wire(rows[1],db,0));reroute_click(TS_MATRIX_NODES,0);
    assert(!cards[1]->output_route.matrix_gain[db] && cards[1]->output_route.matrix_gain[TS_MATRIX_NODES]==original[1].matrix_gain[dc]);
    /* Track sources use the same operation, with their row controls retained. */
    pages.tracker.lanes[0].output_route=(TsSourceRoute){.matrix_enabled=1,.pan=-32,.width=51};
    pages.tracker.lanes[0].output_route.matrix_gain[dc]=743;mx_rows(&pages,&bank);fx_build(&pages,&bank);
    fx_clear_wires();assert(fx_select_wire((MatrixRow){.kind=0,.index=0},dc,0));reroute_click(db,0);
    TsSourceRoute *track=&pages.tracker.lanes[0].output_route;
    assert(!track->matrix_gain[dc] && track->matrix_gain[db]==743 && track->pan==-32 && track->width==51);
    /* Restore the fixture before the remaining controller tests. */
    free(before);free(after);assert(mx_apply(saved,0,&audio,&ui,&pages,&bank));free(saved);edit_settle();
    for(int i=0;i<48;++i)ts_mosaic_delete(g->canvas[0].scene,cards[i]->id);
    assert(ts_gallery_remove(g,1,error,sizeof(error)));matrix_history_clear();fx_view(1,&pages,&bank);
    puts("96-cable rerouting, parallel sends/colors, undo/redo, collisions, filters, sidechains and feedback passed.");
}
