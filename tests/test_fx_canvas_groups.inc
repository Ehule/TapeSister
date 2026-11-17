/* Follow the application's native Gallery -> Matrix -> Mosaic dispatch order.
   Matrix can forward unhandled keys by rewriting their window ID. */
static int rug_shortcut(Uint32 window,SDL_Keymod mod,int repeat)
{
    static PortalController portal;static SisterWindow sister;static TransformController transform;
    MosaicController mosaic={.pages=&pages};SDL_Event e;SDL_zero(e);
    e.type=SDL_KEYDOWN;e.key.windowID=window;e.key.keysym.sym=SDLK_g;e.key.keysym.mod=mod;e.key.repeat=repeat;
    if(gallery_windows_event(&e,0,&audio,&ui,&bank,&mosaic))return 1;
    if(event(&e)){assert(e.key.windowID==window);return 2;}
    if(event_window_id(&e)==SDL_GetWindowID(main_window) &&
       mosaic_event(&e,main_window,0,&audio,&ui,&bank,&mosaic,&portal,&sister,&transform,48000))return 3;
    return 0;
}
static void rug_pixels_current(void)
{
    /* Reusing the cable raster must match a fresh render of the current rug. */
    SDL_Surface *cached=rendered_frame();fx_cable_cache.valid=0;
    SDL_Surface *fresh=rendered_frame();assert(cached->w==fresh->w && cached->h==fresh->h);
    for(int y=0;y<fresh->h;++y)assert(!memcmp((char *)cached->pixels+y*cached->pitch,
        (char *)fresh->pixels+y*fresh->pitch,(size_t)fresh->w*4));
    SDL_FreeSurface(cached);SDL_FreeSurface(fresh);
}
static void rug_groups(void)
{
    MatrixWindow *w=&matrix_window;edit_settle();MatrixSnapshot *saved=mx_snapshot(0,&audio,&pages,&bank);assert(saved);
    TsMatrixLayout *l=w->layout;l->canvas_snap=0;fx_canvas.movement=0;fx_canvas.active_view=0;fx_build(&pages,&bank);
    /* Unavailable grouping must stay in FX Canvas, never open Gallery. */
    w->original_active=1;assert(rug_shortcut(w->id,KMOD_CTRL,0)==2);
    assert(!ui.mosaic_gallery_open && !l->group_count);w->original_active=0;
    fx_canvas.meta=1;assert(rug_shortcut(w->id,KMOD_CTRL,0)==2);
    assert(!ui.mosaic_gallery_open && !l->group_count);fx_canvas.meta=0;
    fx_clear_selection();assert(rug_shortcut(w->id,KMOD_CTRL,0)==2);
    assert(!ui.mosaic_gallery_open && !l->group_count);
    FxCanvasNode *a=node(TS_MATRIX_MAIN),*b=node(TS_MATRIX_PRISM);assert(a && b);MatrixRow ar=a->row,br=b->row;
    TsFxCanvasPosition *pa=fx_position(ar,1),*pb=fx_position(br,1);pa->x=100;pa->y=100;pb->x=450;pb->y=100;
    int label=l->label_count<TS_FX_LABEL_LIMIT?l->label_count++:0;l->labels[label]=(TsFxCanvasLabel){.x=100,.y=260,.scale=1};strcpy(l->labels[label].text,"ANGELIC NOISE\nSHARED FINISHING");
    fx_build(&pages,&bank);fx_clear_selection();fx_select(node(TS_MATRIX_MAIN),0);fx_select(node(TS_MATRIX_PRISM),1);fx_canvas.labels[label]=1;
    int history=w->history_cursor;assert(rug_shortcut(w->id,KMOD_CTRL,0)==2);
    assert(!ui.mosaic_gallery_open && l->group_count==1 && w->history_cursor==history+1);
    uint32_t id=fx_canvas.group_selected;assert(id && pa->group==id && pb->group==id && l->labels[label].group==id);
    assert(rug_shortcut(w->id,KMOD_CTRL,1)==2 && l->group_count==1 && w->history_cursor==history+1);
    /* Mosaic still opens Gallery while FX Canvas is visible. Native Gallery
       must also leave a grouping key addressed to FX Canvas alone. */
    assert(rug_shortcut(SDL_GetWindowID(main_window),KMOD_CTRL,0)==3 && ui.mosaic_gallery_open);
    w->original_active=1;assert(rug_shortcut(w->id,KMOD_CTRL,0)==2);
    assert(ui.mosaic_gallery_detached && gallery_windows[0].visible && l->group_count==1);w->original_active=0;
    ui.mosaic_gallery_open=0;gallery_windows_sync(&ui);gallery_windows_close();
    key(SDLK_z,KMOD_CTRL);assert(!l->group_count && !pa->group);key(SDLK_y,KMOD_CTRL);assert(l->group_count==1 && pa->group==id);
    edit_settle();fx_canvas.group_selected=id;fx_fit_selection(1);fx_build(&pages,&bank);TsFxCanvasGroup original=*fx_group(id);
    rug_pixels_current();cache_poll();uint64_t frames=mx_present_count;
    click(fx_right()-8,FX_TOP+8,1);assert(!fx_canvas.group_selected);cache_poll();
    assert(mx_present_count>frames);rug_pixels_current();
    float oldx=pa->x,oldy=pa->y,oldlabel=l->labels[label].x;SDL_Rect r=fx_group_rect(fx_group(id));
    pointer(SDL_MOUSEBUTTONDOWN,r.x+12,r.y+12,SDL_BUTTON_LEFT,1);assert(w->drag==FX_DRAG_GROUP);
    rug_pixels_current();
    pointer(SDL_MOUSEMOTION,r.x+72,r.y+42,0,0);pointer(SDL_MOUSEBUTTONUP,r.x+72,r.y+42,SDL_BUTTON_LEFT,1);
    float dx=fx_group(id)->x-original.x,dy=fx_group(id)->y-original.y;
    assert(dx>0 && dy>0 && fabsf(pa->x-oldx-dx)<.01f && fabsf(pa->y-oldy-dy)<.01f && fabsf(l->labels[label].x-oldlabel-dx)<.01f);
    key(SDLK_z,KMOD_CTRL);assert(fabsf(pa->x-oldx)<.01f);key(SDLK_y,KMOD_CTRL);edit_settle();fx_build(&pages,&bank);
    /* A member may move, but cannot be dragged or scaled outside its rug. */
    fx_clear_selection();fx_select(node(TS_MATRIX_MAIN),0);fx_canvas.mouse_x=400;fx_canvas.mouse_y=400;
    key(SDLK_g,0);pointer(SDL_MOUSEMOTION,-2000,-2000,0,0);key(SDLK_RETURN,0);
    TsFxCanvasGroup *g=fx_group(id);assert(pa->x>=g->x+7.9f && pa->y>=g->y+31.9f);
    key(SDLK_s,0);pointer(SDL_MOUSEMOTION,3000,0,0,0);key(SDLK_RETURN,0);assert(pa->x+FX_NODE_W*pa->scale<=g->x+g->w+.01f && pa->y+FX_NODE_H*pa->scale<=g->y+g->h+.01f);
    key(SDLK_z,KMOD_CTRL);key(SDLK_z,KMOD_CTRL);edit_settle();fx_build(&pages,&bank);
    /* Smaller overlapping rug wins even when stored before a bigger one. */
    TsFxCanvasGroup large=*g;large.id=id+1;large.x-=30;large.y-=30;large.w+=60;large.h+=60;l->groups[l->group_count++]=large;
    r=fx_group_rect(g);assert(fx_group_hit(r.x+10,r.y+10,0)==id && fx_group_hit(r.x+10,r.y+10,1)==id);
    pointer(SDL_MOUSEBUTTONDOWN,r.x+10,r.y+10,SDL_BUTTON_RIGHT,1);pointer(SDL_MOUSEBUTTONUP,r.x+10,r.y+10,SDL_BUTTON_RIGHT,1);
    context_action(FX_CONTEXT_PROPERTIES);
    assert(fx_canvas.properties && fx_canvas.properties_group==id);properties_click(16+8*33+10,110);properties_click(334,258);
    SDL_Event text;SDL_zero(text);text.type=SDL_TEXTINPUT;text.text.windowID=fx_properties_window.id;strcpy(text.text.text,"SHARED FINISHING");assert(event(&text));key(SDLK_RETURN,0);
    assert(!strcmp(g->name,"SHARED FINISHING") && g->color==fx_swatch(8) && fabsf(g->opacity-.5f)<.01f);
    rug_pixels_current();
    assert(ts_matrix_layout_valid(l));FILE *f=tmpfile();assert(f && ts_matrix_layout_write(f,l));rewind(f);static TsMatrixLayout loaded;ts_matrix_layout_default(&loaded);
    char line[2048];while(fgets(line,sizeof(line),f)){line[strcspn(line,"\r\n")]=0;char *eq=strchr(line,'=');assert(eq);*eq++=0;assert(ts_matrix_layout_read(&loaded,line,eq)==1);}fclose(f);
    assert(ts_matrix_layout_valid(&loaded) && loaded.group_count==2 && !memcmp(loaded.groups,l->groups,sizeof(l->groups)));
    assert(loaded.canvas[pa-l->canvas].group==id && loaded.labels[label].group==id);
    const char *preview=getenv("TS_FX_GROUP_PREVIEW");if(preview){fx_canvas.group_selected=id;fx_fit_selection(1);screenshot(preview);}
    fx_clear_selection();fx_canvas.group_selected=id;key(SDLK_g,KMOD_ALT);assert(l->group_count==1 && !pa->group && !l->labels[label].group);key(SDLK_z,KMOD_CTRL);assert(l->group_count==2 && pa->group==id);
    edit_settle();fx_canvas.group_selected=id;r=fx_group_rect(fx_group(id));oldx=pa->x;
    pointer(SDL_MOUSEBUTTONDOWN,r.x+12,r.y+12,SDL_BUTTON_LEFT,1);pointer(SDL_MOUSEMOTION,r.x+82,r.y+42,0,0);key(SDLK_ESCAPE,0);assert(pa->x==oldx && !w->drag);
    assert(mx_apply(saved,0,&audio,&ui,&pages,&bank));free(saved);edit_settle();matrix_history_clear();fx_clear_selection();fx_view(1,&pages,&bank);
    puts("Rug membership, move, clamp, resize, overlap priority, properties, codec, undo and cancellation passed.");
}
