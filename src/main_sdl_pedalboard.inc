/* The viewport never changes DSP order or steals the parameter mouse wheel. */
static void sister_fx_scroll_stop(SisterWindow *sister)
{
    sister->fx_scroll_drag = sister->fx_scroll_repeat = 0;
}

static void sister_fx_scroll_to(SisterWindow *sister, int first)
{
    ts_ui_pointer_drag_cancel(&sister->parameter_drag);
    ts_sister_ui_fx_scroll_set(&sister->model, first);
    sister->rendered_model_valid = 0;
}

static int sister_pedalboard_action(SDL_AudioDeviceID device, AudioState *audio,
                                    TsUiState *ui, SisterWindow *sister, TsSisterUiHit hit)
{
    TsSisterFxControls *fx = &sister->model.parameters.fx;
    if (hit.action == TS_SISTER_UI_ACTION_FX_SCROLL) {
        sister_fx_scroll_to(sister, ts_sister_ui_fx_scroll(&sister->model) + hit.index);
        return 1;
    }
    if (hit.action == TS_SISTER_UI_ACTION_FX_REMOVE_CANCEL) {
        sister->model.fx_remove_confirmation = 0;
        return 1;
    }
    if (hit.action != TS_SISTER_UI_ACTION_FX_ADD &&
        hit.action != TS_SISTER_UI_ACTION_FX_REMOVE &&
        hit.action != TS_SISTER_UI_ACTION_FX_REMOVE_CONFIRM) return 0;
    sister_fx_scroll_stop(sister);
    ts_ui_pointer_drag_cancel(&sister->parameter_drag);
    if (hit.action == TS_SISTER_UI_ACTION_FX_REMOVE_CONFIRM &&
        !sister->model.fx_remove_confirmation) return 1;
    if (hit.action == TS_SISTER_UI_ACTION_FX_ADD) {
        if (fx->slot_count >= TS_SISTER_FX_SLOT_COUNT) {
            snprintf(sister->model.status,sizeof(sister->model.status),"PEDALBOARD MAXIMUM: 20 SLOTS");
            return 1;
        }
        fx->slot[fx->slot_count++] = ts_sister_fx_empty_slot();
        sister_fx_scroll_to(sister, fx->slot_count - TS_SISTER_UI_FX_VISIBLE);
    } else {
        if (fx->slot_count <= TS_SISTER_FX_SLOT_MIN) {
            sister->model.fx_remove_confirmation = 0;
            snprintf(sister->model.status,sizeof(sister->model.status),"PEDALBOARD MINIMUM: 5 SLOTS");
            return 1;
        }
        if (hit.action == TS_SISTER_UI_ACTION_FX_REMOVE &&
            fx->slot[fx->slot_count-1].type != TS_SISTER_FX_EMPTY) {
            sister->model.fx_remove_confirmation = 1;
            return 1;
        }
        fx->slot[--fx->slot_count] = ts_sister_fx_empty_slot();
        sister_fx_scroll_to(sister, sister->model.fx_scroll);
    }
    sister->model.fx_remove_confirmation = 0;
    if (device) SDL_LockAudioDevice(device);
    ts_sister_runtime_set_parameters(&audio->sister, &sister->model.parameters);
    ts_sister_runtime_mark_selected_preset_modified(&audio->sister);
    if (device) SDL_UnlockAudioDevice(device);
    ui->project_modified = 1;
    snprintf(sister->model.status,sizeof(sister->model.status),
             "%d SLOTS / + APPENDS / - REMOVES LAST / INI DEFAULT UNCHANGED",fx->slot_count);
    return 1;
}

