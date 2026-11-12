static void edit_settle(void)
{
    for(int f=0;f<1024;++f) {
        ts_matrix_effects_tick(&audio.sister.matrix_effects);
        for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)ts_matrix_effects_process(&audio.sister.matrix_effects,i,(TsStereoFrame){0},(TsStereoFrame){0},0);
    }
    mx_collect_effects(0,&audio);
}
static void edit_wheel(int x,int y,int amount,int mod)
{
    int width,height;SDL_GetWindowSize(matrix_window.window,&width,&height);
    SDL_Event e;SDL_zero(e);e.type=SDL_MOUSEWHEEL;e.wheel.windowID=matrix_window.id;e.wheel.y=amount;
#if SDL_VERSION_ATLEAST(2,26,0)
    e.wheel.mouseX=x*width/MATRIX_W;e.wheel.mouseY=y*height/MATRIX_H;
#else
    SDL_WarpMouseInWindow(matrix_window.window,x*width/MATRIX_W,y*height/MATRIX_H);
#endif
    SDL_SetModState(mod);assert(event(&e));SDL_SetModState(KMOD_NONE);
}
static int edit_slot(uint32_t id)
{for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)if(matrix_window.controls.effect[i].id==id)return i;return -1;}
static void edit_controls(void)
{
    MatrixWindow *w=&matrix_window;edit_settle();
    MatrixSnapshot *saved=mx_snapshot(0,&audio,&pages,&bank);assert(saved);
    MatrixSnapshot *fixture=mx_snapshot(0,&audio,&pages,&bank);assert(fixture);
    TsMatrixControls *c=&fixture->controls;ts_matrix_default(c);c->enabled=1;
    /* Keep IDs globally monotonic even while replacing this test fixture. */
    c->next_effect_id=w->controls.next_effect_id;
    for(int i=0;i<TS_MATRIX_ROWS;++i)c->row[i]=(TsSourceRoute){.matrix_enabled=1};
    int types[]={TS_SISTER_FX_REVERB,TS_SISTER_FX_TRANSIENT,TS_MATRIX_FX_COMPRESSOR,TS_MATRIX_FX_META,TS_MATRIX_FX_SUNVOX};
    uint32_t id[5];
    for(int i=0;i<5;++i){assert(ts_matrix_effect_add(c,types[i])==i);id[i]=c->effect[i].id;}
    assert(ts_matrix_connect(c,TS_MATRIX_EFFECT_BASE,TS_MATRIX_EFFECT_BASE+1,1000)==1);
    assert(ts_matrix_connect(c,TS_MATRIX_EFFECT_BASE,TS_MATRIX_EFFECT_SC_BASE+2,500)==1);
    ts_matrix_layout_default(&fixture->layout);fixture->layout.canvas_snap=0;assert(mx_apply(fixture,0,&audio,&ui,&pages,&bank));free(fixture);
    matrix_history_clear();w->layout=&audio.sister.matrix_layout;fx_view(1,&pages,&bank);fx_canvas.fit=0;
    SDL_SetWindowSize(w->window,1280,840);TsMatrixLayout *l=w->layout;l->canvas_x=l->canvas_y=0;l->canvas_zoom=1;l->canvas_view=1;
    fx_build(&pages,&bank);
    for(int i=0;i<l->canvas_count;++i)l->canvas[i].x=l->canvas[i].y=-5000;
    const float xy[5][2]={{40,50},{390,50},{390,260},{40,260},{740,450}};
    for(int i=0;i<5;++i){TsFxCanvasPosition *p=fx_position(instance(id[i])->row,0);p->x=xy[i][0];p->y=xy[i][1];}
    fx_build(&pages,&bank);edit_settle();
    void *engine=audio.sister.matrix_effects.current[0];assert(engine);
    /* Shift-click toggles modules without creating a self-route. */
    click(80,190,1);assert(fx_chosen(instance(id[0])->row));
    SDL_SetModState(KMOD_SHIFT);click(450,190,1);SDL_SetModState(KMOD_NONE);
    assert(w->selection_count==2 && fx_chosen(instance(id[1])->row));
    assert(!w->controls.row[TS_MATRIX_EFFECT_BASE+1].matrix_gain[TS_MATRIX_EFFECT_BASE+1]);
    SDL_SetModState(KMOD_SHIFT);click(450,190,1);SDL_SetModState(KMOD_NONE);assert(w->selection_count==1);
    SDL_SetModState(KMOD_SHIFT);click(450,190,1);SDL_SetModState(KMOD_NONE);
    int undo=w->history_cursor;pointer(SDL_MOUSEMOTION,450,190,0,0);key(SDLK_g,0);assert(fx_modal());
    pointer(SDL_MOUSEMOTION,510,215,0,0);pointer(SDL_MOUSEBUTTONUP,510,215,SDL_BUTTON_LEFT,1);assert(fx_modal());
    key(SDLK_RETURN,0);assert(!w->drag && w->history_cursor==undo+1);
    fx_build(&pages,&bank);assert(instance(id[0])->x==100 && instance(id[1])->x==450 && instance(id[0])->y==75);
    assert(audio.sister.matrix_effects.current[0]==engine);
    key(SDLK_z,KMOD_CTRL);fx_build(&pages,&bank);assert(instance(id[0])->x==40 && instance(id[1])->x==390);
    undo=w->history_cursor;key(SDLK_g,0);pointer(SDL_MOUSEMOTION,560,240,0,0);key(SDLK_ESCAPE,0);
    fx_build(&pages,&bank);assert(instance(id[0])->x==40 && w->history_cursor==undo);
    pointer(SDL_MOUSEMOTION,300,200,0,0);key(SDLK_g,0);click(325,225,1);
    fx_build(&pages,&bank);assert(!w->drag && instance(id[0])->x==65 && instance(id[1])->x==415);
    key(SDLK_z,KMOD_CTRL);fx_build(&pages,&bank);
    pointer(SDL_MOUSEMOTION,300,200,0,0);key(SDLK_s,0);pointer(SDL_MOUSEMOTION,400,200,0,0);key(SDLK_RETURN,0);
    fx_build(&pages,&bank);FxCanvasNode *a=instance(id[0]),*b=instance(id[1]);
    assert(fabsf(a->scale-sqrtf(2))<.0001f && a->scale==b->scale);
    assert(fabsf(a->x+fx_node_w(a)*.5f-145)<.001f && fabsf(b->x+fx_node_w(b)*.5f-495)<.001f);
    assert(fx_font_scaled(2,a->scale).rows[7]>fx_font(2).rows[7]);view_codec();
    SDL_Surface *frame=rendered_frame();SDL_FreeSurface(frame);
    const char *preview=getenv("TS_FX_PREVIEW");if(preview){char path[1024];snprintf(path,sizeof(path),"%s/edit-controls-scaled.bmp",preview);screenshot(path);}
    key(SDLK_z,KMOD_CTRL);fx_build(&pages,&bank);assert(instance(id[0])->scale==1);
    /* Escape and focus loss restore even an in-flight view edit. */
    pointer(SDL_MOUSEMOTION,300,200,0,0);key(SDLK_s,0);pointer(SDL_MOUSEMOTION,1200,-1000,0,0);key(SDLK_ESCAPE,0);
    fx_build(&pages,&bank);assert(instance(id[0])->scale==1);
    key(SDLK_g,0);pointer(SDL_MOUSEMOTION,500,400,0,0);
    SDL_Event lost;SDL_zero(lost);lost.type=SDL_WINDOWEVENT;lost.window.windowID=w->id;lost.window.event=SDL_WINDOWEVENT_FOCUS_LOST;assert(event(&lost));
    fx_build(&pages,&bank);assert(!w->drag && !w->pending && instance(id[0])->x==40);
    static TsMatrixLayout legacy;ts_matrix_layout_default(&legacy);
    assert(ts_matrix_layout_read(&legacy,"FxCanvas.Node0","4,123,4,8")==1 && legacy.canvas[0].scale==0);
    assert(ts_matrix_layout_read(&legacy,"FxCanvas.Node1","4,124,4,8,nan")==-1);
    assert(ts_matrix_layout_read(&legacy,"FxCanvas.Node1","4,124,4,8,0.1")==-1);
    /* Rectangle crosses both cables while containing neither pair of endpoints. */
    for(int straight=0;straight<2;++straight) {
        l->canvas_straight=straight;
        SDL_SetModState(KMOD_CTRL);pointer(SDL_MOUSEBUTTONDOWN,320,180,SDL_BUTTON_LEFT,1);
        pointer(SDL_MOUSEMOTION,360,470,0,0);pointer(SDL_MOUSEBUTTONUP,360,470,SDL_BUTTON_LEFT,1);SDL_SetModState(KMOD_NONE);
        assert(!w->selection_count && fx_canvas.wires.count==2);
    }
    SDL_SetModState(KMOD_SHIFT);click(340,209,1);SDL_SetModState(KMOD_NONE);assert(fx_canvas.wires.count==1);
    SDL_SetModState(KMOD_SHIFT);click(340,209,1);SDL_SetModState(KMOD_NONE);assert(fx_canvas.wires.count==2);
    SDL_SetModState(KMOD_SHIFT);click(80,190,1);SDL_SetModState(KMOD_NONE);assert(w->selection_count==1 && fx_canvas.wires.count==2);
    /* Cancelling a replacement marquee restores the exact mixed selection. */
    SDL_SetModState(KMOD_CTRL);pointer(SDL_MOUSEBUTTONDOWN,800,150,SDL_BUTTON_LEFT,1);pointer(SDL_MOUSEMOTION,900,200,0,0);
    key(SDLK_ESCAPE,0);SDL_SetModState(KMOD_NONE);assert(w->selection_count==1 && fx_canvas.wires.count==2);
    fx_clear_selection();w->effect_open=0;
    float zoom=l->canvas_zoom;edit_wheel(340,210,1,0);assert(l->canvas_zoom>zoom);
    assert(w->controls.row[TS_MATRIX_EFFECT_BASE].matrix_gain[TS_MATRIX_EFFECT_BASE+1]==1000);
    assert(w->controls.row[TS_MATRIX_EFFECT_BASE].matrix_gain[TS_MATRIX_EFFECT_SC_BASE+2]==500);
    key(SDLK_z,KMOD_CTRL);assert(l->canvas_zoom==zoom);
    /* Inspector wheel edits its parameter; cable gain and canvas zoom stay put. */
    static TsMatrixEffectControls before_effect;before_effect=w->controls.effect[0];w->effect_open=id[0];
    edit_wheel(1100,265,1,0);assert(l->canvas_zoom==zoom && memcmp(&before_effect,&w->controls.effect[0],sizeof(before_effect)));
    assert(w->controls.row[TS_MATRIX_EFFECT_BASE].matrix_gain[TS_MATRIX_EFFECT_BASE+1]==1000);
    key(SDLK_z,KMOD_CTRL);w->effect_open=0;
    edit_wheel(340,210,2,KMOD_SHIFT);
    int g1=w->controls.row[TS_MATRIX_EFFECT_BASE].matrix_gain[TS_MATRIX_EFFECT_BASE+1],g2=w->controls.row[TS_MATRIX_EFFECT_BASE].matrix_gain[TS_MATRIX_EFFECT_SC_BASE+2];
    assert(g1>1000 && abs(g1-2*g2)<=1 && l->canvas_zoom==zoom && audio.sister.matrix_effects.current[0]==engine);
    edit_wheel(340,210,100,KMOD_SHIFT);
    assert(w->controls.row[TS_MATRIX_EFFECT_BASE].matrix_gain[TS_MATRIX_EFFECT_BASE+1]==2000);
    assert(abs(w->controls.row[TS_MATRIX_EFFECT_BASE].matrix_gain[TS_MATRIX_EFFECT_SC_BASE+2]-1000)<=1);
    undo=w->history_cursor;key(SDLK_x,0);
    assert(w->controls.effect[0].id==id[0] && w->controls.effect[1].id==id[1]);
    assert(!w->controls.row[TS_MATRIX_EFFECT_BASE].matrix_gain[TS_MATRIX_EFFECT_BASE+1] && !w->controls.row[TS_MATRIX_EFFECT_BASE].matrix_gain[TS_MATRIX_EFFECT_SC_BASE+2]);
    assert(w->history_cursor==undo+1);key(SDLK_x,0);assert(w->history_cursor==undo+1);key(SDLK_z,KMOD_CTRL);
    /* Resized ports and Shift-drag continue to patch the real Matrix. */
    fx_build(&pages,&bank);TsFxCanvasPosition *p=fx_position(instance(id[0])->row,0);p->scale=1.25f;fx_build(&pages,&bank);a=instance(id[0]);
    pointer(SDL_MOUSEBUTTONDOWN,fx_x(a->x+fx_node_w(a)),fx_y(a->y+fx_node_h(a)*.5f),SDL_BUTTON_LEFT,1);
    pointer(SDL_MOUSEBUTTONUP,410,445,SDL_BUTTON_LEFT,1);assert(w->controls.row[TS_MATRIX_EFFECT_BASE].matrix_gain[TS_MATRIX_EFFECT_SC_BASE+2]==1000);
    p->scale=0;fx_build(&pages,&bank);
    SDL_SetModState(KMOD_SHIFT);pointer(SDL_MOUSEBUTTONDOWN,80,190,SDL_BUTTON_LEFT,1);pointer(SDL_MOUSEMOTION,450,190,0,0);pointer(SDL_MOUSEBUTTONUP,450,190,SDL_BUTTON_LEFT,1);SDL_SetModState(KMOD_NONE);
    assert(!w->controls.row[TS_MATRIX_EFFECT_BASE].matrix_gain[TS_MATRIX_EFFECT_BASE+1]);
    /* Copy complete settings and internal normal, sidechain and delayed edges. */
    w->controls.feedback=1;
    assert(ts_matrix_connect(&w->controls,TS_MATRIX_EFFECT_BASE,TS_MATRIX_EFFECT_BASE+1,700)==1);
    assert(ts_matrix_connect(&w->controls,TS_MATRIX_EFFECT_BASE+1,TS_MATRIX_EFFECT_BASE,300)==1);
    assert(w->controls.delayed[TS_MATRIX_EFFECT_BASE+1]&ts_matrix_feedback_bit(TS_MATRIX_EFFECT_BASE));
    assert(ts_matrix_connect(&w->controls,TS_MATRIX_MAIN,TS_MATRIX_EFFECT_BASE,1000)==1);
    assert(ts_matrix_connect(&w->controls,TS_MATRIX_EFFECT_BASE+2,TS_MATRIX_NODES,1000)==1);
    mx_commit_controls(0,&audio,&ui);edit_settle();fx_build(&pages,&bank);
    fx_clear_selection();fx_clear_wires();fx_select_wire(instance(id[0])->row,TS_MATRIX_EFFECT_BASE+1,0);
    fx_select_wire(instance(id[1])->row,TS_MATRIX_EFFECT_BASE,0);edit_wheel(340,210,100,KMOD_SHIFT);
    assert(w->controls.row[TS_MATRIX_EFFECT_BASE+1].matrix_gain[TS_MATRIX_EFFECT_BASE]==500);
    assert(abs(w->controls.row[TS_MATRIX_EFFECT_BASE].matrix_gain[TS_MATRIX_EFFECT_BASE+1]-1167)<=1);
    key(SDLK_z,KMOD_CTRL);
    /* Paint while patching with an existing multi-wire selection. */
    SDL_SetModState(KMOD_SHIFT);pointer(SDL_MOUSEBUTTONDOWN,80,190,SDL_BUTTON_LEFT,1);pointer(SDL_MOUSEMOTION,340,290,0,0);
    frame=rendered_frame();SDL_FreeSurface(frame);key(SDLK_ESCAPE,0);SDL_SetModState(KMOD_NONE);
    assert(fx_canvas.wires.count==2 && !w->drag);
    /* Old sends to an unused physical slot must not feed its pasted occupant. */
    w->controls.row[TS_MATRIX_MAIN].matrix_gain[TS_MATRIX_EFFECT_BASE+5]=250;
    pages.tracker.lanes[0].output_route=(TsSourceRoute){.matrix_enabled=1};
    pages.tracker.lanes[0].output_route.matrix_gain[TS_MATRIX_EFFECT_BASE+5]=300;
    w->controls.effect[0].a=.73f;w->controls.effect[1].enabled=0;w->controls.effect[2].compressor.external=1;
    w->controls.effect[3].meta.macro[0]=.23f;w->controls.effect[4].sunvox.value[0]=1234;
    mx_commit_controls(0,&audio,&ui);edit_settle();fx_build(&pages,&bank);fx_clear_selection();fx_clear_wires();
    for(int i=0;i<5;++i)fx_select(instance(id[i]),1);
    key(SDLK_c,KMOD_CTRL);assert(fx_clipboard.count==5);undo=w->history_cursor;key(SDLK_v,KMOD_CTRL);
    uint32_t clone[5];for(int i=0;i<5;++i)clone[i]=w->controls.effect[5+i].id;
    assert(clone[0] && w->selection_count==5 && w->history_cursor==undo+1);
    assert(!pages.tracker.lanes[0].output_route.matrix_gain[TS_MATRIX_EFFECT_BASE+5]);
    for(int i=0;i<5;++i) {
        TsMatrixEffectControls check=w->controls.effect[5+i];assert(check.id!=id[i]);check.id=id[i];
        assert(!memcmp(&check,&w->controls.effect[i],sizeof(check)));
        assert(audio.sister.matrix_effects.current[i]!=audio.sister.matrix_effects.current[5+i]);
        for(int d=0;d<TS_MATRIX_DESTINATIONS;++d)if(w->controls.row[TS_MATRIX_EFFECT_BASE+5+i].matrix_gain[d])
            assert((d>=TS_MATRIX_EFFECT_BASE+5 && d<TS_MATRIX_EFFECT_BASE+10) || (d>=TS_MATRIX_EFFECT_SC_BASE+5 && d<TS_MATRIX_EFFECT_SC_BASE+10));
        assert(!w->controls.row[TS_MATRIX_MAIN].matrix_gain[TS_MATRIX_EFFECT_BASE+5+i]);
    }
    assert(audio.sister.matrix_effects.current[0]==engine);
    assert(w->controls.row[TS_MATRIX_EFFECT_BASE+5].matrix_gain[TS_MATRIX_EFFECT_BASE+6]==700);
    assert(w->controls.row[TS_MATRIX_EFFECT_BASE+5].matrix_gain[TS_MATRIX_EFFECT_SC_BASE+7]==1000);
    assert(w->controls.delayed[TS_MATRIX_EFFECT_BASE+6]&ts_matrix_feedback_bit(TS_MATRIX_EFFECT_BASE+5));
    view_codec();edit_settle();key(SDLK_z,KMOD_CTRL);assert(edit_slot(clone[0])<0);edit_settle();key(SDLK_y,KMOD_CTRL);assert(edit_slot(clone[0])>=0);edit_settle();
    /* Mixed deletion is one edit; a wire alone never implies its endpoint. */
    fx_build(&pages,&bank);fx_clear_selection();fx_select(instance(clone[0]),0);fx_clear_wires();fx_select_wire(instance(id[0])->row,TS_MATRIX_EFFECT_BASE+1,0);
    undo=w->history_cursor;key(SDLK_DELETE,0);assert(fx_canvas.confirm_delete);key(SDLK_n,0);assert(edit_slot(clone[0])<0 && edit_slot(id[0])>=0 && edit_slot(id[1])>=0);
    assert(!w->controls.row[TS_MATRIX_EFFECT_BASE].matrix_gain[TS_MATRIX_EFFECT_BASE+1] && w->history_cursor==undo+1);
    edit_settle();key(SDLK_z,KMOD_CTRL);assert(edit_slot(clone[0])>=0 && w->controls.row[TS_MATRIX_EFFECT_BASE].matrix_gain[TS_MATRIX_EFFECT_BASE+1]==700);edit_settle();
    /* Not enough room must not paste a partial branch or spend instance IDs. */
    while(ts_matrix_effect_add(&w->controls,TS_SISTER_FX_DISTORTION)>=0){}
    mx_commit_controls(0,&audio,&ui);edit_settle();static TsMatrixControls full;full=w->controls;undo=w->history_cursor;
    key(SDLK_v,KMOD_CTRL);assert(!memcmp(&full,&w->controls,sizeof(full)) && w->history_cursor==undo && strstr(w->status,"NOTHING PASTED"));
    /* A replaced physical destination must not inherit an old wire selection. */
    fx_build(&pages,&bank);fx_clear_selection();fx_clear_wires();fx_select_wire(instance(id[0])->row,TS_MATRIX_EFFECT_BASE+1,0);
    ts_matrix_effect_remove(&w->controls,1);assert(ts_matrix_effect_add(&w->controls,TS_SISTER_FX_DELAY)==1);
    assert(ts_matrix_connect(&w->controls,TS_MATRIX_EFFECT_BASE,TS_MATRIX_EFFECT_BASE+1,600)==1);
    mx_commit_controls(0,&audio,&ui);mx_rows(&pages,&bank);fx_build(&pages,&bank);key(SDLK_x,0);
    assert(w->controls.row[TS_MATRIX_EFFECT_BASE].matrix_gain[TS_MATRIX_EFFECT_BASE+1]==600 && !fx_canvas.wires.count);
    edit_settle();assert(mx_apply(saved,0,&audio,&ui,&pages,&bank));free(saved);edit_settle();matrix_history_clear();
    fx_view(1,&pages,&bank);fx_canvas.fit=0;fx_build(&pages,&bank);
    puts("Seven FX editing controls passed: modal transforms, cable selection/gain, atomic branch copy/delete and recall.");
}
