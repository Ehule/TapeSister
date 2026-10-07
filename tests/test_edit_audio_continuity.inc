/* Exercise the real controller/callback while the UI owns an in-flight edit. */
typedef struct { SDL_sem *done; float block[512]; } EditCallbackProbe;

static int edit_callback_probe(void *userdata)
{
    EditCallbackProbe *probe = userdata;
    SDL_LockAudioDevice(device);
    audio_callback(&audio, (Uint8 *)probe->block, sizeof(probe->block));
    SDL_UnlockAudioDevice(device);
    SDL_SemPost(probe->done);
    return 0;
}

static int probe_control_renders;
static unsigned control_render_callbacks;

static void check_render_callback(void)
{
    if (!probe_control_renders) return;
    EditCallbackProbe probe = {0};
    probe.done = SDL_CreateSemaphore(0); assert(probe.done);
    SDL_Thread *thread = SDL_CreateThread(edit_callback_probe, "control-audio", &probe);
    assert(thread && SDL_SemWaitTimeout(probe.done, 2000) == 0);
    SDL_WaitThread(thread, NULL); SDL_DestroySemaphore(probe.done);
    double energy = 0;
    for (int i = 0; i < 512; ++i) {
        assert(isfinite(probe.block[i])); energy += fabsf(probe.block[i]);
    }
    assert(energy > .001);
    ++control_render_callbacks;
}

static int checked_material_preview(TsInstrument *instrument, TsMaterialMacroGesture *gesture,
                                    float amount, char *error, size_t size)
{
    check_render_callback();
    int ok = ts_instrument_material_macro_gesture_preview(instrument, gesture, amount, error, size);
    check_render_callback();
    return ok;
}

static int checked_amplitude_preview(TsInstrument *instrument, TsAmplitudeGesture *gesture,
    size_t first, float first_gain, size_t last, float last_gain, char *error, size_t size)
{
    check_render_callback();
    int ok = ts_instrument_amplitude_gesture_preview(instrument, gesture,
        first, first_gain, last, last_gain, error, size);
    check_render_callback();
    return ok;
}

static int checked_canvas_preview(TsInstrument *instrument, TsCanvasGesture *gesture,
                                  int64_t delta, char *error, size_t size)
{
    check_render_callback();
    int ok = ts_instrument_canvas_gesture_preview(instrument, gesture, delta, error, size);
    check_render_callback();
    return ok;
}

static void test_edit_audio_continuity(void)
{
    reset_route(0);
    ts_sister_runtime_disable(&audio.sister);
    ui.workbench_loop_active = ui.workbench_loop_persistent = ui.play_view = 0;
    ui.keyboard_hold = 1;
    keyboard_loop_policy(&audio, &ui);
    click_note(0, 0); click_note(4, 0);
    assert(ts_note_bank_count(&audio.notes) == 2 && !audio.playing);
    assert(power_audio_energy() > .01);
    TsNativeSampleEdit edit;
    char error[160];
    assert(begin_smear_gesture(device, &audio, &ui, &instrument, 0));
    begin_native_sample_edit(device, &audio, &instrument, &edit);
    assert(edit.unlocked && edit.needed[0]);
    const TsNoteVoice *first = ts_note_bank_display_voice(&audio.notes);
    assert(first && first->sample == &edit.copy[0]);
    double position = first->position;
    uint64_t serial = first->serial;
    uint64_t original = ts_sample_hash(first->sample);
    /* This replaces/frees Current while the callback owns its stable copy. */
    assert(ts_instrument_smear_gesture_preview(&instrument, &ui.smear_gesture,
                                               .35f, error, sizeof(error)));
    assert(ts_sample_hash(first->sample) == original);
    EditCallbackProbe probe = {0};
    probe.done = SDL_CreateSemaphore(0); assert(probe.done);
    SDL_Thread *thread = SDL_CreateThread(edit_callback_probe, "edit-audio", &probe);
    assert(thread);
    /* Fails if the expensive render still holds the output lock. */
    assert(SDL_SemWaitTimeout(probe.done, 2000) == 0);
    SDL_WaitThread(thread, NULL); SDL_DestroySemaphore(probe.done);
    double energy = 0;
    for (int i = 0; i < 512; ++i) { assert(isfinite(probe.block[i])); energy += fabsf(probe.block[i]); }
    assert(energy > .01 && first->position != position);
    position = first->position;
    finish_native_sample_edit(device, &audio, &ui, &instrument, &edit);
    assert(ts_note_bank_count(&audio.notes) == 2);
    assert(first->sample == &instrument.current && first->serial == serial);
    assert(fabs(first->position - position) < 1e-6);
    float previous = audio.notes.sample_handoff.last.l;
    TsStereoFrame value = ts_note_bank_read_stereo(&audio.notes);
    assert(fabsf(value.l - previous) < 1e-5f); /* Existing residual fade. */
    serial = first->serial;
    begin_native_sample_edit(device, &audio, &instrument, &edit);
    assert(!ts_instrument_smear_gesture_preview(&instrument, &ui.smear_gesture,
                                                -1.0f, error, sizeof(error)));
    finish_native_sample_edit(device, &audio, &ui, &instrument, &edit);
    assert(first->sample == &instrument.current && first->serial == serial);
    end_smear_gesture(device, &audio, &ui, &instrument, 0);
    assert(voices() == 2);
    assert(begin_warp_gesture(device, &audio, &ui, &instrument, 0));
    assert(preview_warp_gesture(device, &audio, &ui, &instrument, .25f, 44100));
    end_warp_gesture(device, &audio, &ui, &instrument, 0);
    assert(begin_tear_gesture(device, &audio, &ui, &instrument, 0));
    assert(preview_tear_gesture(device, &audio, &ui, &instrument, .25f, 44100));
    end_tear_gesture(device, &audio, &ui, &instrument, 1);
    assert(voices() == 2 && ui.keyboard_hold && !audio.playing);
    for (int i = 0; i < TS_NOTE_BANK_VOICE_CAPACITY; ++i)
        assert(audio.notes.voices[i].sample != &edit.copy[0] &&
               audio.notes.voices[i].sample != &edit.copy[1]);
    stop_all_force(device, &audio, &ui);
    assert(ts_sister_runtime_enable(&audio.sister, 44100, 2, 2, 1.0, error, sizeof(error)));
}