static int sister_pedalboard_event(SDL_AudioDeviceID device, AudioState *audio,
                                   TsUiState *ui, SisterWindow *sister, const SDL_Event *event)
{
    if (event->type == SDL_WINDOWEVENT &&
        (event->window.event == SDL_WINDOWEVENT_FOCUS_LOST ||
         event->window.event == SDL_WINDOWEVENT_HIDDEN ||
         event->window.event == SDL_WINDOWEVENT_MINIMIZED ||
         event->window.event == SDL_WINDOWEVENT_LEAVE)) sister_fx_scroll_stop(sister);
    if (sister->model.fx_page != 1 || sister->model.preset_manage_open || ui->midi_learn_active) {
        sister_fx_scroll_stop(sister); return 0;
    }
    if (sister->model.fx_remove_confirmation) {
        TsSisterUiHit hit = {0};
        if (event->type == SDL_KEYDOWN) {
            if (event->key.keysym.sym == SDLK_ESCAPE) hit.action = TS_SISTER_UI_ACTION_FX_REMOVE_CANCEL;
            if (!event->key.repeat && (event->key.keysym.sym == SDLK_RETURN || event->key.keysym.sym == SDLK_KP_ENTER))
                hit.action = TS_SISTER_UI_ACTION_FX_REMOVE_CONFIRM;
        } else if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {
            int x,y;
            if (sister_event_mouse(sister->window,event->button.x,event->button.y,&x,&y))
                hit = ts_sister_ui_hit_test_model(&sister->model,x,y);
        }
        if (hit.action) sister_pedalboard_action(device,audio,ui,sister,hit);
        /* Let note releases and window events through, even while confirming. */
        return event->type == SDL_KEYDOWN || event->type == SDL_TEXTINPUT ||
               event->type == SDL_MOUSEBUTTONDOWN || event->type == SDL_MOUSEWHEEL;
    }
    if (event->type == SDL_KEYDOWN &&
        !(event->key.keysym.mod & (KMOD_CTRL|KMOD_ALT|KMOD_GUI)) &&
        (event->key.keysym.sym == SDLK_UP || event->key.keysym.sym == SDLK_DOWN)) {
        sister_fx_scroll_to(sister,ts_sister_ui_fx_scroll(&sister->model) +
                            (event->key.keysym.sym == SDLK_UP ? -1 : 1));
        return 1;
    }
    if (event->type == SDL_MOUSEBUTTONUP && event->button.button == SDL_BUTTON_LEFT) {
        int used = sister->fx_scroll_drag || sister->fx_scroll_repeat;
        sister_fx_scroll_stop(sister); return used;
    }
    if (event->type == SDL_MOUSEMOTION && sister->fx_scroll_drag) {
        if (!(event->motion.state & SDL_BUTTON_LMASK)) { sister_fx_scroll_stop(sister); return 1; }
        int x,y,top,height;
        if (sister_event_mouse(sister->window,event->motion.x,event->motion.y,&x,&y)) {
            ts_sister_ui_fx_thumb(&sister->model,&top,&height);
            int travel = TS_SISTER_UI_FX_SCROLL_H-height;
            int range = sister->model.parameters.fx.slot_count-TS_SISTER_UI_FX_VISIBLE;
            if (travel > 0) sister_fx_scroll_to(sister,(int)lrintf(
                (float)(y-sister->fx_scroll_grab-TS_SISTER_UI_FX_SCROLL_TOP)*range/travel));
        }
        return 1;
    }
    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {
        int x,y;
        if (!sister_event_mouse(sister->window,event->button.x,event->button.y,&x,&y)) return 0;
        TsSisterUiHit hit = ts_sister_ui_hit_test_model(&sister->model,x,y);
        if (hit.action == TS_SISTER_UI_ACTION_FX_SCROLL) {
            sister_pedalboard_action(device,audio,ui,sister,hit);
            sister->fx_scroll_repeat = hit.index;
            sister->fx_scroll_next = SDL_GetTicks()+350;
            return 1;
        }
        if (hit.action == TS_SISTER_UI_ACTION_FX_SCROLL_TRACK) {
            int top,height;
            ts_sister_ui_fx_thumb(&sister->model,&top,&height);
            if (y >= top && y < top+height) {
                sister->fx_scroll_drag = 1; sister->fx_scroll_grab = y-top;
                ts_ui_pointer_drag_cancel(&sister->parameter_drag);
            } else sister_fx_scroll_to(sister,ts_sister_ui_fx_scroll(&sister->model) +
                                        (y < top ? -TS_SISTER_UI_FX_VISIBLE : TS_SISTER_UI_FX_VISIBLE));
            return 1;
        }
        if (hit.action == TS_SISTER_UI_ACTION_FX_ADD || hit.action == TS_SISTER_UI_ACTION_FX_REMOVE)
            return sister_pedalboard_action(device,audio,ui,sister,hit);
    }
    /* Wheel events continue through the existing parameter handler unchanged. */
    return 0;
}

static void sister_pedalboard_tick(SisterWindow *sister, Uint32 now)
{
    if (!sister->fx_scroll_repeat) return;
    if (!sister->window || sister->model.fx_page != 1 || sister->model.preset_manage_open ||
        !(SDL_GetMouseState(NULL,NULL) & SDL_BUTTON_LMASK)) { sister_fx_scroll_stop(sister); return; }
    if ((Sint32)(now-sister->fx_scroll_next) >= 0) {
        sister_fx_scroll_to(sister,ts_sister_ui_fx_scroll(&sister->model)+sister->fx_scroll_repeat);
        sister->fx_scroll_next = now+90;
    }
}
