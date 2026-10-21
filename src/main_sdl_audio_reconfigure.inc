/* Control-thread transaction: join every endpoint before changing backend or
   ASIO's shared duplex stream. Logical handles and the project remain alive. */
static void ts_audio_session_close(void)
{
    ts_capture_reconfigure_pending = 0;
    ts_audio_insert_devices_suspend();
    ts_audio_close_device(TS_AUDIO_LOGICAL_CAPTURE);
    ts_audio_close_device(TS_AUDIO_LOGICAL_OUTPUT);
#if defined(TAPESISTER_HAS_ASIO)
    ts_asio_quit();
#endif
    SDL_AudioQuit();
    /* SDL may reuse device IDs after AudioInit; discard the retired catalog's
       queued notifications before any new handles can be allocated. */
    SDL_FlushEvents(SDL_AUDIODEVICEADDED, SDL_AUDIODEVICEREMOVED);
    if (ts_output_obtained_target) SDL_zero(*ts_output_obtained_target);
    AudioState *audio = ts_output_userdata;
    if (audio) {
        audio->output_rate = 0;
        audio->output_device_channels = 0;
        audio->sister.insert.output_channels = 0;
        ts_sister_runtime_input_available(&audio->sister, 0);
    }
}

static int ts_audio_session_open(TsConfig *config, int capture,
                                 const char *restore_driver,
                                 char *error, size_t size)
{
    ts_live_config = config;
    if (!ts_audio_apply_backend_before_sdl(config->audio_backend)) goto failed;
    const char *driver = ts_asio_selected || ts_jack_selected ? "dummy" :
        restore_driver ? restore_driver : ts_audio_backend_sdl_driver(config->audio_backend);
    if (SDL_AudioInit(driver) != 0 || !ts_audio_log_active_backend(config->audio_backend)) goto failed;
    SDL_AudioSpec desired = ts_output_desired, obtained;
    SDL_AudioSpec *target = ts_output_obtained_target;
    desired.samples = (Uint16)config->audio_buffer_frames;
    SDL_zero(obtained);
    ts_audio_open_device(NULL, 0, &desired, &obtained, ts_output_allowed_changes);
    ts_output_obtained_target = target;
    if (target) *target = obtained;
    if (!ts_real_output) goto failed;
    AudioState *audio = ts_output_userdata;
    if (audio) {
        audio->output_rate = obtained.freq;
        if (!(audio->sister.enabled &&
              audio->sister.machine.buffer.sample_rate == (uint32_t)obtained.freq &&
              audio->sister.output_channels == 2) &&
            !ts_sister_runtime_reconfigure(&audio->sister, (uint32_t)obtained.freq, 2, error, size))
            return 0;
    }
    if (capture && !ts_reopen_capture_for_config()) {
        snprintf(error, size, "%s", ts_capture_reconfigure_error);
        return 0;
    }
    /* No callback can see a partly initialized output/input layout. */
    if (capture) ts_audio_pause_device(TS_AUDIO_LOGICAL_CAPTURE, 0);
    ts_audio_pause_device(TS_AUDIO_LOGICAL_OUTPUT, 0);
#if defined(TAPESISTER_HAS_ASIO)
    if (ts_asio_selected && !ts_asio_running()) {
        snprintf(error, size, "ASIO driver could not start its stream");
        return 0;
    }
#endif
    return 1;
failed:
    snprintf(error, size, "%.140s", SDL_GetError());
    return 0;
}

