static void ts_audio_catalog_refresh(void)
{
    int input_ok = ts_audio_name_list_refresh(
        1, &ts_device_catalog.input_names, &ts_device_catalog.input_count);
    int output_ok = ts_audio_name_list_refresh(
        0, &ts_device_catalog.output_names, &ts_device_catalog.output_count);
    if (ts_live_ui != NULL) {
        snprintf(ts_live_ui->status, sizeof(ts_live_ui->status),
                 input_ok && output_ok ?
                 "AUDIO DEVICES REFRESHED - CLICK < OR > TO SELECT" :
                 "AUDIO DEVICE LIST PARTIAL - SYSTEM DEFAULT REMAINS AVAILABLE");
    }
}

static void ts_audio_catalog_free(void)
{
    ts_audio_name_list_free(&ts_device_catalog.input_names,
                            &ts_device_catalog.input_count);
    ts_audio_name_list_free(&ts_device_catalog.output_names,
                            &ts_device_catalog.output_count);
}

static void ts_cycle_device_name(char *configured, size_t configured_size,
                                 char **names, int count, int direction)
{
    int current = 0;
    int total = count + 1;
    if (configured == NULL || configured_size == 0) return;
    if (configured[0] != '\0') {
        current = 0;
        for (int i = 0; i < count; ++i) {
            if (strcmp(configured, names[i]) == 0) {
                current = i + 1;
                break;
            }
        }
    }
    if (total <= 1) {
        configured[0] = '\0';
        return;
    }
    current = (current + (direction >= 0 ? 1 : total - 1)) % total;
    if (current == 0) configured[0] = '\0';
    else ts_copy_name(configured, configured_size, names[current - 1]);
}

static void ts_cycle_input_channel(int direction)
{
    int channel;
    if (ts_live_config == NULL) return;
    channel = ts_live_config->record_input_channel;
    if (direction >= 0)
        channel = channel >= TS_RECORD_INPUT_CHANNEL_MAX ?
                  TS_RECORD_INPUT_CHANNEL_MIN : channel + 1;
    else
        channel = channel <= TS_RECORD_INPUT_CHANNEL_MIN ?
                  TS_RECORD_INPUT_CHANNEL_MAX : channel - 1;
    ts_live_config->record_input_channel = channel;
}

static int ts_capture_name_matches_config(void)
{
    const char *wanted = ts_configured_input_name();
    if (wanted == NULL) return ts_active_capture[0] == '\0';
    return strcmp(wanted, ts_active_capture) == 0;
}

static void ts_mark_capture_config_after_save(void)
{
    ExternalInputState *input = (ExternalInputState *)ts_capture_userdata;
    ts_capture_reconfigure_error[0] = '\0';
    if (ts_real_capture != 0 && !ts_capture_name_matches_config())
        ts_capture_reconfigure_pending = 1;
    if (input != NULL && ts_live_config != NULL &&
        ts_live_config->record_input_channel <= input->channels)
        input->input_channel = ts_live_config->record_input_channel;
}

static int ts_reopen_capture_for_config(void)
{
    SDL_AudioSpec obtained;
    SDL_AudioDeviceID replacement;
    SDL_AudioDeviceID old = ts_real_capture;
    ExternalInputState *input = (ExternalInputState *)ts_capture_userdata;
    const char *wanted = ts_configured_input_name();

    if (ts_capture_desired.freq <= 0 || input == NULL) {
        snprintf(ts_capture_reconfigure_error,
                 sizeof(ts_capture_reconfigure_error),
                 "Recording input has not been initialized");
        return 0;
    }

    SDL_zero(obtained);
    replacement = SDL_OpenAudioDevice(
        wanted, 1, &ts_capture_desired, &obtained, ts_capture_allowed_changes);
    if (replacement == 0) {
        snprintf(ts_capture_reconfigure_error,
                 sizeof(ts_capture_reconfigure_error),
                 "Configured recording input unavailable: %.96s",
                 SDL_GetError());
        return 0;
    }
    if (obtained.format != AUDIO_F32SYS || obtained.freq <= 0 ||
        obtained.channels == 0) {
        SDL_CloseAudioDevice(replacement);
        snprintf(ts_capture_reconfigure_error,
                 sizeof(ts_capture_reconfigure_error),
                 "Configured recording input returned unsupported audio");
        return 0;
    }
    if (ts_live_config != NULL &&
        ts_live_config->record_input_channel > (int)obtained.channels) {
        SDL_CloseAudioDevice(replacement);
        snprintf(ts_capture_reconfigure_error,
                 sizeof(ts_capture_reconfigure_error),
                 "Input channel %d unavailable; device has %d channel%s",
                 ts_live_config->record_input_channel,
                 (int)obtained.channels,
                 obtained.channels == 1 ? "" : "s");
        return 0;
    }

    if (old != 0) SDL_CloseAudioDevice(old);
    ts_real_capture = replacement;
    input->channels = obtained.channels;
    input->sample_rate = (uint32_t)obtained.freq;
    input->input_channel = ts_live_config != NULL ?
                           ts_live_config->record_input_channel : 1;
    ts_copy_name(input->device_label, sizeof(input->device_label),
                 wanted != NULL ? wanted : "SYSTEM DEFAULT");
    ts_copy_name(ts_active_capture, sizeof(ts_active_capture),
                 wanted != NULL ? wanted : "");
    ts_capture_reconfigure_pending = 0;
    ts_capture_reconfigure_error[0] = '\0';
    return 1;
}

static int ts_switch_output_for_config(char *error, size_t error_size)
{
    SDL_AudioSpec obtained;
    SDL_AudioDeviceID replacement;
    SDL_AudioDeviceID old = ts_real_output;
    const char *wanted = ts_configured_output_name();
    AudioState *audio = (AudioState *)ts_output_userdata;

    if (old != 0) {
        if ((wanted == NULL && ts_active_output[0] == '\0') ||
            (wanted != NULL && strcmp(wanted, ts_active_output) == 0))
            return 1;
    }
    if (ts_output_desired.freq <= 0) {
        snprintf(error, error_size, "Playback audio has not been initialized");
        return 0;