static void test_idle_hold_locking(void)
{
    reset_route(0);
    ui.workbench_loop_active = ui.workbench_loop_persistent = ui.play_view = 0;
    ui.keyboard_hold = 1;
    keyboard_loop_policy(&audio, &ui);
    click_note(0, 0);
    ts_realtime_diagnostics_init(&audio.realtime_diagnostics);
    audio.realtime_counter_frequency = SDL_GetPerformanceFrequency();
    audio.realtime_diagnostics_enabled = 1;
    void *previous_userdata = ts_output_userdata;
    ts_output_userdata = &audio;
    refresh_workbench_loop(device, &audio, &ui, &instrument);
    uint64_t before = atomic_load(&audio.realtime_diagnostics.control_count);
    for (int i = 0; i < 120; ++i) {
        runtime_note_collect_snapshots(device, &audio);
        refresh_workbench_loop(device, &audio, &ui, &instrument);
    }
    assert(atomic_load(&audio.realtime_diagnostics.control_count) == before);
    ui.play_view = 1;
    instrument.view_first = 100; instrument.view_last = 2000;
    refresh_workbench_loop(device, &audio, &ui, &instrument);
    assert(atomic_load(&audio.realtime_diagnostics.control_count) == before + 1);
    const TsNoteVoice *voice = ts_note_bank_display_voice(&audio.notes);
    assert(voice && voice->range_first == 100 && voice->range_last == 2000);
    double step = voice->step;
    instrument.tuning.fine_tune_cents += 100;
    refresh_workbench_loop(device, &audio, &ui, &instrument);
    assert(voice->step != step);
    instrument.tuning.fine_tune_cents -= 100;
    ui.play_view = 0;
    refresh_workbench_loop(device, &audio, &ui, &instrument);
    audio.realtime_diagnostics_enabled = 0;
    ts_output_userdata = previous_userdata;
    stop_all_force(device, &audio, &ui);
}

