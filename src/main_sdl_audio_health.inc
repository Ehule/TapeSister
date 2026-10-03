/* Modeless, low-refresh-rate audio health window. All formatting, drawing and
   clipboard operations stay on the UI thread; the reader never locks audio. */
typedef struct {
    int valid;
    Uint32 captured;
    int hold, keyboard_voices, held_voices, tile_voices;
    int arp, outer_arp, arp_slot, audition, mosaic;
    int tracker, tracker_voices, tracker_row;
    int sister, rolling, tape_hold, monitor;
    unsigned sources;
    int board, fallout;
    float fallout_mix;
    TsSisterFxSlotControls slot[TS_SISTER_FX_SLOT_COUNT];
    int prism, prism_mode, prism_lenses, prism_sequence, prism_matrix;
    int send_assigned, send_available, return_available, router_sequence;
    TsRouterControls router;
    float router_wet[TS_ROUTER_COUNT];
} TsAudioHealthSetup;

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    TsFramebuffer *framebuffer;
    Uint32 window_id, last_update, last_callback, message_until;
    uint64_t seen_callbacks;
    uint32_t device_generation;
    unsigned driver_base, control_base, worker_base;
    unsigned driver_reports, control_skips, worker_gaps;
    int visible, native_kind;
    TsRealtimeDiagnosticsSnapshot snapshot;
    TsAudioHealthSetup setup;
    char message[80];
} TsAudioHealthWindow;
static TsAudioHealthWindow ts_health;

/* Called on the UI thread while its existing voice-state lock is held. Copy
   small state only, at most 4 Hz while visible. No new audio-callback work, lock,
   allocation, formatting or source ownership is introduced. */
static void ts_audio_health_capture_setup(const AudioState *audio,
                                         const TsUiState *ui, Uint32 now)
{
    TsAudioHealthSetup *s = &ts_health.setup;
    if (!ts_health.visible || (s->valid && now - s->captured < 250u)) return;
    const TsSisterRuntime *r = &audio->sister;
    const TsSisterParameters *p = &r->parameters;
    s->hold = ui->keyboard_hold;
    s->keyboard_voices = s->held_voices = s->tile_voices = 0;
    for (int i = 0; i < audio->notes.render_limit; ++i) {
        const TsNoteVoice *v = &audio->notes.voices[i];
        if (!v->active) continue;
        ++s->keyboard_voices;
        if (v->latched || audio->notes.sustain) ++s->held_voices;
    }
    const TsPerformanceBank *banks[] = {&audio->performance, &r->performance, &audio->tile_launchers};
    for (unsigned b = 0; b < sizeof(banks) / sizeof(banks[0]); ++b) {
        const TsPerformanceBank *bank = banks[b];
        for (int i = 0; i < bank->render_limit; ++i) {
            const TsPerformanceVoice *v = &bank->voices[i];
            if (!v->active) continue;
            if (v->tile_launched) ++s->tile_voices;
            else {
                ++s->keyboard_voices;
                if (v->latched || bank->sustain) ++s->held_voices;
            }
        }
    }
    s->arp = audio->keyboard_sequence.running;
    s->outer_arp = audio->keyboard_sequence.slot_running;
    s->arp_slot = audio->keyboard_sequence.active_slot;
    s->audition = audio->playing;
    s->mosaic = audio->mosaic && audio->mosaic->playing;
    s->tracker=audio->tracker.running?(audio->tracker.paused?2:1):0;
    s->tracker_row=audio->tracker.row<0?0:audio->tracker.row;
    for(int i=0;i<TS_TRACKER_LANES;++i)s->tracker_voices+=audio->tracker.lanes[i].voice.active;
    s->sister = r->enabled; s->rolling = r->rolling; s->tape_hold = r->held;
    s->monitor = r->monitor_enabled; s->sources = r->source_switches;
    s->board = p->fx.enabled; s->fallout = p->fx.fallout.enabled;
    s->fallout_mix = p->fx.fallout.mix;
    memcpy(s->slot, p->fx.slot, sizeof(s->slot));
    s->prism = p->prism.enabled; s->prism_mode = p->prism.mode;
    s->prism_lenses = p->prism.lenses;
    s->prism_sequence = p->prism.seq_enabled;
    s->prism_matrix = r->prism.matrix.running;
    s->send_assigned = r->insert.controls.send_pair > 0;
    s->send_available = ts_insert_send_available(&r->insert);
    s->return_available = ts_insert_return_available(&r->insert);
    s->router = r->router.controls;
    memcpy(s->router.order, r->router.order, sizeof(s->router.order));
    memcpy(s->router_wet, r->router.wet, sizeof(s->router_wet));
    s->router_sequence = r->router.transport.running;
    s->captured = now; s->valid = 1;
}

