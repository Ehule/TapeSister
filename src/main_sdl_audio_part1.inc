static int ts_locked_tile_dim_percent = 38;
static int ts_last_sample_page = -1;
static TsUiState *ts_live_ui;

typedef struct {
    int x0, y0, x1, y1;
    int valid;
} TsTileBounds;
static TsTileBounds ts_tile_bounds[TS_BANK_SLOT_COUNT];
static int ts_tile_bounds_ready;

static int ts_first_source(uint16_t mask)
{
    for (int slot = 0; slot < TS_BANK_SLOT_COUNT; ++slot)
        if ((mask & (uint16_t)(1u << slot)) != 0u) return slot;
    return -1;
}

static void runtime_clear_group(AudioState *audio)
{
    if (audio == NULL) return;
    audio->performance_source_mask = 0u;
    audio->performance_shift_pending = 0;
    audio->performance_group_latched = 0;
    audio->performance_raw_mix = (TsStereoFrame){0.0f, 0.0f};
    audio->capture_trigger_ready = 0;
    audio->preserve_release_tails = 0;
    ts_performance_clear(&audio->performance);
}

static int runtime_capture_source_matches(const AudioState *audio,
                                          int selected_slot)
{
    if (audio == NULL) return 0;
    if (audio->performance_group_latched &&
        audio->performance_source_mask != 0u) return 1;
    return audio->capture.source_slot == selected_slot;
}

static TsUiBankAction runtime_bank_action(AudioState *audio, int right_button,
                                          unsigned modifiers)
{
    if (audio != NULL && !right_button &&
        (atomic_load_explicit(&audio->capture.state, memory_order_acquire) ==
             TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER ||
         atomic_load_explicit(&audio->capture.state, memory_order_acquire) ==
             TS_CAPTURE_RECORDING) &&
        modifiers == TS_UI_BANK_MOD_SHIFT) {
        audio->performance_shift_pending = 1;
        return TS_UI_BANK_ACTION_AUDITION;
    }
    if (audio != NULL) audio->performance_shift_pending = 0;
    return ts_ui_bank_action(right_button, modifiers);
}

static int runtime_capture_arm(AudioState *audio, int destination_slot,
                               size_t capacity_frames, uint32_t sample_rate,
                               uint8_t channels, char *error,
                               size_t error_size)
{
    int ok;
    if (audio == NULL) return 0;
    runtime_clear_group(audio);
    audio->capture_scaled = 0;
    audio->capture_requires_scaling = 0;
    ok = ts_capture_arm_channels(&audio->capture, destination_slot,
                                 capacity_frames, sample_rate, channels,
                                 error, error_size);
    return ok;
}

static int runtime_capture_arm_overdub(AudioState *audio, int destination_slot,
                                       size_t capacity_frames,
                                       uint32_t sample_rate, const float *base,
                                       size_t base_frames, uint32_t base_rate,
                                       uint8_t base_channels, char *error,
                                       size_t error_size)
{
    int ok;
    if (audio == NULL) return 0;
    runtime_clear_group(audio);
    audio->capture_scaled = 0;
    audio->capture_requires_scaling = 0;
    ok = ts_capture_arm_overdub_channels(
        &audio->capture, destination_slot, capacity_frames, sample_rate,
        base_channels, base, base_frames, base_rate, base_channels,
        error, error_size);
    return ok;
}

