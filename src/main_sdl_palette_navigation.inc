/* Palette selection is navigation, not a transport/effect command. Keep the
   editor raised after exposing the destination, and respect pending edits. */
static void palette_window_navigate(SDL_Window *main,SDL_AudioDeviceID device,
    AudioState *audio,TsUiState *ui,SisterWindow *sister,TsSamplePages *pages,
    TsInstrument *bank,MosaicController *mosaic,PortalController *portal,
    TransformController *transform)
{
    PaletteWindow *w=&palette_window;int group=w->requested_group;
    w->requested_group=0;
    if(group<=TS_THEME_OVERALL || group>=TS_THEME_GROUP_COUNT)return;
    int opened=1;
    if(group>=TS_THEME_SISTER && group<=TS_THEME_PRISM) {
        if(sister->model.preset_manage_open) {
            snprintf(w->message,sizeof(w->message),"FINISH THE OPEN SISTER PRESET DIALOG FIRST");goto done;
        }
        if(!sister_window_ensure(sister,&ui->config)){opened=0;goto done;}
        sister->model.fx_page=group-TS_THEME_SISTER;
        sister->model.fallout_lfo_open=0;
        sister_window_show(sister,&ui->palette);
        goto shown;
    }
    switch(group) {
      case TS_THEME_ROUTER:
        if(matrix_window.visible){matrix_window.compressor_open=0;matrix_window.dirty=1;SDL_RaiseWindow(matrix_window.window);}
        else opened=matrix_show(device,audio,ui,pages,bank,-1,-1);
        goto shown;
      case TS_THEME_SPATIAL:opened=spatial_show(device,audio,ui);goto shown;
      case TS_THEME_HEALTH:opened=ts_audio_health_show(ui);goto shown;
      case TS_THEME_TRANSPORT:performance_show(ui,audio);opened=performance_transport.visible;goto shown;
      case TS_THEME_EQ:
        if(matrix_window.visible && matrix_window.compressor_open){SDL_RaiseWindow(matrix_window.window);goto shown;}
        break;
      default:break;
    }
    if(group==TS_THEME_BROWSER && (ui->config_open || ui->browser.mode!=TS_BROWSER_CLOSED)) {
        application_window_focus(main,sister);goto shown;
    }
    /* An unchanged configuration preview may be put away without writing a
       file; edited configuration and real confirmations retain ownership. */
    if(ui->config_open && !memcmp(&ui->config,&ui->config_before_edit,sizeof(ui->config))) {
        ui->config_open=0;SDL_StopTextInput();
    }
    if(ui_blocking_dialog_open_except_workspaces(ui) || ui->fm_bank_choice_open ||
       ui->fm_full_choice_open || transform->worker || portal_edit_gesture_active(ui) ||
       (ui->portal.open && (ui->portal.manage_open || ui->portal.macro_edit || ui->portal.full_action)) ||
       (ui->canvas_eq_open && group!=TS_THEME_EQ)) {
        snprintf(w->message,sizeof(w->message),"FINISH THE OPEN EDIT OR DIALOG BEFORE CHANGING VIEW");goto done;
    }
    if(group==TS_THEME_PORTAL && ui->portal.open) {
        application_window_focus(main,sister);goto shown;
    }
    if(ui->portal.open && portal->worker) {
        snprintf(w->message,sizeof(w->message),"FINISH THE PORTAL RENDER BEFORE CHANGING VIEW");goto done;
    }
    if((group==TS_THEME_TRACKER || group==TS_THEME_MOSAIC || group==TS_THEME_PORTAL) &&
       (ui->external_record_bank || (ui->capture_state!=TS_CAPTURE_IDLE && !ui->mosaic_recording && !ui->quick_capture_active))) {
        snprintf(w->message,sizeof(w->message),"FINISH CAPTURE BEFORE CHANGING THIS WORKSPACE");goto done;
    }
    if(ui->tracker_open && group!=TS_THEME_TRACKER && ts_tapehead_frame()) {
        if(!ts_tapehead_export(&pages->tracker,ui->status,sizeof(ui->status))){opened=0;goto done;}
        ts_tapehead_focus_lost();
    }
    if((group==TS_THEME_MOSAIC || group==TS_THEME_TRACKER) && mosaic->active) {
        int was_fm=ui->fm_open;
        if(was_fm){mosaic->fm_owner=mosaic->active;mosaic->fm_epoch=ui->mosaic->epoch;workspace_park_fm(ui,mosaic);}
        if(!mosaic_leave(device,audio,ui,bank,mosaic)) {
            if(ui->mosaic_edit_choice)mosaic->fm_edit_exit=was_fm;
            else if(was_fm){ui->fm_open=1;mosaic->fm_parked=0;}
            application_window_focus(main,sister);
            snprintf(w->message,sizeof(w->message),"RESOLVE THE MOSAIC EDIT, THEN SELECT THE COMPONENT AGAIN");goto done;
        }
    }
    if(ui->portal.open)portal_close(device,audio,ui,portal);
    if(group!=TS_THEME_EQ)ui->master_eq_open=ui->master_eq_drag=ui->master_eq_presets_open=0;
    ui->router_open=0;
    if(ui->midi_learn_active && group!=TS_THEME_MIDI)midi_learn_set_active(ui,sister,0);
    if(group==TS_THEME_EQ) {
        if(!ui->master_eq_open)master_eq_open(device,audio,ui);
    } else if(group==TS_THEME_MOSAIC) {
        if(ui->fm_open){mosaic->fm_owner=mosaic->active;mosaic->fm_epoch=ui->mosaic->epoch;workspace_park_fm(ui,mosaic);}
        ui->tracker_open=0;ui->mosaic_open=1;mosaic_drag_end(ui,mosaic);
    } else if(group==TS_THEME_TRACKER) {
        if(!ui->tracker_open)opened=embedded_open(main,device,audio,ui,pages,bank,audio->output_rate>0?audio->output_rate:48000);
        if(opened) {
            if(ui->fm_open){mosaic->fm_owner=mosaic->active;mosaic->fm_epoch=ui->mosaic->epoch;workspace_park_fm(ui,mosaic);}
            ui->mosaic_open=0;ui->tracker_open=1;ts_tapehead_theme_apply(&ui->palette);
        }
    } else {
        ui->tracker_open=ui->mosaic_open=0;
        if(group==TS_THEME_PORTAL) {
            if(ui->fm_open){mosaic->fm_owner=mosaic->active;mosaic->fm_epoch=ui->mosaic->epoch;workspace_park_fm(ui,mosaic);}
            ui->portal.open=1;
            /* Reuse an existing preview. First opening snapshots the tile,
               without the normal Portal shortcut's global transport stop. */
            if(!ui->portal.source && !portal->worker) {
                portal->selection_scope=bank->has_selection;
                (void)portal_source(device,audio,ui,bank,portal);
            }
        } else if(group==TS_THEME_ARP) {
            ui->show_keyboard=1;ui->show_recipes=ui->show_ingredients=0;
            ui->keyboard_sequence_open=1;
            if(device)SDL_LockAudioDevice(device);
            keyboard_sequence_ui(audio,ui);
            if(device)SDL_UnlockAudioDevice(device);
        } else if(group==TS_THEME_CANVAS)ui->keyboard_sequence_open=0;
        else if(group==TS_THEME_BROWSER)begin_config(ui);
        else if(group==TS_THEME_MIDI)midi_learn_set_active(ui,sister,1);
    }
    application_window_focus(main,sister);
shown:
    if(opened)snprintf(w->message,sizeof(w->message),"%s / LIVE COLOR EDITING",ts_theme_group_name(group));
done:
    if(!opened)snprintf(w->message,sizeof(w->message),"%.127s",ui->status);
    w->dirty=1;
    SDL_RaiseWindow(w->window);
}