static const char *health_on(int enabled) { return enabled ? "ON" : "OFF"; }
static const char *health_running(int running) { return running ? "RUNNING" : "STOPPED"; }
static const char *health_route(const TsAudioHealthSetup *s, int stage)
{
    int active = ts_router_active(&s->router, stage);
    float wet = s->router_wet[stage];
    if (active && wet < .99999f) return "routing in";
    if (!active && wet > .00001f) return "bypassing";
    return active ? "routed" : "bypassed";
}

static void ts_audio_health_setup_report(char *text, size_t size, Uint32 now)
{
    if (!size) return;
    const TsAudioHealthSetup *s = &ts_health.setup;
    if (!s->valid) {
        snprintf(text, size, "Active setup: awaiting UI snapshot\n");
        return;
    }
    char source[96] = "";
    const char *names[] = {"Tiles", "FM", "EXT", "Preview", "TapeHead", "Tracker"};
    for (int i = 0; i < TS_SISTER_SOURCE_COUNT; ++i) if (s->sources & (1u << i)) {
        size_t used = strlen(source);
        snprintf(source + used, sizeof(source) - used, "%s%s", used ? ", " : "", names[i]);
    }
    unsigned enabled_slots = 0;
    for (int i = 0; i < TS_SISTER_FX_SLOT_COUNT; ++i)
        if (s->slot[i].type != TS_SISTER_FX_EMPTY && s->slot[i].enabled) ++enabled_slots;
    snprintf(text, size,
        "Active setup (UI snapshot %.3f s old; not the setup at a retained maximum):\n"
        "  Keyboard HOLD: %s; keyboard voice instances: %d; latched/sustained: %d\n"
        "  ARP: %s; outer ARP: %s; active slot: %d\n"
        "  Audition: %s; tile voice instances: %d; Mosaic: %s\n"
        "  SisterTracker: %s; row: %d; voice instances: %d\n"
        "  Sister Machine: %s, %s; rolling: %s; tape HOLD: %s; monitor: %s\n"
        "  Sister source switches: %s\n"
        "  Fallout: %s, %s; FX master: %s; mix: %.1f%%\n"
        "  Prism: %s, %s; base mode: %s; base lenses: %d; lens sequence: %s; Matrix: %s\n"
        "  Insert: %s; send assigned: %s; send available: %s; return available: %s\n"
        "  Router order: %s > %s > %s > %s > %s; sequencer: %s\n"
        "  Pedalboard master: %s, %s; enabled slots: %u/4\n",
        (now - s->captured) / 1000.0, health_on(s->hold), s->keyboard_voices, s->held_voices,
        health_running(s->arp), health_running(s->outer_arp),
        s->arp || s->outer_arp ? s->arp_slot + 1 : 0,
        health_running(s->audition), s->tile_voices, health_running(s->mosaic),
        s->tracker==2?"paused":health_running(s->tracker),s->tracker_row,s->tracker_voices,
        health_on(s->sister), health_route(s, TS_ROUTER_SISTER), health_on(s->rolling),
        health_on(s->tape_hold), health_on(s->monitor), source[0] ? source : "none",
        health_on(s->fallout), health_route(s, TS_ROUTER_FALLOUT), health_on(s->board), s->fallout_mix * 100,
        health_on(s->prism), health_route(s, TS_ROUTER_PRISM), ts_prism_mode_name(s->prism_mode),
        s->prism_lenses, health_on(s->prism_sequence), health_running(s->prism_matrix),
        health_route(s, TS_ROUTER_INSERT), health_on(s->send_assigned),
        health_on(s->send_available), health_on(s->return_available),
        ts_router_name(s->router.order[0]), ts_router_name(s->router.order[1]),
        ts_router_name(s->router.order[2]), ts_router_name(s->router.order[3]),
        ts_router_name(s->router.order[4]), health_running(s->router_sequence),
        health_on(s->board), health_route(s, TS_ROUTER_PEDALBOARD), enabled_slots);
    for (int i = 0; i < TS_SISTER_FX_SLOT_COUNT; ++i) {
        const TsSisterFxSlotControls *slot = &s->slot[i];
        char places[48] = "";
        const char *labels[] = {"PRE", "H1", "H2", "H3", "POST"};
        for (int b = 0; b < TS_SISTER_FX_LOCATION_COUNT; ++b) if (slot->placement & (1u << b)) {
            size_t used = strlen(places);
            snprintf(places + used, sizeof(places) - used, "%s%s", used ? "+" : "", labels[b]);
        }
        size_t used = strlen(text);
        snprintf(text + used, size - used, "    Slot %d: %s; %s; mix %.1f%%; placement %s\n",
            i + 1, ts_sister_fx_type_name(slot->type),
            slot->type == TS_SISTER_FX_EMPTY ? "EMPTY" : health_on(slot->enabled),
            slot->mix * 100, places[0] ? places : "none");
    }
    size_t used = strlen(text);
    snprintf(text + used, size - used,
        "  States describe transport, switches and routing, not detected sound; fades/tails may continue.\n");
}