static int runtime_capture_set_source(AudioState *audio, int source_slot,
                                      char *error, size_t error_size)
{
    TsCaptureRecorder *recorder;
    int had_group;
    if (audio == NULL) return 0;
    recorder = &audio->capture;
    if (source_slot == TS_CAPTURE_SOURCE_SYNTH) {
        runtime_clear_group(audio);
        return ts_capture_set_source(recorder, source_slot, error, error_size);
    }
    audio->capture_trigger_ready = 0;
    had_group = audio->performance_group_latched &&
                audio->performance_source_mask != 0u;
    if (audio->performance_shift_pending) {
        uint16_t bit;
        int removing;
        audio->performance_shift_pending = 0;
        if (recorder->state != TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER &&
            recorder->state != TS_CAPTURE_RECORDING) {
            snprintf(error, error_size, "Capture is not accepting source tiles");
            return 0;
        }
        if (source_slot < 0 || source_slot >= TS_BANK_SLOT_COUNT) {
            snprintf(error, error_size, "Invalid Capture source tile");
            return 0;
        }
        if (source_slot == recorder->destination_slot && !recorder->overdub) {
            snprintf(error, error_size,
                     "Capture target cannot join its source group");
            return 0;
        }
        bit = (uint16_t)(1u << source_slot);
        removing = (audio->performance_source_mask & bit) != 0u;
        if (removing)
            audio->performance_source_mask &= (uint16_t)~bit;
        else
            audio->performance_source_mask |= bit;
        audio->performance_group_latched =
            audio->performance_source_mask != 0u;
        recorder->source_slot =
            ts_first_source(audio->performance_source_mask);
        if (recorder->source_slot >= 0)
            recorder->provenance_slot = recorder->source_slot;
        if (removing)
            ts_performance_release_sources_after_pass(
                &audio->performance, bit);
        if (error != NULL && error_size > 0u) error[0] = '\0';
        return 1;
    }

    audio->performance_source_mask =
        source_slot >= 0 && source_slot < TS_BANK_SLOT_COUNT ?
        (uint16_t)(1u << source_slot) : 0u;
    audio->performance_group_latched = 0;
    if (had_group) {
        ts_performance_release_after_pass(&audio->performance);
        if (source_slot < 0 || source_slot >= TS_BANK_SLOT_COUNT)
            audio->preserve_release_tails = 1;
        else if (ts_live_ui != NULL && ts_live_ui->workbench_loop_persistent)
            audio->preserve_release_tails = 0;
        else if (ts_live_ui != NULL && ts_live_ui->workbench_loop_active)
            audio->preserve_release_tails = 2;
        else
            audio->preserve_release_tails = 1;
    } else {
        ts_performance_clear(&audio->performance);
        audio->preserve_release_tails = 0;
    }
    if (!ts_capture_set_source(recorder, source_slot, error, error_size))
        return 0;
    return had_group || recorder->state == TS_CAPTURE_RECORDING ? 0 : 1;
}

static int runtime_capture_trigger(AudioState *audio, char *error,
                                   size_t error_size)
{
    int ready;
    if (audio == NULL) return 0;
    ready = audio->capture_trigger_ready;
    audio->capture_trigger_ready = 0;
    if (audio->performance_group_latched &&
        audio->performance_source_mask != 0u && !ready) {
        if (error != NULL && error_size > 0u)
            snprintf(error, error_size,
                     "Play a QWERTY/MIDI note or staged chord to start the source group");
        return 0;
    }
    return ts_capture_trigger(&audio->capture, error, error_size);
}

static void runtime_scale_capture_if_needed(AudioState *audio)
{
    TsCaptureRecorder *recorder;
    if (audio == NULL) return;
    recorder = &audio->capture;
    if (recorder->overdub || audio->capture_scaled ||
        !audio->capture_requires_scaling ||
        recorder->recorded_frames == 0u) return;
    (void)ts_performance_peak_scale_channels(
        recorder->buffer, recorder->recorded_frames, recorder->channels, 0.98f);
    audio->capture_scaled = 1;
}

static int runtime_capture_cancel(AudioState *audio)
{
    int result;
    if (audio == NULL) return 0;
    result = ts_capture_cancel(&audio->capture);
    if (result) runtime_clear_group(audio);
    return result;
}

static int runtime_capture_stop(AudioState *audio, char *error,
                                size_t error_size)
{
    int result;
    if (audio == NULL) return 0;
    result = ts_capture_stop(&audio->capture, error, error_size);
    return result;
}

static int runtime_capture_write_frame(AudioState *audio, TsStereoFrame frame)
{
    int completed;
    if (audio == NULL) return 0;
    if (audio->performance_group_latched &&
        audio->capture.state == TS_CAPTURE_RECORDING) {
        frame = audio->performance_raw_mix;
        audio->capture_requires_scaling = 1;
    }
    completed = ts_capture_write_frame(&audio->capture, frame);
    return completed;
}

