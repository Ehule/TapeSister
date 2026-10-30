/* Project replacement is a control-thread transaction. Prepare the empty
   session first; only then quiesce both callbacks, join workers and retire
   their documents. Device handles and user libraries belong to the app. */
typedef struct {
    AudioState *audio;
    TsUiState *ui;
    TsInstrument *instrument, *record;
    TsSamplePages *pages;
    int *record_active;
    SisterWindow *sister;
    ExternalInputState *input;
    MosaicController *mosaic;
    PortalController *portal;
    TransformController *transform;
    ImportController *import;
    BrowserAudition *audition;
    PendingFileOperation *file;
    TsSample *clipboard, *selection, *drone, *fm;
    FmBankHistory *fm_history;
    TsFmSeedSequence *seeds;
    TsExchangeOffer *exchange;
} ProjectSession;

static void begin_new_confirmation(ProjectSession *s)
{
    TsUiState *ui = s->ui;
    if (ui_blocking_dialog_open_except_workspaces(ui)) return;
    if (s->audio->capture.state != TS_CAPTURE_IDLE ||
        s->audio->sister.capture.state != TS_CAPTURE_IDLE ||
        external_capture_busy(s->input) || s->input->mosaic_writer ||
        ts_performance_recorder_state(&s->sister->performance_recorder) == TS_PERFORMANCE_FILE_RECORDING ||
        portal_edit_gesture_active(ui)) {
        snprintf(ui->status, sizeof(ui->status), "FINISH OR CANCEL THE CURRENT CAPTURE / EDIT BEFORE NEW");
        return;
    }
    /* Opening or cancelling the prompt never discards a document or take. */
    int exported = ts_tapehead_export(ui->tracker, ui->status, sizeof(ui->status));
    ui->exit_has_unsaved = !exported || runtime_project_state_hash(
        s->pages, s->instrument, s->record, *s->record_active,
        &s->audio->sister, &s->audio->keyboard_sequence) != ui->saved_state_hash;
    ui->exit_for_new = 1;
    ui->exit_confirm_open = 1;
    ui->exit_choice = 2;
    ui->exit_after_save = 0;
    ts_session_log_event("project_new_prompt","opened unsaved=%d",ui->exit_has_unsaved);
    snprintf(ui->status, sizeof(ui->status), "NEW PROJECT? SAVE YOUR WORK BEFORE CLEARING");
}

static void project_audio_free(AudioState *audio)
{
    ts_tracker_playback_free(&audio->tracker);
    ts_performance_free(&audio->performance);
    runtime_mosaic_sources_free(audio);
    ts_performance_free(&audio->tile_launchers);
    for (int i = 0; i < TS_NOTE_BANK_VOICE_CAPACITY; ++i)
        ts_sample_free(&audio->note_snapshots[i]);
    ts_keyboard_sequence_source_free(audio->keyboard_sequence.source);
    ts_sister_runtime_free(&audio->sister);
    ts_capture_free(&audio->capture);
}