static unsigned health_count(uint64_t value)
{ return value > 999999u ? 999999u : (unsigned)value; }

static void ts_audio_health_native(unsigned *driver, unsigned *control, unsigned *worker, int *kind)
{
    *driver = *control = *worker = 0; *kind = 0;
#if defined(TAPESISTER_HAS_ASIO)
    if (ts_asio_owns(ts_real_output)) {
        *kind = 1;
        *driver = ts_asio_driver_xruns();
        *control = ts_asio_control_skips();
        return;
    }
#endif
#if defined(__linux__)
    TsJackEndpoint *e = ts_jack_endpoint(ts_real_output);
    if (e && e->client) {
        *kind = 2;
        *worker = atomic_load_explicit(&e->xruns, memory_order_relaxed);
    }
#endif
}

static void ts_audio_health_sample(AudioState *audio, Uint32 now)
{
    unsigned driver, control, worker;
    ts_audio_health_native(&driver, &control, &worker, &ts_health.native_kind);
    if (ts_health.device_generation != audio->audio_health_generation) {
        ts_health.device_generation = audio->audio_health_generation;
        ts_health.driver_base = ts_health.control_base = ts_health.worker_base = 0;
    }
    /* Native counters restart with their device. Also tolerate counter wrap. */
    if (driver < ts_health.driver_base) ts_health.driver_base = 0;
    if (control < ts_health.control_base) ts_health.control_base = 0;
    if (worker < ts_health.worker_base) ts_health.worker_base = 0;
    ts_health.driver_reports = driver - ts_health.driver_base;
    ts_health.control_skips = control - ts_health.control_base;
    ts_health.worker_gaps = worker - ts_health.worker_base;
    ts_realtime_diagnostics_get(&audio->realtime_diagnostics, &ts_health.snapshot);
    if (ts_health.snapshot.callback_count != ts_health.seen_callbacks) {
        ts_health.seen_callbacks = ts_health.snapshot.callback_count;
        ts_health.last_callback = now;
    }
}