static TsNoteStartResult runtime_note_start_event(
    AudioState *audio, const TsInstrument *instrument, const TsTuning *tuning,
    TsAuditionSource source, const TsNoteEvent *event, int latched,
    int output_rate)
{
    int started;
    int sister_started;
    int sister_before;
    int sister_after;
    TsNoteStartResult result;
    if (audio == NULL) return TS_NOTE_START_FAILED;
    ts_performance_set_attack_ms(&audio->performance, audio->notes.attack_ms);
    sister_before = ts_performance_count(&audio->sister.performance);
    sister_started = ts_sister_runtime_note_on(
        &audio->sister, instrument, event, latched, output_rate);
    /* TILES is a true insert performance group. A successful Sister group
       trigger owns this note event; do not also start the ordinary current
       tile voice and then hide it downstream. */
    if (sister_started > 0) {
        sister_after = ts_performance_count(&audio->sister.performance);
        return latched && sister_after < sister_before ?
               TS_NOTE_TOGGLED_OFF : TS_NOTE_STARTED;
    }
    if (!audio->performance_group_latched ||
        audio->performance_source_mask == 0u) {
        result = ts_note_bank_start_tuned_event(
            &audio->notes, instrument, tuning, source, event, latched,
            output_rate);
    } else {
        started = ts_performance_trigger_group_event(
            &audio->performance, instrument, audio->performance_source_mask,
            event, latched, output_rate);
        audio->capture_trigger_ready =
            started > 0 && ts_performance_count(&audio->performance) > 0;
        result = started > 0 ? TS_NOTE_STARTED : TS_NOTE_START_FAILED;
    }
    return result == TS_NOTE_STARTED || sister_started > 0 ?
           TS_NOTE_STARTED : result;
}

static int runtime_staged_start(AudioState *audio,
                                const TsInstrument *instrument,
                                const TsTuning *tuning,
                                TsAuditionSource source,
                                uint32_t staged_notes,
                                int keyboard_base_note, int output_rate)
{
    if (audio == NULL) return 0;
    ts_performance_set_attack_ms(&audio->performance, audio->notes.attack_ms);
    if (!audio->performance_group_latched ||
        audio->performance_source_mask == 0u)
        return ts_note_bank_start_staged_chord(
            &audio->notes, instrument, tuning, source, staged_notes,
            keyboard_base_note, output_rate);
    ts_note_bank_clear(&audio->notes);
    {
        int started = ts_performance_trigger_staged(
            &audio->performance, instrument, audio->performance_source_mask,
            staged_notes, keyboard_base_note, output_rate);
        audio->capture_trigger_ready = started > 0;
        return started;
    }
}

static void runtime_note_release_event(AudioState *audio,
                                       const TsNoteEvent *event)
{
    if (audio == NULL) return;
    ts_note_bank_release_event(&audio->notes, event);
    ts_performance_release_event(&audio->performance, event);
    ts_sister_runtime_note_off(&audio->sister, event);
}

static void runtime_note_release_midi_channel(AudioState *audio, int channel)
{
    if (audio == NULL) return;
    ts_note_bank_release_midi_channel(&audio->notes, channel);
    ts_performance_release_midi_channel(&audio->performance, channel);
    ts_sister_runtime_release_midi_channel(&audio->sister, channel);
}

static void runtime_note_clear(AudioState *audio)
{
    if (audio == NULL) return;
    ts_note_bank_clear(&audio->notes);
    ts_sister_runtime_panic(&audio->sister);
    audio->capture_trigger_ready = 0;
    if (audio->preserve_release_tails > 0)
        --audio->preserve_release_tails;
    else {
        ts_performance_clear(&audio->performance);
        audio->performance_raw_mix = (TsStereoFrame){0.0f, 0.0f};
    }
}

