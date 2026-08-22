#include "tapesister/midi_input.h"
#include "tapesister/note_bank.h"
#include "tapesister/performance.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK FAILED: %s:%d: %s\n", \
                __FILE__, __LINE__, #condition); \
        return 1; \
    } \
} while (0)

static float sample_data[] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};

static void prepare_instrument(TsInstrument *instrument)
{
    ts_instrument_init(instrument);
    for (int slot = 0; slot < 2; ++slot) {
        TsBankSlot *bank = &instrument->bank[slot];
        bank->occupied = 1;
        bank->sample.data = sample_data;
        bank->sample.frames = sizeof(sample_data) / sizeof(sample_data[0]);
        bank->sample.sample_rate = 48000u;
        bank->has_loop = 1;
        bank->loop_first = 0u;
        bank->loop_last = bank->sample.frames;
        bank->loop_mode = TS_LOOP_FORWARD;
        bank->audible_tuning.root_note = 60;
    }
    instrument->current = instrument->bank[0].sample;
    instrument->has_loop = 1;
    instrument->loop_first = 0u;
    instrument->loop_last = instrument->current.frames;
    instrument->loop_mode = TS_LOOP_FORWARD;
    instrument->tuning.root_note = 60;
}

int main(void)
{
    TsMidiEvent midi;
    TsNoteEvent qwerty;
    TsNoteEvent c4_channel_1;
    TsNoteEvent c4_channel_2;
    TsInstrument instrument;
    TsNoteBank notes;
    TsPerformanceBank performance;
    const TsTuning unity = {60, 0.0f};
    float raw = 0.0f;

    CHECK(ts_note_event_qwerty(&qwerty, 0, 60));
    CHECK(qwerty.origin == TS_NOTE_ORIGIN_QWERTY && qwerty.midi_note == 60 &&
          qwerty.velocity == 127 && qwerty.channel == -1);
    CHECK(ts_midi_decode_short_message(0x90u, 60u, 96u, &midi));
    CHECK(midi.action == TS_MIDI_ACTION_NOTE_ON && midi.note.midi_note == 60 &&
          midi.note.velocity == 96 && midi.note.channel == 0);
    c4_channel_1 = midi.note;
    CHECK(ts_midi_decode_short_message(0x91u, 60u, 127u, &midi));
    c4_channel_2 = midi.note;
    CHECK(ts_midi_decode_short_message(0x90u, 60u, 0u, &midi));
    CHECK(midi.action == TS_MIDI_ACTION_NOTE_OFF && midi.note.midi_note == 60);
    CHECK(ts_midi_decode_short_message(0x80u, 61u, 64u, &midi));
    CHECK(midi.action == TS_MIDI_ACTION_NOTE_OFF && midi.note.midi_note == 61);
    CHECK(ts_midi_decode_short_message(0xb3u, 123u, 0u, &midi));
    CHECK(midi.action == TS_MIDI_ACTION_PANIC && midi.channel == 3);
    CHECK(!ts_midi_decode_short_message(0xb0u, 1u, 64u, &midi));
    CHECK(!ts_midi_decode_short_message(0xf8u, 0u, 0u, &midi));
    CHECK(fabsf(ts_note_event_gain(&c4_channel_1) - 96.0f / 127.0f) < 0.0001f);

    prepare_instrument(&instrument);
    ts_note_bank_init(&notes);
    CHECK(ts_note_bank_start_tuned_event(
              &notes, &instrument, &unity, TS_AUDITION_CURRENT,
              &qwerty, 0, 48000) == TS_NOTE_STARTED);
    CHECK(ts_note_bank_start_tuned_event(
              &notes, &instrument, &unity, TS_AUDITION_CURRENT,
              &c4_channel_1, 0, 48000) == TS_NOTE_STARTED);
    CHECK(ts_note_bank_start_tuned_event(
              &notes, &instrument, &unity, TS_AUDITION_CURRENT,
              &c4_channel_2, 0, 48000) == TS_NOTE_STARTED);
    CHECK(ts_note_bank_count(&notes) == 3);
    CHECK(fabs(notes.voices[0].step - 1.0) < 0.000001);
    ts_note_bank_release_event(&notes, &c4_channel_1);
    CHECK(ts_note_bank_count(&notes) == 2);
    ts_note_bank_release_midi_channel(&notes, 1);
    CHECK(ts_note_bank_count(&notes) == 1);
    ts_note_bank_release_event(&notes, &qwerty);
    CHECK(ts_note_bank_count(&notes) == 0);

    ts_performance_init(&performance);
    CHECK(ts_performance_trigger_group_event(
              &performance, &instrument, 0x0003u, &c4_channel_1,
              0, 48000) == 2);
    CHECK(ts_performance_count(&performance) == 2);
    CHECK(fabsf(performance.voices[0].gain - 96.0f / 127.0f) < 0.0001f);
    (void)ts_performance_read(&performance, &raw);
    CHECK(fabsf(raw - 2.0f * 0.5f * 96.0f / 127.0f) < 0.0001f);
    ts_performance_release_event(&performance, &c4_channel_1);
    CHECK(ts_performance_count(&performance) == 0);

    /* The no-backend build remains a valid nonfatal runtime configuration. */
    {
        TsMidiInput *input = ts_midi_input_create();
        char error[160];
        CHECK(input != NULL);
        CHECK(ts_midi_input_port_count(input) == 0);
        CHECK(!ts_midi_input_is_active(input));
#ifdef TAPESISTER_HAS_MIDI
        CHECK(ts_midi_input_rescan(input, error, sizeof(error)));
        CHECK(ts_midi_input_configure(input, "", 0, error, sizeof(error)));
        CHECK(!ts_midi_input_configure(input, "Missing MIDI Device", 1,
                                       error, sizeof(error)));
#else
        CHECK(!ts_midi_input_rescan(input, error, sizeof(error)));
        CHECK(!ts_midi_input_configure(input, "", 0,
                                       error, sizeof(error)));
#endif
        CHECK(!ts_midi_input_configure(input, "", 17,
                                       error, sizeof(error)));
        ts_midi_input_destroy(input);
    }

    /* Samples above are borrowed test storage and must not be freed. */
    memset(&instrument, 0, sizeof(instrument));
    puts("MIDI event, identity, velocity, and performance tests passed");
    return 0;
}
