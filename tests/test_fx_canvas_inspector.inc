static void inspector_select(int node_id)
{
    fx_build(&pages,&bank);FxCanvasNode *n=node(node_id);assert(n);
    fx_clear_wires();fx_select(n,0);matrix_window.effect_open=matrix_window.compressor_open=0;
    mx_processor_read(&audio,&matrix_window.fixed);assert(fx_inspector_build()>0);
}
static void inspector_controls(const char *directory)
{
    MatrixWindow *w=&matrix_window;MatrixSnapshot *saved=mx_snapshot(0,&audio,&pages,&bank);assert(saved);
    int ow,oh;SDL_GetWindowSize(w->window,&ow,&oh);SDL_SetWindowSize(w->window,MATRIX_W,MATRIX_H);
    /* A cancelled confirmation changes neither routing nor history. */
    int undo=w->history_cursor;static TsMatrixControls routing;routing=w->controls;
    click(1100,82,1);assert(fx_canvas.confirm_clear);assert(!memcmp(&routing,&w->controls,sizeof(routing)));
    if(directory){char path[1024];snprintf(path,sizeof(path),"%s/clear-confirmation.bmp",directory);screenshot(path);}
    key(SDLK_RETURN,0);assert(!fx_canvas.confirm_clear && w->history_cursor==undo);
    click(1100,82,1);click(400,458,1);assert(!fx_canvas.confirm_clear && w->history_cursor==undo);
    click(1100,82,1);key(SDLK_ESCAPE,0);assert(!fx_canvas.confirm_clear && w->history_cursor==undo);
    /* Inspector wheel must edit the audible EQ, without moving the view. */
    inspector_select(TS_MATRIX_EQ);float zoom=w->layout->canvas_zoom,old=audio.sister.master_eq.controls.band[0].gain_db;
    edit_wheel(1110,230+3*53+20,1,0);
    assert(audio.sister.master_eq.controls.band[0].gain_db>old && w->layout->canvas_zoom==zoom);
    key(SDLK_z,KMOD_CTRL);assert(audio.sister.master_eq.controls.band[0].gain_db==old);
    fx_inspector_build();click(1200,623,1);assert(fx_canvas.inspector_page==1);
    old=audio.sister.master_eq.controls.band[1].frequency;edit_wheel(1110,250,1,0);
    assert(audio.sister.master_eq.controls.band[1].frequency>old);key(SDLK_z,KMOD_CTRL);
    const int fixed[]={TS_MATRIX_MAIN,TS_MATRIX_SISTER,TS_MATRIX_PRISM,TS_MATRIX_BOARD,TS_MATRIX_FALLOUT,TS_MATRIX_LIMITER,TS_MATRIX_COMPRESSOR};
    for(size_t i=0;i<sizeof(fixed)/sizeof(*fixed);++i)inspector_select(fixed[i]);
    inspector_select(TS_MATRIX_LIMITER);
    TsSisterLimiter *limiter=&audio.sister.limiter;assert(ts_sister_limiter_reconfigure(limiter,48000));
    for(int i=0;i<1000;++i)ts_sister_limiter_process(limiter,(TsStereoFrame){.2f,-.1f},NULL,NULL);
    TsStereoFrame *storage=limiter->delay;uint64_t frames=limiter->processed_frames;old=limiter->ceiling_db;
    edit_wheel(1110,250,-1,0);assert(limiter->ceiling_db<old && limiter->delay==storage && limiter->processed_frames==frames);
    key(SDLK_z,KMOD_CTRL);assert(limiter->ceiling_db==old && limiter->delay==storage && limiter->processed_frames==frames);
    edit_wheel(1110,303,1,0);assert(limiter->delay_ramp && limiter->delay==storage && limiter->processed_frames==frames);
    key(SDLK_z,KMOD_CTRL);
    /* The compact SunVox submenu navigates before committing one creation. */
    fx_popup_open(400,220);click(fx_canvas.popup_x+60,fx_canvas.popup_y+40+(TS_MATRIX_FX_SUNVOX-1)*30,1);
    assert(fx_canvas.popup==2 && !sunvox_editor.node);
    if(directory){char path[1024];snprintf(path,sizeof(path),"%s/sunvox-menu.bmp",directory);screenshot(path);}
    int list[512],nc;const char *categories[32];int count=fx_popup_catalog(list,categories,&nc);assert(count==ts_sunvox_factory_count());
    key(SDLK_PAGEDOWN,0);assert(fx_canvas.popup_selected==FX_POPUP_ROWS);
    key(SDLK_END,0);assert(fx_canvas.popup_selected==count-1);
    key(SDLK_ESCAPE,0);assert(fx_canvas.popup==1);key(SDLK_ESCAPE,0);assert(!fx_canvas.popup);
    int factory=-1;TsSunVoxControls patch;
    for(int i=0;i<ts_sunvox_factory_count();++i) {
        assert(ts_sunvox_select(&patch,i));TsSunVoxEngine *probe=ts_sunvox_new(&patch,48000);assert(probe);ts_sunvox_free(probe);
        if(ts_sunvox_parameter_count(&patch)>FX_INSPECT_ROWS){factory=i;break;}
    }
    assert(factory>=0);fx_popup_open(400,220);fx_canvas.popup=2;fx_canvas.popup_selected=factory;
    undo=w->history_cursor;key(SDLK_RETURN,0);assert(!fx_canvas.popup && !sunvox_editor.node && w->history_cursor==undo+1);
    int slot=mx_effect_slot();assert(slot>=0 && w->controls.effect[slot].sunvox.patch==patch.patch);
    uint32_t id=w->effect_open;TsMatrixEffectNode *engine=audio.sister.matrix_effects.current[slot];assert(engine);
    assert(fx_inspector_build()==ts_sunvox_parameter_count(&patch)+3);
    zoom=w->layout->canvas_zoom;edit_wheel(1110,250,1,0);
    assert(w->controls.effect[slot].sunvox.value[0]>=0 && w->layout->canvas_zoom==zoom && engine==audio.sister.matrix_effects.current[slot]);
    pointer(SDL_MOUSEBUTTONDOWN,1110,250,SDL_BUTTON_RIGHT,1);pointer(SDL_MOUSEBUTTONUP,1110,250,SDL_BUTTON_RIGHT,1);
    assert(w->controls.effect[slot].sunvox.value[0]==-1);
    click(1200,623,1);assert(fx_canvas.inspector_page==1);edit_wheel(1110,250,1,0);
    assert(w->controls.effect[slot].sunvox.value[7]>=0);
    if(directory){char path[1024];snprintf(path,sizeof(path),"%s/sunvox-inspector.bmp",directory);screenshot(path);}
    click(1100,660,1);assert(sunvox_editor.node==id);key(SDLK_ESCAPE,0);assert(!sunvox_editor.node);
    /* World grid snapping keeps group offsets, works after zoom/pan, and has
       an explicit off switch and temporary Alt bypass. */
    fx_build(&pages,&bank);FxCanvasNode *n=instance(id);assert(n);fx_select(n,0);
    TsFxCanvasPosition *pos=fx_position(n->row,0);pos->x=5;pos->y=11;
    w->layout->canvas_snap=1;w->layout->canvas_zoom=.5f;w->layout->canvas_x=17;w->layout->canvas_y=23;
    fx_build(&pages,&bank);fx_canvas.mouse_x=400;fx_canvas.mouse_y=300;
    key(SDLK_g,0);pointer(SDL_MOUSEMOTION,417,319,0,0);key(SDLK_RETURN,0);
    pos=fx_position(n->row,0);assert(pos->x==32 && pos->y==64);
    key(SDLK_z,KMOD_CTRL);pos=fx_position(n->row,0);assert(pos->x==5 && pos->y==11);
    fx_build(&pages,&bank);fx_canvas.mouse_x=400;fx_canvas.mouse_y=300;key(SDLK_g,0);
    SDL_SetModState(KMOD_ALT);pointer(SDL_MOUSEMOTION,417,319,0,0);key(SDLK_RETURN,0);SDL_SetModState(KMOD_NONE);
    pos=fx_position(n->row,0);assert(pos->x==39 && pos->y==49);
    click(900,82,1);assert(!w->layout->canvas_snap);view_codec();
    assert(mx_apply(saved,0,&audio,&ui,&pages,&bank));free(saved);matrix_history_clear();
    w->layout=&audio.sister.matrix_layout;w->mosaic=ui.mosaic;w->epoch=ui.mosaic?ui.mosaic->epoch:0;
    SDL_SetWindowSize(w->window,ow,oh);fx_build(&pages,&bank);fx_clear_selection();
}