static void runtime_note_render(AudioState *audio, TsAudioBuses *buses,
                                TsStereoFrame *synth_capture)
{
    TsStereoFrame sample = {0.0f, 0.0f};
    TsStereoFrame fm = {0.0f, 0.0f};
    TsStereoFrame synth = {0.0f, 0.0f};
    TsStereoFrame performance;
    TsStereoFrame launcher;
    if (audio == NULL || buses == NULL) return;
    ts_note_bank_read_buses(&audio->notes, &sample, &fm, &synth);
    performance = ts_performance_read_stereo(
        &audio->performance, &audio->performance_raw_mix);
    launcher = audio->tile_launcher_active ?
        ts_performance_read_stereo(&audio->tile_launchers, NULL) :
        (TsStereoFrame){0.0f, 0.0f};
    audio->tile_launcher_mix = launcher;
    buses->tile_performance.l += sample.l + performance.l + launcher.l;
    buses->tile_performance.r += sample.r + performance.r + launcher.r;
    buses->fm.l += fm.l;
    buses->fm.r += fm.r;
    if (synth_capture != NULL) *synth_capture = synth;
}

static void runtime_note_sync(AudioState *audio,
                              const TsInstrument *instrument, int output_rate)
{
    if (audio == NULL) return;
    ts_note_bank_sync(&audio->notes, instrument, output_rate);
    if (ts_performance_count(&audio->performance) > 0)
        ts_performance_sync(&audio->performance, instrument, output_rate);
    if (ts_performance_count(&audio->tile_launchers) > 0)
        ts_performance_sync(&audio->tile_launchers, instrument, output_rate);
    ts_sister_runtime_sync_sources(&audio->sister, instrument, output_rate);
}
enum {
    TS_AUDIO_CONFIG_PANEL_Y = 40,
    TS_AUDIO_CONFIG_PANEL_H = 155,
    TS_AUDIO_CONFIG_PATH_Y = 58,
    TS_AUDIO_CONFIG_PATH_STEP = 17,
    TS_AUDIO_CONFIG_ROW_H = 16,
    TS_AUDIO_CONFIG_VALUE_X = 188,
    TS_AUDIO_CONFIG_BACKEND_Y = 127,
    TS_AUDIO_CONFIG_INPUT_Y = 144,
    TS_AUDIO_CONFIG_OUTPUT_Y = 161,
    TS_MIDI_CONFIG_INPUT_Y = 178,
    TS_AUDIO_CONFIG_INPUT_W = 430,
    TS_AUDIO_CONFIG_CHANNEL_X = 455,
    TS_AUDIO_CONFIG_CHANNEL_W = 165
};

typedef char TsAudioConfigMustLeaveActionsVisible[
    TS_AUDIO_CONFIG_PANEL_Y + TS_AUDIO_CONFIG_PANEL_H <= TS_CONFIG_ACTION_Y ?
    1 : -1];

#define TS_AUDIO_LOGICAL_OUTPUT ((SDL_AudioDeviceID)0x7fff0001u)
#define TS_AUDIO_LOGICAL_CAPTURE ((SDL_AudioDeviceID)0x7fff0002u)

typedef struct {
    char **input_names;
    int input_count;
    char **output_names;
    int output_count;
} TsAudioDeviceCatalog;

static SDL_AudioDeviceID ts_real_output;
static SDL_AudioDeviceID ts_real_capture;
static TsAudioEndpoint ts_output_endpoint;
static TsAudioEndpoint ts_capture_endpoint;
static int ts_audio_endpoints_initialized;
static TsAudioBackend ts_started_backend = TS_AUDIO_BACKEND_AUTO;
static SDL_AudioSpec ts_output_desired;
static SDL_AudioSpec ts_capture_desired;
static SDL_AudioSpec *ts_output_obtained_target;
static int ts_output_allowed_changes;
static int ts_capture_allowed_changes;
static void *ts_output_userdata;
static void *ts_capture_userdata;
static TsConfig *ts_live_config;
static TsAudioDeviceCatalog ts_device_catalog;
static int ts_config_was_open;
static int ts_capture_reconfigure_pending;
static char ts_active_output[TS_CONFIG_PATH_MAX];
static char ts_active_capture[TS_CONFIG_PATH_MAX];
static char ts_pending_notice[160];
static char ts_capture_reconfigure_error[160];

static void ts_copy_name(char *destination, size_t size, const char *source);

