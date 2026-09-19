static void router_commit(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui)
{
    ts_router_sanitize(&ui->config.router);
    if(device)SDL_LockAudioDevice(device);
    ts_sister_runtime_set_router(&audio->sister,&ui->config.router);
    if(device)SDL_UnlockAudioDevice(device);
}
static int router_event(const SDL_Event *e,SDL_Window *window,SDL_AudioDeviceID device,
    AudioState *audio,TsUiState *ui,SisterWindow *sister)
{
    Uint32 id=event_window_id(e);int x=-1,y=-1;
    int main=id==SDL_GetWindowID(window),key=e->type==SDL_KEYDOWN;
    if(!main && (!sister->window || id!=sister->window_id))return 0;
    if(e->type==SDL_WINDOWEVENT && e->window.event==SDL_WINDOWEVENT_FOCUS_LOST){ui->router_drag=0;return 0;}
    if(ui_blocking_dialog_open_except_fm(ui) || ui->fm_bank_choice_open || ui->fm_full_choice_open || ui->midi_learn_active) {
        ui->router_open=ui->router_drag=0;return 0;
    }
    if(e->type==SDL_MOUSEBUTTONDOWN && main)logical_mouse(window,e->button.x,e->button.y,&x,&y);
    int toggle=key && e->key.keysym.sym==SDLK_F9 && !(e->key.keysym.mod&(KMOD_CTRL|KMOD_ALT|KMOD_GUI));
    toggle|=main && ui->master_eq_open && e->type==SDL_MOUSEBUTTONDOWN && e->button.button==SDL_BUTTON_LEFT &&
        x>=310 && x<620 && y>=356 && y<381;
    if(toggle) {
        if(key && e->key.repeat)return 1;
        ui->router_open=!ui->router_open;ui->router_drag=0;
        if(ui->router_open) {
            if(device)SDL_LockAudioDevice(device);
            ui->config.router=audio->sister.router.controls;
            if(device)SDL_UnlockAudioDevice(device);
            ui->master_eq_open=0;ui->master_eq_drag=0;
            application_window_focus(window,sister);
        }
        snprintf(ui->status,sizeof(ui->status),"ROUTER: DRAG ORDER / BYPASS / SOLO - F9 OPENS/CLOSES");return 1;
    }
    if(!ui->router_open || !main)return 0;
    if(e->type==SDL_KEYUP)return 0;
    if(key) {
        SDL_Keycode k=e->key.keysym.sym;
        if(k==SDLK_ESCAPE){if(ui->router_drag)ui->router_drag=0;else ui->router_open=0;return 1;}
        if(k==SDLK_TAB || k==SDLK_BACKQUOTE){ui->router_open=ui->router_drag=0;return 0;}
        if(k==SDLK_SPACE){if(!e->key.repeat)stop_all(device,audio,ui);return 1;}
        if(e->key.keysym.mod&(KMOD_CTRL|KMOD_ALT|KMOD_GUI))return 0;
        if(note_for_key(k)>=0 || (k>=SDLK_F1 && k<=SDLK_F8) || k==SDLK_F11)return 0;
        return 1;
    }
    if(e->type==SDL_MOUSEMOTION) {
        logical_mouse(window,e->motion.x,e->motion.y,&x,&y);
        if(!(e->motion.state&SDL_BUTTON_LMASK))ui->router_drag=0;
    } else if(e->type==SDL_MOUSEBUTTONUP)logical_mouse(window,e->button.x,e->button.y,&x,&y);
    else if(e->type!=SDL_MOUSEBUTTONDOWN)return e->type==SDL_MOUSEWHEEL;
    int row=y>=120 && y<304?(y-120)/48:-1;
    if(e->type==SDL_MOUSEBUTTONUP) {
        if(ui->router_drag && e->button.button==SDL_BUTTON_LEFT && row>=0 && x>=64 && x<616) {
            ts_router_move(&ui->config.router,ui->router_drag-1,row);router_commit(device,audio,ui);
        }
        ui->router_drag=0;
        return ui->mouse_note<0;
    }
    if(ui->router_drag){ui->router_drop=row;return 1;}
    if(e->type==SDL_MOUSEBUTTONDOWN) {
        if(y<32){ui->router_open=0;return 0;}
        if(x>=574 && y>=42 && y<66){ui->router_open=0;return 1;}
        if(row>=0 && x>=64 && x<616 && e->button.button==SDL_BUTTON_LEFT) {
            int stage=ui->config.router.order[row];
            if(x>=512)ts_router_toggle_solo(&ui->config.router,stage);
            else if(x>=414)ts_router_toggle_bypass(&ui->config.router,stage);
            else {ui->router_drag=row+1;ui->router_drop=row;return 1;}
            router_commit(device,audio,ui);
        }
    }
    return y<382; /* Preserve the global file recorder. */
}
