/* Matrix edits use the same bounded control publication as ordinary Prism. */
static int sister_prism_matrix_takeover(SDL_AudioDeviceID device,AudioState *audio,SisterWindow *s)
{
    if(device)SDL_LockAudioDevice(device);
    TsSisterParameters p=audio->sister.parameters;
    int changed=ts_prism_matrix_export(&audio->sister.prism,&p.prism);
    if(changed || p.prism.matrix_run) {
        p.prism.matrix_run=0;ts_sister_runtime_set_parameters(&audio->sister,&p);
        s->model.parameters=audio->sister.parameters;
        s->model.routing.prism=ts_prism_view(&audio->sister.prism);
    }
    if(device)SDL_UnlockAudioDevice(device);
    s->model.prism_matrix_edit=0;
    s->rendered_model_valid=0;return changed;
}
static void sister_prism_matrix_save(SDL_AudioDeviceID device,AudioState *audio,SisterWindow *s)
{
    int state=s->model.prism_matrix_edit-1;if(state<0 || state>=TS_PRISM_STATES)return;
    TsPrismControls c=s->model.parameters.prism;ts_prism_capture(&c,state);
    sister_prism_publish(device,audio,s,&c);
    s->model.prism_matrix_edit=0;s->model.prism_edit_endpoint=0;s->model.prism_panel=3;
    snprintf(s->model.status,sizeof(s->model.status),"SAVED %c / NEXT VISIT USES THIS STATE / CURRENT TRANSITION KEPT",'A'+state);
}
static int sister_prism_matrix_event(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,
                                      SisterWindow *s,const SDL_Event *event)
{
    int x,y,wheel=0;
    if(event->type==SDL_MOUSEWHEEL) {
        int raw_x,raw_y;
#if SDL_VERSION_ATLEAST(2,26,0)
        raw_x=event->wheel.mouseX;raw_y=event->wheel.mouseY;
#else
        SDL_GetMouseState(&raw_x,&raw_y);
#endif
        if(!sister_event_mouse(s->window,raw_x,raw_y,&x,&y))return 0;
        wheel=event->wheel.direction==SDL_MOUSEWHEEL_FLIPPED?-event->wheel.y:event->wheel.y;
    } else if(event->type==SDL_MOUSEBUTTONDOWN) {
        if(!sister_event_mouse(s->window,event->button.x,event->button.y,&x,&y))return 0;
    } else return 0;
    if(s->model.prism_panel!=3) {
        if(s->model.prism_matrix_edit && event->type==SDL_MOUSEBUTTONDOWN && event->button.button==SDL_BUTTON_LEFT &&
           x>=530 && x<616 && y>=250 && y<272) {
            sister_prism_matrix_save(device,audio,s);return 1;
        }
        if(event->type==SDL_MOUSEBUTTONDOWN && event->button.button==SDL_BUTTON_LEFT &&
           x>=534 && x<616 && y>=48 && y<70) {
            s->model.prism_matrix_previous_panel=s->model.prism_panel;s->model.prism_panel=3;
            s->rendered_model_valid=0;return 1;
        }
        return 0;
    }
    if(y<40 || y>=345)return 0;
    /* This existing parameter keeps its MIDI identity and drag behavior. */
    if(x>=480 && x<616 && y>=78 && y<100 && event->type==SDL_MOUSEBUTTONDOWN && event->button.button==SDL_BUTTON_LEFT)return 0;
    int cell=ts_sister_ui_prism_matrix_cell(x,y);
    if(event->type==SDL_MOUSEWHEEL && (!wheel || !ts_ui_wheel_guard_accept(&ui->wheel_guard,
        WHEEL_TARGET_SISTER+0x280+(cell>=0?cell:64),SDL_GetTicks())))return 1;
    int right=event->type==SDL_MOUSEBUTTONDOWN && event->button.button==SDL_BUTTON_RIGHT;
    int middle=event->type==SDL_MOUSEBUTTONDOWN && event->button.button==SDL_BUTTON_MIDDLE;
    int change=wheel?wheel:right?-1:1;
    TsPrismControls c=s->model.parameters.prism;
    if(cell>=0) {
        if(event->type==SDL_MOUSEBUTTONDOWN && event->button.button==SDL_BUTTON_LEFT && (SDL_GetModState()&KMOD_SHIFT))c.matrix.length=cell+1;
        else if(middle)c.matrix.step[cell]=-1;
        else c.matrix.step[cell]=((c.matrix.step[cell]+1+change%13+13)%13)-1;
        snprintf(s->model.status,sizeof(s->model.status),"MATRIX STEP %02d / %s / CHANGES APPLY ON NEXT VISIT",cell+1,
            c.matrix.step[cell]<0?"HOLD":"A-L REFERENCE");
    } else if(y>=48 && y<70 && x>=16 && x<86 && !wheel) {
        s->model.prism_panel=s->model.prism_matrix_previous_panel;s->rendered_model_valid=0;return 1;
    } else if(y>=78 && y<100) {
        if(x>=16 && x<78 && !wheel) {
            c.matrix_run=1;++c.matrix_restart;if(!c.matrix_restart)++c.matrix_restart;
            snprintf(s->model.status,sizeof(s->model.status),"MATRIX PLAY / %02d STEPS / AUDIO CLOCK",c.matrix.length);
        } else if(x>=86 && x<176 && !wheel) {
            sister_prism_matrix_takeover(device,audio,s);memset(s->model.prism_browse,0,sizeof(s->model.prism_browse));
            snprintf(s->model.status,sizeof(s->model.status),"MATRIX STOPPED / SOUND HELD / NEXT PLAY STARTS AT 01");return 1;
        } else if(x>=184 && x<246 && !wheel)c.matrix.loop=!c.matrix.loop;
        else if(x>=254 && x<340)c.matrix.length=SDL_clamp(c.matrix.length+change,1,TS_PRISM_MATRIX_STEPS);
        else if(x>=348 && x<472) {
            if(wheel)c.matrix.step_seconds*=powf((SDL_GetModState()&KMOD_SHIFT)?1.01f:1.1f,(float)wheel);
            else c.matrix.step_seconds=ts_prism_time_from_normalized((x-348)/123.f);
        } else if(x>=480 && x<616 && wheel) {
            if(ts_sister_ui_parameter_locked(&s->model,TS_SISTER_UI_PARAM_PRISM_TIME)) {
                snprintf(s->model.status,sizeof(s->model.status),"MORPH TIME LOCKED / SHIFT-CLICK TO UNLOCK");return 1;
            }
            c.morph_seconds*=powf((SDL_GetModState()&KMOD_SHIFT)?1.01f:1.1f,(float)wheel);
        }
        else return 1;
    } else if(y>=318 && y<340 && x>=16 && x<616 && (x-16)%50<46 && !wheel) {
        int state=(x-16)/50;
        /* The live patch prefix is an editor while Matrix owns its frozen pair.
           It reaches the bank only on SAVE; no audition interrupts playback. */
        int side=c.endpoint[0]==state?0:1;ts_prism_assign_endpoint(&c,side,state);
        int enabled=c.enabled;ts_prism_recall(&c,state);c.enabled=enabled;
        c.morph_enabled=0;c.morph_trigger=0;c.active_pair_valid=0;
        s->model.prism_matrix_edit=state+1;
        s->model.prism_edit_endpoint=state+1;memset(s->model.prism_browse,0,sizeof(s->model.prism_browse));
        s->model.prism_panel=0;
        snprintf(s->model.status,sizeof(s->model.status),"DRAFT %c / MATRIX KEEPS PLAYING / SAVE %c APPLIES ON NEXT VISIT",'A'+state,'A'+state);
    } else return 1;
    sister_prism_publish(device,audio,s,&c);return 1;
}
