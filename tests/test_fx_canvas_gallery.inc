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
    float samples[16]={.1f};TsSample sample={.data=samples,.frames=16,.channels=1,.sample_rate=48000};
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
    TsFxCanvasPosition *p=fx_position(first,1),*q=fx_position(second,1);assert(p!=q);
    p->x=-310;p->y=100;q->x=-200;q->y=410;
    view_codec();
    /* Overview headers must still paint at the scale needed for all movements. */
    w->layout->canvas_zoom=.2f;w->layout->canvas_x=500;w->layout->canvas_y=100;
    SDL_Surface *frame=rendered_frame();FxFontMetrics font=fx_font_scaled(1,2.f/3);
    int hx=mx_px(fx_x(p->x)),hy=mx_py(fx_y(p->y)-fx_line_height(font)-1);
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
    click(400,90+2*32+12,1);assert(fx_canvas.movement==second.canvas);
    assert(gallery_cards()==1 && !instance(controls.effect[a].id) && instance(controls.effect[b].id));
    assert(node(TS_MATRIX_MAIN) && node(TS_MATRIX_EQ) && node(TS_MATRIX_LIMITER));
    assert(g->active==0 && !g->canvas[0].scene->playing && g->canvas[0].scene->time==0);
    assert(!memcmp(&cards[1]->output_route,&saved,sizeof(saved)) && audio.sister.matrix_effects.current[a]==live);
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
    toolbar_click(FX_TOOL_MOVEMENT);click(400,90+2*32+12,1);
    assert(gallery_cards()==1 && node(TS_MATRIX_MOSAIC));
    /* A deleted filtered movement cannot leave the view stuck or redirect IDs. */
    assert(ts_gallery_switch(g,0,error,sizeof(error)));
    ui.mosaic=audio.mosaic=pages.mosaic=ts_gallery_active(g);mx_bind_mosaic(&ui,&pages);
    assert(ts_gallery_remove(g,1,error,sizeof(error)));mx_rows(&pages,&bank);fx_build(&pages,&bank);
    assert(!fx_canvas.movement && gallery_cards()==1 && !mx_card(second));
    matrix_history_clear();
}
