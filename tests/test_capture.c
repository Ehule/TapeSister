#include "tapesister/capture.h"
#include "tapesister/note_bank.h"
#include "tapesister/sample.h"
#include "tapesister/ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

static int prepare_source_and_blank(TsInstrument *instrument,
                                    size_t source_frames,
                                    size_t target_frames,
                                    char *error, size_t error_size)
{
    ts_instrument_init(instrument);
    if (!ts_instrument_activate_silence(instrument, source_frames, 48000,
                                        error, error_size)) return 0;
    for (size_t frame = 0; frame < source_frames; ++frame)
        instrument->current.data[frame] = frame < source_frames / 2u ? 0.125f : 0.75f;
    snprintf(instrument->current.name, sizeof(instrument->current.name), "CAPTURE SOURCE");
    if (!ts_instrument_select_bank(instrument, 1, error, error_size) ||
        !ts_instrument_activate_silence(instrument, target_frames, 48000,
                                        error, error_size) ||
        !ts_instrument_select_bank(instrument, 0, error, error_size)) return 0;
    return 1;
}

static void test_recorder_boundaries_and_states(void)
{
    TsCaptureRecorder recorder;
    char error[160];
    const float samples[] = {0.9f, -0.4f, 0.25f, 0.1f};
    ts_capture_init(&recorder);
    CHECK(ts_capture_arm(&recorder, 3, 4, 48000, error, sizeof(error)));
    CHECK(recorder.state == TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER);
    CHECK(strcmp(ts_capture_state_name(recorder.state), "ARMED") == 0);
    CHECK(!ts_capture_set_source(&recorder, 3, error, sizeof(error)));
    CHECK(ts_capture_set_source(&recorder, 1, error, sizeof(error)));
    CHECK(ts_capture_toggle_staged_note(&recorder, 0, error, sizeof(error)));
    CHECK(ts_capture_toggle_staged_note(&recorder, 4, error, sizeof(error)));
    CHECK(recorder.destination_slot == 3 && recorder.source_slot == 1);
    CHECK(recorder.staged_notes == ((1u << 0) | (1u << 4)));
    CHECK(ts_capture_trigger(&recorder, error, sizeof(error)));
    CHECK(recorder.state == TS_CAPTURE_RECORDING && recorder.staged_notes == 0u);
    CHECK(ts_capture_set_source(&recorder, 2, error, sizeof(error)));
    CHECK(recorder.source_slot == 2 && recorder.provenance_slot == 2);
    CHECK(!ts_capture_set_source(&recorder, 3, error, sizeof(error)));
    for (int frame = 0; frame < 3; ++frame) {
        CHECK(!ts_capture_write_sample(&recorder, samples[frame]));
        CHECK(recorder.recorded_frames == (size_t)frame + 1u);
    }
    CHECK(ts_capture_progress(&recorder) > 0.74f &&
          ts_capture_progress(&recorder) < 0.76f);
    CHECK(ts_capture_write_sample(&recorder, samples[3]));
    CHECK(recorder.state == TS_CAPTURE_COMPLETED);
    CHECK(recorder.recorded_frames == 4u);
    CHECK(recorder.buffer[0] == samples[0]);
    CHECK(recorder.buffer[3] == samples[3]);
    CHECK(!ts_capture_write_sample(&recorder, 1.0f));
    CHECK(recorder.recorded_frames == 4u);
    ts_capture_free(&recorder);

    ts_capture_init(&recorder);
    CHECK(ts_capture_arm(&recorder, 2, 8, 48000, error, sizeof(error)));
    CHECK(ts_capture_set_source(&recorder, 0, error, sizeof(error)));
    CHECK(ts_capture_trigger(&recorder, error, sizeof(error)));
    CHECK(!ts_capture_write_sample(&recorder, 0.5f));
    CHECK(ts_capture_stop(&recorder, error, sizeof(error)));
    CHECK(recorder.state == TS_CAPTURE_COMPLETED && recorder.stopped_early);
    ts_capture_free(&recorder);

    ts_capture_init(&recorder);
    CHECK(ts_capture_arm(&recorder, 2, 8, 48000, error, sizeof(error)));
    CHECK(ts_capture_cancel(&recorder));
    CHECK(recorder.state == TS_CAPTURE_CANCELED);
    ts_capture_free(&recorder);

    {
        float base[4] = {0.1f, 0.2f, 0.3f, 0.4f};
        ts_capture_init(&recorder);
        CHECK(ts_capture_arm_overdub(&recorder, 2, 8, 48000,
                                     base, 4, 48000,
                                     error, sizeof(error)));
        base[0] = -0.9f;
        CHECK(recorder.overdub && recorder.overdub_base_frames == 4u);
        CHECK(fabsf(recorder.overdub_base[0] - 0.1f) < 0.0001f);
        CHECK(ts_capture_set_source(&recorder, 2, error, sizeof(error)));
        CHECK(ts_capture_trigger(&recorder, error, sizeof(error)));
        ts_capture_free(&recorder);
    }
}

