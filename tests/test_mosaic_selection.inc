static void mosaic_select_click(SDL_Window *window,int x,int y,SDL_Keymod mod)
{SDL_SetModState(mod);click(window,x,y,1);release(window,x,y);SDL_SetModState(KMOD_NONE);}
static void mosaic_check_caret(int x,int y,int w)
{
    static TsFramebuffer on,off;
    ui.text_cursor_visible=1;ts_ui_render(&on,&ui,&instrument);
    ui.text_cursor_visible=0;ts_ui_render(&off,&ui,&instrument);
    int changed=0;
    for(int yy=y;yy<y+24;++yy)for(int xx=x;xx<x+w;++xx)
        changed+=on.pixels[yy*640+xx]!=off.pixels[yy*640+xx];
    assert(changed>0 && changed<20);ui.text_cursor_visible=1;
}
static void test_mosaic_selection(SDL_Window *window)
{
    TsMosaic *saved=ui.mosaic,*scene=ts_mosaic_create();assert(scene);
    ui.mosaic=audio.mosaic=scene;ui.mosaic_open=1;ui.mosaic_follow=0;
    ui.mosaic_scroll=ui.mosaic_xscroll=0;ui.mosaic_scale=24;ui.mosaic_hscale=1;
    mosaic_poll(0,&ui,&instrument,&mosaic);
    char error[160];TsMosaicSource *source=ts_mosaic_source(scene,&instrument.current,error,sizeof(error));assert(source);
    TsMosaicEvent *a=ts_mosaic_add(scene,source,2,20);a->duration=4;
    TsMosaicEvent *b=ts_mosaic_add(scene,source,4,130);b->duration=2;
    TsMosaicEvent *c=ts_mosaic_add(scene,source,1,240);c->duration=3;
    scene->time=10;scene->playing=1;
    mosaic_select_click(window,290,186,KMOD_NONE);
    mosaic_select_click(window,180,138,KMOD_SHIFT);
    mosaic_select_click(window,410,114,KMOD_SHIFT);
    assert(ui.mosaic_selected==b->id && mosaic_chosen(&ui,0) && mosaic_chosen(&ui,1) && mosaic_chosen(&ui,2));
    int history=scene->history_count;
    /* Normal dragging from a secondary card never changes the primary. */
    click(window,180,138,1);motion(window,190,162);release(window,190,162);
    assert(ui.mosaic_selected==b->id && a->start==3 && b->start==5 && c->start==2);
    key(window,SDLK_z,KMOD_CTRL);assert(a->start==2 && b->start==4 && c->start==1);
    /* Shift-click toggles a member; removing the primary promotes the oldest. */
    mosaic_select_click(window,290,186,KMOD_SHIFT);assert(ui.mosaic_selected==a->id && !mosaic_chosen(&ui,1));
    mosaic_select_click(window,290,186,KMOD_SHIFT);assert(ui.mosaic_selected==a->id && mosaic_chosen(&ui,1));
    mosaic_select_only(&ui,b->id);mosaic_select_add(&ui,a->id);
    /* Shift-box adds members while preserving the existing primary. */
    SDL_SetModState(KMOD_SHIFT);click(window,388,80,1);motion(window,480,165);release(window,480,165);SDL_SetModState(KMOD_NONE);
    assert(ui.mosaic_selected==b->id && mosaic_chosen(&ui,2));
    key(window,SDLK_RIGHT,KMOD_NONE);assert(a->x==21 && b->x==131 && c->x==241);
    key(window,SDLK_LEFT,KMOD_SHIFT);assert(a->x==11 && b->x==121 && c->x==231);
    assert(a->start==2 && b->start==4 && c->start==1);
    SDL_Event repeat;SDL_zero(repeat);repeat.type=SDL_KEYDOWN;repeat.key.windowID=SDL_GetWindowID(window);
    repeat.key.keysym.sym=SDLK_LEFT;repeat.key.repeat=1;dispatch(&repeat,window);assert(a->x==10);
    key(window,SDLK_LEFT,KMOD_SHIFT);assert(a->x==0 && b->x==110 && c->x==220);
    history=scene->history_count;key(window,SDLK_LEFT,KMOD_NONE);assert(scene->history_count==history);
    a->x=20;b->x=130;c->x=240;
    /* All four alignments preserve lengths, primary, playhead and transport. */
    key(window,SDLK_UP,KMOD_ALT);assert(a->start==4 && b->start==4 && c->start==4);
    key(window,SDLK_DOWN,KMOD_ALT);assert(a->start==2 && b->start==4 && c->start==3);
    key(window,SDLK_LEFT,KMOD_ALT);assert(a->start==10 && b->start==10 && c->start==10);
    key(window,SDLK_RIGHT,KMOD_ALT);assert(a->start==6 && b->start==8 && c->start==7);
    assert(a->duration==4 && b->duration==2 && c->duration==3 && ui.mosaic_selected==b->id);
    assert(scene->time==10 && scene->playing);
    uint64_t aligned=ts_mosaic_hash(scene);scene->time=.5;history=scene->history_count;
    key(window,SDLK_RIGHT,KMOD_ALT);assert(ts_mosaic_hash(scene)==aligned && scene->history_count==history);
    scene->time=10;key(window,SDLK_z,KMOD_CTRL);assert(a->start==10 && b->start==10 && c->start==10);
    key(window,SDLK_y,KMOD_CTRL);assert(ts_mosaic_hash(scene)==aligned);
    /* Single-card primary alignment is a modal notice with no undo entry. */
    mosaic_select_only(&ui,b->id);history=scene->history_count;
    key(window,SDLK_UP,KMOD_ALT);assert(ui.mosaic_align_open && ts_mosaic_hash(scene)==aligned);
    ui_sync_dialog_workspaces(&ui);assert(ui.mosaic_open && ui_dialog_open(&ui) && !main_waveform_detail_allowed(&ui));
    key(window,SDLK_RIGHT,KMOD_NONE);assert(b->x==130 && ui.mosaic_align_open);
    key(window,SDLK_RETURN,KMOD_NONE);assert(!ui.mosaic_align_open && scene->history_count==history);
    key(window,SDLK_DOWN,KMOD_ALT);assert(ui.mosaic_align_open);click(window,280,230,1);
    assert(!ui.mosaic_align_open && scene->history_count==history);
    key(window,SDLK_LEFT,KMOD_ALT);assert(b->start==10);key(window,SDLK_RIGHT,KMOD_ALT);assert(b->start==8);
    mosaic_select_only(&ui,b->id);mosaic_select_add(&ui,a->id);mosaic_select_add(&ui,c->id);
    a->start=2;b->start=4;c->start=1;
    /* The primary's copy remains primary even when dragging another member. */
    SDL_SetModState(KMOD_SHIFT);click(window,180,138,1);motion(window,190,162);release(window,190,162);SDL_SetModState(KMOD_NONE);
    assert(mosaic_event_count(scene)==6);
    TsMosaicEvent *primary=ts_mosaic_find(scene,ui.mosaic_selected);
    assert(primary && primary->duration==b->duration && primary->start==5);
    key(window,SDLK_z,KMOD_CTRL);assert(mosaic_event_count(scene)==3);
    mosaic_select_only(&ui,b->id);mosaic_select_add(&ui,a->id);mosaic_select_add(&ui,c->id);
    key(window,SDLK_r,KMOD_CTRL);assert(ui.mosaic_scatter_open && mosaic.scatter_pool[0].id==b->id);
    click(window,140,120,1);mosaic_check_caret(130,115,160);key(window,SDLK_RETURN,KMOD_NONE);
    click(window,330,120,1);mosaic_check_caret(310,115,180);key(window,SDLK_RETURN,KMOD_NONE);
    click(window,140,160,1);mosaic_check_caret(130,151,110);key(window,SDLK_ESCAPE,KMOD_NONE);
    click(window,130,150,2);assert(ui.mosaic_loop_open);
    mosaic_check_caret(140,173,170);key(window,SDLK_TAB,KMOD_NONE);mosaic_check_caret(330,173,170);
    key(window,SDLK_ESCAPE,KMOD_NONE);
    /* Distinct bank cards with identical audio must stay distinct and ordered. */
    TsInstrument *original=malloc(sizeof(*original));assert(original);*original=instrument;ts_instrument_init(&instrument);
    assert(ts_instrument_generate(&instrument,TS_GENERATOR_METALLIC,777,error,sizeof(error)));
    assert(ts_instrument_copy_selected(&instrument,1,error,sizeof(error)));
    assert(ts_instrument_copy_selected(&instrument,2,error,sizeof(error)));
    snprintf(instrument.bank[0].sample.name,sizeof(instrument.bank[0].sample.name),"SOURCE ONE");
    snprintf(instrument.bank[1].sample.name,sizeof(instrument.bank[1].sample.name),"SOURCE TWO");
    snprintf(instrument.current.name,sizeof(instrument.current.name),"SOURCE THREE");
    ui.mosaic_source_page=0;mosaic_sources_refresh(&ui,&instrument,&mosaic);
    mosaic_select_click(window,20,119,KMOD_NONE);
    mosaic_select_click(window,20,85,KMOD_SHIFT);mosaic_select_click(window,69,85,KMOD_SHIFT);
    assert(mosaic.picked_count==3 && mosaic.picked_keys[0].slot==2 && mosaic.picked_keys[1].slot==0 && mosaic.picked_keys[2].slot==1);
    assert(mosaic.picked_sources[0].source==mosaic.picked_sources[1].source);
    TsMosaicSource *picked=mosaic.picked_sources[0].source;assert(picked->pins==3);
    click(window,20,85,1);motion(window,171,114);
    assert(ui.mosaic_ghost_count==3 && ui.mosaic_ghosts[0].x==20 && ui.mosaic_ghosts[1].x==114 && ui.mosaic_ghosts[2].x==208);
    release(window,171,114);assert(mosaic_event_count(scene)==6 && !mosaic.picked_count && !picked->pins);
    primary=ts_mosaic_find(scene,ui.mosaic_selected);assert(primary && !strcmp(primary->name,"SOURCE THREE"));
    assert(primary->start==2 && primary->x==20);
    TsMosaicEvent selected[TS_MOSAIC_EVENTS];assert(mosaic_selected_events(&ui,selected)==3);
    assert(!strcmp(selected[1].name,"SOURCE ONE") && !strcmp(selected[2].name,"SOURCE TWO"));
    for(int i=0;i<3;++i)assert(selected[i].start==2 && selected[i].x==20+i*94);
    key(window,SDLK_r,KMOD_CTRL);assert(ui.mosaic_scatter_open && !mosaic.scatter_from_sources && mosaic.scatter_pool[0].id==primary->id);
    key(window,SDLK_ESCAPE,KMOD_NONE);key(window,SDLK_z,KMOD_CTRL);assert(mosaic_event_count(scene)==3);
    key(window,SDLK_y,KMOD_CTRL);assert(mosaic_event_count(scene)==6);
    /* No partial insertion, selection loss or extra undo when the row won't fit. */
    while(mosaic_event_count(scene)<TS_MOSAIC_EVENTS-2)assert(ts_mosaic_add(scene,source,30,0));
    ui.mosaic_source_page=0;mosaic_sources_refresh(&ui,&instrument,&mosaic);
    mosaic_select_click(window,20,119,KMOD_NONE);mosaic_select_click(window,20,85,KMOD_SHIFT);mosaic_select_click(window,69,85,KMOD_SHIFT);
    history=scene->history_count;uint64_t before=ts_mosaic_hash(scene),parent=ui.mosaic_selected;
    click(window,20,85,1);motion(window,171,114);release(window,171,114);
    assert(ts_mosaic_hash(scene)==before && scene->history_count==history && ui.mosaic_selected==parent && mosaic.picked_count==3);
    mosaic_picks_clear(&ui,&mosaic);assert(!picked->pins);
    ts_instrument_free(&instrument);instrument=*original;free(original);
    /* Render the primary and secondary borders at logical and native sizes. */
    for(int i=3;i<TS_MOSAIC_EVENTS;++i)if(scene->events[i].id)ts_mosaic_delete(scene,scene->events[i].id);
    mosaic_select_only(&ui,b->id);mosaic_select_add(&ui,a->id);mosaic_select_add(&ui,c->id);
    scene->time=3;mosaic_poll(0,&ui,&instrument,&mosaic);
    snprintf(ui.status,sizeof(ui.status),"PRIMARY RED / SHIFT CLICK SELECT / ALT ARROWS ALIGN");
    static TsFramebuffer fb;ts_ui_render(&fb,&ui,&instrument);
    assert(fb.pixels[114*640+171]!=fb.pixels[162*640+281]);
    if(getenv("TS_MOSAIC_SELECTION_SCREENSHOT"))assert(ts_ui_write_ppm(&fb,getenv("TS_MOSAIC_SELECTION_SCREENSHOT")));
    mosaic_native_check(&instrument,NULL);
    mosaic_drag_end(&ui,&mosaic);ui.mosaic=audio.mosaic=saved;ts_mosaic_free(scene);
    ui.mosaic_scroll=ui.mosaic_xscroll=0;ui.mosaic_scale=24;ui.mosaic_hscale=1;ui.mosaic_follow=0;
    mosaic_poll(0,&ui,&instrument,&mosaic);
}
