/* Actual application controllers and callback; dummy SDL never plays hardware. */
#ifndef SDL_MAIN_HANDLED
#define SDL_MAIN_HANDLED
#endif
#define main tapesister_application_main
#include "../src/main_sdl.c"
#undef main
#include <assert.h>

static void render_loop(AudioState *audio)
{
    float block[512];double energy=0;
    for (int n=0;n<4;++n) {
        audio_callback(audio,(Uint8*)block,sizeof(block));
        assert(audio->playing && audio->looping);
        for (size_t i=0;i<512;++i) {assert(isfinite(block[i]));energy+=fabsf(block[i]);}
    }
    if(audio->range_end-audio->range_start>=100)assert(energy>0);
}

static void test_import_keyboard(SDL_AudioDeviceID device, AudioState *audio,
                                  TsUiState *ui, ImportController *c)
{
    static TsInstrument instrument;TsSample pending;ts_sample_init(&pending);
    ts_instrument_init(&instrument);SDL_Event event={0};
    const TsSample *sample=&c->decoded.sample;uint64_t original=ts_sample_hash(sample);
    uint64_t tile=ts_sample_hash(&instrument.current);
#define IMPORT_KEY(K,T,M) do {memset(&event,0,sizeof(event));event.type=(T);event.key.keysym.sym=(K);event.key.keysym.mod=(M);assert(import_preview_event(&event,device,audio,ui,&instrument,&pending,c,44100));} while(0)
    ui->import_preview_loop=1;ui->import_preview_has_selection=1;
    ui->import_preview_selection_first=1000;ui->import_preview_selection_last=10000;
    ts_ui_keyboard_set_octave(ui,4);
    IMPORT_KEY(SDLK_z,SDL_KEYDOWN,KMOD_NONE);
    TsNoteVoice *low=&audio->notes.voices[0];
    assert(low->active && low->preview && !low->synth && low->sample==sample);
    assert(low->midi_note==60 && low->step==1 && low->position==1000 && !audio->playing);
    IMPORT_KEY(SDLK_q,SDL_KEYDOWN,KMOD_NONE);
    TsNoteVoice *high=&audio->notes.voices[1];
    assert(high->midi_note==72 && high->step==2 && ui->import_preview_note_count==2);
    event.key.repeat=1;assert(import_preview_event(&event,device,audio,ui,&instrument,&pending,c,44100));
    assert(ts_note_bank_count(&audio->notes)==2);
    ts_note_bank_sync(&audio->notes,&instrument,44100);assert(low->sample==sample);
    double left=0,right=0;float block[512];
    for(int n=0;n<4;++n) {
        audio_callback(audio,(Uint8*)block,sizeof(block));
        for(int i=0;i<512;i+=2) {assert(isfinite(block[i]) && isfinite(block[i+1]));left+=fabsf(block[i]);right+=fabsf(block[i+1]);}
    }
    assert(left>0 && right>0);
    if(sample->channels==2)assert(fabs(left-2*right)<.001);
    IMPORT_KEY(SDLK_F6,SDL_KEYDOWN,KMOD_NONE);assert(low->midi_note==60);
    IMPORT_KEY(SDLK_q,SDL_KEYUP,KMOD_CTRL);assert(!high->active && low->active);
    IMPORT_KEY(SDLK_s,SDL_KEYDOWN,KMOD_NONE);assert(high->midi_note==73 && ui->import_preview_open);
    IMPORT_KEY(SDLK_r,SDL_KEYDOWN,KMOD_NONE);assert(ts_note_bank_count(&audio->notes)==3 && !ui->import_preview_raw);
    IMPORT_KEY(SDLK_s,SDL_KEYUP,KMOD_NONE);IMPORT_KEY(SDLK_r,SDL_KEYUP,KMOD_NONE);
    low->position=4000;size_t attack=low->attack_frame;
    resize_import_selection(device,audio,ui,c,61,1);
    assert(low->range_first<1000 && low->position==4000 && low->attack_frame==attack);
    ui->import_preview_selection_first=5000;ui->import_preview_selection_last=6000;
    assert(sync_import_preview_loop(device,audio,ui,c,0) && low->position==5000);
    clear_import_selection(device,audio,ui,c);assert(low->active && low->range_last==sample->frames);
    IMPORT_KEY(SDLK_l,SDL_KEYDOWN,KMOD_NONE);assert(!low->looping && low->active && !audio->playing);
    IMPORT_KEY(SDLK_z,SDL_KEYUP,KMOD_NONE);assert(!low->active);
    IMPORT_KEY(SDLK_l,SDL_KEYDOWN,KMOD_NONE);assert(!low->active && !audio->playing);
    ui->import_preview_playhead=3456;poll_import_playback(device,audio,ui,c);assert(ui->import_preview_playhead==3456);
    ts_ui_keyboard_set_octave(ui,4);
    const SDL_Keycode chord[]={SDLK_z,SDLK_x,SDLK_c,SDLK_v,SDLK_b,SDLK_n};
    for(int i=0;i<6;++i)IMPORT_KEY(chord[i],SDL_KEYDOWN,KMOD_NONE);
    assert(ts_note_bank_count(&audio->notes)==5 && strstr(ui->import_preview_message,"LIMIT"));
    IMPORT_KEY(SDLK_SPACE,SDL_KEYDOWN,KMOD_NONE);
    assert(ts_note_bank_count(&audio->notes)==0 && !audio->playing);
    IMPORT_KEY(SDLK_c,SDL_KEYDOWN,KMOD_CTRL);assert(ts_note_bank_count(&audio->notes)==0);
    IMPORT_KEY(SDLK_z,SDL_KEYDOWN,KMOD_NONE);
    event.type=SDL_WINDOWEVENT;event.window.event=SDL_WINDOWEVENT_FOCUS_LOST;
    assert(!import_preview_event(&event,device,audio,ui,&instrument,&pending,c,44100));
    assert(ts_note_bank_count(&audio->notes)==0);
    assert(ts_sample_hash(sample)==original && ts_sample_hash(&instrument.current)==tile);
    SDL_AudioDeviceID connected=ts_real_output;ts_real_output=0;
    IMPORT_KEY(SDLK_z,SDL_KEYDOWN,KMOD_NONE);assert(ts_note_bank_count(&audio->notes)==0);
    ts_real_output=connected;
    IMPORT_KEY(SDLK_z,SDL_KEYDOWN,KMOD_NONE);
    IMPORT_KEY(SDLK_ESCAPE,SDL_KEYDOWN,KMOD_NONE);
    assert(!ui->import_preview_open && ts_note_bank_count(&audio->notes)==0);
    show_import_preview_tab(ui,c);assert(ui->import_preview_open);
    IMPORT_KEY(SDLK_z,SDL_KEYDOWN,KMOD_NONE);
    IMPORT_KEY(SDLK_r,SDL_KEYDOWN,KMOD_CTRL); /* missing-file decode fails safely */
    assert(!ui->import_preview_raw && ts_note_bank_count(&audio->notes)==0);
    assert(ts_sample_hash(sample)==original);
    for(int i=0;i<TS_NOTE_BANK_VOICE_CAPACITY;++i)assert(audio->notes.voices[i].sample!=sample);
    snprintf(ui->status,sizeof(ui->status),"FILE PREVIEW - CURRENT TILE UNCHANGED");
    snprintf(ui->import_preview_message,sizeof(ui->import_preview_message),"PREVIEW READY - QWERTY PLAYS NOTES; SPACE PLAYS THE RANGE");
    /* The actual late overlay must leave both browser panels byte-identical. */
    static TsFramebuffer before,after;
    ui->show_keyboard=ui->show_recipes=ui->show_ingredients=0;
    ui->import_preview_sample=sample;ui->import_preview_kind=TS_AUDIO_IMPORT_WAV;
    snprintf(ui->import_preview_name,sizeof(ui->import_preview_name),"KEYBOARD PREVIEW.WAV");
    refresh_import_waveform_view(ui,c);instrument.selected_slot=2;
    ts_ui_render(&before,ui,&instrument);after=before;
    ts_overlay_tile_states(&after,ui,&instrument);assert(!memcmp(&before,&after,sizeof(before)));
    const char *shot=getenv("TS_TEST_IMPORT_SCREENSHOT");
    if(shot && sample->channels==2)assert(ts_ui_write_ppm(&after,shot));
    ui->import_preview_open=0;ui->browser.mode=TS_BROWSER_LOAD_WAV;
    ts_ui_render(&before,ui,&instrument);after=before;
    ts_overlay_tile_states(&after,ui,&instrument);assert(!memcmp(&before,&after,sizeof(before)));
    event.type=SDL_KEYDOWN;event.key.keysym.sym=SDLK_z;
    assert(!import_preview_event(&event,device,audio,ui,&instrument,&pending,c,44100));
    assert(ts_note_bank_count(&audio->notes)==0);
    ui->browser.mode=TS_BROWSER_CLOSED;ui->import_preview_open=1;
    /* Main tiles still get their late state borders. */
    memset(&before,0,sizeof(before));after=before;ui->import_preview_open=0;
    ts_overlay_tile_states(&after,ui,&instrument);assert(memcmp(&before,&after,sizeof(before)));
    ui->import_preview_open=1;
    IMPORT_KEY(SDLK_z,SDL_KEYDOWN,KMOD_NONE);
    stop_import_preview(device,audio,ui,c);
    for(int i=0;i<TS_NOTE_BANK_VOICE_CAPACITY;++i)assert(audio->notes.voices[i].sample!=sample);
    /* Shift+Enter imports the selected range; bare S above only played a note. */
    ui->import_preview_has_selection=1;ui->import_preview_selection_first=1000;
    ui->import_preview_selection_last=2000;c->destination_slot=0;
    IMPORT_KEY(SDLK_z,SDL_KEYDOWN,KMOD_NONE);
    IMPORT_KEY(SDLK_RETURN,SDL_KEYDOWN,KMOD_SHIFT);
    assert(!ui->import_preview_open && instrument.current.frames==1000);
    assert(ts_note_bank_count(&audio->notes)==0);
    for(int i=0;i<TS_NOTE_BANK_VOICE_CAPACITY;++i)assert(audio->notes.voices[i].sample!=sample);
#undef IMPORT_KEY
    ts_sample_free(&pending);ts_instrument_free(&instrument);
}