static int prepare_overdub_target(TsInstrument *instrument, size_t frames,
                                  float value, char *error, size_t error_size)
{
    float *base;
    int ok;
    if (!prepare_source_and_blank(instrument, 32u, frames,
                                  error, error_size)) return 0;
    base = (float *)malloc(frames * sizeof(*base));
    if (base == NULL) return 0;
    for (size_t frame = 0; frame < frames; ++frame) base[frame] = value;
    ok = ts_instrument_commit_capture(instrument, 1, 0, base, frames, 48000,
                                      0, 0, error, error_size);
    free(base);
    if (!ok) return 0;
    snprintf(instrument->current.name, sizeof(instrument->current.name),
             "VOCAL BED");
    instrument->tuning.root_note = 52;
    instrument->tuning.fine_tune_cents = 11.0f;
    instrument->audible_tuning = instrument->tuning;
    instrument->has_loop = 1;
    instrument->loop_first = 1u;
    instrument->loop_last = frames - 1u;
    return ts_instrument_select_bank(instrument, 0, error, error_size) &&
           ts_instrument_select_bank(instrument, 1, error, error_size);
}

static void test_overdub_mix_growth_and_atomic_history(void)
{
    TsInstrument instrument;
    char error[160];
    float base[8];
    float quiet[8];
    float loud[8];
    float longer[16];
    uint64_t before_hash;
    uint64_t overdub_hash;
    int undo_before;

    CHECK(prepare_overdub_target(&instrument, 8u, 0.2f,
                                 error, sizeof(error)));
    memcpy(base, instrument.current.data, sizeof(base));
    for (size_t frame = 0; frame < 8u; ++frame) quiet[frame] = 0.3f;
    before_hash = ts_sample_hash(&instrument.current);
    undo_before = instrument.undo_count;
    CHECK(ts_instrument_commit_overdub(&instrument, 1, 1,
                                       base, 8u, 48000,
                                       quiet, 8u, 48000, 0,
                                       error, sizeof(error)));
    CHECK(instrument.undo_count == undo_before + 1);
    CHECK(instrument.current.frames == 8u);
    CHECK(fabsf(instrument.current.data[0] - 0.5f) < 0.0001f);
    CHECK(instrument.tuning.root_note == 52);
    CHECK(fabsf(instrument.tuning.fine_tune_cents - 11.0f) < 0.0001f);
    CHECK(instrument.has_loop && instrument.loop_first == 1u &&
          instrument.loop_last == 7u);
    overdub_hash = ts_sample_hash(&instrument.current);
    CHECK(ts_instrument_undo(&instrument, error, sizeof(error)));
    CHECK(ts_sample_hash(&instrument.current) == before_hash);
    CHECK(fabsf(instrument.current.data[0] - 0.2f) < 0.0001f);
    CHECK(ts_instrument_redo(&instrument, error, sizeof(error)));
    CHECK(ts_sample_hash(&instrument.current) == overdub_hash);
    ts_instrument_free(&instrument);

    CHECK(prepare_overdub_target(&instrument, 8u, 0.8f,
                                 error, sizeof(error)));
    memcpy(base, instrument.current.data, sizeof(base));
    for (size_t frame = 0; frame < 8u; ++frame)
        loud[frame] = (frame & 1u) == 0u ? 0.8f : 0.0f;
    CHECK(ts_instrument_commit_overdub(&instrument, 1, 0,
                                       base, 8u, 48000,
                                       loud, 8u, 48000, 0,
                                       error, sizeof(error)));
    CHECK(fabsf(ts_sample_peak(&instrument.current) - 0.98f) < 0.0001f);
    CHECK(fabsf(instrument.current.data[1] - 0.49f) < 0.0001f);
    ts_instrument_free(&instrument);

    CHECK(prepare_overdub_target(&instrument, 8u, 0.1f,
                                 error, sizeof(error)));
    memcpy(base, instrument.current.data, sizeof(base));
    for (size_t frame = 0; frame < 16u; ++frame) longer[frame] = 0.2f;
    CHECK(ts_instrument_commit_overdub(&instrument, 1, 0,
                                       base, 8u, 48000,
                                       longer, 16u, 48000, 1,
                                       error, sizeof(error)));
    CHECK(instrument.current.frames == 16u);
    CHECK(fabsf(instrument.current.data[4] - 0.3f) < 0.0001f);
    CHECK(fabsf(instrument.current.data[12] - 0.2f) < 0.0001f);
    CHECK(ts_instrument_undo(&instrument, error, sizeof(error)));
    CHECK(instrument.current.frames == 8u);
    ts_instrument_free(&instrument);

    CHECK(prepare_overdub_target(&instrument, 8u, 0.2f,
                                 error, sizeof(error)));
    memcpy(base, instrument.current.data, sizeof(base));
    instrument.current.data[0] = 0.21f;
    CHECK(ts_instrument_select_bank(&instrument, 0, error, sizeof(error)));
    CHECK(ts_instrument_select_bank(&instrument, 1, error, sizeof(error)));
    CHECK(!ts_instrument_commit_overdub(&instrument, 1, 0,
                                        base, 8u, 48000,
                                        quiet, 8u, 48000, 0,
                                        error, sizeof(error)));
    CHECK(strstr(error, "changed while armed") != NULL);
    ts_instrument_free(&instrument);
}

