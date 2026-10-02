static void tracker_repeat_poll(TsUiState *ui,Uint32 now,Uint32 buttons);
static unsigned tracker_alias(const TsSisterTracker *t,TsTileId id)
{
    if(id)for(unsigned i=1;i<TS_TRACKER_ALIASES;++i)if(t->aliases[i]==id)return i;
    return 0;
}
/* Prepare outside audio exclusion; publish only immutable definitions under it. */
static void tracker_refresh(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,
                            TsSamplePages *pages,const TsInstrument *instrument,int rate)
{
    ui->tracker_cursor_visible=(((SDL_GetTicks()-ui->tracker_cursor_epoch)/450u)&1u)==0;
    tracker_repeat_poll(ui,SDL_GetTicks(),SDL_GetMouseState(NULL,NULL));
    TsTrackerPlayback *rt=&audio->tracker;
    ui->tracker_running=atomic_load_explicit(&rt->display_running,memory_order_acquire);
    ui->tracker_row=atomic_load_explicit(&rt->display_row,memory_order_relaxed);
    ui->tracker_heard_pattern=atomic_load_explicit(&rt->display_pattern,memory_order_relaxed);
    ui->tracker_missing=atomic_load_explicit(&rt->display_missing,memory_order_acquire);
    ui->tracker_block=atomic_load_explicit(&rt->display_block,memory_order_acquire);
    ui->tracker_block_rows=atomic_load_explicit(&rt->display_block_rows,memory_order_relaxed);
    ui->tracker_block_lanes=atomic_load_explicit(&rt->display_block_lanes,memory_order_relaxed);
    TsTrackerLayout g=ts_tracker_layout(&pages->tracker,ui->tracker_expanded);
    if(!ui->tracker_open && !ui->tracker_running && !rt->prepared) {ts_tracker_playback_collect(rt);return;}
    TsPatternId id=ui->tracker_running?ui->tracker_heard_pattern:pages->tracker.editor_pattern;
    uint64_t stamp=ts_tracker_playback_stamp(pages,instrument,id,rate);
    if((!rt->prepared && stamp) || (rt->prepared && rt->prepared->stamp!=stamp)) {
        char error[160];
        TsTrackerPrepared *next=stamp?ts_tracker_playback_prepare(rt,pages,instrument,id,rate,error,sizeof(error)):NULL;
        if(stamp && !next) {snprintf(ui->status,sizeof(ui->status),"TRACKER: %.140s",error);return;}
        if(device)SDL_LockAudioDevice(device);
        TsTrackerPrepared *old=ts_tracker_playback_publish(rt,next);
        if(device)SDL_UnlockAudioDevice(device);
        ts_tracker_prepared_free(old);
    }
    ts_tracker_playback_collect(rt);
    const TsTrackerPattern *p=ts_sister_tracker_pattern_const(&pages->tracker,pages->tracker.editor_pattern);
    if(p) {
        if(pages->tracker.editor_row>=p->rows)pages->tracker.editor_row=p->rows-1;
        if(pages->tracker.follow && !ui->tracker_follow_hold && ui->tracker_running && ui->tracker_heard_pattern==p->id) {
            int row=(int)ui->tracker_row;
            if(row<ui->tracker_scroll || row>=ui->tracker_scroll+g.rows)ui->tracker_scroll=row;
        }
        int maximum=p->rows>g.rows?p->rows-g.rows:0;
        if(ui->tracker_scroll>maximum)ui->tracker_scroll=maximum;
    }
}
static void tracker_play(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,
                         TsSamplePages *pages,const TsInstrument *instrument,int rate)
{
    ui->tracker_follow_hold=0;
    char error[160];
    TsTrackerPrepared *next=ts_tracker_playback_prepare(&audio->tracker,pages,instrument,
        pages->tracker.editor_pattern,rate,error,sizeof(error));
    if(!next){snprintf(ui->status,sizeof(ui->status),"TRACKER: %.140s",error);return;}
    if(device)SDL_LockAudioDevice(device);
    TsTrackerPrepared *old=ts_tracker_playback_publish(&audio->tracker,next);
    ts_tracker_playback_start(&audio->tracker);
    if(device)SDL_UnlockAudioDevice(device);
    ts_tracker_prepared_free(old);ts_tracker_playback_collect(&audio->tracker);
    snprintf(ui->status,sizeof(ui->status),"PLAYING PATTERN %u - STANDARD CLOCK",pages->tracker.editor_pattern);
}
/* Mark updates pass a value under callback exclusion, never editor storage. */
static void tracker_queue_selection(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui)
{
    if(!ts_tracker_edit_selected(ui->tracker_edit,ui->tracker))return;
    TsTrackerRegion r=ts_tracker_edit_region(ui->tracker_edit,ui->tracker);
    TsTrackerBlock b={ui->tracker->editor_pattern,r.row0,r.row1,r.lane0,r.lane1};
    if(device)SDL_LockAudioDevice(device);
    ts_tracker_playback_queue_block(&audio->tracker,b);
    if(device)SDL_UnlockAudioDevice(device);
}
static void tracker_block_toggle(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,
    TsSamplePages *pages,const TsInstrument *instrument,int rate)
{
    if(device)SDL_LockAudioDevice(device);
    int active=audio->tracker.block_active;
    if(active)ts_tracker_playback_stop(&audio->tracker);
    if(device)SDL_UnlockAudioDevice(device);
    if(active){snprintf(ui->status,sizeof(ui->status),"BLOCK LOOP STOPPED");return;}
    if(!ts_tracker_edit_selected(ui->tracker_edit,ui->tracker)) {
        snprintf(ui->status,sizeof(ui->status),"SELECT A BLOCK FIRST - SHIFT ARROWS OR DRAG");return;
    }
    TsTrackerRegion r=ts_tracker_edit_region(ui->tracker_edit,ui->tracker);
    TsTrackerBlock b={ui->tracker->editor_pattern,r.row0,r.row1,r.lane0,r.lane1};
    char error[160];
    TsTrackerPrepared *next=ts_tracker_playback_prepare(&audio->tracker,pages,instrument,b.pattern,rate,error,sizeof(error));
    if(!next){snprintf(ui->status,sizeof(ui->status),"TRACKER: %.140s",error);return;}
    if(device)SDL_LockAudioDevice(device);
    TsTrackerPrepared *old=ts_tracker_playback_publish(&audio->tracker,next);
    ts_tracker_playback_start_block(&audio->tracker,b);
    if(device)SDL_UnlockAudioDevice(device);
    ts_tracker_prepared_free(old);ts_tracker_playback_collect(&audio->tracker);ui->tracker_follow_hold=0;
    snprintf(ui->status,sizeof(ui->status),"BLOCK LOOP - SHIFT ARROWS RESIZE AT SEAM / CTRL L STOP / F7 F8 CAPTURE");
}
static void tracker_visible(TsUiState *ui)
{
    TsTrackerLayout g=ts_tracker_layout(ui->tracker,ui->tracker_expanded);
    const TsTrackerPattern *p=ts_sister_tracker_pattern_const(ui->tracker,ui->tracker->editor_pattern);
    if(!p)return;
    int row=ui->tracker->editor_row;
    if(row<ui->tracker_scroll)ui->tracker_scroll=row;
    if(row>=ui->tracker_scroll+g.rows)ui->tracker_scroll=row-g.rows+1;
}
static void tracker_advance(TsUiState *ui)
{
    TsSisterTracker *t=ui->tracker;
    const TsTrackerPattern *p=ts_sister_tracker_pattern_const(t,t->editor_pattern);
    if(p)t->editor_row=(t->editor_row+t->edit_step)%p->rows;
    ui->tracker_hex_digit=0;tracker_visible(ui);
}
static void tracker_select_pattern(TsUiState *ui,int direction)
{
    TsSisterTracker *t=ui->tracker;int index=0;
    for(int i=0;i<t->pattern_count;++i)if(t->patterns[i]->id==t->editor_pattern){index=i;break;}
    if(t->pattern_count)t->editor_pattern=t->patterns[(index+direction+t->pattern_count)%t->pattern_count]->id;
    t->editor_row=0;ui->tracker_scroll=ui->tracker_hex_digit=0;
}
static int tracker_hex(SDL_Keycode key)
{
    if(key>=SDLK_0 && key<=SDLK_9)return key-SDLK_0;
    if(key>=SDLK_a && key<=SDLK_f)return key-SDLK_a+10;
    if(key>=SDLK_KP_1 && key<=SDLK_KP_9)return key-SDLK_KP_1+1;
    return key==SDLK_KP_0?0:-1;
}
static int tracker_begin(TsUiState *ui,TsTrackerPattern *p)
{
    if(ts_tracker_edit_begin(ui->tracker_edit,p))return 1;
    snprintf(ui->status,sizeof(ui->status),"TRACKER EDIT MEMORY UNAVAILABLE");return 0;
}
static void tracker_repeat_end(TsUiState *ui)
{
    if(ui->tracker_repeat_control==3)
        ts_tracker_edit_commit(ui->tracker_edit,ts_sister_tracker_pattern(ui->tracker,ui->tracker->editor_pattern));
    ui->tracker_repeat_control=0;
}
static void tracker_adjust(TsUiState *ui,int control,int delta)
{
    TsSisterTracker *t=ui->tracker;
    TsTrackerPattern *p=ts_sister_tracker_pattern(t,t->editor_pattern);
    if(control==1)t->bpm=SDL_clamp((int)t->bpm+delta,32,999);
    else if(control==2)t->ticks_per_line=SDL_clamp((int)t->ticks_per_line+delta,1,31);
    else if(control==3 && p)ts_sister_tracker_set_rows(t,p->id,SDL_clamp((int)p->rows+delta,1,256));
}
static void tracker_repeat_poll(TsUiState *ui,Uint32 now,Uint32 buttons)
{
    if(!ui->tracker_repeat_control)return;
    if(!ui->tracker_open || ui_dialog_open(ui) || ui->router_open || ui->master_eq_open ||
       !(buttons&SDL_BUTTON(ui->tracker_repeat_button))) {tracker_repeat_end(ui);return;}
    if((Sint32)(now-ui->tracker_repeat_next)>=0) {
        tracker_adjust(ui,ui->tracker_repeat_control,ui->tracker_repeat_delta);
        ui->tracker_repeat_next=now+65; /* No catch-up burst after a busy frame. */
    }
}
static void tracker_clear(TsUiState *ui,TsTrackerCell *c,int whole)
{
    if(whole)memset(c,0,sizeof(*c));
    else if(ui->tracker_field==0){c->note_kind=TS_TRACKER_NOTE_NONE;c->note=0;}
    else if(ui->tracker_field==1)c->tile_id=0;
    else if(ui->tracker_field==2){c->has_volume=0;c->volume=0;}
    else if(ui->tracker_field==3){c->tune_command=0;c->tune_value=0;}
    else if(ui->tracker_field==4)c->tune_value=0;
    else if(ui->tracker_field==5){c->fx_command=0;c->fx_value=0;}
    else c->fx_value=0;
    ui->tracker_hex_digit=0;
}
/* Prefer the visible Sample slot number for a new alias; never retarget a bound
   alias or register the same stable ID twice. */
