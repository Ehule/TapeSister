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