static void test_staging_and_live_note_timing(void)
{
    TsInstrument instrument;
    TsNoteBank notes;
    TsCaptureRecorder recorder;
    char error[160];
    CHECK(prepare_source_and_blank(&instrument, 64, 16, error, sizeof(error)));
    for (size_t frame = 0; frame < instrument.current.frames; ++frame)
        instrument.current.data[frame] = 0.5f;
    instrument.has_loop = 1;
    instrument.loop_first = 0;
    instrument.loop_last = instrument.current.frames;
    ts_note_bank_init(&notes);
    CHECK(ts_note_bank_start_staged_chord(
              &notes, &instrument, &instrument.audible_tuning,
              TS_AUDITION_CURRENT, (1u << 0) | (1u << 4) | (1u << 7),
              TS_KEYBOARD_BASE_NOTE, 48000) == 3);
    CHECK(ts_note_bank_count(&notes) == 3);
    {
        double position = -1.0;
        int active = 0;
        for (int voice = 0; voice < TS_NOTE_VOICE_LIMIT; ++voice) {
            if (!notes.voices[voice].active) continue;
            if (active == 0) position = notes.voices[voice].position;
            CHECK(notes.voices[voice].position == position);
            ++active;
        }
        CHECK(active == 3);
    }

    ts_note_bank_clear(&notes);
    /* This timing test predates the configurable onset envelope and exercises
       exact voice-join arithmetic with that envelope explicitly disabled. */
    ts_note_bank_set_attack_ms(&notes, 0);
    ts_capture_init(&recorder);
    CHECK(ts_capture_arm(&recorder, 1, 8, 48000, error, sizeof(error)));
    CHECK(ts_capture_set_source(&recorder, 0, error, sizeof(error)));
    CHECK(ts_capture_trigger(&recorder, error, sizeof(error)));
    CHECK(ts_note_bank_start_tuned_at(
              &notes, &instrument, &instrument.audible_tuning,
              TS_AUDITION_CURRENT, 0, TS_KEYBOARD_BASE_NOTE, 1,
              48000) == TS_NOTE_STARTED);
    for (int frame = 0; frame < 3; ++frame)
        CHECK(!ts_capture_write_sample(&recorder, ts_note_bank_read(&notes)));
    CHECK(ts_note_bank_start_tuned_at(
              &notes, &instrument, &instrument.audible_tuning,
              TS_AUDITION_CURRENT, 4, TS_KEYBOARD_BASE_NOTE, 1,
              48000) == TS_NOTE_STARTED);
    for (int frame = 3; frame < 8; ++frame)
        (void)ts_capture_write_sample(&recorder, ts_note_bank_read(&notes));
    CHECK(fabsf(recorder.buffer[0] - 0.5f) < 0.0001f);
    CHECK(fabsf(recorder.buffer[2] - 0.5f) < 0.0001f);
    CHECK(recorder.buffer[3] > 0.70f);
    CHECK(recorder.state == TS_CAPTURE_COMPLETED);
    ts_capture_free(&recorder);
    ts_instrument_free(&instrument);
}

