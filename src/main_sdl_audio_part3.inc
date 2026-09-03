    }

    SDL_zero(obtained);
    desired = ts_output_desired;
    desired.samples = (Uint16)ts_live_config->audio_buffer_frames;
    replacement = SDL_OpenAudioDevice(
        wanted, 0, &desired, &obtained, ts_output_allowed_changes);
    if (replacement == 0) {
        snprintf(error, error_size, "%.120s", SDL_GetError());
        return 0;
    }

    if (old != 0 && audio != NULL && ts_live_ui != NULL)
        stop_all_force(TS_AUDIO_LOGICAL_OUTPUT, audio, ts_live_ui);
    if (old != 0) SDL_PauseAudioDevice(old, 1);
    ts_real_output = replacement;
    ts_output_desired = desired;
    if (ts_output_obtained_target != NULL)
        *ts_output_obtained_target = obtained;
    if (audio != NULL) audio->output_rate = obtained.freq;
    if (audio != NULL)
        (void)ts_sister_runtime_reconfigure(
            &audio->sister, (uint32_t)obtained.freq, obtained.channels,
            NULL, 0u);
    ts_copy_name(ts_active_output, sizeof(ts_active_output),
                 wanted != NULL ? wanted : "");
    if (old != 0) SDL_CloseAudioDevice(old);
    SDL_PauseAudioDevice(replacement, 0);
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
    uint8_t preferred;
    uint8_t maximum;

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
        if (candidate == 0) continue;
        if (candidate_spec.format == attempt.format &&
            candidate_spec.freq > 0 && candidate_spec.samples > 0 &&
            ts_input_capture_probe_accepts(attempt.channels,
                                           candidate_spec.channels)) {
            if (old != 0) SDL_CloseAudioDevice(old);
            ts_real_capture = candidate;
            ts_capture_desired = attempt;
            ts_capture_allowed_changes = allowed_changes;
            ts_capture_userdata = desired->userdata;
            ts_copy_name(ts_active_capture, sizeof(ts_active_capture),
                         device != NULL ? device : "");
            ts_capture_reconfigure_pending = 0;
            ts_capture_reconfigure_error[0] = '\0';
            if (obtained != NULL) *obtained = candidate_spec;
            return TS_AUDIO_LOGICAL_CAPTURE;
        }
        SDL_CloseAudioDevice(candidate);
        candidate = 0;
    }
    SDL_SetError("No exact float capture layout available with 1-8 channels");
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
        int configured_override = 0;
        char first_error[120] = "";
        if (device == NULL && ts_configured_output_name() != NULL) {
            wanted = ts_configured_output_name();
            configured_override = 1;
        }

        real = SDL_OpenAudioDevice(wanted, 0, desired, obtained, allowed_changes);
        if (real == 0 && configured_override) {
            snprintf(first_error, sizeof(first_error), "%.110s", SDL_GetError());
            real = SDL_OpenAudioDevice(NULL, 0, desired, obtained, allowed_changes);
            if (real != 0) {
                snprintf(ts_pending_notice, sizeof(ts_pending_notice),
                         "OUTPUT %.64s UNAVAILABLE - USING SYSTEM DEFAULT",
                         wanted);
                ts_active_output[0] = '\0';
            } else {
                snprintf(ts_pending_notice, sizeof(ts_pending_notice),
                         "AUDIO OUTPUT UNAVAILABLE: %.100s",
                         first_error[0] != '\0' ? first_error : SDL_GetError());
            }
        } else if (real != 0) {
            ts_copy_name(ts_active_output, sizeof(ts_active_output),
                         wanted != NULL ? wanted : "");
        } else {
            snprintf(ts_pending_notice, sizeof(ts_pending_notice),
                     "AUDIO OUTPUT UNAVAILABLE: %.112s", SDL_GetError());
        }
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

static void ts_audio_close_device(SDL_AudioDeviceID device)
{
    SDL_AudioDeviceID real = ts_audio_real_id(device);
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
