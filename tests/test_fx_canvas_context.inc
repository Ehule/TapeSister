static void context_card(uint32_t id)
{
    fx_build(&pages,&bank);FxCanvasNode *n=instance(id);assert(n);
    int x=fx_x(n->x+fx_node_w(n)*.5f),y=fx_y(n->y+fx_node_h(n)*.5f);
    pointer(SDL_MOUSEBUTTONDOWN,x,y,SDL_BUTTON_RIGHT,1);pointer(SDL_MOUSEBUTTONUP,x,y,SDL_BUTTON_RIGHT,1);
    assert(fx_canvas.popup==FX_CONTEXT_MENU && fx_canvas.context_has_card && fx_canvas.context_card.tile==id);
}
static void context_actions(void)
{
    MatrixWindow *w=&matrix_window;edit_settle();MatrixSnapshot *saved=mx_snapshot(0,&audio,&pages,&bank);assert(saved);
    MatrixSnapshot *fixture=mx_snapshot(0,&audio,&pages,&bank);assert(fixture);
    TsMatrixControls *c=&fixture->controls;ts_matrix_default(c);c->enabled=1;c->next_effect_id=w->controls.next_effect_id;
    for(int i=0;i<TS_MATRIX_ROWS;++i)c->row[i]=(TsSourceRoute){.matrix_enabled=1};
    for(int i=0;i<fixture->count;++i)fixture->routes[i].route=(TsSourceRoute){.matrix_enabled=1};
    int types[]={TS_SISTER_FX_DELAY,TS_SISTER_FX_DISTORTION,TS_MATRIX_FX_COMPRESSOR,TS_SISTER_FX_REVERB};uint32_t ids[4];
    for(int i=0;i<4;++i){assert(ts_matrix_effect_add(c,types[i])==i);ids[i]=c->effect[i].id;}
    int base=TS_MATRIX_EFFECT_BASE;
    c->row[TS_MATRIX_MOSAIC].matrix_gain[base]=1000;c->row[TS_MATRIX_TRACKER].matrix_gain[base]=1000;
    c->row[base].matrix_gain[base+1]=1000;c->row[base].matrix_gain[base+2]=500;
    c->row[base+1].matrix_gain[base+2]=1000;c->row[base+1].matrix_gain[TS_MATRIX_NODES]=500;
    c->row[base+2].matrix_gain[TS_MATRIX_NODES]=1000;c->row[base+3].matrix_gain[TS_MATRIX_EFFECT_SC_BASE+2]=1000;
    MatrixRow inherited={0},parked={0};int found=0;
    for(int i=0;i<fixture->count;++i)if(fixture->routes[i].row.kind==3) {
        if(!found){inherited=fixture->routes[i].row;fixture->routes[i].route=(TsSourceRoute){0};found=1;}
        else if(fixture->routes[i].row.canvas!=inherited.canvas){parked=fixture->routes[i].row;fixture->routes[i].route.matrix_gain[base]=700;found=2;break;}
    }
    assert(found==2);ts_matrix_layout_default(&fixture->layout);
    assert(mx_apply(fixture,0,&audio,&ui,&pages,&bank));edit_settle();matrix_history_clear();fx_view(1,&pages,&bank);
    fx_canvas.fit=0;w->layout->canvas_zoom=.75f;w->layout->canvas_x=w->layout->canvas_y=0;
    for(int i=0;i<4;++i){TsFxCanvasPosition *p=fx_position(instance(ids[i])->row,1);p->x=60+300*(i%3);p->y=80+180*(i/3);}
    fx_build(&pages,&bank);void *engine=audio.sister.matrix_effects.current[1];
    fx_select(instance(ids[0]),0);fx_select(instance(ids[1]),1);context_card(ids[1]);assert(w->selection_count==2);
    int history=w->history_cursor;key(SDLK_ESCAPE,0);assert(!fx_canvas.popup && w->selection_count==2 && w->history_cursor==history);
    context_card(ids[1]);SDL_Surface *frame=rendered_frame();assert(frame);SDL_FreeSurface(frame);
    const char *preview=getenv("TS_FX_CONTEXT_PREVIEW");if(preview)screenshot(preview);
    /* Opening or dismissing a menu never inserts a layout/audio undo entry. */
    click(24,MATRIX_H-40,1);assert(!fx_canvas.popup && w->history_cursor==history && w->selection_count==2);
    fx_canvas.movement=inherited.canvas;fx_build(&pages,&bank);context_card(ids[1]);context_action(FX_CONTEXT_INPUTS);
    assert(!fx_canvas.movement && !fx_canvas.active_view && !fx_canvas.popup);
    assert(fx_chosen(instance(ids[0])->row) && fx_chosen(node(TS_MATRIX_MOSAIC)->row) && fx_chosen(node(TS_MATRIX_TRACKER)->row));
    assert(fx_chosen(inherited) && fx_chosen(parked));
    assert(!fx_chosen(instance(ids[1])->row) && !fx_chosen(instance(ids[2])->row) && !fx_chosen(instance(ids[3])->row));
    assert(w->history_cursor==history && audio.sister.matrix_effects.current[1]==engine);
    context_card(ids[1]);context_action(FX_CONTEXT_OUTPUTS);
    assert(w->selection_count==2 && fx_chosen(instance(ids[2])->row) && fx_chosen((MatrixRow){.kind=5,.index=TS_MATRIX_NODES}));
    assert(!fx_chosen(instance(ids[0])->row) && !fx_chosen(instance(ids[1])->row) && !fx_chosen(instance(ids[3])->row));
    context_card(ids[2]);context_action(FX_CONTEXT_INPUTS);assert(fx_chosen(instance(ids[3])->row)); /* sidechain */
    /* Output traversal follows all branches and deduplicates their shared sink. */
    context_card(ids[0]);context_action(FX_CONTEXT_OUTPUTS);assert(w->selection_count==3);
    c->row[base+2].matrix_gain[base]=250;c->delayed[base+2]=ts_matrix_feedback_bit(base);c->feedback=1;
    assert(mx_apply(fixture,0,&audio,&ui,&pages,&bank));edit_settle();fx_view(1,&pages,&bank);fx_canvas.fit=0;
    w->layout->canvas_zoom=.75f;w->layout->canvas_x=w->layout->canvas_y=0;
    for(int i=0;i<4;++i){TsFxCanvasPosition *p=fx_position(instance(ids[i])->row,1);p->x=60+300*(i%3);p->y=80+180*(i/3);}
    fx_build(&pages,&bank);context_card(ids[1]);context_action(FX_CONTEXT_OUTPUTS);
    assert(w->selection_count==3 && fx_chosen(instance(ids[0])->row) && fx_chosen(instance(ids[2])->row));
    assert(!fx_chosen(instance(ids[1])->row) && !fx_chosen(instance(ids[3])->row));
    context_card(ids[1]);context_action(FX_CONTEXT_INPUTS);
    assert(fx_chosen(instance(ids[0])->row) && fx_chosen(instance(ids[2])->row) && fx_chosen(instance(ids[3])->row));
    /* The same menu drives grouping and arrangement, each with one undo step. */
    fx_select(instance(ids[0]),0);fx_select(instance(ids[1]),1);context_card(ids[1]);history=w->history_cursor;
    context_action(FX_CONTEXT_GROUP);assert(w->layout->group_count==1 && fx_canvas.group_selected && w->history_cursor==history+1);
    context_card(ids[1]);context_action(FX_CONTEXT_UNGROUP);assert(!w->layout->group_count && w->history_cursor==history+2);
    fx_select(instance(ids[0]),0);fx_select(instance(ids[1]),1);context_card(ids[1]);history=w->history_cursor;
    context_action(FX_CONTEXT_NAMES);assert(w->history_cursor==history+1);key(SDLK_z,KMOD_CTRL);fx_build(&pages,&bank);
    context_card(ids[1]);context_action(FX_CONTEXT_PALETTE);assert(palette_window.visible);palette_window_hide(&ui,0);palette_window_close();
    context_card(ids[1]);key(SDLK_DOWN,0);key(SDLK_RETURN,0);assert(fx_canvas.popup==1);key(SDLK_ESCAPE,0);
    /* The canvas mouse pan remains available. */
    pointer(SDL_MOUSEBUTTONDOWN,500,500,SDL_BUTTON_MIDDLE,1);assert(w->drag==FX_DRAG_PAN);
    pointer(SDL_MOUSEBUTTONUP,500,500,SDL_BUTTON_MIDDLE,1);assert(!w->drag);
    free(fixture);assert(mx_apply(saved,0,&audio,&ui,&pages,&bank));free(saved);edit_settle();matrix_history_clear();fx_view(1,&pages,&bank);
}
