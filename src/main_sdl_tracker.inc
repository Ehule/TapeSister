static unsigned tracker_alias(const TsSisterTracker *t,TsTileId id)
{
    if(id)for(unsigned i=1;i<TS_TRACKER_ALIASES;++i)if(t->aliases[i]==id)return i;
    return 0;
}
/* Prepare outside audio exclusion; publish only immutable definitions under it. */
static void tracker_refresh(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,
                            TsSamplePages *pages,const TsInstrument *instrument,int rate)
{
    TsTrackerPlayback *rt=&audio->tracker;
    ui->tracker_running=atomic_load_explicit(&rt->display_running,memory_order_acquire);
    ui->tracker_row=atomic_load_explicit(&rt->display_row,memory_order_relaxed);
    ui->tracker_heard_pattern=atomic_load_explicit(&rt->display_pattern,memory_order_relaxed);
    ui->tracker_missing=atomic_load_explicit(&rt->display_missing,memory_order_acquire);
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
        if(pages->tracker.follow && ui->tracker_running && ui->tracker_heard_pattern==p->id) {
            int row=(int)ui->tracker_row;
            if(row<ui->tracker_scroll || row>=ui->tracker_scroll+18)ui->tracker_scroll=row;
        }
        int maximum=p->rows>18?p->rows-18:0;
        if(ui->tracker_scroll>maximum)ui->tracker_scroll=maximum;
    }
}
static void tracker_play(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,
                         TsSamplePages *pages,const TsInstrument *instrument,int rate)
{
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
static void tracker_visible(TsUiState *ui)
{
    const TsTrackerPattern *p=ts_sister_tracker_pattern_const(ui->tracker,ui->tracker->editor_pattern);
    if(!p)return;
    int row=ui->tracker->editor_row;
    if(row<ui->tracker_scroll)ui->tracker_scroll=row;
    if(row>=ui->tracker_scroll+18)ui->tracker_scroll=row-17;
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
    return -1;
}
static void tracker_clear(TsUiState *ui,TsTrackerCell *c,int whole)
{
    if(whole)memset(c,0,sizeof(*c));
    else if(ui->tracker_field==0)c->note_kind=TS_TRACKER_NOTE_NONE;
    else if(ui->tracker_field==1)c->tile_id=0;
    else c->has_volume=0;
    ui->tracker_hex_digit=0;
}
static int tracker_event(const SDL_Event *event,SDL_Window *window,SDL_AudioDeviceID device,
    AudioState *audio,TsUiState *ui,TsSamplePages *pages,TsInstrument *instrument,int rate)
{
    Uint32 id=event->type==SDL_KEYDOWN?event->key.windowID:
        event->type==SDL_MOUSEBUTTONDOWN?event->button.windowID:
        event->type==SDL_MOUSEWHEEL?event->wheel.windowID:0;
    if(id!=SDL_GetWindowID(window))return 0;
    int key=event->type==SDL_KEYDOWN;SDL_Keymod mod=key?(SDL_Keymod)event->key.keysym.mod:KMOD_NONE;
    if(key && event->key.keysym.sym==SDLK_F10 && !(mod&(KMOD_CTRL|KMOD_ALT|KMOD_GUI))) {
        if(event->key.repeat)return 1;
        if(ui_dialog_open(ui) || ui->mosaic_editing || ui->fm_open || ui->selecting ||
           ui->tape_dragging || ui->dragging_loop_endpoint || ui->wave_pointer_pending) {
            snprintf(ui->status,sizeof(ui->status),"RETURN TO SAMPLE WORKSPACE BEFORE OPENING TRACKER");return 1;
        }
        if(!pages->tracker.pattern_count && !ts_sister_tracker_add_pattern(&pages->tracker,64,
            &pages->tracker.editor_pattern,ui->status,sizeof(ui->status)))return 1;
        ui->tracker_open=!ui->tracker_open;ui->mosaic_open=0;ui->tracker_hex_digit=0;
        snprintf(ui->status,sizeof(ui->status),ui->tracker_open?
            "SELECT NOTE/TILE/VOL; USE TILE BINDS THE CURRENT SAMPLE":"SISTERTRACKER CONTINUES IN BACKGROUND");
        tracker_refresh(device,audio,ui,pages,instrument,rate);return 1;
    }
    if(!ui->tracker_open || ui_dialog_open(ui))return 0;
    TsSisterTracker *t=&pages->tracker;
    TsTrackerPattern *p=ts_sister_tracker_pattern(t,t->editor_pattern);
    if(!p)return 0;
    TsTrackerCell *c=&p->cells[t->editor_row][t->editor_lane];
    if(key) {
        SDL_Keycode sym=event->key.keysym.sym;
        /* Global save/window/panic shortcuts retain their host handlers. */
        if(mod&(KMOD_CTRL|KMOD_ALT|KMOD_GUI))return 0;
        if(sym==SDLK_ESCAPE){ui->tracker_open=0;return 1;}
        if(sym==SDLK_SPACE || sym==SDLK_RETURN) {
            if(!event->key.repeat) {
                unsigned state=atomic_load_explicit(&audio->tracker.display_running,memory_order_acquire);
                if(!state || sym==SDLK_RETURN)tracker_play(device,audio,ui,pages,instrument,rate);
                else {if(device)SDL_LockAudioDevice(device);ts_tracker_playback_pause(&audio->tracker,state==1);
                    if(device)SDL_UnlockAudioDevice(device);}
            }return 1;
        }
        if(sym>=SDLK_F1 && sym<=SDLK_F8){ts_ui_keyboard_set_octave(ui,(int)(sym-SDLK_F1));return 1;}
        if(sym==SDLK_UP || sym==SDLK_DOWN) {
            t->editor_row=(t->editor_row+(sym==SDLK_UP?-1:1)+p->rows)%p->rows;
        } else if(sym==SDLK_LEFT || sym==SDLK_RIGHT || sym==SDLK_TAB) {
            int field=t->editor_lane*3+ui->tracker_field;
            field=(field+((sym==SDLK_LEFT || (sym==SDLK_TAB && (mod&KMOD_SHIFT)))?-1:1)+24)%24;
            t->editor_lane=field/3;ui->tracker_field=field%3;
        } else if(sym==SDLK_PAGEUP || sym==SDLK_PAGEDOWN) {
            int row=t->editor_row+(sym==SDLK_PAGEUP?-18:18);
            t->editor_row=row<0?0:row>=p->rows?p->rows-1:row;
        } else if(sym==SDLK_HOME)t->editor_row=0;
        else if(sym==SDLK_END)t->editor_row=p->rows-1;
        else if(sym==SDLK_DELETE || sym==SDLK_BACKSPACE)tracker_clear(ui,c,!!(mod&KMOD_SHIFT));
        else if(ui->tracker_field==0 && note_for_key(sym)>=0) {
            int note=ui->keyboard_base_note+note_for_key(sym);
            if(note<=127){c->note_kind=TS_TRACKER_NOTE_PITCH;c->note=note;tracker_advance(ui);}return 1;
        } else if(ui->tracker_field!=0 && tracker_hex(sym)>=0) {
            if(!ui->tracker_hex_digit) {
                ui->tracker_hex_value=tracker_hex(sym)<<4;ui->tracker_hex_digit=1;return 1;
            }
            unsigned value=(unsigned)ui->tracker_hex_value|(unsigned)tracker_hex(sym);
            ui->tracker_hex_digit=0;
            if(ui->tracker_field==1) {
                if(value && !t->aliases[value]) {
                    snprintf(ui->status,sizeof(ui->status),"ALIAS %02X IS UNBOUND - USE TILE FIRST",value);return 1;
                }
                c->tile_id=t->aliases[value];
            } else {c->has_volume=1;c->volume=value>0x40?0x40:value;}
            tracker_advance(ui);return 1;
        } else return sym==SDLK_F9 || sym==SDLK_F12?0:1;
        ui->tracker_hex_digit=0;tracker_visible(ui);return 1;
    }
    if(event->type==SDL_MOUSEWHEEL) {
        ui->tracker_scroll-=event->wheel.y*3;
        if(ui->tracker_scroll<0)ui->tracker_scroll=0;
        int max=p->rows>18?p->rows-18:0;if(ui->tracker_scroll>max)ui->tracker_scroll=max;return 1;
    }
    if(event->type!=SDL_MOUSEBUTTONDOWN)return 0;
    int x,y;logical_mouse(window,event->button.x,event->button.y,&x,&y);
    int delta=event->button.button==SDL_BUTTON_RIGHT?-1:1;
    ui->tracker_hex_digit=0;
    if(y>=4 && y<28) {
        if(x>=170 && x<212)tracker_play(device,audio,ui,pages,instrument,rate);
        else if(x>=216 && x<266){if(device)SDL_LockAudioDevice(device);
            ts_tracker_playback_pause(&audio->tracker,!audio->tracker.paused);if(device)SDL_UnlockAudioDevice(device);}
        else if(x>=270 && x<312){if(device)SDL_LockAudioDevice(device);
            ts_tracker_playback_stop(&audio->tracker);if(device)SDL_UnlockAudioDevice(device);}
        else if(x>=316 && x<358)ui->tracker_open=0;
        else if(x>=362 && x<406) {
            SDL_Event save={0};save.type=SDL_KEYDOWN;save.key.windowID=SDL_GetWindowID(window);
            save.key.keysym.sym=SDLK_s;save.key.keysym.mod=KMOD_CTRL;SDL_PushEvent(&save);
        } else if(x>=410 && x<466)t->follow=!t->follow;
        else return 0;
    } else if(y>=34 && y<50) {
        if(x>=10 && x<30)tracker_select_pattern(ui,-1);
        else if(x>=144 && x<164)tracker_select_pattern(ui,1);
        else if(x>=168 && x<204) {
            if(ts_sister_tracker_add_pattern(t,64,&t->editor_pattern,ui->status,sizeof(ui->status))) {
                t->editor_row=0;ui->tracker_scroll=0;
            }
        } else if(x>=212 && x<290) {int bpm=t->bpm+delta;if(bpm>=32 && bpm<=999)t->bpm=bpm;}
        else if(x>=294 && x<350) {int tpl=t->ticks_per_line+delta;if(tpl>=1 && tpl<=31)t->ticks_per_line=tpl;}
        else if(x>=354 && x<426) {int rows=p->rows+delta*(SDL_GetModState()&KMOD_SHIFT?16:1);
            if(rows>=1 && rows<=256)ts_sister_tracker_set_rows(t,p->id,rows);}
        else if(x>=430 && x<486)t->loop=!t->loop;
    } else if(y>=65 && y<81 && x>=34 && x<626) {
        int lane=(x-34)/74,local=(x-34)%74;
        if(local<23)t->lanes[lane].muted=!t->lanes[lane].muted;
        else if(local<45){ui->tracker_solo^=(uint8_t)(1u<<lane);if(device)SDL_LockAudioDevice(device);
            ts_tracker_playback_solo(&audio->tracker,ui->tracker_solo);if(device)SDL_UnlockAudioDevice(device);}
        else {float trim=t->lanes[lane].trim+delta*.1f;t->lanes[lane].trim=fmaxf(0,fminf(2,trim));}
    } else if(y>=96 && y<312 && x>=34 && x<626) {
        int row=ui->tracker_scroll+(y-96)/12;
        if(row<p->rows){t->editor_row=row;t->editor_lane=(x-34)/74;
            int local=(x-34)%74;ui->tracker_field=local<30?0:local<50?1:2;ui->tracker_hex_digit=0;}
    } else if(y>=332 && y<356) {
        if(x>=10 && x<84) {
            int slot=instrument->selected_slot;uint8_t alias;
            if(slot<0 || !instrument->bank[slot].occupied || !instrument->current.data)
                snprintf(ui->status,sizeof(ui->status),"SELECT AN OCCUPIED SAMPLE TILE FIRST");
            else if(ts_sister_tracker_bind_tile(t,instrument->bank[slot].tile_id,&alias,ui->status,sizeof(ui->status))) {
                c->tile_id=instrument->bank[slot].tile_id;snprintf(ui->status,sizeof(ui->status),"BOUND CURRENT TILE AS %02X",alias);
            }
        } else if(x>=88 && x<140) {
            int alias=(int)tracker_alias(t,c->tile_id),step=x<112?-1:1;
            for(int n=0;n<256;++n){alias=(alias+step+256)%256;if(!alias || t->aliases[alias]){c->tile_id=t->aliases[alias];break;}}
        } else if(x>=146 && x<178){c->note_kind=TS_TRACKER_NOTE_OFF;tracker_advance(ui);}
        else if(x>=182 && x<214){c->note_kind=TS_TRACKER_NOTE_CUT;tracker_advance(ui);}
        else if(x>=218 && x<266)tracker_clear(ui,c,1);
        else if(x>=270 && x<336)t->edit_step=(t->edit_step+delta+17)%17;
        else if(x>=340 && x<412)ts_ui_keyboard_set_octave(ui,ui->keyboard_octave+delta);
        else if(x>=424 && x<570){ui->tracker_field=(x-424)/50;ui->tracker_hex_digit=0;}
    }
    tracker_visible(ui);return 1;
}