static void test_import(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui,
                         const TsSample *source, int channels)
{
    ImportController c;import_controller_init(&c);
    c.decoded.kind=TS_AUDIO_IMPORT_WAV;
    TsSample *sample=&c.decoded.sample;
    sample->frames=source->frames;sample->channels=(uint8_t)channels;
    sample->sample_rate=source->sample_rate;
    sample->data=malloc(sample->frames*(size_t)channels*sizeof(float));assert(sample->data);
    for(size_t i=0;i<sample->frames;++i)
        for(int ch=0;ch<channels;++ch)sample->data[i*(size_t)channels+ch]=source->data[i]*(ch?0.5f:1.0f);
    uint64_t hash=ts_sample_hash(sample);
    ui->import_preview_open=ui->import_preview_available=1;
    ui->import_preview_loop=ui->import_preview_has_selection=1;
    ui->import_preview_selection_first=1000;ui->import_preview_selection_last=10000;
    ui->import_preview_playhead=4000;ui->config.rotate_wheel_coarse=2;
    ts_ui_reset_import_view(ui,sample->frames);
    audition_import_preview(device,audio,ui,&c,44100);
    assert(ui->import_preview_active && audio->sample==sample);
    audio->attack_frame=123;
    /* Same endpoint choice and zero-crossing steps as the canvas. */
    static TsInstrument canvas;
    memset(&canvas,0,sizeof(canvas));canvas.current=*sample;
    canvas.has_selection=1;canvas.selection_first=1000;canvas.selection_last=10000;
    assert(ts_instrument_resize_selection(&canvas,1,1,2));
    resize_import_selection(device,audio,ui,&c,36+25,1);
    assert(ui->import_preview_selection_first==canvas.selection_first);
    assert(audio->range_start==canvas.selection_first && audio->position==4000);
    assert(audio->attack_frame==123 && audio->playing);
    assert(ts_instrument_resize_selection(&canvas,2,0,2));
    resize_import_selection(device,audio,ui,&c,36+110,-1);
    assert(ui->import_preview_selection_last==canvas.selection_last);
    assert(audio->range_end==canvas.selection_last && audio->position==4000);
    size_t first=audio->range_start,last=audio->range_end;
    resize_import_selection(device,audio,ui,&c,36+500,1);
    assert(audio->range_start==first && audio->range_end==last);
    /* Dragging can replace the range without stopping, including reversed drags. */
    begin_import_selection(device,audio,ui,&c,36+200);
    assert(audio->playing && ui->import_preview_active && audio->position==4000);
    update_import_selection(device,audio,ui,&c,36+350);
    assert(audio->range_start==ui->import_preview_selection_first);
    assert(audio->range_end==ui->import_preview_selection_last);
    assert(audio->position==audio->range_start && audio->attack_frame==123);
    render_loop(audio);
    update_import_selection(device,audio,ui,&c,36+100);
    assert(audio->range_start<audio->range_end && audio->playing);
    clear_import_selection(device,audio,ui,&c);
    assert(audio->range_start==0 && audio->range_end==sample->frames && audio->playing);
    render_loop(audio);
    /* Tiny loops stay valid, and loop OFF/stopped previews never auto-start. */
    ui->import_preview_has_selection=1;ui->import_preview_selection_first=200;
    ui->import_preview_selection_last=201;
    assert(sync_import_preview_loop(device,audio,ui,&c,0));
    assert(audio->crossfade_frames==0 && audio->position==200);render_loop(audio);
    audition_import_preview(device,audio,ui,&c,44100);assert(!audio->playing);
    ui->import_preview_selection_first=1000;ui->import_preview_selection_last=10000;
    resize_import_selection(device,audio,ui,&c,61,1);
    assert(ui->import_preview_selection_first<1000 && !audio->playing);
    ui->import_preview_loop=0;
    audition_import_preview(device,audio,ui,&c,44100);assert(audio->playing);
    begin_import_selection(device,audio,ui,&c,100);assert(!audio->playing);
    assert(ts_sample_hash(sample)==hash);
    test_import_keyboard(device,audio,ui,&c);
    close_import_preview(device,audio,ui,&c);
    memset(&canvas.current,0,sizeof(canvas.current));
}

