#ifdef NDEBUG
#undef NDEBUG
#endif
#define SDL_MAIN_HANDLED
#define main tapesister_application_main
#include "../src/main_sdl.c"
#undef main
#include <assert.h>
static SDL_Window *test_window;
static AudioState *test_audio;
static TsUiState *test_ui;
static TsInstrument *test_bank;
static TsSamplePages *test_pages;
static char test_error[256];
static void press(SDL_Keycode key,SDL_Scancode sc,SDL_Keymod mod) {
    SDL_Event e;SDL_zero(e);e.type=SDL_KEYDOWN;e.key.windowID=SDL_GetWindowID(test_window);
    e.key.keysym.sym=key;e.key.keysym.scancode=sc;e.key.keysym.mod=mod;
    assert(tracker_event(&e,test_window,0,test_audio,test_ui,test_pages,test_bank,48000));
    e.type=SDL_KEYUP;tracker_event(&e,test_window,0,test_audio,test_ui,test_pages,test_bank,48000);
}
static void wheel(int x,int y,float delta) {
    SDL_Event e;SDL_zero(e);e.type=SDL_MOUSEWHEEL;e.wheel.windowID=SDL_GetWindowID(test_window);
    e.wheel.mouseX=x;e.wheel.mouseY=y;e.wheel.y=(int)delta;e.wheel.preciseY=delta;
    assert(tracker_event(&e,test_window,0,test_audio,test_ui,test_pages,test_bank,48000));
}
static TsTrackerPattern *pat(void) {return ts_sister_tracker_pattern(&test_pages->tracker,test_pages->tracker.editor_pattern);}
static void snapshot(const char *path) {
    TsFramebuffer *fb=malloc(sizeof(*fb));assert(fb);ts_ui_render(fb,test_ui,test_bank);
    SDL_Surface *surface=SDL_CreateRGBSurfaceFrom(fb->pixels,640,400,32,640*4,0xff0000,0xff00,0xff,0xff000000);
    assert(surface && !SDL_SaveBMP(surface,path));SDL_FreeSurface(surface);free(fb);
}
static void exact_cycle_capture(void) {
    TsPerformanceRecorder rec;ts_performance_recorder_init(&rec);
    assert(ts_performance_recorder_start(&rec,"embedded-cycle.wav",48000,2,8192,test_error,sizeof(test_error)));
    test_audio->sister_file_recorder=&rec;atomic_store(&test_audio->sister_file_tap,TS_SISTER_TAP_MIX);
    atomic_store(&test_audio->tracker_capture,4);
    float out[512*2];
    for(int callback=0;callback<16 && ts_performance_recorder_state(&rec)==TS_PERFORMANCE_FILE_RECORDING;++callback) {
        uint64_t previous=atomic_load(&rec.accepted_frames);
        audio_callback(test_audio,(Uint8*)out,sizeof(out));
        uint64_t current=atomic_load(&rec.accepted_frames);
        /* Every accepted frame is the final audible mix, including processors. */
        unsigned accepted=0;
        for(unsigned f=0;f<512;++f) {
            unsigned flags=ts_tapehead_capture_flags(f);
            if((flags&2) && accepted+previous==1920)break;
            if(current==previous)break;
            if(previous || (flags&2) || accepted) {
                assert(rec.ring[previous+accepted].l==out[f*2]);
                assert(rec.ring[previous+accepted].r==out[f*2+1]);++accepted;
                if(accepted==current-previous)break;
            }
        }
        assert(accepted==current-previous);
    }
    assert(atomic_load(&rec.accepted_frames)==1920);
    assert(ts_tapehead_block_active() && ts_tapehead_running());
    assert(!atomic_load(&test_audio->tracker_capture));
    while(ts_performance_recorder_pump(&rec,8192)){}
    TsSample captured;ts_sample_init(&captured);
    assert(ts_performance_recorder_load(&rec,&captured,test_error,sizeof(test_error)));
    assert(captured.frames==1920 && captured.channels==2);ts_sample_free(&captured);
    ts_performance_recorder_free(&rec);test_audio->sister_file_recorder=NULL;remove("embedded-cycle.wav");
}
static void click(int x,int y) {
    SDL_Event event;SDL_zero(event);event.type=SDL_MOUSEBUTTONDOWN;
    event.button.windowID=SDL_GetWindowID(test_window);event.button.button=SDL_BUTTON_LEFT;
    event.button.x=x;event.button.y=y;
    assert(tracker_event(&event,test_window,0,test_audio,test_ui,test_pages,test_bank,48000));
    event.type=SDL_MOUSEBUTTONUP;assert(tracker_event(&event,test_window,0,test_audio,test_ui,test_pages,test_bank,48000));
}
static void replayer_commands(void) {
    TsSisterTracker *t=&test_pages->tracker;const unsigned record_size=7+256*8*7;
    ts_tapehead_stop();
    /* Return from full view for original Play controls. */
    press(SDLK_BACKSPACE,SDL_SCANCODE_BACKSPACE,KMOD_CTRL|KMOD_ALT);
    uint8_t *score=t->embedded_data,*record=score+52+80+256;
    score[18]=record[0];score[19]=score[20]=0;t->editor_pattern=pat()->id;t->editor_row=0;
    /* C00 silences the triggered note; C40 restores it through the real FX path. */
    record[7]=49;record[8]=1;record[9]=0;record[10]=12;record[11]=0;record[12]=record[13]=0;
    assert(ts_tapehead_sync(test_pages,test_bank,48000,test_error,sizeof(test_error)));
    click(390,25);assert(ts_tapehead_running());
    float out[512*2];audio_callback(test_audio,(Uint8*)out,sizeof(out));
    assert(fabsf(out[1000])+fabsf(out[1001])<1e-6f);ts_tapehead_stop();
    score=t->embedded_data;record=score+52+80+256;record[11]=64;
    assert(ts_tapehead_sync(test_pages,test_bank,48000,test_error,sizeof(test_error)));click(390,25);
    audio_callback(test_audio,(Uint8*)out,sizeof(out));assert(out[1000]<-.001f && out[1001]>.001f);ts_tapehead_stop();
    /* Volume-column 10 is an explicit zero, not an empty field. */
    score=t->embedded_data;record=score+52+80+256;record[9]=0x10;record[10]=record[11]=0;
    assert(ts_tapehead_sync(test_pages,test_bank,48000,test_error,sizeof(test_error)));click(390,25);
    audio_callback(test_audio,(Uint8*)out,sizeof(out));assert(fabsf(out[1000])+fabsf(out[1001])<1e-6f);ts_tapehead_stop();
    /* Two physical patterns in the order list use the original Song transport. */
    score=t->embedded_data;record=score+52+80+256;uint8_t *second=record+record_size;
    record[9]=0x50;record[1]=second[1]=2;record[2]=second[2]=0;
    ts_sister_tracker_pattern(t,(TsPatternId)(record[3]|record[4]<<8|record[5]<<16|record[6]<<24))->rows=2;
    ts_sister_tracker_pattern(t,(TsPatternId)(second[3]|second[4]<<8|second[5]<<16|second[6]<<24))->rows=2;
    memset(second+7,0,256*8*7);second[7]=49;second[8]=1;second[9]=0x10;
    score[14]=2;score[15]=0;score[16]=score[17]=score[51]=0;
    for(int lane=0;lane<8;++lane){score[52+lane*10]=score[53+lane*10]=0;score[54+lane*10]=0;}
    uint8_t *orders=score+52+80;orders[0]=record[0];orders[1]=second[0];
    t->order_count=2;t->orders[0]=t->patterns[0]->id;t->orders[1]=t->patterns[1]->id;
    assert(ts_tapehead_sync(test_pages,test_bank,48000,test_error,sizeof(test_error)));click(390,9);
    audio_callback(test_audio,(Uint8*)out,sizeof(out));assert(out[1000]<-.001f);
    for(int i=0;i<4;++i)audio_callback(test_audio,(Uint8*)out,sizeof(out));
    assert(fabsf(out[1000])+fabsf(out[1001])<1e-6f && ts_tapehead_running());
    ts_tapehead_stop();assert(ts_tapehead_export(t,test_error,sizeof(test_error)));
}
static void project_roundtrip(void) {
    TsInstrument *record=malloc(sizeof(*record)),*restored=malloc(sizeof(*restored));
    TsSamplePages *pages=malloc(sizeof(*pages));assert(record&&restored&&pages);
    ts_instrument_init(record);ts_instrument_init(restored);assert(ts_sample_pages_init(pages,test_error,sizeof(test_error)));
    TsSisterProjectState state;ts_sister_project_state_init(&state,48000);
    assert(ts_sample_pages_save_project(test_pages,test_bank,record,&state,"embedded-project/embedded-project.tsr",test_error,sizeof(test_error)));
    assert(ts_sample_pages_load_project(pages,restored,record,"embedded-project/embedded-project.tsr",test_error,sizeof(test_error)));
    assert(ts_sister_tracker_hash(&pages->tracker)==ts_sister_tracker_hash(&test_pages->tracker));
    ts_sample_pages_free(pages);ts_instrument_free(restored);ts_instrument_free(record);free(pages);free(restored);free(record);
}
int main(int argc,char **argv) {
    SDL_SetHint(SDL_HINT_VIDEODRIVER,"dummy");assert(!SDL_Init(SDL_INIT_VIDEO|SDL_INIT_TIMER));
    test_window=SDL_CreateWindow("Embedded fixture",0,0,640,400,SDL_WINDOW_HIDDEN);assert(test_window);
    test_audio=calloc(1,sizeof(*test_audio));test_ui=calloc(1,sizeof(*test_ui));
    test_bank=malloc(sizeof(*test_bank));test_pages=malloc(sizeof(*test_pages));assert(test_audio&&test_ui&&test_bank&&test_pages);
    ts_ui_init(test_ui);ts_instrument_init(test_bank);assert(ts_sample_pages_init(test_pages,test_error,sizeof(test_error)));
    test_ui->tracker=&test_pages->tracker;test_audio->output_rate=48000;
    ts_tracker_playback_init(&test_audio->tracker);ts_note_bank_init(&test_audio->notes);
    ts_audio_mixer_init(&test_audio->mixer);ts_sister_runtime_init(&test_audio->sister);
    ts_performance_init(&test_audio->performance);ts_performance_init(&test_audio->tile_launchers);
    ts_keyboard_sequence_init(&test_audio->keyboard_sequence);ts_capture_init(&test_audio->capture);
    assert(ts_sister_runtime_reconfigure(&test_audio->sister,48000,2,test_error,sizeof(test_error)));
    assert(ts_instrument_select_bank(test_bank,0,test_error,sizeof(test_error)));
    assert(ts_instrument_activate_silence(test_bank,8192,48000,test_error,sizeof(test_error)));
    /* Float stereo with opposite polarity would disappear in a mono adapter. */
    free(test_bank->current.data);test_bank->current.channels=2;
    test_bank->current.data=calloc(test_bank->current.frames*2,sizeof(float));assert(test_bank->current.data);
    for(size_t i=0;i<test_bank->current.frames;++i){test_bank->current.data[i*2]=.1234567f;test_bank->current.data[i*2+1]=-.2345678f;}
    test_bank->has_loop=1;test_bank->loop_first=0;test_bank->loop_last=test_bank->current.frames;
    assert(ts_instrument_sync_selected(test_bank,test_error,sizeof(test_error)));
    TsTileId tile=test_bank->bank[0].tile_id;
    press(SDLK_F10,SDL_SCANCODE_F10,KMOD_NONE);assert(test_ui->tracker_open&&test_ui->tracker_embedded_frame);
    assert(test_pages->tracker.embedded_size && ts_sister_tracker_validate(&test_pages->tracker,test_error,sizeof(test_error)));
    press(SDLK_z,SDL_SCANCODE_Z,KMOD_NONE);assert(pat()->cells[0][0].note_kind==TS_TRACKER_NOTE_PITCH);
    assert(pat()->cells[0][0].tile_id==tile);assert(test_pages->tracker.editor_row==1);
    press(SDLK_x,SDL_SCANCODE_X,KMOD_NONE);assert(test_pages->tracker.editor_row==2);
    press(SDLK_UP,SDL_SCANCODE_UP,KMOD_NONE);assert(test_pages->tracker.editor_row==1);
    press(SDLK_BACKSPACE,SDL_SCANCODE_BACKSPACE,KMOD_NONE);
    assert(test_pages->tracker.editor_row==0 && pat()->cells[1][0].note_kind==TS_TRACKER_NOTE_NONE);
    assert(pat()->cells[0][0].note_kind==TS_TRACKER_NOTE_PITCH);
    press(SDLK_z,SDL_SCANCODE_Z,KMOD_CTRL);assert(pat()->cells[1][0].note_kind==TS_TRACKER_NOTE_PITCH);
    press(SDLK_y,SDL_SCANCODE_Y,KMOD_CTRL);assert(pat()->cells[1][0].note_kind==TS_TRACKER_NOTE_NONE);
    unsigned step=test_pages->tracker.edit_step;
    press(SDLK_BACKQUOTE,SDL_SCANCODE_GRAVE,KMOD_NONE);assert(test_pages->tracker.edit_step==(step+1)%17);
    press(SDLK_BACKQUOTE,SDL_SCANCODE_GRAVE,KMOD_SHIFT);assert(test_pages->tracker.edit_step==step);
    wheel(156,66,1);assert(test_pages->tracker.edit_step==(step+1)%17);
    wheel(156,66,-1);assert(test_pages->tracker.edit_step==step);
    unsigned octave=test_pages->tracker.embedded_data[21];wheel(313,160,1);
    assert(test_pages->tracker.embedded_data[21]==octave+1);wheel(313,160,-1);
    /* MIDI uses upstream note entry and velocity, not a parallel canvas voice. */
    TsMidiEvent midi;assert(ts_midi_decode_short_message(0x92,67,100,&midi));
    unsigned midi_row=test_pages->tracker.editor_row;TsTrackerCell before_midi=pat()->cells[midi_row][0];
    handle_midi_event(0,test_audio,test_ui,test_bank,NULL,&midi,48000);
    assert(pat()->cells[midi_row][0].note==67 && pat()->cells[midi_row][0].tile_id==tile);
    assert(pat()->cells[midi_row][0].has_volume && !ts_note_bank_count(&test_audio->notes));
    assert(ts_midi_decode_short_message(0x82,67,0,&midi));handle_midi_event(0,test_audio,test_ui,test_bank,NULL,&midi,48000);
    press(SDLK_z,SDL_SCANCODE_Z,KMOD_CTRL);assert(!memcmp(&pat()->cells[midi_row][0],&before_midi,sizeof(before_midi)));
    /* Original block clipboard and pattern extraction are retained. */
    press(SDLK_HOME,SDL_SCANCODE_HOME,KMOD_NONE);press(SDLK_DOWN,SDL_SCANCODE_DOWN,KMOD_ALT);
    press(SDLK_F4,SDL_SCANCODE_F4,KMOD_ALT);
    press(SDLK_DOWN,SDL_SCANCODE_DOWN,KMOD_NONE);press(SDLK_DOWN,SDL_SCANCODE_DOWN,KMOD_NONE);
    unsigned paste_row=test_pages->tracker.editor_row;
    press(SDLK_F5,SDL_SCANCODE_F5,KMOD_ALT);assert(pat()->cells[paste_row][0].note==pat()->cells[0][0].note);
    press(SDLK_z,SDL_SCANCODE_Z,KMOD_CTRL);assert(pat()->cells[paste_row][0].note_kind==TS_TRACKER_NOTE_NONE);
    unsigned count=test_pages->tracker.pattern_count;
    press(SDLK_F8,SDL_SCANCODE_F8,KMOD_NONE);assert(test_pages->tracker.pattern_count>count);
    /* Retain song-wide LEN and raw volume/effect columns in the project. */
    uint8_t *score=test_pages->tracker.embedded_data;
    score[12]=1;test_pages->tracker.ticks_per_line=1;
    uint8_t *record=score+52+80+256;
    score[52]=3;score[62]=5;score[51]=2;
    /* Hidden-row raw volume slide, effect Pxx, M/N survive native mirroring. */
    uint8_t *raw=record+7+(200*8+3)*7;
    raw[2]=0xa5;raw[3]=25;raw[4]=0x33;raw[5]=0x16;raw[6]=0xff;
    assert(ts_tapehead_sync(test_pages,test_bank,48000,test_error,sizeof(test_error)));
    assert(ts_tapehead_export(&test_pages->tracker,test_error,sizeof(test_error)));
    score=test_pages->tracker.embedded_data;record=score+52+80+256;
    assert(score[52]==3 && score[62]==5 && score[51]==2);
    raw=record+7+(200*8+3)*7;assert(raw[2]==0xa5&&raw[3]==25&&raw[4]==0x33&&raw[5]==0x16&&raw[6]==0xff);
    if(argc>2) {tracker_refresh(0,test_audio,test_ui,test_pages,test_bank,48000);snapshot(argv[2]);}
    /* Original marking and Ctrl+L block transport. */
    press(SDLK_HOME,SDL_SCANCODE_HOME,KMOD_NONE);
    press(SDLK_DOWN,SDL_SCANCODE_DOWN,KMOD_ALT);
    press(SDLK_DOWN,SDL_SCANCODE_DOWN,KMOD_ALT);
    press(SDLK_l,SDL_SCANCODE_L,KMOD_CTRL);assert(ts_tapehead_block_active());
    float rendered[512*2];audio_callback(test_audio,(Uint8*)rendered,sizeof(rendered));
    float left=0,right=0;for(int i=0;i<512;++i){left+=rendered[i*2];right+=rendered[i*2+1];}
    assert(left>0.01f && right<-.01f);
    exact_cycle_capture();
    /* A changed generation retains immutable sounding audio until retrigger. */
    for(size_t i=0;i<test_bank->current.frames;++i){test_bank->current.data[i*2]=-.321f;test_bank->current.data[i*2+1]=.432f;}
    ++test_bank->generation;
    assert(ts_tapehead_sync(test_pages,test_bank,48000,test_error,sizeof(test_error)));
    audio_callback(test_audio,(Uint8*)rendered,sizeof(rendered));
    assert(rendered[500*2]>.001f && rendered[500*2+1]<-.001f);
    for(int block=0;block<6;++block)audio_callback(test_audio,(Uint8*)rendered,sizeof(rendered));
    assert(rendered[500*2]<-.001f && rendered[500*2+1]>.001f);
    /* Hiding/reopening is a view change, retaining the original transport. */
    press(SDLK_F10,SDL_SCANCODE_F10,KMOD_NONE);assert(!test_ui->tracker_open&&ts_tapehead_block_active());
    audio_callback(test_audio,(Uint8*)rendered,sizeof(rendered));
    press(SDLK_F10,SDL_SCANCODE_F10,KMOD_NONE);assert(test_ui->tracker_open&&ts_tapehead_block_active());
    press(SDLK_l,SDL_SCANCODE_L,KMOD_CTRL);assert(!ts_tapehead_block_active());
    press(SDLK_BACKSPACE,SDL_SCANCODE_BACKSPACE,KMOD_CTRL|KMOD_ALT);
    tracker_refresh(0,test_audio,test_ui,test_pages,test_bank,48000);
    assert(test_pages->tracker.embedded_data[27] || test_pages->tracker.embedded_data[28]);
    /* Host overlays own input and do not change upstream STEP. */
    step=test_pages->tracker.edit_step;test_ui->router_open=1;
    SDL_Event overlay;SDL_zero(overlay);overlay.type=SDL_KEYDOWN;overlay.key.windowID=SDL_GetWindowID(test_window);
    overlay.key.keysym.sym=SDLK_BACKQUOTE;overlay.key.keysym.scancode=SDL_SCANCODE_GRAVE;
    assert(!tracker_event(&overlay,test_window,0,test_audio,test_ui,test_pages,test_bank,48000));
    assert(test_pages->tracker.edit_step==step);test_ui->router_open=0;
    /* Versioned score extension survives native project codec, with deep clones. */
    assert(ts_tapehead_export(&test_pages->tracker,test_error,sizeof(test_error)));
    assert(ts_sister_tracker_save_file(&test_pages->tracker,"embedded-test.tst",test_error,sizeof(test_error)));
    TsSisterTracker loaded;ts_sister_tracker_init(&loaded);
    assert(ts_sister_tracker_load_file(&loaded,"embedded-test.tst",test_error,sizeof(test_error)));
    assert(ts_sister_tracker_hash(&loaded)==ts_sister_tracker_hash(&test_pages->tracker));
    assert(loaded.embedded_data!=test_pages->tracker.embedded_data);
    loaded.embedded_data[8]=0;loaded.embedded_data[9]=0;
    assert(!ts_sister_tracker_validate(&loaded,test_error,sizeof(test_error)));
    ts_sister_tracker_free(&loaded);remove("embedded-test.tst");
    if(argc>1)snapshot(argv[1]);
    replayer_commands();
    project_roundtrip();
    /* Zero speed is a real original editor value and persists in v2. */
    test_pages->tracker.embedded_data[12]=0;test_pages->tracker.ticks_per_line=0;
    assert(ts_tapehead_sync(test_pages,test_bank,48000,test_error,sizeof(test_error)));
    assert(ts_tapehead_export(&test_pages->tracker,test_error,sizeof(test_error)));
    assert(!test_pages->tracker.ticks_per_line);
    /* Shift+F10 stays with the original bookmark controls. */
    press(SDLK_F10,SDL_SCANCODE_F10,KMOD_SHIFT);assert(test_ui->tracker_open);
    /* Key releases owned by the canvas pass through even while F10 is open. */
    SDL_Event release;SDL_zero(release);release.type=SDL_KEYUP;release.key.windowID=SDL_GetWindowID(test_window);
    release.key.keysym.scancode=SDL_SCANCODE_A;release.key.keysym.sym=SDLK_a;
    assert(!tracker_event(&release,test_window,0,test_audio,test_ui,test_pages,test_bank,48000));
    /* Move, deletion and slot reuse preserve identity; old events stay missing. */
    assert(ts_sample_pages_move_tile(test_pages,test_bank,(TsTileLocation){0,0},(TsTileLocation){0,4},test_error,sizeof(test_error)));
    assert(ts_tapehead_sync(test_pages,test_bank,48000,test_error,sizeof(test_error)));
    assert(test_pages->tracker.aliases[1]==tile && test_bank->bank[4].tile_id==tile);
    assert(ts_instrument_bank_clear(test_bank,4,test_error,sizeof(test_error)));
    assert(ts_instrument_select_bank(test_bank,4,test_error,sizeof(test_error)));
    assert(ts_instrument_activate_silence(test_bank,64,48000,test_error,sizeof(test_error)));
    assert(test_bank->bank[4].tile_id!=tile);
    assert(ts_tapehead_sync(test_pages,test_bank,48000,test_error,sizeof(test_error)));
    assert(test_pages->tracker.aliases[1]==tile);
    assert(ts_tapehead_sync(test_pages,test_bank,44100,test_error,sizeof(test_error)));
    assert(ts_tapehead_render(512,44100));assert(!ts_tapehead_render(512,48000));
    stop_all_force(0,test_audio,test_ui);assert(!ts_tapehead_running());
    ts_tapehead_close();ts_tracker_playback_free(&test_audio->tracker);ts_sister_runtime_free(&test_audio->sister);
    ts_tracker_edit_free(test_ui->tracker_edit);
    ts_sample_pages_free(test_pages);ts_instrument_free(test_bank);
    free(test_pages);free(test_bank);free(test_ui);free(test_audio);SDL_DestroyWindow(test_window);SDL_Quit();
    puts("Embedded TapeHead host, editor, float stereo, block loop and persistence checks passed");return 0;
}
