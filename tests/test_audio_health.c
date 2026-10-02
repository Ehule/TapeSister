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

    SDL_Event e = {0}; e.type = SDL_KEYDOWN;
    e.key.windowID = SDL_GetWindowID(main_window); e.key.keysym.sym = SDLK_F12;
    assert(ts_audio_health_event(&e, main_window, a, ui) && ts_health.visible);
    for (unsigned i = 0; i < 25; ++i)
        ts_realtime_diagnostics_record_timed(&a->realtime_diagnostics, i * 10000u,
            2500, 1000000, 48000, 480, 0);
    ts_audio_health_update(a, ui);
    char report[4096]; ts_audio_health_report(a, report, sizeof(report));
    assert(strstr(report, "48000 Hz, 256 frames, 2 channels"));
    assert(strstr(report, "not exposed by this backend"));
    assert(strstr(report, "Recent processing: 25.00%"));
    assert(strstr(report, "not round-trip latency"));
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
    ts_audio_health_close(); ts_native_close(ts_real_output);
    ts_real_output = 0; ts_output_userdata = NULL;
    SDL_DestroyWindow(main_window); free(a); free(ui); SDL_Quit();
    puts("Audio Health: counters, nested locks, concurrent snapshots, actual device, window isolation, reset and clipboard passed");
    return 0;
}