static void ts_audio_endpoints_init(void)
{
    if (ts_audio_endpoints_initialized) return;
    ts_audio_endpoint_init(&ts_output_endpoint, TS_AUDIO_ENDPOINT_OUTPUT,
                           (uint32_t)TS_AUDIO_LOGICAL_OUTPUT);
    ts_audio_endpoint_init(&ts_capture_endpoint, TS_AUDIO_ENDPOINT_CAPTURE,
                           (uint32_t)TS_AUDIO_LOGICAL_CAPTURE);
    ts_audio_endpoints_initialized = 1;
}

static const char *ts_audio_format_name(SDL_AudioFormat format)
{
    if (format == AUDIO_F32SYS) return "F32";
    if (format == AUDIO_S16SYS) return "S16";
    if (format == AUDIO_S32SYS) return "S32";
    if (format == AUDIO_U8) return "U8";
    return "OTHER";
}

static void ts_audio_active_device_name(char *name, size_t name_size,
                                        const char *requested, int capture)
{
    if (requested != NULL && requested[0] != '\0') {
        ts_copy_name(name, name_size, requested);
        return;
    }
#if SDL_VERSION_ATLEAST(2, 24, 0)
    {
        char *default_name = NULL;
        SDL_AudioSpec default_spec;
        SDL_zero(default_spec);
        if (SDL_GetDefaultAudioInfo(&default_name, &default_spec, capture) == 0 &&
            default_name != NULL && default_name[0] != '\0') {
            ts_copy_name(name, name_size, default_name);
            SDL_free(default_name);
            return;
        }
        if (default_name != NULL) SDL_free(default_name);
    }
#else
    (void)capture;
#endif
    ts_copy_name(name, name_size, "SYSTEM DEFAULT");
}

static void ts_audio_log_endpoint(const TsAudioEndpoint *endpoint)
{
    char message[4096];
    if (!ts_audio_endpoint_format_diagnostic(endpoint, message,
                                             sizeof(message)))
        snprintf(message, sizeof(message), "%s diagnostic was truncated",
                 endpoint != NULL &&
                 endpoint->role == TS_AUDIO_ENDPOINT_CAPTURE ?
                 "capture" : "output");
    fprintf(stderr, "TapeSister audio: %s\n", message);
    diagnostic_log("%s", message);
}

static void ts_audio_reconfigure_sister_if_needed(AudioState *audio,
                                                   const SDL_AudioSpec *spec)
{
    if (audio == NULL || spec == NULL || spec->freq <= 0) return;
    if (audio->sister.enabled &&
        audio->sister.machine.buffer.sample_rate == (uint32_t)spec->freq &&
        audio->sister.output_channels == spec->channels)
        return;
    (void)ts_sister_runtime_reconfigure(
        &audio->sister, (uint32_t)spec->freq, spec->channels, NULL, 0u);
}

static void ts_cycle_audio_backend(int direction)
{
    int backend;
    if (ts_live_config == NULL) return;
    backend = (int)ts_live_config->audio_backend;
    backend = (backend + (direction >= 0 ? 1 : 2)) % 3;
    ts_live_config->audio_backend = (TsAudioBackend)backend;
    ts_live_config->audio_backend_invalid = 0;
}

static void ts_cycle_midi_device(int direction)
{
    int count;
    int total;
    int current = 0;
    const char *configured;
    if (ts_live_config == NULL || ts_midi_input == NULL) return;
    configured = ts_live_config->midi_input_device;
    count = ts_midi_input_port_count(ts_midi_input);
    total = count + 2;
    if (strcmp(configured, "OFF") == 0) current = 1;
    else if (configured[0] != '\0') {
        for (int i = 0; i < count; ++i) {
            const char *name = ts_midi_input_port_name(ts_midi_input, i);
            if (name != NULL && strcmp(configured, name) == 0) {
                current = i + 2;
                break;
            }
        }
    }
    current = (current + (direction >= 0 ? 1 : total - 1)) % total;
    if (current == 0) ts_live_config->midi_input_device[0] = '\0';
    else if (current == 1)
        ts_copy_name(ts_live_config->midi_input_device,
                     sizeof(ts_live_config->midi_input_device), "OFF");
    else
        ts_copy_name(ts_live_config->midi_input_device,
                     sizeof(ts_live_config->midi_input_device),
                     ts_midi_input_port_name(ts_midi_input, current - 2));
}

