/* The shared FX page is a view. Only explicit edits publish to a stable board
   ID; selecting P1/P2 never changes the shared Sister parameter bank. */
static uint32_t board_requested;
static int board_request_pending;
static MatrixSnapshot *board_before;
static void sister_board_open(uint32_t id)
{ board_requested=id;board_request_pending=1; }
static int sister_board_slot(const TsMatrixControls *c,uint32_t id)
{
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)
        if(c->effect[i].id==id && c->effect[i].type==TS_MATRIX_FX_PEDALBOARD)return i;
    return -1;
}
static void sister_board_history_finish(SDL_AudioDeviceID device,AudioState *audio,
    TsSamplePages *pages,TsInstrument *bank)
{
    if(!board_before)return;
    MatrixSnapshot *after=mx_snapshot(device,audio,pages,bank);
    if(after && !mx_equal(board_before,after))mx_push(board_before);else free(board_before);
    free(after);board_before=NULL;
}
static void sister_board_sync(AudioState *audio,TsUiState *ui,SisterWindow *s)
{
    uint64_t epoch=ui->mosaic?ui->mosaic->epoch:0;
    if(s->board_context!=ui->mosaic || s->board_epoch!=epoch) {
        s->board_id=0;s->model.fx_scroll=0;s->model.fx_remove_confirmation=0;
        s->board_context=ui->mosaic;s->board_epoch=epoch;
        free(board_before);board_before=NULL;
    }
    int count=1;uint32_t ids[TS_MATRIX_EFFECT_CAPACITY+1]={0};
    const TsMatrixControls *c=&audio->sister.matrix.controls;
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)if(c->effect[i].id && c->effect[i].type==TS_MATRIX_FX_PEDALBOARD) {
        int at=count++;while(at>1 && ids[at-1]>c->effect[i].id){ids[at]=ids[at-1];--at;}ids[at]=c->effect[i].id;
    }
    int changed=memcmp(s->board_ids,ids,sizeof(ids))!=0;
    memcpy(s->board_ids,ids,sizeof(ids));s->model.board_count=count;
    int show_requested=board_request_pending;
    if(board_request_pending) {
        s->board_id=board_requested;board_request_pending=0;s->model.fx_page=1;
        s->model.fx_scroll=0;s->model.fx_remove_confirmation=0;changed=1;
        s->model.preset_manage_open=0;character_filter_hide();
    }
    int slot=sister_board_slot(c,s->board_id);
    if(slot<0 && s->board_id){s->board_id=0;s->model.fx_scroll=0;s->model.fx_remove_confirmation=0;changed=1;}
    s->model.board_selected=0;
    if(s->board_id) {
        for(int i=1;i<count;++i)if(ids[i]==s->board_id)s->model.board_selected=i;
        changed|=memcmp(&s->board_controls,&c->effect[slot],sizeof(s->board_controls))!=0;
        s->board_controls=c->effect[slot];
    }
    if(changed) {
        s->model.board_tab_first=(s->model.board_selected/8)*8;
        s->rendered_model_valid=0;
    }
    s->model.board_tab_first=SDL_clamp(s->model.board_tab_first,0,((count-1)/8)*8);
    if(show_requested && sister_window_ensure(s,&ui->config))sister_window_show(s,&ui->palette);
}
static int sister_board_publish(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,SisterWindow *s)
{
    int slot=sister_board_slot(&audio->sister.matrix.controls,s->board_id);
    if(slot<0)return 0;
    matrix_window.controls=audio->sister.matrix.controls;
    TsMatrixEffectControls *c=&matrix_window.controls.effect[slot];
    c->board.count=s->model.parameters.fx.slot_count;
    memcpy(c->board.slot,s->model.parameters.fx.slot,sizeof(c->board.slot));
    c->board.transition=s->model.parameters.fx.transition;
    c->enabled=s->model.parameters.fx.enabled;c->transition=s->model.parameters.fx.master_transition;
    TsMatrixEffectControls requested=*c;
    mx_commit_controls(device,audio,ui);
    int ok=!memcmp(&requested,&audio->sister.matrix.controls.effect[slot],sizeof(*c));
    if(!ok)snprintf(s->model.status,sizeof(s->model.status),"%.127s",matrix_window.status);
    /* Commit may roll controls back on failed preparation. Always display the
       committed bank; keep that failure visible without touching P1. */
    s->board_controls=audio->sister.matrix.controls.effect[slot];
    s->rendered_model_valid=0;if(ok)ui->project_modified=1;
    return ok;
}
static int sister_board_event(const SDL_Event *e,SDL_AudioDeviceID device,AudioState *audio,
    TsUiState *ui,SisterWindow *s,TsSamplePages *pages,TsInstrument *bank)
{
    if(!s->window || event_window_id(e)!=s->window_id)return 0;
    sister_board_sync(audio,ui,s);
    if(e->type==SDL_WINDOWEVENT || e->type==SDL_MOUSEBUTTONUP) {
        sister_board_history_finish(device,audio,pages,bank);
        if(e->type==SDL_MOUSEBUTTONUP){ts_ui_pointer_drag_cancel(&s->parameter_drag);s->grain_time_drag=s->fx_scroll_drag=0;}
        return 0;
    }
    if(s->model.fx_page!=1 || s->model.preset_manage_open)return 0;
    if(e->type==SDL_KEYDOWN && (e->key.keysym.mod&KMOD_CTRL) && e->key.keysym.sym==SDLK_z && s->board_id) {
        sister_board_history_finish(device,audio,pages,bank);mx_bind_mosaic(ui,pages);
        mx_undo(device,audio,ui,pages,bank,(e->key.keysym.mod&KMOD_SHIFT)!=0);
        sister_board_sync(audio,ui,s);s->rendered_model_valid=0;return 1;
    }
    if(e->type==SDL_KEYDOWN && s->board_id && s->model.fx_remove_confirmation) {
        if(e->key.keysym.sym==SDLK_ESCAPE){s->model.fx_remove_confirmation=0;s->rendered_model_valid=0;return 1;}
        if(!e->key.repeat && (e->key.keysym.sym==SDLK_RETURN || e->key.keysym.sym==SDLK_KP_ENTER)) {
            mx_bind_mosaic(ui,pages);mx_original(device,audio,pages,bank);
            if(!board_before)board_before=mx_snapshot(device,audio,pages,bank);
            if(board_before) {
                TsSisterParameters shared=s->model.parameters;
                s->model.parameters=sister_board_view(s)->parameters;
                TsSisterFxControls *fx=&s->model.parameters.fx;
                if(fx->slot_count>TS_SISTER_FX_SLOT_MIN) {
                    fx->slot[--fx->slot_count]=ts_sister_fx_empty_slot();
                    sister_board_publish(device,audio,ui,s);
                }
                s->model.parameters=shared;s->model.fx_remove_confirmation=0;
                sister_board_history_finish(device,audio,pages,bank);
            }
        }
        return 1;
    }
    if(e->type==SDL_KEYDOWN && s->board_id &&
        !(e->key.keysym.mod&(KMOD_CTRL|KMOD_ALT|KMOD_GUI)) &&
        (e->key.keysym.sym==SDLK_UP || e->key.keysym.sym==SDLK_DOWN)) {
        s->model.fx_scroll=SDL_clamp(s->model.fx_scroll+(e->key.keysym.sym==SDLK_UP?-1:1),
            0,s->board_controls.board.count-TS_SISTER_UI_FX_VISIBLE);
        ts_ui_pointer_drag_cancel(&s->parameter_drag);s->rendered_model_valid=0;return 1;
    }
    int down=e->type==SDL_MOUSEBUTTONDOWN,motion=e->type==SDL_MOUSEMOTION,wheel=e->type==SDL_MOUSEWHEEL;
    if(!down && !motion && !wheel)return 0;
    int rawx,rawy,x,y;
    if(down){rawx=e->button.x;rawy=e->button.y;}
    else if(motion){rawx=e->motion.x;rawy=e->motion.y;}
    else wheel_event_mouse(e,&rawx,&rawy);
    if(!sister_event_mouse(s->window,rawx,rawy,&x,&y))return 0;
    const TsSisterUiModel *view=sister_board_view(s);
    TsSisterUiHit hit=ts_sister_ui_hit_test_model(view,x,y);
    if(hit.action==TS_SISTER_UI_ACTION_BOARD_SELECT || hit.action==TS_SISTER_UI_ACTION_BOARD_TABS) {
        if(down && e->button.button==SDL_BUTTON_LEFT) {
            sister_board_history_finish(device,audio,pages,bank);
            ts_ui_pointer_drag_cancel(&s->parameter_drag);s->grain_time_drag=s->fx_scroll_drag=0;
            if(hit.action==TS_SISTER_UI_ACTION_BOARD_SELECT && hit.index>=0 && hit.index<s->model.board_count) {
                s->board_id=s->board_ids[hit.index];s->model.board_selected=hit.index;
                s->model.fx_scroll=s->model.fx_remove_confirmation=0;character_filter_hide();
                snprintf(s->model.status,sizeof(s->model.status),"P%d / %s",hit.index+1,hit.index?"INDEPENDENT CANVAS PEDALBOARD":"SHARED SISTER PEDALBOARD");
                sister_board_sync(audio,ui,s);
            } else s->model.board_tab_first=SDL_clamp(s->model.board_tab_first+hit.index,0,((s->model.board_count-1)/8)*8);
            s->rendered_model_valid=0;
        }
        return 1;
    }
    if(!s->board_id)return 0;
    if(s->model.fx_remove_confirmation && hit.action!=TS_SISTER_UI_ACTION_FX_REMOVE_CONFIRM &&
        hit.action!=TS_SISTER_UI_ACTION_FX_REMOVE_CANCEL)return 1;
    /* Header and capture controls remain global. Presets on this first slice
       are project-owned; never recall a Sister preset into an independent board. */
    if(y<36 || hit.action==TS_SISTER_UI_ACTION_RECORD_FILE)return 0;
    if(y>=370 && x>=230 && x<430) {
        snprintf(s->model.status,sizeof(s->model.status),"CANVAS PEDALBOARDS SAVE WITH THE PROJECT / CTRL+Z UNDO");return 1;
    }
    if(y>=370)return 0;
    if(ui->midi_learn_active)return 1;
    mx_bind_mosaic(ui,pages);
    TsSisterParameters shared=s->model.parameters;
    s->model.parameters=view->parameters;
    int handled=1,changed=0;
    TsSisterFxControls *fx=&s->model.parameters.fx;
    if(!board_before && (down || wheel)){mx_original(device,audio,pages,bank);board_before=mx_snapshot(device,audio,pages,bank);}
    if(!board_before && (down || wheel)){snprintf(s->model.status,sizeof(s->model.status),"UNCHANGED: CANNOT ALLOCATE UNDO SNAPSHOT");goto finish;}
    if(sister_grain_event(device,audio,ui,s,e))goto finish;
    if(motion && s->fx_scroll_drag) {
        if(!(e->motion.state&SDL_BUTTON_LMASK))s->fx_scroll_drag=0;
        else {
            int top,height;ts_sister_ui_fx_thumb(&s->model,&top,&height);
            int travel=TS_SISTER_UI_FX_SCROLL_H-height;
            if(travel>0)ts_sister_ui_fx_scroll_set(&s->model,(int)lrintf(
                (float)(y-s->fx_scroll_grab-TS_SISTER_UI_FX_SCROLL_TOP)*
                (fx->slot_count-TS_SISTER_UI_FX_VISIBLE)/travel));
            s->rendered_model_valid=0;
        }
    } else if(hit.action==TS_SISTER_UI_ACTION_FX_SCROLL || hit.action==TS_SISTER_UI_ACTION_FX_SCROLL_TRACK) {
        int first=ts_sister_ui_fx_scroll(&s->model);
        if(wheel)first-=3*wheel_event_delta(e,0);
        else if(down && e->button.button==SDL_BUTTON_LEFT) {
            if(hit.action==TS_SISTER_UI_ACTION_FX_SCROLL)first+=hit.index;
            else {
                int top,height;ts_sister_ui_fx_thumb(&s->model,&top,&height);
                if(y>=top && y<top+height){s->fx_scroll_drag=1;s->fx_scroll_grab=y-top;}
                else first+=y<top?-TS_SISTER_UI_FX_VISIBLE:TS_SISTER_UI_FX_VISIBLE;
            }
        }
        ts_sister_ui_fx_scroll_set(&s->model,first);s->rendered_model_valid=0;
    } else if(hit.action==TS_SISTER_UI_ACTION_PARAMETER) {
        int offset=ts_sister_ui_slot_offset(hit.index);
        if(offset<0 && hit.index!=TS_SISTER_UI_PARAM_FX_TRANSITION && hit.index!=TS_SISTER_UI_PARAM_MASTER_FX_TRANSITION)goto finish;
        if(offset>=0 && offset>=fx->slot_count*5)goto finish;
        if(offset>=0 && (fx->slot[offset/5].parameter_locks&(1u<<(offset%5))) && !(down && (SDL_GetModState()&KMOD_SHIFT)))goto finish;
        if(down && e->button.button==SDL_BUTTON_LEFT && (SDL_GetModState()&KMOD_SHIFT) && offset>=0) {
            fx->slot[offset/5].parameter_locks^=1u<<(offset%5);changed=1;
        } else if(down || wheel || ts_ui_pointer_drag_accept_motion(&s->parameter_drag,hit.index,e->motion.state)) {
            float v=wheel?sister_parameter_wheel_normalized(&s->model.parameters,hit.index,wheel_event_delta(e,0),(SDL_GetModState()&KMOD_SHIFT)!=0):hit.normalized;
            sister_set_parameter(&s->model.parameters,hit.index,v);changed=1;
            if(down)ts_ui_pointer_drag_begin(&s->parameter_drag,hit.index,e->button.button==SDL_BUTTON_LEFT?SDL_BUTTON_LMASK:SDL_BUTTON_RMASK);
        }
    } else if(down && e->button.button==SDL_BUTTON_LEFT) {
        int row=hit.index;
        switch(hit.action) {
        case TS_SISTER_UI_ACTION_FX_ADD:
            if(fx->slot_count<20){fx->slot[fx->slot_count++]=ts_sister_fx_empty_slot();ts_sister_ui_fx_scroll_set(&s->model,fx->slot_count-5);changed=1;}break;
        case TS_SISTER_UI_ACTION_FX_REMOVE:
            if(fx->slot_count>5) {
                if(fx->slot[fx->slot_count-1].type)s->model.fx_remove_confirmation=1;
                else {fx->slot[--fx->slot_count]=ts_sister_fx_empty_slot();changed=1;}
            }break;
        case TS_SISTER_UI_ACTION_FX_REMOVE_CONFIRM:
            if(s->model.fx_remove_confirmation && fx->slot_count>5){fx->slot[--fx->slot_count]=ts_sister_fx_empty_slot();changed=1;}
            s->model.fx_remove_confirmation=0;break;
        case TS_SISTER_UI_ACTION_FX_REMOVE_CANCEL:s->model.fx_remove_confirmation=0;break;
        case TS_SISTER_UI_ACTION_FX_SLOT_TYPE:
            if(row>=0 && row<fx->slot_count){sister_fx_slot_select_type(&fx->slot[row],(fx->slot[row].type+1)%TS_SISTER_FX_TYPE_COUNT);character_filter_hide();changed=1;}break;
        case TS_SISTER_UI_ACTION_FX_SLOT_TOGGLE:
            if(row>=0 && row<fx->slot_count){fx->slot[row].enabled=!fx->slot[row].enabled;changed=1;}break;
        case TS_SISTER_UI_ACTION_FX_TOGGLE:fx->enabled=!fx->enabled;changed=1;break;
        case TS_SISTER_UI_ACTION_FX_SLOT_MOVE: {
            int at=row>>8,other=at+((row&255)?1:-1);
            if(at>=0 && at<fx->slot_count && other>=0 && other<fx->slot_count){
                TsSisterFxSlotControls t=fx->slot[at];fx->slot[at]=fx->slot[other];fx->slot[other]=t;changed=1;character_filter_hide();
                if(other<s->model.fx_scroll)ts_sister_ui_fx_scroll_set(&s->model,other);
                if(other>=s->model.fx_scroll+5)ts_sister_ui_fx_scroll_set(&s->model,other-4);
            }break;}
        case TS_SISTER_UI_ACTION_FILTER_EDIT:
            character_filter_matrix_open(s->board_id);character_filter_window.source=2;character_filter_window.slot=row;break;
        default:break;
        }
        s->rendered_model_valid=0;
    }
    if(changed)sister_board_publish(device,audio,ui,s);
finish:
    s->model.parameters=shared;
    if(!motion && (!down || hit.action!=TS_SISTER_UI_ACTION_PARAMETER))sister_board_history_finish(device,audio,pages,bank);
    return handled;
}