static unsigned tracker_bind_sample(TsUiState *ui,const TsInstrument *instrument,int slot)
{
    TsSisterTracker *t=ui->tracker;
    if(slot<0 || slot>=TS_BANK_SLOT_COUNT || !instrument->bank[slot].occupied || !instrument->bank[slot].tile_id)return 0;
    TsTileId id=instrument->bank[slot].tile_id;
    unsigned alias=tracker_alias(t,id);if(alias)return alias;
    if(!t->aliases[slot+1]) {t->aliases[slot+1]=id;return slot+1;}
    uint8_t bound=0;
    return ts_sister_tracker_bind_tile(t,id,&bound,ui->status,sizeof(ui->status))?bound:0;
}
static int tracker_enter_value(TsUiState *ui,TsInstrument *instrument,unsigned value)
{
    TsSisterTracker *t=ui->tracker;
    TsTrackerPattern *p=ts_sister_tracker_pattern(t,t->editor_pattern);
    TsTrackerCell *c=&p->cells[t->editor_row][t->editor_lane];
    if((ui->tracker_field==4 && !c->tune_command) || (ui->tracker_field==6 && !c->fx_command)) {
        snprintf(ui->status,sizeof(ui->status),"ENTER THE TUNING M/N OR FX COMMAND FIRST");return 0;
    }
    if(ui->tracker_field==4 && c->tune_command=='M' && value>127) {
        snprintf(ui->status,sizeof(ui->status),"M ROOT NOTE MUST BE 00-7F (MIDI 0-127)");return 0;
    }
    if(ui->tracker_field==1 && value && !t->aliases[value]) {
        unsigned alias=value<=TS_BANK_SLOT_COUNT?tracker_bind_sample(ui,instrument,(int)value-1):0;
        if(!alias) {snprintf(ui->status,sizeof(ui->status),"TILE %02X IS UNBOUND; SELECT A SAMPLE AND USE TILE",value);return 0;}
        if(alias!=value)snprintf(ui->status,sizeof(ui->status),"SAMPLE TILE %02X USES EXISTING ALIAS %02X",value,alias);
        value=alias;
    }
    if(!tracker_begin(ui,p))return 0;
    if(ui->tracker_field==1)c->tile_id=t->aliases[value];
    else if(ui->tracker_field==2){c->has_volume=1;c->volume=value>0x40?0x40:value;}
    else if(ui->tracker_field==4)c->tune_value=value;
    else if(ui->tracker_field==6)c->fx_value=value;
    ts_tracker_edit_commit(ui->tracker_edit,p);tracker_advance(ui);return 1;
}
enum { TRACKER_COPY,TRACKER_CUT,TRACKER_PASTE,TRACKER_MIX,TRACKER_UNDO,TRACKER_REDO,
       TRACKER_INSERT,TRACKER_DELETE_ROW,TRACKER_DOWN,TRACKER_UP,TRACKER_OCT_DOWN,
       TRACKER_OCT_UP,TRACKER_INTERPOLATE,TRACKER_FILL,TRACKER_REVERSE,TRACKER_DUPLICATE,
       TRACKER_SELECT_LANE,TRACKER_SELECT_PATTERN,TRACKER_CLONE,TRACKER_EXTRACT,TRACKER_EXPAND,TRACKER_SHRINK };
