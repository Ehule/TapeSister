    }

    SDL_zero(obtained);
    desired = ts_output_desired;
    desired.samples = (Uint16)ts_live_config->audio_buffer_frames;
    ts_audio_endpoint_configure(&ts_output_endpoint,
                                wanted != NULL ? wanted : "");
    ts_audio_endpoint_begin_open(&ts_output_endpoint, 0);
    replacement = SDL_OpenAudioDevice(
        wanted, 0, &desired, &obtained, ts_output_allowed_changes);
    if (replacement == 0) {
        snprintf(error, error_size, "%.120s", SDL_GetError());
        if (old != 0) {
            ts_output_endpoint = previous;
            ts_audio_endpoint_configure(&ts_output_endpoint,
                                        wanted != NULL ? wanted : "");
            ts_output_endpoint.state = TS_AUDIO_CONNECTION_FALLBACK_ACTIVE;
            ts_output_endpoint.fallback_approved = 1;
            snprintf(ts_output_endpoint.last_error,
                     sizeof(ts_output_endpoint.last_error), "%s", error);
        } else {
            ts_audio_endpoint_open_failed(&ts_output_endpoint, error);
        }
        ts_audio_log_endpoint(&ts_output_endpoint);
        return 0;
    }

    if (old != 0 && audio != NULL && ts_live_ui != NULL)
        stop_all_force(TS_AUDIO_LOGICAL_OUTPUT, audio, ts_live_ui);
    if (old != 0) SDL_PauseAudioDevice(old, 1);
    ts_real_output = replacement;
    ts_audio_active_device_name(active_name, sizeof(active_name), wanted, 0);
    ts_audio_endpoint_opened(
        &ts_output_endpoint, (uint32_t)replacement,
        active_name, 0,
        (uint32_t)obtained.freq, obtained.channels, obtained.samples,
        ts_audio_format_name(obtained.format));
    ts_output_desired = desired;
    if (ts_output_obtained_target != NULL)
        *ts_output_obtained_target = obtained;
    if (audio != NULL) audio->output_rate = obtained.freq;
    ts_audio_reconfigure_sister_if_needed(audio, &obtained);
    ts_copy_name(ts_active_output, sizeof(ts_active_output),
                 wanted != NULL ? wanted : "");
    if (old != 0) SDL_CloseAudioDevice(old);
    SDL_PauseAudioDevice(replacement, 0);
    ts_audio_log_endpoint(&ts_output_endpoint);
    return 1;
}

static uint8_t ts_audio_capture_preferred_channels(const char *device)
{
    SDL_AudioSpec spec;
    int count;

#if SDL_VERSION_ATLEAST(2, 24, 0)
    if (device == NULL) {
        SDL_zero(spec);
        if (SDL_GetDefaultAudioInfo(NULL, &spec, 1) == 0 &&
            spec.channels >= 1u) {
            return spec.channels > TS_INPUT_DEVICE_CHANNEL_MAX ?
                   TS_INPUT_DEVICE_CHANNEL_MAX : spec.channels;
        }
    }
#endif

#if SDL_VERSION_ATLEAST(2, 0, 16)
    count = SDL_GetNumAudioDevices(1);
    for (int index = 0; index < count; ++index) {
        const char *name = SDL_GetAudioDeviceName(index, 1);
        if (device != NULL && (name == NULL || strcmp(name, device) != 0))
            continue;
        SDL_zero(spec);
        if (SDL_GetAudioDeviceSpec(index, 1, &spec) == 0 &&
            spec.channels >= 1u) {
            return spec.channels > TS_INPUT_DEVICE_CHANNEL_MAX ?
                   TS_INPUT_DEVICE_CHANNEL_MAX : spec.channels;
        }
        if (device == NULL) break;
    }
#else
    (void)device;
    (void)spec;
    count = 0;
#endif
    (void)count;
    return 0u;
}