static void ts_cycle_midi_channel(int direction)
{
    int channel;
    if (ts_live_config == NULL) return;
    channel = ts_live_config->midi_input_channel;
    if (direction >= 0)
        channel = channel >= TS_MIDI_INPUT_CHANNEL_MAX ?
                  TS_MIDI_INPUT_CHANNEL_MIN : channel + 1;
    else
        channel = channel <= TS_MIDI_INPUT_CHANNEL_MIN ?
                  TS_MIDI_INPUT_CHANNEL_MAX : channel - 1;
    ts_live_config->midi_input_channel = channel;
}

static void ts_cycle_audio_buffer(int direction)
{
    static const int frames[] = {256, 512, 1024};
    int current = 1;
    if (ts_live_config == NULL) return;
    for (int index = 0; index < 3; ++index)
        if (ts_live_config->audio_buffer_frames == frames[index]) {
            current = index;
            break;
        }
    current = (current + (direction >= 0 ? 1 : 2)) % 3;
    ts_live_config->audio_buffer_frames = frames[current];
}

static void ts_copy_name(char *destination, size_t size, const char *source)
{
    size_t length;
    if (destination == NULL || size == 0) return;
    if (source == NULL) source = "";
    length = strlen(source);
    if (length >= size) length = size - 1u;
    memcpy(destination, source, length);
    destination[length] = '\0';
}

static const char *ts_configured_output_name(void)
{
    return ts_live_config != NULL && ts_live_config->audio_output_device[0] != '\0' ?
           ts_live_config->audio_output_device : NULL;
}

static const char *ts_configured_input_name(void)
{
    return ts_live_config != NULL && ts_live_config->record_input_device[0] != '\0' ?
           ts_live_config->record_input_device : NULL;
}

static SDL_AudioDeviceID ts_audio_real_id(SDL_AudioDeviceID logical)
{
    if (logical == TS_AUDIO_LOGICAL_OUTPUT) return ts_real_output;
    if (logical == TS_AUDIO_LOGICAL_CAPTURE) return ts_real_capture;
    return logical;
}

static int ts_audio_apply_backend_before_sdl(TsAudioBackend backend)
{
    const char *driver = ts_audio_backend_sdl_driver(backend);
    ts_audio_endpoints_init();
    ts_started_backend = backend;
#ifdef _WIN32
    if (driver != NULL &&
        !SDL_SetHintWithPriority(SDL_HINT_AUDIODRIVER, driver,
                                 SDL_HINT_OVERRIDE)) {
        fprintf(stderr,
                "TapeSister audio: could not request configured backend %s\n",
                ts_audio_backend_name(backend));
        return 0;
    }
#else
    if (driver != NULL)
        fprintf(stderr,
                "TapeSister audio: backend %s is Windows-only; using Auto here\n",
                ts_audio_backend_name(backend));
#endif
    fprintf(stderr, "TapeSister audio: configured backend=%s\n",
            ts_audio_backend_name(backend));
    return 1;
}

static int ts_audio_log_active_backend(TsAudioBackend backend)
{
    const char *active = SDL_GetCurrentAudioDriver();
    const char *requested = ts_audio_backend_sdl_driver(backend);
#ifndef _WIN32
    requested = NULL;
#endif
    fprintf(stderr,
            "TapeSister audio: configured backend=%s active SDL backend=%s\n",
            ts_audio_backend_name(backend),
            active != NULL ? active : "NONE");
    diagnostic_log("configured backend=%s active SDL backend=%s",
                   ts_audio_backend_name(backend),
                   active != NULL ? active : "NONE");
    if (requested != NULL &&
        (active == NULL || strcmp(active, requested) != 0)) {
        fprintf(stderr,
                "TapeSister audio: requested backend %s did not activate; "
                "refusing silent backend substitution\n",
                ts_audio_backend_name(backend));
        diagnostic_log(
            "requested backend %s did not activate; startup stopped",
            ts_audio_backend_name(backend));
        return 0;
    }
    return 1;
}