static void test_live_selection_sync_reaches_capture(void)
{
    TsInstrument instrument;
    TsNoteBank notes;
    TsCaptureRecorder recorder;
    char error[160];
    CHECK(prepare_source_and_blank(&instrument, 32, 6, error, sizeof(error)));
    instrument.has_loop = 1;
    instrument.loop_first = 0;
    instrument.loop_last = 8;
    ts_note_bank_init(&notes);
    ts_note_bank_set_attack_ms(&notes, 0);
    ts_capture_init(&recorder);
    CHECK(ts_capture_arm(&recorder, 1, 4, 48000, error, sizeof(error)));
    CHECK(ts_capture_set_source(&recorder, 0, error, sizeof(error)));
    CHECK(ts_note_bank_start_tuned_at(
              &notes, &instrument, &instrument.audible_tuning,
              TS_AUDITION_CURRENT, 0, TS_KEYBOARD_BASE_NOTE, 1,
              48000) == TS_NOTE_STARTED);
    CHECK(ts_capture_trigger(&recorder, error, sizeof(error)));
    CHECK(!ts_capture_write_sample(&recorder, ts_note_bank_read(&notes)));
    CHECK(!ts_capture_write_sample(&recorder, ts_note_bank_read(&notes)));
    instrument.loop_first = 16;
    instrument.loop_last = 28;
    ts_note_bank_sync(&notes, &instrument, 48000);
    CHECK(!ts_capture_write_sample(&recorder, ts_note_bank_read(&notes)));
    CHECK(ts_capture_write_sample(&recorder, ts_note_bank_read(&notes)));
    CHECK(recorder.buffer[0] < 0.2f && recorder.buffer[1] < 0.2f);
    CHECK(recorder.buffer[2] > 0.7f && recorder.buffer[3] > 0.7f);
    ts_capture_free(&recorder);
    ts_instrument_free(&instrument);
}

static void test_pitch_change_and_multiple_playheads_reach_capture(void)
{
    TsInstrument instrument;
    TsNoteBank notes;
    TsCaptureRecorder recorder;
    TsTuning shifted;
    char error[160];
    double first_step;
    double octave_step;
    CHECK(prepare_source_and_blank(&instrument, 64, 6, error, sizeof(error)));
    for (size_t frame = 0; frame < instrument.current.frames; ++frame)
        instrument.current.data[frame] = (float)frame / 256.0f;
    instrument.has_loop = 1;
    instrument.loop_first = 0u;
    instrument.loop_last = instrument.current.frames;
    ts_note_bank_init(&notes);
    CHECK(ts_note_bank_start_tuned_at(
              &notes, &instrument, &instrument.audible_tuning,
              TS_AUDITION_CURRENT, 0, TS_KEYBOARD_BASE_NOTE, 1,
              48000) == TS_NOTE_STARTED);
    CHECK(ts_note_bank_start_tuned_at(
              &notes, &instrument, &instrument.audible_tuning,
              TS_AUDITION_CURRENT, 12, TS_KEYBOARD_BASE_NOTE, 1,
              48000) == TS_NOTE_STARTED);
    CHECK(ts_note_bank_count(&notes) == 2);
    first_step = notes.voices[0].step;
    octave_step = notes.voices[1].step;
    CHECK(octave_step > first_step * 1.99 && octave_step < first_step * 2.01);

    ts_capture_init(&recorder);
    CHECK(ts_capture_arm(&recorder, 1, 6, 48000, error, sizeof(error)));
    CHECK(ts_capture_set_source(&recorder, 0, error, sizeof(error)));
    CHECK(ts_capture_trigger(&recorder, error, sizeof(error)));
    CHECK(!ts_capture_write_sample(&recorder, ts_note_bank_read(&notes)));
    CHECK(!ts_capture_write_sample(&recorder, ts_note_bank_read(&notes)));
    shifted = instrument.audible_tuning;
    shifted.fine_tune_cents = 100.0f;
    ts_note_bank_sync_tuned(&notes, &instrument, &shifted, 48000);
    CHECK(fabs(notes.voices[0].step - first_step) > 0.0001);
    CHECK(fabs(notes.voices[1].step - octave_step) > 0.0001);
    for (int frame = 2; frame < 6; ++frame)
        (void)ts_capture_write_sample(&recorder, ts_note_bank_read(&notes));
    CHECK(recorder.state == TS_CAPTURE_COMPLETED);
    CHECK(recorder.buffer[0] == 0.0f);
    CHECK(recorder.buffer[1] > recorder.buffer[0]);
    CHECK(recorder.buffer[5] > recorder.buffer[2]);
    ts_capture_free(&recorder);
    ts_instrument_free(&instrument);
}

