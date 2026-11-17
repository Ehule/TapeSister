static void consolidation_rejected(MatrixSnapshot *fixture,const int *slots,int count,int meta,const char *reason)
{
    size_t bytes=sizeof(*fixture)+(size_t)fixture->count*sizeof(fixture->routes[0]);
    MatrixSnapshot *copy=malloc(bytes);assert(copy);memcpy(copy,fixture,bytes);
    const char *error=fx_consolidate_plan(copy,slots,count,meta);assert(error && strstr(error,reason));
    /* Failure must precede any edit to the staged graph as well as publication. */
    assert(!memcmp(copy,fixture,bytes));free(copy);
}
static void consolidate_and_arrange(void)
{
    edit_settle();MatrixSnapshot *saved=mx_snapshot(0,&audio,&pages,&bank);assert(saved);
    MatrixSnapshot *fixture=mx_snapshot(0,&audio,&pages,&bank);assert(fixture);
    ts_matrix_default(&fixture->controls);TsMatrixControls *c=&fixture->controls;c->enabled=1;
    c->next_effect_id=matrix_window.controls.next_effect_id;
    for(int i=0;i<TS_MATRIX_ROWS;++i)c->row[i]=(TsSourceRoute){.matrix_enabled=1};
    for(int i=0;i<fixture->count;++i)fixture->routes[i].route=(TsSourceRoute){0};
    int types[]={TS_SISTER_FX_DELAY,TS_SISTER_FX_DISTORTION,TS_SISTER_FX_TRANSIENT};uint32_t ids[3];
    for(int i=0;i<3;++i){assert(ts_matrix_effect_add(c,types[i])==i);ids[i]=c->effect[i].id;}
    int base=TS_MATRIX_EFFECT_BASE,slots[]={2,0,1};
    c->row[TS_MATRIX_MOSAIC].matrix_gain[base]=730;c->row[TS_MATRIX_TRACKER].matrix_gain[base]=420;
    c->row[base].matrix_gain[base+1]=1000;c->row[base+1].matrix_gain[base+2]=1000;
    c->row[base+2].matrix_gain[TS_MATRIX_MAIN]=850;c->row[base+2].matrix_gain[TS_MATRIX_NODES]=330;c->row[base+2].pan=-17;
    c->effect[0].a=.12f;c->effect[0].mix=.42f;c->effect[0].gain_db=-3;c->effect[1].enabled=0;
    c->effect[2].transient_power=2;
    ts_matrix_layout_default(&fixture->layout);assert(mx_apply(fixture,0,&audio,&ui,&pages,&bank));matrix_history_clear();
    fx_view(1,&pages,&bank);fx_canvas.fit=0;fx_build(&pages,&bank);edit_settle();
    FxCanvasNode *a=instance(ids[0]),*b=instance(ids[1]),*d=instance(ids[2]);assert(a && b && d);
    TsFxCanvasPosition *pa=fx_position(a->row,1),*pb=fx_position(b->row,1),*pd=fx_position(d->row,1);
    snprintf(pa->name,sizeof(pa->name),"Effect 10");snprintf(pb->name,sizeof(pb->name),"effect 2");snprintf(pd->name,sizeof(pd->name),"Effect 1");
    pa->color=0xffbc84a9;pa->x=pb->x=pd->x=64;pa->y=pb->y=pd->y=64;pd->scale=2;
    fx_build(&pages,&bank);a=instance(ids[0]);b=instance(ids[1]);d=instance(ids[2]);
    fx_select(a,0);fx_select(b,1);fx_select(d,1);
    void *engine=audio.sister.matrix_effects.current[0];
    key(SDLK_r,KMOD_CTRL);fx_build(&pages,&bank);
    assert(instance(ids[2])->x==64 && instance(ids[2])->y==64);
    assert(instance(ids[1])->x>=64+2*FX_NODE_W && instance(ids[1])->y==64);
    assert(instance(ids[0])->x==64 && instance(ids[0])->y>=64+2*FX_NODE_H);
    assert(audio.sister.matrix_effects.current[0]==engine);view_codec();
    matrix_window.layout->canvas_x=matrix_window.layout->canvas_y=0;matrix_window.layout->canvas_zoom=.5f;
    SDL_Surface *ports=rendered_frame();SDL_FreeSurface(ports);a=instance(ids[0]);
    int px=mx_px(fx_x(a->x)),py=mx_py(fx_y(a->y+fx_node_h(a)*.5f));
    assert((matrix_window.pixels[(size_t)py*matrix_window.pixel_w+px]&0xffffff)==0xbc84a9);
    px=mx_px(fx_x(a->x+fx_node_w(a)));
    assert((matrix_window.pixels[(size_t)py*matrix_window.pixel_w+px]&0xffffff)==0xbc84a9);
    key(SDLK_z,KMOD_CTRL);key(SDLK_r,KMOD_CTRL|KMOD_SHIFT);fx_build(&pages,&bank);
    assert(instance(ids[0])->x==64 && instance(ids[0])->y==64);assert(audio.sister.matrix_effects.current[0]==engine);
    key(SDLK_z,KMOD_CTRL);
    fx_clear_wires();fx_select_wire(instance(ids[2])->row,TS_MATRIX_MAIN,0);fx_wire_set_color(fx_canvas.wires.items[0],0xffaa7788);fx_clear_wires();
    key(SDLK_p,KMOD_CTRL|KMOD_SHIFT);edit_settle();
    assert(matrix_window.controls.effect[0].id==ids[0] && matrix_window.controls.effect[0].type==TS_MATRIX_FX_PEDALBOARD);
    assert(!matrix_window.controls.effect[1].id && !matrix_window.controls.effect[2].id);
    const TsPedalboardControls *board=&matrix_window.controls.effect[0].board;
    for(int i=0;i<3;++i)assert(board->slot[i].type==types[i]);
    assert(board->slot[0].parameter_a==.12f && board->slot[0].mix==.42f && board->slot[0].gain_db==-3);
    assert(!board->slot[1].enabled && board->slot[2].transient_power==2);
    assert(matrix_window.controls.row[TS_MATRIX_MOSAIC].matrix_gain[base]==730);
    assert(matrix_window.controls.row[TS_MATRIX_TRACKER].matrix_gain[base]==420);
    assert(!memcmp(&matrix_window.controls.row[base],&c->row[base+2],sizeof(TsSourceRoute)));
    assert(fx_wire_color(instance(ids[0])->row,TS_MATRIX_MAIN)==0xffaa7788);view_codec();
    FILE *f=tmpfile();assert(f && ts_matrix_write(f,&matrix_window.controls));rewind(f);
    TsMatrixControls *read=malloc(sizeof(*read));assert(read);ts_matrix_default(read);char line[4096];
    while(fgets(line,sizeof(line),f)){line[strcspn(line,"\r\n")]=0;char *eq=strchr(line,'=');assert(eq);*eq++=0;assert(ts_matrix_read(read,line,eq)==1);}
    assert(ts_matrix_valid(read));assert(!memcmp(&read->effect[0].board,board,sizeof(*board)));free(read);fclose(f);
    key(SDLK_z,KMOD_CTRL);edit_settle();assert(matrix_window.controls.effect[2].id==ids[2]);
    key(SDLK_y,KMOD_CTRL);edit_settle();assert(matrix_window.controls.effect[0].type==TS_MATRIX_FX_PEDALBOARD);
    key(SDLK_z,KMOD_CTRL);edit_settle();fx_build(&pages,&bank);
    fx_select(instance(ids[2]),0);fx_select(instance(ids[0]),1);fx_select(instance(ids[1]),1);
    key(SDLK_p,KMOD_CTRL|KMOD_ALT);edit_settle();assert(matrix_window.controls.effect[0].type==TS_MATRIX_FX_META);
    assert(matrix_window.controls.effect[0].meta.count==3 && matrix_window.controls.effect[0].meta.stage[1].type==TS_SISTER_FX_DISTORTION);
    key(SDLK_z,KMOD_CTRL);edit_settle();
    /* Fail closed for the user's different-input example and every boundary. */
    c->row[TS_MATRIX_TRACKER].matrix_gain[base+1]=500;consolidation_rejected(fixture,slots,3,0,"Different inputs");c->row[TS_MATRIX_TRACKER].matrix_gain[base+1]=0;
    fixture->routes[0].route.matrix_gain[base+2]=400;consolidation_rejected(fixture,slots,3,0,"Different input cards");fixture->routes[0].route.matrix_gain[base+2]=0;
    c->row[base].matrix_gain[TS_MATRIX_MAIN]=500;consolidation_rejected(fixture,slots,3,0,"extra output");c->row[base].matrix_gain[TS_MATRIX_MAIN]=0;
    c->row[base].matrix_gain[base+2]=1000;consolidation_rejected(fixture,slots,3,0,"Parallel");c->row[base].matrix_gain[base+2]=0;
    c->row[base].matrix_gain[base+1]=500;consolidation_rejected(fixture,slots,3,0,"non-unity");c->row[base].matrix_gain[base+1]=1000;
    c->row[base].pan=15;consolidation_rejected(fixture,slots,3,0,"pan or width");c->row[base].pan=0;
    c->delayed[TS_MATRIX_TRACKER]=ts_matrix_feedback_bit(base);consolidation_rejected(fixture,slots,3,0,"delayed feedback");c->delayed[TS_MATRIX_TRACKER]=0;
    int mod=ts_matrix_effect_add(c,TS_MATRIX_FX_MODULATOR);assert(mod>=0);
    c->effect[mod].modulation.map_count=1;c->effect[mod].modulation.map[0].target=ids[1];
    consolidation_rejected(fixture,slots,3,0,"modulator targets");ts_matrix_effect_remove(c,mod);
    c->effect[1].type=TS_MATRIX_FX_COMPRESSOR;consolidation_rejected(fixture,slots,3,0,"cannot be nested");c->effect[1].type=TS_SISTER_FX_DISTORTION;
    c->effect[2].transient_states.captured=1;consolidation_rejected(fixture,slots,3,1,"captured states");c->effect[2].transient_states.captured=0;
    c->effect[1].type=TS_MATRIX_FX_SUNVOX;ts_sunvox_default(&c->effect[1].sunvox);
    consolidation_rejected(fixture,slots,3,1,"cannot hold SunVox");
    assert(!fx_consolidate_plan(fixture,slots,3,0));assert(c->effect[0].board.slot[1].type==TS_SISTER_FX_SUNVOX);
    /* A failed UI command gives a reason and creates no undo entry. */
    fx_clear_selection();int history=matrix_window.history_cursor;key(SDLK_p,KMOD_CTRL|KMOD_SHIFT);
    assert(strstr(matrix_window.status,"CONSOLIDATION FAILED") && strstr(matrix_window.status,"at least two"));assert(matrix_window.history_cursor==history);
    free(fixture);assert(mx_apply(saved,0,&audio,&ui,&pages,&bank));free(saved);edit_settle();matrix_history_clear();fx_view(1,&pages,&bank);
}

