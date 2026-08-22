    }

    SDL_zero(obtained);
    replacement = SDL_OpenAudioDevice(
        wanted, 0, &ts_output_desired, &obtained, ts_output_allowed_changes);
    if (replacement == 0) {
        snprintf(error, error_size, "%.120s", SDL_GetError());
        return 0;
    }

    if (old != 0 && audio != NULL && ts_live_ui != NULL)
        stop_all_force(TS_AUDIO_LOGICAL_OUTPUT, audio, ts_live_ui);

    SDL_PauseAudioDevice(replacement, 0);
    ts_real_output = replacement;
    if (ts_output_obtained_target != NULL)
        *ts_output_obtained_target = obtained;
    if (audio != NULL) audio->output_rate = obtained.freq;
    ts_copy_name(ts_active_output, sizeof(ts_active_output),
                 wanted != NULL ? wanted : "");
    if (old != 0) SDL_CloseAudioDevice(old);
    return 1;
}

static SDL_AudioDeviceID ts_audio_open_device(const char *device, int iscapture,
                                               const SDL_AudioSpec *desired,
                                               SDL_AudioSpec *obtained,
                                               int allowed_changes)
{
    SDL_AudioDeviceID real;
    if (iscapture) {
        real = SDL_OpenAudioDevice(device, 1, desired, obtained, allowed_changes);
        if (real == 0) return 0;
        if (ts_real_capture != 0) SDL_CloseAudioDevice(ts_real_capture);
        ts_real_capture = real;
        if (desired != NULL) ts_capture_desired = *desired;
        ts_capture_allowed_changes = allowed_changes;
        ts_capture_userdata = desired != NULL ? desired->userdata : NULL;
        ts_copy_name(ts_active_capture, sizeof(ts_active_capture),
                     device != NULL ? device : "");
        ts_capture_reconfigure_pending = 0;
        ts_capture_reconfigure_error[0] = '\0';
        return TS_AUDIO_LOGICAL_CAPTURE;
    }

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
    else if (device == TS_AUDIO_LOGICAL_CAPTURE) ts_real_capture = 0;
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

static int ts_audio_main_config_save(const TsConfig *config, const char *path,
                                     char *error, size_t error_size)
{
    char switch_error[160];
    int output_changed;
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
    ts_live_config = (TsConfig *)config;

    output_changed = ts_real_output == 0 ||
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
