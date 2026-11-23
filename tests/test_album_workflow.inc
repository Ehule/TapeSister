/* Exercises the same controllers and save worker as the native application. */
static void album_key(SDL_Window *window,SDL_Keycode key,int mod)
{
    SDL_Event e;SDL_zero(e);e.type=SDL_KEYDOWN;e.key.windowID=matrix_window.id;
    e.key.keysym.sym=key;e.key.keysym.mod=mod;
    assert(matrix_event(&e,window,0,&audio,&ui,&pages,&bank));
}
static void album_layout_test(SDL_Window *window)
{
    ui.mosaic_gallery_open=ui.browser.mode=0;ui.mosaic_open=1;ui.tracker_open=0;
    main_layout_ui=&ui;main_layout_window=window;
    SDL_Surface *surface=SDL_CreateRGBSurfaceWithFormat(0,640,400,32,SDL_PIXELFORMAT_ARGB8888);assert(surface);
    SDL_Renderer *renderer=SDL_CreateSoftwareRenderer(surface);assert(renderer);main_layout_renderer=renderer;
    SDL_Texture *texture=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,640,400);assert(texture);
    static uint32_t pixels[640*400];const uint32_t colors[]={0xffe08080,0xff80e080,0xff8080e0,0xffe0e080,0xff80e0e0};
    TsMosaicLayout normal=ts_mosaic_layout_make(100,100);
    for(int i=0;i<TS_MOSAIC_LAYOUT_PARTS;++i) {
        TsUiRect s=normal.source[i];for(int y=s.y;y<s.y+s.h;++y)for(int x=s.x;x<s.x+s.w;++x)pixels[y*640+x]=colors[i];
    }
    assert(!SDL_UpdateTexture(texture,NULL,pixels,640*sizeof(*pixels)));
    double zoom=ui.mosaic_scale,hzoom=ui.mosaic_hscale;
    for(int percent=50;percent<=100;percent+=10) {
        ui.config.mosaic_toolbar_percent=percent;ui.config.mosaic_sources_percent=150-percent;
        TsMosaicLayout layout=mosaic_layout_current();main_surface_copy(renderer,texture);SDL_RenderPresent(renderer);
        for(int i=0;i<TS_MOSAIC_LAYOUT_PARTS;++i) {
            TsUiRect d=layout.destination[i],s=layout.source[i];int x=d.x+d.w/2,y=d.y+d.h/2,lx,ly;
            uint32_t pixel=*(uint32_t *)((unsigned char *)surface->pixels+y*surface->pitch+x*4);
            assert(pixel==colors[i]);logical_mouse(window,x,y,&lx,&ly);
            assert(abs(lx-(s.x+s.w/2))<=1 && abs(ly-(s.y+s.h/2))<=1);
        }
    }
    mosaic_layout_adjust(&ui,5);assert(ui.config.mosaic_toolbar_percent==100 && ui.config.mosaic_sources_percent==100);
    mosaic_layout_adjust(&ui,1);mosaic_layout_adjust(&ui,3);
    assert(ui.config.mosaic_toolbar_percent==90 && ui.config.mosaic_sources_percent==90);
    assert(ui.mosaic_scale==zoom && ui.mosaic_hscale==hzoom);
    assert(ts_config_save(&ui.config,"album-layout.ini",error,sizeof(error)));
    TsConfig loaded;ts_config_init(&loaded);assert(ts_config_load(&loaded,"album-layout.ini",error,sizeof(error)));
    assert(loaded.mosaic_toolbar_percent==90 && loaded.mosaic_sources_percent==90);
    /* Capture the real framebuffer through the resized native presentation. */
    for(int compact=0;compact<2;++compact) {
        ui.config.mosaic_toolbar_percent=compact?70:100;ui.config.mosaic_sources_percent=compact?80:100;
        ts_ui_render(&frame,&ui,&bank);assert(!SDL_UpdateTexture(texture,NULL,frame.pixels,640*4));
        main_surface_copy(renderer,texture);SDL_RenderPresent(renderer);
        assert(!SDL_SaveBMP(surface,compact?"album-compact.bmp":"album-normal.bmp"));
    }
    mosaic_layout_adjust(&ui,5);main_layout_renderer=NULL;main_layout_window=NULL;main_layout_ui=NULL;
    SDL_DestroyTexture(texture);SDL_DestroyRenderer(renderer);SDL_FreeSurface(surface);
}
static void test_album_workflow(SDL_Window *window)
{
    assert(!strcmp(ts_audio_activity_name(0,0,0,0,0),"NO OUTPUT"));
    assert(!strcmp(ts_audio_activity_name(1,1,1,1,1),"IDLE"));
    assert(!strcmp(ts_audio_activity_name(1,0,1,0,0),"PLAYING"));
    assert(!strcmp(ts_audio_activity_name(1,0,0,1,0),"VOICES"));
    assert(!strcmp(ts_audio_activity_name(1,0,0,0,1),"LIVE INPUT"));
    assert(!strcmp(ts_audio_activity_name(1,0,0,0,0),"FX RUNNING"));
    movement_navigator_open(&ui,&pages);assert(pages.gallery && ui.mosaic_gallery_open);
    TsMosaicGallery *g=pages.gallery;audio.gallery=ui.mosaic_gallery=g;
    assert(ts_gallery_rename(g,0,"Old Movement",error,sizeof(error)));
    assert(ts_gallery_add(g,error,sizeof(error)));assert(ts_gallery_rename(g,1,"Other Movement",error,sizeof(error)));
    TsMosaicEvent *card=&ui.mosaic->events[0];strcpy(card->name,"Old Movement - Glass Texture Layer");
    uint64_t card_id=card->id,canvas_id=g->canvas[0].id;double start=card->start;
    TsSourceRoute route=card->output_route;TsMatrixEffects effects=audio.sister.matrix_effects;
    TsMosaicEvent *custom=ts_mosaic_add(ui.mosaic,card->source,2,1);assert(custom);strcpy(custom->name,"Custom Layer");
    TsMatrixLayout *layout=&audio.sister.matrix_layout;
    layout->canvas_count=1;layout->canvas[0]=(TsFxCanvasPosition){.kind=3,.id=card_id,.canvas=canvas_id};
    strcpy(layout->canvas[0].name,card->name);
    layout->heading_count=1;layout->headings[0].canvas=canvas_id;strcpy(layout->headings[0].label.text,"Old Movement - E minor");
    assert(matrix_show(0,&audio,&ui,&pages,&bank,-1,-1));fx_view(1,&pages,&bank);
    assert(mosaic_gallery_rename_movement(0,&audio,&ui,&pages,&bank,0,"Before the Distance"));
    assert(!strcmp(card->name,"Before the Distance - Glass Texture Layer") && !strcmp(custom->name,"Custom Layer"));
    assert(!strcmp(g->canvas[0].name,"Before the Distance") && !strcmp(g->canvas[1].name,"Other Movement"));
    assert(!strcmp(layout->headings[0].label.text,"Before the Distance - E minor"));
    assert(!strcmp(layout->canvas[0].name,card->name));
    assert(card->id==card_id && card->start==start && !memcmp(&card->output_route,&route,sizeof(route)));
    mx_undo(0,&audio,&ui,&pages,&bank,0);
    assert(!strcmp(card->name,"Old Movement - Glass Texture Layer") && !strcmp(g->canvas[0].name,"Old Movement"));
    assert(!strcmp(layout->headings[0].label.text,"Old Movement - E minor"));
    mx_undo(0,&audio,&ui,&pages,&bank,1);
    assert(!strcmp(card->name,"Before the Distance - Glass Texture Layer"));
    assert(!memcmp(&effects,&audio.sister.matrix_effects,sizeof(effects))); /* Live DSP ownership/tails survived. */
    char long_name[48];memset(long_name,'X',47);long_name[47]=0;
    assert(!mosaic_gallery_rename_movement(0,&audio,&ui,&pages,&bank,0,long_name));
    assert(!strcmp(g->canvas[0].name,"Before the Distance") && !strcmp(card->name,"Before the Distance - Glass Texture Layer"));
    assert(mosaic_gallery_rename_movement(0,&audio,&ui,&pages,&bank,0,"Before the Distance Across the Empty Room"));
    assert(strlen(card->name)>47 && !strcmp(layout->canvas[0].name,card->name));
    ui.mosaic_selected=card->id;card->duration=30;card->looping=1;ui.mosaic_scale=12;
    mosaic_sources_refresh(&ui,&bank,&mosaic);album_layout_test(window);
    /* FX focusing is view-only. */
    int active=g->active,playing=ui.mosaic->playing;double time=ui.mosaic->time;
    movement_focus_fx(0,&audio,&ui,&pages,&bank,1);
    assert(fx_canvas.movement==g->canvas[1].id && g->active==active && ui.mosaic->playing==playing && ui.mosaic->time==time);
    ui.mosaic_gallery_pinned=1;ui.mosaic_gallery_open=1;ui.mosaic_gallery_selected=0;
    SDL_Event enter;SDL_zero(enter);enter.type=SDL_KEYDOWN;enter.key.keysym.sym=SDLK_RETURN;
    assert(mosaic_gallery_event(&enter,0,&audio,&ui,&bank,&mosaic,0,0,KMOD_NONE));assert(ui.mosaic_gallery_open);
    ui.mosaic_gallery_open=0;ui.project_modified=1;
    album_key(window,SDLK_s,KMOD_CTRL|KMOD_SHIFT);
    assert(ui.browser.mode==TS_BROWSER_SAVE_RECIPE && !strcmp(ui.browser.filename,"old-project.tsr"));browser_cancel(&ui);
    /* Real browser -> pending operation -> worker export. It must contain edits
       and must not mark the current document saved or change its path. */
    uint64_t saved_hash=ui.saved_state_hash,saved_ui_hash=ui.saved_ui_hash;
    char active_path[sizeof(ui.project_path)];strcpy(active_path,ui.project_path);
    project_menu_action(&ui,3);assert(ui.browser.mode==TS_BROWSER_EXPORT_PROJECT_ZIP);
    char cwd[TS_BROWSER_PATH_MAX];assert(getcwd(cwd,sizeof(cwd)));strcpy(ui.browser.directory,cwd);
    strcpy(ui.browser.filename,"Unmade-Light.zip");ui.browser.filename_focus=1;
    browser_action(0,&audio,&ui,&bank,&selection,&pages,&record,0,&file,0);assert(file.active && file.mode==TS_BROWSER_EXPORT_PROJECT_ZIP);
    file.presented=1;worker_poll(0);assert(project_save_worker);
    ui_sync_dialog_workspaces(&ui);assert(ui.mosaic_open);worker_finish(0);
    fprintf(stderr,"Album export: %s\n",ui.status);assert(strstr(ui.status,"EXPORTED ZIP"));
    assert(!strcmp(ui.project_path,active_path) && ui.project_modified);
    assert(ui.saved_state_hash==saved_hash && ui.saved_ui_hash==saved_ui_hash);
    FILE *zip=fopen("Unmade-Light.zip","rb");assert(zip);unsigned char signature[4];assert(fread(signature,1,4,zip)==4 && signature[0]=='P' && signature[1]=='K');fclose(zip);
    /* Cancellation leaves an existing destination byte-for-byte intact. */
    FILE *sentinel=fopen("cancel.zip","wb");assert(sentinel);fputs("KEEP OLD ZIP",sentinel);fclose(sentinel);
    project_menu_action(&ui,3);strcpy(ui.browser.directory,cwd);strcpy(ui.browser.filename,"cancel.zip");ui.browser.filename_focus=1;ui.browser.overwrite_armed=1;
    browser_action(0,&audio,&ui,&bank,&selection,&pages,&record,0,&file,0);assert(file.active);file.presented=1;
    SDL_AtomicSet(&save_test_gate,1);worker_poll(0);assert(project_save_worker);project_save_cancel(0);SDL_AtomicSet(&save_test_gate,0);worker_finish(0);
    sentinel=fopen("cancel.zip","rb");assert(sentinel);char text[32]={0};assert(fread(text,1,31,sentinel)==12);fclose(sentinel);assert(!strcmp(text,"KEEP OLD ZIP"));
    browser_cancel(&ui);matrix_hide();
    puts("Album workflow: rename/undo, navigation, layout paint/input, CPU states, ZIP and cancellation passed.");
}