static void save_in_fx_canvas(void)
{
    MatrixWindow *w=&matrix_window;float zoom=w->layout->canvas_zoom;int mosaic=ui.mosaic_open;
    snprintf(ui.project_path,sizeof(ui.project_path),"fx-overlay-save/FX Overlay.tsr");
    key(SDLK_s,KMOD_CTRL);assert(ui.project_save_window_id==w->id && ui.project_overwrite_confirm_open);
    ui_sync_dialog_workspaces(&ui);assert(ui.mosaic_open==mosaic && w->visible && w->canvas);
    SDL_Surface *frame=rendered_frame();assert(frame);SDL_FreeSurface(frame);
    const char *preview=getenv("TS_FX_PREVIEW");if(preview){char p[1024];snprintf(p,sizeof(p),"%s/save-overlay.bmp",preview);screenshot(p);}
    int ww,wh,mw,mh;SDL_GetWindowSize(w->window,&ww,&wh);SDL_GetWindowSize(main_window,&mw,&mh);
    SDL_Event e;SDL_zero(e);e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=w->id;e.button.x=ww/2;e.button.y=wh/2;
    project_save_overlay_event(&e,main_window,&ui);int x,y;logical_mouse(main_window,e.button.x,e.button.y,&x,&y);
    assert(e.button.windowID==SDL_GetWindowID(main_window) && abs(x-320)<=1 && abs(y-200)<=1);
    e.type=SDL_WINDOWEVENT;e.window.windowID=w->id;e.window.event=SDL_WINDOWEVENT_CLOSE;
    project_save_overlay_event(&e,main_window,&ui);assert(e.type==SDL_KEYDOWN && e.key.keysym.sym==SDLK_ESCAPE);
    ui.project_overwrite_confirm_open=0;project_save_overlay_poll(&ui);assert(!ui.project_save_window_id && w->layout->canvas_zoom==zoom && w->visible);
    ui.project_path[0]=0;key(SDLK_s,KMOD_CTRL);assert(ui.browser.mode==TS_BROWSER_SAVE_RECIPE && ui.project_save_window_id==w->id);
    ui_sync_dialog_workspaces(&ui);assert(ui.mosaic_open==mosaic);ui.browser.mode=TS_BROWSER_CLOSED;project_save_overlay_poll(&ui);
}
