static MatrixRow gallery_card(uint64_t scene,uint64_t card)
{
    for(int i=0;i<fx_canvas.count;++i) {
        MatrixRow r=fx_canvas.node[i].row;
        if(r.kind==3 && r.canvas==scene && r.tile==card)return r;
    }
    assert(!"Missing Gallery card");return (MatrixRow){0};
}
static int gallery_cards(void)
{int n=0;for(int i=0;i<fx_canvas.count;++i)n+=fx_canvas.node[i].row.kind==3;return n;}
static void gallery_view(void)
{
    char error[256];MatrixWindow *w=&matrix_window;
    matrix_history_clear();
    assert(!pages.gallery);
    if(!pages.mosaic)pages.mosaic=ts_mosaic_create();
    pages.gallery=ts_gallery_create(pages.mosaic);assert(pages.gallery);
    assert(ts_gallery_add(pages.gallery,error,sizeof(error)));
    TsMosaicGallery *g=pages.gallery;
    float samples[16];for(int i=0;i<16;++i)samples[i]=.1f;
    TsSample sample={.data=samples,.frames=16,.channels=1,.sample_rate=48000};
    TsMosaicEvent *cards[2];
    for(int c=0;c<2;++c) {
        TsMosaic *m=g->canvas[c].scene;TsMosaicSource *s=ts_mosaic_source(m,&sample,error,sizeof(error));assert(s);
        cards[c]=ts_mosaic_add(m,s,0,0);assert(cards[c]);
        snprintf(cards[c]->name,sizeof(cards[c]->name),"Movement %d",c+1);
    }
    assert(cards[0]->id==cards[1]->id); /* IDs are local to their movement. */
    TsMatrixControls controls;ts_matrix_default(&controls);controls.enabled=1;
    int a=ts_matrix_effect_add(&controls,TS_SISTER_FX_DELAY),b=ts_matrix_effect_add(&controls,TS_SISTER_FX_REVERB);
    assert(a>=0 && b>=0);
    for(int i=0;i<TS_MATRIX_ROWS;++i)memset(controls.row[i].matrix_gain,0,sizeof(controls.row[i].matrix_gain));
    for(int c=0;c<2;++c) {
        int fx=TS_MATRIX_EFFECT_BASE+(c?b:a);
        cards[c]->output_route.matrix_enabled=1;cards[c]->output_route.matrix_gain[fx]=1000;
        controls.row[fx].matrix_gain[TS_MATRIX_MAIN]=1000;
    }
    controls.row[TS_MATRIX_MAIN].matrix_gain[TS_MATRIX_EQ]=1000;
    controls.row[TS_MATRIX_EQ].matrix_gain[TS_MATRIX_LIMITER]=1000;
    controls.row[TS_MATRIX_LIMITER].matrix_gain[TS_MATRIX_NODES]=1000;
    assert(ts_matrix_effects_restore(&audio.sister.matrix_effects,&controls,48000));
    assert(ts_matrix_set(&audio.sister.matrix,&controls,48000));
    w->controls=ui.config.matrix=controls;
    ui.mosaic=audio.mosaic=pages.mosaic=g->canvas[0].scene;audio.gallery=g;
    mx_bind_mosaic(&ui,&pages);ts_matrix_layout_default(w->layout);fx_view(1,&pages,&bank);
    assert(gallery_cards()==2 && !fx_canvas.movement);
    MatrixRow first=gallery_card(g->canvas[0].id,cards[0]->id),second=gallery_card(g->canvas[1].id,cards[1]->id);
    assert(!mx_same(first,second));
    /* Selection travels only between the active Gallery canvas and FX Canvas. */
    ui.mosaic_open=1;mosaic_select_only(&ui,cards[0]->id);
    fx_selection_from_mosaic(&ui,&pages,&bank);assert(w->selection_count==1 && fx_chosen(first) && !fx_chosen(second));
    fx_selection_to_mosaic(&ui);assert(ui.mosaic_selected==cards[0]->id);
    w->selection[w->selection_count++]=second;fx_selection_to_mosaic(&ui);assert(!ui.mosaic_selected);
    fx_clear_selection();w->selection[0]=second;w->selection_count=1;fx_selection_to_mosaic(&ui);assert(!ui.mosaic_selected);
    fx_clear_selection();w->selection[0]=first;w->selection_count=1;
    key(SDLK_ESCAPE,0);assert(!w->visible && ui.mosaic_open && ui.mosaic_selected==cards[0]->id && !audio.engine_stopped);
    static SisterWindow editor;ts_sister_ui_model_init(&editor.model,&ui.config);
    SDL_Event nav;SDL_zero(nav);nav.type=SDL_KEYDOWN;nav.key.windowID=SDL_GetWindowID(main_window);nav.key.keysym.sym=SDLK_e;
    ui.mosaic_source_keys=1;assert(!fx_navigation_event(&nav,main_window,0,&audio,&ui,&editor,&pages,&bank) && !w->visible);
    ui.mosaic_source_keys=0;assert(fx_navigation_event(&nav,main_window,0,&audio,&ui,&editor,&pages,&bank));
    assert(w->visible && w->canvas && fx_chosen(first));
    TsMatrixEffectNode *navigation_live=audio.sister.matrix_effects.current[a];
    nav.key.windowID=w->id;
    assert(fx_navigation_event(&nav,main_window,0,&audio,&ui,&editor,&pages,&bank));
    assert(!w->visible && ui.mosaic_selected==cards[0]->id && !audio.engine_stopped);
    nav.key.windowID=SDL_GetWindowID(main_window);
    assert(fx_navigation_event(&nav,main_window,0,&audio,&ui,&editor,&pages,&bank));
    assert(w->visible && fx_chosen(first) && audio.sister.matrix_effects.current[a]==navigation_live);
    /* All Sister pages return to the workspace that opened them. Child editors
       must preserve that return target instead of replacing it with themselves. */
    assert(sister_window_ensure(&editor,&ui.config));
    for(int page=0;page<3;++page) {
        editor.model.fx_page=page;
        sister_window_show(&editor,&ui.palette);editor.return_window_id=w->id;
        sister_escape(main_window,&editor);assert(!editor.model.visible && w->visible);
        sister_window_show(&editor,&ui.palette);editor.return_window_id=SDL_GetWindowID(main_window);
        sister_escape(main_window,&editor);assert(!editor.model.visible && ui.mosaic_open);
    }
    /* A board request can be serviced after focus has changed. Capture the
       double-click origin immediately, and keep it through nested editors. */
    sister_board_open_from(0,w->id);
    SDL_SetWindowInputFocus(main_window);
    sister_board_sync(&audio,&ui,&editor);
    assert(editor.return_window_id==w->id && editor.model.visible);
    sister_board_return(0);sister_board_sync(&audio,&ui,&editor);
    assert(editor.return_window_id==w->id);
    sister_escape(main_window,&editor);assert(!editor.model.visible && w->visible);
    SDL_DestroyTexture(editor.texture);SDL_DestroyRenderer(editor.renderer);SDL_DestroyWindow(editor.window);
    memset(&editor,0,sizeof(editor));
    /* Playing snapshots include the outgoing movement during a crossfade. */
    g->outgoing=1;
    for(int i=0;i<2;++i){TsMosaic *m=g->canvas[i].scene;m->playing=1;m->voices[0].id=cards[i]->id;m->voices[0].active=1;m->voices[0].audible_gain=1;}
    float saved_gain=audio.mixer.program_gain,saved_mosaic_gain=audio.sister.performance_gain_current[2];
    audio.mixer.program_gain=audio.sister.performance_gain_current[2]=1;
    g->canvas[0].scene->voices[0].output_route.activity_source=.25f;
    g->canvas[1].scene->voices[0].output_route.activity_source=.5f;
    assert(!fx_canvas.active_view);
    mx_capture_activity(0,&audio,&pages,&bank,50);assert(fx_row_playing(first) && fx_row_playing(second));
    assert(fx_row_level(first)==.25f && fx_row_level(second)==.5f);
    mx_capture_activity(0,&audio,&pages,&bank,50);
    assert(fx_row_playing(first) && fx_row_playing(second) && !fx_row_level(first) && !fx_row_level(second));
    TsMosaic *playing=g->canvas[0].scene;
    for(int frame=0;frame<12;++frame)(void)ts_mosaic_read(playing,48000);
    mx_capture_activity(0,&audio,&pages,&bank,50);
    assert(fx_row_level(first)>0 && !fx_row_level(second));
    playing->time=0;
    audio.mixer.program_gain=saved_gain;audio.sister.performance_gain_current[2]=saved_mosaic_gain;
    toolbar_click(FX_TOOL_MOVEMENT);click(400,122+12,1);assert(fx_canvas.active_view && gallery_cards()==2);
    assert(fx_label_visible(heading_index(first.canvas)) && fx_label_visible(heading_index(second.canvas)));
    if(getenv("TS_FX_PREVIEW")){char path[1024];snprintf(path,sizeof(path),"%s/playing.bmp",getenv("TS_FX_PREVIEW"));screenshot(path);}
    g->outgoing=-1;g->canvas[1].scene->playing=0;
    mx_capture_activity(0,&audio,&pages,&bank,50);fx_build(&pages,&bank);assert(gallery_cards()==1 && fx_row_playing(first) && !fx_row_playing(second));
    assert(fx_label_visible(heading_index(first.canvas)) && !fx_label_visible(heading_index(second.canvas)));
    g->canvas[0].scene->playing=0;
    mx_capture_activity(0,&audio,&pages,&bank,50);fx_build(&pages,&bank);assert(gallery_cards()==0);
    assert(!fx_label_visible(heading_index(first.canvas)) && !fx_label_visible(heading_index(second.canvas)));
    toolbar_click(FX_TOOL_MOVEMENT);click(400,102,1);assert(!fx_canvas.active_view && gallery_cards()==2);
    fx_clear_selection();

    TsFxCanvasPosition *p=fx_position(first,1),*q=fx_position(second,1);assert(p!=q);
    p->x=-310;p->y=100;q->x=-200;q->y=410;
    view_codec();
    /* Overview headers must still paint at the scale needed for all movements. */
    w->layout->canvas_zoom=.2f;w->layout->canvas_x=500;w->layout->canvas_y=100;
    SDL_Surface *frame=rendered_frame();FxFontMetrics font=fx_font_scaled(1,2.f/3);
    int heading=-1;
    for(int i=0;i<w->layout->heading_count;++i)if(w->layout->headings[i].canvas==g->canvas[0].id)heading=TS_FX_LABEL_LIMIT+i;
    assert(heading>=0);SDL_Rect heading_rect=fx_text_object_rect(heading);
    int hx=mx_px(heading_rect.x),hy=mx_py(heading_rect.y);
    const char *glyph=ts_ui_glyph(g->canvas[0].name[0]);
    for(int y=0;y<7;++y)for(int x=0;x<5;++x) {
        const uint32_t *pixels=(const uint32_t *)((const char *)frame->pixels+(hy+font.rows[y])*frame->pitch);
        assert(((pixels[hx+font.columns[x]]&0xffffff)==(FX_CABLE&0xffffff))==(glyph[y*5+x]=='1'));
    }
    SDL_FreeSurface(frame);
    TsMatrixEffectNode *live=audio.sister.matrix_effects.current[a];
    TsSourceRoute saved=cards[1]->output_route;
    /* Real dropdown events filter the view without switching or editing audio. */
    toolbar_click(FX_TOOL_MOVEMENT);assert(fx_canvas.movement_popup);
    click(400,90+3*32+12,1);assert(fx_canvas.movement==second.canvas);
    assert(gallery_cards()==1 && !instance(controls.effect[a].id) && instance(controls.effect[b].id));
    assert(node(TS_MATRIX_MAIN) && node(TS_MATRIX_EQ) && node(TS_MATRIX_LIMITER));
    assert(g->active==0 && !g->canvas[0].scene->playing && g->canvas[0].scene->time==0);
    assert(!memcmp(&cards[1]->output_route,&saved,sizeof(saved)) && audio.sister.matrix_effects.current[a]==live);
    /* New unconnected effects must exist in the filtered graph before add
       positions/selects them. View exceptions never create audio connections. */
    MatrixSnapshot *visibility_before=mx_snapshot(0,&audio,&pages,&bank);assert(visibility_before);
    fx_popup_open(300,220);click(fx_canvas.popup_x+55,fx_canvas.popup_y+40,1);
    uint32_t added=w->effect_open;FxCanvasNode *created=instance(added);
    assert(created && created->outside_filter && fx_chosen(created->row) && gallery_cards()==1);
    assert(fx_x(created->x)>=20 && fx_y(created->y)>=FX_TOP);
    assert(fx_x(created->x+fx_node_w(created))<=fx_right() && fx_y(created->y+fx_node_h(created))<=FX_BOTTOM);
    assert(!memcmp(&cards[1]->output_route,&saved,sizeof(saved)) && g->active==0 && !g->canvas[0].scene->playing);
    fx_clear_selection();w->effect_open=0;fx_build(&pages,&bank);assert(instance(added));
    uint64_t builds=fx_graph_cache.builds;fx_build(&pages,&bank);assert(fx_graph_cache.builds==builds);
    key(SDLK_z,KMOD_CTRL);fx_build(&pages,&bank);assert(!instance(added));
    key(SDLK_y,KMOD_CTRL);fx_build(&pages,&bank);assert(instance(added));
    toolbar_click(FX_TOOL_MOVEMENT);click(400,90+3*32+12,1);assert(!instance(added));
    MatrixRow picked={.kind=4,.tile=added};int selected=fx_source_at(picked);assert(selected>=0);
    mx_select(selected,0);w->effect_open=added;fx_build(&pages,&bank);assert(instance(added)->outside_filter);
    fx_copy();key(SDLK_v,KMOD_CTRL);fx_build(&pages,&bank);
    assert(w->effect_open!=added && instance(w->effect_open) && w->selection_count==1);
    assert(mx_apply(visibility_before,0,&audio,&ui,&pages,&bank));free(visibility_before);
    memset(fx_canvas.revealed,0,sizeof(fx_canvas.revealed));fx_clear_selection();w->effect_open=0;
    matrix_history_clear();fx_build(&pages,&bank);
    /* Editing the parked card resolves through its scene, including undo. */
    MatrixSnapshot *before=mx_snapshot(0,&audio,&pages,&bank);assert(before);
    w->selected=fx_source_at(second);TsSourceRoute changed=saved;changed.matrix_gain[TS_MATRIX_EFFECT_BASE+b]=300;
    mx_commit_route(0,&audio,&ui,&pages,&bank,changed);mx_push(before);
    assert(cards[1]->output_route.matrix_gain[TS_MATRIX_EFFECT_BASE+b]==300);
    assert(cards[0]->output_route.matrix_gain[TS_MATRIX_EFFECT_BASE+a]==1000);
    assert(ts_gallery_switch(g,1,error,sizeof(error)));
    ui.mosaic=audio.mosaic=pages.mosaic=ts_gallery_active(g);mx_bind_mosaic(&ui,&pages);
    mx_undo(0,&audio,&ui,&pages,&bank,0);assert(!memcmp(&cards[1]->output_route,&saved,sizeof(saved)));
    assert(fx_position(first,0)->x==-310 && fx_position(second,0)->x==-200);
    before=mx_snapshot(0,&audio,&pages,&bank);assert(before);
    mx_effect_remove(0,&audio,&ui,&pages,&bank,a);mx_push(before);
    assert(!cards[0]->output_route.matrix_gain[TS_MATRIX_EFFECT_BASE+a]);
    assert(cards[1]->output_route.matrix_gain[TS_MATRIX_EFFECT_BASE+b]==1000);
    mx_undo(0,&audio,&ui,&pages,&bank,0);
    assert(cards[0]->output_route.matrix_gain[TS_MATRIX_EFFECT_BASE+a]==1000);
    before=mx_snapshot(0,&audio,&pages,&bank);assert(before);
    mx_reset_routes(0,&audio,&ui,&pages,&bank,0);mx_push(before);
    assert(!cards[0]->output_route.matrix_gain[TS_MATRIX_EFFECT_BASE+a] && !cards[1]->output_route.matrix_gain[TS_MATRIX_EFFECT_BASE+b]);
    mx_undo(0,&audio,&ui,&pages,&bank,0);
    assert(cards[0]->output_route.matrix_gain[TS_MATRIX_EFFECT_BASE+a]==1000 && cards[1]->output_route.matrix_gain[TS_MATRIX_EFFECT_BASE+b]==1000);
    /* Return to ALL, then include a card using the implicit Mosaic bus. */
    toolbar_click(FX_TOOL_MOVEMENT);click(100,102,1);assert(!fx_canvas.movement && gallery_cards()==2);
    cards[1]->output_route=(TsSourceRoute){0};mx_rows(&pages,&bank);fx_build(&pages,&bank);assert(gallery_cards()==2);
    toolbar_click(FX_TOOL_MOVEMENT);click(400,90+3*32+12,1);
    assert(gallery_cards()==1 && node(TS_MATRIX_MOSAIC));
    /* A deleted filtered movement cannot leave the view stuck or redirect IDs. */
    assert(ts_gallery_switch(g,0,error,sizeof(error)));
    ui.mosaic=audio.mosaic=pages.mosaic=ts_gallery_active(g);mx_bind_mosaic(&ui,&pages);
    assert(ts_gallery_remove(g,1,error,sizeof(error)));mx_rows(&pages,&bank);fx_build(&pages,&bank);
    assert(!fx_canvas.movement && gallery_cards()==1 && !mx_card(second));
    matrix_history_clear();
}
