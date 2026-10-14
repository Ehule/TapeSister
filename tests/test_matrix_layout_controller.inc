static void matrix_key(SDL_Keycode key,SDL_Keymod modifiers)
{
    SDL_Event e;SDL_zero(e);e.type=SDL_KEYDOWN;e.key.windowID=matrix_window.id;
    e.key.keysym.sym=key;e.key.keysym.mod=modifiers;
    assert(matrix_event(&e,window,0,&a,&u,&pages,&bank));
}
static void matrix_layout_controller(const char *directory)
{
    MatrixWindow *w=&matrix_window;ts_matrix_layout_default(&a.sister.matrix_layout);
    matrix_history_clear();assert(matrix_show(0,&a,&u,&pages,&bank,-1,-1));
    w->compressor_open=0;w->column=0;
    TsRoutingMatrix *dsp=malloc(sizeof(*dsp));assert(dsp);*dsp=a.sister.matrix;
    TsSourceRoute first=pages.tracker.lanes[0].output_route;
    uint64_t hash=paged_project_state_hash(&pages,&bank,NULL,&a.sister,NULL);
    assert(w->rows[0].kind==0 && w->rows[0].index==0);
    matrix_click(80,MATRIX_Y+12,SDL_BUTTON_LEFT);
    matrix_key(SDLK_DOWN,KMOD_SHIFT);
    assert(w->selected==1 && w->anchor==1 && w->rows[1].kind==0 && w->rows[1].index==0);
    assert(w->rows[0].index==1 && mx_chosen(1) && w->history_count==1);
    assert(hash!=paged_project_state_hash(&pages,&bank,NULL,&a.sister,NULL));
    assert(!memcmp(dsp,&a.sister.matrix,sizeof(*dsp)));
    assert(!memcmp(&first,&pages.tracker.lanes[0].output_route,sizeof(first)));
    mx_paint(&a,&pages,&bank);
    assert(w->pixels[(MATRIX_Y+MATRIX_RH)*MATRIX_W+20]==0xffffce69u);
    matrix_key(SDLK_z,KMOD_CTRL);
    assert(w->selected==0 && !a.sister.matrix_layout.row_count);
    assert(!memcmp(dsp,&a.sister.matrix,sizeof(*dsp)));
    matrix_key(SDLK_y,KMOD_CTRL);assert(w->selected==1);
    assert(!memcmp(dsp,&a.sister.matrix,sizeof(*dsp)));
    matrix_key(SDLK_LEFT,KMOD_SHIFT);assert(w->selected==1 && w->history_count==1);
    /* Existing range selection uses the moved row as its anchor. */
    SDL_SetModState(KMOD_SHIFT);matrix_click(80,MATRIX_Y+2*MATRIX_RH+12,SDL_BUTTON_LEFT);SDL_SetModState(KMOD_NONE);
    assert(w->selection_count==2 && mx_chosen(1) && mx_chosen(2) && !mx_chosen(0));
    matrix_click(MATRIX_X+8*MATRIX_CW+20,140,SDL_BUTTON_LEFT);
    assert(!memcmp(dsp,&a.sister.matrix,sizeof(*dsp)));
    assert(!memcmp(&first,&pages.tracker.lanes[0].output_route,sizeof(first)));
    matrix_click(80,MATRIX_Y+MATRIX_RH+12,SDL_BUTTON_LEFT);
    /* Column identity, focus and meters survive crossing a scroll boundary. */
    matrix_click(MATRIX_X+8*MATRIX_CW+20,140,SDL_BUTTON_LEFT);
    assert(w->destination==8 && w->layout_focus==2);
    for(int i=0;i<8;++i)matrix_key(SDLK_LEFT,KMOD_SHIFT);
    assert(mx_column(0)==8 && w->destination==8);
    assert(!memcmp(dsp,&a.sister.matrix,sizeof(*dsp)));
    int history=w->history_count;matrix_key(SDLK_LEFT,KMOD_SHIFT);assert(w->history_count==history);
    w->receiving[8]=.5f;mx_paint(&a,&pages,&bank);
    assert(w->pixels[128*MATRIX_W+MATRIX_X]==0xffffce69u);
    assert(w->pixels[165*MATRIX_W+MATRIX_X+5]!=0xff202b30u);
    for(int i=0;i<TS_MATRIX_DESTINATIONS-1;++i)matrix_key(SDLK_RIGHT,KMOD_SHIFT);
    assert(mx_column(TS_MATRIX_DESTINATIONS-1)==8 && w->column==TS_MATRIX_DESTINATIONS-MATRIX_COLS);
    matrix_key(SDLK_RIGHT,KMOD_SHIFT);assert(mx_column(TS_MATRIX_DESTINATIONS-1)==8);
    assert(!memcmp(dsp,&a.sister.matrix,sizeof(*dsp)));
    /* A relocated header/cell still edits the original hardware destination. */
    int y=MATRIX_Y+(w->selected-w->scroll)*MATRIX_RH+12;
    int x=MATRIX_X+9*MATRIX_CW+20;
    matrix_click(x,y,SDL_BUTTON_LEFT);
    assert(w->destination==8);
    assert(pages.tracker.lanes[0].output_route.matrix_gain[8]!=first.matrix_gain[8]);
    matrix_key(SDLK_z,KMOD_CTRL);
    assert(!memcmp(&first,&pages.tracker.lanes[0].output_route,sizeof(first)));
    /* Compressor controls reveal the sidechain by identity after rearranging it. */
    int sc=mx_column_position(TS_MATRIX_SIDECHAIN);
    matrix_click(MATRIX_X+(sc-w->column)*MATRIX_CW+20,140,SDL_BUTTON_LEFT);
    assert(w->destination==TS_MATRIX_SIDECHAIN);
    for(int i=0;i<sc;++i)matrix_key(SDLK_LEFT,KMOD_SHIFT);
    assert(mx_column(0)==TS_MATRIX_SIDECHAIN);
    w->column=TS_MATRIX_DESTINATIONS-MATRIX_COLS;
    matrix_click(1200,34,SDL_BUTTON_LEFT);
    assert(w->compressor_open && w->column==0);
    matrix_click(1200,34,SDL_BUTTON_LEFT);assert(!w->compressor_open);
    /* Independent filters preserve global order, including hidden rows. */
    matrix_click(455,102,SDL_BUTTON_LEFT);assert(w->rows[0].kind==2 && w->rows[0].index==TS_MATRIX_NODES);
    matrix_click(80,MATRIX_Y+12,SDL_BUTTON_LEFT);matrix_key(SDLK_DOWN,KMOD_SHIFT);
    assert(w->rows[0].index==TS_MATRIX_NODES+1 && w->rows[1].index==TS_MATRIX_NODES);
    matrix_click(80,102,SDL_BUTTON_LEFT);
    assert(w->rows[0].kind==0 && w->rows[0].index==1 && w->rows[1].index==0);
    int in=-1;for(int i=0;i<w->count;++i)if(w->rows[i].kind==2 && w->rows[i].index==TS_MATRIX_NODES)in=i;
    assert(in>0 && w->rows[in-1].kind==2 && w->rows[in-1].index==TS_MATRIX_NODES+1);
    /* The selected label follows movement beyond the 17-row viewport. */
    matrix_click(80,MATRIX_Y+12,SDL_BUTTON_LEFT);
    for(int i=0;i<MATRIX_VISIBLE+1 && i<w->count-1;++i)matrix_key(SDLK_DOWN,KMOD_SHIFT);
    assert(w->selected>=MATRIX_VISIBLE && w->scroll>0 && w->selected<w->scroll+MATRIX_VISIBLE);
    TsMatrixLayout *saved=malloc(sizeof(*saved));assert(saved);*saved=a.sister.matrix_layout;
    matrix_hide();assert(matrix_show(0,&a,&u,&pages,&bank,-1,-1));
    assert(!memcmp(saved,&a.sister.matrix_layout,sizeof(*saved)));
    /* Full state round trip and old project migration, not just a UI cache. */
    TsSisterProjectState *state=malloc(sizeof(*state));assert(state);
    ts_sister_project_state_capture(state,&a.sister,1,NULL);
    assert(ts_sister_project_state_save_file(state,"matrix-layout-state.ini",error,sizeof(error)));
    int present=0;ts_sister_project_state_init(state,48000);
    assert(ts_sister_project_state_load_file(state,"matrix-layout-state.ini",48000,&present,error,sizeof(error)) && present);
    assert(!memcmp(saved,&state->matrix_layout,sizeof(*saved)));remove("matrix-layout-state.ini");
    ts_matrix_layout_default(&a.sister.matrix_layout);
    assert(ts_sister_project_state_apply(state,&a.sister,&bank));mx_rows(&pages,&bank);
    assert(!memcmp(saved,&a.sister.matrix_layout,sizeof(*saved)));
    /* Layout reset is undoable and leaves the graph intact. */
    *dsp=a.sister.matrix;matrix_click(90,140,SDL_BUTTON_LEFT);
    assert(!a.sister.matrix_layout.row_count && mx_column(0)==0);
    assert(!memcmp(dsp,&a.sister.matrix,sizeof(*dsp)));
    matrix_key(SDLK_z,KMOD_CTRL);
    assert(!memcmp(saved,&a.sister.matrix_layout,sizeof(*saved)));
    assert(!memcmp(dsp,&a.sister.matrix,sizeof(*dsp)));
    FILE *old=fopen("matrix-layout-old.ini","wb");assert(old);
    fputs("TapeSister Sister Project State\nVersion=39\nPageCount=1\nActivePage=0\n",old);fclose(old);
    assert(ts_sister_project_state_load_file(state,"matrix-layout-old.ini",48000,&present,error,sizeof(error)));
    assert(!state->matrix_layout.row_count && state->matrix_layout.column[8]==8);remove("matrix-layout-old.ini");
    /* Stable tile/card identities cannot be redirected to a replacement slot. */
    TsMatrixLayout *layout=&a.sister.matrix_layout;ts_matrix_layout_default(layout);
    layout->row_count=2;layout->row_kind[0]=3;layout->row_id[0]=77;
    layout->row_kind[1]=1;layout->row_id[1]=42;
    w->filter=0;w->count=3;
    w->rows[0]=(MatrixRow){.kind=1,.index=0,.tile=43}; /* replacement in the old slot */
    w->rows[1]=(MatrixRow){.kind=1,.index=9,.tile=42}; /* original moved elsewhere */
    w->rows[2]=(MatrixRow){.kind=3,.index=5,.tile=77};
    mx_layout_rows();assert(w->rows[0].kind==3 && w->rows[0].tile==77);
    assert(w->rows[1].tile==42 && w->rows[1].index==9 && w->rows[2].tile==43);
    *layout=*saved;w->selected=-1;mx_rows(&pages,&bank);
    /* Native framebuffer shows the moved focus and the new shortcut hint. */
    w->column=0;w->scroll=0;matrix_click(80,MATRIX_Y+12,SDL_BUTTON_LEFT);
    matrix_shot(directory,"matrix-layout-order");
    free(saved);free(state);free(dsp);ts_matrix_layout_default(&a.sister.matrix_layout);matrix_history_clear();
    puts("Matrix layout: Shift arrows, focus, filters, scrolling, routing identity, undo and project persistence passed");
}
