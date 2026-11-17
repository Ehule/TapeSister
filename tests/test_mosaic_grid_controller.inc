static void grid_wheel(SDL_Window *window,int delta,SDL_Keymod mod,int flipped)
{
    SDL_Event e;SDL_zero(e);e.type=SDL_MOUSEWHEEL;e.wheel.windowID=SDL_GetWindowID(window);
    e.wheel.y=delta;e.wheel.direction=flipped?SDL_MOUSEWHEEL_FLIPPED:SDL_MOUSEWHEEL_NORMAL;
#if SDL_VERSION_ATLEAST(2,26,0)
    e.wheel.mouseX=220;e.wheel.mouseY=140;
#else
    SDL_WarpMouseInWindow(window,220,140);
#endif
    SDL_SetModState(mod);assert(dispatch(&e,window));SDL_SetModState(KMOD_NONE);
}
static void test_mosaic_grid_pitch(SDL_Window *window)
{
    TsMosaic *saved=ui.mosaic,*m=ts_mosaic_create();assert(m);ui.mosaic=audio.mosaic=m;
    ui.mosaic_open=1;ui.mosaic_scroll=ui.mosaic_xscroll=0;ui.mosaic_scale=100;ui.mosaic_hscale=1;
    char error[160];TsMosaicSource *source=ts_mosaic_source(m,&instrument.current,error,sizeof(error));assert(source);
    TsMosaicEvent *a=ts_mosaic_add(m,source,1.03,20),*b=ts_mosaic_add(m,source,1.4,140);
    a->duration=b->duration=2;b->note_count=3;b->notes[0]=64;b->notes[1]=67;b->notes[2]=71;
    mosaic_poll(0,&ui,&instrument,&mosaic);mosaic_select_only(&ui,a->id);mosaic_select_add(&ui,b->id);
    int history=m->history_count;double scale=ui.mosaic_scale;
    grid_wheel(window,1,KMOD_CTRL|KMOD_SHIFT,0);
    assert(a->notes[0]==61 && b->notes[0]==65 && b->notes[1]==68 && b->notes[2]==72);
    assert(m->history_count==history+1 && ui.mosaic_scale==scale && a->duration==2 && a->start==1.03);
    key(window,SDLK_z,KMOD_CTRL);assert(a->notes[0]==60 && b->notes[2]==71);
    key(window,SDLK_y,KMOD_CTRL);assert(a->notes[0]==61 && b->notes[2]==72);
    grid_wheel(window,1,KMOD_CTRL|KMOD_SHIFT,1);assert(a->notes[0]==60 && b->notes[2]==71);
    grid_wheel(window,0,KMOD_CTRL|KMOD_SHIFT,0);assert(a->notes[0]==60);
    a->notes[0]=120;b->notes[0]=123;b->notes[1]=125;b->notes[2]=127;
    grid_wheel(window,1,KMOD_CTRL|KMOD_SHIFT,0);assert(a->notes[0]==120 && b->notes[2]==127);
    grid_wheel(window,-2,KMOD_CTRL|KMOD_SHIFT,0);assert(a->notes[0]==118 && b->notes[2]==125);
    a->notes[0]=0;grid_wheel(window,-1,KMOD_CTRL|KMOD_SHIFT,0);assert(!a->notes[0] && b->notes[2]==125);
    mosaic_select_only(&ui,0);grid_wheel(window,1,KMOD_CTRL|KMOD_SHIFT,0);assert(ui.mosaic_scale==scale);
    a->notes[0]=60;b->notes[0]=64;b->notes[1]=67;b->notes[2]=71;
    mosaic_select_only(&ui,a->id);mosaic_select_add(&ui,b->id);
    click(window,TS_MOSAIC_GRID_X+10,TS_MOSAIC_GRID_Y+4,1);assert(m->grid.enabled);
    SDL_Event e;SDL_zero(e);e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=SDL_GetWindowID(window);
    e.button.button=SDL_BUTTON_RIGHT;e.button.x=TS_MOSAIC_GRID_X+10;e.button.y=TS_MOSAIC_GRID_Y+4;
    assert(dispatch(&e,window));assert(ui.mosaic_grid_open && ui_dialog_open(&ui));
    ui_sync_dialog_workspaces(&ui);assert(ui.mosaic_open);
    click(window,150,160,1);click(window,136+3*62+10,210,1); /* Seconds / 4. */
    assert(ui.mosaic_grid_edit.unit==0 && ui.mosaic_grid_edit.division==4 && m->grid.unit==1);
    const char *shot=getenv("TS_MOSAIC_GRID_PREVIEW");if(shot){static TsFramebuffer fb;ts_ui_render(&fb,&ui,&instrument);assert(ts_ui_write_ppm(&fb,shot));}
    key(window,SDLK_ESCAPE,0);assert(!ui.mosaic_grid_open && m->grid.unit==1);
    assert(dispatch(&e,window));click(window,150,160,1);click(window,150,290,1);
    assert(!ui.mosaic_grid_open && m->grid.unit==0 && m->grid.division==4);
    /* Ordinary movement stays free; Alt snaps the earliest selected start,
       preserving the group's timing offsets and one undo transaction. */
    SDL_SetModState(KMOD_ALT);click(window,190,200,1);motion(window,190,215);release(window,190,215);SDL_SetModState(KMOD_NONE);
    assert(fabs(a->start-1.25)<1e-8 && fabs(b->start-a->start-.37)<1e-8);
    key(window,SDLK_z,KMOD_CTRL);assert(a->start==1.03 && b->start==1.4);
    click(window,190,200,1);motion(window,190,215);release(window,190,215);
    assert(fabs(a->start-1.18)<1e-8);key(window,SDLK_z,KMOD_CTRL);
    /* Turning the grid off restores the existing Alt card-edge snap. */
    click(window,TS_MOSAIC_GRID_X+10,TS_MOSAIC_GRID_Y+4,1);assert(!m->grid.enabled);
    mosaic_select_only(&ui,a->id);SDL_SetModState(KMOD_ALT);click(window,190,200,1);motion(window,190,232);release(window,190,232);SDL_SetModState(KMOD_NONE);
    assert(fabs(a->start-b->start)<1e-8);key(window,SDLK_z,KMOD_CTRL);
    m->grid.enabled=1;ui.mosaic_ghosts[0]=*a;ui.mosaic_ghost_count=1;ui.mosaic_scale_anchor=0;
    double delta=mosaic_scale_snap(&ui,.11);TsMosaicEvent preview;ts_ui_mosaic_scale_event(a,&preview,delta,0);
    assert(fabs(preview.start+preview.duration-3.25)<1e-8);
    /* Pointer resizing uses the same grid; source-placement previews do too. */
    ui.mosaic_ghost_count=0;ui.mosaic_scale=70;
    SDL_SetModState(KMOD_ALT);click(window,190,276,1);assert(mosaic.drag==2);
    motion(window,190,287);release(window,190,287);SDL_SetModState(KMOD_NONE);
    assert(fabs(a->start+a->duration-3.25)<1e-8);key(window,SDLK_z,KMOD_CTRL);
    SDL_SetModState(KMOD_ALT);click(window,190,139,1);assert(mosaic.drag==6);
    motion(window,190,150);release(window,190,150);SDL_SetModState(KMOD_NONE);
    assert(fabs(a->start-1.25)<1e-8 && fabs(a->start+a->duration-3.03)<1e-8);key(window,SDLK_z,KMOD_CTRL);
    mosaic.source_group=0;mosaic.source_drag=*a;SDL_SetModState(KMOD_ALT);
    assert(mosaic_source_preview(&ui,&mosaic,250,150));SDL_SetModState(KMOD_NONE);
    assert(fabs(ui.mosaic_ghosts[0].start-1.25)<1e-8);ui.mosaic_ghost_count=0;
    /* A beat grid follows the master tempo, not the zoom or tape speed. */
    m->grid=(TsMosaicGrid){1,1,4};int bpm=audio.sister.clock_controls.bpm;audio.sister.clock_controls.bpm=90;
    key(window,SDLK_LCTRL,KMOD_CTRL);assert(fabs(ts_mosaic_grid_step(m->grid,ui.mosaic_grid_quarter)-1.0/6)<1e-8);
    audio.sister.clock_controls.bpm=bpm;
    ui.mosaic_ghost_count=0;ui.mosaic=audio.mosaic=saved;mosaic_poll(0,&ui,&instrument,&mosaic);ts_mosaic_free(m);
    puts("Mosaic semitone transposition, note limits, undo, grid dialog and contextual Alt snapping passed.");
}