static TsTrackerRegion tracker_scope(TsUiState *ui,int scope)
{
    TsTrackerRegion r=ts_tracker_edit_region(ui->tracker_edit,ui->tracker);
    const TsTrackerPattern *p=ts_sister_tracker_pattern_const(ui->tracker,ui->tracker->editor_pattern);
    if(scope==1 || scope==2) {r.row0=0;r.row1=p->rows-1;r.lane0=r.lane1=ui->tracker->editor_lane;}
    if(scope==2){r.lane0=0;r.lane1=TS_TRACKER_LANES-1;}
    return r;
}
static void tracker_command(TsUiState *ui,int command,int scope,int whole)
{
    TsSisterTracker *t=ui->tracker;TsTrackerEdit *e=ui->tracker_edit;
    TsTrackerPattern *p=ts_sister_tracker_pattern(t,t->editor_pattern);
    TsTrackerRegion r=tracker_scope(ui,scope);ui->tracker_hex_digit=0;
    if(command==TRACKER_SELECT_LANE || command==TRACKER_SELECT_PATTERN) {
        e->anchor_row=0;e->anchor_lane=command==TRACKER_SELECT_PATTERN?0:t->editor_lane;
        e->end_row=p->rows-1;e->end_lane=command==TRACKER_SELECT_PATTERN?TS_TRACKER_LANES-1:t->editor_lane;
        e->selected_pattern=p->id;e->selected=1;return;
    }
    if(command==TRACKER_UNDO || command==TRACKER_REDO) {
        int ok=ts_tracker_edit_undo(e,t,command==TRACKER_REDO);tracker_visible(ui);
        snprintf(ui->status,sizeof(ui->status),ok?(command==TRACKER_REDO?"REDO":"UNDO"):"NO EDIT HISTORY");return;
    }
    if(command==TRACKER_EXTRACT) {
        TsPatternId made;
        if(ts_tracker_edit_extract(e,t,&made,ui->status,sizeof(ui->status)))
            snprintf(ui->status,sizeof(ui->status),"BLOCK COPIED TO PATTERN %03u - UNDO AVAILABLE",made);
        return;
    }
    if(command==TRACKER_CLONE) {
        if(ts_sister_tracker_copy_pattern(t,p->id,&t->editor_pattern,ui->status,sizeof(ui->status))) {
            t->editor_row=0;ui->tracker_scroll=0;ts_tracker_edit_anchor(e,t);
            snprintf(ui->status,sizeof(ui->status),"CLONED PATTERN %03u",t->editor_pattern);
        }return;
    }
    if(command==TRACKER_COPY || command==TRACKER_CUT || command==TRACKER_DUPLICATE) {
        ts_tracker_edit_copy(e,p,r);
        if(command==TRACKER_COPY) {snprintf(ui->status,sizeof(ui->status),"COPIED %u ROWS, %u LANES",e->clipboard_rows,e->clipboard_lanes);return;}
    }
    if((command==TRACKER_PASTE || command==TRACKER_MIX) && !e->clipboard_rows) {
        snprintf(ui->status,sizeof(ui->status),"TRACKER CLIPBOARD IS EMPTY");return;
    }
    if(!tracker_begin(ui,p))return;
    switch(command) {
    case TRACKER_EXPAND:case TRACKER_SHRINK:
        if(ts_tracker_edit_scale(p,command==TRACKER_EXPAND)) {
            t->editor_row=command==TRACKER_EXPAND?t->editor_row*2:t->editor_row/2;
            if(t->editor_row>=p->rows)t->editor_row=p->rows-1;
        } else snprintf(ui->status,sizeof(ui->status),"PATTERN CANNOT %s AT THIS LENGTH",command==TRACKER_EXPAND?"EXPAND":"SHRINK");
        break;
    case TRACKER_CUT:ts_tracker_edit_clear(p,r,e->mask);break;
    case TRACKER_PASTE:case TRACKER_MIX:
        ts_tracker_edit_paste(e,p,scope?0:t->editor_row,scope==2?0:t->editor_lane,command==TRACKER_MIX);break;
    case TRACKER_INSERT:case TRACKER_DELETE_ROW:
        ts_tracker_edit_shift_rows(p,t->editor_row,t->editor_lane,whole,command==TRACKER_INSERT);break;
    case TRACKER_DOWN:case TRACKER_UP:case TRACKER_OCT_DOWN:case TRACKER_OCT_UP:
        if(!ts_tracker_edit_selected(e,t) && !scope)r=tracker_scope(ui,1);
        ts_tracker_edit_transpose(p,r,command==TRACKER_DOWN?-1:command==TRACKER_UP?1:command==TRACKER_OCT_DOWN?-12:12);break;
    case TRACKER_INTERPOLATE:
        if(!ts_tracker_edit_interpolate(p,r))snprintf(ui->status,sizeof(ui->status),"SELECT ROWS WITH EXPLICIT START AND END VOLUMES");break;
    case TRACKER_FILL:ts_tracker_edit_fill(p,r,e->mask);break;
    case TRACKER_REVERSE:ts_tracker_edit_reverse(p,r,e->mask);break;
    case TRACKER_DUPLICATE:ts_tracker_edit_paste(e,p,r.row1+1,r.lane0,0);break;
    }
    ts_tracker_edit_commit(e,p);tracker_visible(ui);
}
static int tracker_control_at(int x,int y,int expanded)
{
    if(expanded || y<34 || y>=50)return 0;
    return x>=212 && x<290?1:x>=294 && x<350?2:x>=354 && x<426?3:0;
}
static void tracker_fx(SDL_Window *window)
{
    SDL_Event e;SDL_zero(e);e.type=SDL_KEYDOWN;e.key.windowID=SDL_GetWindowID(window);
    e.key.keysym.sym=SDLK_TAB;SDL_PushEvent(&e);
}
static int tracker_event(const SDL_Event *event,SDL_Window *window,SDL_AudioDeviceID device,
    AudioState *audio,TsUiState *ui,TsSamplePages *pages,TsInstrument *instrument,int rate)
{
    if(event_window_id(event)!=SDL_GetWindowID(window))return 0;
    int key=event->type==SDL_KEYDOWN;SDL_Keymod mod=key?(SDL_Keymod)event->key.keysym.mod:SDL_GetModState();
    if(key && event->key.keysym.sym==SDLK_F10 && !(mod&(KMOD_CTRL|KMOD_ALT|KMOD_GUI))) {
        if(event->key.repeat)return 1;
        if(ui_dialog_open(ui) || ui->mosaic_editing || ui->fm_open || ui->selecting ||
           ui->tape_dragging || ui->dragging_loop_endpoint || ui->wave_pointer_pending) {
            snprintf(ui->status,sizeof(ui->status),"RETURN TO SAMPLE WORKSPACE BEFORE OPENING TRACKER");return 1;
        }
        if(!ui->tracker_edit)ui->tracker_edit=ts_tracker_edit_new();
        if(!ui->tracker_edit){snprintf(ui->status,sizeof(ui->status),"TRACKER EDIT MEMORY UNAVAILABLE");return 1;}
        if(!pages->tracker.pattern_count && !ts_sister_tracker_add_pattern(&pages->tracker,64,
            &pages->tracker.editor_pattern,ui->status,sizeof(ui->status)))return 1;
        tracker_repeat_end(ui);release_mouse_note_before_keyboard_move(device,audio,ui);
        ui->tracker_open=!ui->tracker_open;ui->mosaic_open=0;ui->tracker_hex_digit=0;
        ui->tracker_cursor_epoch=SDL_GetTicks();
        ui->tracker_edit->dragging=0;
        snprintf(ui->status,sizeof(ui->status),ui->tracker_open?
            "ARROWS EDIT / TAB FX / F9 ROUTER / F10 SAMPLE / EDIT TOOLS BELOW":"SISTERTRACKER CONTINUES IN BACKGROUND");
        tracker_refresh(device,audio,ui,pages,instrument,rate);return 1;
    }
    if(!ui->tracker_open || ui_dialog_open(ui))return 0;
    if(ui->midi_learn_active)return 0; /* Host Learn owns input; only visible OUT can be mapped. */
    TsSisterTracker *t=&pages->tracker;TsTrackerEdit *edit=ui->tracker_edit;
    TsTrackerPattern *p=ts_sister_tracker_pattern(t,t->editor_pattern);
    if(!p || !edit)return 0;
    TsTrackerCell *c=&p->cells[t->editor_row][t->editor_lane];
    TsTrackerLayout g=ts_tracker_layout(t,ui->tracker_expanded);
    if(event->type==SDL_WINDOWEVENT) {
        if(event->window.event==SDL_WINDOWEVENT_FOCUS_LOST || event->window.event==SDL_WINDOWEVENT_LEAVE ||
           event->window.event==SDL_WINDOWEVENT_HIDDEN) {tracker_repeat_end(ui);edit->dragging=0;}
        return 0; /* Host focus-loss cleanup still releases manual notes. */
    }
    if(key) {
        SDL_Keycode sym=event->key.keysym.sym;tracker_repeat_end(ui);edit->dragging=0;
        ui->tracker_cursor_epoch=SDL_GetTicks();
        if(sym==SDLK_BACKSPACE && (mod&(KMOD_CTRL|KMOD_ALT))==(KMOD_CTRL|KMOD_ALT)) {
            if(!event->key.repeat){ui->tracker_expanded=!ui->tracker_expanded;tracker_visible(ui);}
            return 1;
        }
        if(sym==SDLK_c && (mod&KMOD_ALT) && !(mod&(KMOD_CTRL|KMOD_GUI))) {
            tracker_command(ui,TRACKER_SELECT_LANE,0,0);tracker_queue_selection(device,audio,ui);return 1;
        }
        if(sym==SDLK_l && (mod&KMOD_CTRL) && !(mod&(KMOD_ALT|KMOD_GUI))) {
            if(!event->key.repeat)tracker_block_toggle(device,audio,ui,pages,instrument,rate);
            return 1;
        }
        if(sym==SDLK_F8 && !(mod&(KMOD_CTRL|KMOD_ALT|KMOD_GUI|KMOD_SHIFT))) {
            if(!event->key.repeat)tracker_command(ui,TRACKER_EXTRACT,0,0);
            return 1;
        }
        if(sym==SDLK_TAB || sym==SDLK_F9 || sym==SDLK_F12){ui->tracker_hex_digit=0;return 0;}
        /* Tapehead's track/pattern/block F-key operations, alongside standard
           clipboard shortcuts. Save, MIDI learn, focus and panic stay global. */
        if(sym>=SDLK_F1 && sym<=SDLK_F5 && (mod&(KMOD_CTRL|KMOD_ALT|KMOD_SHIFT)) && !(mod&KMOD_GUI)) {
            int scope=(mod&KMOD_CTRL)?2:(mod&KMOD_SHIFT)?1:0;
            tracker_command(ui,sym==SDLK_F1?TRACKER_DOWN:sym==SDLK_F2?TRACKER_UP:
                sym==SDLK_F3?TRACKER_CUT:sym==SDLK_F4?TRACKER_COPY:TRACKER_PASTE,scope,0);return 1;
        }
        if((mod&(KMOD_CTRL|KMOD_GUI)) && !(mod&KMOD_ALT)) {
            int command=-1;
            if(sym==SDLK_z)command=mod&KMOD_SHIFT?TRACKER_REDO:TRACKER_UNDO;
            else if(sym==SDLK_y)command=TRACKER_REDO;
            else if(sym==SDLK_c)command=TRACKER_COPY;
            else if(sym==SDLK_x)command=TRACKER_CUT;
            else if(sym==SDLK_v)command=mod&KMOD_SHIFT?TRACKER_INTERPOLATE:TRACKER_PASTE;
            else if(sym==SDLK_a)command=TRACKER_SELECT_PATTERN;
            else if(sym==SDLK_b)command=TRACKER_DUPLICATE;
            else if(sym==SDLK_d)command=TRACKER_FILL;
            else if(sym==SDLK_r)command=TRACKER_REVERSE;
            else if(sym==SDLK_e){if(!event->key.repeat)edit->tools_open=!edit->tools_open;return 1;}
            else if(sym==SDLK_UP)command=mod&KMOD_SHIFT?TRACKER_OCT_UP:TRACKER_UP;
            else if(sym==SDLK_DOWN)command=mod&KMOD_SHIFT?TRACKER_OCT_DOWN:TRACKER_DOWN;
            if(command>=0){if(!event->key.repeat){tracker_command(ui,command,0,0);if(command==TRACKER_SELECT_PATTERN)tracker_queue_selection(device,audio,ui);}return 1;}
            return sym==SDLK_s || sym==SDLK_m || ((sym==SDLK_p || sym==SDLK_f) && (mod&KMOD_SHIFT))?0:1;
        }
        if(mod&(KMOD_ALT|KMOD_GUI|KMOD_CTRL))return 1;
        if(sym==SDLK_ESCAPE) {
            if(edit->tools_open)edit->tools_open=0;
            else if(ts_tracker_edit_selected(edit,t))ts_tracker_edit_anchor(edit,t);
            else ui->tracker_open=0;return 1;
        }
        if((sym==SDLK_RETURN || sym==SDLK_KP_ENTER) && ui->tracker_hex_digit) {
            unsigned value=(unsigned)ui->tracker_hex_value>>4;ui->tracker_hex_digit=0;
            tracker_enter_value(ui,instrument,value);return 1;
        }
        if(sym==SDLK_SPACE || sym==SDLK_RETURN || sym==SDLK_KP_ENTER) {
            if(!event->key.repeat) {
                if(sym!=SDLK_SPACE)tracker_play(device,audio,ui,pages,instrument,rate);
                else {
                    /* Transport decisions use current callback-owned state,
                       not a possibly stale display snapshot. */
                    if(device)SDL_LockAudioDevice(device);
                    int running=audio->tracker.running;
                    if(running)ts_tracker_playback_pause(&audio->tracker,!audio->tracker.paused);
                    if(device)SDL_UnlockAudioDevice(device);
                    if(!running)tracker_play(device,audio,ui,pages,instrument,rate);
                }
            }return 1;
        }
        if(sym>=SDLK_F1 && sym<=SDLK_F8){ts_ui_keyboard_set_octave(ui,(int)(sym-SDLK_F1));return 1;}
        if(sym==SDLK_INSERT || sym==SDLK_BACKSPACE) {
            tracker_command(ui,sym==SDLK_INSERT?TRACKER_INSERT:TRACKER_DELETE_ROW,0,!!(mod&KMOD_SHIFT));return 1;
        }
        if(sym==SDLK_DELETE) {
            if(tracker_begin(ui,p)) {
                if(ts_tracker_edit_selected(edit,t))ts_tracker_edit_clear(p,ts_tracker_edit_region(edit,t),mod&KMOD_SHIFT?TS_TRACKER_EDIT_ALL:edit->mask);
                else tracker_clear(ui,c,!!(mod&KMOD_SHIFT));
                ts_tracker_edit_commit(edit,p);
            }return 1;
        }
        int navigation=sym==SDLK_UP || sym==SDLK_DOWN || sym==SDLK_LEFT || sym==SDLK_RIGHT ||
            sym==SDLK_PAGEUP || sym==SDLK_PAGEDOWN || sym==SDLK_HOME || sym==SDLK_END;
        if(navigation) {
            if(mod&KMOD_SHIFT) {
                if(!ts_tracker_edit_selected(edit,t)){ts_tracker_edit_anchor(edit,t);edit->selected=1;}
                else {t->editor_row=SDL_clamp(edit->end_row,0,p->rows-1);t->editor_lane=edit->end_lane;}
            }
            if(sym==SDLK_UP || sym==SDLK_DOWN)t->editor_row=SDL_clamp((int)t->editor_row+(sym==SDLK_UP?-1:1),0,p->rows-1);
            else if(sym==SDLK_LEFT || sym==SDLK_RIGHT) {
                if(mod&KMOD_SHIFT)t->editor_lane=SDL_clamp((int)t->editor_lane+(sym==SDLK_LEFT?-1:1),0,7);
                else {int count=TS_TRACKER_LANES*TS_TRACKER_FIELD_COUNT,f=t->editor_lane*TS_TRACKER_FIELD_COUNT+ui->tracker_field;
                    f=(f+(sym==SDLK_LEFT?-1:1)+count)%count;t->editor_lane=f/TS_TRACKER_FIELD_COUNT;ui->tracker_field=f%TS_TRACKER_FIELD_COUNT;}
            } else if(sym==SDLK_PAGEUP || sym==SDLK_PAGEDOWN)t->editor_row=SDL_clamp((int)t->editor_row+(sym==SDLK_PAGEUP?-g.rows:g.rows),0,p->rows-1);
            else t->editor_row=sym==SDLK_HOME?0:p->rows-1;
            if(mod&KMOD_SHIFT){ts_tracker_edit_mark(edit,t,t->editor_row,t->editor_lane);tracker_queue_selection(device,audio,ui);}
            ui->tracker_hex_digit=0;ui->tracker_follow_hold=1;tracker_visible(ui);return 1;
        }
        if(edit->tools_open)return 1;
        if(ui->tracker_field==3 || ui->tracker_field==5) {
            if(event->key.repeat)return 1;
            int command=0;
            if(ui->tracker_field==3 && (sym==SDLK_m || sym==SDLK_n))command=sym-SDLK_a+'A';
            if(ui->tracker_field==5) {
                if(sym>=SDLK_a && sym<=SDLK_z)command=sym-SDLK_a+'A';
                else if(sym>=SDLK_0 && sym<=SDLK_9)command=sym-SDLK_0+'0';
                else if(sym>=SDLK_KP_1 && sym<=SDLK_KP_9)command=sym-SDLK_KP_1+'1';
                else if(sym==SDLK_KP_0)command='0';
            }
            if(command && tracker_begin(ui,p)) {
                if(ui->tracker_field==3){c->tune_command=command;if(command=='M' && c->tune_value>127)c->tune_value=127;}
                else c->fx_command=command;
                ts_tracker_edit_commit(edit,p);++ui->tracker_field;ui->tracker_hex_digit=0;
                snprintf(ui->status,sizeof(ui->status),"TUNING / FX DEFINITIONS SAVED; COMMAND AUDIO EXECUTION IS NOT ENABLED YET");
            }return 1;
        }
        if(ui->tracker_field==0 && note_for_key(sym)>=0) {
            int note=ui->keyboard_base_note+note_for_key(sym);
            if(note<=127 && tracker_begin(ui,p)) {
                c->note_kind=TS_TRACKER_NOTE_PITCH;c->note=note;ts_tracker_edit_commit(edit,p);tracker_advance(ui);
            }return 1;
        }
        int digit=tracker_hex(sym);
        if((ui->tracker_field==1 || ui->tracker_field==2 || ui->tracker_field==4 || ui->tracker_field==6) && digit>=0) {
            if(event->key.repeat)return 1;
            if(!ui->tracker_hex_digit) {ui->tracker_hex_value=digit<<4;ui->tracker_hex_digit=1;return 1;}
            unsigned value=(unsigned)ui->tracker_hex_value|digit;ui->tracker_hex_digit=0;
            tracker_enter_value(ui,instrument,value);return 1;
        }
        return 1;
    }
    if(event->type==SDL_KEYUP)return 0; /* Release manual keys held before opening. */
    if(event->type==SDL_TEXTINPUT)return 1;
    if(event->type==SDL_MOUSEWHEEL) {
        tracker_repeat_end(ui);int rawx,rawy,x,y;wheel_event_mouse(event,&rawx,&rawy);logical_mouse(window,rawx,rawy,&x,&y);
        int control=tracker_control_at(x,y,ui->tracker_expanded),detents=wheel_event_detents(event,0);
        int lane=y>=g.mix_y && y<g.mix_y+10 && x>=TS_TRACKER_LANE_X && x<TS_TRACKER_LANE_RIGHT && (x-TS_TRACKER_LANE_X)%TS_TRACKER_LANE_WIDTH>=49?(x-TS_TRACKER_LANE_X)/TS_TRACKER_LANE_WIDTH:-1;
        int grid=y>=g.grid_y && y<g.bottom && x>=6 && x<630;
        int output=!g.expanded && ts_ui_master_output_contains(x,y);
        int target=control?0x700+control:lane>=0?0x710+lane:grid?0x720:output?0x730:-1;
        if(target<0 || edit->tools_open || !ts_ui_wheel_guard_accept(&ui->wheel_guard,target,SDL_GetTicks()))return 1;
        if(output)set_master_output_gain(device,audio,ui,NULL,ui->master_output.gain+
            ((mod&KMOD_SHIFT)?.01f:.05f)*(float)wheel_event_delta(event,0));
        else if(control && detents) {
            if(control!=3 || tracker_begin(ui,p)) {
                tracker_adjust(ui,control,detents*(control==3 && (mod&KMOD_SHIFT)?16:1));
                if(control==3)ts_tracker_edit_commit(edit,p);
            }
        } else if(lane>=0)t->lanes[lane].trim=fmaxf(0,fminf(2,t->lanes[lane].trim+.1f*(float)wheel_event_delta(event,0)));
        else if(grid && detents) {
            ui->tracker_scroll=SDL_clamp(ui->tracker_scroll-detents,0,p->rows>g.rows?p->rows-g.rows:0);
            ui->tracker_follow_hold=1;
        }return 1;
    }
    if(event->type==SDL_MOUSEBUTTONUP) {
        if(event->button.button==ui->tracker_repeat_button)tracker_repeat_end(ui);
        if(event->button.button==SDL_BUTTON_LEFT)edit->dragging=0;
        return ui->master_output_dragging?0:1;
    }
    if(event->type==SDL_MOUSEMOTION) {
        int x,y;logical_mouse(window,event->motion.x,event->motion.y,&x,&y);
        ui->tracker_hover_x=x;ui->tracker_hover_y=y;
        if(ui->tracker_repeat_control && tracker_control_at(x,y,ui->tracker_expanded)!=ui->tracker_repeat_control)tracker_repeat_end(ui);
        if(edit->dragging && (event->motion.state&SDL_BUTTON_LMASK)) {
            if(x>=TS_TRACKER_LANE_X && x<TS_TRACKER_LANE_RIGHT && y>=g.grid_y && y<g.bottom) {
                int row=SDL_clamp(ui->tracker_scroll+(y-g.grid_y)/TS_TRACKER_ROW_HEIGHT,0,p->rows-1),lane=(x-TS_TRACKER_LANE_X)/TS_TRACKER_LANE_WIDTH;
                if(row!=edit->drag_row || lane!=edit->drag_lane) {
                    if(edit->dragging==1){edit->anchor_row=edit->drag_row;edit->anchor_lane=edit->drag_lane;edit->selected_pattern=p->id;edit->selected=1;edit->dragging=2;}
                    ts_tracker_edit_mark(edit,t,row,lane);t->editor_row=row;t->editor_lane=lane;
                    tracker_queue_selection(device,audio,ui);
                }
            }
        } else edit->dragging=0;
        return ui->master_output_dragging?0:1;
    }
    if(event->type!=SDL_MOUSEBUTTONDOWN)return 0;
    int x,y;logical_mouse(window,event->button.x,event->button.y,&x,&y);
    int left=event->button.button==SDL_BUTTON_LEFT,right=event->button.button==SDL_BUTTON_RIGHT;
    tracker_repeat_end(ui);edit->dragging=0;ui->tracker_hex_digit=0;ui->tracker_cursor_epoch=SDL_GetTicks();
    if(!g.expanded && left && x>=544 && x<630 && y>=382 && y<397)return 0; /* Visible FILE OUT. */
    if(!g.expanded && left && ts_ui_master_output_contains(x,y))return 0; /* The visible host fader. */
    if(!left && !right)return 1;
    int delta=right?-1:1;
    if(!g.expanded && y>=4 && y<28) {
        if(x>=170 && x<212)tracker_play(device,audio,ui,pages,instrument,rate);
        else if(x>=216 && x<266){if(device)SDL_LockAudioDevice(device);
            ts_tracker_playback_pause(&audio->tracker,!audio->tracker.paused);if(device)SDL_UnlockAudioDevice(device);}
        else if(x>=270 && x<312){if(device)SDL_LockAudioDevice(device);
            ts_tracker_playback_stop(&audio->tracker);if(device)SDL_UnlockAudioDevice(device);}
        else if(x>=316 && x<358)ui->tracker_open=0;
        else if(x>=362 && x<406) {
            SDL_Event save;SDL_zero(save);save.type=SDL_KEYDOWN;save.key.windowID=SDL_GetWindowID(window);
            save.key.keysym.sym=SDLK_s;save.key.keysym.mod=KMOD_CTRL;SDL_PushEvent(&save);
        } else if(x>=410 && x<466){t->follow=ui->tracker_follow_hold?1:!t->follow;ui->tracker_follow_hold=0;}
        else if(x>=466 && x<490)tracker_fx(window);
        return 1; /* Blank header and title must never hit hidden Sample controls. */
    }
    if(edit->tools_open) {
        if(x>=536 && x<612 && y>=111 && y<129){edit->tools_open=0;return 1;}
        if(x>=28 && x<612 && y>=138 && y<224) {
            int col=(x-28)/98,row=(y-138)/22;
            if(col<6 && (x-28)%98<94 && (y-138)%22<18){int command=row*6+col;if(command<=TRACKER_SHRINK){tracker_command(ui,command,0,0);if(command==TRACKER_SELECT_LANE || command==TRACKER_SELECT_PATTERN)tracker_queue_selection(device,audio,ui);}}
        } else if(y>=245 && y<263 && x>=28 && x<518) {
            int col=(x-28)/98;if((x-28)%98<94)edit->mask^=1u<<col;
        }return 1;
    }
    if(y>=g.len_y-1 && y<g.ratio_y+10 && x>=TS_TRACKER_LANE_X && x<TS_TRACKER_LANE_RIGHT) {
        t->editor_lane=(x-TS_TRACKER_LANE_X)/TS_TRACKER_LANE_WIDTH;
        snprintf(ui->status,sizeof(ui->status),"LEN/FT SHOW SAVED SETUP; LENGTH AND RATIO PLAYBACK IS NOT ENABLED YET");
        return 1;
    }
    if(!g.expanded && y>=34 && y<50) {
        int control=tracker_control_at(x,y,ui->tracker_expanded);
        if(control) {
            if(control!=3 || tracker_begin(ui,p)) {
                ui->tracker_repeat_control=control;ui->tracker_repeat_delta=delta*(control==3 && (mod&KMOD_SHIFT)?16:1);
                ui->tracker_repeat_button=event->button.button;ui->tracker_repeat_next=SDL_GetTicks()+300;
                tracker_adjust(ui,control,ui->tracker_repeat_delta);
            }
        } else if(x>=10 && x<30)tracker_select_pattern(ui,-1);
        else if(x>=144 && x<164)tracker_select_pattern(ui,1);
        else if(x>=168 && x<204) {
            if(ts_sister_tracker_add_pattern(t,64,&t->editor_pattern,ui->status,sizeof(ui->status))) {
                t->editor_row=0;ui->tracker_scroll=0;ts_tracker_edit_anchor(edit,t);
            }
        } else if(x>=430 && x<486)t->loop=!t->loop;
    } else if(y>=g.mix_y && y<g.mix_y+10 && x>=TS_TRACKER_LANE_X && x<TS_TRACKER_LANE_RIGHT) {
        int lane=(x-TS_TRACKER_LANE_X)/TS_TRACKER_LANE_WIDTH,local=(x-TS_TRACKER_LANE_X)%TS_TRACKER_LANE_WIDTH;
        if(local>=2 && local<12)t->lanes[lane].muted=!t->lanes[lane].muted;
        else if(local>=14 && local<24){ui->tracker_solo^=(uint8_t)(1u<<lane);if(device)SDL_LockAudioDevice(device);
            ts_tracker_playback_solo(&audio->tracker,ui->tracker_solo);if(device)SDL_UnlockAudioDevice(device);}
        else if(local>=49){float trim=t->lanes[lane].trim+delta*.1f;t->lanes[lane].trim=fmaxf(0,fminf(2,trim));}
    } else if(y>=g.grid_y && y<g.bottom && x>=TS_TRACKER_LANE_X && x<TS_TRACKER_LANE_RIGHT) {
        int row=ui->tracker_scroll+(y-g.grid_y)/TS_TRACKER_ROW_HEIGHT;
        if(row<p->rows) {
            if(mod&KMOD_SHIFT) {
                ts_tracker_edit_mark(edit,t,row,(x-TS_TRACKER_LANE_X)/TS_TRACKER_LANE_WIDTH);
                tracker_queue_selection(device,audio,ui);
            }
            t->editor_row=row;t->editor_lane=(x-TS_TRACKER_LANE_X)/TS_TRACKER_LANE_WIDTH;
            edit->drag_row=row;edit->drag_lane=t->editor_lane;
            int local=(x-TS_TRACKER_LANE_X)%TS_TRACKER_LANE_WIDTH;ui->tracker_field=ts_tracker_field_at(local);
            edit->dragging=left?((mod&KMOD_SHIFT)?2:1):0;ui->tracker_follow_hold=1;
        }
    } else if(!g.expanded && y>=332 && y<356) {
        if(x>=10 && x<84) {
            unsigned alias=tracker_bind_sample(ui,instrument,instrument->selected_slot);
            if(!alias)snprintf(ui->status,sizeof(ui->status),"SELECT AN OCCUPIED SAMPLE TILE FIRST");
            else if(tracker_begin(ui,p)) {
                c->tile_id=t->aliases[alias];ts_tracker_edit_commit(edit,p);
                snprintf(ui->status,sizeof(ui->status),"SAMPLE TILE %02X -> ALIAS %02X; EMPTY TILE INHERITS PER LANE",instrument->selected_slot+1,alias);
            }
        } else if(x>=88 && x<140) {
            int alias=(int)tracker_alias(t,c->tile_id),step=x<112?-1:1;
            if(tracker_begin(ui,p)) {
                for(int n=0;n<256;++n){alias=(alias+step+256)%256;if(!alias || t->aliases[alias]){c->tile_id=t->aliases[alias];break;}}
                ts_tracker_edit_commit(edit,p);
            }
        } else if((x>=146 && x<178) || (x>=182 && x<214)) {
            if(tracker_begin(ui,p)){c->note_kind=x<178?TS_TRACKER_NOTE_OFF:TS_TRACKER_NOTE_CUT;ts_tracker_edit_commit(edit,p);tracker_advance(ui);}
        } else if(x>=218 && x<266){if(tracker_begin(ui,p)){tracker_clear(ui,c,1);ts_tracker_edit_commit(edit,p);}}
        else if(x>=270 && x<336)t->edit_step=(t->edit_step+delta+17)%17;
        else if(x>=340 && x<412)ts_ui_keyboard_set_octave(ui,ui->keyboard_octave+delta);
        else if(x>=418 && x<574) {
            int group=(x-418)/32;ui->tracker_field=group<3?group:group==3?3:5;
        }
        else if(x>=578 && x<630)edit->tools_open=1;
    }
    /* Scrolling does not move the cursor or snap the viewport back to it. */
    return 1;
}