static SDL_AudioDeviceID ts_audio_open_capture_widest(
    const char *device, const SDL_AudioSpec *desired, SDL_AudioSpec *obtained,
    int allowed_changes)
{
    SDL_AudioSpec attempt;
    SDL_AudioSpec candidate_spec;
    SDL_AudioDeviceID candidate = 0;
    SDL_AudioDeviceID old = ts_real_capture;
    TsAudioEndpoint previous;
    char open_error[TS_AUDIO_ENDPOINT_ERROR_MAX] = "";
    char active_name[TS_AUDIO_ENDPOINT_NAME_MAX];
    uint8_t preferred;
    uint8_t maximum;

    ts_audio_endpoints_init();
    previous = ts_capture_endpoint;
    ts_audio_endpoint_configure(&ts_capture_endpoint,
                                device != NULL ? device : "");
    if (old == 0) ts_audio_endpoint_begin_open(&ts_capture_endpoint, 0);

    if (desired == NULL) {
        SDL_SetError("Capture format is missing");
        return 0;
    }
    maximum = desired->channels;
    if (maximum == 0u || maximum > TS_INPUT_DEVICE_CHANNEL_MAX)
        maximum = TS_INPUT_DEVICE_CHANNEL_MAX;
    preferred = ts_audio_capture_preferred_channels(device);
    if (preferred != 0u) maximum = preferred;
    if (obtained != NULL) SDL_zero(*obtained);

    /*
     * Start with the selected device's advertised layout. This matters on
     * PipeWire/PulseAudio, where asking for eight channels can create an
     * artificial remixed stream instead of exposing the node's physical
     * channel count. Older/opaque backends fall back to a descending exact
     * probe. Rate and buffer-size negotiation remain enabled. This is a
     * control-thread device-open path; no probing reaches either callback.
     */
    for (uint8_t probe = 0u; ; ++probe) {
        uint8_t channels = ts_input_capture_probe_request(maximum, probe);
        if (channels == 0u) break;
        attempt = *desired;
        attempt.channels = channels;
        SDL_zero(candidate_spec);
        candidate = SDL_OpenAudioDevice(
            device, 1, &attempt, &candidate_spec, allowed_changes);
        if (candidate == 0) {
            snprintf(open_error, sizeof(open_error), "%s", SDL_GetError());
            continue;
        }
        if (candidate_spec.format == attempt.format &&
            candidate_spec.freq > 0 && candidate_spec.samples > 0 &&
            ts_input_capture_probe_accepts(attempt.channels,
                                           candidate_spec.channels)) {
            if (old != 0) SDL_CloseAudioDevice(old);
            ts_real_capture = candidate;
            ts_audio_active_device_name(active_name, sizeof(active_name),
                                        device, 1);
            ts_audio_endpoint_opened(
                &ts_capture_endpoint, (uint32_t)candidate,
                active_name, 0,
                (uint32_t)candidate_spec.freq, candidate_spec.channels,
                candidate_spec.samples,
                ts_audio_format_name(candidate_spec.format));
            ts_capture_desired = attempt;
            ts_capture_allowed_changes = allowed_changes;
            ts_capture_userdata = desired->userdata;
            ts_copy_name(ts_active_capture, sizeof(ts_active_capture),
                         device != NULL ? device : "");
            ts_capture_reconfigure_pending = 0;
            ts_capture_reconfigure_error[0] = '\0';
            if (obtained != NULL) *obtained = candidate_spec;
            ts_audio_log_endpoint(&ts_capture_endpoint);
            return TS_AUDIO_LOGICAL_CAPTURE;
        }
        SDL_CloseAudioDevice(candidate);
        candidate = 0;
    }
    if (open_error[0] == '\0')
        snprintf(open_error, sizeof(open_error),
                 "No exact float capture layout available with 1-8 channels");
    SDL_SetError("%s", open_error);
    if (old != 0) {
        ts_capture_endpoint = previous;
        ts_audio_endpoint_configure(&ts_capture_endpoint,
                                    device != NULL ? device : "");
        ts_capture_endpoint.state = TS_AUDIO_CONNECTION_FALLBACK_ACTIVE;
        ts_capture_endpoint.fallback_approved = 1;
        snprintf(ts_capture_endpoint.last_error,
                 sizeof(ts_capture_endpoint.last_error), "%s", open_error);
    } else {
        ts_audio_endpoint_open_failed(&ts_capture_endpoint, open_error);
    }
    ts_audio_log_endpoint(&ts_capture_endpoint);
    return 0;
}

