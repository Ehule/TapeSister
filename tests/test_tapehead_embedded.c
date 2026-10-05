#ifdef NDEBUG
#undef NDEBUG
#endif
#define SDL_MAIN_HANDLED
#define main tapesister_application_main
#include "../src/main_sdl.c"
#undef main
#include <assert.h>
#include "../third_party/tapehead/application/src/ft2_fasttracks.h"
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
static void workspace_buttons(int block_loop) {
    static SisterWindow sister;
    SDL_setenv("SDL_AUDIODRIVER","dummy",1);assert(!SDL_InitSubSystem(SDL_INIT_AUDIO));
    SDL_AudioSpec spec={0};spec.freq=48000;spec.channels=2;spec.format=AUDIO_F32SYS;spec.samples=256;
    SDL_AudioDeviceID device=SDL_OpenAudioDevice(NULL,0,&spec,NULL,0);assert(device);
    ts_real_output=device;
    ts_sister_ui_model_init(&sister.model,&test_ui->config);
    const int buttons[][3]={{390,110,1},{390,128,3},{325,110,0},{325,25,2}};
    for(unsigned i=0;i<sizeof(buttons)/sizeof(buttons[0]);++i) {
        TsMidiEvent midi={0};assert(ts_midi_decode_short_message(0x90,72,100,&midi));
        assert(ts_tapehead_midi(&midi,1));
        click(buttons[i][0],buttons[i][1]);
        uint64_t score_hash=ts_sister_tracker_hash(&test_pages->tracker);
        tracker_refresh(0,test_audio,test_ui,test_pages,test_bank,48000,&sister);
        assert(sister.window && sister.renderer && sister.texture && sister.model.visible);
        assert(sister.model.fx_page==buttons[i][2]);
        assert(SDL_GetWindowFlags(sister.window)&SDL_WINDOW_SHOWN);
        assert(ts_tapehead_running() && ts_tapehead_block_active()==block_loop);
        assert(ts_sister_tracker_hash(&test_pages->tracker)==score_hash);
        midi.action=TS_MIDI_ACTION_NOTE_OFF;assert(!ts_tapehead_midi(&midi,0));
        /* MIDI belongs to the host while the Sister window covers the tracker. */
        assert(ts_midi_decode_short_message(0x90,74,100,&midi));
        handle_midi_event(device,test_audio,test_ui,test_bank,NULL,&midi,48000,!sister.model.visible);
        assert(ts_note_bank_count(&test_audio->notes)==1);
        assert(ts_sister_tracker_hash(&test_pages->tracker)==score_hash);
        midi.action=TS_MIDI_ACTION_NOTE_OFF;
        handle_midi_event(device,test_audio,test_ui,test_bank,NULL,&midi,48000,!sister.model.visible);
        assert(!ts_note_bank_count(&test_audio->notes));
        application_window_focus(test_window,&sister);
        assert(!sister.model.visible && test_ui->tracker_open);
        midi.action=TS_MIDI_ACTION_NOTE_ON;
        handle_midi_event(device,test_audio,test_ui,test_bank,NULL,&midi,48000,!sister.model.visible);
        assert(!ts_note_bank_count(&test_audio->notes));
        midi.action=TS_MIDI_ACTION_NOTE_OFF;assert(ts_tapehead_midi(&midi,0));
    }
    click(325,128);tracker_refresh(0,test_audio,test_ui,test_pages,test_bank,48000,&sister);
    assert(!test_ui->tracker_open && ts_tapehead_running() && ts_tapehead_block_active()==block_loop);
    press(SDLK_F10,SDL_SCANCODE_F10,KMOD_NONE);assert(test_ui->tracker_open);
    SDL_DestroyTexture(sister.texture);SDL_DestroyRenderer(sister.renderer);SDL_DestroyWindow(sister.window);
    memset(&sister,0,sizeof(sister));
    ts_real_output=0;SDL_CloseAudioDevice(device);
}
static void audit_blank_song(unsigned rows);
static void project_roundtrip(void);
static double transport_audio_energy(void) {
    float out[1024];double energy=0;
    audio_callback(test_audio,(Uint8*)out,sizeof(out));
    for(int i=0;i<1024;++i)energy+=fabsf(out[i]);
    return energy;
}
typedef struct {SDL_Event click;atomic_int timed_out;} ModalInput;
static ModalInput *modal_pending;
static SDL_Surface *modal_capture;
static void modal_present(void *context,const uint32_t *pixels) {
    embedded_present(context,pixels);
    assert(!SDL_RenderReadPixels(embedded_host.renderer,NULL,modal_capture->format->format,
                                modal_capture->pixels,modal_capture->pitch));
    /* Respond only after the dialog is visible. Both clicks share one batch. */
    if(modal_pending) {
        assert(SDL_PushEvent(&modal_pending->click)==1);
        modal_pending->click.type=SDL_MOUSEBUTTONUP;
        assert(SDL_PushEvent(&modal_pending->click)==1);modal_pending=NULL;
    }
}
static Uint32 modal_input(Uint32 interval,void *context) {
    (void)interval;ModalInput *input=context;
    atomic_store(&input->timed_out,1);
    SDL_Event event;SDL_zero(event);event.type=SDL_KEYDOWN;
    event.key.keysym.sym=SDLK_ESCAPE;SDL_PushEvent(&event);return 0;
}
static void shrink_dialog(void) {
    audit_blank_song(64);
    ts_tapehead_close();
    TsTapeHeadHost host={test_window,NULL,embedded_lock,embedded_unlock,modal_present};
    assert(ts_tapehead_init(&host,48000,test_error,sizeof(test_error)));
    assert(ts_tapehead_sync(test_pages,test_bank,48000,test_error,sizeof(test_error)));
    test_ui->tracker_embedded_frame=ts_tapehead_frame();
    SDL_SetWindowSize(test_window,1400,900);
    embedded_host.renderer=SDL_CreateRenderer(test_window,-1,SDL_RENDERER_SOFTWARE);
    assert(embedded_host.renderer);
    embedded_host.texture=SDL_CreateTexture(embedded_host.renderer,SDL_PIXELFORMAT_ARGB8888,
                                            SDL_TEXTUREACCESS_STREAMING,632,400);
    assert(embedded_host.texture);
    uint32_t *pixels=malloc(632*400*4);assert(pixels);
    for(int y=0;y<400;++y)for(int x=0;x<632;++x)
        pixels[y*632+x]=x<316?0xff112233:0xff445566;
    embedded_present(NULL,pixels);free(pixels);
    SDL_Surface *surface=SDL_CreateRGBSurfaceWithFormat(0,1400,900,32,SDL_PIXELFORMAT_ARGB8888);
    modal_capture=surface;
    assert(surface && !SDL_RenderReadPixels(embedded_host.renderer,NULL,surface->format->format,surface->pixels,surface->pitch));
    uint32_t *row=(uint32_t*)((Uint8*)surface->pixels+899*surface->pitch);
    assert((row[0]&0xffffff)==0 && (row[1399]&0xffffff)==0);
    assert(row[100]==0xff112233 && row[1300]==0xff445566);
    for(int confirm=0;confirm<2;++confirm) {
        /* Raw window coordinates, delivered in one event batch. */
        SDL_Event event;SDL_zero(event);event.type=SDL_MOUSEBUTTONDOWN;
        event.button.windowID=SDL_GetWindowID(test_window);event.button.button=SDL_BUTTON_LEFT;
        event.button.x=(confirm?254:354)*1400/640;event.button.y=299*900/400;
        ModalInput input={0};input.click=event;atomic_init(&input.timed_out,0);
        modal_pending=&input;
        SDL_TimerID timeout=SDL_AddTimer(1000,modal_input,&input);assert(timeout);
        click(265*1400/640,68*900/400);SDL_RemoveTimer(timeout);
        assert(!atomic_load(&input.timed_out) && !modal_pending);
        assert(pat()->rows==(confirm?32:64));
        assert(ts_sister_tracker_validate(&test_pages->tracker,test_error,sizeof(test_error)));
    }
    assert(!SDL_SaveBMP(surface,"embedded-shrink-dialog.bmp"));SDL_FreeSurface(surface);
    modal_capture=NULL;
    press(SDLK_z,SDL_SCANCODE_Z,KMOD_CTRL);assert(pat()->rows==64);
    SDL_DestroyTexture(embedded_host.texture);SDL_DestroyRenderer(embedded_host.renderer);
    embedded_host.texture=NULL;embedded_host.renderer=NULL;
    SDL_SetWindowSize(test_window,640,400);
}
static void pattern_transport_controls(int extended_length) {
    float out[1024];
    audit_blank_song(extended_length?2:64);
    if(extended_length) {
        /* A LEN control head can display rows beyond the physical pattern. */
        uint8_t *score=test_pages->tracker.embedded_data;
        score[51]=1;score[52]=16;score[54]=1;score[57]=1;
        test_pages->tracker.control_lane=0;test_pages->tracker.lanes[0].length=16;
        test_pages->tracker.lanes[0].mode=TS_TRACKER_PATTERN;
    } else {test_pages->tracker.embedded_data[12]=6;test_pages->tracker.ticks_per_line=6;}
    assert(ts_instrument_select_bank(test_bank,1,test_error,sizeof(test_error)));
    test_bank->has_loop=1;test_bank->loop_first=0;test_bank->loop_last=test_bank->current.frames;
    ++test_bank->generation;assert(ts_instrument_sync_selected(test_bank,test_error,sizeof(test_error)));
    assert(ts_tapehead_sync(test_pages,test_bank,48000,test_error,sizeof(test_error)));
    press(SDLK_z,SDL_SCANCODE_Z,KMOD_NONE);
    SDL_setenv("SDL_AUDIODRIVER","dummy",1);assert(!SDL_InitSubSystem(SDL_INIT_AUDIO));
    SDL_AudioSpec spec={0};spec.freq=48000;spec.channels=2;spec.format=AUDIO_F32SYS;spec.samples=512;
    spec.callback=audio_callback;spec.userdata=test_audio;
    SDL_AudioDeviceID device=SDL_OpenAudioDevice(NULL,0,&spec,NULL,0);assert(device);
    ts_real_output=device;embedded_host.device=&device;
    click(390,27);assert(ts_tapehead_running() && !ts_tapehead_block_active());
    for(int n=0;n<24;++n) {
        audio_callback(test_audio,(Uint8*)out,sizeof(out));
        SDL_Delay(11); /* Drain the same timestamped row updates as a live device. */
        tracker_refresh(0,test_audio,test_ui,test_pages,test_bank,48000,NULL);
    }
    if(!extended_length)assert(transport_audio_energy()>.01);
    SDL_Event e;SDL_zero(e);e.type=SDL_KEYDOWN;e.key.windowID=SDL_GetWindowID(test_window);
    SDL_PauseAudioDevice(device,0);SDL_Delay(150);
    e.key.keysym.sym=SDLK_LCTRL;e.key.keysym.scancode=SDL_SCANCODE_LCTRL;e.key.keysym.mod=KMOD_LCTRL;
    SDL_SetModState(KMOD_LCTRL);
    assert(tracker_event(&e,test_window,0,test_audio,test_ui,test_pages,test_bank,48000));
    assert(ts_sister_tracker_validate(&test_pages->tracker,test_error,sizeof(test_error)));
    if(extended_length)assert(test_pages->tracker.editor_row>=pat()->rows);
    assert(ts_tapehead_running());
    tracker_refresh(0,test_audio,test_ui,test_pages,test_bank,48000,NULL);
    assert(ts_tapehead_running());
    e.type=SDL_KEYUP;e.key.keysym.mod=KMOD_NONE;SDL_SetModState(KMOD_NONE);
    assert(tracker_event(&e,test_window,0,test_audio,test_ui,test_pages,test_bank,48000));
    tracker_refresh(0,test_audio,test_ui,test_pages,test_bank,48000,NULL);
    assert(ts_tapehead_running());
    SDL_PauseAudioDevice(device,1);
    if(!extended_length)assert(transport_audio_energy()>.01);
    SDL_CloseAudioDevice(device);ts_real_output=0;embedded_host.device=NULL;
    if(extended_length) {
        project_roundtrip(); /* Extended editor positions also survive disk persistence. */
        TsSisterTracker *t=&test_pages->tracker;unsigned row=t->editor_row;
        uint8_t saved=t->embedded_data[19];
        t->embedded_data[19]=16;t->editor_row=16;
        assert(!ts_sister_tracker_validate(t,test_error,sizeof(test_error)));
        t->embedded_data[19]=saved;t->editor_row=row;
        assert(ts_sister_tracker_validate(t,test_error,sizeof(test_error)));
    }
    workspace_buttons(0);
    if(!extended_length)assert(transport_audio_energy()>.01);
    click(390,44);assert(!ts_tapehead_running());
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
    state.page_count=test_pages->page_count;state.active_page=test_pages->active_page;
    assert(ts_sample_pages_save_project(test_pages,test_bank,record,&state,"embedded-project/embedded-project.tsr",test_error,sizeof(test_error)));
    assert(ts_sample_pages_load_project(pages,restored,record,"embedded-project/embedded-project.tsr",test_error,sizeof(test_error)));
    assert(ts_sister_tracker_hash(&pages->tracker)==ts_sister_tracker_hash(&test_pages->tracker));
    ts_sample_pages_free(pages);ts_instrument_free(restored);ts_instrument_free(record);free(pages);free(restored);free(record);
}
static uint8_t *prefs(void) {TsSisterTracker *t=&test_pages->tracker;return t->embedded_data+t->embedded_size-128;}
static void export_score(void) {assert(ts_tapehead_export(&test_pages->tracker,test_error,sizeof(test_error)));}
static void mouse_click(int x,int y,int button) {
    SDL_Event event;SDL_zero(event);event.type=SDL_MOUSEBUTTONDOWN;
    event.button.windowID=SDL_GetWindowID(test_window);event.button.button=button;event.button.x=x;event.button.y=y;
    assert(tracker_event(&event,test_window,0,test_audio,test_ui,test_pages,test_bank,48000));
    event.type=SDL_MOUSEBUTTONUP;assert(tracker_event(&event,test_window,0,test_audio,test_ui,test_pages,test_bank,48000));
}
static void audit_blank_song(unsigned rows) {
    ts_tapehead_stop();export_score();TsSisterTracker *t=&test_pages->tracker;
    uint8_t *s=t->embedded_data,*record=s+52+80+256;
    s[12]=1;s[14]=1;s[15]=s[16]=s[17]=s[19]=s[20]=0;s[18]=record[0];
    s[24]=s[25]=1;s[26]=s[51]=0;s[52+80]=record[0];
    record[1]=rows;record[2]=rows>>8;memset(record+7,0,256*8*7);
    TsPatternId id=record[3]|(uint32_t)record[4]<<8|(uint32_t)record[5]<<16|(uint32_t)record[6]<<24;
    ts_sister_tracker_pattern(t,id)->rows=rows;t->editor_pattern=id;t->editor_row=0;t->ticks_per_line=1;
    for(int lane=0;lane<8;++lane) {uint8_t *l=s+52+lane*10;memset(l,0,10);l[3]=7;l[8]=1;}
    assert(ts_tapehead_sync(test_pages,test_bank,48000,test_error,sizeof(test_error)));export_score();
}
static void audit_recording(void) {
    audit_blank_song(7);
    click(90,10);assert(test_pages->tracker.order_count==2 && pat()->rows==7); /* IPL + INP */
    press(SDLK_HOME,SDL_SCANCODE_HOME,KMOD_NONE);press(SDLK_z,SDL_SCANCODE_Z,KMOD_NONE);
    TsTrackerCell copied=pat()->cells[0][0];assert(copied.note_kind==TS_TRACKER_NOTE_PITCH);
    SDL_SetModState(KMOD_SHIFT);click(90,10);SDL_SetModState(KMOD_NONE);
    assert(test_pages->tracker.order_count==3 && pat()->rows==7);
    assert(!memcmp(&copied,&pat()->cells[0][0],sizeof(copied))); /* Shift+INP duplicates */
    audit_blank_song(3);click(390,60);assert(ts_tapehead_running());
    float out[2048];for(int i=0;i<8;++i)audio_callback(test_audio,(Uint8*)out,sizeof(out));
    export_score();assert(test_pages->tracker.order_count>=2);
    for(unsigned i=0;i<test_pages->tracker.order_count;++i)
        assert(ts_sister_tracker_pattern(&test_pages->tracker,test_pages->tracker.orders[i])->rows==3);
    ts_tapehead_stop();
    for(int silent=0;silent<=1;++silent) {
        audit_blank_song(32);prefs()[4]=silent;prefs()[7]=0;
        assert(ts_tapehead_sync(test_pages,test_bank,48000,test_error,sizeof(test_error)));
        click(390,77);assert(ts_tapehead_running());
        audio_callback(test_audio,(Uint8*)out,128*2*sizeof(float));
        TsMidiEvent midi={0};assert(ts_midi_decode_short_message(0x90,76,100,&midi));
        assert(ts_tapehead_midi(&midi,1));
        audio_callback(test_audio,(Uint8*)out,sizeof(out));
        float energy=0;for(int i=0;i<2048;++i)energy+=fabsf(out[i]);
        if(silent)assert(energy<1e-6f);else assert(energy>.001f);
        export_score();int found=0;for(int lane=0;lane<8;++lane)found|=pat()->cells[0][lane].note==76;assert(found);
        midi.action=TS_MIDI_ACTION_NOTE_OFF;ts_tapehead_midi(&midi,0);ts_tapehead_stop();
    }
}

