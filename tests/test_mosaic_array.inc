static void array_open_menu(SDL_Window *window,int x,int y)
{
    SDL_Event e;SDL_zero(e);e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=SDL_GetWindowID(window);
    e.button.button=SDL_BUTTON_RIGHT;e.button.x=x;e.button.y=y;assert(dispatch(&e,window));
    assert(ui.mosaic_context_open);
    click(window,ui.mosaic_context_x+20,ui.mosaic_context_y+2+7*24+10,1);
    assert(!ui.mosaic_context_open && ui.mosaic_array_open && ui_dialog_open(&ui));
    ui_sync_dialog_workspaces(&ui);assert(ui.mosaic_open);
    const char *shot=getenv("TS_MOSAIC_ARRAY_PREVIEW");
    if(shot){static TsFramebuffer fb;ts_ui_render(&fb,&ui,&instrument);assert(ts_ui_write_ppm(&fb,shot));}
}
static void array_menu(SDL_Window *window,int x,int y)
{
    array_open_menu(window,x,y);key(window,SDLK_RETURN,KMOD_NONE);
    if(ui.mosaic_array_open)key(window,SDLK_ESCAPE,KMOD_NONE);
}
static void array_text(SDL_Window *window,const char *text)
{
    key(window,SDLK_a,KMOD_CTRL);
    SDL_Event e;SDL_zero(e);e.type=SDL_TEXTINPUT;e.text.windowID=SDL_GetWindowID(window);
    snprintf(e.text.text,sizeof(e.text.text),"%s",text);assert(dispatch(&e,window));
}
static void test_mosaic_array(SDL_Window *window)
{
    TsMosaic *saved=ui.mosaic,*m=ts_mosaic_create();assert(m);ui.mosaic=audio.mosaic=m;
    ui.mosaic_open=1;ui.mosaic_scroll=0;ui.mosaic_xscroll=70;ui.mosaic_scale=20;ui.mosaic_hscale=.5;
    int volume_hidden=ui.config.mosaic_volume_hidden;ui.config.mosaic_volume_hidden=1;
    mosaic_poll(0,&ui,&instrument,&mosaic);m->grid=(TsMosaicGrid){1,0,2};
    /* The actual workflow: time selection, sidebar source click/release, then
       empty-space menu. Clamping the menu must not change the original x. */
    SDL_SetModState(KMOD_SHIFT);click(window,130,70,1);SDL_SetModState(KMOD_NONE);
    mosaic_nav_mouse(window,SDL_MOUSEMOTION,SDL_BUTTON_LEFT,130,110);release(window,130,110);
    assert(fabs(m->loop_range.start-.2)<1e-9 && fabs(m->loop_range.end-2.2)<1e-9);
    int n=instrument.selected_slot,x=20+(n%2)*49,y=85+(n/2)*34;
    click(window,x,y,1);release(window,x,y);assert(mosaic.picked_count==1);
    TsMosaicEvent expected=mosaic.picked_sources[0];int history=m->history_count;
    uint64_t before=ts_mosaic_hash(m);ui.project_modified=0;
    array_menu(window,610,330);
    assert(clipboard_card_count(m)==4 && m->history_count==history+1 && ui.project_modified);
    for(int i=0;i<4;++i) {
        TsMosaicEvent *e=&m->events[i];assert(mosaic_chosen(&ui,i));
        expected.id=e->id;expected.revision=e->revision;expected.start=.5*(i+1);expected.x=988;
        assert(!memcmp(e,&expected,sizeof(expected)));
    }
    assert(m->loop_range.start==.2 && m->loop_range.end==2.2 && !m->repeat);
    uint64_t after=ts_mosaic_hash(m);key(window,SDLK_z,KMOD_CTRL);assert(ts_mosaic_hash(m)==before && !clipboard_card_count(m));
    key(window,SDLK_y,KMOD_CTRL);assert(ts_mosaic_hash(m)==after && clipboard_card_count(m)==4);
    /* SPACE keeps every grid start and full duration, separating new cards
       from existing cards and one another. An obstacle spans the whole range. */
    TsMosaicEvent *obstacle=ts_mosaic_add(m,expected.source,0,988);assert(obstacle);obstacle->duration=10;obstacle->width=180;
    mosaic.picked_sources[0].duration=3;m->auto_space=1;before=ts_mosaic_hash(m);
    array_menu(window,610,330);assert(clipboard_card_count(m)==9);
    for(int i=5;i<9;++i) {
        const TsMosaicEvent *a=&m->events[i];assert(a->start==.5*(i-4) && a->duration==3 && a->x>=1172);
        for(int j=0;j<i;++j) {
            const TsMosaicEvent *b=&m->events[j];
            assert(a->start>=b->start+b->duration-1e-9 || b->start>=a->start+a->duration-1e-9 ||
                a->x>=b->x+b->width+TS_MOSAIC_GUTTER || b->x>=a->x+a->width+TS_MOSAIC_GUTTER);
        }
    }
    key(window,SDLK_z,KMOD_CTRL);assert(ts_mosaic_hash(m)==before && clipboard_card_count(m)==5);
    /* A selected edited source retains notes, fades, routing, and loop region.
       Shift-selected sources repeat as an ordered row at each tick. */
    m->auto_space=0;m->events[0].duration=3;m->events[0].notes[0]=69;m->events[0].pan=.4f;
    m->events[0].fade_in=.125;m->events[0].output_route.matrix_enabled=1;
    m->events[0].output_route.matrix_gain[TS_MATRIX_MAIN]=700;m->events[1].duration=.75;
    mosaic_source_filter_set(&ui,&instrument,&mosaic,TS_MOSAIC_BROWSER_EVENTS);
    click(window,20,85,1);release(window,20,85);
    SDL_SetModState(KMOD_SHIFT);click(window,69,85,1);release(window,69,85);SDL_SetModState(KMOD_NONE);
    assert(mosaic.picked_count==2);TsMosaicEvent expected_row[2]={mosaic.picked_sources[0],mosaic.picked_sources[1]};
    assert(ts_mosaic_set_loop_range(m,3,4));m->grid=(TsMosaicGrid){1,0,4};array_menu(window,610,330);
    assert(clipboard_card_count(m)==13);
    for(int i=5;i<13;++i) {
        TsMosaicEvent *e=&m->events[i],want=expected_row[(i-5)%2];
        want.id=e->id;want.revision=e->revision;want.start=3+((i-5)/2)*.25;
        want.x=988+((i-5)%2)*(expected_row[0].width+TS_MOSAIC_GUTTER);
        assert(!memcmp(e,&want,sizeof(want)));
    }
    key(window,SDLK_z,KMOD_CTRL);assert(clipboard_card_count(m)==5);
    /* Beat grid follows tempo despite zoom/speed, and excludes the range end.
       Dense visible lines can be thinned, but all 64 actual starts are used. */
    mosaic_pick_remove(&ui,&mosaic,1);int bpm=audio.sister.clock_controls.bpm;
    audio.sister.clock_controls.bpm=90;m->grid=(TsMosaicGrid){1,1,4};m->speed=2;
    assert(ts_mosaic_set_loop_range(m,1.0/3,1));array_menu(window,610,330);
    assert(clipboard_card_count(m)==9);
    for(int i=5;i<9;++i)assert(fabs(m->events[i].start-(i-3)/6.0)<1e-12);
    key(window,SDLK_z,KMOD_CTRL);audio.sister.clock_controls.bpm=bpm;
    m->grid=(TsMosaicGrid){1,0,64};assert(ts_mosaic_set_loop_range(m,0,1));array_menu(window,610,330);
    assert(clipboard_card_count(m)==69);
    for(int i=5;i<69;++i)assert(m->events[i].start==(i-5)/64.0);
    key(window,SDLK_z,KMOD_CTRL);
    /* Fit an exact count into a short selection, independently of the grid.
       Durations are retained even though all ten cards overlap in time. */
    assert(ts_mosaic_set_loop_range(m,.2,1.2));before=ts_mosaic_hash(m);
    array_open_menu(window,610,330);click(window,400,90,1);assert(ui.mosaic_array_mode==2);
    click(window,190,130,1);array_text(window,"10");
    grid_wheel(window,1,KMOD_NONE,0);assert(!strcmp(ui.mosaic_array_count,"11"));
    grid_wheel(window,-1,KMOD_NONE,0);assert(!strcmp(ui.mosaic_array_count,"10"));click(window,160,338,1);
    assert(!ui.mosaic_array_open && clipboard_card_count(m)==15);
    for(int i=5;i<15;++i)assert(fabs(m->events[i].start-(.2+(i-5)*.1))<1e-12 && m->events[i].duration==3 && m->events[i].x==988);
    assert(m->loop_range.start==.2 && m->loop_range.end==1.2);
    key(window,SDLK_z,KMOD_CTRL);assert(ts_mosaic_hash(m)==before);
    /* Count + grid works without a time selection, anchored at the original
       right-click time. The chosen grid is local to the array dialog. */
    assert(ts_mosaic_set_loop_range(m,0,0));before=ts_mosaic_hash(m);
    array_open_menu(window,610,330);assert(ui.mosaic_array_mode==0);
    click(window,190,130,1);array_text(window,"0");key(window,SDLK_RETURN,KMOD_NONE);
    assert(ui.mosaic_array_open && strstr(ui.mosaic_array_status,"COUNT MUST BE") && ts_mosaic_hash(m)==before);
    array_text(window,"6");click(window,450,130,1);array_text(window,"3");
    click(window,120,175,1);click(window,330,220,1);key(window,SDLK_RETURN,KMOD_NONE);
    assert(!ui.mosaic_array_open && clipboard_card_count(m)==11);
    for(int i=5;i<11;++i)assert(m->events[i].start==13.25+(i-5)*.75);
    assert(m->grid.division==64 && !m->loop_range.end);
    key(window,SDLK_z,KMOD_CTRL);assert(ts_mosaic_hash(m)==before);
    /* Every rejection preserves the entire scene, selection and undo cursor. */
    assert(ts_mosaic_set_loop_range(m,0,60));before=ts_mosaic_hash(m);history=m->history_cursor;
    mosaic_selection_repair(&ui);uint64_t primary=ui.mosaic_selected;array_menu(window,610,330);
    assert(strstr(ui.status,"3840 CARDS NEEDED") && ts_mosaic_hash(m)==before && m->history_cursor==history && ui.mosaic_selected==primary);
    assert(ts_mosaic_set_loop_range(m,.001,.002));before=ts_mosaic_hash(m);array_menu(window,610,330);
    assert(strstr(ui.status,"NO GRID STARTS") && ts_mosaic_hash(m)==before);
    m->grid.enabled=0;before=ts_mosaic_hash(m);array_open_menu(window,610,330);
    click(window,120,175,1);key(window,SDLK_ESCAPE,KMOD_NONE);assert(ts_mosaic_hash(m)==before && !m->grid.enabled);m->grid.enabled=1;
    assert(ts_mosaic_set_loop_range(m,0,0));array_open_menu(window,610,330);click(window,260,90,1);
    key(window,SDLK_RETURN,KMOD_NONE);assert(ui.mosaic_array_open && strstr(ui.status,"SELECT A TIME RANGE"));key(window,SDLK_ESCAPE,KMOD_NONE);
    mosaic_picks_clear(&ui,&mosaic);ui.mosaic_source_selected=-1;
    assert(ts_mosaic_set_loop_range(m,0,1));array_menu(window,610,330);assert(strstr(ui.status,"SELECT A SOURCE"));
    /* Without group picks, a highlighted sidebar entry still works. */
    mosaic_source_select_event(&ui,&m->events[0]);assert(ui.mosaic_source_selected>=0);m->grid=(TsMosaicGrid){1,0,1};array_menu(window,610,330);
    assert(clipboard_card_count(m)==6 && !mosaic.drag && m->events[5].source==m->events[0].source);
    mosaic_picks_clear(&ui,&mosaic);mosaic_drag_end(&ui,&mosaic);
    ui.config.mosaic_volume_hidden=volume_hidden;
    ui.mosaic=audio.mosaic=saved;mosaic_poll(0,&ui,&instrument,&mosaic);ts_mosaic_free(m);
    puts("Mosaic source arrays: menu workflow, full source settings, grid/tempo, spacing, atomic limits and undo passed.");
}