static void test_portal_keyboard(SDL_Window *window,SDL_AudioDeviceID device,AudioState *audio,
    TsUiState *ui,TsInstrument *instrument,PortalController *c,SisterWindow *sister,TransformController *transform)
{
    TsPortalUi *p=&ui->portal;SDL_Event event={0};
#define SEND() assert(portal_event(&event,window,device,audio,ui,instrument,c,sister,44100,transform))
#define KEY(K,T) do {memset(&event,0,sizeof(event));event.type=(T);event.key.windowID=SDL_GetWindowID(window);event.key.keysym.sym=(K);SEND();} while(0)
    p->open=p->loop=1;p->listen_result=0;p->dragging_wave=-1;
    ts_ui_keyboard_set_octave(ui,4);
    for(int i=0;i<2;++i) {
        TsPortalWave *w=&p->waves[i];ts_portal_wave_reset(w,i?p->result:p->source);
        w->has_selection=1;w->selection_first=1000;w->selection_last=10000;
    }
    p->playing=0;p->waves[0].playhead=6000;
    portal_poll(device,audio,ui,instrument,c);assert(p->waves[0].playhead==6000);
    portal_play(device,audio,ui,c,44100,0);assert(audio->playing);
    KEY(SDLK_z,SDL_KEYDOWN);assert(!audio->playing && ts_note_bank_count(&audio->notes)==1);
    KEY(SDLK_q,SDL_KEYDOWN);assert(ts_note_bank_count(&audio->notes)==2);
    TsNoteVoice *low=&audio->notes.voices[0],*high=&audio->notes.voices[1];
    assert(low->preview && !low->synth && low->sample==p->source && low->position==1000);
    assert(low->midi_note==60 && fabs(low->step-1)<1e-9);
    assert(high->midi_note==72 && fabs(high->step-2)<1e-9);
    uint64_t serial=high->serial;event.key.repeat=1;SEND();assert(high->serial==serial);
    /* A normal Current sync must never redirect Portal-owned voices. */
    ts_note_bank_sync(&audio->notes,instrument,44100);
    assert(low->active && low->sample==p->source && low->range_first==1000);
    double energy=0;float block[512];
    for(int n=0;n<4;++n) {
        audio_callback(audio,(Uint8*)block,sizeof(block));
        for(int i=0;i<512;++i) {assert(isfinite(block[i]));energy+=fabsf(block[i]);}
    }
    assert(energy>0);
    TsStereoFrame sample,fm,capture;ts_note_bank_read_buses(&audio->notes,&sample,&fm,&capture);
    assert(fm.l==0 && fm.r==0 && capture.l==0 && capture.r==0);
    KEY(SDLK_F6,SDL_KEYDOWN);assert(ts_ui_keyboard_base_note(ui)==72 && low->midi_note==60);
    p->name_focus=1;KEY(SDLK_q,SDL_KEYUP);assert(!high->active && low->active);
    KEY(SDLK_c,SDL_KEYDOWN);assert(ts_note_bank_count(&audio->notes)==1);
    p->name_focus=0;p->search_focus=1;KEY(SDLK_d,SDL_KEYDOWN);assert(ts_note_bank_count(&audio->notes)==1);
    p->search_focus=0;p->number_focus=0;KEY(SDLK_2,SDL_KEYDOWN);assert(ts_note_bank_count(&audio->notes)==1);p->number_focus=-1;
    KEY(SDLK_q,SDL_KEYDOWN);assert(high->midi_note==84 && fabs(high->step-4)<1e-9);
    KEY(SDLK_q,SDL_KEYUP);
    KEY(SDLK_TAB,SDL_KEYDOWN);assert(p->listen_result==1 && low->active && low->sample==p->result && low->pitch==1);
    size_t attack=low->attack_frame;low->position=4000;
    portal_resize_selection(device,audio,ui,1,169,1);
    assert(low->range_first==p->waves[1].selection_first && low->range_first<1000);
    assert(low->position==4000 && low->attack_frame==attack && low->pitch==1);
    p->waves[1].selection_first=5000;p->waves[1].selection_last=6000;
    assert(portal_sync_loop(device,audio,p,1,0));assert(low->position==5000 && low->range_last==6000);
    p->waves[1].has_selection=0;assert(portal_sync_loop(device,audio,p,1,0));
    assert(low->range_last==p->result->frames && low->active);
    p->manage_open=1;KEY(SDLK_z,SDL_KEYUP);assert(!low->active);p->manage_open=0;
    /* Modifier shortcuts and text input cannot leak notes or main-page actions. */
    KEY(SDLK_z,SDL_KEYDOWN);KEY(SDLK_SPACE,SDL_KEYDOWN);assert(ts_note_bank_count(&audio->notes)==0 && !audio->playing);
    event.key.keysym.sym=SDLK_c;event.key.keysym.mod=KMOD_CTRL;SEND();assert(ts_note_bank_count(&audio->notes)==0);
    KEY(SDLK_z,SDL_KEYDOWN);
    memset(&event,0,sizeof(event));event.type=SDL_MOUSEBUTTONDOWN;
    event.button.windowID=SDL_GetWindowID(window);event.button.button=SDL_BUTTON_LEFT;
    event.button.x=350;event.button.y=230;SEND();
    assert(!p->loop && audio->notes.voices[0].active && !audio->notes.voices[0].looping && !audio->playing);
    KEY(SDLK_z,SDL_KEYUP);assert(ts_note_bank_count(&audio->notes)==0);
    /* Toggle after release, before polling: never resurrect unity playback. */
    memset(&event,0,sizeof(event));event.type=SDL_MOUSEBUTTONDOWN;
    event.button.windowID=SDL_GetWindowID(window);event.button.button=SDL_BUTTON_LEFT;
    event.button.x=350;event.button.y=230;SEND();assert(p->loop && !audio->playing && ts_note_bank_count(&audio->notes)==0);
    p->loop=1;ts_ui_keyboard_set_octave(ui,4);
    const SDL_Keycode chord[]={SDLK_z,SDLK_x,SDLK_c,SDLK_v,SDLK_b,SDLK_n};
    for(int i=0;i<6;++i)KEY(chord[i],SDL_KEYDOWN);
    assert(ts_note_bank_count(&audio->notes)==5 && strstr(p->message,"LIMIT"));
    portal_poll(device,audio,ui,instrument,c);assert(p->note_count==5 && p->playing);
    memset(&event,0,sizeof(event));event.type=SDL_WINDOWEVENT;event.window.windowID=SDL_GetWindowID(window);
    event.window.event=SDL_WINDOWEVENT_FOCUS_LOST;
    (void)portal_event(&event,window,device,audio,ui,instrument,c,sister,44100,transform);
    assert(ts_note_bank_count(&audio->notes)==0);
    KEY(SDLK_z,SDL_KEYDOWN);portal_invalidate(device,audio,p,c);
    assert(ts_note_bank_count(&audio->notes)==0 && !p->result);
    p->listen_result=0;KEY(SDLK_z,SDL_KEYDOWN);assert(ts_note_bank_count(&audio->notes)==1);
    portal_close(device,audio,ui,c);assert(ts_note_bank_count(&audio->notes)==0);
    for(int i=0;i<TS_NOTE_BANK_VOICE_CAPACITY;++i)assert(!audio->notes.voices[i].preview);
#undef KEY
#undef SEND
}