static void ts_audio_health_reset(AudioState *audio)
{
    /* One brief reset under existing exclusion; normal telemetry reads never
       acquire this lock. Do not count the reset itself as a control edit. */
    SDL_AudioDeviceID real = ts_real_output;
    if (real) ts_native_lock(real);
    ts_realtime_diagnostics_reset(&audio->realtime_diagnostics);
    if (real) ts_native_unlock(real);
    ts_audio_health_native(&ts_health.driver_base, &ts_health.control_base,
                          &ts_health.worker_base, &ts_health.native_kind);
    ts_health.device_generation = audio->audio_health_generation;
    ts_health.seen_callbacks = 0;
    audio->audio_health_started = ts_health.last_callback = SDL_GetTicks();
    ts_health.last_update = 0;
    ts_audio_health_sample(audio, SDL_GetTicks());
}

static void ts_audio_health_report(AudioState *audio, char *report, size_t size)
{
    ts_audio_health_sample(audio, SDL_GetTicks());
    const TsRealtimeDiagnosticsSnapshot *s = &ts_health.snapshot;
    const char *driver = ts_native_current_driver();
    char setup[3072];
    ts_audio_health_setup_report(setup, sizeof(setup), SDL_GetTicks());
    snprintf(report, size,
        "TapeSister Audio Health\n"
        "Backend: %s\nOutput: %s\nConnection: %s\n"
        "Actual output: %u Hz, %u frames, %u channels\n"
        "Buffer duration: %.3f ms (not round-trip latency)\n"
        "%s"
        "Measurement age: %.1f s; telemetry: %s\n"
        "Callbacks: %llu; frames: %llu\n"
        "Recent processing: %.2f%%; average %.3f ms; peak %.3f ms\n"
        "Maximum callback: %.3f ms; near budget: %llu; over budget: %llu\n"
        "Long start gaps: %llu; maximum extra gap: %.3f ms\n"
        "Control holds: %llu; maximum hold: %.3f ms; holds over buffer: %llu\n"
        "Maximum UI lock-acquisition wait: %.3f ms\n"
        "Longest hold call site: %s\n"
        "Native counters available: %s\n"
        "ASIO driver reports: %u; ASIO control-lock skips: %u; JACK worker gaps: %u\n"
        "Callback configuration: 0x%04x\n"
        "Start gaps may include scheduling/backend batching; they do not prove a dropout.\n"
        "Control wait is UI waiting to acquire a lock; hold is time playback is excluded.\n"
        "Native counters cover the main output stream, since reset/device restart.\n",
        driver ? driver : "none", ts_output_endpoint.active_name,
        ts_audio_connection_state_name(ts_output_endpoint.state),
        ts_output_endpoint.sample_rate, ts_output_endpoint.buffer_frames,
        ts_output_endpoint.channels,
        ts_output_endpoint.sample_rate ? 1000.0 * ts_output_endpoint.buffer_frames /
            ts_output_endpoint.sample_rate : 0,
        setup,
        (SDL_GetTicks() - audio->audio_health_started) / 1000.0,
        audio->realtime_diagnostics_enabled ? "enabled" : "unavailable",
        (unsigned long long)s->callback_count, (unsigned long long)s->frame_count,
        s->recent_load_percent, s->recent_average_microseconds / 1000,
        s->recent_worst_microseconds / 1000, s->worst_microseconds / 1000,
        (unsigned long long)s->near_overruns, (unsigned long long)s->deadline_overruns,
        (unsigned long long)s->delayed_starts, s->start_gap_microseconds / 1000,
        (unsigned long long)s->control_count, s->control_worst_microseconds / 1000,
        (unsigned long long)s->control_long_holds, s->control_wait_worst_microseconds / 1000,
        s->control_worst_site ? s->control_worst_site : "none",
        ts_health.native_kind == 1 ? "ASIO" : ts_health.native_kind == 2 ? "JACK worker" : "not exposed by this backend",
        ts_health.driver_reports, ts_health.control_skips, ts_health.worker_gaps,
        s->active_configuration);
}

