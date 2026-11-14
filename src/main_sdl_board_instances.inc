/* The shared FX page is a view. Only explicit edits publish to a stable board
   ID; selecting P1/P2 never changes the shared Sister parameter bank. */
static uint32_t board_requested;
static int board_request_pending,board_preserve_origin;
static MatrixSnapshot *board_before;
static void sister_board_open(uint32_t id)
{ board_requested=id;board_request_pending=1;board_preserve_origin=0; }
static void sister_board_return(uint32_t id)
{ sister_board_open(id);board_preserve_origin=1; }
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
    if(!pedalboard_presets_loaded) {
        char path[TS_RUNTIME_PATH_MAX],error[160];ts_sister_preset_bank_init(&pedalboard_presets,48000);
        if(pedalboard_presets_file_path(path,sizeof(path)))ts_sister_preset_load(&pedalboard_presets,path,48000,error,sizeof(error));
        pedalboard_presets_loaded=1;s->board_preset_index=SIZE_MAX;
    }
    uint32_t previous_board=s->board_id;
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
    if(s->board_id && slot<0){s->board_id=0;s->model.fx_scroll=0;s->model.fx_remove_confirmation=0;changed=1;}
    s->model.board_selected=0;
    if(s->board_id) {
        for(int i=1;i<count;++i)if(ids[i]==s->board_id)s->model.board_selected=i;
        changed|=memcmp(&s->board_controls,&c->effect[slot],sizeof(s->board_controls))!=0;
        s->board_controls=c->effect[slot];
    }
    if(s->board_id!=previous_board){s->board_preset_index=SIZE_MAX;s->board_preset_modified=0;}
    sister_preset_model_sync(s,&audio->sister);
    if(changed) {
        s->model.board_tab_first=(s->model.board_selected/8)*8;
        s->rendered_model_valid=0;
    }
    s->model.board_tab_first=SDL_clamp(s->model.board_tab_first,0,((count-1)/8)*8);
    if(show_requested && sister_window_ensure(s,&ui->config)){Uint32 origin=s->return_window_id;sister_window_show(s,&ui->palette);if(board_preserve_origin)s->return_window_id=origin;}
}
static int sister_board_publish(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,SisterWindow *s)
{
    if(!s->board_id) {
        TsSisterParameters next=audio->sister.parameters;next.fx=s->model.parameters.fx;
        TsSisterSunVoxPlan plan;
        if(!sister_stage_fx(device,audio,&next.fx,&plan)) {
            snprintf(s->model.status,sizeof(s->model.status),"UNCHANGED: CANNOT PREPARE EFFECT");return 0;
        }
        if(device)SDL_LockAudioDevice(device);
        ts_sister_sunvox_commit(&audio->sister.post_fx,&plan);
        ts_sister_runtime_set_parameters(&audio->sister,&next);
        ts_sister_runtime_mark_selected_preset_modified(&audio->sister);
        if(device)SDL_UnlockAudioDevice(device);
        ts_sister_sunvox_discard(&plan);s->model.parameters=audio->sister.parameters;
        matrix_window.fixed.parameters=audio->sister.parameters;
        s->rendered_model_valid=0;ui->project_modified=1;return 1;
    }
    int slot=sister_board_slot(&audio->sister.matrix.controls,s->board_id);
    if(slot<0)return 0;
    matrix_window.controls=audio->sister.matrix.controls;
    TsMatrixEffectControls *c=&matrix_window.controls.effect[slot];
    c->board.count=s->model.parameters.fx.slot_count;c->board.next_id=s->model.parameters.fx.next_slot_id;
    memcpy(c->board.slot,s->model.parameters.fx.slot,sizeof(c->board.slot));
    c->board.transition=s->model.parameters.fx.transition;
    c->enabled=s->model.parameters.fx.enabled;c->transition=s->model.parameters.fx.master_transition;
    ts_pedalboard_identify(&c->board);
    TsMatrixEffectControls requested=*c;
    mx_commit_controls(device,audio,ui);
    int ok=!memcmp(&requested,&audio->sister.matrix.controls.effect[slot],sizeof(*c));
    if(!ok)snprintf(s->model.status,sizeof(s->model.status),"%.127s",matrix_window.status);
    /* Commit may roll controls back on failed preparation. Always display the
       committed bank; keep that failure visible without touching P1. */
    s->board_controls=audio->sister.matrix.controls.effect[slot];
    s->rendered_model_valid=0;if(ok){ui->project_modified=1;s->board_preset_modified=1;}
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
    if(e->type==SDL_KEYDOWN && (e->key.keysym.mod&KMOD_CTRL) && e->key.keysym.sym==SDLK_z) {
        sister_board_history_finish(device,audio,pages,bank);mx_bind_mosaic(ui,pages);
        mx_undo(device,audio,ui,pages,bank,(e->key.keysym.mod&KMOD_SHIFT)!=0);
        sister_board_sync(audio,ui,s);s->rendered_model_valid=0;return 1;
    }
    if(e->type==SDL_KEYDOWN && s->model.fx_remove_confirmation) {
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
                s->model.parameters=s->board_id?shared:audio->sister.parameters;s->model.fx_remove_confirmation=0;
                sister_board_history_finish(device,audio,pages,bank);
            }
        }
        return 1;
    }
    if(e->type==SDL_KEYDOWN &&
        !(e->key.keysym.mod&(KMOD_CTRL|KMOD_ALT|KMOD_GUI)) &&
        (e->key.keysym.sym==SDLK_UP || e->key.keysym.sym==SDLK_DOWN)) {
        s->model.fx_scroll=SDL_clamp(s->model.fx_scroll+(e->key.keysym.sym==SDLK_UP?-1:1),
            0,sister_board_view(s)->parameters.fx.slot_count-TS_SISTER_UI_FX_VISIBLE);
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
                s->board_id=s->board_ids[hit.index];s->board_preset_index=SIZE_MAX;s->board_preset_modified=0;s->model.board_selected=hit.index;
                s->model.fx_scroll=s->model.fx_remove_confirmation=0;character_filter_hide();
                snprintf(s->model.status,sizeof(s->model.status),"P%d / %s",hit.index+1,hit.index?"INDEPENDENT CANVAS PEDALBOARD":"SHARED SISTER PEDALBOARD");
                sister_board_sync(audio,ui,s);
            } else s->model.board_tab_first=SDL_clamp(s->model.board_tab_first+hit.index,0,((s->model.board_count-1)/8)*8);
            s->rendered_model_valid=0;
        }
        return 1;
    }
    if(s->model.fx_remove_confirmation && hit.action!=TS_SISTER_UI_ACTION_FX_REMOVE_CONFIRM &&
        hit.action!=TS_SISTER_UI_ACTION_FX_REMOVE_CANCEL)return 1;
    /* Header and capture controls remain global. Added boards use their
       separate preset bank; P1 retains the Sister preset controls. */
    if(y<36 || hit.action==TS_SISTER_UI_ACTION_RECORD_FILE)return 0;
    if(s->board_id && down && e->button.button==SDL_BUTTON_LEFT &&
        (hit.action==TS_SISTER_UI_ACTION_PRESET_PREVIOUS || hit.action==TS_SISTER_UI_ACTION_PRESET_NEXT)) {
        if(!pedalboard_presets.count)return 1;
        size_t at=s->board_preset_index;
        if(at>=pedalboard_presets.count)at=hit.action==TS_SISTER_UI_ACTION_PRESET_NEXT?0:pedalboard_presets.count-1;
        else at=(at+pedalboard_presets.count+(hit.action==TS_SISTER_UI_ACTION_PRESET_NEXT?1:-1))%pedalboard_presets.count;
        mx_bind_mosaic(ui,pages);mx_original(device,audio,pages,bank);board_before=mx_snapshot(device,audio,pages,bank);
        if(board_before)sister_board_preset_recall(device,audio,ui,s,at);
        sister_board_history_finish(device,audio,pages,bank);return 1;
    }
    if(y>=370)return 0;
    if(ui->midi_learn_active) {
        if(down && e->button.button==SDL_BUTTON_LEFT) {
            int offset=hit.action==TS_SISTER_UI_ACTION_PARAMETER?ts_sister_ui_slot_offset(hit.index):-1;
            int row=offset>=0?offset/5:hit.action==TS_SISTER_UI_ACTION_FX_SLOT_TOGGLE?hit.index:-1;
            char target[80]={0};
            if(row>=0 && row<view->parameters.fx.slot_count)
                snprintf(target,sizeof(target),"board.%s.%u.%u.%d",offset>=0?"param":"toggle",s->board_id,view->parameters.fx.slot[row].id,offset>=0?offset%5:0);
            if(!target[0] && !s->board_id)ts_sister_ui_midi_target(hit,target,sizeof(target));
            midi_learn_click(ui,s,target);
        }return 1;
    }
    mx_bind_mosaic(ui,pages);
    TsSisterParameters shared=s->model.parameters;
    s->model.parameters=view->parameters;
    int handled=1,changed=0,open_sunvox=-1;
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
        if(offset<0 && hit.index!=TS_SISTER_UI_PARAM_FX_TRANSITION && hit.index!=TS_SISTER_UI_PARAM_MASTER_FX_TRANSITION && (s->board_id || hit.index!=TS_SISTER_UI_PARAM_MASTER_FX_FEEDBACK))goto finish;
        if(offset>=0 && offset>=fx->slot_count*5)goto finish;
        if(offset>=0 && ts_sister_ui_parameter_locked(view,hit.index) && !(down && (SDL_GetModState()&KMOD_SHIFT)))goto finish;
        if(down && e->button.button==SDL_BUTTON_LEFT && (SDL_GetModState()&KMOD_SHIFT) && offset>=0) {
            if(!s->board_id && offset<25){if(device)SDL_LockAudioDevice(device);sister_toggle_parameter_lock(audio,s,hit.index);if(device)SDL_UnlockAudioDevice(device);}
            else {fx->slot[offset/5].parameter_locks^=1u<<(offset%5);changed=1;}
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
        case TS_SISTER_UI_ACTION_FX_SLOT_PLACEMENT: {
            int at=row>>8;uint8_t place=row&255;
            if(!s->board_id && at>=0 && at<fx->slot_count && (place&TS_SISTER_FX_PLACE_ALL)) {
                if(place==TS_SISTER_FX_PLACE_PRE || place==TS_SISTER_FX_PLACE_POST)fx->slot[at].placement=place;
                else {uint8_t heads=(fx->slot[at].placement&TS_SISTER_FX_PLACE_HEADS)^place;fx->slot[at].placement=heads?heads:place;}
                changed=1;
            }break;}
        case TS_SISTER_UI_ACTION_SUNVOX_EDIT:open_sunvox=row;break;
        case TS_SISTER_UI_ACTION_FILTER_EDIT:
            if(s->board_id){character_filter_matrix_open(s->board_id);character_filter_window.source=2;character_filter_window.slot=row;character_filter_window.child=fx->slot[row].id;}
            else character_filter_open(s,row);
            break;
        default:break;
        }
        s->rendered_model_valid=0;
    }
    if(changed && !sister_board_publish(device,audio,ui,s))open_sunvox=-1;
    if(open_sunvox>=0 && open_sunvox<fx->slot_count) {
        uint32_t child=fx->slot[open_sunvox].id;
        sister_board_history_finish(device,audio,pages,bank);
        matrix_show(device,audio,ui,pages,bank,-1,-1);svfx_open(s->board_id);
        sunvox_editor.child=child;sunvox_editor.sister=s;
    }
