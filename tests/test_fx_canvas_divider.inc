static void divider_controls(const char *dir,uint32_t effect_id)
{
    MatrixWindow *w=&matrix_window;TsMatrixLayout *l=w->layout;
    static TsMatrixLayout saved;saved=*l;
    int oldw,oldh;SDL_GetWindowSize(w->window,&oldw,&oldh);SDL_SetWindowSize(w->window,MATRIX_W,MATRIX_H);
    fx_build(&pages,&bank);FxCanvasNode *n=instance(effect_id);assert(n);fx_select(n,0);w->effect_open=effect_id;
    TsMatrixEffectNode *engine=audio.sister.matrix_effects.current[mx_effect_slot()];
    int count=w->history_count;MatrixSnapshot *original=w->original;
    FxFontMetrics font=fx_font(2);
    pointer(SDL_MOUSEBUTTONDOWN,1004,400,SDL_BUTTON_LEFT,1);
    assert(w->drag==FX_DRAG_DIVIDER && !w->pending && w->original==original);
    pointer(SDL_MOUSEMOTION,784,400,0,0);
    assert(l->canvas_split==780 && w->history_count==count && !w->pending);
    pointer(SDL_MOUSEBUTTONUP,784,400,SDL_BUTTON_LEFT,1);assert(!w->drag);
    assert(l->canvas_x==saved.canvas_x && l->canvas_y==saved.canvas_y && l->canvas_zoom==saved.canvas_zoom);
    assert(!memcmp(l->canvas,saved.canvas,sizeof(l->canvas)));
    FxFontMetrics after=fx_font(2);assert(!memcmp(&font,&after,sizeof(font)));
    assert(audio.sister.matrix_effects.current[mx_effect_slot()]==engine);
    view_codec();
    /* Cancelling a graph gesture restores its coordinates without resetting the pane. */
    key(SDLK_g,0);assert(w->drag==FX_DRAG_GRAB);
    pointer(SDL_MOUSEMOTION,500,300,0,0);key(SDLK_ESCAPE,0);
    assert(l->canvas_split==780 && !w->drag && !w->pending);
    pointer(SDL_MOUSEBUTTONDOWN,600,400,SDL_BUTTON_MIDDLE,1);
    pointer(SDL_MOUSEMOTION,640,450,0,0);key(SDLK_ESCAPE,0);
    assert(l->canvas_split==780 && l->canvas_x==saved.canvas_x && l->canvas_y==saved.canvas_y);
    matrix_hide();assert(matrix_show(0,&audio,&ui,&pages,&bank,-1,-1));fx_view(1,&pages,&bank);
    assert(l->canvas_split==780);fx_build(&pages,&bank);fx_select(instance(effect_id),0);w->effect_open=effect_id;
    /* The widened inspector consumes wheel and slider input; the graph cannot zoom underneath it. */
    fx_inspector_build();float value=fx_inspector.control[0].value,zoom=l->canvas_zoom;
    SDL_Event wheel;SDL_zero(wheel);wheel.type=SDL_MOUSEWHEEL;wheel.wheel.windowID=w->id;wheel.wheel.y=value>.5f?-1:1;
#if SDL_VERSION_ATLEAST(2,26,0)
    wheel.wheel.mouseX=850;wheel.wheel.mouseY=250;
#else
    SDL_WarpMouseInWindow(w->window,850,250);
#endif
    assert(event(&wheel));fx_inspector_build();
    assert(fx_inspector.control[0].value!=value && l->canvas_zoom==zoom);
    key(SDLK_z,KMOD_CTRL);assert(l->canvas_split==780);fx_inspector_build();assert(fx_inspector.control[0].value==value);
    click(1100,270,1);assert(l->canvas_zoom==zoom && !w->drag);
    pointer(SDL_MOUSEBUTTONDOWN,784,400,SDL_BUTTON_LEFT,1);pointer(SDL_MOUSEMOTION,764,400,0,0);
    pointer(SDL_MOUSEBUTTONUP,764,400,SDL_BUTTON_LEFT,1);key(SDLK_z,KMOD_CTRL);
    assert(l->canvas_split==760);l->canvas_split=780;
    fx_popup_open(760,400);assert(fx_canvas.popup_x+FX_POPUP_W<fx_right());fx_canvas.popup=0;
    pointer(SDL_MOUSEBUTTONDOWN,784,400,SDL_BUTTON_LEFT,1);pointer(SDL_MOUSEMOTION,100,400,0,0);
    assert(l->canvas_split==TS_FX_CANVAS_SPLIT_MIN);key(SDLK_ESCAPE,0);assert(l->canvas_split==780 && !w->drag && !w->pending);
    pointer(SDL_MOUSEBUTTONDOWN,784,400,SDL_BUTTON_LEFT,1);pointer(SDL_MOUSEMOTION,1250,400,0,0);
    assert(l->canvas_split==TS_FX_CANVAS_SPLIT_MAX);
    SDL_Event focus;SDL_zero(focus);focus.type=SDL_WINDOWEVENT;focus.window.windowID=w->id;focus.window.event=SDL_WINDOWEVENT_FOCUS_LOST;
    assert(event(&focus));assert(l->canvas_split==780 && !w->drag);
    /* Resizing the OS window keeps the same split ratio and readable text. */
    const int sizes[][2]={{960,630},{1366,699},{1920,1080}};
    for(int i=0;i<3;++i) {
        SDL_SetWindowSize(w->window,sizes[i][0],sizes[i][1]);
        l->canvas_split=i==0?TS_FX_CANVAS_SPLIT_MAX:i==1?TS_FX_CANVAS_SPLIT_MIN:780;
        SDL_Surface *frame=rendered_frame();check_letter(frame,fx_inspector_x(),125,'E',2,FX_FOCUS);
        if(dir){char path[1024];snprintf(path,sizeof(path),"%s/divider-%d.bmp",dir,i);assert(!SDL_SaveBMP(frame,path));}
        SDL_FreeSurface(frame);
    }
    SDL_SetWindowSize(w->window,MATRIX_W,MATRIX_H);click(784,400,2);
    assert(l->canvas_split==TS_FX_CANVAS_SPLIT_DEFAULT && !w->drag && !w->pending);
    l->canvas_split=TS_FX_CANVAS_SPLIT_MIN;
    /* FIT from the inner Meta view still frames the outer graph for its own pane. */
    fx_canvas.meta=effect_id;assert(fx_right()==1260);fx_fit();fx_canvas.meta=0;
    for(int i=0;i<fx_canvas.count;++i){n=&fx_canvas.node[i];assert(fx_x(n->x)>=20 && fx_x(n->x+fx_node_w(n))<=fx_right());}
    view_codec();
    assert(ts_matrix_layout_read(l,"FxCanvas.Split","639")==-1);
    assert(ts_matrix_layout_read(l,"FxCanvas.Split","1041")==-1);
    assert(ts_matrix_layout_read(l,"FxCanvas.Split","800x")==-1);
    assert(ts_matrix_layout_read(l,"FxCanvas.Split","-1")==-1);
    *l=saved;SDL_SetWindowSize(w->window,oldw,oldh);fx_build(&pages,&bank);
}
