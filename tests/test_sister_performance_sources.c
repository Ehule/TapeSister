#include "sister_test_helpers.h"
#include "tapesister/audio_mixer.h"

#include <assert.h>
#include <stdio.h>

static float tape_peak(const TsSisterRuntime *runtime)
{
    float peak = 0.0f;
    size_t scalars = runtime->machine.buffer.capacity_frames *
                     runtime->machine.buffer.channels;
    for (size_t i = 0u; i < scalars; ++i)
        if (fabsf(runtime->machine.buffer.data[i]) > peak)
            peak = fabsf(runtime->machine.buffer.data[i]);
    return peak;
}

int main(void)
{
    TsSisterRuntime runtime;
    TsSisterSourceFrames sources = {0};
    TsSisterRuntimeFrame frame;
    TsInstrument empty;
    TsAudioMixer mixer;
    TsAudioBuses buses;
    TsInstrument ensemble;
    TsNoteEvent qwerty;
    TsNoteEvent midi;
    TsInstrument live_edit;
    char error[160];
    ts_instrument_init(&empty);
    assert(sister_test_enable(&runtime, 1000u, 2u, 0.1));
    ts_sister_runtime_set_monitor(&runtime, 0);

    /* Live FM is an independent bus and requires no occupied tile. */
    ts_sister_runtime_set_sources(&runtime, TS_SISTER_SOURCE_FM);
    sources.fm = (TsStereoFrame){0.25f, -0.125f};
    for (int i = 0; i < 8; ++i)
        frame = ts_sister_runtime_process_frame(&runtime, &sources);
    assert(tape_peak(&runtime) > 0.01f);
    assert(frame.monitor_return.l == 0.0f && frame.monitor_return.r == 0.0f);
    sources.fm = sister_silence();
    frame = ts_sister_runtime_process_frame(&runtime, &sources);
    assert(frame.input.l == 0.0f && frame.input.r == 0.0f);

    assert(ts_sister_machine_clear_offline(&runtime.machine));
    ts_sister_runtime_set_sources(&runtime, TS_SISTER_SOURCE_EXT);
    ts_sister_runtime_input_available(&runtime, 1);
    sources.external = (TsStereoFrame){0.2f, 0.4f};
    for (int i = 0; i < 8; ++i)
        frame = ts_sister_runtime_process_frame(&runtime, &sources);
    assert(tape_peak(&runtime) > 0.01f);
    assert(frame.monitor_return.l == 0.0f && frame.monitor_return.r == 0.0f);

    /* Insert routing removes the selected direct bus and returns it once. */
    ts_audio_mixer_init(&mixer);
    ts_audio_buses_clear(&buses);
    buses.fm = (TsStereoFrame){0.3f, 0.3f};
    buses.sister = (TsStereoFrame){0.2f, 0.2f};
    ts_audio_buses_apply_source_dry(&buses, 0.0f, 0, 0, 1, 0);
    frame.monitor_return = ts_audio_mixer_render(&mixer, &buses);
    assert(sister_close(frame.monitor_return.l, 0.2f, 0.0001f));

    /* A wholly empty bank cannot accidentally become a tile prerequisite. */
    assert(ts_sister_runtime_set_page(&runtime, 0u, &empty));
    assert(ts_sister_runtime_source_mask(&runtime) == 0u);
    assert(!ts_sister_runtime_arm_capture(
        &runtime, &empty, 0, 8u, 1000u, 1u,
        TS_SISTER_TAP_MIX, 0u, error, sizeof(error)));

    /* QWERTY and MIDI admit the same complete 1/2/3/16-tile ensembles. */
    assert(sister_test_make_tiles(&ensemble, 16, 0, 1000u, 32u));
    assert(ts_sister_runtime_set_page(&runtime, 2u, &ensemble));
    ts_sister_runtime_set_sources(&runtime, TS_SISTER_SOURCE_TILES);
    assert(ts_note_event_qwerty(&qwerty, 0, TS_KEYBOARD_BASE_NOTE));
    assert(ts_note_event_midi(&midi, 60, 100, 3));

    /* Shift-click latch toggles remain visible while sounding and clear on
       the second press even though Sister, not the ordinary note bank, owns
       the voices. */
    ts_sister_runtime_clear_source_mask(&runtime);
    assert(ts_sister_runtime_set_source_slot(&runtime, &ensemble, 0, 1));
    assert(ts_sister_runtime_note_on(
               &runtime, &ensemble, &qwerty, 1, 1000) == 1);
    assert(ts_performance_visible_mask(
               &runtime.performance, TS_KEYBOARD_BASE_NOTE) == 1u);
    assert(ts_sister_runtime_note_on(
               &runtime, &ensemble, &qwerty, 1, 1000) == 1);
    assert(ts_performance_count(&runtime.performance) == 0);
    assert(ts_performance_visible_mask(
               &runtime.performance, TS_KEYBOARD_BASE_NOTE) == 0u);

    for (int members = 1; members <= 3; ++members) {
        ts_sister_runtime_clear_source_mask(&runtime);
        for (int slot = 0; slot < members; ++slot)
            assert(ts_sister_runtime_set_source_slot(
                &runtime, &ensemble, slot, 1));
        assert(ts_sister_runtime_note_on(
                   &runtime, &ensemble, &qwerty, 0, 1000) == members);
        assert(ts_performance_visible_mask(
                   &runtime.performance, TS_KEYBOARD_BASE_NOTE) == 1u);
        assert(ts_performance_source_display_voice(
                   &runtime.performance, 0) != NULL);
        ts_sister_runtime_panic(&runtime);
        assert(ts_sister_runtime_note_on(
                   &runtime, &ensemble, &midi, 0, 1000) == members);
        ts_sister_runtime_panic(&runtime);
    }
    ts_sister_runtime_clear_source_mask(&runtime);
    for (int slot = 0; slot < TS_BANK_SLOT_COUNT; ++slot)
        assert(ts_sister_runtime_set_source_slot(
            &runtime, &ensemble, slot, 1));
    assert(ts_sister_runtime_note_on(
               &runtime, &ensemble, &qwerty, 0, 1000) == TS_BANK_SLOT_COUNT);
    ts_sister_runtime_panic(&runtime);
    assert(ts_sister_runtime_note_on(
               &runtime, &ensemble, &midi, 0, 1000) == TS_BANK_SLOT_COUNT);
    ts_sister_runtime_panic(&runtime);
    ts_instrument_free(&ensemble);

    /* A real destructive edit publishes a new generation without retargeting
       the sounding one-shot. Undo/redo use the same safe replacement seam. */
    assert(sister_test_make_tiles(&live_edit, 1, 1, 1000u, 128u));
    for (size_t frame_index = 0; frame_index < live_edit.current.frames;
        ++frame_index)
        live_edit.parent.data[frame_index] = live_edit.current.data[frame_index] =
            0.6f * sinf((float)frame_index * 0.071f);
    ts_sample_touch(&live_edit.current);
    ts_sample_touch(&live_edit.parent);
    assert(ts_instrument_select_bank(&live_edit, 1, error, sizeof(error)));
    assert(ts_instrument_select_bank(&live_edit, 0, error, sizeof(error)));
    assert(ts_sister_runtime_set_page(&runtime, 3u, &live_edit));
    assert(ts_sister_runtime_set_source_slot(&runtime, &live_edit, 0, 1));
    assert(ts_sister_runtime_note_on(
               &runtime, &live_edit, &qwerty, 0, 1000) == 1);
    {
        TsPerformanceGeneration *old = runtime.performance.voices[0].generation;
        uint64_t before = ts_sample_hash(&live_edit.current);
        assert(ts_instrument_apply_warp(&live_edit, 0.8f,
                                        error, sizeof(error)));
        assert(ts_sample_hash(&live_edit.current) != before);
        ts_sister_runtime_sync_sources(&runtime, &live_edit, 1000);
        assert(runtime.performance.voices[0].generation == old);
        ts_sister_runtime_panic(&runtime);
        assert(ts_sister_runtime_note_on(
                   &runtime, &live_edit, &qwerty, 0, 1000) == 1);
        assert(runtime.performance.voices[0].generation != old);
        old = runtime.performance.voices[0].generation;
        assert(ts_instrument_undo(&live_edit, error, sizeof(error)));
        ts_sister_runtime_sync_sources(&runtime, &live_edit, 1000);
        assert(runtime.performance.voices[0].generation == old);
        ts_sister_runtime_panic(&runtime);
        assert(ts_instrument_redo(&live_edit, error, sizeof(error)));
        ts_sister_runtime_sync_sources(&runtime, &live_edit, 1000);
    }
    ts_sister_runtime_panic(&runtime);
    ts_instrument_free(&live_edit);
    ts_sister_runtime_free(&runtime);
    ts_instrument_free(&empty);
    puts("sister performance-source tests passed");
    return 0;
}
