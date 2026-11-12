static void properties_click(int x,int y)
{
    int width,height;SDL_GetWindowSize(fx_properties_window.window,&width,&height);
    SDL_Event e;SDL_zero(e);e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=fx_properties_window.id;
    e.button.button=SDL_BUTTON_LEFT;e.button.x=x*width/FX_PROPERTIES_W;e.button.y=y*height/FX_PROPERTIES_H;
    assert(event(&e));e.type=SDL_MOUSEBUTTONUP;assert(event(&e));
}
static void patch_workflow(void)
{
    MatrixWindow *w=&matrix_window;edit_settle();MatrixSnapshot *saved=mx_snapshot(0,&audio,&pages,&bank);assert(saved);
    w->controls.enabled=1;for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)ts_matrix_effect_remove(&w->controls,i);
    int dist=ts_matrix_effect_add(&w->controls,TS_SISTER_FX_DISTORTION),comp=ts_matrix_effect_add(&w->controls,TS_MATRIX_FX_COMPRESSOR);
    uint32_t id=w->controls.effect[dist].id,cid=w->controls.effect[comp].id;
    assert(ts_matrix_connect(&w->controls,TS_MATRIX_EFFECT_BASE+dist,TS_MATRIX_EFFECT_BASE+comp,800)==1);
    assert(ts_matrix_connect(&w->controls,TS_MATRIX_EFFECT_BASE+comp,TS_MATRIX_MAIN,1000)==1);
    mx_commit_controls(0,&audio,&ui);mx_rows(&pages,&bank);fx_build(&pages,&bank);fx_fit();
    fx_clear_selection();fx_clear_wires();fx_select(node(TS_MATRIX_MOSAIC),0);fx_select(node(TS_MATRIX_FM),1);
    FxCanvasNode *dest=instance(id);int undo=w->history_cursor;
    SDL_SetModState(KMOD_CTRL|KMOD_SHIFT);click(fx_x(dest->x)+10,fx_y(dest->y)+10,1);SDL_SetModState(0);
    assert(w->selection_count==2 && w->controls.row[TS_MATRIX_MOSAIC].matrix_gain[TS_MATRIX_EFFECT_BASE+dist]==1000);
    assert(w->controls.row[TS_MATRIX_FM].matrix_gain[TS_MATRIX_EFFECT_BASE+dist]==1000 && w->history_cursor==undo+1);
    key(SDLK_z,KMOD_CTRL);assert(w->selection_count==2 && !w->controls.row[TS_MATRIX_MOSAIC].matrix_gain[TS_MATRIX_EFFECT_BASE+dist]);
    key(SDLK_y,KMOD_CTRL);assert(w->selection_count==2);edit_settle();fx_build(&pages,&bank);
    fx_select(instance(id),0);key(SDLK_DELETE,0);assert(fx_canvas.confirm_delete && edit_slot(id)>=0);
    key(SDLK_ESCAPE,0);assert(!fx_canvas.confirm_delete && edit_slot(id)>=0);
    key(SDLK_DELETE,0);key(SDLK_y,0);assert(edit_slot(id)<0 && edit_slot(cid)==comp);
    assert(w->controls.row[TS_MATRIX_MOSAIC].matrix_gain[TS_MATRIX_EFFECT_BASE+comp]==800);
    assert(w->controls.row[TS_MATRIX_FM].matrix_gain[TS_MATRIX_EFFECT_BASE+comp]==800);
    assert(w->controls.row[TS_MATRIX_EFFECT_BASE+comp].matrix_gain[TS_MATRIX_MAIN]==1000);
    edit_settle();key(SDLK_z,KMOD_CTRL);edit_settle();fx_build(&pages,&bank);fx_select(instance(id),0);
    key(SDLK_DELETE,0);key(SDLK_n,0);assert(edit_slot(id)<0 && !w->controls.row[TS_MATRIX_MOSAIC].matrix_gain[TS_MATRIX_EFFECT_BASE+comp]);
    edit_settle();key(SDLK_z,KMOD_CTRL);edit_settle();fx_build(&pages,&bank);fx_select(instance(id),0);
    /* F2 properties commit once and survive layout persistence and undo. */
    int split=w->layout->canvas_split;w->layout->canvas_split=TS_FX_CANVAS_SPLIT_MIN;
    key(SDLK_F2,0);assert(fx_canvas.properties);
    Uint32 flags=SDL_GetWindowFlags(fx_properties_window.window);
    assert((flags&SDL_WINDOW_ALWAYS_ON_TOP) && !(flags&(SDL_WINDOW_MAXIMIZED|SDL_WINDOW_FULLSCREEN|SDL_WINDOW_BORDERLESS)));
    int width,height;SDL_GetWindowSize(fx_properties_window.window,&width,&height);
    assert(width==FX_PROPERTIES_W && height==FX_PROPERTIES_H);
    SDL_SetWindowPosition(fx_properties_window.window,42,64);
    SDL_SetWindowSize(fx_properties_window.window,FX_PROPERTIES_W*2,FX_PROPERTIES_H*2);
    SDL_Event text;SDL_zero(text);text.type=SDL_TEXTINPUT;text.text.windowID=fx_properties_window.id;
    snprintf(text.text.text,sizeof(text.text.text),"ANGELIC DRIVE");assert(event(&text));
    /* A click in the canvas cannot type into or commit the floating editor. */
    SDL_Event other=text;other.text.windowID=w->id;snprintf(other.text.text,sizeof(other.text.text),"WRONG WINDOW");
    assert(event(&other));assert(!strcmp(fx_canvas.properties_name,"ANGELIC DRIVE"));
    key(SDLK_RETURN,KMOD_ALT);assert(fx_canvas.properties && !fx_position(instance(id)->row,0)->name[0]);
    properties_click(16+8*33+10,110);uint32_t color=fx_canvas.properties_color;assert(color==fx_swatch(8));
    const char *preview=getenv("TS_FX_PREVIEW");if(preview){char path[1024];snprintf(path,sizeof(path),"%s/card-properties.bmp",preview);fx_properties_paint();
        SDL_Surface *image=SDL_CreateRGBSurfaceWithFormatFrom(fx_properties_window.pixels,FX_PROPERTIES_W,FX_PROPERTIES_H,32,FX_PROPERTIES_W*4,SDL_PIXELFORMAT_ARGB8888);
        assert(image && !SDL_SaveBMP(image,path));SDL_FreeSurface(image);}
    key(SDLK_RETURN,0);w->layout->canvas_split=split;TsFxCanvasPosition *position=fx_position(instance(id)->row,0);assert(position);
    assert(!strcmp(position->name,"ANGELIC DRIVE") && position->color==color);view_codec();
    key(SDLK_z,KMOD_CTRL);assert(!position->name[0] && !position->color);key(SDLK_y,KMOD_CTRL);view_codec();
    /* Reopening preserves placement; Cancel and the title-bar close do not commit. */
    key(SDLK_F2,0);int moved_x,moved_y;SDL_GetWindowPosition(fx_properties_window.window,&moved_x,&moved_y);
    assert(moved_x==42 && moved_y==64);
    properties_click(440,294);assert(!fx_canvas.properties_name[0] && !fx_canvas.properties_color);
    properties_click(210,294);assert(!fx_canvas.properties && !strcmp(position->name,"ANGELIC DRIVE"));
    key(SDLK_F2,0);SDL_Event close;SDL_zero(close);close.type=SDL_WINDOWEVENT;close.window.windowID=fx_properties_window.id;
    close.window.event=SDL_WINDOWEVENT_CLOSE;assert(event(&close));assert(!fx_canvas.properties && w->visible);
    key(SDLK_F2,0);properties_click(440,294);properties_click(70,294);
    assert(!fx_canvas.properties && !position->name[0] && !position->color);
    key(SDLK_z,KMOD_CTRL);assert(!strcmp(position->name,"ANGELIC DRIVE") && position->color==color);
    key(SDLK_F2,0);matrix_hide();assert(!fx_canvas.properties && (SDL_GetWindowFlags(fx_properties_window.window)&SDL_WINDOW_HIDDEN));
    w->visible=1;SDL_ShowWindow(w->window);
    /* Native replacement uses the actual picker and preserves all main cables. */
    w->effect_open=id;click(1095,708,1);assert(fx_canvas.replace_id==id && fx_canvas.popup);
    int x=fx_canvas.popup_x,y=fx_canvas.popup_y;
    click(x+40,y+32+(TS_SISTER_FX_DELAY-1)*30+12,1);
    assert(!fx_canvas.popup && edit_slot(id)==dist && w->controls.effect[dist].type==TS_SISTER_FX_DELAY);
    assert(w->controls.row[TS_MATRIX_MOSAIC].matrix_gain[TS_MATRIX_EFFECT_BASE+dist]==1000);
    assert(w->controls.row[TS_MATRIX_EFFECT_BASE+dist].matrix_gain[TS_MATRIX_EFFECT_BASE+comp]==800);
    assert(!strcmp(position->name,"ANGELIC DRIVE") && position->color==color);
    fx_copy();assert(fx_clipboard.count==1 && fx_clipboard.position[0].color==color && !strcmp(fx_clipboard.position[0].name,"ANGELIC DRIVE"));
    edit_settle();key(SDLK_z,KMOD_CTRL);assert(w->controls.effect[dist].type==TS_SISTER_FX_DISTORTION);edit_settle();
    /* Caret blinking changes ink, never the underlying text or cursor offset. */
    fx_label_begin(-1);snprintf(fx_canvas.label_text,sizeof(fx_canvas.label_text),"ANGELIC\nNOISE");fx_canvas.label_cursor=7;
    SDL_Surface *on=rendered_frame();fx_canvas.text_blink=SDL_GetTicks()-600;SDL_Surface *off=rendered_frame();
    assert(memcmp(on->pixels,off->pixels,(size_t)on->h*on->pitch));assert(!strcmp(fx_canvas.label_text,"ANGELIC\nNOISE") && fx_canvas.label_cursor==7);
    SDL_FreeSurface(on);SDL_FreeSurface(off);key(SDLK_ESCAPE,0);
    assert(mx_apply(saved,0,&audio,&ui,&pages,&bank));free(saved);edit_settle();matrix_history_clear();fx_view(1,&pages,&bank);
    puts("Bulk patch/undo selection, retained/disconnected deletion, native replacement, properties persistence and blinking caret passed.");
}
static void patch_twenty_cards(void)
{
    MatrixWindow *w=&matrix_window;char error[256];TsMosaicGallery *g=pages.gallery;assert(g && g->count==1);
    assert(ts_gallery_add(g,error,sizeof(error)));
    float data[16]={.1f};TsSample sample={.data=data,.frames=16,.channels=1,.sample_rate=48000};
    TsMosaicEvent *cards[20];
    for(int c=0;c<2;++c) {
        TsMosaic *m=g->canvas[c].scene;TsMosaicSource *source=ts_mosaic_source(m,&sample,error,sizeof(error));assert(source);
        for(int i=0;i<10;++i){cards[c*10+i]=ts_mosaic_add(m,source,i*2,i*150);assert(cards[c*10+i]);}
    }
    MatrixSnapshot *saved=mx_snapshot(0,&audio,&pages,&bank);assert(saved);
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)ts_matrix_effect_remove(&w->controls,i);
    int a=ts_matrix_effect_add(&w->controls,TS_SISTER_FX_DISTORTION),b=ts_matrix_effect_add(&w->controls,TS_MATRIX_FX_COMPRESSOR);
    int da=TS_MATRIX_EFFECT_BASE+a,db=TS_MATRIX_EFFECT_BASE+b;uint32_t id=w->controls.effect[a].id;
    assert(ts_matrix_connect(&w->controls,da,db,1000)==1);
    assert(ts_matrix_connect(&w->controls,db,TS_MATRIX_MAIN,1000)==1);
    mx_commit_controls(0,&audio,&ui);mx_rows(&pages,&bank);fx_build(&pages,&bank);fx_fit();fx_clear_selection();
    for(int i=0;i<20;++i)w->selection[w->selection_count++]=gallery_card(g->canvas[i/10].id,cards[i]->id);
    FxCanvasNode *dest=instance(id);
    SDL_SetModState(KMOD_CTRL|KMOD_SHIFT);click(fx_x(dest->x+fx_node_w(dest)),fx_y(dest->y+fx_node_h(dest)*.5f),1);SDL_SetModState(0);
    for(int i=0;i<20;++i)assert(cards[i]->output_route.matrix_gain[da]==1000);
    key(SDLK_z,KMOD_CTRL);assert(w->selection_count==20);
    for(int i=0;i<20;++i)assert(!cards[i]->output_route.matrix_gain[da]);
    key(SDLK_y,KMOD_CTRL);edit_settle();
    /* A gain overflow refuses the entire splice, including hidden movements. */
    cards[19]->output_route.matrix_gain[db]=1900;
    fx_canvas.movement=g->canvas[0].id;fx_build(&pages,&bank);fx_select(instance(id),0);
    key(SDLK_DELETE,0);key(SDLK_y,0);assert(fx_canvas.confirm_delete && edit_slot(id)>=0);
    for(int i=0;i<20;++i)assert(cards[i]->output_route.matrix_gain[da]==1000);
    cards[19]->output_route.matrix_gain[db]=0;key(SDLK_y,0);assert(!fx_canvas.confirm_delete && edit_slot(id)<0);
    for(int i=0;i<20;++i)assert(!cards[i]->output_route.matrix_gain[da] && cards[i]->output_route.matrix_gain[db]==1000);
    edit_settle();key(SDLK_z,KMOD_CTRL);edit_settle();
    /* Replacing a compressor drops only its sidechain and Undo restores it. */
    w->controls.row[TS_MATRIX_FM].matrix_gain[TS_MATRIX_EFFECT_SC_BASE+b]=500;mx_commit_controls(0,&audio,&ui);
    w->effect_open=w->controls.effect[b].id;click(1095,708,1);
    click(fx_canvas.popup_x+40,fx_canvas.popup_y+32+(TS_SISTER_FX_DELAY-1)*30+12,1);
    assert(w->controls.effect[b].type==TS_SISTER_FX_DELAY && !w->controls.row[TS_MATRIX_FM].matrix_gain[TS_MATRIX_EFFECT_SC_BASE+b]);
    assert(w->controls.row[da].matrix_gain[db]==1000 && w->controls.row[db].matrix_gain[TS_MATRIX_MAIN]==1000);
    edit_settle();key(SDLK_z,KMOD_CTRL);assert(w->controls.row[TS_MATRIX_FM].matrix_gain[TS_MATRIX_EFFECT_SC_BASE+b]==500);edit_settle();
    assert(mx_apply(saved,0,&audio,&ui,&pages,&bank));free(saved);edit_settle();
    for(int i=0;i<10;++i)ts_mosaic_delete(g->canvas[0].scene,cards[i]->id);
    assert(ts_gallery_remove(g,1,error,sizeof(error)));fx_canvas.movement=0;matrix_history_clear();fx_view(1,&pages,&bank);
    puts("Twenty-card patch, selected-source undo, hidden-movement retain links, atomic overflow rejection and sidechain replacement passed.");
}