static void ts_audio_health_draw(TsFramebuffer *fb, const TsUiState *ui,
                                 const AudioState *audio, Uint32 now)
{
    const TsRealtimeDiagnosticsSnapshot *s = &ts_health.snapshot;
    uint32_t ink = ui->palette.colors[TS_PALETTE_PATTERN_TEXT];
    uint32_t accent = ui->palette.colors[TS_PALETTE_MOUSE];
    uint32_t effect = ui->palette.colors[TS_PALETTE_PATTERN_EFFECT];
    uint32_t dim = 0xffa5a0aeu, edge = 0xff48434au;
    uint32_t red = 0xffeb777du, amber = 0xffdfbc72u;
    int active = ts_audio_output_is_available() && s->callback_count &&
                 now - ts_health.last_callback < 1000u;
    const char *state = !audio->realtime_diagnostics_enabled ? "TIMING UNAVAILABLE" :
        !ts_audio_output_is_available() ? "OUTPUT UNAVAILABLE" : active ? "MONITORING" : "WAITING FOR AUDIO";
    char line[160];
    cfg_rect(fb, 0, 0, 640, 400, 0xff1b191fu);
    cfg_frame(fb, 8, 8, 624, 384, 0xff242125u, accent);
    cfg_text(fb, 20, 20, "AUDIO HEALTH", accent);
    cfg_text(fb, 490, 20, state, active ? effect : dim);
    const char *driver = ts_native_current_driver();
    snprintf(line, sizeof(line), "%.16s / %.78s", driver ? driver : "NONE",
             ts_output_endpoint.active_name[0] ? ts_output_endpoint.active_name : "NO ACTIVE DEVICE");
    cfg_text(fb, 20, 40, line, ink);
    snprintf(line, sizeof(line), "%u HZ   %u FRAMES   %.3F MS PER BUFFER",
             ts_output_endpoint.sample_rate, ts_output_endpoint.buffer_frames,
             ts_output_endpoint.sample_rate ? 1000.0 * ts_output_endpoint.buffer_frames /
                 ts_output_endpoint.sample_rate : 0);
    cfg_text(fb, 20, 55, line, dim);
    cfg_rect(fb, 20, 73, 600, 1, edge);
    cfg_text(fb, 20, 87, "PROCESSING LOAD", accent);
    if (active && s->recent_callback_count)
        snprintf(line, sizeof(line), "%.1F%% OF BUFFER TIME", s->recent_load_percent);
    else snprintf(line, sizeof(line), "WAITING FOR A FRESH WINDOW");
    uint32_t load_color = s->recent_load_percent >= 100 ? red : s->recent_load_percent >= 90 ? amber : effect;
    cfg_text(fb, 350, 87, line, active ? load_color : dim);
    cfg_frame(fb, 20, 102, 600, 12, 0xff17151au, edge);
    if (active && s->recent_callback_count) {
        int filled = (int)(fmax(0, fmin(100, s->recent_load_percent)) * 5.96);
        cfg_rect(fb, 22, 104, filled, 8, load_color);
    }
    cfg_rect(fb, 20 + 540, 103, 1, 10, amber);
    snprintf(line, sizeof(line), "RECENT AVG %.3F MS    RECENT PEAK %.3F MS",
             s->recent_average_microseconds / 1000, s->recent_worst_microseconds / 1000);
    cfg_text(fb, 20, 128, line, active ? ink : dim);
    snprintf(line, sizeof(line), "MAX %.3F MS    OVER BUDGET %u    NEAR %u",
             s->worst_microseconds / 1000, health_count(s->deadline_overruns), health_count(s->near_overruns));
    cfg_text(fb, 20, 146, line, s->deadline_overruns ? amber : ink);
    cfg_rect(fb, 20, 164, 600, 1, edge);
    cfg_text(fb, 20, 177, "TIMING BETWEEN CALLBACKS", accent);
    snprintf(line, sizeof(line), "LONG START GAPS %u    LONGEST EXTRA GAP %.3F MS",
             health_count(s->delayed_starts), s->start_gap_microseconds / 1000);
    cfg_text(fb, 20, 196, line, ink);
    cfg_text(fb, 20, 212, "GAPS CAN INCLUDE SCHEDULING AND BACKEND BATCHING.", dim);
    cfg_rect(fb, 20, 230, 600, 1, edge);
    cfg_text(fb, 20, 243, "INTERFACE EDITS", accent);
    snprintf(line, sizeof(line), "MAX HOLD %.3F MS   OVER BUFFER %u   MAX UI WAIT %.3F MS",
             s->control_worst_microseconds / 1000, health_count(s->control_long_holds),
             s->control_wait_worst_microseconds / 1000);
    cfg_text(fb, 20, 262, line, s->control_long_holds ? amber : ink);
    cfg_text(fb, 20, 278, "HOLD: PLAYBACK EXCLUDED. WAIT: INTERFACE WAITING FOR AUDIO.", dim);
    cfg_rect(fb, 20, 296, 600, 1, edge);
    cfg_text(fb, 20, 308, "BACKEND SIGNALS", accent);
    if (ts_health.native_kind == 1)
        snprintf(line, sizeof(line), "DRIVER REPORTS %u    CONTROL-LOCK SKIPS %u",
                 health_count(ts_health.driver_reports), health_count(ts_health.control_skips));
    else if (ts_health.native_kind == 2)
        snprintf(line, sizeof(line), "WORKER QUEUE GAPS %u", health_count(ts_health.worker_gaps));
    else snprintf(line, sizeof(line), "DROPOUT COUNTERS NOT EXPOSED BY THIS BACKEND");
    cfg_text(fb, 20, 327, line, ink);
    cfg_text(fb, 20, 347, now < ts_health.message_until ? ts_health.message :
             "F12: OPEN/CLOSE    RESET STARTS A NEW MEASUREMENT", dim);
    cfg_frame(fb, 20, 367, 120, 19, 0xff1b191cu, edge);
    cfg_text(fb, 65, 373, "RESET", accent);
    cfg_frame(fb, 150, 367, 150, 19, 0xff1b191cu, edge);
    cfg_text(fb, 192, 373, "COPY REPORT", effect);
    cfg_frame(fb, 500, 367, 120, 19, 0xff1b191cu, edge);
    cfg_text(fb, 545, 373, "CLOSE", ink);
}

