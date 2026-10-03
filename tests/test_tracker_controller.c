#ifdef NDEBUG
#undef NDEBUG
#endif
#define SDL_MAIN_HANDLED
#define main tapesister_application_main
#include "../src/main_sdl.c"
#undef main
#include <assert.h>
static SDL_Window *window;
static AudioState *a;
static TsUiState *ui;
static TsInstrument *instrument;
static TsSamplePages *pages;
static void key(SDL_Keycode sym)
{
    SDL_Event e;SDL_zero(e);e.type=SDL_KEYDOWN;e.key.windowID=SDL_GetWindowID(window);e.key.keysym.sym=sym;
    e.key.repeat=0;
    assert(tracker_event(&e,window,0,a,ui,pages,instrument,48000));
}
static void click(int x,int y)
{
    SDL_Event e;SDL_zero(e);e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=SDL_GetWindowID(window);
    e.button.button=SDL_BUTTON_LEFT;e.button.x=x;e.button.y=y;
    assert(tracker_event(&e,window,0,a,ui,pages,instrument,48000));
}
static void modified(SDL_Keycode sym,SDL_Keymod mod)
{
    SDL_Event e;SDL_zero(e);e.type=SDL_KEYDOWN;e.key.windowID=SDL_GetWindowID(window);
    e.key.keysym.sym=sym;e.key.keysym.mod=mod;
    assert(tracker_event(&e,window,0,a,ui,pages,instrument,48000));
}
static void wheel(int x,int y,int detents,int flipped,float precise)
{
    SDL_Event e;SDL_zero(e);e.type=SDL_MOUSEWHEEL;e.wheel.windowID=SDL_GetWindowID(window);
    e.wheel.y=detents;e.wheel.direction=flipped?SDL_MOUSEWHEEL_FLIPPED:SDL_MOUSEWHEEL_NORMAL;
#if SDL_VERSION_ATLEAST(2,26,0)
    e.wheel.mouseX=x;e.wheel.mouseY=y;
#else
    SDL_WarpMouseInWindow(window,x,y);
#endif
#if SDL_VERSION_ATLEAST(2,0,18)
    e.wheel.preciseY=precise;
#endif
    assert(tracker_event(&e,window,0,a,ui,pages,instrument,48000));
}
static void fasttracks_controls(void)
{
    TsSisterTracker *t=ui->tracker;TsTrackerEdit *edit=ui->tracker_edit;
    TsTrackerPattern *p=ts_sister_tracker_pattern(t,t->editor_pattern);
    TsTrackerPattern saved=*p;
    TsTrackerLane lanes[8];memcpy(lanes,t->lanes,sizeof(lanes));
    int control=t->control_lane,uses=t->fasttracks_uses_length,bypass=t->length_bypass;
    int tpl=t->ticks_per_line,bpm=t->bpm,expanded=ui->tracker_expanded;
    ts_tracker_playback_stop(&a->tracker);edit->tools_open=0;
    for(int view=0;view<2;++view) {
        ui->tracker_expanded=view;TsTrackerLayout g=ts_tracker_layout(t,view);
        t->lanes[0].length=0;t->lanes[0].mode=TS_TRACKER_STANDARD;t->lanes[0].ratio=7;t->lanes[0].direction=TS_TRACKER_FORWARD;
        unsigned row=t->editor_row,lane=t->editor_lane;
        click(TS_TRACKER_LANE_X+20,g.len_y+2);assert(t->lanes[0].length==1);
        SDL_SetModState(KMOD_SHIFT);ui->wheel_guard=(TsUiWheelGuard){0};
        wheel(TS_TRACKER_LANE_X+20,g.len_y+2,2,0,2);assert(t->lanes[0].length==17);
        SDL_SetModState(KMOD_CTRL);wheel(TS_TRACKER_LANE_X+20,g.len_y+2,-1,0,-1);assert(!t->lanes[0].length);
        SDL_SetModState(KMOD_NONE);t->lanes[0].length=255;
        wheel(TS_TRACKER_LANE_X+20,g.len_y+2,4,0,4);assert(t->lanes[0].length==256);
        t->control_lane=-1;click(TS_TRACKER_LANE_X+65,g.len_y+2);assert(t->control_lane==0);
        click(TS_TRACKER_LANE_X+65,g.len_y+2);assert(t->control_lane==-1);
        click(TS_TRACKER_LANE_X+3,g.ratio_y+2);assert(t->lanes[0].mode==TS_TRACKER_PATTERN);
        ui->wheel_guard=(TsUiWheelGuard){0};wheel(TS_TRACKER_LANE_X+23,g.ratio_y+2,-1,1,-1);assert(t->lanes[0].ratio==8);
        click(TS_TRACKER_LANE_X+30,g.ratio_y+2);assert(t->lanes[0].direction==TS_TRACKER_REVERSE);
        SDL_Event e;SDL_zero(e);e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=SDL_GetWindowID(window);
        e.button.button=SDL_BUTTON_RIGHT;e.button.x=TS_TRACKER_LANE_X+23;e.button.y=g.ratio_y+2;
        assert(tracker_event(&e,window,0,a,ui,pages,instrument,48000));assert(t->lanes[0].direction==TS_TRACKER_PING_PONG);
        modified(SDLK_1,KMOD_CTRL|KMOD_SHIFT);assert(t->lanes[0].mode==TS_TRACKER_STANDARD);
        modified(SDLK_1,KMOD_ALT|KMOD_SHIFT);assert(t->lanes[0].ratio==9);
        assert(t->editor_row==row && t->editor_lane==lane); /* Header controls preserve edit focus. */
        t->length_bypass=0;click(8,g.len_y+2);assert(t->length_bypass);
        t->fasttracks_uses_length=1;click(8,g.ratio_y+2);assert(!t->fasttracks_uses_length);
        edit->tools_open=1;click(TS_TRACKER_LANE_X+3,g.ratio_y+2);
        modified(SDLK_1,KMOD_CTRL|KMOD_SHIFT);assert(t->lanes[0].mode==TS_TRACKER_STANDARD);edit->tools_open=0;
    }
    memset(p->cells,0,sizeof(p->cells));p->rows=8;t->ticks_per_line=1;t->bpm=125;t->control_lane=-1;t->length_bypass=0;
    for(int i=0;i<8;++i){t->lanes[i].length=0;t->lanes[i].mode=TS_TRACKER_STANDARD;}
    unsigned alias=tracker_bind_sample(ui,instrument,instrument->selected_slot);assert(alias);
    for(int row=0;row<8;++row)p->cells[row][0]=(TsTrackerCell){.tile_id=t->aliases[alias],.note_kind=TS_TRACKER_NOTE_PITCH,.note=60+row};
    t->lanes[0].mode=TS_TRACKER_PATTERN;t->lanes[0].ratio=14;t->lanes[0].direction=TS_TRACKER_FORWARD;
    tracker_play(0,a,ui,pages,instrument,48000);
    for(int frame=0;frame<961;++frame)(void)ts_tracker_playback_read(&a->tracker,48000);
    ts_tracker_playback_end_block(&a->tracker);tracker_refresh(0,a,ui,pages,instrument,48000);
    assert(ui->tracker_lane_row[0]==2 && ui->tracker_lane_row[1]==1 && a->tracker.lanes[0].notes_started==3);
    uint64_t count=a->tracker.lanes[0].notes_started;double position=a->tracker.lanes[0].voice.position;
    t->control_lane=0;t->fasttracks_uses_length=1;t->length_bypass=1;
    tracker_refresh(0,a,ui,pages,instrument,48000);
    assert(a->tracker.prepared->control_lane==0 && a->tracker.prepared->fasttracks_uses_length && a->tracker.prepared->length_bypass);
    assert(a->tracker.lanes[0].notes_started==count && a->tracker.lanes[0].voice.position==position);
    ts_tracker_playback_stop(&a->tracker);*p=saved;memcpy(t->lanes,lanes,sizeof(lanes));
    t->control_lane=control;t->fasttracks_uses_length=uses;t->length_bypass=bypass;t->ticks_per_line=tpl;t->bpm=bpm;
    ui->tracker_expanded=expanded;SDL_SetModState(KMOD_NONE);ui->wheel_guard=(TsUiWheelGuard){0};
    tracker_refresh(0,a,ui,pages,instrument,48000);
}
static void editor_controls(void)
{
    TsSisterTracker *t=&pages->tracker;TsTrackerEdit *edit=ui->tracker_edit;
    TsTrackerPattern *p=ts_sister_tracker_pattern(t,t->editor_pattern);
    ts_tracker_edit_reset(edit);t->editor_row=0;t->editor_lane=0;ui->tracker_scroll=0;
    /* The formerly hidden CDP and Sister-logo hits are owned by Tracker. */
    click(492,15);click(15,15);assert(!ui->portal.open);
    SDL_Event e;SDL_zero(e);e.type=SDL_KEYDOWN;e.key.windowID=SDL_GetWindowID(window);e.key.keysym.sym=SDLK_TAB;
    assert(!tracker_event(&e,window,0,a,ui,pages,instrument,48000));
    e.key.keysym.sym=SDLK_s;e.key.keysym.mod=KMOD_CTRL;
    assert(!tracker_event(&e,window,0,a,ui,pages,instrument,48000));
    e.key.keysym.sym=SDLK_n; /* Hidden Sample normalize must never run. */
    assert(tracker_event(&e,window,0,a,ui,pages,instrument,48000));
    SDL_FlushEvents(SDL_FIRSTEVENT,SDL_LASTEVENT);click(478,15);
    assert(SDL_PeepEvents(&e,1,SDL_GETEVENT,SDL_KEYDOWN,SDL_KEYDOWN)==1 && e.key.keysym.sym==SDLK_TAB);
    /* Fresh aliases can be entered directly, including keypad digits. */
    assert(ts_instrument_copy_selected(instrument,1,ui->status,sizeof(ui->status)));
    assert(!t->aliases[2]);click(TS_TRACKER_LANE_X+29,TS_TRACKER_GRID_Y+TS_TRACKER_ROW_HEIGHT*0+2);key(SDLK_KP_0);key(SDLK_KP_2);
    assert(p->cells[0][0].tile_id==instrument->bank[1].tile_id && t->aliases[2]==instrument->bank[1].tile_id);
    /* A stale/missing alias must never be rebound to a red Sample selection. */
    TsTileId missing=ts_tile_id_new();t->aliases[3]=missing;click(TS_TRACKER_LANE_X+29,TS_TRACKER_GRID_Y+TS_TRACKER_ROW_HEIGHT*1+2);key(SDLK_0);key(SDLK_3);
    assert(p->cells[1][0].tile_id==missing && t->aliases[3]==missing);
    click(TS_TRACKER_LANE_X+29,TS_TRACKER_GRID_Y+TS_TRACKER_ROW_HEIGHT*2+2);key(SDLK_KP_2);key(SDLK_KP_ENTER);assert(p->cells[2][0].tile_id==t->aliases[2]);
    /* Silent edits, clipboard and history cannot stop or reposition audio. */
    tracker_play(0,a,ui,pages,instrument,48000);float block[1024];audio_callback(a,(Uint8*)block,sizeof(block));
    double phase=a->tracker.lanes[0].voice.position;unsigned row=a->tracker.row;
    click(38,TS_TRACKER_GRID_Y+TS_TRACKER_ROW_HEIGHT*0+2);modified(SDLK_DOWN,KMOD_SHIFT);modified(SDLK_RIGHT,KMOD_SHIFT);
    assert(edit->selected && t->editor_row==1 && t->editor_lane==1);
    modified(SDLK_c,KMOD_CTRL);assert(edit->clipboard_rows==2 && edit->clipboard_lanes==2);
    click(TS_TRACKER_LANE_X+4+TS_TRACKER_LANE_WIDTH*2,TS_TRACKER_GRID_Y+TS_TRACKER_ROW_HEIGHT*4+2);modified(SDLK_v,KMOD_CTRL);
    assert(p->cells[4][2].tile_id==p->cells[0][0].tile_id);
    modified(SDLK_z,KMOD_CTRL);assert(!p->cells[4][2].tile_id);
    modified(SDLK_y,KMOD_CTRL);assert(p->cells[4][2].tile_id==p->cells[0][0].tile_id);
    assert(a->tracker.running && a->tracker.row==row && a->tracker.lanes[0].voice.position==phase);
    modified(SDLK_F4,KMOD_CTRL);assert(edit->clipboard_rows==p->rows && edit->clipboard_lanes==8);
    modified(SDLK_F4,KMOD_SHIFT);assert(edit->clipboard_lanes==1);
    p->cells[4][2].note_kind=TS_TRACKER_NOTE_PITCH;p->cells[4][2].note=60;
    modified(SDLK_F2,KMOD_SHIFT);assert(p->cells[4][2].note==61);
    key(SDLK_INSERT);assert(!p->cells[4][2].note_kind && p->cells[5][2].note==61);
    key(SDLK_BACKSPACE);assert(p->cells[4][2].note==61 && !p->cells[5][2].note_kind);
    /* Drag selection owns motion and release even over hidden Sample tiles. */
    click(38,TS_TRACKER_GRID_Y+TS_TRACKER_ROW_HEIGHT*0+2);SDL_zero(e);e.type=SDL_MOUSEMOTION;e.motion.windowID=SDL_GetWindowID(window);
    e.motion.x=TS_TRACKER_LANE_X+4+TS_TRACKER_LANE_WIDTH;e.motion.y=TS_TRACKER_GRID_Y+TS_TRACKER_ROW_HEIGHT*3+2;e.motion.state=SDL_BUTTON_LMASK;
    assert(tracker_event(&e,window,0,a,ui,pages,instrument,48000));assert(edit->selected && t->editor_row==3 && t->editor_lane==1);
    /* Dragging back to the starting cell must shrink the mark again. */
    e.motion.x=38;e.motion.y=TS_TRACKER_GRID_Y+2;
    assert(tracker_event(&e,window,0,a,ui,pages,instrument,48000));
    TsTrackerRegion returned=ts_tracker_edit_region(edit,t);
    assert(returned.row0==0 && returned.row1==0 && returned.lane0==0 && returned.lane1==0);
    e.motion.x=TS_TRACKER_LANE_X+4+TS_TRACKER_LANE_WIDTH;e.motion.y=TS_TRACKER_GRID_Y+3*TS_TRACKER_ROW_HEIGHT+2;
    assert(tracker_event(&e,window,0,a,ui,pages,instrument,48000));
    e.type=SDL_MOUSEBUTTONUP;e.button.windowID=SDL_GetWindowID(window);e.button.button=SDL_BUTTON_LEFT;
    assert(tracker_event(&e,window,0,a,ui,pages,instrument,48000) && !edit->dragging);
    /* One held ROWS gesture makes one undo step and stops on release/leave. */
    unsigned history=edit->position;click(370,40);assert(p->rows==65);
    Uint32 due=ui->tracker_repeat_next;tracker_repeat_poll(ui,due-1,SDL_BUTTON_LMASK);assert(p->rows==65);
    tracker_repeat_poll(ui,due,SDL_BUTTON_LMASK);assert(p->rows==66);
    tracker_repeat_poll(ui,due+65,SDL_BUTTON_LMASK);assert(p->rows==67);
    tracker_repeat_poll(ui,due+66,0);assert(!ui->tracker_repeat_control && edit->position==history+1);
    modified(SDLK_z,KMOD_CTRL);assert(p->rows==64);
    SDL_zero(e);e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=SDL_GetWindowID(window);
    e.button.button=SDL_BUTTON_RIGHT;e.button.x=310;e.button.y=40;unsigned tpl=t->ticks_per_line;
    assert(tracker_event(&e,window,0,a,ui,pages,instrument,48000));tracker_repeat_poll(ui,ui->tracker_repeat_next,SDL_BUTTON_RMASK);
    assert(t->ticks_per_line==(tpl>2?tpl-2:1));
    e.type=SDL_WINDOWEVENT;e.window.windowID=SDL_GetWindowID(window);e.window.event=SDL_WINDOWEVENT_LEAVE;
    assert(!tracker_event(&e,window,0,a,ui,pages,instrument,48000) && !ui->tracker_repeat_control);
    t->bpm=998;click(230,40);tracker_repeat_poll(ui,ui->tracker_repeat_next,SDL_BUTTON_LMASK);assert(t->bpm==999);
    tracker_repeat_end(ui);
    /* Wheel follows hover, SDL direction and detents; FOLLOW cannot snap back. */
    ui->wheel_guard=(TsUiWheelGuard){0};ui->tracker_scroll=10;t->follow=1;
    wheel(50,150,2,0,2);assert(ui->tracker_scroll==8 && ui->tracker_follow_hold);
    tracker_refresh(0,a,ui,pages,instrument,48000);assert(ui->tracker_scroll==8);
    wheel(50,150,2,1,2);assert(ui->tracker_scroll==10);
    wheel(50,150,0,0,.3f);assert(ui->tracker_scroll==10); /* SDL accumulates fractional detents. */
    unsigned bpm=t->bpm;wheel(230,40,-1,0,-1);assert(t->bpm==bpm); /* Protect previous wheel owner. */
    ui->wheel_guard=(TsUiWheelGuard){0};wheel(230,40,-1,0,-1);assert(t->bpm==bpm-1 && ui->tracker_scroll==10);
    ui->wheel_guard=(TsUiWheelGuard){0};float trim=t->lanes[0].trim;wheel(92,TS_TRACKER_MIX_Y+7,0,0,.5f);
    assert(fabs(t->lanes[0].trim-trim-.05f)<1e-6);
    ui->wheel_guard=(TsUiWheelGuard){0};wheel(480,15,-1,0,-1);assert(!ui->portal.open && ui->tracker_scroll==10);
    char target[96];assert(!ts_ui_midi_target_from_point(ui,15,342,target,sizeof(target)));
    assert(ts_ui_midi_target_from_point(ui,540,15,target,sizeof(target)) && !strcmp(target,"main.master_output"));
    ui->midi_learn_active=1;SDL_zero(e);e.type=SDL_KEYDOWN;e.key.windowID=SDL_GetWindowID(window);e.key.keysym.sym=SDLK_ESCAPE;
    assert(!tracker_event(&e,window,0,a,ui,pages,instrument,48000));ui->midi_learn_active=0;
    click(425,14);assert(t->follow && !ui->tracker_follow_hold);
    /* Visible editing panel uses the same operations as keyboard shortcuts. */
    click(600,342);assert(edit->tools_open);click(425,146);assert(edit->position==history-1);
    click(545,118);assert(!edit->tools_open);ui->tracker_scroll=0;t->editor_row=0;t->editor_lane=0;ui->tracker_field=1;
    ui->tracker_hex_digit=1;ui->tracker_hex_value=0x20;ui->tracker_cursor_visible=1;
}
static void tapehead_columns_and_overlay(void)
{
    ui->tracker_follow_hold=1; /* This fixture edits a manually scrolled grid. */
    TsSisterTracker *t=ui->tracker;TsTrackerPattern *p=ts_sister_tracker_pattern(t,t->editor_pattern);
    ui->tracker_scroll=0;t->editor_row=0;t->editor_lane=0;ui->tracker_hex_digit=0;
    ts_tracker_edit_anchor(ui->tracker_edit,t);
    click(TS_TRACKER_LANE_X+46,TS_TRACKER_GRID_Y+2);assert(ui->tracker_field==3);
    key(SDLK_m);assert(ui->tracker_field==4 && p->cells[0][0].tune_command=='M');
    key(SDLK_3);assert(ui->tracker_hex_digit==1);key(SDLK_c);
    assert(p->cells[0][0].tune_value==60 && t->editor_row==1);
    key(SDLK_f);key(SDLK_f);assert(!p->cells[1][0].tune_command && t->editor_row==1);
    click(TS_TRACKER_LANE_X+46,TS_TRACKER_GRID_Y+TS_TRACKER_ROW_HEIGHT+2);key(SDLK_m);key(SDLK_f);key(SDLK_f);
    assert(p->cells[1][0].tune_value==0 && t->editor_row==1); /* M range survives project validation. */
    key(SDLK_LEFT);key(SDLK_n);key(SDLK_f);key(SDLK_f);
    assert(p->cells[1][0].tune_command=='N' && p->cells[1][0].tune_value==255 && t->editor_row==2);
    click(TS_TRACKER_LANE_X+59,TS_TRACKER_GRID_Y+2);key(SDLK_z);key(SDLK_0);key(SDLK_0);
    assert(p->cells[0][0].fx_command=='Z' && p->cells[0][0].fx_value==0);
    click(TS_TRACKER_LANE_X+59,TS_TRACKER_GRID_Y+TS_TRACKER_ROW_HEIGHT+2);key(SDLK_KP_0);key(SDLK_KP_0);key(SDLK_KP_0);
    assert(p->cells[1][0].fx_command=='0' && p->cells[1][0].fx_value==0); /* Explicit 000 is not empty. */
    modified(SDLK_z,KMOD_CTRL);assert(!p->cells[1][0].fx_command); /* Unchanged 00 parameter adds no history. */
    modified(SDLK_y,KMOD_CTRL);assert(p->cells[1][0].fx_command=='0');
    ui->tracker_field=6;t->editor_lane=7;key(SDLK_RIGHT);assert(t->editor_lane==0 && ui->tracker_field==0);
    key(SDLK_LEFT);assert(t->editor_lane==7 && ui->tracker_field==6);
    click(TS_TRACKER_LANE_X+TS_TRACKER_LANE_WIDTH*7+63,TS_TRACKER_GRID_Y+2);
    assert(t->editor_lane==7 && ui->tracker_field==6);
    char error[128];assert(ts_sister_tracker_validate(t,error,sizeof(error)));
    /* Exercise the late native audio UI overlay: selected red border, Sister
       routing outlines, locked dimming and performance-source borders. */
    TsFramebuffer *fb=malloc(sizeof(*fb)),*before=malloc(sizeof(*before));assert(fb && before);
    ui->show_keyboard=ui->show_recipes=ui->show_ingredients=0;
    ui->sister_source_mask=15;instrument->bank[0].locked=1;
    int old_dim=ts_locked_tile_dim_percent;ts_locked_tile_dim_percent=50;
    void *old_output=ts_output_userdata;ts_output_userdata=a;
    a->performance_group_latched=1;a->performance_source_mask=3;
    ts_ui_render(fb,ui,instrument);memcpy(before,fb,sizeof(*fb));
    a->performance_group_latched=1;a->performance_source_mask=3;
    ts_overlay_tile_states(fb,ui,instrument);assert(!memcmp(before,fb,sizeof(*fb)));
    /* Sample still draws its visible identity and routing state. */
    ui->tracker_open=0;ts_overlay_tile_states(fb,ui,instrument);assert(memcmp(before,fb,sizeof(*fb)));
    ui->tracker_open=1;ui->sister_source_mask=0;instrument->bank[0].locked=0;
    a->performance_group_latched=0;a->performance_source_mask=0;ts_locked_tile_dim_percent=old_dim;ts_output_userdata=old_output;
    free(before);free(fb);
    t->editor_row=0;t->editor_lane=0;ui->tracker_field=1;ui->tracker_hex_digit=0;
    ts_tracker_edit_anchor(ui->tracker_edit,t);
}
static void independent_marks_and_layout(void)
{
    TsSisterTracker *t=ui->tracker;TsTrackerEdit *e=ui->tracker_edit;
    TsTrackerPattern *p=ts_sister_tracker_pattern(t,t->editor_pattern);
    ts_tracker_playback_stop(&a->tracker);tracker_refresh(0,a,ui,pages,instrument,48000);ui->tracker_scroll=0;t->editor_row=2;t->editor_lane=2;
    ts_tracker_edit_anchor(e,t);modified(SDLK_DOWN,KMOD_SHIFT);modified(SDLK_RIGHT,KMOD_SHIFT);
    TsTrackerRegion expected=ts_tracker_edit_region(e,t);assert(expected.row0==2 && expected.row1==3 && expected.lane0==2 && expected.lane1==3);
    key(SDLK_UP);key(SDLK_RIGHT);click(38,TS_TRACKER_GRID_Y+8*12+2);key(SDLK_z);
    TsTrackerRegion actual=ts_tracker_edit_region(e,t);assert(!memcmp(&actual,&expected,sizeof(actual)));
    assert(t->editor_row==9 && !t->editor_lane); /* Plain click, entry and auto-advance keep marks. */
    p->cells[2][2].tile_id=t->aliases[1];p->cells[2][2].note_kind=TS_TRACKER_NOTE_PITCH;p->cells[2][2].note=60;
    modified(SDLK_c,KMOD_CTRL);modified(SDLK_v,KMOD_CTRL);assert(p->cells[9][0].tile_id==t->aliases[1]);
    unsigned count=t->pattern_count;TsPatternId source=t->editor_pattern;key(SDLK_F8);
    assert(t->pattern_count==count+1 && t->editor_pattern==source && t->editor_row==9);
    TsPatternId made=t->patterns[t->pattern_count-1]->id;modified(SDLK_z,KMOD_CTRL);assert(!ts_sister_tracker_pattern(t,made));
    modified(SDLK_y,KMOD_CTRL);assert(ts_sister_tracker_pattern(t,made));
    modified(SDLK_l,KMOD_CTRL);assert(a->tracker.block_active && a->tracker.block.row0==2 && a->tracker.block.row1==3);
    float block[2];audio_callback(a,(Uint8*)block,sizeof(block));assert(a->tracker.row==2);
    key(SDLK_HOME);key(SDLK_RIGHT);assert(!a->tracker.block_pending);
    modified(SDLK_DOWN,KMOD_SHIFT);assert(e->end_row==4 && a->tracker.block_pending && a->tracker.block.row1==3);
    TsPatternId other=t->patterns[t->pattern_count-1]->id;t->editor_pattern=other;
    assert(!ts_tracker_edit_selected(e,t));tracker_refresh(0,a,ui,pages,instrument,48000);
    assert(a->tracker.prepared->pattern.id==source);
    t->editor_row=0;t->editor_lane=0;modified(SDLK_DOWN,KMOD_SHIFT);
    assert(a->tracker.pending_block.pattern==source && a->tracker.pending_block.row1==4);
    t->editor_pattern=source;t->editor_row=0;ui->tracker_scroll=0;
    modified(SDLK_l,KMOD_CTRL);assert(!a->tracker.running);tracker_refresh(0,a,ui,pages,instrument,48000);
    modified(SDLK_c,KMOD_ALT);actual=ts_tracker_edit_region(e,t);assert(actual.row0==0 && actual.row1==p->rows-1 && actual.lane0==0 && actual.lane1==0);
    key(SDLK_ESCAPE);assert(!e->selected && ui->tracker_open);
    TsTrackerLayout normal=ts_tracker_layout(t,0);assert(normal.rows==19);
    ui->master_output_dragging=1;
    modified(SDLK_BACKSPACE,KMOD_CTRL|KMOD_ALT);assert(ui->tracker_expanded && !ui->master_output_dragging);
    TsTrackerLayout full=ts_tracker_layout(t,1);assert(full.rows==29 && full.grid_y<normal.grid_y);
    unsigned bpm=t->bpm;click(230,40);assert(t->bpm==bpm && t->editor_row==0); /* Old BPM coordinate is grid now. */
    click(TS_TRACKER_LANE_X+3+7*TS_TRACKER_LANE_WIDTH,full.grid_y+(full.rows-1)*12+2);
    assert(t->editor_row==28 && t->editor_lane==7);
    click(TS_TRACKER_LANE_X+3,full.mix_y+3);assert(t->lanes[0].muted);click(TS_TRACKER_LANE_X+3,full.mix_y+3);
    click(TS_TRACKER_LANE_X+15,full.mix_y+3);assert(ui->tracker_solo&1);click(TS_TRACKER_LANE_X+15,full.mix_y+3);
    char target[96];assert(!ts_ui_midi_target_from_point(ui,540,15,target,sizeof(target)));
    SisterWindow *sister=calloc(1,sizeof(*sister));assert(sister);SDL_Event ev;SDL_zero(ev);
    ev.type=SDL_MOUSEBUTTONDOWN;ev.button.windowID=SDL_GetWindowID(window);ev.button.button=SDL_BUTTON_LEFT;ev.button.x=505;ev.button.y=15;
    assert(!master_eq_event(&ev,window,0,a,ui,sister) && !ui->master_eq_open);free(sister);
    snprintf(t->lanes[3].name,sizeof(t->lanes[3].name),"BELL CLOUD");TsTrackerLayout named=ts_tracker_layout(t,1);
    assert(named.name_y>=0 && named.grid_y==full.grid_y+10 && named.rows==28);
    click(TS_TRACKER_LANE_X+27+3*TS_TRACKER_LANE_WIDTH,named.grid_y+12+2);assert(t->editor_row==1 && t->editor_lane==3 && ui->tracker_field==1);
    snprintf(t->lanes[3].name,sizeof(t->lanes[3].name),"TRACK 4");
    modified(SDLK_e,KMOD_CTRL);assert(e->tools_open);key(SDLK_ESCAPE);assert(!e->tools_open && ui->tracker_expanded);
    modified(SDLK_BACKSPACE,KMOD_CTRL|KMOD_ALT);assert(!ui->tracker_expanded);
    ui->tracker_scroll=0;t->editor_row=0;t->editor_lane=0;ui->tracker_field=1;
}
static int pixels_of_color(const TsFramebuffer *fb,int x,int y,int w,int h,uint32_t color)
{
    int count=0;
    for(int yy=y;yy<y+h;++yy)for(int xx=x;xx<x+w;++xx)count+=fb->pixels[yy*640+xx]==color;
    return count;
}
static void tapehead_transport_visuals(void)
{
    ts_tracker_playback_stop(&a->tracker);tracker_refresh(0,a,ui,pages,instrument,48000);
    TsSisterTracker *t=ui->tracker,saved_tracker=*t;
    TsTrackerPattern *p=ts_sister_tracker_pattern(t,t->editor_pattern),saved_pattern=*p;
    TsUiState *saved_ui=malloc(sizeof(*ui));TsFramebuffer *fb=malloc(sizeof(*fb));assert(saved_ui && fb);
    *saved_ui=*ui;
    ui->tracker_edit->tools_open=0;ts_tracker_edit_anchor(ui->tracker_edit,t);
    p->rows=64;memset(p->cells,0,sizeof(p->cells));
    for(int lane=0;lane<8;++lane) {
        t->lanes[lane].length=0;t->lanes[lane].mode=TS_TRACKER_STANDARD;t->lanes[lane].muted=0;
        for(int row=0;row<64;++row)p->cells[row][lane]=(TsTrackerCell){.note_kind=TS_TRACKER_NOTE_PITCH,.note=60,.has_volume=1,.volume=32};
    }
    t->follow=1;t->control_lane=3;t->length_bypass=0;t->fasttracks_uses_length=1;
    t->lanes[1].length=4;t->lanes[2].mode=TS_TRACKER_PATTERN;
    t->lanes[3].length=7;t->lanes[3].mode=TS_TRACKER_PATTERN;
    t->lanes[4].length=6;t->lanes[4].mode=TS_TRACKER_PATTERN;
    ui->tracker_running=1;ui->tracker_heard_pattern=p->id;ui->tracker_block=0;ui->tracker_follow_hold=0;
    ui->tracker_master_row=12;ui->tracker_row=2; /* CONTROL must not move the master band. */
    ui->tracker_breathe=255;ui->tracker_hex_digit=0;ui->tracker_cursor_visible=0;
    t->editor_row=60;t->editor_lane=7;
    ui->tracker_lane_row[0]=12;ui->tracker_lane_row[1]=1;ui->tracker_lane_row[2]=3;
    ui->tracker_lane_row[3]=2;ui->tracker_lane_row[4]=2;
    for(int expanded=0;expanded<2;++expanded) {
        ui->tracker_expanded=expanded;TsTrackerLayout g=ts_tracker_layout(t,expanded);
        ts_ui_render(fb,ui,instrument);
        int band_y=g.grid_y+(g.rows/2)*TS_TRACKER_ROW_HEIGHT;
        assert(fb->pixels[band_y*640+5]==ui->palette.colors[TS_PALETTE_DESKTOP]);
        const TsPaletteColor heads[]={TS_PALETTE_TRACK_LENGTH_PLAYHEAD,TS_PALETTE_FASTTRACKS_PLAYHEAD,
            TS_PALETTE_CONTROL_PLAYHEAD,TS_PALETTE_FASTTRACKS_LENGTH_PLAYHEAD};
        const int rows[]={1,3,2,2};
        for(int i=0;i<4;++i) {
            int x=TS_TRACKER_LANE_X+(i+1)*TS_TRACKER_LANE_WIDTH+1,y=g.grid_y+rows[i]*TS_TRACKER_ROW_HEIGHT;
            uint32_t color=ui->palette.colors[heads[i]];
            assert(fb->pixels[y*640+x]==color);
            assert(fb->pixels[(y+TS_TRACKER_ROW_HEIGHT-1)*640+x+TS_TRACKER_LANE_WIDTH-3]==color);
        }
        /* The same screen slot is row 3 in a private lane, but follows master
           row 12 in the normal lane. Click and drag must use those sources. */
        click(TS_TRACKER_LANE_X+2*TS_TRACKER_LANE_WIDTH+10,g.grid_y+5*TS_TRACKER_ROW_HEIGHT+2);
        assert(t->editor_row==5 && t->editor_lane==2 && !ui->tracker_follow_hold);
        SDL_Event motion;SDL_zero(motion);motion.type=SDL_MOUSEMOTION;
        motion.motion.windowID=SDL_GetWindowID(window);motion.motion.state=SDL_BUTTON_LMASK;
        motion.motion.x=TS_TRACKER_LANE_X+TS_TRACKER_LANE_WIDTH+10;
        motion.motion.y=g.grid_y+2*TS_TRACKER_ROW_HEIGHT+2;
        assert(tracker_event(&motion,window,0,a,ui,pages,instrument,48000));
        TsTrackerRegion dragged=ts_tracker_edit_region(ui->tracker_edit,t);
        assert(dragged.row0==2 && dragged.row1==5 && dragged.lane0==1 && dragged.lane1==2);
        ts_tracker_edit_anchor(ui->tracker_edit,t);
        click(TS_TRACKER_LANE_X+10,band_y+2);assert(t->editor_row==12 && !ui->tracker_follow_hold);
        SDL_SetModState(KMOD_SHIFT);
        click(TS_TRACKER_LANE_X+TS_TRACKER_LANE_WIDTH+10,g.grid_y+2*TS_TRACKER_ROW_HEIGHT+2);
        SDL_SetModState(KMOD_NONE);
        TsTrackerRegion mark=ts_tracker_edit_region(ui->tracker_edit,t);
        assert(mark.row0==2 && mark.row1==12 && mark.lane0==0 && mark.lane1==1);
        ts_tracker_edit_anchor(ui->tracker_edit,t);
        t->editor_row=60;t->editor_lane=7;
        /* Private text stays put through master movement; it pages only when
           that lane's source head crosses the next page boundary. */
        ui->tracker_master_row=13;
        click(TS_TRACKER_LANE_X+TS_TRACKER_LANE_WIDTH+10,g.grid_y+2*TS_TRACKER_ROW_HEIGHT+2);
        assert(t->editor_row==2);
        ui->tracker_lane_row[2]=g.rows;
        click(TS_TRACKER_LANE_X+2*TS_TRACKER_LANE_WIDTH+10,g.grid_y+2);
        assert(t->editor_row==(unsigned)g.rows);
        ui->tracker_lane_row[2]=3;ui->tracker_master_row=12;
    }
    ui->tracker_expanded=0;TsTrackerLayout g=ts_tracker_layout(t,0);
    t->editor_row=60;t->editor_lane=7;ts_tracker_edit_anchor(ui->tracker_edit,t);
    ts_ui_render(fb,ui,instrument);
    int x=TS_TRACKER_LANE_X+TS_TRACKER_LANE_WIDTH;
    /* Exact default-palette dim: (orange + 2*desktop)/3 = 106,79,31. */
    assert(pixels_of_color(fb,x+2,g.grid_y+2*12+2,24,8,0xffffae20u)>0);
    assert(pixels_of_color(fb,x+2,g.grid_y+5*12+2,24,8,0xff6a4f1fu)>0);
    x=TS_TRACKER_LANE_X+4*TS_TRACKER_LANE_WIDTH;
    assert(pixels_of_color(fb,x+2,g.grid_y+8*12+2,24,8,0xff154a60u)>0); /* Dim FT text. */
    t->fasttracks_uses_length=0;ts_ui_render(fb,ui,instrument);
    assert(pixels_of_color(fb,x+2,g.grid_y+8*12+2,24,8,ui->palette.colors[TS_PALETTE_TEXT_ON_BLOCK])>0);
    assert(fb->pixels[(g.grid_y+2*12)*640+x+1]==ui->palette.colors[TS_PALETTE_FASTTRACKS_PLAYHEAD]);
    /* Bypass restores normal scrolling and full field colors for LEN lanes. */
    t->length_bypass=1;ts_ui_render(fb,ui,instrument);
    x=TS_TRACKER_LANE_X+TS_TRACKER_LANE_WIDTH;
    assert(pixels_of_color(fb,x+2,g.grid_y+5*12+2,24,8,0xffffae20u)>0);
    t->length_bypass=0;t->fasttracks_uses_length=1;
    /* A manually scrolled view and another edited pattern use ordinary cells. */
    ui->tracker_follow_hold=1;ui->tracker_scroll=20;
    click(TS_TRACKER_LANE_X+2*TS_TRACKER_LANE_WIDTH+10,g.grid_y+2);assert(t->editor_row==20);
    ui->tracker_follow_hold=0;ui->tracker_heard_pattern=p->id+100;
    click(TS_TRACKER_LANE_X+2*TS_TRACKER_LANE_WIDTH+10,g.grid_y+2);assert(t->editor_row==20);
    ui->tracker_heard_pattern=p->id;
    /* Literal block audition has no inactive-LEN shading or private pages. */
    ui->tracker_block=1;ui->tracker_block_rows=2|(5<<8);ui->tracker_block_lanes=0|(7<<8);
    ui->tracker_master_row=3;
    click(TS_TRACKER_LANE_X+2*TS_TRACKER_LANE_WIDTH+10,g.grid_y+(g.rows/2)*12+2);
    assert(t->editor_row==3);ui->tracker_block=0;
    /* Extended blank rows can be displayed but never edit retained hidden data. */
    p->rows=4;t->lanes[2].length=64;ui->tracker_lane_row[2]=25;
    unsigned before=t->editor_row;
    click(TS_TRACKER_LANE_X+2*TS_TRACKER_LANE_WIDTH+10,g.grid_y+2);
    assert(t->editor_row==before);
    ui->tracker_edit->dragging=0;*p=saved_pattern;*t=saved_tracker;*ui=*saved_ui;
    ts_tracker_edit_anchor(ui->tracker_edit,t);free(saved_ui);free(fb);
}
static void block_capture_frames(void)
{
    TsSisterTracker *t=ui->tracker;TsTrackerPattern *p=ts_sister_tracker_pattern(t,t->editor_pattern);
    t->bpm=125;t->ticks_per_line=1;t->editor_row=2;t->editor_lane=2;
    p->cells[0][2].tile_id=t->aliases[1];p->cells[2][2]=(TsTrackerCell){.note_kind=TS_TRACKER_NOTE_PITCH,.note=60};
    ts_tracker_edit_anchor(ui->tracker_edit,t);ts_tracker_edit_mark(ui->tracker_edit,t,3,2);
    modified(SDLK_l,KMOD_CTRL);assert(a->tracker.block_active);
    float frame[2];for(int i=0;i<317;++i)audio_callback(a,(Uint8*)frame,sizeof(frame));
    TsPerformanceRecorder recorder;ts_performance_recorder_init(&recorder);char error[128];
    assert(ts_performance_recorder_start(&recorder,"tracker-cycle.wav",48000,2,5000,error,sizeof(error)));
    a->sister_file_recorder=&recorder;atomic_store(&a->tracker_capture,4);
    for(int i=317;i<1920;++i)audio_callback(a,(Uint8*)frame,sizeof(frame));
    assert(!atomic_load(&recorder.accepted_frames)); /* Mid-cycle arming waits for the seam. */
    for(int i=0;i<1920;++i) {
        audio_callback(a,(Uint8*)frame,sizeof(frame));
        assert(recorder.ring[i].l==frame[0] && recorder.ring[i].r==frame[1]);
    }
    assert(atomic_load(&recorder.accepted_frames)==1920 && a->tracker.running);
    audio_callback(a,(Uint8*)frame,sizeof(frame));assert(ts_performance_recorder_state(&recorder)==TS_PERFORMANCE_FILE_STOPPING);
    assert(a->tracker.running && a->tracker.block_active && !atomic_load(&a->tracker_capture));
    while(ts_performance_recorder_pump(&recorder,8192)){}
    ts_performance_recorder_free(&recorder);remove("tracker-cycle.wav");
    assert(ts_performance_recorder_start(&recorder,"tracker-performance.wav",48000,2,6000,error,sizeof(error)));
    atomic_store(&a->tracker_capture,1);
    while(a->tracker.loop_cycles<3)audio_callback(a,(Uint8*)frame,sizeof(frame));
    assert(atomic_load(&recorder.accepted_frames)==1);
    for(int i=1;i<800;++i)audio_callback(a,(Uint8*)frame,sizeof(frame));
    ts_tracker_playback_pause(&a->tracker,1);
    for(int i=0;i<2000;++i)audio_callback(a,(Uint8*)frame,sizeof(frame));
    assert(atomic_load(&recorder.accepted_frames)==2800);ts_tracker_playback_pause(&a->tracker,0);
    /* Shrink next cycle to one row; F7 stop must follow its actual seam. */
    assert(ts_tracker_playback_queue_block(&a->tracker,(TsTrackerBlock){p->id,3,3,2,2}));
    while(a->tracker.loop_cycles<4)audio_callback(a,(Uint8*)frame,sizeof(frame));
    assert(atomic_load(&recorder.accepted_frames)==3921);atomic_store(&a->tracker_capture,3);
    while(ts_performance_recorder_state(&recorder)==TS_PERFORMANCE_FILE_RECORDING)audio_callback(a,(Uint8*)frame,sizeof(frame));
    assert(atomic_load(&recorder.accepted_frames)==4880 && a->tracker.running);
    while(ts_performance_recorder_pump(&recorder,8192)){}
    ts_performance_recorder_free(&recorder);remove("tracker-performance.wav");a->sister_file_recorder=NULL;
    /* Exercise actual F7/F8 dispatch, busy-recorder ownership and writer setup. */
    SisterWindow *sister=calloc(1,sizeof(*sister));assert(sister);
    ts_performance_recorder_init(&sister->performance_recorder);a->sister_file_recorder=&sister->performance_recorder;
    SDL_AudioDeviceID saved_output=ts_real_output;ts_real_output=1;
    SDL_setenv("TAPESISTER_CAPTURES","tracker-capture-test",1);
    SDL_Event event;SDL_zero(event);event.type=SDL_KEYDOWN;event.key.windowID=SDL_GetWindowID(window);event.key.keysym.sym=SDLK_F7;
    assert(tracker_capture_event(&event,window,0,a,ui,sister,48000));assert(atomic_load(&a->tracker_capture)==1);
    char path[1200];snprintf(path,sizeof(path),"%s",sister->performance_recorder.path);
    event.key.keysym.sym=SDLK_F8;assert(tracker_capture_event(&event,window,0,a,ui,sister,48000));
    assert(atomic_load(&a->tracker_capture)==1 && strstr(ui->status,"FINISH CURRENT"));
    event.key.keysym.sym=SDLK_F7;assert(tracker_capture_event(&event,window,0,a,ui,sister,48000));
    assert(!atomic_load(&a->tracker_capture));SDL_WaitThread(sister->performance_writer,NULL);sister->performance_writer=NULL;
    sister_poll_file_capture(sister);remove(path);
    event.key.keysym.sym=SDLK_F8;assert(tracker_capture_event(&event,window,0,a,ui,sister,48000));
    assert(atomic_load(&a->tracker_capture)==4);
    snprintf(path,sizeof(path),"%s",sister->performance_recorder.path);
    for(int i=0;i<1920;++i)audio_callback(a,(Uint8*)frame,sizeof(frame));
    assert(atomic_load(&sister->performance_recorder.accepted_frames)==960);
    SDL_WaitThread(sister->performance_writer,NULL);sister->performance_writer=NULL;
    TsSample captured;ts_sample_init(&captured);
    assert(ts_performance_recorder_load(&sister->performance_recorder,&captured,error,sizeof(error)));
    assert(captured.frames==960 && captured.channels==2);ts_sample_free(&captured);
    sister_poll_file_capture(sister);remove(path);a->sister_file_recorder=NULL;free(sister);ts_real_output=saved_output;
    SDL_setenv("TAPESISTER_CAPTURES","",1);
    ts_tracker_playback_stop(&a->tracker);t->editor_row=0;t->editor_lane=0;ts_tracker_edit_anchor(ui->tracker_edit,t);
}
int main(int argc,char **argv)
{
    SDL_setenv("SDL_VIDEODRIVER","dummy",1);assert(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER));
    window=SDL_CreateWindow("Tracker",0,0,640,400,0);assert(window);
    a=calloc(1,sizeof(*a));ui=malloc(sizeof(*ui));instrument=malloc(sizeof(*instrument));pages=malloc(sizeof(*pages));
    assert(a && ui && instrument && pages);
    char error[256];
    ts_ui_init(ui);ts_instrument_init(instrument);assert(ts_sample_pages_init(pages,error,sizeof(error)));ui->tracker=&pages->tracker;
    ts_tracker_playback_init(&a->tracker);ts_note_bank_init(&a->notes);
    ts_performance_init(&a->performance);ts_performance_init(&a->tile_launchers);
    ts_keyboard_sequence_init(&a->keyboard_sequence);ts_capture_init(&a->capture);ts_audio_mixer_init(&a->mixer);
    ts_sister_runtime_init(&a->sister);a->output_rate=48000;a->fm_output_gain=a->fm_output_target=1;
    assert(ts_sister_runtime_reconfigure(&a->sister,48000,2,error,sizeof(error)));
    assert(ts_instrument_select_bank(instrument,0,error,sizeof(error)));
    assert(ts_instrument_activate_silence(instrument,48000,48000,error,sizeof(error)));
    for(size_t i=0;i<instrument->current.frames;++i)instrument->current.data[i]=.2f;
    instrument->has_loop=1;instrument->loop_first=0;instrument->loop_last=48000;
    assert(ts_instrument_sync_selected(instrument,error,sizeof(error)));
    key(SDLK_F10);assert(ui->tracker_open && pages->tracker.pattern_count==1 && !a->tracker.running);
    TsTrackerPattern *p=ts_sister_tracker_pattern(&pages->tracker,pages->tracker.editor_pattern);
    click(20,342);assert(p->cells[0][0].tile_id==instrument->bank[0].tile_id);
    key(SDLK_z);assert(p->cells[0][0].note==ui->keyboard_base_note && p->cells[0][0].note_kind==TS_TRACKER_NOTE_PITCH);
    assert(pages->tracker.editor_row==1 && !ts_note_bank_count(&a->notes));
    /* Two hex digits must retain the high nibble even for an unbound prefix. */
    pages->tracker.aliases[0x12]=pages->tracker.aliases[1];pages->tracker.aliases[1]=0;
    click(TS_TRACKER_LANE_X+29,TS_TRACKER_GRID_Y+TS_TRACKER_ROW_HEIGHT*1+2);assert(ui->tracker_field==1);key(SDLK_1);key(SDLK_2);
    assert(p->cells[1][0].tile_id==instrument->bank[0].tile_id);
    click(TS_TRACKER_LANE_X+38,TS_TRACKER_GRID_Y+TS_TRACKER_ROW_HEIGHT*2+2);assert(ui->tracker_field==2);key(SDLK_2);key(SDLK_0);
    assert(p->cells[2][0].has_volume && p->cells[2][0].volume==0x20);
    click(38,TS_TRACKER_GRID_Y+TS_TRACKER_ROW_HEIGHT*3+2);click(150,342);assert(p->cells[3][0].note_kind==TS_TRACKER_NOTE_OFF);
    click(38,TS_TRACKER_GRID_Y+TS_TRACKER_ROW_HEIGHT*4+2);click(186,342);assert(p->cells[4][0].note_kind==TS_TRACKER_NOTE_CUT);
    p->cells[0][1]=p->cells[0][0];click(180,14);assert(a->tracker.running);
    float block[1024];audio_callback(a,(Uint8*)block,sizeof(block));
    assert(a->tracker.lanes[0].voice.active && a->tracker.lanes[1].voice.active);
    assert(fabs(a->mixer.buses.capture.l-.4)<1e-5 && fabs(block[1022]-.32)<1e-5);
    assert(atomic_load_explicit(&a->tracker.display_running,memory_order_acquire)==1);
    double phase=a->tracker.lanes[0].voice.position;
    key(SDLK_SPACE);assert(a->tracker.paused);audio_callback(a,(Uint8*)block,sizeof(block));
    assert(a->tracker.lanes[0].voice.position==phase && fabs(block[1022])<1e-5);
    key(SDLK_SPACE);assert(!a->tracker.paused);audio_callback(a,(Uint8*)block,sizeof(block));
    /* Space must act on transport state even when UI telemetry is stale. */
    atomic_store(&a->tracker.display_running,2);key(SDLK_SPACE);assert(a->tracker.paused);
    atomic_store(&a->tracker.display_running,0);key(SDLK_SPACE);assert(!a->tracker.paused);
    key(SDLK_F10);assert(!ui->tracker_open && a->tracker.running);
    /* Stop belongs to tracker; an existing held manual note keeps its phase. */
    TsNoteEvent n;TsTuning tuning={60,0};assert(ts_note_event_qwerty(&n,0,60));
    assert(ts_note_bank_start_sample_event(&a->notes,&instrument->current,&tuning,&n,1,48000)==TS_NOTE_STARTED);
    ts_tracker_playback_stop(&a->tracker);assert(ts_note_bank_count(&a->notes)==1);
    audio_callback(a,(Uint8*)block,sizeof(block));assert(ts_note_bank_count(&a->notes)==1 && block[1022]>.1f);
    ts_note_bank_clear(&a->notes);tracker_play(0,a,ui,pages,instrument,48000);
    audio_callback(a,(Uint8*)block,sizeof(block));
    /* FILE OUT receives the exact final audible Main mix. */
    TsPerformanceRecorder rec;ts_performance_recorder_init(&rec);
    assert(ts_performance_recorder_start(&rec,"tracker-file-out.wav",48000,2,2048,error,sizeof(error)));
    a->sister_file_recorder=&rec;atomic_init(&a->sister_file_tap,TS_SISTER_TAP_MIX);
    audio_callback(a,(Uint8*)block,sizeof(block));assert(atomic_load(&rec.accepted_frames)==512);
    for(int i=0;i<512;++i){assert(rec.ring[i].l==block[i*2] && rec.ring[i].r==block[i*2+1]);}
    a->sister_file_recorder=NULL;assert(ts_performance_recorder_request_stop(&rec));
    while(ts_performance_recorder_pump(&rec,2048)){}
    ts_performance_recorder_free(&rec);remove("tracker-file-out.wav");
    /* TRACK is a selectable Sister input; an unselected Main bus cannot leak. */
    assert(ts_sister_runtime_enable(&a->sister,48000,2,2,5,error,sizeof(error)));
    ts_sister_runtime_set_sources(&a->sister,TS_SISTER_SOURCE_TRACK);
    audio_callback(a,(Uint8*)block,sizeof(block));audio_callback(a,(Uint8*)block,sizeof(block));
    assert(fabs(a->sister.last_frame.input.l-.4)<1e-5);
    ts_sister_runtime_set_sources(&a->sister,0);
    audio_callback(a,(Uint8*)block,sizeof(block));audio_callback(a,(Uint8*)block,sizeof(block));
    assert(fabs(a->sister.last_frame.input.l)<1e-6 && a->mixer.buses.tracker.l==0);
    TsSisterUiModel model;ts_sister_ui_model_init(&model,&ui->config);
    TsSisterUiHit hit=ts_sister_ui_hit_test_model(&model,345,180);assert(hit.action==TS_SISTER_UI_ACTION_SOURCE_TRACK && hit.index==5);
    char target[96];assert(ts_sister_ui_midi_target(hit,target,sizeof(target)));
    assert(!strcmp(target,"sister.source.tracker"));
    TsSisterUiHit mapped;assert(midi_sister_hit_from_target(target,.5f,&mapped));assert(mapped.index==5);
    /* A different editor pattern cannot switch the heard pattern until Play. */
    TsPatternId heard=a->tracker.prepared->pattern.id,new_id;
    assert(ts_sister_tracker_add_pattern(&pages->tracker,64,&new_id,error,sizeof(error)));
    pages->tracker.editor_pattern=new_id;ui->tracker_open=1;tracker_refresh(0,a,ui,pages,instrument,48000);
    assert(a->tracker.prepared->pattern.id==heard);
    tracker_play(0,a,ui,pages,instrument,48000);assert(a->tracker.prepared->pattern.id==new_id);
    pages->tracker.editor_pattern=heard;ui->tracker_scroll=0;
    tracker_refresh(0,a,ui,pages,instrument,48000);
    editor_controls();
    tapehead_columns_and_overlay();
    independent_marks_and_layout();
    block_capture_frames();
    fasttracks_controls();
    tapehead_transport_visuals();
    if(argc>1) {
        TsSisterTracker *t=ui->tracker;t->editor_row=2;t->editor_lane=2;
        ts_tracker_edit_anchor(ui->tracker_edit,t);ts_tracker_edit_mark(ui->tracker_edit,t,5,3);
        modified(SDLK_l,KMOD_CTRL);float frame[2];audio_callback(a,(Uint8*)frame,sizeof(frame));
        t->editor_row=0;t->editor_lane=0;ui->tracker_field=1;tracker_refresh(0,a,ui,pages,instrument,48000);
        ui->tracker_cursor_visible=1;ui->tracker_hover_y=0;
        TsFramebuffer *fb=malloc(sizeof(*fb));assert(fb);ts_ui_render(fb,ui,instrument);
        SDL_Surface *s=SDL_CreateRGBSurfaceFrom(fb->pixels,640,400,32,640*4,0xff0000,0xff00,0xff,0xff000000);
        assert(s && !SDL_SaveBMP(s,argv[1]));
        if(argc>2){ui->tracker_edit->tools_open=1;ts_ui_render(fb,ui,instrument);assert(!SDL_SaveBMP(s,argv[2]));ui->tracker_edit->tools_open=0;}
        if(argc>3){ui->tracker_expanded=1;ts_ui_render(fb,ui,instrument);assert(!SDL_SaveBMP(s,argv[3]));ui->tracker_expanded=0;}
        if(argc>4) {
            TsTrackerPattern *p=ts_sister_tracker_pattern(t,t->editor_pattern);
            unsigned alias=tracker_bind_sample(ui,instrument,instrument->selected_slot);assert(alias);
            static const unsigned lengths[]={3,5,7,16,0,9,11,4},ratios[]={7,14,0,12,7,16,6,3};
            memset(p->cells,0,sizeof(p->cells));p->rows=16;
            for(int lane=0;lane<8;++lane) {
                t->lanes[lane].length=lengths[lane];t->lanes[lane].ratio=ratios[lane];
                t->lanes[lane].mode=lane==3 || lane==4?TS_TRACKER_STANDARD:TS_TRACKER_PATTERN;
                t->lanes[lane].direction=lane==1?TS_TRACKER_REVERSE:lane==6?TS_TRACKER_PING_PONG:TS_TRACKER_FORWARD;
                for(int row=0;row<16;row+=2)p->cells[row][lane]=(TsTrackerCell){.tile_id=row?0:t->aliases[alias],.note_kind=TS_TRACKER_NOTE_PITCH,.note=48+lane+row};
            }
            t->ticks_per_line=3;t->bpm=125;t->control_lane=2;t->fasttracks_uses_length=1;t->length_bypass=0;
            t->editor_row=0;t->editor_lane=0;ts_tracker_edit_anchor(ui->tracker_edit,t);
            tracker_play(0,a,ui,pages,instrument,48000);
            for(int frame=0;frame<24001;++frame)(void)ts_tracker_playback_read(&a->tracker,48000);
            ts_tracker_playback_end_block(&a->tracker);tracker_refresh(0,a,ui,pages,instrument,48000);
            ui->tracker_hover_y=0;ui->tracker_field=0;
            ts_ui_render(fb,ui,instrument);assert(!SDL_SaveBMP(s,argv[4]));
            if(argc>5){ui->tracker_expanded=1;ts_ui_render(fb,ui,instrument);assert(!SDL_SaveBMP(s,argv[5]));ui->tracker_expanded=0;}
        }
        SDL_FreeSurface(s);free(fb);
    }
    stop_all_force(0,a,ui);assert(!a->tracker.running && !ts_note_bank_count(&a->notes));
    ts_tracker_playback_free(&a->tracker);ts_sister_runtime_free(&a->sister);
    ts_tracker_edit_free(ui->tracker_edit);
    ts_sample_pages_free(pages);ts_instrument_free(instrument);free(pages);free(instrument);free(ui);free(a);
    SDL_DestroyWindow(window);SDL_Quit();puts("SisterTracker controller checks passed");return 0;
}
