static void ts_audio_catalog_refresh(void)
{
    char midi_error[160];
    int input_ok = ts_audio_name_list_refresh(
        1, &ts_device_catalog.input_names, &ts_device_catalog.input_count);
    int output_ok = ts_audio_name_list_refresh(
        0, &ts_device_catalog.output_names, &ts_device_catalog.output_count);
    int midi_ok = ts_midi_input != NULL &&
        ts_midi_input_rescan(ts_midi_input, midi_error, sizeof(midi_error));
    if (ts_live_ui != NULL) {
        snprintf(ts_live_ui->status, sizeof(ts_live_ui->status),
                 input_ok && output_ok && midi_ok ?
                 "AUDIO/MIDI DEVICES REFRESHED - CLICK < OR > TO SELECT" :
                 "DEVICE LIST PARTIAL - AUDIO DEFAULTS AND MIDI OFF REMAIN AVAILABLE");
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
    ExternalInputState *input = (ExternalInputState *)ts_capture_userdata;
    int channel;
    if (ts_live_config == NULL) return;
    if (input != NULL && input->recorder.state != TS_EXTERNAL_CAPTURE_IDLE &&
        input->recorder.state != TS_EXTERNAL_CAPTURE_CANCELED) {
        if (ts_live_ui != NULL)
            snprintf(ts_live_ui->status, sizeof(ts_live_ui->status),
                     "STOP OR CANCEL REC BEFORE CHANGING INPUT MODE");
        return;
    }
    channel = ts_live_config->record_input_channel;
    if (direction >= 0)
        channel = channel >= TS_RECORD_INPUT_CHANNEL_MAX ?
                  TS_RECORD_INPUT_CHANNEL_MIN : channel + 1;
    else
        channel = channel <= TS_RECORD_INPUT_CHANNEL_MIN ?
                  TS_RECORD_INPUT_CHANNEL_MAX : channel - 1;
    ts_live_config->record_input_channel = channel;
    if (input != NULL) {
        int monitor_enabled = ts_input_monitor_enabled(&input->monitor);
        ts_audio_lock_device(TS_AUDIO_LOGICAL_CAPTURE);
        input->input_channel = channel;
        ts_input_monitor_set_enabled(&input->monitor, 0, input->sample_rate);
        if (monitor_enabled)
            ts_input_monitor_set_enabled(&input->monitor, 1,
                                         input->sample_rate);
        ts_audio_unlock_device(TS_AUDIO_LOGICAL_CAPTURE);
    }
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
    if (ts_real_capture != 0 &&
        (!ts_capture_name_matches_config() ||
         (ts_live_config != NULL && ts_capture_desired.samples !=
             (Uint16)ts_live_config->audio_buffer_frames)))
        ts_capture_reconfigure_pending = 1;
    if (input != NULL && ts_live_config != NULL &&
        ts_input_channel_mode_valid(ts_live_config->record_input_channel)) {
        ts_audio_lock_device(TS_AUDIO_LOGICAL_CAPTURE);
        input->input_channel = ts_live_config->record_input_channel;
        ts_audio_unlock_device(TS_AUDIO_LOGICAL_CAPTURE);
    }
}

static int ts_reopen_capture_for_config(void)
{
    SDL_AudioSpec obtained;
    SDL_AudioSpec desired;
    SDL_AudioDeviceID replacement;
    ExternalInputState *input = (ExternalInputState *)ts_capture_userdata;
    AudioState *audio = (AudioState *)ts_output_userdata;
    const char *wanted = ts_configured_input_name();

    if (ts_capture_desired.freq <= 0 || input == NULL) {
        snprintf(ts_capture_reconfigure_error,
                 sizeof(ts_capture_reconfigure_error),
                 "Recording input has not been initialized");
        return 0;
    }
    if (input->recorder.state == TS_EXTERNAL_CAPTURE_ARMED ||
        input->recorder.state == TS_EXTERNAL_CAPTURE_RECORDING) {
        snprintf(ts_capture_reconfigure_error,
                 sizeof(ts_capture_reconfigure_error),
                 "Stop or cancel REC before switching the input device");
        return 0;
    }

    SDL_zero(obtained);
    desired = ts_capture_desired;
    desired.channels = TS_INPUT_DEVICE_CHANNEL_MAX;
    if (ts_live_config != NULL)
        desired.samples = (Uint16)ts_live_config->audio_buffer_frames;
    replacement = ts_audio_open_capture_widest(
        wanted, &desired, &obtained, ts_capture_allowed_changes);
    if (replacement == 0) {
        if (audio != NULL && ts_real_capture == 0)
            ts_sister_runtime_input_available(&audio->sister, 0);
        snprintf(ts_capture_reconfigure_error,
                 sizeof(ts_capture_reconfigure_error),
                 "Configured recording input unavailable: %.96s",
                 SDL_GetError());
        return 0;
    }
    if (obtained.format != AUDIO_F32SYS || obtained.freq <= 0 ||
        obtained.channels == 0 ||
        obtained.channels > TS_INPUT_DEVICE_CHANNEL_MAX) {
        ts_audio_close_device(replacement);
        if (audio != NULL && ts_real_capture == 0)
            ts_sister_runtime_input_available(&audio->sister, 0);
        snprintf(ts_capture_reconfigure_error,
                 sizeof(ts_capture_reconfigure_error),
                 "Configured recording input must provide float audio with 1-8 channels");
        return 0;
    }
    input->channels = obtained.channels;
    input->sample_rate = (uint32_t)obtained.freq;
    ts_input_monitor_set_prime_frames(
        &input->monitor,
        ts_input_monitor_recommended_prime_frames(obtained.samples));
    ts_input_activity_set_available(&input->activity, obtained.channels);
    if (audio != NULL)
        ts_sister_runtime_input_available(&audio->sister, 1);
    input->input_channel = ts_live_config != NULL ?
                           ts_live_config->record_input_channel :
                           TS_INPUT_CHANNEL_MIX;
    {
        int monitor_enabled = ts_input_monitor_enabled(&input->monitor);
        ts_input_monitor_set_enabled(&input->monitor, 0, input->sample_rate);
        if (monitor_enabled)
            ts_input_monitor_set_enabled(&input->monitor, 1, input->sample_rate);
    }
    ts_copy_name(input->device_label, sizeof(input->device_label),
                 wanted != NULL ? wanted : "SYSTEM DEFAULT");
    ts_copy_name(ts_active_capture, sizeof(ts_active_capture),
                 wanted != NULL ? wanted : "");
    ts_capture_reconfigure_pending = 0;
    ts_capture_reconfigure_error[0] = '\0';
    diagnostic_log(
        "capture device reopened: %s rate=%u channels=%d buffer=%u prime=%u",
        input->device_label, input->sample_rate, input->channels,
        (unsigned)obtained.samples,
        ts_input_monitor_recommended_prime_frames(obtained.samples));
    return 1;
}

static int ts_switch_output_for_config(char *error, size_t error_size)
{
    SDL_AudioSpec desired;
    SDL_AudioSpec obtained;
    SDL_AudioDeviceID replacement;
    SDL_AudioDeviceID old = ts_real_output;
    const char *wanted = ts_configured_output_name();
    AudioState *audio = (AudioState *)ts_output_userdata;

    if (audio != NULL && audio->capture.state != TS_CAPTURE_IDLE) {
        snprintf(error, error_size,
                 "Finish or cancel Capture before switching output");
        return 0;
    }
    if (audio != NULL && audio->record_bank_recorder != NULL &&
        audio->record_source != NULL &&
        atomic_load_explicit(audio->record_source, memory_order_acquire) ==
            TS_RECORD_SOURCE_SYNTH &&
        (audio->record_bank_recorder->state == TS_EXTERNAL_CAPTURE_ARMED ||
         audio->record_bank_recorder->state == TS_EXTERNAL_CAPTURE_RECORDING)) {
        snprintf(error, error_size,
                 "Stop or cancel SYNTH REC before switching output");
        return 0;
    }

    if (old != 0 && ts_live_config != NULL &&
        ts_output_desired.samples ==
            (Uint16)ts_live_config->audio_buffer_frames) {
        if ((wanted == NULL && ts_active_output[0] == '\0') ||
            (wanted != NULL && strcmp(wanted, ts_active_output) == 0))
            return 1;
    }
    if (ts_output_desired.freq <= 0) {
        snprintf(error, error_size, "Playback audio has not been initialized");
        return 0;
