static void sister_grain_publish(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,SisterWindow *sister)
{
    if(device)SDL_LockAudioDevice(device);
    ts_sister_runtime_set_parameters(&audio->sister,&sister->model.parameters);
    ts_sister_runtime_mark_selected_preset_modified(&audio->sister);
    if(device)SDL_UnlockAudioDevice(device);
    ui->project_modified=1;sister->rendered_model_valid=0;
}
static int sister_grain_event(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,
    SisterWindow *sister,const SDL_Event *event)
{
    int x=0,y=0,down=event->type==SDL_MOUSEBUTTONDOWN,motion=event->type==SDL_MOUSEMOTION;
    if(event->type==SDL_MOUSEBUTTONUP && event->button.button==SDL_BUTTON_LEFT && sister->grain_time_drag) {
        sister->grain_time_drag=0;return 1;
    }
    if(!down && !motion && event->type!=SDL_MOUSEWHEEL)return 0;
    int mx,my;
    if(down){mx=event->button.x;my=event->button.y;}
    else if(motion){mx=event->motion.x;my=event->motion.y;}
    else wheel_event_mouse(event,&mx,&my);
    if(!sister_event_mouse(sister->window,mx,my,&x,&y))return 0;
    TsSisterUiHit hit=ts_sister_ui_hit_test_model(&sister->model,x,y);
    if(motion && sister->grain_time_drag) {
        if(!(event->motion.state&SDL_BUTTON_LMASK)){sister->grain_time_drag=0;return 1;}
        hit.action=TS_SISTER_UI_ACTION_GRAIN_TIME;hit.index=sister->grain_time_drag-1;
        hit.normalized=fmaxf(0,fminf(1,(x-508)/111.f));
    }
    if(hit.action!=TS_SISTER_UI_ACTION_GRAIN_STATE && hit.action!=TS_SISTER_UI_ACTION_GRAIN_TIME)return 0;
    int row=hit.action==TS_SISTER_UI_ACTION_GRAIN_STATE?hit.index>>8:hit.index;
    if(row<0 || row>=sister->model.parameters.fx.slot_count)return 1;
    TsSisterFxSlotControls *slot=&sister->model.parameters.fx.slot[row];
    if(slot->type!=TS_SISTER_FX_GRAIN)return 1;
    TsGrainStates *bank=&slot->grain_states;
    if(hit.action==TS_SISTER_UI_ACTION_GRAIN_STATE) {
        int state=hit.index&255;if(state>=TS_GRAIN_STATES)return 1;
        if(motion) {
            if(bank->captured&(1u<<state))snprintf(sister->model.status,sizeof(sister->model.status),
                "STATE %d: %.0F MS / %.1F HZ / %+.1F ST / CLICK PLAY / RIGHT EDIT / SHIFT SAVE / MIDDLE CLEAR",state+1,
                ts_sister_grain_size_ms(bank->value[state][0]),ts_sister_grain_density_hz(bank->value[state][1]),ts_sister_grain_pitch_semitones(bank->value[state][2]));
            else snprintf(sister->model.status,sizeof(sister->model.status),"STATE %d EMPTY / CLICK TO CAPTURE SIZE, DENSITY AND PITCH",state+1);
            return 0;
        }
        if(!down)return 1;
        ts_ui_pointer_drag_cancel(&sister->parameter_drag);
        if(event->button.button==SDL_BUTTON_MIDDLE) {
            bank->captured&=(uint8_t)~(1u<<state);
            if(bank->selected==state+1)bank->selected=bank->editing=0;
        } else if(event->button.button==SDL_BUTTON_RIGHT)ts_grain_state_recall(slot,state,1);
        else if(event->button.button==SDL_BUTTON_LEFT) {
            if((SDL_GetModState()&KMOD_SHIFT) || !(bank->captured&(1u<<state)))ts_grain_state_capture(slot,state);
            else ts_grain_state_recall(slot,state,0);
        } else return 1;
        snprintf(sister->model.status,sizeof(sister->model.status),"GRAIN STATE %d / RIGHT-CLICK EDIT / SHIFT-CLICK SAVE / MIDDLE-CLICK CLEAR",state+1);
    } else {
        if(motion && !sister->grain_time_drag) {
            snprintf(sister->model.status,sizeof(sister->model.status),"GRAIN GLIDE %.2F S / DRAG OR WHEEL / RIGHT-CLICK INSTANT / MAX 10 MIN",bank->seconds);return 0;
        }
        if(event->type==SDL_MOUSEWHEEL)bank->seconds=fmaxf(0,fminf(600,bank->seconds+(float)wheel_event_delta(event,0)*(SDL_GetModState()&KMOD_SHIFT?.1f:1.f)));
        else if(down && event->button.button==SDL_BUTTON_RIGHT)bank->seconds=0;
        else if(motion || (down && event->button.button==SDL_BUTTON_LEFT)) {
            bank->seconds=(powf(6001.f,fmaxf(0,fminf(1,hit.normalized)))-1)*.1f;
            if(down){sister->grain_time_drag=row+1;ts_ui_pointer_drag_cancel(&sister->parameter_drag);}
        } else return 1;
        snprintf(sister->model.status,sizeof(sister->model.status),"GRAIN GLIDE %.2F S / CLICK A STATE TO START",bank->seconds);
    }
    sister_grain_publish(device,audio,ui,sister);return 1;
}
