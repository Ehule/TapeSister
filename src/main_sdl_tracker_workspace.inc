static void tracker_workspace_enter(TsUiState *ui,SDL_Window *main,Uint32 origin)
{
    ui->tracker_return_mosaic=ui->mosaic_open;ui->tracker_return_window_id=origin;
    ui->tracker_open=1;ui->mosaic_open=0;
    if(origin!=SDL_GetWindowID(main)){
        if(SDL_GetWindowFlags(main)&SDL_WINDOW_MINIMIZED)SDL_RestoreWindow(main);
        SDL_ShowWindow(main);SDL_RaiseWindow(main);SDL_SetWindowInputFocus(main);
    }
}
static void tracker_workspace_leave(TsUiState *ui)
{
    ui->tracker_open=0;ui->mosaic_open=ui->tracker_return_mosaic;
    editor_return_window(ui->tracker_return_window_id);
    ui->tracker_return_window_id=0;ui->tracker_return_mosaic=0;
}
static int tracker_workspace_event_source(const SDL_Event *e,SDL_Window *main)
{
    Uint32 id=event_window_id(e);
    return id==SDL_GetWindowID(main) || (matrix_window.visible && id==matrix_window.id &&
        e->type==SDL_KEYDOWN && e->key.keysym.sym==SDLK_F10);
}
