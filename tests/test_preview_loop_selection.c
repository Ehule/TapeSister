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

static void test_import(SDL_AudioDeviceID device, AudioState *audio, TsUiState *ui,
                         const TsSample *source, int channels)
{
    ImportController c;import_controller_init(&c);
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
    close_import_preview(device,audio,ui,&c);
    memset(&canvas.current,0,sizeof(canvas.current));
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
    test_import(device,&audio,&ui,&c.source,1);
    test_import(device,&audio,&ui,&c.source,2);
    portal_free(&c);ts_instrument_free(&instrument);
    ts_performance_free(&audio.performance);ts_performance_free(&audio.tile_launchers);
    ts_sister_runtime_free(&audio.sister);ts_capture_free(&audio.capture);
    SDL_DestroyWindow(window);SDL_CloseAudioDevice(device);ts_real_output=0;SDL_Quit();
    puts("Live preview loop selection tests passed");return 0;
}