static SDL_AudioDeviceID ts_audio_open_device(const char *device, int iscapture,
                                               const SDL_AudioSpec *desired,
                                               SDL_AudioSpec *obtained,
                                               int allowed_changes)
{
    SDL_AudioDeviceID real;
    if (iscapture)
        return ts_audio_open_capture_widest(
            device, desired, obtained, allowed_changes);

    if (desired != NULL) ts_output_desired = *desired;
    ts_output_allowed_changes = allowed_changes;
    ts_output_userdata = desired != NULL ? desired->userdata : NULL;
    ts_output_obtained_target = obtained;

    {
        const char *wanted = device;
        char active_name[TS_AUDIO_ENDPOINT_NAME_MAX];
        if (device == NULL && ts_configured_output_name() != NULL) {
            wanted = ts_configured_output_name();
        }

        ts_audio_endpoints_init();
        ts_audio_endpoint_configure(&ts_output_endpoint,
                                    wanted != NULL ? wanted : "");
        ts_audio_endpoint_begin_open(&ts_output_endpoint, 0);
        real = SDL_OpenAudioDevice(wanted, 0, desired, obtained, allowed_changes);
        if (real != 0) {
            ts_copy_name(ts_active_output, sizeof(ts_active_output),
                         wanted != NULL ? wanted : "");
            ts_audio_active_device_name(active_name, sizeof(active_name),
                                        wanted, 0);
            ts_audio_endpoint_opened(
                &ts_output_endpoint, (uint32_t)real,
                active_name, 0,
                obtained != NULL ? (uint32_t)obtained->freq : 0u,
                obtained != NULL ? obtained->channels : 0u,
                obtained != NULL ? obtained->samples : 0u,
                obtained != NULL ? ts_audio_format_name(obtained->format) :
                                   "UNKNOWN");
        } else {
            const char *open_error = SDL_GetError();
            ts_audio_endpoint_open_failed(&ts_output_endpoint, open_error);
            snprintf(ts_pending_notice, sizeof(ts_pending_notice),
                     "AUDIO OUTPUT UNAVAILABLE: %.112s", open_error);
        }
        ts_audio_log_endpoint(&ts_output_endpoint);
    }

    ts_real_output = real;
    if (real == 0 && obtained != NULL) SDL_zero(*obtained);
    /*
     * Keep a stable logical handle even when startup output is absent. The
     * obtained rate remains zero, so existing audition guards still report
     * AUDIO UNAVAILABLE, while SAVE CONFIG can recover without restarting.
     */
    return TS_AUDIO_LOGICAL_OUTPUT;
}

static int ts_audio_open_approved_default_output(char *error,
                                                  size_t error_size)
{
    SDL_AudioSpec desired;
    SDL_AudioSpec obtained;
    SDL_AudioDeviceID replacement;
    AudioState *audio = (AudioState *)ts_output_userdata;
    char active_name[TS_AUDIO_ENDPOINT_NAME_MAX];
    if (ts_live_config == NULL || ts_output_desired.freq <= 0) {
        snprintf(error, error_size, "Playback audio has not been initialized");
        return 0;
    }
    ts_audio_endpoint_configure(&ts_output_endpoint,
                                ts_live_config->audio_output_device);
    if (!ts_audio_endpoint_may_open_default(&ts_output_endpoint, 1)) {
        snprintf(error, error_size, "Default output was not approved");
        return 0;
    }
    desired = ts_output_desired;
    desired.samples = (Uint16)ts_live_config->audio_buffer_frames;
    SDL_zero(obtained);
    ts_audio_endpoint_begin_open(&ts_output_endpoint, 1);
    replacement = SDL_OpenAudioDevice(NULL, 0, &desired, &obtained,
                                      ts_output_allowed_changes);
    if (replacement == 0) {
        snprintf(error, error_size, "%.150s", SDL_GetError());
        ts_audio_endpoint_open_failed(&ts_output_endpoint, error);
        ts_audio_log_endpoint(&ts_output_endpoint);
        return 0;
    }
    ts_real_output = replacement;
    ts_output_desired = desired;
    if (ts_output_obtained_target != NULL)
        *ts_output_obtained_target = obtained;
    if (audio != NULL) {
        audio->output_rate = obtained.freq;
        ts_audio_reconfigure_sister_if_needed(audio, &obtained);
    }
    ts_active_output[0] = '\0';
    ts_audio_active_device_name(active_name, sizeof(active_name), NULL, 0);
    ts_audio_endpoint_opened(
        &ts_output_endpoint, (uint32_t)replacement, active_name, 1,
        (uint32_t)obtained.freq, obtained.channels, obtained.samples,
        ts_audio_format_name(obtained.format));
    SDL_PauseAudioDevice(replacement, 0);
    ts_audio_log_endpoint(&ts_output_endpoint);
    error[0] = '\0';
    return 1;
}

