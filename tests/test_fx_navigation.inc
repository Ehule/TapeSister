static void navigation_keys(void)
{
    static SisterWindow editor;
    ts_sister_ui_model_init(&editor.model,&ui.config);
    MatrixWindow *w=&matrix_window;
    assert(!w->window); /* Exercise the first E, before F9 or any routing editor. */
    SDL_Event e;SDL_zero(e);e.type=SDL_KEYDOWN;
    e.key.windowID=SDL_GetWindowID(main_window);e.key.keysym.sym=SDLK_e;e.key.keysym.scancode=SDL_SCANCODE_E;
#define NAV() fx_navigation_event(&e,main_window,0,&audio,&ui,&editor,&pages,&bank)
    ui.mosaic_source_keys=1;assert(!NAV() && !w->visible);ui.mosaic_source_keys=0;
    ui.mosaic_live_open=1;assert(!NAV());ui.mosaic_live_open=0;
    ui.renaming_bank_slot=0;assert(!NAV());ui.renaming_bank_slot=-1;
    for(int i=0;i<4;++i) {
        e.key.keysym.mod=(SDL_Keymod[]){KMOD_CTRL,KMOD_ALT,KMOD_SHIFT,KMOD_GUI}[i];assert(!NAV());
    }
    e.key.keysym.mod=KMOD_NUM|KMOD_CAPS; /* Lock modifiers must not block E. */
    assert(NAV() && w->visible && w->canvas);
    e.key.windowID=w->id;e.key.repeat=1;assert(NAV() && w->visible);
    e.key.repeat=0;e.type=SDL_KEYUP;assert(NAV() && w->visible);
    /* A text field in the receiving window retains E. */
    e.type=SDL_KEYDOWN;fx_canvas.label_editing=1;assert(!NAV() && w->visible);fx_canvas.label_editing=0;
    w->rename=0;assert(!NAV() && w->visible);w->rename=-1;
    fx_canvas.confirm_delete=1;assert(!NAV() && w->visible);fx_canvas.confirm_delete=0;
    assert(NAV() && !w->visible && ui.mosaic_open && !ui.tracker_open);
    e.key.windowID=SDL_GetWindowID(main_window);e.type=SDL_KEYUP;assert(NAV() && !w->visible);
    e.type=SDL_KEYDOWN;e.key.repeat=1;assert(NAV() && !w->visible);e.key.repeat=0;
    /* Physical E also works when the Linux keyboard layout changes the symbol. */
    e.key.keysym.sym=0x0443;e.key.keysym.mod=0;
    assert(NAV() && w->visible && w->canvas);
    e.key.windowID=w->id;assert(NAV() && !w->visible);
    /* A modeless editor behind Mosaic cannot own Mosaic's keyboard events. */
    e.key.windowID=SDL_GetWindowID(main_window);e.key.keysym.sym=SDLK_e;
    editor.model.preset_manage_open=1;w->rename=0;sunvox_editor.focus=1;
    assert(NAV() && w->visible);
    editor.model.preset_manage_open=0;
    e.key.windowID=w->id;
    ui.mosaic_source_keys=1;assert(!NAV() && w->visible);ui.mosaic_source_keys=0;
    assert(NAV() && !w->visible);
    /* Existing editor -> FX navigation remains available; second E returns to Mosaic. */
    assert(sister_window_ensure(&editor,&ui.config));sister_window_show(&editor,&ui.palette);
    e.key.windowID=editor.window_id;assert(NAV() && w->visible && !editor.model.visible);
    e.key.windowID=w->id;assert(NAV() && !w->visible);
    SDL_DestroyTexture(editor.texture);SDL_DestroyRenderer(editor.renderer);SDL_DestroyWindow(editor.window);
    memset(&editor,0,sizeof(editor));
    matrix_close();
#undef NAV
}