static int project_new(SDL_Window *window, SDL_AudioDeviceID device,
                        SDL_AudioDeviceID input_device, ProjectSession *s)
{
    AudioState *audio = s->audio;
    TsUiState *ui = s->ui;
    /* These structures are deliberately heap-owned: AudioState alone is
       close to the default Windows thread stack budget. */
    AudioState *next = calloc(1, sizeof(*next));
    TsUiState *view = calloc(1, sizeof(*view));
    TsSamplePages pages = {0};
    TsMosaic *mosaic = NULL;
    char error[160] = "Not enough memory for a new project";
    int prepared = 0;
    if (!next || !view) goto failed;
    ts_sister_runtime_init(&next->sister);
    next->sister.parameters.fx.slot_count = ts_sister_fx_slot_count(ui->config.pedalboard_slots);
    ts_capture_init(&next->capture);
    ts_note_bank_init(&next->notes);
    ts_keyboard_sequence_init(&next->keyboard_sequence);
    ts_tracker_playback_init(&next->tracker);
    ts_performance_init(&next->performance);
    ts_performance_init(&next->mosaic_source_performance);
    ts_performance_init(&next->tile_launchers);
    ts_audio_mixer_init(&next->mixer);
    atomic_init(&next->tile_launcher_mask, 0u);
    atomic_init(&next->tracker_capture, 0u);
    atomic_init(&next->sister_file_tap, TS_SISTER_TAP_MIX);
    ts_realtime_diagnostics_init(&next->realtime_diagnostics);
    tapeLinkReaderInit(&next->live_link);
    prepared = 1;
    if (!ts_sample_pages_init(&pages, error, sizeof(error))) goto failed;
    mosaic = ts_mosaic_create();
    if (!mosaic) goto failed;
    ts_ui_init(view);
    view->config = ui->config;
    view->palette = ui->palette;
    view->recipes = ui->recipes;
    memcpy(view->cdp_presets, ui->cdp_presets, sizeof(view->cdp_presets));
    memcpy(view->dsp_presets, ui->dsp_presets, sizeof(view->dsp_presets));
    view->portal.library = ui->portal.library;
    snprintf(view->browser.directory, sizeof(view->browser.directory), "%s", ui->browser.directory);
    ts_ui_refresh_cdp_catalog(view);
    view->tracker = &s->pages->tracker;
    view->sample_page_count = 1;
    view->mosaic = next->mosaic = pages.mosaic = mosaic;
    if(!ts_sample_pages_enable_gallery(&pages,error,sizeof(error)))goto failed;
    next->gallery=view->mosaic_gallery=pages.gallery;
    view->mosaic_scale = 24.0;
    view->mosaic_hscale = 1.0;
    /* Invalidate native waveform caches even if the allocator reuses an address. */
    for (int i = 0; i < TS_UI_WAVEFORM_COUNT; ++i)
        view->waveform_revisions[i] = ui->waveform_revisions[i] + 1u;
    next->output_rate = audio->output_rate;
    next->output_device_channels = audio->output_device_channels;
    next->output_range = audio->output_range;
    next->input_range = audio->input_range;
    next->speakers = audio->speakers;
    next->bank_slot = -1;
    next->fm_output_gain = next->fm_output_target = (float)view->config.fm_output_percent / 100.0f;
    next->realtime_counter_frequency = audio->realtime_counter_frequency;
    next->realtime_diagnostics_enabled = audio->realtime_diagnostics_enabled;
    next->audio_health_generation = audio->audio_health_generation + 1u;
    next->audio_health_started = SDL_GetTicks();
    next->live_link_available = -1;
    next->input_monitor = audio->input_monitor;
    next->external_monitor_enabled = audio->external_monitor_enabled;
    next->record_bank_recorder = audio->record_bank_recorder;
    next->record_source = audio->record_source;
    next->sister_file_recorder = audio->sister_file_recorder;
    ts_sister_runtime_configure_limiter(&next->sister, view->config.sister_limiter_enabled,
        view->config.sister_limiter_ceiling_db, view->config.sister_limiter_lookahead_ms,
        view->config.sister_limiter_release_ms);
    ts_sister_runtime_set_master_output_gain(&next->sister, (float)view->config.master_output_percent / 100.0f);
    ts_sister_runtime_input_available(&next->sister, s->input->ownership.available);
    if (!ts_sister_runtime_reconfigure(&next->sister,
            audio->output_rate > 0 ? (uint32_t)audio->output_rate : 48000u,
            2u, error, sizeof(error))) goto failed;
    /* Configuration keeps device/MIDI/UI preferences, while project processors
       start from their defaults rather than inheriting the previous session. */
    view->config.master_eq = next->sister.master_eq.controls;
    view->config.eq_morph = next->sister.master_eq.morph;
    view->config.compressor = next->sister.compressor.controls;
    view->config.spatial = next->sister.spatial.controls;
    view->config.router = view->router_live = next->sister.router.controls;
    view->config.router_performance = next->sister.router.performance;
    view->config.matrix = next->sister.matrix.controls;
    view->config.insert = next->sister.insert.controls;

    ts_session_log_discontinuity("new project");
    /* No fallible project allocations remain past this point. Pause before
       joining workers; none may publish into the new document generation. */
    if (input_device) SDL_PauseAudioDevice(input_device, 1);
    if (device) SDL_PauseAudioDevice(device, 1);
    ts_tapehead_project_replaced();
    browser_audition_stop(device, audio, ui, s->audition, 1);
    discard_import_worker(s->import);
    ts_audio_import_free(&s->import->decoded);
    import_controller_init(s->import);
    if (s->transform->worker) {
        TransformWorker *w = s->transform->worker;
        SDL_AtomicSet(&w->cancel, 1);
        SDL_WaitThread(w->thread, NULL);
        ts_sample_free(&w->input);
        ts_sample_free(&w->dsp_output);
        ts_cdp_run_result_free(&w->result);
        free(w);
    }
    discard_transform_preview(device, audio, ui, s->transform);
    transform_controller_init(s->transform);
    discard_fm_preview_job(ui);
    portal_free(s->portal);
    canvas_eq_close(device, audio, ui);
    /* A FILE writer may be draining after STOP. Complete its WAV header before
       reusing the recorder; finished recordings on disk remain intact. */
    if (s->sister->performance_writer) {
        SDL_WaitThread(s->sister->performance_writer, NULL);
        s->sister->performance_writer = NULL;
    }
    ts_performance_recorder_free(&s->sister->performance_recorder);
    ts_performance_recorder_init(&s->sister->performance_recorder);
    ts_external_recorder_free(&s->input->recorder);
    ts_external_recorder_init(&s->input->recorder);
    atomic_store(&s->input->direct_monitor, 0);
    atomic_store(&s->input->record_source, TS_RECORD_SOURCE_EXT);
    s->input->ownership.requests &= TS_INPUT_CONSUMER_ACTIVITY;
    ts_input_monitor_set_enabled(&s->input->monitor, 0, s->input->sample_rate);
    s->input->mosaic_recording = s->input->quick_recording = 0;
    tapeLinkReaderClose(&audio->live_link);
    next->live_link_buffer = audio->live_link_buffer;
    next->live_link_buffer_frames = audio->live_link_buffer_frames;
    if (next->live_link_buffer)
        memset(next->live_link_buffer, 0, next->live_link_buffer_frames * 2u * sizeof(float));
    project_audio_free(audio);
    ts_tracker_edit_free(ui->tracker_edit);
    mosaic_controller_free(s->mosaic);
    ts_mosaic_free(ts_sample_pages_mosaic_root(s->pages));
    ts_sample_pages_free(s->pages);
    ts_instrument_free(s->instrument);
    ts_instrument_init(s->instrument);
    ts_instrument_free(s->record);
    ts_instrument_init(s->record);
    ts_sample_free(s->clipboard);
    ts_sample_free(s->selection);
    ts_sample_free(s->drone);
    ts_sample_free(s->fm);
    fm_bank_history_free(s->fm_history);
    fm_bank_history_init(s->fm_history);
    ts_exchange_offer_init(s->exchange);
    *s->record_active = 0;
    *s->file = (PendingFileOperation){0};
    *s->pages = pages;
    *audio = *next;
    *ui = *view;
    free(next);
    free(view);
    /* Reconnect controller pointers, never old project buffers. */
    s->mosaic->pages = s->pages;
    char library_path[TS_CDP_PATH_MAX];
    snprintf(library_path, sizeof(library_path), "%s", s->portal->library_path);
    int writable = s->portal->library_writable;
    memset(s->portal, 0, sizeof(*s->portal));
    snprintf(s->portal->library_path, sizeof(s->portal->library_path), "%s", library_path);
    s->portal->library_writable = writable;
    ts_sample_init(&s->portal->source);
    ts_sample_init(&s->portal->create_original);
    for (int i = 0; i < TS_PORTAL_HISTORY; ++i)
        ts_cdp_run_result_init(&s->portal->history[i]);
    s->portal->mosaic = s->mosaic;
    s->portal->pages = s->pages;
    s->portal->create_seeds = s->seeds;
    s->portal->external_input = s->input;
    int sister_visible = s->sister->model.visible;
    ts_sister_ui_model_init(&s->sister->model, &ui->config);
    s->sister->model.parameters = audio->sister.parameters;
    s->sister->model.visible = sister_visible;
    s->sister->power_visual = TS_SISTER_UI_POWER_VISUAL_NONE;
    s->sister->preset_index = s->sister->fallout_preset_index = SIZE_MAX;
    s->sister->fallout_preset_modified = 0;
    s->sister->parameter_lock_gesture = s->sister->prism_drag_lens = 0;
    ts_ui_pointer_drag_cancel(&s->sister->parameter_drag);
    s->sister->rendered_model_valid = 0;
    sister_prism_bank_sync(s->sister);
    matrix_close();
    source_route_close();
    performance_close();
    spatial_window_close();
    SDL_StopTextInput();
    SDL_CaptureMouse(SDL_FALSE);
    ui->saved_state_hash = runtime_project_state_hash(s->pages, s->instrument,
        s->record, 0, &audio->sister, &audio->keyboard_sequence);
    project_mark_saved(ui, s->pages, s->instrument, s->record, &audio->sister, &audio->keyboard_sequence);
    snprintf(ui->status, sizeof(ui->status), "NEW UNTITLED PROJECT - READY");
    if (window) SDL_SetWindowTitle(window, TAPESISTER_WINDOW_TITLE " - Untitled");
    if (input_device) SDL_PauseAudioDevice(input_device, !s->input->ownership.requests);
    if (device) SDL_PauseAudioDevice(device, 0);
    return 1;

failed:
    if (prepared) project_audio_free(next);
    free(next);
    free(view);
    ts_sample_pages_free(&pages);
    ts_mosaic_free(mosaic);
    ui->new_project_pending = ui->exit_for_new = ui->exit_after_save = 0;
    snprintf(ui->status, sizeof(ui->status), "NEW FAILED - CURRENT PROJECT KEPT: %.112s", error);
    return 0;
}