static void audit_controls(const char *config_image,const char *record_image) {
    TsSisterTracker saved;ts_sister_tracker_init(&saved);ts_tapehead_stop();export_score();
    assert(ts_sister_tracker_clone(&saved,&test_pages->tracker,test_error,sizeof(test_error)));
    /* Open the visible Config button, not a test-only preference setter. */
    click(390,146);assert(ts_tapehead_config_visible());
    unsigned silent=prefs()[4];click(20,45);assert(prefs()[4]==!silent);
    press(SDLK_BACKQUOTE,SDL_SCANCODE_GRAVE,KMOD_CTRL);assert(prefs()[4]==silent);
    if(!prefs()[5])click(20,58);assert(prefs()[5]);
    if(!prefs()[6])click(20,71);assert(prefs()[6]);
    if(!prefs()[7])click(20,84);assert(prefs()[7]);
    /* Disabling INP must disarm APG, with no misleading checked box. */
    click(20,71);assert(!prefs()[6]&&!prefs()[7]);click(20,84);assert(!prefs()[7]);
    click(20,71);click(20,84);assert(prefs()[6]&&prefs()[7]);
    if(record_image) {ts_tapehead_tick();snapshot(record_image);}
    /* Palette presets and RGB controls work on Recording as well as Layout. */
    unsigned pal=prefs()[27];click(550,96);assert(prefs()[27]==(pal+1)%12);
    click(430,9);click(626,19);assert(prefs()[27]==11);
    uint8_t colors[62];memcpy(colors,prefs()+32,sizeof(colors));
    wheel(430,30,-9);click(430,9);click(626,19);
    assert(memcmp(colors,prefs()+32,sizeof(colors))); /* Includes individual field/playhead colors. */
    click(149,26);assert(ts_tapehead_config_visible());
    unsigned hex=prefs()[18];click(20,58);assert(prefs()[18]==!hex);
    unsigned font=prefs()[24];click(300,95);assert(prefs()[24]==(font+1)%4);
    if(config_image) {ts_tapehead_tick();snapshot(config_image);}
    uint8_t expected[128];memcpy(expected,prefs(),sizeof(expected));
    assert(ts_tapehead_preferences_save("audit-defaults.cfg",test_error,sizeof(test_error)));
    click(20,58);assert(prefs()[18]!=expected[18]);
    assert(ts_tapehead_preferences_load("audit-defaults.cfg",test_error,sizeof(test_error)));export_score();
    assert(!memcmp(prefs(),expected,sizeof(expected)));remove("audit-defaults.cfg");
    /* Escape closes Config without leaving the tracker or prompting to quit. */
    press(SDLK_ESCAPE,SDL_SCANCODE_ESCAPE,KMOD_NONE);assert(test_ui->tracker_open&&!ts_tapehead_config_visible());
    /* Hovering the tile panel cannot change the invisible sample slot. */
    unsigned alias=test_pages->tracker.embedded_data[22];wheel(490,132,-1);assert(test_pages->tracker.embedded_data[22]==alias);
    /* The removed song-name hit target must not capture typing over the tiles. */
    uint8_t old_title[21];memcpy(old_title,test_pages->tracker.embedded_data+30,sizeof(old_title));
    click(475,162);SDL_Event text;SDL_zero(text);text.type=SDL_TEXTINPUT;text.text.windowID=SDL_GetWindowID(test_window);
    snprintf(text.text.text,sizeof(text.text.text),"Audit score");
    assert(tracker_event(&text,test_window,0,test_audio,test_ui,test_pages,test_bank,48000));
    assert(!memcmp(test_pages->tracker.embedded_data+30,old_title,sizeof(old_title)));
    click(325,61);assert(ts_tapehead_action()==TS_TH_SAVE_PROJECT);
    click(325,44);assert(ts_tapehead_action()==TS_TH_OPEN_PROJECT);
    click(390,95);assert(ts_tapehead_action()==TS_TH_CAPTURE);
    /* Ratio wheel selects a usable private head. LEN wheel clamps at maximum. */
    wheel(45,187,1);assert(test_pages->tracker.lanes[0].mode==TS_TRACKER_PATTERN);
    unsigned ratio=test_pages->tracker.lanes[0].ratio;wheel(45,187,-1);
    assert(test_pages->tracker.lanes[0].ratio==(ratio+16)%17);
    ratio=test_pages->tracker.lanes[0].ratio;
    wheel(45,187,.25f);wheel(45,187,.25f);wheel(45,187,.25f);assert(test_pages->tracker.lanes[0].ratio==ratio);
    wheel(45,187,.25f);assert(test_pages->tracker.lanes[0].ratio==(ratio+1)%17);
    unsigned step=test_pages->tracker.edit_step;wheel(45,187,.5f);wheel(156,66,.5f);assert(test_pages->tracker.edit_step==step);
    wheel(156,66,.5f);assert(test_pages->tracker.edit_step==(step+1)%17);
    uint8_t *s=test_pages->tracker.embedded_data;s[52]=0;s[53]=1;
    assert(ts_tapehead_sync(test_pages,test_bank,48000,test_error,sizeof(test_error)));
    wheel(45,179,1);assert(test_pages->tracker.lanes[0].length==256);
    SDL_SetModState(KMOD_CTRL);wheel(45,179,1);SDL_SetModState(KMOD_NONE);assert(!test_pages->tracker.lanes[0].length);
    SDL_SetModState(KMOD_SHIFT);wheel(45,179,1);SDL_SetModState(KMOD_NONE);assert(test_pages->tracker.lanes[0].length==8);
    /* Right-click cycles Forward, Reverse, Bounce without affecting the ratio. */
    fastTracksPOCSetDirection(0,0);export_score();
    mouse_click(50,187,SDL_BUTTON_RIGHT);assert(test_pages->tracker.lanes[0].direction==TS_TRACKER_REVERSE);
    mouse_click(50,187,SDL_BUTTON_RIGHT);assert(test_pages->tracker.lanes[0].direction==TS_TRACKER_PING_PONG);
    ratio=test_pages->tracker.lanes[0].ratio;
    press(SDLK_BACKSPACE,SDL_SCANCODE_BACKSPACE,KMOD_CTRL|KMOD_ALT);
    wheel(45,14,1);assert(test_pages->tracker.lanes[0].ratio==(ratio+1)%17);
    press(SDLK_BACKSPACE,SDL_SCANCODE_BACKSPACE,KMOD_CTRL|KMOD_ALT);
    export_score();project_roundtrip();
    audit_recording();
    /* Actual audio callbacks traverse 0,1,2,3,2,1,0,1 without repeating endpoints. */
    fastTracksPOCSetMode(0,FAST_TRACKS_MODE_PATTERN);
    fastTracksPOCSetTrackLength(0,0,4);fastTracksPOCSetRatioIndex(0,7);fastTracksPOCSetDirection(0,2);
    fastTracksPOCSetUsesTrackLengths(true);fastTracksPOCSetControlTrack(0,-1);
    export_score();s=test_pages->tracker.embedded_data;s[12]=1;test_pages->tracker.ticks_per_line=1;s[19]=s[20]=0;test_pages->tracker.editor_row=0;
    assert(ts_tapehead_sync(test_pages,test_bank,48000,test_error,sizeof(test_error)));click(390,25);
    int previous=fastTracksPOCGetSourceRow(0),observed=0;const int path[]={1,2,3,2,1,0,1};
    float out[128];
    for(int block=0;block<300 && observed<7;++block) {
        audio_callback(test_audio,(Uint8*)out,sizeof(out));int row=fastTracksPOCGetSourceRow(0);
        if(row!=previous) {assert(row==path[observed++]);previous=row;}
    }
    assert(observed==7);ts_tapehead_stop();
    /* At 5:1 every crossed row is emitted, including a bounce inside one tick. */
    fastTracksRuntimeState_t runtime;fastTracksPOCGetRuntimeState(&runtime);
    runtime.tracks[0].sourceRow=0;runtime.tracks[0].reversed=false;runtime.tracks[0].transportStarted=false;
    runtime.tracks[0].tickAccumulator=0;runtime.tracks[0].ratioIndex=16;runtime.tracks[0].lastTPL=1;
    fastTracksPOCSetRuntimeState(&runtime);fastTracksCrossing_t crossings[8];
    assert(!fastTracksPOCAdvanceAudio(0,0,1,crossings,8));
    assert(fastTracksPOCAdvanceAudio(0,0,1,crossings,8)==5);
    for(int i=0;i<5;++i)assert(crossings[i].sourceRow==path[i]);
    fastTracksPOCSetTrackLength(0,0,1);assert(fastTracksPOCAdvanceAudio(0,0,1,crossings,8)==5);
    for(int i=0;i<5;++i)assert(!crossings[i].sourceRow);
    /* Song Bounce crosses unequal pattern boundaries, reflecting only at song ends. */
    export_score();s=test_pages->tracker.embedded_data;
    uint8_t *first=s+52+80+256,*second=first+7+256*8*7;assert(s[8]>=2);
    first[1]=3;first[2]=0;second[1]=2;second[2]=0;s[14]=2;s[15]=0;
    s[52+80]=first[0];s[52+80+1]=second[0];s[52]=s[53]=0;s[54]=FAST_TRACKS_MODE_SONG;s[55]=7;s[56]=2;
    s[19]=s[20]=0;
    assert(ts_tapehead_sync(test_pages,test_bank,48000,test_error,sizeof(test_error)));
    fastTracksPOCGetRuntimeState(&runtime);runtime.tracks[0].sourceOrder=0;runtime.tracks[0].sourceRow=0;
    runtime.tracks[0].transportStarted=false;runtime.tracks[0].tickAccumulator=0;runtime.tracks[0].lastTPL=1;
    fastTracksPOCSetRuntimeState(&runtime);assert(!fastTracksPOCAdvanceAudio(0,0,1,crossings,8));
    const int song_orders[]={0,0,1,1,1,0,0,0,0},song_rows[]={1,2,0,1,0,2,1,0,1};
    for(int i=0;i<9;++i) {
        assert(fastTracksPOCAdvanceAudio(0,0,1,crossings,8)==1);
        assert(fastTracksPOCGetSourceOrder(0)==song_orders[i] && crossings[0].sourceRow==song_rows[i]);
    }
    /* Old STH1 projects still load. Truncated/invalid new preferences do not. */
    export_score();s=test_pages->tracker.embedded_data;unsigned size=test_pages->tracker.embedded_size;
    assert(!ts_tracker_embedded_validate(s,size-1));uint8_t before=prefs()[32];prefs()[32]=255;
    assert(!ts_tracker_embedded_validate(s,size));prefs()[32]=before;
    s[3]='1';s[56]=0;s[25]=0;s[29]=2;assert(ts_tracker_embedded_validate(s,size-128));
    test_pages->tracker.embedded_size-=128;
    assert(ts_tapehead_sync(test_pages,test_bank,48000,test_error,sizeof(test_error)));export_score();
    assert(prefs()[29]==0 && prefs()[28]==2); /* STH1 retains its own LEN and colors. */
    ts_sister_tracker_free(&test_pages->tracker);test_pages->tracker=saved;
    assert(ts_tapehead_sync(test_pages,test_bank,48000,test_error,sizeof(test_error)));export_score();
}

