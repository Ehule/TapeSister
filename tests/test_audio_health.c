#ifdef NDEBUG
#undef NDEBUG
#endif
#define SDL_MAIN_HANDLED
#define main tapesister_application_main
#include "../src/main_sdl.c"
#undef main
#include <assert.h>

static void silence(void *unused, Uint8 *data, int bytes)
{ (void)unused; memset(data, 0, bytes); }

static void named_edit_for_health(AudioState *audio)
{
    lock_edit(TS_AUDIO_LOGICAL_OUTPUT, audio);
    SDL_Delay(1);
    ts_audio_unlock_device(TS_AUDIO_LOGICAL_OUTPUT);
}

static int publish(void *data)
{
    TsRealtimeDiagnostics *d = data;
    for (unsigned i = 0; i < 100000; ++i) {
        unsigned rate = (i / 25) % 2 ? 48000 : 96000;
        ts_realtime_diagnostics_record_timed(d, (uint64_t)i * 10000,
            2500, 1000000, rate, rate / 100, 0);
    }
    return 0;
}

int main(int argc, char **argv)
{
    SDL_setenv("SDL_VIDEODRIVER", "dummy", 1);
    SDL_setenv("SDL_AUDIODRIVER", "dummy", 1);
    /* Dummy windows have no native GPU surface. Keep both the renderer and its
       backing framebuffer in software, independent of the CI host's drivers. */
    assert(SDL_SetHintWithPriority(SDL_HINT_RENDER_DRIVER, "software", SDL_HINT_OVERRIDE));
    assert(SDL_SetHintWithPriority(SDL_HINT_FRAMEBUFFER_ACCELERATION, "0", SDL_HINT_OVERRIDE));
    assert(!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER));
    AudioState *a = calloc(1, sizeof(*a));
    TsUiState *ui = calloc(1, sizeof(*ui)); assert(a && ui);
    ts_ui_init(ui);
    ts_realtime_diagnostics_init(&a->realtime_diagnostics);
    a->realtime_diagnostics_enabled = 1;
    a->realtime_counter_frequency = SDL_GetPerformanceFrequency();
    a->audio_health_started = SDL_GetTicks();
    SDL_Window *main_window = SDL_CreateWindow("main", 0, 0, 640, 400, 0);
    assert(main_window);
    assert(window_drawable(main_window));SDL_HideWindow(main_window);
    assert(!window_drawable(main_window));SDL_ShowWindow(main_window);
    assert(window_drawable(main_window));
    SDL_Renderer *damage_renderer=SDL_CreateRenderer(main_window,-1,SDL_RENDERER_SOFTWARE);assert(damage_renderer);
    SDL_Texture *damage_texture=SDL_CreateTexture(damage_renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,640,400);assert(damage_texture);
    TsFramebuffer *damage_frame=calloc(1,sizeof(*damage_frame));uint32_t *damage_previous=calloc(640*400,sizeof(uint32_t));assert(damage_frame && damage_previous);
    int damage_valid=0;
    assert(update_texture_damage(damage_texture,damage_frame,damage_previous,&damage_valid)==1);
    assert(update_texture_damage(damage_texture,damage_frame,damage_previous,&damage_valid)==2);
    damage_frame->pixels[20]=0xffffffffu;
    assert(update_texture_damage(damage_texture,damage_frame,damage_previous,&damage_valid)==1);
    damage_valid=0;assert(update_texture_damage(damage_texture,damage_frame,damage_previous,&damage_valid)==1);
    SDL_DestroyTexture(damage_texture);SDL_DestroyRenderer(damage_renderer);free(damage_frame);free(damage_previous);
    ts_profile_init(SDL_GetPerformanceCounter,SDL_GetPerformanceFrequency());
    SDL_AudioSpec want = {0}, got = {0};
    want.freq = 48000; want.format = AUDIO_F32SYS; want.channels = 2;
    want.samples = 256; want.callback = silence;
    ts_real_output = ts_native_open(NULL, 0, &want, &got, 0); assert(ts_real_output);
    ts_output_userdata = a;
    ts_audio_endpoints_init();
    ts_audio_endpoint_configure(&ts_output_endpoint, "");
    ts_audio_endpoint_begin_open(&ts_output_endpoint, 0);
    ts_audio_endpoint_opened(&ts_output_endpoint, ts_real_output, "Test output", 0,
        got.freq, got.channels, got.samples, "F32");
    ui->config.audio_buffer_frames = 2048; /* Display must use the obtained size. */
    ts_audio_lock_device_at(TS_AUDIO_LOGICAL_OUTPUT, "outer edit");
    ts_audio_lock_device_at(TS_AUDIO_LOGICAL_OUTPUT, "nested edit");
    ts_audio_unlock_device(TS_AUDIO_LOGICAL_OUTPUT);
    assert(atomic_load(&a->realtime_diagnostics.control_count) == 0);
    SDL_Delay(1); /* Ensure a nonzero hold on low-resolution platform clocks. */
    ts_audio_unlock_device(TS_AUDIO_LOGICAL_OUTPUT);
    TsRealtimeDiagnosticsSnapshot s;
    ts_realtime_diagnostics_get(&a->realtime_diagnostics, &s);
    assert(s.control_count == 1 && !strcmp(s.control_worst_site, "outer edit"));
    ts_realtime_diagnostics_reset(&a->realtime_diagnostics);
    named_edit_for_health(a);
    ts_realtime_diagnostics_get(&a->realtime_diagnostics, &s);
    assert(s.control_count == 1 && !strcmp(s.control_worst_site, "named_edit_for_health"));

    SDL_Event e = {0}; e.type = SDL_KEYDOWN;
    e.key.windowID = SDL_GetWindowID(main_window); e.key.keysym.sym = SDLK_F12;
    int handled = ts_audio_health_event(&e, main_window, a, ui);
    if (!ts_health.visible)
        fprintf(stderr, "Audio Health open failed: %s; SDL: %s\n", ui->status, SDL_GetError());
    assert(handled && ts_health.visible);
    SDL_RendererInfo renderer_info;
    assert(!SDL_GetRendererInfo(ts_health.renderer, &renderer_info));
    assert(renderer_info.flags & SDL_RENDERER_SOFTWARE);
    SDL_Event profile_event={0};profile_event.type=SDL_MOUSEBUTTONDOWN;
    profile_event.button.windowID=ts_health.window_id;profile_event.button.button=SDL_BUTTON_LEFT;
    profile_event.button.x=500;profile_event.button.y=470;
    assert(ts_audio_health_event(&profile_event,main_window,a,ui));assert(ts_profile_enabled());
    ts_audio_health_update(a,ui);
    if (SDL_getenv("TAPESISTER_PROFILE_SCREENSHOT")) {
        SDL_Surface *surface=SDL_CreateRGBSurfaceFrom(ts_health.framebuffer->pixels,640,400,32,640*4,0x00ff0000u,0x0000ff00u,0x000000ffu,0xff000000u);
        assert(surface);assert(!SDL_SaveBMP(surface,SDL_getenv("TAPESISTER_PROFILE_SCREENSHOT")));SDL_FreeSurface(surface);
    }
    assert(ts_audio_health_event(&profile_event,main_window,a,ui));assert(!ts_profile_enabled());
    for (unsigned i = 0; i < 25; ++i)
        ts_realtime_diagnostics_record_timed(&a->realtime_diagnostics, i * 10000u,
            2500, 1000000, 48000, 480, 0);
    ts_audio_health_update(a, ui);
    char report[4096]; ts_audio_health_report(a, report, sizeof(report));
    assert(strstr(report, "48000 Hz, 256 frames, 2 channels"));
    assert(strstr(report, "Build: " TAPESISTER_BUILD_MARKER));
    assert(strstr(report, "Longest hold call site: named_edit_for_health"));
    assert(strstr(report, "not exposed by this backend"));
    assert(strstr(report, "Recent processing: 25.00%"));
    assert(strstr(report, "not round-trip latency"));
    assert(strstr(report, "Active setup: awaiting UI snapshot"));
    /* Copy uses the same scaled coordinate mapping as the resizable window. */
    e.type = SDL_MOUSEBUTTONDOWN; e.button.windowID = ts_health.window_id;
    e.button.button = SDL_BUTTON_LEFT; e.button.x = 250; e.button.y = 470;
    assert(ts_audio_health_event(&e, main_window, a, ui));
    char *copied = SDL_GetClipboardText(); assert(strstr(copied, "TapeSister Audio Health")); SDL_free(copied);
    e.type = SDL_MOUSEWHEEL; e.wheel.windowID = ts_health.window_id; e.wheel.y = 1;
    assert(ts_audio_health_event(&e, main_window, a, ui));
    e.type = SDL_KEYDOWN; e.key.windowID = ts_health.window_id; e.key.keysym.sym = SDLK_z;
    assert(ts_audio_health_event(&e, main_window, a, ui));
    ts_note_bank_init(&a->notes);
    TsSample sample; ts_sample_init(&sample);
    sample.frames = 128; sample.channels = 1; sample.sample_rate = 48000;
    sample.data = calloc(sample.frames, sizeof(float)); assert(sample.data);
    TsTuning tuning = {60, 0}; TsNoteEvent note;
    assert(ts_note_event_qwerty(&note, note_for_key(SDLK_z), 60));
    assert(ts_note_bank_start_sample_event(&a->notes, &sample, &tuning, &note, 0, 48000) == TS_NOTE_STARTED);
    e.type = SDL_KEYUP;
    assert(ts_audio_health_event(&e, main_window, a, ui));
    assert(ts_note_bank_count(&a->notes) == 0); /* No stuck key across focus. */
    ts_note_bank_set_sustain(&a->notes, 1);
    assert(ts_note_bank_start_sample_event(&a->notes, &sample, &tuning, &note, 0, 48000) == TS_NOTE_STARTED);
    assert(ts_audio_health_event(&e, main_window, a, ui));
    assert(ts_note_bank_count(&a->notes) == 1); /* HOLD remains intentional. */
    /* Reports describe simultaneous workload and effective Router bypass,
       including processors missing from the old callback bitfield. */
    ui->keyboard_hold = 1;
    a->keyboard_sequence.running = a->keyboard_sequence.slot_running = 1;
    a->keyboard_sequence.active_slot = 4;
    a->sister.enabled = a->sister.rolling = a->sister.monitor_enabled = 1;
    a->sister.source_switches = TS_SISTER_SOURCE_TILES | TS_SISTER_SOURCE_FM;
    ts_router_init(&a->sister.router);
    for (int i = 0; i < TS_ROUTER_COUNT; ++i) a->sister.router.wet[i] = 1;
    ts_sister_fx_controls_default(&a->sister.parameters.fx);
    a->sister.parameters.fx.enabled = a->sister.parameters.fx.fallout.enabled = 1;
    a->sister.parameters.fx.slot[0] = (TsSisterFxSlotControls){
        .type = TS_SISTER_FX_DELAY, .enabled = 1,
        .placement = TS_SISTER_FX_PLACE_H1 | TS_SISTER_FX_PLACE_POST, .mix = .4f};
    a->sister.parameters.fx.slot[1] = (TsSisterFxSlotControls){
        .type = TS_SISTER_FX_REVERB, .enabled = 0, .placement = TS_SISTER_FX_PLACE_POST};
    a->sister.parameters.prism.enabled = 1;
    a->sister.parameters.prism.lenses = 12;
    a->sister.prism.matrix.running = 1;
    a->tracker.lanes[0].voice.active=1;
    ts_audio_health_capture_setup(a, ui, 1000);
    assert(ts_health.setup.tracker_voices==1);
    char setup[3072]; ts_audio_health_setup_report(setup, sizeof(setup), 1020);
    assert(strstr(setup, "Keyboard HOLD: ON; keyboard voice instances: 1; latched/sustained: 1"));
    assert(strstr(setup, "ARP: RUNNING; outer ARP: RUNNING; active slot: 5"));
    assert(strstr(setup, "Sister Machine: ON, routed"));
    assert(strstr(setup, "Sister source switches: Tiles, FM"));
    assert(strstr(setup, "Fallout: ON, routed; FX master: ON"));
    assert(strstr(setup, "Prism: ON, routed") && strstr(setup, "base lenses: 12"));
    assert(strstr(setup, "Matrix: RUNNING"));
    assert(strstr(setup, "mix 40.0%; placement H1+POST"));
    assert(strstr(setup, "Slot 2: REVERB; OFF"));
    assert(strstr(setup, "send assigned: OFF; send available: OFF"));
    a->sister.router.controls.bypass_mask = 1u << TS_ROUTER_PRISM;
    a->sister.router.wet[TS_ROUTER_PRISM] = 0;
    a->sister.parameters.fx.enabled = 0;
    ts_audio_health_capture_setup(a, ui, 1100); /* Bounded refresh; no live-state reads in report. */
    ts_audio_health_setup_report(setup, sizeof(setup), 1100);
    assert(strstr(setup, "Prism: ON, routed"));
    ts_audio_health_capture_setup(a, ui, 1250);
    assert(ts_health.setup.tracker_voices==1);
    ts_audio_health_setup_report(setup, sizeof(setup), 1250);
    assert(strstr(setup, "Prism: ON, bypassed"));
    assert(strstr(setup, "Fallout: ON, routed; FX master: OFF"));
    assert(strstr(setup, "Pedalboard master: OFF"));
    ts_audio_health_report(a, report, sizeof(report));
    assert(strstr(report, "Slot 4:") && strstr(report, "Native counters cover"));
    a->keyboard_sequence.running = a->keyboard_sequence.slot_running = 0;
    a->sister.enabled = a->sister.parameters.prism.enabled = 0;
    a->sister.prism.matrix.running = 0;
    ts_audio_health_capture_setup(a, ui, 1500);
    ts_audio_health_setup_report(setup, sizeof(setup), 1500);
    assert(strstr(setup, "ARP: STOPPED; outer ARP: STOPPED; active slot: 0"));
    assert(strstr(setup, "Sister Machine: OFF") && strstr(setup, "Prism: OFF"));
    assert(strstr(setup, "Matrix: STOPPED"));
    char short_report[17]; memset(short_report, 'x', sizeof(short_report));
    ts_audio_health_setup_report(short_report, sizeof(short_report) - 1, 1250);
    assert(short_report[15] == '\0' && short_report[16] == 'x');
    ts_note_bank_clear(&a->notes); ts_sample_free(&sample);
    e.type = SDL_DROPFILE; e.drop.windowID = ts_health.window_id; e.drop.file = SDL_strdup("test.wav");
    assert(ts_audio_health_event(&e, main_window, a, ui));
    if (argc > 1) {
        SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormatFrom(ts_health.framebuffer->pixels,
            640, 400, 32, 640 * 4, SDL_PIXELFORMAT_ARGB8888); assert(surface);
        assert(!SDL_SaveBMP(surface, argv[1])); SDL_FreeSurface(surface);
    }
    ts_audio_health_reset(a);
    ts_realtime_diagnostics_get(&a->realtime_diagnostics, &s);
    assert(!s.callback_count && !s.control_count && !s.recent_callback_count);
    e.type = SDL_WINDOWEVENT; e.window.windowID = ts_health.window_id; e.window.event = SDL_WINDOWEVENT_CLOSE;
    assert(ts_audio_health_event(&e, main_window, a, ui) && !ts_health.visible);
    ui->config_open = 1;
    e.type = SDL_MOUSEBUTTONDOWN; e.button.windowID = SDL_GetWindowID(main_window);
    e.button.button = SDL_BUTTON_LEFT; e.button.x = 200; e.button.y = 48;
    assert(ts_audio_health_event(&e, main_window, a, ui) && ts_health.visible);
    e.type = SDL_KEYDOWN; e.key.windowID = SDL_GetWindowID(main_window);
    e.key.keysym.sym = SDLK_F12; e.key.keysym.mod = 0; e.key.repeat = 0;
    assert(ts_audio_health_event(&e, main_window, a, ui) && !ts_health.visible);

    /* Concurrent rate changes must never mix the recent numerator/denominator. */
    SDL_Thread *thread = SDL_CreateThread(publish, "health publisher", &a->realtime_diagnostics);
    assert(thread);
    for (unsigned i = 0; i < 100000; ++i) {
        ts_realtime_diagnostics_get(&a->realtime_diagnostics, &s);
        assert(s.recent_load_percent == 0 || fabs(s.recent_load_percent - 25) < .00001);
    }
    SDL_WaitThread(thread, NULL);
    assert(spatial_show(0,a,ui));
    SDL_HideWindow(spatial_window.window);spatial_window.presented=0;
    spatial_window.framebuffer->pixels[0]=0x12345678u;
    spatial_window_update(ui);assert(!spatial_window.presented);
    assert(spatial_window.framebuffer->pixels[0]==0x12345678u);
    spatial_window_close();
    ts_audio_health_close(); ts_native_close(ts_real_output);
    ts_real_output = 0; ts_output_userdata = NULL;
    SDL_DestroyWindow(main_window); free(a); free(ui); SDL_Quit();
    puts("Audio Health: counters, nested locks, concurrent snapshots, actual device, window isolation, reset and clipboard passed");
    return 0;
}
