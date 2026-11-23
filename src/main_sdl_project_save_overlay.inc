/* Host save dialogs are composited over the routing canvas. Their existing
   handlers still receive host coordinates; no workspace or focus change. */
static int project_save_overlay_active(const TsUiState *ui)
{
    return ui->project_save_window_id && (ui->project_overwrite_confirm_open ||
        ui->browser.mode==TS_BROWSER_SAVE_RECIPE || ui->browser.mode==TS_BROWSER_EXPORT_PROJECT_ZIP || ui->file_busy);
}
static void project_save_overlay_poll(TsUiState *ui)
{
    if(!ui->project_save_window_id || project_save_overlay_active(ui))return;
    if(ui->project_save_window_id==matrix_window.id) {
        snprintf(matrix_window.status,sizeof(matrix_window.status),"%s",ui->status);
        matrix_window.dirty=1;
    }
    ui->project_save_window_id=0;
}
static void project_save_overlay_event(SDL_Event *e,SDL_Window *main,TsUiState *ui)
{
    if(!project_save_overlay_active(ui) || event_window_id(e)!=ui->project_save_window_id)return;
    SDL_Window *owner=SDL_GetWindowFromID(ui->project_save_window_id);if(!owner)return;
    int ow,oh,mw,mh;SDL_GetWindowSize(owner,&ow,&oh);SDL_GetWindowSize(main,&mw,&mh);
    Uint32 id=SDL_GetWindowID(main);
    switch(e->type) {
    case SDL_KEYDOWN:case SDL_KEYUP:e->key.windowID=id;break;
    case SDL_TEXTINPUT:e->text.windowID=id;break;
    case SDL_TEXTEDITING:e->edit.windowID=id;break;
    case SDL_MOUSEBUTTONDOWN:case SDL_MOUSEBUTTONUP:
        e->button.windowID=id;e->button.x=e->button.x*mw/SDL_max(1,ow);e->button.y=e->button.y*mh/SDL_max(1,oh);break;
    case SDL_MOUSEMOTION:
        e->motion.windowID=id;e->motion.x=e->motion.x*mw/SDL_max(1,ow);e->motion.y=e->motion.y*mh/SDL_max(1,oh);break;
    case SDL_MOUSEWHEEL:
        e->wheel.windowID=id;
#if SDL_VERSION_ATLEAST(2,26,0)
        e->wheel.mouseX=e->wheel.mouseX*mw/SDL_max(1,ow);e->wheel.mouseY=e->wheel.mouseY*mh/SDL_max(1,oh);
#endif
        break;
    case SDL_WINDOWEVENT:
        if(e->window.event==SDL_WINDOWEVENT_CLOSE) {
            SDL_zero(*e);e->type=SDL_KEYDOWN;e->key.windowID=id;e->key.keysym.sym=SDLK_ESCAPE;
        }
        break;
    }
}
