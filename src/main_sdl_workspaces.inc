/* Workspace focus is independent of playback ownership. FM can be parked
   behind Mosaic without losing its patch; Sister is a separate window. */
static void workspace_park_fm(SDL_AudioDeviceID device,AudioState *audio,
                              TsUiState *ui,MosaicController *c)
{
    if(device)SDL_LockAudioDevice(device);
    runtime_note_clear(audio);
    if(device)SDL_UnlockAudioDevice(device);
    ui->active_notes=0;ui->fm_held_notes=0;ui->fm_output_dragging=0;
    ui->fm_open=0;c->fm_parked=1;
}

static void workspace_open_fm(SDL_AudioDeviceID device,AudioState *audio,
                              TsUiState *ui,TsInstrument *instrument,
                              MosaicController *c,TsSample *preview)
{
    if(c->fm_parked && c->fm_epoch==ui->mosaic->epoch &&
       ui->fm_preview_sample==preview && preview->data &&
       (!c->active || c->active==c->fm_owner) &&
       (!c->fm_owner || ts_mosaic_find(ui->mosaic,c->fm_owner))) {
        if(c->fm_owner && c->active!=c->fm_owner &&
           !mosaic_enter(device,audio,ui,instrument,c,c->fm_owner))return;
        ui->mosaic_open=0;ui->fm_open=1;c->fm_parked=0;
    } else {
        c->fm_parked=0;c->fm_owner=c->active;c->fm_epoch=ui->mosaic->epoch;ui->mosaic_open=0;
        begin_fm_workspace(device,audio,ui,instrument,preview);
    }
}

static int workspace_event(const SDL_Event *event,SDL_Window *window,
    SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,TsInstrument *instrument,
    MosaicController *c,PortalController *portal,SisterWindow *sister,
    TransformController *transform,TsSample *preview)
{
    int key=event->type==SDL_KEYDOWN;
    int id=key?(int)event->key.windowID:event->type==SDL_MOUSEBUTTONDOWN?(int)event->button.windowID:0;
    int from_sister=sister->window && (uint32_t)id==sister->window_id;
    if((uint32_t)id!=SDL_GetWindowID(window) && !from_sister)return 0;
    SDL_Keymod mod=key?(SDL_Keymod)event->key.keysym.mod:KMOD_NONE;
    if(from_sister && key && event->key.keysym.sym==SDLK_ESCAPE && !ui->midi_learn_active) {
        if(!event->key.repeat) {
            if(sister->model.fallout_lfo_open)sister->model.fallout_lfo_open=0;
            else if(sister->model.preset_manage_open) {
                sister->model.preset_manage_open=sister->model.preset_editing=sister->model.preset_confirmation=0;
                SDL_StopTextInput();
            } else application_window_focus(window,sister);
        }
        return 1;
    }
    int to_mosaic=mosaic_toggle_key(event);
    int to_fm=key && event->key.keysym.sym==SDLK_BACKQUOTE && (mod&KMOD_SHIFT) &&
        !(mod&(KMOD_CTRL|KMOD_ALT|KMOD_GUI));
    int to_portal=key && event->key.keysym.sym==SDLK_p &&
        (mod&(KMOD_CTRL|KMOD_SHIFT))==(KMOD_CTRL|KMOD_SHIFT) && !(mod&(KMOD_ALT|KMOD_GUI));
    if(!from_sister && event->type==SDL_MOUSEBUTTONDOWN && event->button.button==SDL_BUTTON_LEFT && !ui->portal.open) {
        int x,y;logical_mouse(window,event->button.x,event->button.y,&x,&y);
        to_mosaic=x>=300 && x<356 && y>=4 && y<28;
    }
    if(!to_mosaic && !to_fm && !to_portal)return 0;
    if(key && event->key.repeat)return 1;
    if(ui_blocking_dialog_open_except_workspaces(ui) || ui->fm_bank_choice_open ||
       ui->fm_full_choice_open || sister->model.preset_manage_open ||
       (ui->portal.open && (ui->portal.manage_open || ui->portal.macro_edit || ui->portal.full_action)))return 1;
    if(ui->external_record_bank || ui->capture_state!=TS_CAPTURE_IDLE || transform->worker || portal_edit_gesture_active(ui)) {
        snprintf(ui->status,sizeof(ui->status),"FINISH CAPTURE OR EDIT BEFORE CHANGING WORKSPACE");return 1;
    }
    if(to_portal) {
        if(ui->portal.open) {
            if(!from_sister) {
                portal_close(device,audio,ui,portal);
                if(c->portal_return_fm)workspace_open_fm(device,audio,ui,instrument,c,preview);
            }
        } else {
            c->portal_return_fm=ui->fm_open;
            if(ui->fm_open) {
                c->fm_owner=c->active;c->fm_epoch=ui->mosaic->epoch;
                workspace_park_fm(device,audio,ui,c);
            }
            if(ui->mosaic_open && ui->mosaic_selected &&
               !mosaic_enter(device,audio,ui,instrument,c,ui->mosaic_selected))return 1;
            ui->mosaic_open=0;
            SDL_Event local=*event;local.key.windowID=SDL_GetWindowID(window);
            portal_event(&local,window,device,audio,ui,instrument,portal,sister,audio->output_rate,transform);
        }
        application_window_focus(window,sister);return 1;
    }
    if(ui->portal.open)portal_close(device,audio,ui,portal);
    /* A key pressed over Sister selects its destination. It must never
       toggle off a destination that is merely open in the other window. */
    if(to_fm) {
        if(ui->fm_open) {
            if(!from_sister) {
                int back=c->fm_return_mosaic;
                c->fm_owner=c->active;c->fm_epoch=ui->mosaic->epoch;
                workspace_park_fm(device,audio,ui,c);
                if(back && c->active && !mosaic_leave(device,audio,ui,instrument,c)) {
                    if(ui->mosaic_edit_choice)c->fm_edit_exit=1;
                    else {ui->fm_open=1;c->fm_parked=0;}
                    return 1;
                }
                if(back)ui->mosaic_open=1;
            }
        } else {
            c->fm_return_mosaic=ui->mosaic_open;
            workspace_open_fm(device,audio,ui,instrument,c,preview);
        }
    } else if(ui->mosaic_open) {
        if(!from_sister) {
            ui->mosaic_open=0;
            if(c->mosaic_return_fm)workspace_open_fm(device,audio,ui,instrument,c,preview);
        }
    } else {
        int was_fm=ui->fm_open;
        if(was_fm && c->active){c->fm_owner=c->active;c->fm_epoch=ui->mosaic->epoch;}
        c->mosaic_return_fm=was_fm;
        if(was_fm)workspace_park_fm(device,audio,ui,c);
        if(c->active && !mosaic_leave(device,audio,ui,instrument,c)) {
            if(ui->mosaic_edit_choice)c->fm_edit_exit=was_fm;
            else if(was_fm){ui->fm_open=1;c->fm_parked=0;}
            application_window_focus(window,sister);return 1;
        }
        if(!was_fm)stop_all_force(device,audio,ui);
        ui->mosaic_open=1;
        snprintf(ui->status,sizeof(ui->status),"MOSAIC - DRAG SOURCES; DOUBLE CLICK EVENTS TO EDIT");
    }
    mosaic_drag_end(ui,c);
    application_window_focus(window,sister);
    return 1;
}