static int ts_audio_device_event_matches(int is_capture,
                                         SDL_AudioDeviceID real_event_id)
{
    ts_audio_endpoints_init();
    return ts_audio_endpoint_matches_removed(
        is_capture ? &ts_capture_endpoint : &ts_output_endpoint,
        is_capture, (uint32_t)real_event_id);
}

static void ts_audio_handle_removed(int is_capture, const char *error)
{
    TsAudioEndpoint *endpoint;
    SDL_AudioDeviceID real;
    ts_audio_endpoints_init();
    endpoint = is_capture ? &ts_capture_endpoint : &ts_output_endpoint;
    real = (SDL_AudioDeviceID)ts_audio_endpoint_removed(endpoint, error);
    if (real != 0) SDL_CloseAudioDevice(real);
    if (is_capture) {
        ts_real_capture = 0;
    } else {
        AudioState *audio = (AudioState *)ts_output_userdata;
        ts_real_output = 0;
        if (ts_output_obtained_target != NULL)
            SDL_zero(*ts_output_obtained_target);
        if (audio != NULL) audio->output_rate = 0;
    }
    ts_audio_log_endpoint(endpoint);
}

static int ts_audio_endpoint_should_retry_output(void)
{
    ts_audio_endpoints_init();
    return ts_audio_endpoint_should_retry(&ts_output_endpoint) ||
           (ts_output_endpoint.state == TS_AUDIO_CONNECTION_FALLBACK_ACTIVE &&
            ts_output_endpoint.explicit_device);
}

static int ts_audio_endpoint_should_retry_capture(void)
{
    ts_audio_endpoints_init();
    return ts_audio_endpoint_should_retry(&ts_capture_endpoint) ||
           (ts_capture_endpoint.state == TS_AUDIO_CONNECTION_FALLBACK_ACTIVE &&
            ts_capture_endpoint.explicit_device);
}

static int ts_audio_output_is_available(void)
{
    return ts_real_output != 0;
}

static char *ts_audio_name_copy(const char *name)
{
    size_t length;
    char *copy;
    if (name == NULL) return NULL;
    length = strlen(name);
    copy = (char *)malloc(length + 1u);
    if (copy != NULL) memcpy(copy, name, length + 1u);
    return copy;
}

static void ts_audio_name_list_free(char ***names, int *count)
{
    if (names != NULL && *names != NULL) {
        for (int i = 0; i < *count; ++i) free((*names)[i]);
        free(*names);
        *names = NULL;
    }
    if (count != NULL) *count = 0;
}

static int ts_audio_name_list_refresh(int capture, char ***names, int *count)
{
    int available = SDL_GetNumAudioDevices(capture);
    char **made = NULL;
    int kept = 0;
    if (available < 0) available = 0;
    if (available > 0) {
        made = (char **)calloc((size_t)available, sizeof(*made));
        if (made == NULL) return 0;
        for (int i = 0; i < available; ++i) {
            const char *name = SDL_GetAudioDeviceName(i, capture);
            if (name == NULL || name[0] == '\0') continue;
            made[kept] = ts_audio_name_copy(name);
            if (made[kept] == NULL) {
                for (int j = 0; j < kept; ++j) free(made[j]);
                free(made);
                return 0;
            }
            ++kept;
        }
    }
    ts_audio_name_list_free(names, count);
    *names = made;
    *count = kept;
    return 1;
}

static int ts_parse_locked_tile_dim(const char *path)
{
    FILE *file;
    char line[256];
    int value = 38;
    if (path == NULL || path[0] == '\0') return value;
    file = fopen(path, "rb");
    if (file == NULL) return value;
    while (fgets(line, sizeof(line), file) != NULL) {
        char *text = line;
        char *equals;
        while (*text != '\0' && isspace((unsigned char)*text)) ++text;
        if (strncmp(text, "locked_tile_dim_percent", 23u) != 0) continue;
        equals = strchr(text, '=');
        if (equals == NULL) continue;
        value = atoi(equals + 1);
        if (value < 0) value = 0;
        if (value > 90) value = 90;
        break;
    }
    fclose(file);
    return value;
}
