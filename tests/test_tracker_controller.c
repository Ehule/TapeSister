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
    assert(!t->aliases[2]);click(67,TS_TRACKER_GRID_Y+TS_TRACKER_ROW_HEIGHT*0+2);key(SDLK_KP_0);key(SDLK_KP_2);
    assert(p->cells[0][0].tile_id==instrument->bank[1].tile_id && t->aliases[2]==instrument->bank[1].tile_id);
    /* A stale/missing alias must never be rebound to a red Sample selection. */
    TsTileId missing=ts_tile_id_new();t->aliases[3]=missing;click(67,TS_TRACKER_GRID_Y+TS_TRACKER_ROW_HEIGHT*1+2);key(SDLK_0);key(SDLK_3);
    assert(p->cells[1][0].tile_id==missing && t->aliases[3]==missing);
    click(67,TS_TRACKER_GRID_Y+TS_TRACKER_ROW_HEIGHT*2+2);key(SDLK_KP_2);key(SDLK_KP_ENTER);assert(p->cells[2][0].tile_id==t->aliases[2]);
    /* Silent edits, clipboard and history cannot stop or reposition audio. */
    tracker_play(0,a,ui,pages,instrument,48000);float block[1024];audio_callback(a,(Uint8*)block,sizeof(block));
    double phase=a->tracker.lanes[0].voice.position;unsigned row=a->tracker.row;
    click(38,TS_TRACKER_GRID_Y+TS_TRACKER_ROW_HEIGHT*0+2);modified(SDLK_DOWN,KMOD_SHIFT);modified(SDLK_RIGHT,KMOD_SHIFT);
    assert(edit->selected && t->editor_row==1 && t->editor_lane==1);
    modified(SDLK_c,KMOD_CTRL);assert(edit->clipboard_rows==2 && edit->clipboard_lanes==2);
    click(38+74*2,TS_TRACKER_GRID_Y+TS_TRACKER_ROW_HEIGHT*4+2);modified(SDLK_v,KMOD_CTRL);
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
    e.motion.x=38+74;e.motion.y=TS_TRACKER_GRID_Y+TS_TRACKER_ROW_HEIGHT*3+2;e.motion.state=SDL_BUTTON_LMASK;
    assert(tracker_event(&e,window,0,a,ui,pages,instrument,48000));assert(edit->selected && t->editor_row==3 && t->editor_lane==1);
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
    click(67,TS_TRACKER_GRID_Y+TS_TRACKER_ROW_HEIGHT*1+2);assert(ui->tracker_field==1);key(SDLK_1);key(SDLK_2);
    assert(p->cells[1][0].tile_id==instrument->bank[0].tile_id);
    click(88,TS_TRACKER_GRID_Y+TS_TRACKER_ROW_HEIGHT*2+2);assert(ui->tracker_field==2);key(SDLK_2);key(SDLK_0);
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
    if(argc>1) {
        TsFramebuffer *fb=malloc(sizeof(*fb));assert(fb);ts_ui_render(fb,ui,instrument);
        SDL_Surface *s=SDL_CreateRGBSurfaceFrom(fb->pixels,640,400,32,640*4,0xff0000,0xff00,0xff,0xff000000);
        assert(s && !SDL_SaveBMP(s,argv[1]));
        if(argc>2){ui->tracker_edit->tools_open=1;ts_ui_render(fb,ui,instrument);assert(!SDL_SaveBMP(s,argv[2]));}
        SDL_FreeSurface(s);free(fb);
    }
    stop_all_force(0,a,ui);assert(!a->tracker.running && !ts_note_bank_count(&a->notes));
    ts_tracker_playback_free(&a->tracker);ts_sister_runtime_free(&a->sister);
    ts_tracker_edit_free(ui->tracker_edit);
    ts_sample_pages_free(pages);ts_instrument_free(instrument);free(pages);free(instrument);free(ui);free(a);
    SDL_DestroyWindow(window);SDL_Quit();puts("SisterTracker controller checks passed");return 0;
}
