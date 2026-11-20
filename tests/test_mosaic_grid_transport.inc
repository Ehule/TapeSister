static void test_mosaic_grid_transport(SDL_Window *window)
{
    TsMosaic *saved=ui.mosaic,*m=ts_mosaic_create();assert(m);ui.mosaic=audio.mosaic=m;
    ui.mosaic_open=1;ui.mosaic_follow=0;mosaic_poll(0,&ui,&instrument,&mosaic);
    ui.mosaic_scroll=1.03;ui.mosaic_scale=100;ui.mosaic_xscroll=0;ui.mosaic_hscale=1;
    m->grid=(TsMosaicGrid){1,0,4};m->playing=1;int history=m->history_count;
    click(window,130,99,1);assert(m->time==1.25 && ui.mosaic_time==1.25 && m->playing);
    m->time=4.8;
    mosaic_nav_mouse(window,SDL_MOUSEBUTTONDOWN,SDL_BUTTON_MIDDLE,220,99);assert(m->time==4.8);
    mosaic_nav_mouse(window,SDL_MOUSEBUTTONUP,SDL_BUTTON_MIDDLE,220,99);assert(m->time==1.25 && m->playing);
    /* Middle-drag remains view navigation even with GRID enabled. */
    mosaic_nav_mouse(window,SDL_MOUSEBUTTONDOWN,SDL_BUTTON_MIDDLE,220,99);
    mosaic_nav_mouse(window,SDL_MOUSEMOTION,SDL_BUTTON_MIDDLE,240,119);
    mosaic_nav_mouse(window,SDL_MOUSEBUTTONUP,SDL_BUTTON_MIDDLE,240,119);
    assert(m->time==1.25 && m->history_count==history);
    ui.mosaic_scroll=1.03;m->grid.enabled=0;click(window,130,99,1);assert(fabs(m->time-1.36)<1e-12);
    m->time=4.8;mosaic_nav_mouse(window,SDL_MOUSEBUTTONDOWN,SDL_BUTTON_MIDDLE,220,99);
    mosaic_nav_mouse(window,SDL_MOUSEBUTTONUP,SDL_BUTTON_MIDDLE,220,99);assert(fabs(m->time-1.36)<1e-12);
    /* Snapping uses the real interval, including dense and tempo-based grids. */
    m->grid=(TsMosaicGrid){1,0,64};click(window,130,99,1);assert(m->time==1.359375);
    int bpm=audio.sister.clock_controls.bpm;audio.sister.clock_controls.bpm=90;m->grid=(TsMosaicGrid){1,1,4};
    click(window,130,99,1);assert(fabs(m->time-4.0/3)<1e-12);audio.sister.clock_controls.bpm=bpm;
    assert(m->history_count==history && m->playing);
    m->grid=(TsMosaicGrid){1,0,4};assert(ts_mosaic_set_loop_range(m,1.13,2.91));
    double transport=m->time;
    /* An arbitrary existing range snaps one handle at a time, even with no
       pointer movement. The opposite edge and playback position stay exact. */
    SDL_SetModState(KMOD_ALT);click(window,130,76,1);
    assert(ui.mosaic_loop_drag==2 && ui.mosaic_loop_preview.start==1.25 && m->loop_range.start==1.13);
    release(window,130,76);SDL_SetModState(KMOD_NONE);
    assert(m->loop_range.start==1.25 && m->loop_range.end==2.91 && m->time==transport && m->history_count==history+1);
    key(window,SDLK_z,KMOD_CTRL);assert(m->loop_range.start==1.13 && m->loop_range.end==2.91);
    key(window,SDLK_y,KMOD_CTRL);assert(m->loop_range.start==1.25);
    SDL_SetModState(KMOD_ALT);click(window,130,254,1);release(window,130,254);SDL_SetModState(KMOD_NONE);
    assert(m->loop_range.start==1.25 && m->loop_range.end==3);
    key(window,SDLK_z,KMOD_CTRL);assert(m->loop_range.end==2.91);
    /* Crossing the opposite edge clamps to a valid grid point, never an
       off-grid .001-second substitute or an inverted/empty selection. */
    assert(ts_mosaic_set_loop_range(m,1.13,2.91));
    SDL_SetModState(KMOD_ALT);click(window,130,76,1);release(window,130,300);SDL_SetModState(KMOD_NONE);
    assert(m->loop_range.start==2.75 && m->loop_range.end==2.91);
    key(window,SDLK_z,KMOD_CTRL);assert(m->loop_range.start==1.13);
    SDL_SetModState(KMOD_ALT);click(window,130,254,1);release(window,130,76);SDL_SetModState(KMOD_NONE);
    assert(m->loop_range.start==1.13 && m->loop_range.end==1.25);
    key(window,SDLK_z,KMOD_CTRL);assert(m->loop_range.end==2.91);
    /* Press/release Alt during a new selection without moving the mouse.
       Raw endpoints are retained so releasing Alt restores free placement. */
    SDL_SetModState(KMOD_SHIFT);click(window,130,77,1);SDL_SetModState(KMOD_NONE);
    mosaic_nav_mouse(window,SDL_MOUSEMOTION,SDL_BUTTON_LEFT,130,252);
    assert(fabs(ui.mosaic_loop_preview.start-1.14)<1e-12 && fabs(ui.mosaic_loop_preview.end-2.89)<1e-12);
    key(window,SDLK_LALT,KMOD_LALT);assert(ui.mosaic_loop_preview.start==1.25 && ui.mosaic_loop_preview.end==3);
    SDL_Event alt_up;SDL_zero(alt_up);alt_up.type=SDL_KEYUP;alt_up.key.windowID=SDL_GetWindowID(window);alt_up.key.keysym.sym=SDLK_LALT;
    assert(dispatch(&alt_up,window));
    assert(fabs(ui.mosaic_loop_preview.start-1.14)<1e-12 && fabs(ui.mosaic_loop_preview.end-2.89)<1e-12);
    SDL_SetModState(KMOD_ALT);release(window,130,252);SDL_SetModState(KMOD_NONE);
    assert(m->loop_range.start==1.25 && m->loop_range.end==3 && m->time==transport);
    key(window,SDLK_z,KMOD_CTRL);assert(m->loop_range.start==1.13 && m->loop_range.end==2.91);
    /* Reverse-direction selection works too; Escape abandons its preview. */
    history=m->history_cursor;
    SDL_SetModState(KMOD_SHIFT|KMOD_ALT);click(window,130,252,1);
    mosaic_nav_mouse(window,SDL_MOUSEMOTION,SDL_BUTTON_LEFT,130,77);
    assert(ui.mosaic_loop_preview.start==1.25 && ui.mosaic_loop_preview.end==3);
    key(window,SDLK_ESCAPE,KMOD_ALT);SDL_SetModState(KMOD_NONE);
    assert(!ui.mosaic_loop_drag && m->loop_range.start==1.13 && m->loop_range.end==2.91 && m->history_cursor==history && m->playing);
    SDL_SetModState(KMOD_SHIFT|KMOD_ALT);click(window,130,252,1);release(window,130,77);SDL_SetModState(KMOD_NONE);
    assert(m->loop_range.start==1.25 && m->loop_range.end==3);
    /* GRID off restores free edges even while Alt is held. */
    m->grid.enabled=0;
    SDL_SetModState(KMOD_SHIFT|KMOD_ALT);click(window,130,252,1);release(window,130,77);SDL_SetModState(KMOD_NONE);
    assert(fabs(m->loop_range.start-1.14)<1e-12 && fabs(m->loop_range.end-2.89)<1e-12);
    assert(m->time==transport && m->playing);
    mosaic_drag_end(&ui,&mosaic);ui.mosaic=audio.mosaic=saved;mosaic_poll(0,&ui,&instrument,&mosaic);ts_mosaic_free(m);
    puts("Mosaic transport grid seeks and live Alt range-edge snapping passed.");
}