static void ts_audio_health_close(void)
{
    if (ts_health.texture) SDL_DestroyTexture(ts_health.texture);
    if (ts_health.renderer) SDL_DestroyRenderer(ts_health.renderer);
    if (ts_health.window) SDL_DestroyWindow(ts_health.window);
    free(ts_health.framebuffer);
    memset(&ts_health, 0, sizeof(ts_health));
}

static int ts_audio_health_show(TsUiState *ui)
{
    if (!ts_health.window) {
        ts_health.window = SDL_CreateWindow("TapeSister - Audio Health",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 800, 500,
            SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_ALWAYS_ON_TOP);
        if (ts_health.window) {
            SDL_SetWindowMinimumSize(ts_health.window, 640, 400);
            ts_health.renderer = SDL_CreateRenderer(ts_health.window, -1, SDL_RENDERER_ACCELERATED);
            if (!ts_health.renderer)
                ts_health.renderer = SDL_CreateRenderer(ts_health.window, -1, SDL_RENDERER_SOFTWARE);
        }
        if (ts_health.renderer)
            ts_health.texture = SDL_CreateTexture(ts_health.renderer, SDL_PIXELFORMAT_ARGB8888,
                SDL_TEXTUREACCESS_STREAMING, TS_UI_WIDTH, TS_UI_HEIGHT);
        ts_health.framebuffer = calloc(1, sizeof(*ts_health.framebuffer));
        if (!ts_health.window || !ts_health.renderer || !ts_health.texture || !ts_health.framebuffer) {
            snprintf(ui->status, sizeof(ui->status), "AUDIO HEALTH WINDOW FAILED: %.105s", SDL_GetError());
            ts_audio_health_close();
            return 0;
        }
        ts_health.window_id = SDL_GetWindowID(ts_health.window);
    }
    ts_health.visible = 1;
    ts_health.last_update = 0;
    SDL_ShowWindow(ts_health.window);
    SDL_RaiseWindow(ts_health.window);
    return 1;
}

