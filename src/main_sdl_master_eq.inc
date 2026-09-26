static void master_eq_commit(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui)
{
    ts_master_eq_sanitize(&ui->config.master_eq);
    if(device)SDL_LockAudioDevice(device);
    ts_master_eq_set(&audio->sister.master_eq,&ui->config.master_eq);
    if(device)SDL_UnlockAudioDevice(device);
}
static int master_eq_midi_band(const char *target,int *band,int *control)
{
    char trailing;
    return sscanf(target,"main.eq.band.%d.%d%c",band,control,&trailing)==2 &&
        *band>=1 && *band<=TS_MASTER_EQ_BANDS && *control>=0 && *control<=2;
}
static void master_eq_open(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui)
{
    if(device)SDL_LockAudioDevice(device);
    ui->config.master_eq=audio->sister.master_eq.controls;
    if(device)SDL_UnlockAudioDevice(device);
    ui->master_eq_rate=audio->output_rate>0?(unsigned)audio->output_rate:48000;
    ui->master_eq_open=1;ui->master_eq_drag=0;ui->master_eq_reset_pending=0;
    snprintf(ui->status,sizeof(ui->status),"MASTER EQ: ROOM / SYSTEM CORRECTION - LIMITER FOLLOWS EQ");
}
static int master_eq_event(const SDL_Event *event,SDL_Window *window,
    SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,SisterWindow *sister)
{
    Uint32 id=event->type==SDL_KEYDOWN || event->type==SDL_KEYUP?event->key.windowID:
        event->type==SDL_MOUSEWHEEL?event->wheel.windowID:
        event->type==SDL_MOUSEMOTION?event->motion.windowID:
        event->type==SDL_WINDOWEVENT?event->window.windowID:event->button.windowID;
    if(event->type==SDL_WINDOWEVENT && event->window.event==SDL_WINDOWEVENT_FOCUS_LOST) {
        ui->master_eq_drag=0;return 0;
    }
    int x=-1,y=-1,button=0;
    double wheel=0;
    if(event->type==SDL_MOUSEBUTTONDOWN || event->type==SDL_MOUSEBUTTONUP) {
        if(id==SDL_GetWindowID(window))logical_mouse(window,event->button.x,event->button.y,&x,&y);
        else if(sister->window && id==sister->window_id && event->type==SDL_MOUSEBUTTONDOWN &&
                event->button.button==SDL_BUTTON_LEFT && !ui->midi_learn_active &&
                !ui_blocking_dialog_open_except_fm(ui) && !ui->fm_bank_choice_open &&
                !ui->fm_full_choice_open && sister_event_mouse(sister->window,event->button.x,event->button.y,&x,&y)) {
            TsSisterUiHit hit=ts_sister_ui_hit_test_model(&sister->model,x,y);
            if(hit.action==TS_SISTER_UI_ACTION_LIMITER_TOGGLE) {
                master_eq_open(device,audio,ui);application_window_focus(window,sister);return 1;
            }
        }
        button=event->button.button;
    }
    if(id!=SDL_GetWindowID(window))return 0;
    if(!ui->master_eq_open) {
        if(event->type==SDL_MOUSEBUTTONDOWN && button==SDL_BUTTON_LEFT &&
           ts_ui_master_limiter_contains(x,y) && !ui->midi_learn_active &&
           !ui_blocking_dialog_open_except_fm(ui) && !ui->fm_bank_choice_open && !ui->fm_full_choice_open) {
            master_eq_open(device,audio,ui);return 1;
        }
        return 0;
    }
    /* Real confirmations/browsers keep ownership, including keyboard entry. */
    if(ui_blocking_dialog_open_except_workspaces(ui) || ui->portal.open ||
       ui->fm_bank_choice_open || ui->fm_full_choice_open) {
        ui->master_eq_open=0;ui->master_eq_drag=0;return 0;
    }
    if(event->type==SDL_KEYUP)return 0; /* Never swallow release of a manual note. */
    if(event->type==SDL_KEYDOWN) {
        SDL_Keycode key=event->key.keysym.sym;
        if(ui->midi_learn_active)return 0;
        if(key==SDLK_ESCAPE){ui->master_eq_open=0;ui->master_eq_drag=0;return 1;}
        if(key==SDLK_TAB || key==SDLK_BACKQUOTE){ui->master_eq_open=0;return 0;}
        if(key==SDLK_SPACE){if(!event->key.repeat)stop_all(device,audio,ui);return 1;}
        if(event->key.keysym.mod&(KMOD_CTRL|KMOD_ALT|KMOD_GUI))return 0;
        if(note_for_key(key)>=0 || (key>=SDLK_F1 && key<=SDLK_F8))return 0;
        return 1;
    }
    if(event->type==SDL_MOUSEBUTTONUP) {
        ui->master_eq_drag=0;
        /* Deliver any note release that began before this panel opened. */
        if(ui->mouse_note>=0)return 0;
        return 1;
    }
    if(event->type==SDL_MOUSEMOTION) {
        if(!(event->motion.state&SDL_BUTTON_LMASK))ui->master_eq_drag=0;
        logical_mouse(window,event->motion.x,event->motion.y,&x,&y);
    } else if(event->type==SDL_MOUSEWHEEL) {
        int rx,ry;wheel_event_mouse(event,&rx,&ry);logical_mouse(window,rx,ry,&x,&y);
        wheel=wheel_event_delta(event,0);
    } else if(event->type!=SDL_MOUSEBUTTONDOWN)return 0;
    if(event->type==SDL_MOUSEBUTTONDOWN && y<32) {
        if(ts_ui_master_limiter_contains(x,y)){ui->master_eq_open=0;return 1;}
        ui->master_eq_open=0;return 0;
    }
    if(y>=382 && !ui->master_eq_drag)return 0; /* Global file recorder remains visible. */
    int node=ts_ui_master_eq_node(ui,x,y),control=ts_ui_master_eq_control(x,y);
    int selected=-1;
    if(event->type==SDL_MOUSEBUTTONDOWN && y>=252 && y<276 && x>=20 && x<448 &&
       (x-20)%88<76)selected=(x-20)/88;
    if(selected>=0 && selected<TS_MASTER_EQ_BANDS) {
        ui->master_eq_reset_pending=0;
        if(ui->midi_learn_active)ui->master_eq_band=selected;
        else if(button==SDL_BUTTON_LEFT) {
            TsMasterEqControls *eq=&ui->config.master_eq;
            if(SDL_GetModState()&KMOD_CTRL)eq->solo_band=eq->solo_band==selected+1?0:selected+1;
            else ts_master_eq_toggle_band(eq,selected);
            master_eq_commit(device,audio,ui);
        }
        return 1;
    }
    if(ui->midi_learn_active) {
        if(event->type==SDL_MOUSEBUTTONDOWN) {
            if(node>=0){ui->master_eq_band=node;return 1;}
            char target[TS_MIDI_TARGET_ID_MAX]={0};
            ts_ui_master_eq_midi_target(ui,x,y,target,sizeof(target));midi_learn_click(ui,sister,target);
        }
        return 1;
    }
    TsEqBand *b=&ui->config.master_eq.band[ui->master_eq_band];
    unsigned rate=ui->master_eq_rate;int changed=0;
    int fine=(SDL_GetModState()&KMOD_SHIFT)!=0;
    if(event->type==SDL_MOUSEBUTTONDOWN) {
        if(y>=44 && y<68) {
            if(x>=596){ui->master_eq_open=0;ui->master_eq_drag=0;return 1;}
            if(x>=484 && x<580) {
                if(ui->master_eq_reset_pending){ts_master_eq_default(&ui->config.master_eq);changed=1;ui->master_eq_reset_pending=0;}
                else ui->master_eq_reset_pending=1;
                if(changed)master_eq_commit(device,audio,ui);
                return 1;
            }
            if(x>=396 && x<474){ui->config.master_eq.enabled=!ui->config.master_eq.enabled;changed=1;}
        }
        ui->master_eq_reset_pending=0;
        if(node>=0) {
            ui->master_eq_band=node;b=&ui->config.master_eq.band[node];
            if(button==SDL_BUTTON_RIGHT){b->gain_db=0;changed=1;}
            else if(button==SDL_BUTTON_MIDDLE){ts_master_eq_toggle_band(&ui->config.master_eq,node);changed=1;}
            else if(button==SDL_BUTTON_LEFT)ui->master_eq_drag=4;
        } else if(y>=278 && y<299) {
            if(x>=90 && x<202){b->type=(b->type+(button==SDL_BUTTON_RIGHT?TS_EQ_TYPE_COUNT-1:1))%TS_EQ_TYPE_COUNT;changed=1;}
        } else if(control>=0) {
            if(control!=1 || ts_master_eq_has_gain(b->type)) {
                if(button==SDL_BUTTON_RIGHT) {
                    TsMasterEqControls flat;ts_master_eq_default(&flat);
                    float n=ts_master_eq_normalized(&flat.band[ui->master_eq_band],control,rate);
                    ts_master_eq_set_normalized(b,control,n,rate);changed=1;
                } else if(button==SDL_BUTTON_LEFT)ui->master_eq_drag=control+1;
            }
        } else if(y>=337 && y<367) {
            if(x>=20 && x<124)set_output_limiter_enabled(device,audio,ui,sister,!audio->sister.limiter.enabled);
            else if(x>=328 && x<510)ui->master_eq_drag=5;
        }
    }
    if(wheel) {
        ui->master_eq_reset_pending=0;
        if(node>=0){ui->master_eq_band=node;b=&ui->config.master_eq.band[node];control=2;}
        if(control>=0 && (control!=1 || ts_master_eq_has_gain(b->type))) {
            if(control==0)b->frequency*=powf(2,wheel*(fine?.01f:.05f));
            else if(control==1)b->gain_db+=wheel*(fine?.1f:.5f);
            else b->q*=powf(2,wheel*(fine?.02f:.1f));
            b->frequency=fminf(b->frequency,ts_master_eq_max_hz(rate));changed=1;
        }
    }
    if(ui->master_eq_drag && (event->type==SDL_MOUSEMOTION || event->type==SDL_MOUSEBUTTONDOWN)) {
        int d=ui->master_eq_drag;
        if(d==4) {
            if(event->type==SDL_MOUSEMOTION) {
                /* Convert relative motion to logical pixels for high-DPI windows. */
                int ww,wh;SDL_GetWindowSize(window,&ww,&wh);
                float frequency=ts_master_eq_normalized(b,0,rate)+(float)event->motion.xrel*TS_UI_WIDTH/fmaxf(1,ww)/552*(fine?.15f:1);
                ts_master_eq_set_normalized(b,0,frequency,rate);
                if(ts_master_eq_has_gain(b->type))b->gain_db-=event->motion.yrel*TS_UI_HEIGHT/fmaxf(1,wh)/4.5f*(fine?.15f:1);
                changed=1;
            }
        } else if(d==5)set_master_output_gain(device,audio,ui,sister,(x-328)/182.f);
        else {ts_master_eq_set_normalized(b,d-1,(x-(20+(d-1)*204))/188.f,rate);changed=1;}
    }
    if(changed)master_eq_commit(device,audio,ui);
    return 1;
}