finish:
    s->model.parameters=s->board_id?shared:audio->sister.parameters;
    if(!motion && (!down || hit.action!=TS_SISTER_UI_ACTION_PARAMETER))sister_board_history_finish(device,audio,pages,bank);
    return handled;
}

/* Saved child identities, never display rows or P-tab numbers. Missing targets
   are inert, including after deletion and reuse of an outer matrix port. */
static int sister_board_target(const char *target,uint32_t *board,uint32_t *child,int *field,int *toggle)
{
    char tail;unsigned b,c;int f;
    *toggle=sscanf(target,"board.toggle.%u.%u.%d%c",&b,&c,&f,&tail)==3;
    if(!*toggle && sscanf(target,"board.param.%u.%u.%d%c",&b,&c,&f,&tail)!=3)return 0;
    if(!c || f<0 || !(f<=4 || (f>=100 && f<100+TS_SUNVOX_CONTROLS) || (f>=300 && f<300+TS_CF_COUNT)))return 0;
    *board=b;*child=c;*field=f;return 1;
}
static int sister_board_child(const TsSisterFxSlotControls *slots,int count,uint32_t id)
{for(int i=0;i<count;++i)if(slots[i].id==id)return i;return -1;}
static float sister_board_field_value(const TsSisterFxSlotControls *slot,int field)
{
    if(field>=300)return slot->type==TS_SISTER_FX_FILTER?slot->filter.value[field-300]:0;
    if(field>=100)return slot->type==TS_SISTER_FX_SUNVOX?ts_sunvox_normalized(&slot->sunvox,field-100):0;
    TsSisterParameters p={0};p.fx.slot[0]=*slot;
    return sister_parameter_normalized(&p,TS_SISTER_UI_SLOT_PARAMETER(0,field));
}
static int sister_board_midi_value(const char *target,const TsUiState *ui,const SisterWindow *s,float *value)
{
    uint32_t b,id;int field,toggle;if(!sister_board_target(target,&b,&id,&field,&toggle))return 0;
    *value=0;const TsSisterFxSlotControls *slots=s->model.parameters.fx.slot;int count=s->model.parameters.fx.slot_count;
    if(b){int at=sister_board_slot(&ui->config.matrix,b);if(at<0)return 1;slots=ui->config.matrix.effect[at].board.slot;count=ui->config.matrix.effect[at].board.count;}
    int at=sister_board_child(slots,count,id);if(at>=0)*value=toggle?slots[at].enabled:sister_board_field_value(&slots[at],field);
    return 1;
}
static int sister_board_midi(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,SisterWindow *s,const char *target,float value)
{
    uint32_t b,id;int field,toggle;if(!sister_board_target(target,&b,&id,&field,&toggle))return 0;
    TsSisterParameters next=audio->sister.parameters;int parent=-1;
    TsSisterFxSlotControls *slots=next.fx.slot;int count=next.fx.slot_count;
    if(b){parent=sister_board_slot(&audio->sister.matrix.controls,b);if(parent<0)return 1;
        matrix_window.controls=audio->sister.matrix.controls;slots=matrix_window.controls.effect[parent].board.slot;count=matrix_window.controls.effect[parent].board.count;}
    int at=sister_board_child(slots,count,id);if(at<0)return 1;TsSisterFxSlotControls *slot=&slots[at];
    if(toggle)slot->enabled=!slot->enabled;
    else if(field>=300){if(slot->type!=TS_SISTER_FX_FILTER || (slot->filter.locks&(1u<<(field-300))))return 1;ts_character_filter_set(&slot->filter,field-300,value);}
    else if(field>=100){if(slot->type!=TS_SISTER_FX_SUNVOX)return 1;slot->sunvox.value[field-100]=(int)lrintf(SDL_clamp(value,0,1)*32768);}
    else {if((slot->parameter_locks&(1u<<field)) || (!b && ts_sister_ui_parameter_locked(&s->model,TS_SISTER_UI_SLOT_PARAMETER(at,field))))return 1;TsSisterParameters p={0};p.fx.slot[0]=*slot;sister_set_parameter(&p,TS_SISTER_UI_SLOT_PARAMETER(0,field),value);*slot=p.fx.slot[0];}
    if(b)mx_commit_controls(device,audio,ui);
    else {
        if(device)SDL_LockAudioDevice(device);
        ts_sister_runtime_set_parameters(&audio->sister,&next);ts_sister_runtime_mark_selected_preset_modified(&audio->sister);
        if(device)SDL_UnlockAudioDevice(device);s->model.parameters=audio->sister.parameters;
    }
    ui->project_modified=1;s->rendered_model_valid=0;return 1;
}

static int sister_board_preset_recall(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,SisterWindow *s,size_t index)
{
    if(index>=pedalboard_presets.count || !s->board_id)return 0;
    TsSisterParameters shared=s->model.parameters;TsSisterFxControls old=sister_board_view(s)->parameters.fx;
    s->model.parameters.fx=pedalboard_presets.entries[index].parameters.fx;
    TsSisterFxControls *next=&s->model.parameters.fx;next->next_slot_id=old.next_slot_id;
    for(int i=0;i<TS_SISTER_FX_SLOT_COUNT;++i){next->slot[i].placement=TS_SISTER_FX_PLACE_POST;next->slot[i].id=i<old.slot_count?old.slot[i].id:0;}
    int ok=sister_board_publish(device,audio,ui,s);s->model.parameters=shared;
    if(ok){s->board_preset_index=index;s->board_preset_modified=0;sister_preset_model_sync(s,&audio->sister);}
    return ok;
}