int main(void)
{
    static AudioState audio;static TsUiState ui;static TsInstrument instrument;
    PortalController c;static SisterWindow sister;TransformController transform;
    SDL_SetMainReady();SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    SDL_setenv("SDL_VIDEODRIVER","dummy",1);
    assert(SDL_Init(SDL_INIT_AUDIO|SDL_INIT_VIDEO|SDL_INIT_TIMER)==0);
    SDL_AudioSpec desired={0};desired.freq=44100;desired.channels=2;
    desired.format=AUDIO_F32SYS;desired.samples=256;
    SDL_AudioDeviceID device=SDL_OpenAudioDevice(NULL,0,&desired,NULL,0);assert(device);
    ts_real_output=device;
    SDL_Window *window=SDL_CreateWindow("Preview loop test",0,0,640,400,0);assert(window);
    ts_ui_init(&ui);ts_instrument_init(&instrument);transform_controller_init(&transform);
    ts_sister_runtime_init(&audio.sister);ts_note_bank_init(&audio.notes);
    ts_performance_init(&audio.performance);ts_performance_init(&audio.tile_launchers);
    ts_capture_init(&audio.capture);ts_audio_mixer_init(&audio.mixer);
    audio.bank_slot=-1;audio.output_rate=44100;
    memset(&c,0,sizeof(c));c.source.frames=44100;c.source.sample_rate=44100;c.source.channels=1;
    c.source.data=malloc(c.source.frames*sizeof(float));assert(c.source.data);
    for(size_t i=0;i<c.source.frames;++i)c.source.data[i]=0.4f*sinf((float)(i%100)*2.0f*(float)M_PI/100.0f);
    uint64_t hash=ts_sample_hash(&c.source);char error[160];
    assert(ts_sample_clone(&c.history[0].output,&c.source,error,sizeof(error)));
    ui.portal.open=ui.portal.loop=1;ui.portal.source=&c.source;
    ui.portal.result=&c.history[0].output;
    ui.config.rotate_wheel_coarse=2;
    for(int which=0;which<2;++which) {
        TsPortalWave *w=&ui.portal.waves[which];
        ts_portal_wave_reset(w,which?ui.portal.result:ui.portal.source);
        w->has_selection=1;w->selection_first=1000;w->selection_last=10000;w->playhead=4000;
        ui.portal.listen_result=which;portal_play(device,&audio,&ui,&c,44100,0);
        audio.attack_frame=123;
        /* Exercise the actual Alt+wheel route, including flipped wheel devices. */
        SDL_WarpMouseInWindow(window,154+15,which?180:80);SDL_SetModState(KMOD_ALT);
        SDL_Event event={0};event.type=SDL_MOUSEWHEEL;event.wheel.windowID=SDL_GetWindowID(window);
        event.wheel.y=1;
        assert(portal_event(&event,window,device,&audio,&ui,&instrument,&c,&sister,44100,&transform));
        assert(w->selection_first<1000 && w->first==0 && w->last==44100);
        assert(audio.range_start==w->selection_first && audio.position==4000 && audio.attack_frame==123);
        size_t expanded=w->selection_first;event.wheel.direction=SDL_MOUSEWHEEL_FLIPPED;
        portal_event(&event,window,device,&audio,&ui,&instrument,&c,&sister,44100,&transform);
        assert(w->selection_first>expanded && audio.playing);
        SDL_SetModState(KMOD_NONE);
        memset(&event,0,sizeof(event));event.type=SDL_MOUSEBUTTONDOWN;
        event.button.windowID=SDL_GetWindowID(window);event.button.button=SDL_BUTTON_LEFT;
        event.button.x=230;event.button.y=which?180:80;
        portal_event(&event,window,device,&audio,&ui,&instrument,&c,&sister,44100,&transform);
        assert(audio.playing && audio.position==4000);
        event.type=SDL_MOUSEMOTION;event.motion.windowID=SDL_GetWindowID(window);
        event.motion.x=330;event.motion.y=which?180:80;
        portal_event(&event,window,device,&audio,&ui,&instrument,&c,&sister,44100,&transform);
        assert(audio.range_start==w->selection_first && audio.range_end==w->selection_last);
        assert(audio.position==audio.range_start && audio.attack_frame==123);
        render_loop(&audio);
        memset(&event,0,sizeof(event));event.type=SDL_MOUSEBUTTONUP;
        event.button.windowID=SDL_GetWindowID(window);event.button.button=SDL_BUTTON_LEFT;
        event.button.x=330;event.button.y=which?180:80;
        portal_event(&event,window,device,&audio,&ui,&instrument,&c,&sister,44100,&transform);
        assert(audio.playing && ui.portal.dragging_wave==-1);
        event.type=SDL_MOUSEBUTTONDOWN;event.button.button=SDL_BUTTON_RIGHT;
        portal_event(&event,window,device,&audio,&ui,&instrument,&c,&sister,44100,&transform);
        assert(!w->has_selection && audio.range_start==0 && audio.range_end==44100 && audio.playing);
        portal_stop(device,&audio,&ui.portal,&c);
        w->has_selection=1;w->selection_first=1000;w->selection_last=10000;
        portal_resize_selection(device,&audio,&ui,which,169,1);
        assert(w->selection_first<1000 && !audio.playing);
        ui.portal.loop=0;portal_play(device,&audio,&ui,&c,44100,0);assert(audio.playing);
        event.button.button=SDL_BUTTON_LEFT;
        portal_event(&event,window,device,&audio,&ui,&instrument,&c,&sister,44100,&transform);
        assert(!audio.playing);ui.portal.loop=1;
    }
    assert(ts_sample_hash(&c.source)==hash && ts_sample_hash(&c.history[0].output)==hash);
    test_portal_keyboard(window,device,&audio,&ui,&instrument,&c,&sister,&transform);
    assert(ts_sample_hash(&c.source)==hash && ts_sample_hash(&c.history[0].output)==hash);
    test_import(device,&audio,&ui,&c.source,1);
    test_import(device,&audio,&ui,&c.source,2);
    portal_free(&c);ts_instrument_free(&instrument);
    ts_performance_free(&audio.performance);ts_performance_free(&audio.tile_launchers);
    ts_sister_runtime_free(&audio.sister);ts_capture_free(&audio.capture);
    SDL_DestroyWindow(window);SDL_CloseAudioDevice(device);ts_real_output=0;SDL_Quit();
    puts("Live preview loop selection tests passed");return 0;
}
