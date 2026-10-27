/* Modeless, low-refresh-rate audio health window. All formatting, drawing and
   clipboard operations stay on the UI thread; the reader never locks audio. */
typedef struct {
    int valid;
    Uint32 captured;
    int hold, keyboard_voices, held_voices, tile_voices;
    int arp, outer_arp, arp_slot, audition, mosaic;
    int mosaic_cards, mosaic_scheduled, mosaic_notes;
    int mosaic_muted, mosaic_solo, mosaic_repeat;
    double mosaic_time;
    TsMosaicRange mosaic_range;
    int tracker, tracker_voices, tracker_row, embedded_tracker;
    int sister, rolling, tape_hold, monitor;
    unsigned sources;
    int board, fallout, slot_count;
    float fallout_mix;
    float board_transition_seconds;
    TsSisterFxSlotControls slot[TS_SISTER_FX_SLOT_COUNT];
    int prism, prism_mode, prism_lenses, prism_sequence, prism_matrix;
    int send_assigned, send_available, return_available, router_sequence;
    TsRouterControls router;
    TsMatrixControls matrix;
    float router_wet[TS_ROUTER_COUNT];
} TsAudioHealthSetup;

enum {
    TS_HEALTH_BUTTON_NONE, TS_HEALTH_BUTTON_MARK, TS_HEALTH_BUTTON_SAVE,
    TS_HEALTH_BUTTON_FOLDER, TS_HEALTH_BUTTON_RESET, TS_HEALTH_BUTTON_COPY,
    TS_HEALTH_BUTTON_PROFILE, TS_HEALTH_BUTTON_CLOSE
};

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    TsFramebuffer *framebuffer;
    Uint32 window_id, last_update, message_until;
    uint32_t device_generation;
    unsigned driver_base, control_base, recovery_base, worker_base;
    unsigned driver_reports, control_skips, control_recoveries, worker_gaps;
    int visible, native_kind;
    int flash_button, flash_error;
    Uint32 flash_until;
    const char *flash_label;
    TsRealtimeDiagnosticsSnapshot snapshot;
    TsAudioHealthSetup setup;
    float board_master_gain;
    double board_fade_remaining;
    char message[80];
} TsAudioHealthWindow;
static TsAudioHealthWindow ts_health;
static Uint32 ts_audio_health_window_id(void){return ts_health.window_id;}
static int ts_audio_health_drawable(void)
{ return ts_health.visible && window_drawable(ts_health.window); }

static void ts_audio_health_flash(int button, int error, const char *label)
{
    ts_health.flash_button=button;ts_health.flash_error=error;
    ts_health.flash_label=label;ts_health.flash_until=SDL_GetTicks()+1250u;
    ts_health.last_update=0;
}

/* Called on the UI thread while its existing voice-state lock is held. Copy
   small state only, at most 4 Hz while visible or session recording. No new audio-callback work, lock,
   allocation, formatting or source ownership is introduced. */
static void ts_audio_health_capture_setup(const AudioState *audio,
                                         const TsUiState *ui, Uint32 now)
{
    TsAudioHealthSetup *s = &ts_health.setup;
    if ((!ts_health.visible && !ts_profile_enabled() && !ts_session_log_enabled()) || (s->valid && now - s->captured < 250u)) return;
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
    s->mosaic_cards=s->mosaic_scheduled=s->mosaic_notes=0;
    s->mosaic_muted=s->mosaic_solo=0;
    s->mosaic_time=audio->mosaic?audio->mosaic->time:0;
    s->mosaic_repeat=audio->mosaic && audio->mosaic->repeat;
    s->mosaic_range=audio->mosaic?audio->mosaic->loop_range:(TsMosaicRange){0,0};
    if(audio->mosaic)for(int i=0;i<TS_MOSAIC_EVENTS;++i) {
        const TsMosaicEvent *e=&audio->mosaic->events[i];if(!e->id)continue;
        ++s->mosaic_cards;
        s->mosaic_muted+=e->muted!=0;s->mosaic_solo+=e->solo!=0;
        if(s->mosaic && audio->mosaic->time+1e-10>=e->start && audio->mosaic->time<e->start+e->duration) {
            ++s->mosaic_scheduled;s->mosaic_notes+=e->note_count;
        }
    }
    s->tracker=audio->tracker.running?(audio->tracker.paused?2:1):0;
    s->embedded_tracker=ts_tapehead_running();
    s->tracker_row=audio->tracker.row<0?0:audio->tracker.row;
    s->tracker_voices=0;
    for(int i=0;i<TS_TRACKER_LANES;++i)s->tracker_voices+=audio->tracker.lanes[i].voice.active;
    s->sister = r->enabled; s->rolling = r->rolling; s->tape_hold = r->held;
    s->monitor = r->monitor_enabled; s->sources = r->source_switches;
    s->slot_count = p->fx.slot_count;
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
    s->router = r->router.controls;s->matrix=r->matrix.controls;
    memcpy(s->router.order, r->router.order, sizeof(s->router.order));
    memcpy(s->router_wet, r->router.wet, sizeof(s->router_wet));
    s->router_sequence = r->router.transport.running;
    s->board_transition_seconds=ts_sister_fx_transition_ms(p->fx.master_transition)/1000;
    ts_health.board_master_gain=ts_sister_post_fx_master_engage(&r->post_fx);
    ts_health.board_fade_remaining=r->post_fx.sample_rate?
        (double)r->post_fx.master_engage.remaining/r->post_fx.sample_rate:0;
    s->captured = now; s->valid = 1;
}

