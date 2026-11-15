static int heading_index(uint64_t canvas)
{
    TsMatrixLayout *l=matrix_window.layout;
    for(int i=0;i<l->heading_count;++i)if(l->headings[i].canvas==canvas)return TS_FX_LABEL_LIMIT+i;
    return -1;
}
static void movement_heading_objects(void)
{
    MatrixWindow *w=&matrix_window;TsMatrixLayout *l=w->layout;TsMosaicGallery *g=pages.gallery;
    char error[256];assert(g && g->count==1);matrix_history_clear();
    static const char *names[]={"KETER","CHOKHMAH","BINAH","CHESED","GEVURAH","TIFERET","NETZACH","HOD","YESOD","MALKHUT"};
    float samples[16]={.1f};TsSample sample={.data=samples,.frames=16,.channels=1,.sample_rate=48000};
    for(int c=0;c<10;++c) {
        if(c)assert(ts_gallery_add(g,error,sizeof(error)));
        snprintf(g->canvas[c].name,TS_GALLERY_NAME,"%s",names[c]);
        if(c) {
            TsMosaic *m=g->canvas[c].scene;TsMosaicSource *s=ts_mosaic_source(m,&sample,error,sizeof(error));assert(s);
            assert(ts_mosaic_add(m,s,0,0));
        }
    }
    /* Simulate an older project with all 64 existing text objects in use. */
    ts_matrix_layout_default(l);l->label_count=TS_FX_LABEL_LIMIT;
    for(int i=0;i<l->label_count;++i)l->labels[i]=(TsFxCanvasLabel){.x=-10000,.y=-10000,.scale=1,.text="Keep existing text"};
    mx_bind_mosaic(&ui,&pages);fx_graph_cache.valid=0;
    fx_canvas.movement=g->canvas[4].id;fx_canvas.active_view=0;fx_view(1,&pages,&bank);
    for(int c=0;c<10;++c) {
        int i=heading_index(g->canvas[c].id);assert(i>=0);
        assert(!strcmp(fx_label_at(i)->text,names[c]));
        assert(fx_label_visible(i)==(c==4));
    }
    int count=l->heading_count;assert(l->label_count==TS_FX_LABEL_LIMIT);
    fx_build(&pages,&bank);assert(l->heading_count==count);
    /* Double-click edits the converted object; Gallery naming stays independent. */
    fx_canvas.movement=0;fx_build(&pages,&bank);w->effect_open=w->compressor_open=0;
    l->canvas_x=l->canvas_y=0;l->canvas_zoom=1;l->canvas_snap=0;l->canvas_view=1;fx_canvas.fit=0;
    uint64_t id=g->canvas[0].id;int index=heading_index(id);TsFxCanvasLabel *p=fx_label_at(index);
    p->x=100;p->y=140;SDL_Rect r=fx_text_object_rect(index);click(r.x+3,r.y+3,2);
    assert(fx_canvas.label_editing && fx_canvas.label_target==index);
    SDL_Event text={0};text.type=SDL_TEXTINPUT;text.text.windowID=w->id;
    snprintf(text.text.text,sizeof(text.text.text),"KETER / edited");assert(event(&text));key(SDLK_RETURN,0);
    assert(!strcmp(p->text,"KETER / edited") && !strcmp(g->canvas[0].name,"KETER"));
    key(SDLK_z,KMOD_CTRL);assert(!strcmp(p->text,"KETER"));key(SDLK_y,KMOD_CTRL);assert(!strcmp(p->text,"KETER / edited"));
    fx_canvas.label_selected=index;TsFxCanvasLabel before=*p;
    pointer(SDL_MOUSEMOTION,200,200,0,0);key(SDLK_g,0);pointer(SDL_MOUSEMOTION,240,230,0,0);key(SDLK_RETURN,0);
    assert(p->x==before.x+40 && p->y==before.y+30);
    fx_canvas.label_selected=index;key(SDLK_s,0);pointer(SDL_MOUSEMOTION,290,230,0,0);key(SDLK_RETURN,0);assert(p->scale>1);
    TsFxCanvasLabel edited=*p;fx_build(&pages,&bank);assert(!memcmp(p,&edited,sizeof(edited)));
    if(getenv("TS_FX_PREVIEW")){char path[1024];snprintf(path,sizeof(path),"%s/movement-heading.bmp",getenv("TS_FX_PREVIEW"));screenshot(path);}
    /* Deletion, Undo and Redo retain the conversion marker. */
    fx_canvas.label_selected=index;key(SDLK_DELETE,0);assert(!p->text[0] && !fx_label_visible(index));
    key(SDLK_z,KMOD_CTRL);assert(!memcmp(p,&edited,sizeof(edited)));
    key(SDLK_y,KMOD_CTRL);assert(!p->text[0]);view_codec();
    TsSisterProjectState *saved=malloc(sizeof(*saved)),*loaded=malloc(sizeof(*loaded));assert(saved && loaded);
    const char *path="fx-heading-project-test.ini";int present=0;
    ts_sister_project_state_capture(saved,&audio.sister,1,"headings");
    assert(ts_sister_project_state_save_file(saved,path,error,sizeof(error)));
    assert(ts_sister_project_state_load_file(loaded,path,48000,&present,error,sizeof(error)) && present);
    assert(!memcmp(&loaded->matrix_layout,l,sizeof(*l)));
    *l=loaded->matrix_layout;free(saved);free(loaded);remove(path);fx_context_reset();fx_graph_cache.valid=0;fx_build(&pages,&bank);
    assert(l->heading_count==count && !fx_label_at(heading_index(id))->text[0]);
    /* Reordering/renaming Gallery never resets an edited or deleted heading. */
    TsMosaicCanvas swap=g->canvas[0];g->canvas[0]=g->canvas[1];g->canvas[1]=swap;g->active=1;
    snprintf(g->canvas[1].name,TS_GALLERY_NAME,"Renamed Gallery movement");
    mx_bind_mosaic(&ui,&pages);fx_build(&pages,&bank);
    assert(l->heading_count==count && !fx_label_at(heading_index(id))->text[0]);
    assert(!strcmp(fx_label_at(heading_index(g->canvas[0].id))->text,"CHOKHMAH"));
    for(int i=0;i<l->label_count;++i)assert(!strcmp(l->labels[i].text,"Keep existing text"));
    /* New scenes get one object; removed identities do not exhaust the table. */
    assert(ts_gallery_add(g,error,sizeof(error)));TsMosaic *m=g->canvas[10].scene;
    TsMosaicSource *s=ts_mosaic_source(m,&sample,error,sizeof(error));assert(s && ts_mosaic_add(m,s,0,0));
    uint64_t added=g->canvas[10].id;mx_rows(&pages,&bank);fx_build(&pages,&bank);assert(heading_index(added)>=0);
    assert(ts_gallery_remove(g,10,error,sizeof(error)));mx_rows(&pages,&bank);fx_build(&pages,&bank);
    assert(heading_index(added)<0 && l->heading_count==count);
    matrix_history_clear();
}