static void test_target_commit_history_and_independence(void)
{
    TsInstrument instrument;
    TsInstrument restored;
    char error[160];
    float captured[8] = {0.8f, -0.6f, 0.4f, -0.2f, 0.1f, 0.3f, -0.5f, 0.7f};
    uint64_t target_before;
    uint64_t captured_hash;
    size_t capacity = 0;
    CHECK(prepare_source_and_blank(&instrument, 32, 8, error, sizeof(error)));
    CHECK(ts_instrument_set_tuning(&instrument, 45, 17.0f,
                                   error, sizeof(error)));
    target_before = ts_sample_hash(&instrument.bank[1].sample);
    CHECK(ts_instrument_capture_target_frames(&instrument, 1, 48000,
                                               &capacity, error, sizeof(error)));
    CHECK(capacity == 8u);
    CHECK(ts_instrument_capture_target_frames(&instrument, 1, 24000,
                                               &capacity, error, sizeof(error)));
    CHECK(capacity == 4u);
    CHECK(ts_instrument_commit_capture(&instrument, 1, 0, captured, 8, 48000,
                                       0, 0, error, sizeof(error)));
    CHECK(instrument.selected_slot == 1);
    CHECK(instrument.bank[1].capture_kind == TS_BANK_CAPTURE_PERFORMANCE);
    CHECK(instrument.bank[1].parent_slot == 0);
    CHECK(instrument.tuning.root_note == TS_KEYBOARD_BASE_NOTE &&
          instrument.audible_tuning.root_note == TS_KEYBOARD_BASE_NOTE &&
          instrument.tuning.fine_tune_cents == 0.0f);
    CHECK(instrument.current.frames == 8u && instrument.undo_count == 1);
    CHECK(fabsf(instrument.current.data[0] - 0.8f) < 0.0001f);
    captured_hash = ts_sample_hash(&instrument.current);
    captured[0] = -1.0f;
    instrument.bank[0].sample.data[0] = -0.9f;
    CHECK(fabsf(instrument.current.data[0] - 0.8f) < 0.0001f);
    CHECK(ts_instrument_undo(&instrument, error, sizeof(error)));
    CHECK(ts_sample_peak(&instrument.current) == 0.0f);
    CHECK(ts_sample_hash(&instrument.current) == target_before);
    CHECK(ts_instrument_redo(&instrument, error, sizeof(error)));
    CHECK(ts_sample_hash(&instrument.current) == captured_hash);
    CHECK(ts_instrument_save_recipe(&instrument, "test-capture.tsr",
                                    error, sizeof(error)));
    ts_instrument_init(&restored);
    CHECK(ts_instrument_load_recipe(&restored, "test-capture.tsr",
                                    error, sizeof(error)));
    CHECK(restored.bank[1].capture_kind == TS_BANK_CAPTURE_PERFORMANCE);
    CHECK(ts_sample_hash(&restored.current) == captured_hash);
    remove("test-capture.tsr");
    ts_instrument_free(&restored);
    ts_instrument_free(&instrument);
}

