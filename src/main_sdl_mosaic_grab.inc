/* G uses the ordinary rigid move planner, but only previews its displacement.
   Confirmation publishes one undoable move; cancellation never touches history,
   source ownership, playback or the selected cards' identities. */
static void mosaic_drag_service(SDL_AudioDeviceID device,TsUiState *ui,
                                MosaicController *c,Uint32 now,int focused,Uint32 buttons)
{
    if(c->drag!=1 && !(c->drag>=7 && c->drag<=9))return;
    /* Only pointer drags depend on a held button. G owns the move until
       confirmation/cancellation, with the same focus and dialog boundaries. */
    if(!focused || (!ui->mosaic_grab && !(buttons&SDL_BUTTON_LMASK)) ||
       ui_dialog_open(ui))mosaic_drag_end(ui,c);
    else if(c->drag==1)mosaic_drag_autoscroll(device,ui,c,now);
}

static int mosaic_grab_event(const SDL_Event *event,SDL_Window *window,SDL_AudioDeviceID device,
                             TsUiState *ui,MosaicController *c,int x,int y,SDL_Keymod mod)
{
    if(!ui->mosaic_grab)return 0;
    if(!ui->mosaic_open || c->active || c->ui_epoch!=ui->mosaic->epoch ||
       ui_dialog_open(ui) || ui->router_open || ui->master_eq_open ||
       (event->type==SDL_WINDOWEVENT && (event->window.event==SDL_WINDOWEVENT_FOCUS_LOST ||
        event->window.event==SDL_WINDOWEVENT_HIDDEN || event->window.event==SDL_WINDOWEVENT_MINIMIZED))) {
        mosaic_drag_end(ui,c);return 0;
    }
    int key=event->type==SDL_KEYDOWN,down=event->type==SDL_MOUSEBUTTONDOWN;
    SDL_Keycode k=key?event->key.keysym.sym:0;
    int cancel=(key && k==SDLK_ESCAPE) || (down && event->button.button==SDL_BUTTON_RIGHT);
    int confirm=(key && (k==SDLK_RETURN || k==SDLK_KP_ENTER)) || (down && event->button.button==SDL_BUTTON_LEFT);
    if(cancel || confirm) {
        if(confirm && down)mosaic_drag_motion(device,ui,c,x,y,mod);
        if(confirm && (fabs(ui->mosaic_grab_x)>1e-9 || fabs(ui->mosaic_grab_time)>1e-9)) {
            if(device)SDL_LockAudioDevice(device);
            ts_mosaic_checkpoint(ui->mosaic);mosaic_move_group(ui,ui->mosaic_grab_x,ui->mosaic_grab_time);
            if(device)SDL_UnlockAudioDevice(device);
        }
        mosaic_drag_end(ui,c);
        snprintf(ui->status,sizeof(ui->status),"GRAB %s / G MOVE SELECTION",cancel?"CANCELLED":"CONFIRMED");return 1;
    }
    if(event->type==SDL_MOUSEMOTION) {
        c->drag_pointer_x=x;c->drag_pointer_y=y;mosaic_drag_motion(device,ui,c,x,y,mod);
    } else if((key || event->type==SDL_KEYUP) &&
              (event->key.keysym.sym==SDLK_LALT || event->key.keysym.sym==SDLK_RALT)) {
        mosaic_drag_motion(device,ui,c,c->drag_pointer_x,c->drag_pointer_y,(SDL_Keymod)event->key.keysym.mod);
    }
    (void)window;
    return key || event->type==SDL_KEYUP || down || event->type==SDL_MOUSEBUTTONUP ||
        event->type==SDL_MOUSEMOTION || event->type==SDL_MOUSEWHEEL;
}
static void mosaic_grab_begin(SDL_Window *window,TsUiState *ui,MosaicController *c)
{
    if(c->drag || c->mix_drag || c->volume_drag || c->pan_active || ui->mosaic_loop_drag || ui->mosaic_marker_drag)return;
    mosaic_drag_end(ui,c);mosaic_selection_repair(ui);
    ui->mosaic_ghost_count=mosaic_selected_events(ui,ui->mosaic_ghosts);
    if(!ui->mosaic_ghost_count){snprintf(ui->status,sizeof(ui->status),"SELECT CARDS / G MOVE SELECTION");return;}
    int px,py;SDL_GetMouseState(&px,&py);logical_mouse(window,px,py,&c->drag_x,&c->drag_y);
    c->drag=1;c->drag_epoch=ui->mosaic->epoch;c->drag_xscroll=ui->mosaic_xscroll;
    c->drag_pointer_x=c->drag_x;c->drag_pointer_y=c->drag_y;c->drag_scroll_ms=SDL_GetTicks();
    ui->mosaic_grab=1;ui->mosaic_follow=0;c->zoom_saved=0;
    snprintf(ui->status,sizeof(ui->status),"G %d / CLICK/ENTER SET / ESC/RMB CANCEL / ALT SNAP",ui->mosaic_ghost_count);
}