static void followup_interpolation(void) {
    audit_blank_song(16);
    uint8_t *record=test_pages->tracker.embedded_data+52+80+256+7;
    record[0]=49;record[1]=1;record[8*8*7]=61;record[8*8*7+1]=1;
    assert(ts_tapehead_sync(test_pages,test_bank,48000,test_error,sizeof(test_error)));export_score();
    press(SDLK_HOME,SDL_SCANCODE_HOME,KMOD_NONE);
    for(int i=0;i<9;++i)press(SDLK_DOWN,SDL_SCANCODE_DOWN,KMOD_ALT);
    press(SDLK_i,SDL_SCANCODE_I,KMOD_CTRL|KMOD_SHIFT);
    assert(ts_tapehead_interpolation_active());
    assert(pat()->cells[4][0].note_kind==TS_TRACKER_NOTE_PITCH);
    press(SDLK_ESCAPE,SDL_SCANCODE_ESCAPE,KMOD_NONE);
    assert(test_ui->tracker_open && !ts_tapehead_interpolation_active());
    assert(pat()->cells[4][0].note_kind==TS_TRACKER_NOTE_NONE);
    press(SDLK_i,SDL_SCANCODE_I,KMOD_CTRL|KMOD_SHIFT);
    press(SDLK_2,SDL_SCANCODE_2,KMOD_NONE); /* Major scale preview. */
    press(SDLK_RETURN,SDL_SCANCODE_RETURN,KMOD_NONE);
    assert(!ts_tapehead_interpolation_active() && pat()->cells[4][0].note_kind==TS_TRACKER_NOTE_PITCH);
    TsTrackerCell accepted=pat()->cells[4][0];
    press(SDLK_z,SDL_SCANCODE_Z,KMOD_CTRL);assert(pat()->cells[4][0].note_kind==TS_TRACKER_NOTE_NONE);
    press(SDLK_y,SDL_SCANCODE_Y,KMOD_CTRL);assert(!memcmp(&accepted,&pat()->cells[4][0],sizeof(accepted)));
    /* Volume, panning effect and tuning previews retain their original chords. */
    const SDL_Keycode keys[]={SDLK_v,SDLK_b,SDLK_t};
    const SDL_Scancode scans[]={SDL_SCANCODE_V,SDL_SCANCODE_B,SDL_SCANCODE_T};
    for(int kind=0;kind<3;++kind) {
        audit_blank_song(9);uint8_t *raw=test_pages->tracker.embedded_data+52+80+256+7;
        if(kind==0) {raw[2]=0x10;raw[8*8*7+2]=0x50;}
        else if(kind==1) {raw[3]=raw[8*8*7+3]=8;raw[4]=0;raw[8*8*7+4]=255;}
        else {raw[5]=raw[8*8*7+5]=0x16;raw[6]=0;raw[8*8*7+6]=255;}
        assert(ts_tapehead_sync(test_pages,test_bank,48000,test_error,sizeof(test_error)));export_score();
        press(SDLK_HOME,SDL_SCANCODE_HOME,KMOD_NONE);
        for(int i=0;i<9;++i)press(SDLK_DOWN,SDL_SCANCODE_DOWN,KMOD_ALT);
        press(keys[kind],scans[kind],KMOD_CTRL|KMOD_SHIFT);assert(ts_tapehead_interpolation_active());
        press(SDLK_RETURN,SDL_SCANCODE_RETURN,KMOD_NONE);assert(!ts_tapehead_interpolation_active());
        raw=test_pages->tracker.embedded_data+52+80+256+7;
        assert(raw[4*8*7+(kind==0?2:kind==1?4:6)]==(kind==0?0x30:128));
        press(SDLK_z,SDL_SCANCODE_Z,KMOD_CTRL);
        raw=test_pages->tracker.embedded_data+52+80+256+7;assert(!raw[4*8*7+(kind==0?2:kind==1?4:6)]);
    }
    SDL_Event midi;SDL_zero(midi);midi.type=SDL_KEYDOWN;midi.key.windowID=SDL_GetWindowID(test_window);
    midi.key.keysym.sym=SDLK_m;midi.key.keysym.mod=KMOD_CTRL|KMOD_SHIFT;
    assert(!tracker_event(&midi,test_window,0,test_audio,test_ui,test_pages,test_bank,48000));
    assert(!ts_tapehead_interpolation_active());
}
static void followup_canvas_palette(const char *image,const char *config_image,const char *browser_image) {
    remove("followup-out.pal");
    export_score();
    if(test_pages->tracker.embedded_data[28])press(SDLK_BACKSPACE,SDL_SCANCODE_BACKSPACE,KMOD_CTRL|KMOD_ALT);
    else if(test_pages->tracker.embedded_data[27])press(SDLK_BACKSPACE,SDL_SCANCODE_BACKSPACE,KMOD_ALT);
    ts_tapehead_tick();
    /* Load opens the actual host load page; the previous synthetic key was unreliable. */
    click(325,44);tracker_refresh(0,test_audio,test_ui,test_pages,test_bank,48000,NULL);
    assert(!test_ui->tracker_open && test_ui->browser.mode==TS_BROWSER_LOAD_WAV);
    ts_browser_close(&test_ui->browser);press(SDLK_F10,SDL_SCANCODE_F10,KMOD_NONE);
    click(390,146);assert(ts_tapehead_config_visible());click(590,136);export_score();
    assert(prefs()[27]==11 && prefs()[32]==63 && prefs()[33]==7 && prefs()[92]==52 && prefs()[93]==57);
    TsPalette p;assert(ts_tapehead_palette_export("followup.pal",test_error,sizeof(test_error)));
    assert(ts_palette_load(&p,"followup.pal",test_error,sizeof(test_error)));
    p.colors[TS_PALETTE_PATTERN_NOTE]=0x336699;p.colors[TS_PALETTE_ACTIVE_TILE]=0x123456;
    assert(ts_palette_save(&p,"followup.pal",test_error,sizeof(test_error)));
    snprintf(test_ui->browser.directory,sizeof(test_ui->browser.directory),".");
    click(440,136);tracker_refresh(0,test_audio,test_ui,test_pages,test_bank,48000,NULL);
    assert(test_ui->tracker_open && test_ui->browser.mode==TS_BROWSER_TRACKER_PALETTE_IMPORT);
    if(browser_image)snapshot(browser_image);
    int entry=-1;for(int i=0;i<test_ui->browser.entry_count;++i)
        if(!strcmp(test_ui->browser.entries[i].name,"followup.pal"))entry=i;
    assert(entry>=0);ts_browser_select(&test_ui->browser,entry);
    PendingFileOperation *pending=calloc(1,sizeof(*pending));assert(pending);
    browser_action(0,test_audio,test_ui,test_bank,NULL,test_pages,NULL,0,pending,0);
    assert(test_ui->browser.mode==TS_BROWSER_CLOSED && ts_tapehead_config_visible());
    assert(prefs()[50]==13 && prefs()[51]==25 && prefs()[52]==38);
    click(517,136);tracker_refresh(0,test_audio,test_ui,test_pages,test_bank,48000,NULL);
    assert(test_ui->browser.mode==TS_BROWSER_TRACKER_PALETTE_EXPORT);
    ts_browser_set_filename(&test_ui->browser,"followup-out.pal");
    browser_action(0,test_audio,test_ui,test_bank,NULL,test_pages,NULL,0,pending,0);
    assert(test_ui->browser.mode==TS_BROWSER_CLOSED);
    assert(ts_palette_load(&p,"followup-out.pal",test_error,sizeof(test_error)));
    assert(p.colors[TS_PALETTE_ACTIVE_TILE]==0xff123456u && p.colors[TS_PALETTE_PATTERN_NOTE]==0xff35659au);
    uint8_t previous[62];memcpy(previous,prefs()+32,62);
    FILE *bad=fopen("followup-bad.pal","wb");assert(bad);fputs("[Palette]\nPatternNote=oops\n",bad);fclose(bad);
    assert(!ts_tapehead_palette_import("followup-bad.pal",test_error,sizeof(test_error)));export_score();
    assert(!memcmp(previous,prefs()+32,62));
    remove("followup.pal");remove("followup-out.pal");remove("followup-bad.pal");free(pending);
    click(590,136);if(config_image){ts_tapehead_tick();snapshot(config_image);}
    press(SDLK_ESCAPE,SDL_SCANCODE_ESCAPE,KMOD_NONE);
    const char *names[]={"KICK","SNARE","HAT","BASS","PAD","CHORD","BELL","VOICE"};
    for(int i=0;i<8;++i) {
        assert(ts_instrument_select_bank(test_bank,i,test_error,sizeof(test_error)));
        if(!test_bank->current.data)assert(ts_instrument_activate_silence(test_bank,2048,48000,test_error,sizeof(test_error)));
        snprintf(test_bank->current.name,sizeof(test_bank->current.name),"%s",names[i]);
        for(size_t f=0;f<test_bank->current.frames;++f)for(unsigned c=0;c<test_bank->current.channels;++c)
            test_bank->current.data[f*test_bank->current.channels+c]=.8f*sinf((float)f*.1f*(i+1))*expf(-3.f*f/test_bank->current.frames);
        ++test_bank->generation;assert(ts_instrument_sync_selected(test_bank,test_error,sizeof(test_error)));
    }
    assert(ts_tapehead_sync(test_pages,test_bank,48000,test_error,sizeof(test_error)));ts_tapehead_tick();
    click(488,34);unsigned alias=test_pages->tracker.embedded_data[22];
    assert(test_pages->tracker.aliases[alias]==test_bank->bank[1].tile_id);
    assert(ts_tapehead_sync(test_pages,test_bank,48000,test_error,sizeof(test_error)));export_score();
    assert(test_pages->tracker.embedded_data[22]==alias); /* Host selection did not silently override it. */
    click(590,112);assert(test_pages->tracker.embedded_data[22]==alias); /* Empty tile is inert. */
    audit_blank_song(16);press(SDLK_HOME,SDL_SCANCODE_HOME,KMOD_NONE);press(SDLK_z,SDL_SCANCODE_Z,KMOD_NONE);
    assert(pat()->cells[0][0].tile_id==test_bank->bank[1].tile_id);
    assert(ts_sample_pages_append(test_pages,test_error,sizeof(test_error)));
    TsInstrument *other=ts_sample_pages_page_mut(test_pages,test_bank,1);assert(other);
    assert(ts_instrument_select_bank(other,2,test_error,sizeof(test_error)));
    assert(ts_instrument_activate_silence(other,1024,48000,test_error,sizeof(test_error)));
    assert(ts_instrument_sync_selected(other,test_error,sizeof(test_error)));
    assert(ts_tapehead_sync(test_pages,test_bank,48000,test_error,sizeof(test_error)));ts_tapehead_tick();
    click(540,133);alias=test_pages->tracker.embedded_data[22]; /* Fifth row spans the next host page. */
    assert(test_pages->tracker.aliases[alias]==other->bank[2].tile_id);
    press(SDLK_z,SDL_SCANCODE_Z,KMOD_NONE);assert(pat()->cells[1][0].tile_id==other->bank[2].tile_id);
    assert(ts_instrument_select_bank(other,7,test_error,sizeof(test_error)));
    assert(ts_instrument_activate_silence(other,1024,48000,test_error,sizeof(test_error)));
    assert(ts_instrument_sync_selected(other,test_error,sizeof(test_error)));
    assert(ts_instrument_select_bank(other,15,test_error,sizeof(test_error)));
    assert(ts_instrument_activate_silence(other,1024,48000,test_error,sizeof(test_error)));
    assert(ts_instrument_sync_selected(other,test_error,sizeof(test_error)));
    assert(ts_tapehead_sync(test_pages,test_bank,48000,test_error,sizeof(test_error)));ts_tapehead_tick();
    click(590,158);alias=test_pages->tracker.embedded_data[22]; /* Last button uses the old title area. */
    assert(test_pages->tracker.aliases[alias]==other->bank[7].tile_id);
    click(577,158);assert(test_pages->tracker.embedded_data[22]==alias); /* Gutter is inert. */
    click(618,10);click(590,58);alias=test_pages->tracker.embedded_data[22];
    assert(test_pages->tracker.aliases[alias]==other->bank[15].tile_id);
    click(590,158);assert(test_pages->tracker.embedded_data[22]==alias); /* Past the last host page. */
    wheel(510,100,1);click(488,34);
    assert(test_pages->tracker.aliases[test_pages->tracker.embedded_data[22]]==test_bank->bank[1].tile_id);
    followup_interpolation();
    if(image) {
        audit_blank_song(32);uint8_t *raw=test_pages->tracker.embedded_data+52+80+256+7;
        const int slots[]={0,1,2,3};
        for(int lane=0;lane<4;++lane)for(int row=lane;row<32;row+=lane==2?2:4) {
            int a=1;while(a<=128 && test_pages->tracker.aliases[a]!=test_bank->bank[slots[lane]].tile_id)++a;
            uint8_t *cell=raw+(row*8+lane)*7;cell[0]=lane==3?37+(row/4)%5:49;cell[1]=a;cell[2]=0x30+row%16;
        }
        assert(ts_tapehead_sync(test_pages,test_bank,48000,test_error,sizeof(test_error)));
        fastTracksPOCSetMode(2,FAST_TRACKS_MODE_PATTERN);fastTracksPOCSetRatioIndex(2,8);
        fastTracksPOCSetMode(3,FAST_TRACKS_MODE_PATTERN);fastTracksPOCSetDirection(3,2);fastTracksPOCSetTrackLength(0,3,13);
        press(SDLK_DOWN,SDL_SCANCODE_DOWN,KMOD_NONE);press(SDLK_DOWN,SDL_SCANCODE_DOWN,KMOD_NONE);
        ts_tapehead_tick();snapshot(image);
    }
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
    handle_midi_event(0,test_audio,test_ui,test_bank,NULL,&midi,48000,1);
    assert(pat()->cells[midi_row][0].note==67 && pat()->cells[midi_row][0].tile_id==tile);
    assert(pat()->cells[midi_row][0].has_volume && !ts_note_bank_count(&test_audio->notes));
    assert(ts_midi_decode_short_message(0x82,67,0,&midi));handle_midi_event(0,test_audio,test_ui,test_bank,NULL,&midi,48000,1);
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
    if(argc>2) {tracker_refresh(0,test_audio,test_ui,test_pages,test_bank,48000,NULL);snapshot(argv[2]);}
    /* Original marking and Ctrl+L block transport. */
    press(SDLK_HOME,SDL_SCANCODE_HOME,KMOD_NONE);
    press(SDLK_DOWN,SDL_SCANCODE_DOWN,KMOD_ALT);
    press(SDLK_DOWN,SDL_SCANCODE_DOWN,KMOD_ALT);
    press(SDLK_l,SDL_SCANCODE_L,KMOD_CTRL);assert(ts_tapehead_block_active());
    float rendered[512*2];audio_callback(test_audio,(Uint8*)rendered,sizeof(rendered));
    float left=0,right=0;for(int i=0;i<512;++i){left+=rendered[i*2];right+=rendered[i*2+1];}
    assert(left>0.01f && right<-.01f);
    exact_cycle_capture();
    workspace_buttons(1);
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
    tracker_refresh(0,test_audio,test_ui,test_pages,test_bank,48000,NULL);
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
    audit_controls(argc>3?argv[3]:NULL,argc>4?argv[4]:NULL);
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
    followup_canvas_palette(argc>2?argv[2]:NULL,argc>3?argv[3]:NULL,argc>5?argv[5]:NULL);
    pattern_transport_controls(0);
    pattern_transport_controls(1);
    shrink_dialog();
    ts_tapehead_close();ts_tracker_playback_free(&test_audio->tracker);ts_sister_runtime_free(&test_audio->sister);
    ts_tracker_edit_free(test_ui->tracker_edit);
    ts_sample_pages_free(test_pages);ts_instrument_free(test_bank);
    free(test_pages);free(test_bank);free(test_ui);free(test_audio);SDL_DestroyWindow(test_window);SDL_Quit();
    puts("Embedded TapeHead host, editor, float stereo, block loop and persistence checks passed");return 0;
}