static void test_early_stop_and_cancel_leave_expected_target(void)
{
    TsInstrument instrument;
    TsCaptureRecorder recorder;
    char error[160];
    float captured[4] = {0.2f, 0.4f, -0.3f, 0.1f};
    uint64_t before;
    CHECK(prepare_source_and_blank(&instrument, 16, 12, error, sizeof(error)));
    before = ts_sample_hash(&instrument.bank[1].sample);
    ts_capture_init(&recorder);
    CHECK(ts_capture_arm(&recorder, 1, 12, 48000, error, sizeof(error)));
    CHECK(ts_capture_set_source(&recorder, 0, error, sizeof(error)));
    CHECK(ts_capture_cancel(&recorder));
    CHECK(ts_sample_hash(&instrument.bank[1].sample) == before);
    ts_capture_free(&recorder);
    CHECK(ts_instrument_commit_capture(&instrument, 1, 0, captured, 4, 48000,
                                       1, 0, error, sizeof(error)));
    CHECK(instrument.current.frames == 4u);
    CHECK(instrument.has_loop == 0);
    CHECK(instrument.has_selection && instrument.selection_first == 0u &&
          instrument.selection_last == 4u);
    ts_instrument_free(&instrument);
}

static void test_auto_resize_capture_expands_blank_target(void)
{
    TsInstrument instrument;
    char error[160];
    float captured[16];
    uint64_t expanded_hash;
    CHECK(prepare_source_and_blank(&instrument, 32, 8,
                                   error, sizeof(error)));
    for (size_t frame = 0; frame < 16u; ++frame)
        captured[frame] = (float)frame / 20.0f;
    CHECK(ts_instrument_commit_capture(&instrument, 1, 0,
                                       captured, 16, 48000,
                                       0, 1, error, sizeof(error)));
    CHECK(instrument.current.frames == 16u);
    CHECK(instrument.has_selection && instrument.selection_first == 0u &&
          instrument.selection_last == 16u);
    CHECK(fabsf(instrument.current.data[15] - captured[15]) < 0.0001f);
    expanded_hash = ts_sample_hash(&instrument.current);
    CHECK(ts_instrument_undo(&instrument, error, sizeof(error)));
    CHECK(instrument.current.frames == 8u);
    CHECK(ts_sample_peak(&instrument.current) == 0.0f);
    CHECK(ts_instrument_redo(&instrument, error, sizeof(error)));
    CHECK(instrument.current.frames == 16u);
    CHECK(ts_sample_hash(&instrument.current) == expanded_hash);
    ts_instrument_free(&instrument);
}

static void test_internal_synth_source_and_split_mix(void)
{
    TsInstrument instrument;
    TsCaptureRecorder recorder;
    TsNoteBank notes;
    TsSample preview;
    TsSample replacement;
    char error[160];
    float captured[8] = {0.6f, -0.4f, 0.3f, -0.2f, 0.1f, 0.2f, -0.1f, 0.4f};
    float synth = 0.0f;
    CHECK(prepare_source_and_blank(&instrument, 32, 8, error, sizeof(error)));
    ts_capture_init(&recorder);
    CHECK(ts_capture_arm(&recorder, 1, 8, 48000, error, sizeof(error)));
    CHECK(ts_capture_set_source(&recorder, TS_CAPTURE_SOURCE_SYNTH,
                                error, sizeof(error)));
    CHECK(ts_capture_trigger(&recorder, error, sizeof(error)));
    ts_capture_free(&recorder);
    CHECK(ts_instrument_commit_capture(&instrument, 1, TS_CAPTURE_SOURCE_SYNTH,
                                       captured, 8, 48000, 0, 0,
                                       error, sizeof(error)));
    CHECK(instrument.bank[1].parent_slot == TS_CAPTURE_SOURCE_SYNTH);
    CHECK(strstr(instrument.current.name, "SYNTH") != NULL);
    CHECK(!instrument.bank[1].has_generator);

    ts_sample_init(&preview);
    ts_sample_init(&replacement);
    preview.data = captured;
    preview.frames = 8u;
    preview.sample_rate = 48000u;
    replacement.data = captured;
    replacement.frames = 8u;
    replacement.sample_rate = 48000u;
    ts_note_bank_init(&notes);
    ts_note_bank_set_attack_ms(&notes, 0);
    CHECK(ts_note_bank_start_sample(&notes, &preview, &instrument.audible_tuning,
                                    0, TS_KEYBOARD_BASE_NOTE, 1, 48000) ==
          TS_NOTE_STARTED);
    CHECK(ts_note_bank_read_split(&notes, &synth) == synth);
    CHECK(fabsf(synth) > 0.0f);
    ts_note_bank_replace_sample(&notes, &preview, &replacement, 48000);
    CHECK(notes.voices[0].sample == &replacement && notes.voices[0].synth);
    CHECK(ts_note_bank_start_sample(&notes, &replacement,
                                    &instrument.audible_tuning, 0,
                                    TS_KEYBOARD_BASE_NOTE, 1, 48000) ==
          TS_NOTE_TOGGLED_OFF);
    ts_instrument_free(&instrument);
}