static const char *health_on(int enabled) { return enabled ? "ON" : "OFF"; }
static const char *health_running(int running) { return running ? "RUNNING" : "STOPPED"; }
static const char *health_route(const TsAudioHealthSetup *s, int stage)
{
    if(s->matrix.enabled)return stage==TS_ROUTER_INSERT?"legacy inactive":"matrix node";
    int active = ts_router_active(&s->router, stage);
    float wet = s->router_wet[stage];
    if (active && wet < .99999f) return "routing in";
    if (!active && wet > .00001f) return "bypassing";
    return active ? "routed" : "bypassed";
}

static void health_matrix_name(char *text,size_t size,const TsMatrixControls *c,int node,int destination)
{
    int sidechain=destination && node>=TS_MATRIX_EFFECT_SC_BASE && node<TS_MATRIX_INSTANCE_DESTINATIONS;
    int effect=sidechain?node-TS_MATRIX_EFFECT_SC_BASE:ts_matrix_effect_index(node);
    if(effect>=0) {
        const TsMatrixEffectControls *fx=&c->effect[effect];
        snprintf(text,size,"%s #%u%s",ts_matrix_effect_name(fx->type),(unsigned)fx->id,sidechain?" SIDECHAIN":"");
    } else snprintf(text,size,"%s",ts_matrix_node_name(node));
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
    for (int i = 0; i < s->slot_count; ++i)
        if (s->slot[i].type != TS_SISTER_FX_EMPTY && s->slot[i].enabled) ++enabled_slots;
    snprintf(text, size,
        "Active setup (UI snapshot %.3f s old; not the setup at a retained maximum):\n"
        "  Keyboard HOLD: %s; keyboard voice instances: %d; latched/sustained: %d\n"
        "  ARP: %s; outer ARP: %s; active slot: %d\n"
        "  Audition: %s; tile voice instances: %d; Mosaic: %s\n"
        "  Mosaic cards: %d total; %d at playhead; %d assigned notes (includes muted/finished one-shots)\n"
        "  Mosaic position: %.6f s; repeat: %s; loop range: %.6f to %.6f s\n"
        "  Mosaic switches: %d muted; %d solo (whole arrangement)\n"
        "  TrackSister (embedded): %s\n"
        "  SisterTracker: %s; row: %d; voice instances: %d (legacy engine)\n"
        "  Sister Machine: %s, %s; rolling: %s; tape HOLD: %s; monitor: %s\n"
        "  Sister source switches: %s\n"
        "  Fallout: %s, %s; FX master: %s; mix: %.1f%%\n"
        "  Prism: %s, %s; base mode: %s; base lenses: %d; lens sequence: %s; Matrix: %s\n"
        "  Insert: %s; send assigned: %s; send available: %s; return available: %s\n"
        "  Router order: %s > %s > %s > %s > %s; sequencer: %s\n"
        "  Pedalboard master: %s, %s; enabled slots: %u/%d\n",
        (now - s->captured) / 1000.0, health_on(s->hold), s->keyboard_voices, s->held_voices,
        health_running(s->arp), health_running(s->outer_arp),
        s->arp || s->outer_arp ? s->arp_slot + 1 : 0,
        health_running(s->audition), s->tile_voices, health_running(s->mosaic),
        s->mosaic_cards,s->mosaic_scheduled,s->mosaic_notes,
        s->mosaic_time,health_on(s->mosaic_repeat),s->mosaic_range.start,s->mosaic_range.end,
        s->mosaic_muted,s->mosaic_solo,
        health_running(s->embedded_tracker),s->tracker==2?"paused":health_running(s->tracker),s->tracker_row,s->tracker_voices,
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
        health_on(s->board), health_route(s, TS_ROUTER_PEDALBOARD), enabled_slots, s->slot_count);
    if(s->matrix.enabled) {
        size_t used=strlen(text);
        int board_insert=s->matrix.board_insert || (s->matrix.serial_mask&(1u<<TS_MATRIX_BOARD));
        snprintf(text+used,size-used,
            "  Unified routing matrix: ON; legacy order, source switches and external Insert assignments above are inactive. Processor power and Pedalboard master remain active.\n"
            "  Matrix MAIN MIX headroom: %s (only signals routed through MAIN MIX)\n"
            "  Pedalboard Matrix mode: %s\n",
            ts_matrix_main_clip(&s->matrix)?"LEGACY CLIP before downstream effects":"FLOAT",
            board_insert?"INSERT (full signal; master OFF passes dry)":"SEND (wet return; master OFF silences this return)");
        for(int row=0;row<TS_MATRIX_ROWS;++row)for(int d=0;d<TS_MATRIX_DESTINATIONS;++d) {
            int gain=s->matrix.row[row].matrix_gain[d];if(!gain)continue;
            char from[64],to[64];
            if(ts_matrix_is_processor(row))health_matrix_name(from,sizeof(from),&s->matrix,row,0);
            else if(row<TS_MATRIX_FM)snprintf(from,sizeof(from),"IN %d/%d %.23s",(row-TS_MATRIX_NODES)*2+1,(row-TS_MATRIX_NODES)*2+2,s->matrix.input_name[row-TS_MATRIX_NODES]);
            else snprintf(from,sizeof(from),"%s",row==TS_MATRIX_FM?"FM":row==TS_MATRIX_MOSAIC?"MOSAIC":
                row==TS_MATRIX_TILES?"LIVE TILE BUS":row==TS_MATRIX_TRACKER?"TRACKSISTER BUS":"LIVE LINK");
            if(d==TS_MATRIX_AMBISONICS)snprintf(to,sizeof(to),"AMBISONICS");
            else if(d>=TS_MATRIX_SPEAKER_BASE)snprintf(to,sizeof(to),"SPEAKER %d",d-TS_MATRIX_SPEAKER_BASE+1);
            else if(!ts_matrix_is_output(d))health_matrix_name(to,sizeof(to),&s->matrix,d,1);
            else snprintf(to,sizeof(to),"OUT %d/%d %.23s",(d-TS_MATRIX_NODES)*2+1,(d-TS_MATRIX_NODES)*2+2,s->matrix.output_name[d-TS_MATRIX_NODES]);
            used=strlen(text);snprintf(text+used,size-used,"    %s -> %s: %+.1f dB; pan %d%s\n",from,to,20*log10f(gain*.001f),s->matrix.row[row].pan,
                ts_matrix_is_processor(row) && (s->matrix.delayed[row]&ts_matrix_feedback_bit(d))?"; feedback delayed 64 samples":"");
        }
        for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i) {
            const TsMatrixEffectControls *fx=&s->matrix.effect[i];
            if(!fx->id)continue;
            used=strlen(text);snprintf(text+used,size-used,"    Matrix effect #%u: %s; %s; mix %.1f%%\n",
                (unsigned)fx->id,ts_matrix_effect_name(fx->type),health_on(fx->enabled),fx->mix*100);
        }
    }
    {
        size_t used=strlen(text);
        snprintf(text+used,size-used,
            "  Pedalboard master fade: %.2f%% current; target %s; %.3f s remaining; configured transition %.3f s\n",
            ts_health.board_master_gain*100,health_on(s->board),ts_health.board_fade_remaining,s->board_transition_seconds);
    }
    for (int i = 0; i < s->slot_count; ++i) {
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

static void ts_audio_health_native(unsigned *driver, unsigned *control, unsigned *recovered, unsigned *worker, int *kind)
{
    *driver = *control = *recovered = *worker = 0; *kind = 0;
#if defined(TAPESISTER_HAS_ASIO)
    if (ts_asio_owns(ts_real_output)) {
        *kind = 1;
        *driver = ts_asio_driver_xruns();
        *control = ts_asio_control_skips();
        *recovered = ts_asio_control_recoveries();
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
    unsigned driver, control, recovered, worker;
    ts_audio_health_native(&driver, &control, &recovered, &worker, &ts_health.native_kind);
    if (ts_health.device_generation != audio->audio_health_generation) {
        ts_health.device_generation = audio->audio_health_generation;
        ts_health.driver_base = ts_health.control_base = ts_health.recovery_base = ts_health.worker_base = 0;
    }
    /* Native counters restart with their device. Also tolerate counter wrap. */
    if (driver < ts_health.driver_base) ts_health.driver_base = 0;
    if (control < ts_health.control_base) ts_health.control_base = 0;
    if (recovered < ts_health.recovery_base) ts_health.recovery_base = 0;
    if (worker < ts_health.worker_base) ts_health.worker_base = 0;
    ts_health.driver_reports = driver - ts_health.driver_base;
    ts_health.control_skips = control - ts_health.control_base;
    ts_health.control_recoveries = recovered - ts_health.recovery_base;
    ts_health.worker_gaps = worker - ts_health.worker_base;
    ts_realtime_diagnostics_get(&audio->realtime_diagnostics, &ts_health.snapshot);
    (void)now;
}

/* Use the callback's clock, not the moment the UI first notices its count.
   Opening Health after a stall must not make an old callback appear fresh. */
static double ts_audio_health_callback_age(const TsRealtimeDiagnosticsSnapshot *s,uint64_t now)
{
    if(!s->completed_ticks || !s->counter_frequency)return -1;
    return now>s->completed_ticks?(double)(now-s->completed_ticks)/s->counter_frequency:0;
}

static void ts_audio_health_progress_report(const AudioState *audio,char *text,size_t size)
{
    if(!size)return;
    uint64_t now=SDL_GetPerformanceCounter();
    double age=ts_audio_health_callback_age(&ts_health.snapshot,now);
    if(!audio->realtime_diagnostics_enabled)snprintf(text,size,"Callback progress: telemetry unavailable\n");
    else if(age<0)snprintf(text,size,"Callback progress: no timed completion observed since reset/device restart\n");
    else snprintf(text,size,"Callback progress: %s; last completion %.3f s ago\n",
        age<1?"recent":"NO RECENT COMPLETION",age);
    size_t used=strlen(text);
    if(ts_control_timing.depth && ts_control_timing.audio==audio) {
        double held=audio->realtime_counter_frequency && now>ts_control_timing.acquired?
            1000.0*(now-ts_control_timing.acquired)/audio->realtime_counter_frequency:0;
        snprintf(text+used,size-used,"Current UI audio lock: HELD; depth %u; %.3f ms; site %s\n",
            ts_control_timing.depth,held,ts_control_timing.site?ts_control_timing.site:"unknown");
    } else snprintf(text+used,size-used,"Current UI audio lock: not held by this UI thread\n");
}

static void ts_audio_health_reset(AudioState *audio)
{
    ts_session_log_text("health_reset","Displayed counters reset; session timeline retained");
    /* One brief reset under existing exclusion; normal telemetry reads never
       acquire this lock. Do not count the reset itself as a control edit. */
    SDL_AudioDeviceID real = ts_real_output;
    if (real) ts_native_lock(real);
    ts_realtime_diagnostics_reset(&audio->realtime_diagnostics);
    if (real) ts_native_unlock(real);
    ts_audio_health_native(&ts_health.driver_base, &ts_health.control_base,
                          &ts_health.recovery_base, &ts_health.worker_base, &ts_health.native_kind);
    ts_health.device_generation = audio->audio_health_generation;
    ts_profile_reset();
    audio->audio_health_started = SDL_GetTicks();
    ts_health.last_update = 0;
    ts_audio_health_sample(audio, SDL_GetTicks());
}

static void ts_audio_health_report(AudioState *audio, char *report, size_t size)
{
    ts_audio_health_sample(audio, SDL_GetTicks());
    const TsRealtimeDiagnosticsSnapshot *s = &ts_health.snapshot;
    const char *driver = ts_native_current_driver();
    char setup[16384],progress[512];
    ts_audio_health_setup_report(setup, sizeof(setup), SDL_GetTicks());
    ts_audio_health_progress_report(audio,progress,sizeof(progress));
    snprintf(report, size,
        "TapeSister Audio Health\n"
        "Build: %s (%s %s)\n"
        "Backend: %s\nOutput: %s\nConnection: %s\n"
        "Actual output: %u Hz, %u frames, %u channels\n"
        "Buffer duration: %.3f ms (not round-trip latency)\n"
        "%s"
        "Measurement age: %.1f s; telemetry: %s\n"
        "Callbacks: %llu; frames: %llu\n"
        "%s"
        "Recent processing: %.2f%%; average %.3f ms; peak %.3f ms\n"
        "Maximum callback: %.3f ms; near budget: %llu; over budget: %llu\n"
        "Long start gaps: %llu; maximum extra gap: %.3f ms\n"
        "Control holds: %llu; maximum hold: %.3f ms; holds over buffer: %llu\n"
        "Maximum UI lock-acquisition wait: %.3f ms\n"
        "Longest hold call site: %s\n"
        "Native counters available: %s\n"
        "ASIO driver reports: %u; ASIO control-lock skips: %u; ASIO short-lock recoveries: %u; JACK worker gaps: %u\n"
        "ASIO retries brief control overlaps for at most 0.5 ms or 1/8 buffer; callback timings exclude this wait.\n"
        "Callback configuration: 0x%04x\n"
        "Start gaps may include scheduling/backend batching; they do not prove a dropout.\n"
        "Control wait is UI waiting to acquire a lock; hold is time playback is excluded.\n"
        "Native counters cover the main output stream, since reset/device restart.\n",
        TAPESISTER_BUILD_MARKER, __DATE__, __TIME__,
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
        progress,
        s->recent_load_percent, s->recent_average_microseconds / 1000,
        s->recent_worst_microseconds / 1000, s->worst_microseconds / 1000,
        (unsigned long long)s->near_overruns, (unsigned long long)s->deadline_overruns,
        (unsigned long long)s->delayed_starts, s->start_gap_microseconds / 1000,
        (unsigned long long)s->control_count, s->control_worst_microseconds / 1000,
        (unsigned long long)s->control_long_holds, s->control_wait_worst_microseconds / 1000,
        s->control_worst_site ? s->control_worst_site : "none",
        ts_health.native_kind == 1 ? "ASIO" : ts_health.native_kind == 2 ? "JACK worker" : "not exposed by this backend",
        ts_health.driver_reports, ts_health.control_skips, ts_health.control_recoveries, ts_health.worker_gaps,
        s->active_configuration);
    size_t used = strlen(report);
    TsSessionLogStatus session;ts_session_log_status(&session);
    if(used+2<size) {
        snprintf(report+used,size-used,"Session recorder: %s; bytes: %llu; lost audio records: %u; lost events: %u\nRecovery file: %s\nSession export: %s; last destination: %s\n",
            ts_session_log_state_name(session.state),(unsigned long long)session.bytes,session.dropped_audio,session.dropped_events,
            *session.path?session.path:"none",session.export_result==1?"PENDING":session.export_result==2?"SAVED":session.export_result==3?"FAILED":"NONE",
            *session.export_path?session.export_path:"none");
        used=strlen(report);
    }
    if (used + 2 < size) { report[used++] = '\n'; ts_profile_report(report + used, size-used); }
}

static void ts_audio_session_marker(void)
{
    int queued=ts_session_log_text("heard_click","User heard an audible click; reaction time may follow the sound");
    ts_audio_health_flash(TS_HEALTH_BUTTON_MARK,!queued,queued?"CLICK MARKED":"NOT RECORDED");
    snprintf(ts_health.message,sizeof(ts_health.message),"%s",queued?"CLICK MARKED IN SESSION LOG":ts_session_log_enabled()?"LOG BUSY; CLICK MARKER NOT QUEUED":"SESSION LOG IS NOT RECORDING");
    ts_health.message_until=SDL_GetTicks()+2500u;ts_health.last_update=0;
}

static int ts_audio_session_folder_url(const char *path,char *url,size_t size)
{
    char absolute[2048];
    if(!path || !*path || size<9)return 0;
#ifdef _WIN32
    WCHAR input[2048],full[2048];
    if(!MultiByteToWideChar(CP_UTF8,0,path,-1,input,2048))return 0;
    DWORD length=GetFullPathNameW(input,2048,full,NULL);
    if(!length || length>=2048 || !WideCharToMultiByte(CP_UTF8,0,full,-1,absolute,sizeof(absolute),NULL,NULL))return 0;
#else
    if(path[0]!='/' && path[0]!='\\' && !(isalpha((unsigned char)path[0]) && path[1]==':')) {
        char cwd[2048];if(!getcwd(cwd,sizeof(cwd)))return 0;
        int length=snprintf(absolute,sizeof(absolute),"%s/%s",cwd,path);
        if(length<0 || length>=(int)sizeof(absolute))return 0;
    } else {
        if(strlen(path)>=sizeof(absolute))return 0;
        snprintf(absolute,sizeof(absolute),"%s",path);
    }
#endif
    for(char *s=absolute;*s;++s)if(*s=='\\')*s='/';
    size_t end=strlen(absolute);while(end && absolute[end-1]!='/')--end;
    size_t begin=absolute[0]=='/' && absolute[1]=='/'?2:0;
    size_t n=absolute[1]==':'?8:7;
    memcpy(url,"file:///",n);
    for(size_t i=begin;i<end;++i) {
        if(n+4>=size)return 0;
        unsigned char c=(unsigned char)absolute[i];
        if((c<128 && isalnum(c)) || c=='/' || c==':' || c=='-' || c=='_' || c=='.')url[n++]=(char)c;
        else n+=(size_t)snprintf(url+n,size-n,"%%%02X",c);
    }
    url[n]=0;
    return end!=0;
}
static void ts_audio_session_folder(const TsUiState *ui)
{
    char path[2048],url[6160];
    int ok=ts_session_log_export_destination(ui->project_path,path,sizeof(path)) &&
        ts_audio_session_folder_url(path,url,sizeof(url)) && !SDL_OpenURL(url);
    ts_audio_health_flash(TS_HEALTH_BUTTON_FOLDER,!ok,ok?"FOLDER OPENED":"OPEN FAILED");
    snprintf(ts_health.message,sizeof(ts_health.message),"%s",ok?"OPENED SESSION LOG FOLDER":"SAVE SESSION FIRST; FOLDER MAY NOT EXIST");
    ts_health.message_until=SDL_GetTicks()+2500u;
}

/* Called by normal UI service, including while Health is closed/minimized.
   Capture uses the existing voice-state lock; all formatting is after unlock. */
static void ts_audio_session_update(AudioState *audio,TsUiState *ui)
{
    static Uint32 last_sample;static TsAudioHealthSetup previous;
    static unsigned last_export;static uint32_t generation=UINT32_MAX;
    static int connection=-1;
    TsSessionLogStatus status;ts_session_log_status(&status);
    if(status.export_serial!=last_export && status.export_result!=1) {
        last_export=status.export_serial;
        if(status.export_result==2 || status.export_result==3) {
            int copied=status.export_result==2 && status.export_copy_path && SDL_SetClipboardText(status.export_path)==0;
            if(status.export_copy_path)
                ts_audio_health_flash(TS_HEALTH_BUTTON_SAVE,status.export_result==3,
                    status.export_result==3?"SAVE FAILED":"SESSION SAVED");
            snprintf(ts_health.message,sizeof(ts_health.message),"%s",status.export_result==3?"SESSION EXPORT FAILED; RECOVERY LOG KEPT":copied?"SAVED SESSION; FILE PATH COPIED":"SAVED SESSION IN LOG FOLDER");
            if(status.export_result==3)
                snprintf(ui->status,sizeof(ui->status),"SESSION LOG EXPORT FAILED; CHECK F12 REPORT FOR PATH");
            ts_health.message_until=SDL_GetTicks()+5000u;ts_health.last_update=0;
        }
    }
    if(!ts_session_log_enabled())return;
    Uint32 now=SDL_GetTicks();
    TsAudioHealthSetup current=ts_health.setup;
    /* Voice counts and current rows belong in one-second samples; routing,
       controls and transport changes retain their own setup snapshot. */
    current.captured=0;current.keyboard_voices=current.held_voices=current.tile_voices=0;
    current.mosaic_scheduled=current.mosaic_notes=0;
    current.mosaic_time=0;
    current.tracker_row=current.tracker_voices=0;
    if(current.valid && memcmp(&current,&previous,sizeof(current))) {
        char setup[32768];ts_audio_health_setup_report(setup,sizeof(setup),now);
        if(strlen(setup)==sizeof(setup)-1)memcpy(setup+sizeof(setup)-14," [TRUNCATED]",13);
        ts_session_log_text("setup_snapshot",setup);previous=current;
    }
    if(last_sample && now-last_sample<1000u)return;
    last_sample=now;
    ts_audio_health_sample(audio,now);
    const TsRealtimeDiagnosticsSnapshot *s=&ts_health.snapshot;
    double callback_age=ts_audio_health_callback_age(s,SDL_GetPerformanceCounter());
    ts_session_log_event("audio_progress","callback_age_s=%.6f ui_lock_depth=%u ui_lock_site=%s mosaic_time=%.6f muted=%d solo=%d",
        callback_age,ts_control_timing.depth,ts_control_timing.depth && ts_control_timing.site?ts_control_timing.site:"none",
        ts_health.setup.mosaic_time,ts_health.setup.mosaic_muted,ts_health.setup.mosaic_solo);
    ts_session_log_event("pedalboard_progress","master_gain=%.6f target=%d fade_remaining_s=%.6f matrix=%d insert=%d",
        ts_health.board_master_gain,ts_health.setup.board,ts_health.board_fade_remaining,
        ts_health.setup.matrix.enabled,ts_health.setup.matrix.board_insert ||
            (ts_health.setup.matrix.serial_mask&(1u<<TS_MATRIX_BOARD))!=0);
    ts_session_log_event("health_sample","generation=%u callbacks=%llu over_budget=%llu near_budget=%llu max_hold_ms=%.6f holds_over_buffer=%llu max_ui_wait_ms=%.6f backend_counter_kind=%d driver=%u lock_skips=%u recoveries=%u worker_gaps=%u profile=%d",
        audio->audio_health_generation,(unsigned long long)s->callback_count,(unsigned long long)s->deadline_overruns,
        (unsigned long long)s->near_overruns,s->control_worst_microseconds/1000,(unsigned long long)s->control_long_holds,
        s->control_wait_worst_microseconds/1000,ts_health.native_kind,ts_health.driver_reports,ts_health.control_skips,
        ts_health.control_recoveries,ts_health.worker_gaps,ts_profile_enabled());
    ts_session_log_event("workload_sample","setup_age_ms=%u keyboard_voices=%d held=%d tile_voices=%d mosaic=%d mosaic_cards=%d mosaic_at_playhead=%d mosaic_notes=%d arp=%d embedded_tracksister=%d legacy_tracker=%d visualizer=%d frozen=%d quality=%d",
        now-ts_health.setup.captured,ts_health.setup.keyboard_voices,ts_health.setup.held_voices,ts_health.setup.tile_voices,
        ts_health.setup.mosaic,ts_health.setup.mosaic_cards,ts_health.setup.mosaic_scheduled,ts_health.setup.mosaic_notes,
        ts_health.setup.arp,ts_health.setup.embedded_tracker,ts_health.setup.tracker,
        ts_mandala_active(),ts_mandala_frozen(),ui->config.visual_quality);
    if(generation!=audio->audio_health_generation || connection!=(int)ts_output_endpoint.state) {
        generation=audio->audio_health_generation;connection=ts_output_endpoint.state;
        ts_session_log_event("audio_device","generation=%u backend=%s output=%.200s connection=%s rate=%u frames=%u channels=%u",
            generation,ts_native_current_driver()?ts_native_current_driver():"none",ts_output_endpoint.active_name,
            ts_audio_connection_state_name(ts_output_endpoint.state),ts_output_endpoint.sample_rate,ts_output_endpoint.buffer_frames,ts_output_endpoint.channels);
    }
    if(ts_profile_enabled()){char profile[8192];ts_profile_report(profile,sizeof(profile));ts_session_log_text("profile_snapshot",profile);}
}

static void ts_audio_profile_draw(TsFramebuffer *fb, uint32_t ink, uint32_t dim)
{
    TsProfileSnapshot s; ts_profile_snapshot(&s); char line[160];
    snprintf(line,sizeof(line),"%.1F S  INCLUSIVE WALL TIME; AUDIO CHILDREN SAMPLED 1/32 BLOCKS",s.seconds);
    cfg_text(fb,20,42,line,dim);
    cfg_text(fb,20,58,"SUBSYSTEM",ink);
    cfg_text(fb,276,58,"AVG MS",ink);cfg_text(fb,348,58,"PEAK MS",ink);
    cfg_text(fb,428,58,"CALLS/S",ink);cfg_text(fb,508,58,"MS/S",ink);cfg_text(fb,566,58,"PARENT%",ink);
    for(int i=0;i<TS_PROF_COUNT;i++) {
        TsProfileValue *v=&s.value[i]; double ms=s.frequency?1000.0*v->ticks/s.frequency:0;
        uint64_t parent=s.value[i>=TS_PROF_AUDIO?TS_PROF_AUDIO:TS_PROF_UI].ticks;
        int y=73+i*(260/(TS_PROF_COUNT-1));cfg_text(fb,20,y,ts_profile_name((TsProfileId)i),ink);
        snprintf(line,sizeof(line),"%.4F",v->calls?ms/v->calls:0);cfg_text(fb,276,y,line,dim);
        snprintf(line,sizeof(line),"%.3F",s.frequency?1000.0*v->peak/s.frequency:0);cfg_text(fb,348,y,line,dim);
        snprintf(line,sizeof(line),"%.0F",s.seconds?v->calls/s.seconds:0);cfg_text(fb,428,y,line,dim);
        snprintf(line,sizeof(line),"%.1F",s.seconds?ms/s.seconds:0);cfg_text(fb,508,y,line,dim);
        snprintf(line,sizeof(line),"%.1F",i==TS_PROF_WAIT?0:parent?100.0*v->ticks/parent:0);cfg_text(fb,566,y,line,dim);
    }
}

static void ts_audio_health_button(TsFramebuffer *fb, const TsUiState *ui, Uint32 now,
                                  int button, int x, int y, int width, int height,
                                  const char *label, uint32_t text)
{
    int flash=ts_health.flash_button==button && !SDL_TICKS_PASSED(now,ts_health.flash_until);
    int lit=flash || (button==TS_HEALTH_BUTTON_PROFILE && ts_profile_enabled());
    uint32_t panel=ts_theme_color(&ui->palette,TS_THEME_HEALTH,
        lit?TS_COLOR_SELECTION:TS_COLOR_PANEL,lit?0xff365746u:0xff1b191cu);
    uint32_t edge=ts_theme_color(&ui->palette,TS_THEME_HEALTH,
        flash && ts_health.flash_error?TS_COLOR_ERROR:lit?TS_COLOR_ACTIVITY:TS_COLOR_EDGE,
        flash && ts_health.flash_error?0xffeb777du:lit?0xff8addb4u:0xff48434au);
    if(lit)text=ts_theme_color(&ui->palette,TS_THEME_HEALTH,TS_COLOR_SELECTEDTEXT,0xfff4f0f5u);
    if(flash && ts_health.flash_label)label=ts_health.flash_label;
    cfg_frame(fb,x,y,width,height,panel,edge);
    if(lit)cfg_rect(fb,x+1,y+1,width-2,2,edge);
    cfg_text(fb,x+(width-(int)strlen(label)*6)/2,y+(height-7)/2,label,text);
}

static void ts_audio_health_draw(TsFramebuffer *fb, const TsUiState *ui,
                                 const AudioState *audio, Uint32 now)
{
    const TsRealtimeDiagnosticsSnapshot *s = &ts_health.snapshot;
    cfg_palette=&ui->palette;cfg_group=TS_THEME_HEALTH;
    uint32_t ink = ts_theme_legacy(&ui->palette,TS_THEME_HEALTH,TS_COLOR_TEXT,TS_PALETTE_PATTERN_TEXT);
    uint32_t accent = ts_theme_legacy(&ui->palette,TS_THEME_HEALTH,TS_COLOR_FOCUS,TS_PALETTE_MOUSE);
    uint32_t effect = ts_theme_legacy(&ui->palette,TS_THEME_HEALTH,TS_COLOR_ACTIVITY,TS_PALETTE_PATTERN_EFFECT);
    uint32_t dim = ts_theme_color(&ui->palette,TS_THEME_HEALTH,TS_COLOR_DIM,0xffa5a0aeu), edge = ts_theme_color(&ui->palette,TS_THEME_HEALTH,TS_COLOR_EDGE,0xff48434au);
    uint32_t red = ts_theme_color(&ui->palette,TS_THEME_HEALTH,TS_COLOR_ERROR,0xffeb777du), amber = ts_theme_color(&ui->palette,TS_THEME_HEALTH,TS_COLOR_WARNING,0xffdfbc72u);
    double age=ts_audio_health_callback_age(s,SDL_GetPerformanceCounter());
    int active = ts_audio_output_is_available() && age>=0 && age<1;
    const char *state = !audio->realtime_diagnostics_enabled ? "TIMING UNAVAILABLE" :
        !ts_audio_output_is_available() ? "OUTPUT UNAVAILABLE" : active ? "MONITORING" :
        age<0?"WAITING FOR AUDIO":"CALLBACKS STALLED";
    char line[160];
    cfg_rect(fb, 0, 0, 640, 400, ts_theme_color(&ui->palette,TS_THEME_HEALTH,TS_COLOR_BACKGROUND,0xff1b191fu));
    cfg_frame(fb, 8, 8, 624, 384, ts_theme_color(&ui->palette,TS_THEME_HEALTH,TS_COLOR_PANEL,0xff242125u), accent);
    cfg_text(fb, 20, 20, ts_profile_enabled() ? "PERFORMANCE PROFILE" : "AUDIO HEALTH", accent);
    if (ts_profile_enabled()) { ts_audio_profile_draw(fb, ink, dim); goto footer; }
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
    cfg_frame(fb, 20, 102, 600, 12, ts_theme_color(&ui->palette,TS_THEME_HEALTH,TS_COLOR_PANEL,0xff17151au), edge);
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
        snprintf(line, sizeof(line), "DRIVER %u   LOCK SKIPS %u   RECOVERED %u",
                 health_count(ts_health.driver_reports), health_count(ts_health.control_skips),
                 health_count(ts_health.control_recoveries));
    else if (ts_health.native_kind == 2)
        snprintf(line, sizeof(line), "WORKER QUEUE GAPS %u", health_count(ts_health.worker_gaps));
    else snprintf(line, sizeof(line), "DROPOUT COUNTERS NOT EXPOSED BY THIS BACKEND");
    cfg_text(fb, 20, 327, line, ink);
footer:
    {
        TsSessionLogStatus session;ts_session_log_status(&session);
        snprintf(line,sizeof(line),"LOG: %s%s",ts_session_log_state_name(session.state),
            session.dropped_audio || session.dropped_events?" / RECORDS LOST":"");
        if(now<ts_health.message_until)snprintf(line,sizeof(line),"%s",ts_health.message);
        cfg_rect(fb,180,18,440,12,ts_theme_color(&ui->palette,TS_THEME_HEALTH,TS_COLOR_PANEL,0xff242125u));
        cfg_text(fb,180,20,line,session.state==TS_SESSION_ERROR || session.state==TS_SESSION_FULL?amber:dim);
        if(now>=ts_health.message_until && !ts_profile_enabled())cfg_text(fb,490,20,state,active?effect:dim);
        const char *labels[]={"HEARD A CLICK","SAVE SESSION","LOG FOLDER"};
        for(int i=0;i<3;++i)
            ts_audio_health_button(fb,ui,now,TS_HEALTH_BUTTON_MARK+i,20+i*202,344,196,18,labels[i],effect);
    }
    ts_audio_health_button(fb,ui,now,TS_HEALTH_BUTTON_RESET,20,367,120,19,"RESET",accent);
    ts_audio_health_button(fb,ui,now,TS_HEALTH_BUTTON_COPY,150,367,150,19,"COPY REPORT",effect);
    ts_audio_health_button(fb,ui,now,TS_HEALTH_BUTTON_PROFILE,310,367,180,19,
        ts_profile_enabled()?"STOP PROFILE":"START PROFILE",effect);
    ts_audio_health_button(fb,ui,now,TS_HEALTH_BUTTON_CLOSE,500,367,120,19,"CLOSE",ink);
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

static int ts_audio_session_shortcut(const SDL_Event *event)
{
    if(event->type==SDL_KEYDOWN && event->key.keysym.sym==SDLK_F12 &&
       (event->key.keysym.mod & KMOD_CTRL) && (event->key.keysym.mod & KMOD_SHIFT) &&
       !(event->key.keysym.mod & (KMOD_ALT|KMOD_GUI))) {
        if(!event->key.repeat)ts_audio_session_marker();
        return 1;
    }
    return 0;
}

static int ts_audio_health_event(const SDL_Event *event, SDL_Window *main_window,
                                  AudioState *audio, TsUiState *ui)
{
    uint32_t id = event_window_id(event);
    int own = ts_health.window_id && id == ts_health.window_id;
    if(ts_audio_session_shortcut(event))return 1;
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
        if(y>=344 && y<362) {
            if(x>=20 && x<216)ts_audio_session_marker();
            else if(x>=222 && x<418) {
                int queued=ts_session_log_export_project(ui->project_path,1);
                ts_audio_health_flash(TS_HEALTH_BUTTON_SAVE,!queued,queued?"SAVE QUEUED":"SAVE UNAVAILABLE");
                snprintf(ts_health.message,sizeof(ts_health.message),"%s",queued?"SAVING SESSION IN BACKGROUND":"SESSION EXPORT UNAVAILABLE");
                ts_health.message_until=SDL_GetTicks()+2500u;
            } else if(x>=424 && x<620)ts_audio_session_folder(ui);
            ts_health.last_update=0;return 1;
        }
        if (y >= 367 && y < 386) {
            if (x >= 20 && x < 140) {
                ts_audio_health_reset(audio);
                ts_audio_health_flash(TS_HEALTH_BUTTON_RESET,0,"RESET DONE");
            }
            else if (x >= 150 && x < 300) {
                char report[32768]; ts_audio_health_report(audio, report, sizeof(report));
                int copied=SDL_SetClipboardText(report)==0;
                ts_audio_health_flash(TS_HEALTH_BUTTON_COPY,!copied,copied?"COPIED":"COPY FAILED");
                snprintf(ts_health.message, sizeof(ts_health.message), "%s",
                    copied ? "AUDIO REPORT COPIED" : "COULD NOT COPY REPORT");
                ts_health.message_until = SDL_GetTicks() + 2500u;
                ts_health.last_update = 0;
            } else if (x >= 310 && x < 490) {
                ts_profile_enable(!ts_profile_enabled());
                ts_session_log_event("profile","enabled=%d",ts_profile_enabled());
                ts_audio_health_flash(TS_HEALTH_BUTTON_PROFILE,0,
                    ts_profile_enabled()?"PROFILE STARTED":"PROFILE STOPPED");
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
    if (!ts_audio_health_drawable() || !ts_health.renderer) return;
    Uint32 now = SDL_GetTicks();
    if (ts_health.last_update && now - ts_health.last_update < 250u) return;
    ts_audio_health_sample(audio, now);
    ts_audio_health_draw(ts_health.framebuffer, ui, audio, now);
    SDL_UpdateTexture(ts_health.texture, NULL, ts_health.framebuffer->pixels, TS_UI_WIDTH * sizeof(uint32_t));
    SDL_RenderCopy(ts_health.renderer, ts_health.texture, NULL, NULL);
    SDL_RenderPresent(ts_health.renderer);
    ts_health.last_update = now;
}
