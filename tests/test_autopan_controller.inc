static void autopan_controller(void)
{
    edit_settle();MatrixSnapshot *saved=mx_snapshot(0,&audio,&pages,&bank);assert(saved);
    MatrixSnapshot *fixture=mx_snapshot(0,&audio,&pages,&bank);assert(fixture);ts_matrix_default(&fixture->controls);fixture->controls.enabled=1;ts_matrix_layout_default(&fixture->layout);
    assert(mx_apply(fixture,0,&audio,&ui,&pages,&bank));free(fixture);edit_settle();matrix_history_clear();
    fx_popup_open(450,300);fx_popup_add(TS_MATRIX_FX_AUTOPAN,-1,0,&audio,&ui,&pages,&bank);edit_settle();
    int slot=mx_effect_slot();assert(slot>=0);uint32_t id=matrix_window.controls.effect[slot].id;
    assert(matrix_window.controls.effect[slot].type==TS_MATRIX_FX_AUTOPAN && audio.sister.matrix_effects.current[slot]);
    fx_canvas.meta=0;fx_canvas.fit=0;fx_canvas.movement=0;fx_canvas.active_view=0;
    matrix_window.layout->canvas_zoom=1;matrix_window.layout->canvas_x=matrix_window.layout->canvas_y=0;
    fx_build(&pages,&bank);FxCanvasNode *n=instance(id);assert(n);TsFxCanvasPosition *position=fx_position(n->row,1);position->x=400;position->y=160;position->scale=1;
    fx_build(&pages,&bank);n=instance(id);fx_select(n,0);
    click(fx_x(n->x)+20,fx_y(n->y)+20,2);assert(autopan_window.visible && autopan_window.node==id);
    SDL_SetWindowSize(autopan_window.window,960,600);
    static SisterWindow sister;memset(&sister,0,sizeof(sister));sister.model.parameters=audio.sister.parameters;
    autopan_window.painted=SDL_GetTicks()-100;autopan_paint(&ui,&sister);
    if(getenv("TS_AUTOPAN_PREVIEW")) {
        SDL_Surface *image=SDL_CreateRGBSurfaceFrom(autopan_window.fb.pixels,640,400,32,640*4,0xff0000,0xff00,0xff,0);
        assert(image && !SDL_SaveBMP(image,getenv("TS_AUTOPAN_PREVIEW")));SDL_FreeSurface(image);
    }
    SDL_Event e;SDL_zero(e);e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=autopan_window.id;e.button.button=SDL_BUTTON_LEFT;
    e.button.x=616*3/2;e.button.y=(66+TS_AP_WAVE*32)*3/2;
    assert(autopan_event(&e,0,&audio,&ui,&sister,&pages,&bank));assert(matrix_window.controls.effect[slot].autopan.value[0]==1);
    assert(audio.sister.matrix.controls.effect[slot].autopan.value[0]==1);
    e.type=SDL_KEYDOWN;e.key.windowID=autopan_window.id;e.key.keysym.sym=SDLK_z;e.key.keysym.mod=KMOD_CTRL;
    assert(autopan_event(&e,0,&audio,&ui,&sister,&pages,&bank));assert(matrix_window.controls.effect[slot].autopan.value[0]==0);
    e.key.keysym.sym=SDLK_y;assert(autopan_event(&e,0,&audio,&ui,&sister,&pages,&bank));assert(matrix_window.controls.effect[slot].autopan.value[0]==1);
    e.key.keysym.sym=SDLK_ESCAPE;e.key.keysym.mod=0;assert(autopan_event(&e,0,&audio,&ui,&sister,&pages,&bank));assert(!autopan_window.visible && matrix_window.visible);
    /* Consolidation carries every oscillator option into both native hosts. */
    fixture=mx_snapshot(0,&audio,&pages,&bank);assert(fixture);
    int delay=ts_matrix_effect_add(&fixture->controls,TS_SISTER_FX_DELAY);assert(delay>=0);uint32_t delay_id=fixture->controls.effect[delay].id;
    fixture->controls.row[TS_MATRIX_EFFECT_BASE+slot]=(TsSourceRoute){.matrix_enabled=1};
    fixture->controls.row[TS_MATRIX_EFFECT_BASE+slot].matrix_gain[TS_MATRIX_EFFECT_BASE+delay]=1000;
    fixture->controls.row[TS_MATRIX_EFFECT_BASE+delay]=(TsSourceRoute){.matrix_enabled=1};
    fixture->controls.row[TS_MATRIX_EFFECT_BASE+delay].matrix_gain[TS_MATRIX_MAIN]=1000;
    assert(mx_apply(fixture,0,&audio,&ui,&pages,&bank));free(fixture);edit_settle();matrix_history_clear();
    fx_build(&pages,&bank);fx_select(instance(id),0);fx_select(instance(delay_id),1);
    consolidate_choice=1;fx_context_open(300,200,NULL);context_action(FX_CONTEXT_CONSOLIDATE);edit_settle();
    slot=mx_effect_slot();assert(slot>=0 && matrix_window.controls.effect[slot].type==TS_MATRIX_FX_PEDALBOARD);
    TsSisterFxSlotControls *child=&matrix_window.controls.effect[slot].board.slot[0];assert(child->type==TS_SISTER_FX_AUTOPAN && child->autopan.value[0]==1);
    autopan_open(&audio,2,matrix_window.controls.effect[slot].id,child->id,0);float *a,*b,*c;assert(autopan_resolve(&ui,&sister,&a,&b,&c)==&child->autopan);autopan_hide();
    mx_undo(0,&audio,&ui,&pages,&bank,0);edit_settle();fx_build(&pages,&bank);fx_select(instance(id),0);fx_select(instance(delay_id),1);
    consolidate_choice=2;fx_context_open(300,200,NULL);context_action(FX_CONTEXT_CONSOLIDATE);edit_settle();slot=mx_effect_slot();
    assert(slot>=0 && matrix_window.controls.effect[slot].meta.stage[0].type==TS_SISTER_FX_AUTOPAN && matrix_window.controls.effect[slot].meta.stage[0].autopan.value[0]==1);
    autopan_open(&audio,3,matrix_window.controls.effect[slot].id,0,0);assert(autopan_resolve(&ui,&sister,&a,&b,&c)==&matrix_window.controls.effect[slot].meta.stage[0].autopan);autopan_hide();
    /* Original P1 shares the editor, publishing and undo. */
    sister.model.parameters=audio.sister.parameters;TsSisterFxSlotControls *p1=&sister.model.parameters.fx.slot[0];
    sister_fx_slot_select_type(p1,TS_SISTER_FX_AUTOPAN);ts_sister_runtime_set_parameters(&audio.sister,&sister.model.parameters);
    autopan_open(&audio,0,0,p1->id,0);e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=autopan_window.id;e.button.button=SDL_BUTTON_LEFT;e.button.x=616*3/2;e.button.y=(66+TS_AP_WAVE*32)*3/2;
    assert(autopan_event(&e,0,&audio,&ui,&sister,&pages,&bank));assert(audio.sister.parameters.fx.slot[0].autopan.value[0]==1);
    e.type=SDL_KEYDOWN;e.key.windowID=autopan_window.id;e.key.keysym.sym=SDLK_z;e.key.keysym.mod=KMOD_CTRL;
    assert(autopan_event(&e,0,&audio,&ui,&sister,&pages,&bank));assert(sister.model.parameters.fx.slot[0].autopan.value[0]==0);
    autopan_close();assert(mx_apply(saved,0,&audio,&ui,&pages,&bank));free(saved);edit_settle();matrix_history_clear();fx_view(1,&pages,&bank);
}