static int ts_audio_resolve_output_failure(SDL_Window *window)
{
    enum {
        TS_OUTPUT_DECISION_RETRY = 1,
        TS_OUTPUT_DECISION_DEFAULT = 2,
        TS_OUTPUT_DECISION_SILENT = 3,
        TS_OUTPUT_DECISION_EXIT = 4
    };
    static const SDL_MessageBoxButtonData buttons[] = {
        {SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, TS_OUTPUT_DECISION_RETRY,
         "Retry selected device"},
        {0, TS_OUTPUT_DECISION_DEFAULT, "Use system default this session"},
        {0, TS_OUTPUT_DECISION_SILENT, "Continue without physical output"},
        {SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, TS_OUTPUT_DECISION_EXIT,
         "Exit TapeSister"}
    };
    SDL_MessageBoxData box;
    char message[640];
    char error[160];
    int selected = TS_OUTPUT_DECISION_SILENT;

    ts_audio_endpoints_init();
    if (ts_audio_output_is_available() || !ts_output_endpoint.explicit_device)
        return ts_audio_output_is_available();
    snprintf(message, sizeof(message),
             "Configured output:\n%s\n\nCould not be opened:\n%s\n\n"
             "TapeSister will not silently use another device. The configured "
             "device will remain unchanged.",
             ts_output_endpoint.configured_name,
             ts_output_endpoint.last_error[0] != '\0' ?
             ts_output_endpoint.last_error : "Unknown SDL error");
    SDL_zero(box);
    box.flags = SDL_MESSAGEBOX_WARNING;
    box.window = window;
    box.title = "TapeSister Audio Output Unavailable";
    box.message = message;
    box.numbuttons = (int)(sizeof(buttons) / sizeof(buttons[0]));
    box.buttons = buttons;
    if (SDL_ShowMessageBox(&box, &selected) != 0) {
        fprintf(stderr, "TapeSister audio: recovery dialog failed: %s\n",
                SDL_GetError());
        selected = TS_OUTPUT_DECISION_SILENT;
    }
    if (selected == TS_OUTPUT_DECISION_EXIT) return -1;
    if (selected == TS_OUTPUT_DECISION_RETRY) {
        if (ts_switch_output_for_config(error, sizeof(error))) return 1;
        snprintf(ts_pending_notice, sizeof(ts_pending_notice),
                 "SELECTED OUTPUT STILL UNAVAILABLE: %.106s", error);
        return 0;
    }
    if (selected == TS_OUTPUT_DECISION_DEFAULT) {
        if (ts_audio_open_approved_default_output(error, sizeof(error))) {
            snprintf(ts_pending_notice, sizeof(ts_pending_notice),
                     "TEMPORARY OUTPUT: SYSTEM DEFAULT - CONFIG UNCHANGED");
            return 1;
        }
        snprintf(ts_pending_notice, sizeof(ts_pending_notice),
                 "SYSTEM DEFAULT OUTPUT FAILED: %.112s", error);
        return 0;
    }
    snprintf(ts_pending_notice, sizeof(ts_pending_notice),
             "PHYSICAL OUTPUT DISCONNECTED - CONFIGURED DEVICE RETAINED");
    return 0;
}

static void ts_audio_close_device(SDL_AudioDeviceID device)
{
    SDL_AudioDeviceID real;
    ts_audio_endpoints_init();
    if (device == TS_AUDIO_LOGICAL_OUTPUT)
        real = (SDL_AudioDeviceID)ts_audio_endpoint_closed(
            &ts_output_endpoint);
    else if (device == TS_AUDIO_LOGICAL_CAPTURE)
        real = (SDL_AudioDeviceID)ts_audio_endpoint_closed(
            &ts_capture_endpoint);
    else
        real = device;
    if (real != 0) SDL_CloseAudioDevice(real);
    if (device == TS_AUDIO_LOGICAL_OUTPUT) ts_real_output = 0;
    else if (device == TS_AUDIO_LOGICAL_CAPTURE) {
        ExternalInputState *input = (ExternalInputState *)ts_capture_userdata;
        ts_real_capture = 0;
        if (input != NULL)
            ts_input_activity_set_available(&input->activity, 0u);
    }
}

static void ts_audio_lock_device(SDL_AudioDeviceID device)
{
    SDL_AudioDeviceID real = ts_audio_real_id(device);
    if (real != 0) SDL_LockAudioDevice(real);
}

static void ts_audio_unlock_device(SDL_AudioDeviceID device)
{
    SDL_AudioDeviceID real = ts_audio_real_id(device);
    if (real != 0) SDL_UnlockAudioDevice(real);
}