static int ts_audio_reconfigure(const TsConfig *config, char *error, size_t size)
{
    AudioState *audio = ts_output_userdata;
    ExternalInputState *input = ts_capture_userdata;
    int busy;
    ts_audio_lock_device(TS_AUDIO_LOGICAL_OUTPUT);
    ts_audio_lock_device(TS_AUDIO_LOGICAL_CAPTURE);
    busy = external_capture_busy(input) || (audio &&
        (audio->capture.state != TS_CAPTURE_IDLE ||
         (audio->record_bank_recorder && audio->record_bank_recorder->state != TS_EXTERNAL_CAPTURE_IDLE) ||
         (audio->sister_file_recorder &&
          ts_performance_recorder_state(audio->sister_file_recorder) != TS_PERFORMANCE_FILE_IDLE)));
    ts_audio_unlock_device(TS_AUDIO_LOGICAL_CAPTURE);
    ts_audio_unlock_device(TS_AUDIO_LOGICAL_OUTPUT);
    if (busy) {
        snprintf(error, size, "Finish or cancel recording before applying audio settings");
        return 0;
    }
    if (!ts_audio_backend_compiled(config->audio_backend)) {
        snprintf(error, size, "Backend %s is unavailable in this build", ts_audio_backend_name(config->audio_backend));
        return 0;
    }
    if (config->audio_backend == TS_AUDIO_BACKEND_ASIO &&
        config->record_input_device[0] &&
        strcmp(config->record_input_device, config->audio_output_device)) {
        snprintf(error, size, "ASIO uses one driver: choose matching input/output or SYSTEM DEFAULT input");
        return 0;
    }
    /* Config is edited in place, so snapshot the active endpoint settings. */
    TsConfig previous = *config;
    previous.audio_backend = ts_started_backend;
    previous.audio_output_range = ts_active_ranges[0];
    previous.audio_input_range = ts_active_ranges[1];
    previous.audio_buffer_frames = ts_output_desired.samples;
    ts_copy_name(previous.audio_output_device, sizeof(previous.audio_output_device), ts_active_output);
    ts_copy_name(previous.record_input_device, sizeof(previous.record_input_device), ts_active_capture);
    ts_copy_name(previous.insert_send_device, sizeof(previous.insert_send_device), insert_names_seen[0]);
    ts_copy_name(previous.insert_return_device, sizeof(previous.insert_return_device), insert_names_seen[1]);
    if (input) previous.record_input_channel = input->input_channel;
    char previous_driver[32];
    ts_copy_name(previous_driver, sizeof(previous_driver), SDL_GetCurrentAudioDriver());
    int had_output = ts_real_output != 0;
    int had_capture = ts_real_capture != 0 || ts_audio_endpoint_should_retry_capture();
    ts_session_log_event("audio_reconfigure","request backend=%s output=%.200s input=%.200s frames=%d previous_backend=%s had_output=%d had_capture=%d",
        ts_audio_backend_name(config->audio_backend),config->audio_output_device,config->record_input_device,
        config->audio_buffer_frames,ts_audio_backend_name(previous.audio_backend),had_output,had_capture);
    if (audio && ts_live_ui) stop_all_force(TS_AUDIO_LOGICAL_OUTPUT, audio, ts_live_ui);
    ts_audio_session_close();
    if (ts_audio_session_open((TsConfig *)config, had_capture, NULL, error, size)) {
        ts_session_log_event("audio_reconfigure","applied backend=%s rate=%u frames=%u channels=%u",
            ts_audio_backend_name(config->audio_backend),ts_output_endpoint.sample_rate,ts_output_endpoint.buffer_frames,ts_output_endpoint.channels);
        ts_capture_reconfigure_error[0] = 0;
        ts_audio_catalog_refresh();
        return 1;
    }
    char failure[160], restore_error[160]="Previous output was unavailable";
    snprintf(failure, sizeof(failure), "%.140s", error);
    ts_session_log_event("audio_reconfigure","failed backend=%s error=%s",ts_audio_backend_name(config->audio_backend),failure);
    ts_audio_session_close();
    int restored = had_output && ts_audio_session_open(&previous, had_capture,
                          previous_driver[0] ? previous_driver : NULL,
                          restore_error, sizeof(restore_error));
    if (restored && audio)
        ts_audio_insert_devices_sync(audio, &previous, 1, restore_error, sizeof(restore_error));
    if (!restored) ts_audio_session_close();
    ts_session_log_event("audio_reconfigure","rollback restored=%d backend=%s error=%s",
        restored,ts_audio_backend_name(previous.audio_backend),restored?"":restore_error);
    ts_live_config = (TsConfig *)config;
    ts_started_backend = previous.audio_backend;
    ts_audio_endpoint_configure(&ts_output_endpoint, config->audio_output_device);
    ts_audio_endpoint_configure(&ts_capture_endpoint, config->record_input_device);
    if (restored) {
        ts_output_endpoint.state = TS_AUDIO_CONNECTION_FALLBACK_ACTIVE;
        ts_output_endpoint.fallback_approved = 1;
        snprintf(error, size, "Previous audio restored: %.120s", failure);
    } else {
        ts_audio_endpoint_open_failed(&ts_output_endpoint, failure);
        if (had_capture) ts_audio_endpoint_open_failed(&ts_capture_endpoint, failure);
        snprintf(error, size, "Audio unavailable; choose devices and save again: %.98s", failure);
    }
    ts_pending_notice[0] = 0;
    ts_audio_catalog_refresh();
    return 0;
}