static int ts_audio_health_event(const SDL_Event *event, SDL_Window *main_window,
                                  AudioState *audio, TsUiState *ui)
{
    uint32_t id = event_window_id(event);
    int own = ts_health.window_id && id == ts_health.window_id;
    if (event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_F12 &&
        !(event->key.keysym.mod & (KMOD_CTRL | KMOD_SHIFT | KMOD_ALT | KMOD_GUI))) {
        if (!event->key.repeat) {
            if (ts_health.visible) {
                ts_health.visible = 0; SDL_HideWindow(ts_health.window);
            } else ts_audio_health_show(ui);
        }
        return 1;
    }
    if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT &&
        id == SDL_GetWindowID(main_window) && ui->config_open) {
        int x, y; logical_mouse(main_window, event->button.x, event->button.y, &x, &y);
        if (x >= 166 && x < 284 && y >= 42 && y < 56) {
            ts_audio_health_show(ui); return 1;
        }
    }
    if (!own) return 0;
    /* A key pressed in the instrument may be released after F12 moves focus.
       Preserve normal release/sustain semantics without starting notes here. */
    if (event->type == SDL_KEYUP && note_for_key(event->key.keysym.sym) >= 0) {
        release_note(TS_AUDIO_LOGICAL_OUTPUT, audio, ui, note_for_key(event->key.keysym.sym));
        return 1;
    }
    if ((event->type == SDL_WINDOWEVENT && event->window.event == SDL_WINDOWEVENT_CLOSE) ||
        (event->type == SDL_KEYDOWN && event->key.keysym.sym == SDLK_ESCAPE)) {
        ts_health.visible = 0; SDL_HideWindow(ts_health.window);
    } else if (event->type == SDL_MOUSEBUTTONDOWN && event->button.button == SDL_BUTTON_LEFT) {
        int x, y; logical_mouse(ts_health.window, event->button.x, event->button.y, &x, &y);
        if (y >= 367 && y < 386) {
            if (x >= 20 && x < 140) ts_audio_health_reset(audio);
            else if (x >= 150 && x < 300) {
                char report[8192]; ts_audio_health_report(audio, report, sizeof(report));
                snprintf(ts_health.message, sizeof(ts_health.message), "%s",
                    SDL_SetClipboardText(report) == 0 ? "AUDIO REPORT COPIED" : "COULD NOT COPY REPORT");
                ts_health.message_until = SDL_GetTicks() + 2500u;
                ts_health.last_update = 0;
            } else if (x >= 500 && x < 620) {
                ts_health.visible = 0; SDL_HideWindow(ts_health.window);
            }
        }
    } else if (event->type == SDL_WINDOWEVENT && event->window.event == SDL_WINDOWEVENT_EXPOSED)
        ts_health.last_update = 0;
    else if (event->type == SDL_DROPFILE || event->type == SDL_DROPTEXT) SDL_free(event->drop.file);
    /* Remaining input in this window never edits or triggers the instrument. */
    return 1;
}

static void ts_audio_health_update(AudioState *audio, const TsUiState *ui)
{
    if (!ts_health.visible || !ts_health.renderer ||
        (SDL_GetWindowFlags(ts_health.window) & SDL_WINDOW_MINIMIZED)) return;
    Uint32 now = SDL_GetTicks();
    if (ts_health.last_update && now - ts_health.last_update < 250u) return;
    ts_audio_health_sample(audio, now);
    ts_audio_health_draw(ts_health.framebuffer, ui, audio, now);
    SDL_UpdateTexture(ts_health.texture, NULL, ts_health.framebuffer->pixels, TS_UI_WIDTH * sizeof(uint32_t));
    SDL_RenderCopy(ts_health.renderer, ts_health.texture, NULL, NULL);
    SDL_RenderPresent(ts_health.renderer);
    ts_health.last_update = now;
}