static void ts_audio_pause_device(SDL_AudioDeviceID device, int pause_on)
{
    if (device == TS_AUDIO_LOGICAL_CAPTURE && pause_on &&
        ts_capture_reconfigure_pending) {
        SDL_AudioDeviceID old = ts_real_capture;
        if (old != 0) SDL_PauseAudioDevice(old, 1);
        (void)ts_reopen_capture_for_config();
    }
    {
        SDL_AudioDeviceID real = ts_audio_real_id(device);
        if (real != 0) SDL_PauseAudioDevice(real, pause_on);
    }
}

static void ts_audio_quit(void)
{
    ts_audio_catalog_free();
    ts_midi_input_destroy(ts_midi_input);
    ts_midi_input = NULL;
    ts_live_ui = NULL;
    ts_live_config = NULL;
    SDL_Quit();
}

static int ts_audio_main_config_load(TsConfig *config, const char *path,
                                     char *error, size_t error_size)
{
    int ok = ts_audio_config_load(config, path, error, error_size);
    if (ok) {
        ts_live_config = config;
        ts_audio_endpoints_init();
        ts_audio_endpoint_configure(&ts_output_endpoint,
                                    config->audio_output_device);
        ts_audio_endpoint_configure(&ts_capture_endpoint,
                                    config->record_input_device);
        ts_locked_tile_dim_percent = ts_parse_locked_tile_dim(path);
    }
    return ok;
}

static int ts_audio_main_config_write(const TsConfig *config, const char *path,
                                      char *error, size_t error_size)
{
    if (!ts_audio_config_save(config, path, error, error_size)) return 0;
    {
        FILE *display = fopen(path, "ab");
        if (display != NULL) {
            fprintf(display,
                    "\n[Display]\n"
                    "; 0 leaves protected tiles unchanged; 90 is the darkest supported dim.\n"
                    "locked_tile_dim_percent=%d\n",
                    ts_locked_tile_dim_percent);
            fclose(display);
        }
    }
    return 1;
}

static int ts_audio_main_config_save(const TsConfig *config, const char *path,
                                     char *error, size_t error_size)
{
    char midi_error[160];
    char switch_error[160];
    int output_changed;
    if (!ts_audio_main_config_write(config, path, error, error_size)) return 0;
    ts_live_config = (TsConfig *)config;

    output_changed = ts_real_output == 0 ||
        ts_output_desired.samples != (Uint16)config->audio_buffer_frames ||
        (config->audio_output_device[0] == '\0' ?
         ts_active_output[0] != '\0' :
         strcmp(config->audio_output_device, ts_active_output) != 0);

    if (output_changed) {
        if (ts_switch_output_for_config(switch_error, sizeof(switch_error))) {
            snprintf(ts_pending_notice, sizeof(ts_pending_notice),
                     "CONFIG SAVED - OUTPUT %.136s",
                     config->audio_output_device[0] != '\0' ?
                     config->audio_output_device : "SYSTEM DEFAULT");
        } else {
            snprintf(ts_pending_notice, sizeof(ts_pending_notice),
                     "OUTPUT SWITCH FAILED - KEPT PREVIOUS: %.100s",
                     switch_error);
        }
    } else {
        snprintf(ts_pending_notice, sizeof(ts_pending_notice),
                 "CONFIG SAVED - AUDIO DEVICE SETTINGS READY");
    }
    if (ts_midi_input != NULL &&
        !ts_midi_input_configure(ts_midi_input, config->midi_input_device,
                                 config->midi_input_channel,
                                 midi_error, sizeof(midi_error)))
        snprintf(ts_pending_notice, sizeof(ts_pending_notice),
                 "CONFIG SAVED - MIDI INPUT UNAVAILABLE: %.104s", midi_error);
    else if (ts_midi_input != NULL && ts_midi_input_is_active(ts_midi_input))
        snprintf(ts_pending_notice, sizeof(ts_pending_notice),
                 "CONFIG SAVED - MIDI READY: %.112s",
                 ts_midi_input_active_name(ts_midi_input));
    else if (ts_midi_input != NULL)
        snprintf(ts_pending_notice, sizeof(ts_pending_notice),
                 "CONFIG SAVED - MIDI %s",
                 strcmp(config->midi_input_device, "OFF") == 0 ?
                 "OFF" : "WAITING FOR AN INPUT");
