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
    CHECK(ts_midi_decode_short_message(0xb0u, 1u, 64u, &midi));
    CHECK(midi.action == TS_MIDI_ACTION_CONTROL &&
          midi.source_kind == TS_MIDI_SOURCE_CC && midi.channel == 0 &&
          midi.number == 1 && midi.value == 64 && midi.maximum == 127);
    CHECK(ts_midi_decode_short_message(0xefu, 0x7fu, 0x7fu, &midi));
    CHECK(midi.action == TS_MIDI_ACTION_CONTROL &&
          midi.source_kind == TS_MIDI_SOURCE_PITCH_BEND &&
          midi.channel == 15 && midi.value == 16383 &&
          midi.maximum == 16383);
    CHECK(!ts_midi_decode_short_message(0xf8u, 0u, 0u, &midi));

    {
        TsMidiMap map;
        TsMidiSource source = {TS_MIDI_SOURCE_PITCH_BEND, 7, 0};
        TsMidiSource parsed;
        char formatted[40];
        ts_midi_map_init(&map);
        CHECK(map.takeover == TS_MIDI_TAKEOVER_PICKUP);
        CHECK(ts_midi_map_assign(&map, "sister.param.delay_time", source));
        CHECK(map.count == 1u);
        CHECK(ts_midi_map_find_source(&map, source) != NULL);
        CHECK(ts_midi_map_find_target_const(
                  &map, "sister.param.delay_time") != NULL);
        CHECK(ts_midi_source_format(source, formatted, sizeof(formatted)));
        CHECK(strcmp(formatted, "pitchbend,8") == 0);
        CHECK(ts_midi_source_parse(formatted, &parsed));
        CHECK(ts_midi_source_equal(source, parsed));
        CHECK(ts_midi_map_remove_target(&map, "sister.param.delay_time"));
        CHECK(map.count == 0u);
    }
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

    /* The shared sample-note path begins at digital silence, reaches unity at
       the configured duration, and can be disabled without editing audio. */
    ts_note_bank_set_attack_ms(&notes, TS_AUDITION_ATTACK_MS_DEFAULT);
    CHECK(ts_note_bank_start_tuned_event(
              &notes, &instrument, &unity, TS_AUDITION_CURRENT,
              &c4_channel_1, 0, 48000) == TS_NOTE_STARTED);
    CHECK(ts_note_bank_read(&notes) == 0.0f);
    {
        size_t attack_frames = ts_audition_attack_frames(
            48000, TS_AUDITION_ATTACK_MS_DEFAULT);
        float heard = 0.0f;
        CHECK(attack_frames == 96u);
        for (size_t frame = 1u; frame < attack_frames; ++frame)
            heard = ts_note_bank_read(&notes);
        CHECK(fabsf(heard - 0.5f * 96.0f / 127.0f) < 0.0001f);
    }
    ts_note_bank_release_event(&notes, &c4_channel_1);
    CHECK(ts_note_bank_count(&notes) == 0);
    ts_note_bank_set_attack_ms(&notes, 0);
    CHECK(ts_note_bank_start_tuned_event(
              &notes, &instrument, &unity, TS_AUDITION_CURRENT,
              &c4_channel_1, 0, 48000) == TS_NOTE_STARTED);
    CHECK(fabsf(ts_note_bank_read(&notes) - 0.5f * 96.0f / 127.0f) < 0.0001f);
    ts_note_bank_release_event(&notes, &c4_channel_1);
    ts_note_bank_clear(&notes);
    CHECK(notes.attack_ms == 0);
    ts_note_bank_set_attack_ms(&notes, TS_AUDITION_ATTACK_MS_DEFAULT);

    /* A TERRA-style twelve-note sweep must not stall on the legacy fifth
       voice. Sample MIDI has an independent 64-voice pool; when that pool is
       full, a new pitch replaces the oldest voice instead of being rejected. */
    for (int note = 36; note < 48; ++note) {
        TsNoteEvent played;
        CHECK(ts_note_event_midi(&played, note, 127, 0));
        CHECK(ts_note_bank_start_tuned_event(
                  &notes, &instrument, &unity, TS_AUDITION_CURRENT,
                  &played, 0, 48000) == TS_NOTE_STARTED);
    }
    CHECK(ts_note_bank_count(&notes) == 12);
    for (int note = 48; note < 100; ++note) {
        TsNoteEvent played;
        CHECK(ts_note_event_midi(&played, note, 127, 0));
        CHECK(ts_note_bank_start_tuned_event(
                  &notes, &instrument, &unity, TS_AUDITION_CURRENT,
                  &played, 0, 48000) == TS_NOTE_STARTED);
    }
    CHECK(ts_note_bank_count(&notes) == TS_MIDI_NOTE_VOICE_LIMIT);
    {
        TsNoteEvent replacement;
        int found_oldest = 0;
        int found_replacement = 0;
        CHECK(ts_note_event_midi(&replacement, 100, 127, 0));
        CHECK(ts_note_bank_start_tuned_event(
                  &notes, &instrument, &unity, TS_AUDITION_CURRENT,
                  &replacement, 0, 48000) == TS_NOTE_STARTED);
        CHECK(ts_note_bank_count(&notes) == TS_MIDI_NOTE_VOICE_LIMIT);
        for (int voice = 0; voice < TS_NOTE_BANK_VOICE_CAPACITY; ++voice) {
            if (!notes.voices[voice].active ||
                notes.voices[voice].origin != TS_NOTE_ORIGIN_MIDI) continue;
            if (notes.voices[voice].midi_note == 36) found_oldest = 1;
            if (notes.voices[voice].midi_note == 100) found_replacement = 1;
        }
        CHECK(!found_oldest && found_replacement);
    }

    /* MIDI sample voices cannot crowd the legacy five-voice QWERTY pool. */
    for (int key = 0; key < TS_NOTE_VOICE_LIMIT; ++key) {
        TsNoteEvent played;
        CHECK(ts_note_event_qwerty(&played, key, 60));
        CHECK(ts_note_bank_start_tuned_event(
                  &notes, &instrument, &unity, TS_AUDITION_CURRENT,
                  &played, 0, 48000) == TS_NOTE_STARTED);
    }
    CHECK(ts_note_bank_count(&notes) == TS_NOTE_BANK_VOICE_CAPACITY);
    {
        TsNoteEvent sixth_qwerty;
        CHECK(ts_note_event_qwerty(&sixth_qwerty, TS_NOTE_VOICE_LIMIT, 60));
        CHECK(ts_note_bank_start_tuned_event(
                  &notes, &instrument, &unity, TS_AUDITION_CURRENT,
                  &sixth_qwerty, 0, 48000) == TS_NOTE_LIMIT_REACHED);
    }
    ts_note_bank_clear(&notes);

    /* FM preview remains deliberately capped at the established five voices. */
    for (int note = 60; note < 60 + TS_NOTE_VOICE_LIMIT; ++note) {
        TsNoteEvent played;
        CHECK(ts_note_event_midi(&played, note, 127, 0));
        CHECK(ts_note_bank_start_sample_event(
                  &notes, &instrument.current, &unity, &played,
                  0, 48000) == TS_NOTE_STARTED);
    }
    {
        TsNoteEvent sixth_fm;
        CHECK(ts_note_event_midi(&sixth_fm, 72, 127, 0));
        CHECK(ts_note_bank_start_sample_event(
                  &notes, &instrument.current, &unity, &sixth_fm,
                  0, 48000) == TS_NOTE_LIMIT_REACHED);
    }
    CHECK(ts_note_bank_count(&notes) == TS_NOTE_VOICE_LIMIT);
    ts_note_bank_clear(&notes);

    ts_performance_init(&performance);
    CHECK(ts_performance_trigger_group_event(
              &performance, &instrument, 0x0003u, &c4_channel_1,
              0, 48000) == 2);
    CHECK(ts_performance_count(&performance) == 2);
    CHECK(fabsf(performance.voices[0].gain - 96.0f / 127.0f) < 0.0001f);
    (void)ts_performance_read(&performance, &raw);
    CHECK(raw == 0.0f);
    {
        size_t attack_frames = ts_audition_attack_frames(
            48000, TS_AUDITION_ATTACK_MS_DEFAULT);
        for (size_t frame = 1u; frame < attack_frames; ++frame)
            (void)ts_performance_read(&performance, &raw);
        CHECK(fabsf(raw - 2.0f * 0.5f * 96.0f / 127.0f) < 0.0001f);
    }
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