static void test_declick_attack_reaches_capture(void)
{
    TsInstrument instrument;
    TsNoteBank notes;
    TsCaptureRecorder recorder;
    char error[160];
    const size_t attack_frames = ts_audition_attack_frames(
        48000, TS_AUDITION_ATTACK_MS_DEFAULT);
    CHECK(attack_frames == 96u);
    CHECK(prepare_source_and_blank(&instrument, 256u, attack_frames,
                                   error, sizeof(error)));
    for (size_t frame = 0u; frame < instrument.current.frames; ++frame)
        instrument.current.data[frame] = 0.5f;
    instrument.has_loop = 1;
    instrument.loop_first = 0u;
    instrument.loop_last = instrument.current.frames;
    ts_note_bank_init(&notes);
    ts_capture_init(&recorder);
    CHECK(ts_capture_arm(&recorder, 1, attack_frames, 48000,
                         error, sizeof(error)));
    CHECK(ts_capture_set_source(&recorder, 0, error, sizeof(error)));
    CHECK(ts_note_bank_start_tuned_at(
              &notes, &instrument, &instrument.audible_tuning,
              TS_AUDITION_CURRENT, 0, TS_KEYBOARD_BASE_NOTE, 1,
              48000) == TS_NOTE_STARTED);
    CHECK(ts_capture_trigger(&recorder, error, sizeof(error)));
    for (size_t frame = 0u; frame < attack_frames; ++frame)
        (void)ts_capture_write_sample(&recorder, ts_note_bank_read(&notes));
    CHECK(recorder.state == TS_CAPTURE_COMPLETED);
    CHECK(recorder.buffer[0] == 0.0f);
    CHECK(recorder.buffer[attack_frames / 2u] > 0.2f &&
          recorder.buffer[attack_frames / 2u] < 0.3f);
    CHECK(fabsf(recorder.buffer[attack_frames - 1u] - 0.5f) < 0.0001f);
    ts_capture_free(&recorder);
    ts_instrument_free(&instrument);
}