static void test_other_edit_controls(void)
{
    char error[160];
    for (int mode = 0; mode < 4; ++mode) {
        reset_route(mode == 1 ? 2 : 0);
        ts_sister_runtime_disable(&audio.sister);
        ui.keyboard_hold = mode != 2;
        ui.workbench_loop_active = mode == 2;
        ui.workbench_loop_persistent = ui.play_view = 0;
        ui.audition_source = mode == 3 ? TS_AUDITION_PARENT : TS_AUDITION_CURRENT;
        TsSample sample = {0};
        sample.frames = 16384; sample.channels = 2; sample.sample_rate = 44100;
        sample.data = malloc(sample.frames * 2 * sizeof(float)); assert(sample.data);
        for (size_t f = 0; f < sample.frames; ++f) {
            sample.data[2*f] = .3f * sinf((float)f * .05f);
            sample.data[2*f+1] = .2f * sinf((float)f * .07f);
        }
        assert(ts_instrument_import_sample(&instrument, &sample, 0, 0, 0,
                                            TS_LOOP_FORWARD, error, sizeof(error)));
        ts_sample_free(&sample);
        keyboard_loop_policy(&audio, &ui);
        if (mode == 2)
            begin_audition(device, &audio, &ui, &instrument, TS_AUDITION_WORKBENCH_LOOP, 1, 44100);
        else { click_note(0, 0); click_note(4, 0); }
        assert(mode == 2 ? audio.playing : voices() == 2);
        assert(power_audio_energy() > .01);
        control_render_callbacks = 0; probe_control_renders = 1;
        for (int macro = TS_MATERIAL_MACRO_BODY; macro <= TS_MATERIAL_MACRO_DRIFT; ++macro) {
            assert(begin_material_macro_gesture(device, &audio, &ui, &instrument, macro, 0));
            assert(preview_material_macro_gesture(device, &audio, &ui, &instrument, .2f, 44100));
            end_material_macro_gesture(device, &audio, &ui, &instrument,
                                       macro == TS_MATERIAL_MACRO_EDGE);
        }
        assert(begin_amplitude_draw(device, &audio, &ui, &instrument, TS_WAVE_X+80, TS_WAVE_Y+30));
        assert(preview_amplitude_draw(device, &audio, &ui, &instrument, TS_WAVE_X+180, TS_WAVE_Y+40));
        end_amplitude_draw(device, &audio, &ui, &instrument, 0);
        /* The dummy video driver cannot capture the mouse; start the real
           gesture directly, then use the actual preview/cancel controllers. */
        size_t old_frames = instrument.current.frames;
        assert(ts_instrument_canvas_gesture_begin(&instrument, &ui.canvas_gesture, 1, error, sizeof(error)));
        ui.canvas_capture_raw_x = 100; ui.canvas_capture_raw_y = 100;
        ui.canvas_drag_logical_x = 0; ui.canvas_drag_start_frames = old_frames;
        preview_canvas_capture(window, device, &audio, &ui, &instrument, 90);
        end_canvas_gesture(window, device, &audio, &ui, &instrument, 1);
        assert(instrument.current.frames == old_frames);
        probe_control_renders = 0;
        assert(control_render_callbacks == 12);
        fprintf(stderr, "Control renders mode=%d notes=%d groups=%d sister=%d audition=%d\n", mode,
            ts_note_bank_count(&audio.notes), ts_performance_count(&audio.performance),
            ts_performance_count(&audio.sister.performance), audio.playing);
        assert(mode == 2 ? audio.playing : voices() == 2);
        ts_instrument_set_selection(&instrument, 2048, 8192);
        assert(begin_stretch_gesture(device, &audio, &ui, &instrument));
        stretch_waveform(device, &audio, &ui, &instrument, 1, 0);
        end_stretch_gesture(device, &audio, &ui, &instrument, 0);
        apply_material_pitch_shift(device, &audio, &ui, &instrument, .5f);
        apply_sample_edit(device, &audio, &ui, &instrument, TS_SAMPLE_EDIT_REVERSE, 1);
        assert(power_audio_energy() > .01);
        assert(mode == 2 ? audio.playing : voices() == 2);
        if (mode == 2) {
            TsStereoFrame previous = audio.audition_edit_handoff.last;
            assert(fabsf(previous.l) + fabsf(previous.r) > .0001f);
            TsNativeSampleEdit edit;
            begin_native_sample_edit(device, &audio, &instrument, &edit);
            assert(edit.unlocked);
            assert(ts_instrument_apply_sample_edit(&instrument, TS_SAMPLE_EDIT_GAIN,
                                                    .1f, error, sizeof(error)));
            finish_native_sample_edit(device, &audio, &ui, &instrument, &edit);
            float frame[2]; audio_callback(&audio, (Uint8 *)frame, sizeof(frame));
            assert(fabsf(audio.audition_edit_handoff.last.l - previous.l) < 1e-5f);
            assert(fabsf(audio.audition_edit_handoff.last.r - previous.r) < 1e-5f);
        }
        stop_all_force(device, &audio, &ui);
    }
    ui.keyboard_hold = ui.workbench_loop_active = 0;
    ts_instrument_free(&instrument);
    ts_instrument_init(&instrument);
    assert(ts_instrument_create_basic(&instrument, TS_FM_WAVE_SINE, error, sizeof(error)));
    assert(ts_sister_runtime_enable(&audio.sister, 44100, 2, 2, 1.0, error, sizeof(error)));
}
