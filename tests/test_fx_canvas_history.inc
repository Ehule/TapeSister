static void history_keeps_view(float x,float y,float zoom,int view)
{
    TsMatrixLayout *l=matrix_window.layout;
    assert(l->canvas_x==x && l->canvas_y==y && l->canvas_zoom==zoom && l->canvas_view==view);
}
static void edit_history_without_navigation(void)
{
    MatrixWindow *w=&matrix_window;edit_settle();MatrixSnapshot *saved=mx_snapshot(0,&audio,&pages,&bank);assert(saved);
    MatrixSnapshot *fixture=mx_snapshot(0,&audio,&pages,&bank);assert(fixture);
    TsMatrixControls *c=&fixture->controls;ts_matrix_default(c);c->enabled=1;c->next_effect_id=w->controls.next_effect_id;
    for(int i=0;i<TS_MATRIX_ROWS;++i)c->row[i]=(TsSourceRoute){.matrix_enabled=1};
    for(int i=0;i<fixture->count;++i)fixture->routes[i].route=(TsSourceRoute){.matrix_enabled=1};
    assert(ts_matrix_effect_add(c,TS_SISTER_FX_DELAY)==0);assert(ts_matrix_effect_add(c,TS_SISTER_FX_DISTORTION)==1);
    uint32_t id=c->effect[0].id;int base=TS_MATRIX_EFFECT_BASE;
    c->row[TS_MATRIX_MOSAIC].matrix_gain[base]=1000;c->row[base].matrix_gain[base+1]=1000;c->row[base+1].matrix_gain[TS_MATRIX_NODES]=1000;
    ts_matrix_layout_default(&fixture->layout);assert(mx_apply(fixture,0,&audio,&ui,&pages,&bank));edit_settle();matrix_history_clear();
    fx_view(1,&pages,&bank);fx_canvas.fit=0;TsMatrixLayout *l=w->layout;
    l->canvas_view=1;l->canvas_x=l->canvas_y=0;l->canvas_zoom=.5f;fx_build(&pages,&bank);fx_select(instance(id),0);
    key(SDLK_m,0);assert(!w->controls.effect[0].enabled && w->history_count==1);
    /* More wheel events than the history capacity must not evict the edit. */
    for(int i=0;i<48;++i)edit_wheel(500,400,i%2?-1:1,0);
    pointer(SDL_MOUSEBUTTONDOWN,500,400,SDL_BUTTON_MIDDLE,1);pointer(SDL_MOUSEMOTION,540,430,0,0);
    pointer(SDL_MOUSEBUTTONUP,540,430,SDL_BUTTON_MIDDLE,1);
    key(SDLK_z,0);key(SDLK_z,KMOD_SHIFT);key(SDLK_HOME,0);toolbar_click(FX_TOOL_FIT);
    MatrixRow target=instance(id)->row;fx_context_open(400,220,&target);context_action(FX_CONTEXT_FIT);
    assert(w->history_count==1 && w->history_cursor==1);
    float x=l->canvas_x,y=l->canvas_y,zoom=l->canvas_zoom;int view=l->canvas_view;
    key(SDLK_z,KMOD_CTRL);assert(w->controls.effect[0].enabled && w->history_cursor==0);history_keeps_view(x,y,zoom,view);
    /* Navigating after undo must retain redo, at the new camera position. */
    edit_wheel(500,400,1,0);x=l->canvas_x;y=l->canvas_y;zoom=l->canvas_zoom;
    assert(w->history_count==1 && w->history_cursor==0);
    key(SDLK_z,KMOD_CTRL|KMOD_SHIFT);assert(!w->controls.effect[0].enabled && w->history_cursor==1);history_keeps_view(x,y,zoom,view);
    key(SDLK_z,KMOD_CTRL);key(SDLK_y,KMOD_CTRL);assert(!w->controls.effect[0].enabled);history_keeps_view(x,y,zoom,view);
    key(SDLK_z,KMOD_CTRL);assert(w->controls.effect[0].enabled);edit_settle();
    /* Parameter and cable-gain wheel events remain real edits. */
    fx_build(&pages,&bank);fx_select(instance(id),0);w->effect_open=id;fx_inspector_build();
    float value=w->controls.effect[0].a;edit_wheel(fx_inspector_x()+60,FX_INSPECT_FIRST+20,value>.5f?-1:1,0);
    assert(w->controls.effect[0].a!=value && w->history_cursor==1);
    edit_wheel(500,400,1,0);x=l->canvas_x;y=l->canvas_y;zoom=l->canvas_zoom;
    key(SDLK_z,KMOD_CTRL);assert(w->controls.effect[0].a==value);history_keeps_view(x,y,zoom,view);
    fx_build(&pages,&bank);fx_clear_selection();fx_select_wire(instance(id)->row,base+1,0);int gain=w->controls.row[base].matrix_gain[base+1];
    edit_wheel(500,400,-1,KMOD_SHIFT);assert(w->controls.row[base].matrix_gain[base+1]!=gain && w->history_cursor==1);
    edit_wheel(500,400,-1,0);x=l->canvas_x;y=l->canvas_y;zoom=l->canvas_zoom;
    key(SDLK_z,KMOD_CTRL);assert(w->controls.row[base].matrix_gain[base+1]==gain);history_keeps_view(x,y,zoom,view);
    /* Position edits undo in world coordinates without moving the camera. */
    fx_build(&pages,&bank);fx_clear_wires();fx_select(instance(id),0);TsFxCanvasPosition *p=fx_position(instance(id)->row,1);float old_x=p->x;
    fx_canvas.mouse_x=400;fx_canvas.mouse_y=300;key(SDLK_g,0);pointer(SDL_MOUSEMOTION,480,300,0,0);key(SDLK_RETURN,0);
    assert(p->x!=old_x && w->history_cursor==1);float moved_x=p->x;
    edit_wheel(500,400,1,0);x=l->canvas_x;y=l->canvas_y;zoom=l->canvas_zoom;
    key(SDLK_z,KMOD_CTRL);assert(p->x==old_x);history_keeps_view(x,y,zoom,view);
    key(SDLK_y,KMOD_CTRL);assert(p->x==moved_x);history_keeps_view(x,y,zoom,view);
    /* A deliberately chosen context-menu Undo has the same behavior. */
    fx_context_open(400,220,&target);context_action(FX_CONTEXT_UNDO);assert(p->x==old_x);history_keeps_view(x,y,zoom,view);
    fx_context_open(400,220,&target);context_action(FX_CONTEXT_REDO);assert(p->x==moved_x);history_keeps_view(x,y,zoom,view);
    free(fixture);assert(mx_apply(saved,0,&audio,&ui,&pages,&bank));free(saved);edit_settle();matrix_history_clear();fx_view(1,&pages,&bank);
}