static void test_capture_feedback_rendering(void)
{
    TsInstrument instrument;
    TsUiState ui;
    TsFramebuffer idle;
    TsFramebuffer recording;
    TsFramebuffer staged;
    TsFramebuffer bank_idle;
    TsFramebuffer bank_armed;
    char error[160];
    CHECK(prepare_source_and_blank(&instrument, 16, 12, error, sizeof(error)));
    ts_ui_init(&ui);
    CHECK(ts_ui_capture_button_from_point(540, 315));
    CHECK(!ts_ui_capture_button_from_point(531, 315));
    CHECK(ts_ui_overdub_button_from_point(390, 315));
    CHECK(ts_ui_record_keep_button_from_point(410, 315));
    CHECK(ts_ui_monitor_button_from_point(470, 315));
    ts_ui_render(&idle, &ui, &instrument);
    ui.capture_state = TS_CAPTURE_RECORDING;
    ui.capture_recorded_frames = 6;
    ui.capture_capacity_frames = 12;
    ui.text_cursor_visible = 1;
    snprintf(ui.overlay, sizeof(ui.overlay), "CAPTURE STARTED");
    ts_ui_render(&recording, &ui, &instrument);
    CHECK(recording.pixels[0] != idle.pixels[0]);
    CHECK(recording.pixels[32 * TS_UI_WIDTH + TS_UI_WIDTH / 4] !=
          idle.pixels[32 * TS_UI_WIDTH + TS_UI_WIDTH / 4]);

    ui.capture_state = TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER;
    ui.capture_recorded_frames = 0u;
    ui.staged_notes = 1u;
    ui.overlay[0] = '\0';
    ts_ui_render(&staged, &ui, &instrument);
    CHECK(staged.pixels[340 * TS_UI_WIDTH + 30] !=
          idle.pixels[340 * TS_UI_WIDTH + 30]);

    ui.show_keyboard = 0;
    ui.capture_state = TS_CAPTURE_IDLE;
    ui.capture_destination_slot = -1;
    ui.capture_source_slot = -1;
    ui.staged_notes = 0u;
    ts_ui_render(&bank_idle, &ui, &instrument);
    ui.capture_state = TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER;
    ui.capture_destination_slot = 1;
    ui.capture_source_slot = 0;
    ts_ui_render(&bank_armed, &ui, &instrument);
    CHECK(bank_armed.pixels[328 * TS_UI_WIDTH + 90] !=
          bank_idle.pixels[328 * TS_UI_WIDTH + 90]);
    CHECK(bank_armed.pixels[338 * TS_UI_WIDTH + 15] !=
          bank_idle.pixels[338 * TS_UI_WIDTH + 15]);

    ui.overdub_confirm_open = 1;
    ui.overdub_confirm_slot = 1;
    ts_ui_render(&recording, &ui, &instrument);
    ui.overdub_confirm_open = 0;
    ts_ui_render(&staged, &ui, &instrument);
    CHECK(recording.pixels[122 * TS_UI_WIDTH + 146] !=
          staged.pixels[122 * TS_UI_WIDTH + 146]);

    ui.file_busy = 1;
    ui.file_busy_phase = 2;
    snprintf(ui.file_busy_label, sizeof(ui.file_busy_label), "SAVING");
    ts_ui_render(&recording, &ui, &instrument);
    ui.file_busy = 0;
    ts_ui_render(&staged, &ui, &instrument);
    CHECK(recording.pixels[142 * TS_UI_WIDTH + 154] !=
          staged.pixels[142 * TS_UI_WIDTH + 154]);

    ui.external_record_bank = 1;
    ui.input_meter_active = 1;
    ui.input_sample_rate = 48000u;
    ui.input_level = 0.001f;
    ui.input_peak = 0.01f;
    ui.input_threshold = powf(10.0f, -30.0f / 20.0f);
    ts_ui_render(&recording, &ui, &instrument);
    ui.input_threshold = powf(10.0f, -60.0f / 20.0f);
    ts_ui_render(&staged, &ui, &instrument);
    CHECK(recording.pixels[110 * TS_UI_WIDTH + 602] !=
          staged.pixels[110 * TS_UI_WIDTH + 602]);

    ui.capture_state = TS_CAPTURE_RECORDING;
    ui.capture_recorded_frames = 24000u;
    ui.input_wave_columns = 2u;
    ui.input_wave_minimum[0] = -0.8f;
    ui.input_wave_maximum[0] = 0.7f;
    ui.input_wave_minimum[1] = -0.2f;
    ui.input_wave_maximum[1] = 0.4f;
    ts_ui_render(&recording, &ui, &instrument);
    CHECK(recording.pixels[(TS_WAVE_Y + TS_WAVE_H / 2) * TS_UI_WIDTH +
                           TS_WAVE_X] !=
          staged.pixels[(TS_WAVE_Y + TS_WAVE_H / 2) * TS_UI_WIDTH +
                        TS_WAVE_X]);
    ts_instrument_free(&instrument);
}

int main(void)
{
    test_recorder_boundaries_and_states();
    test_overdub_mix_growth_and_atomic_history();
    test_staging_and_live_note_timing();
    test_live_selection_sync_reaches_capture();
    test_pitch_change_and_multiple_playheads_reach_capture();
    test_target_commit_history_and_independence();
    test_early_stop_and_cancel_leave_expected_target();
    test_auto_resize_capture_expands_blank_target();
    test_internal_synth_source_and_split_mix();
    test_declick_attack_reaches_capture();
    test_capture_feedback_rendering();
    if (failures != 0) {
        fprintf(stderr, "%d Capture checks failed\n", failures);
        return 1;
    }
    puts("TapeSister Capture recorder, performance timing, history, and tile tests passed");
    return 0;
}
