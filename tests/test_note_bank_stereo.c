#include "tapesister/note_bank.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; \
} } while (0)
#define CLOSE(a,b) (fabsf((a) - (b)) < 0.0001f)

static void prepare_voice(TsNoteBank *bank, const TsSample *sample,
                          double position, double step)
{
    TsNoteVoice *voice;
    ts_note_bank_init(bank);
    voice = &bank->voices[0];
    voice->sample = sample;
    voice->position = position;
    voice->step = fabs(step);
    voice->range_first = 0u;
    voice->range_last = sample->frames;
    voice->direction = step < 0.0 ? -1 : 1;
    voice->gain = 1.0f;
    voice->active = 1;
}

int main(void)
{
    float mono_data[] = {0.0f, 1.0f, 0.0f, -1.0f};
    float stereo_data[] = {0.0f, 1.0f, 1.0f, 0.0f,
                           0.0f, -1.0f, -1.0f, 0.0f};
    TsSample mono = {mono_data, 4u, 48000u, "MONO", 0u, 1u};
    TsSample stereo = {stereo_data, 4u, 48000u, "STEREO", 0u, 2u};
    TsNoteBank bank;
    TsStereoFrame frame;

    prepare_voice(&bank, &mono, 0.5, 1.0);
    frame = ts_note_bank_read_stereo(&bank);
    CHECK(CLOSE(frame.l, 0.5f) && CLOSE(frame.r, 0.5f));
    CHECK(CLOSE((float)bank.voices[0].position, 1.5f));

    prepare_voice(&bank, &stereo, 0.5, 1.0);
    frame = ts_note_bank_read_stereo(&bank);
    CHECK(CLOSE(frame.l, 0.5f) && CLOSE(frame.r, 0.5f));
    frame = ts_note_bank_read_stereo(&bank);
    CHECK(CLOSE(frame.l, 0.5f) && CLOSE(frame.r, -0.5f));
    CHECK(CLOSE((float)bank.voices[0].position, 2.5f));

    prepare_voice(&bank, &stereo, 2.5, -1.0);
    frame = ts_note_bank_read_stereo(&bank);
    CHECK(CLOSE(frame.l, -0.5f) && CLOSE(frame.r, -0.5f));
    CHECK(CLOSE((float)bank.voices[0].position, 1.5f));

    prepare_voice(&bank, &stereo, 0.0, 1.0);
    bank.voices[0].attack_frames = 4u;
    CHECK(CLOSE(ts_note_bank_read_stereo(&bank).l, 0.0f));
    frame = ts_note_bank_read_stereo(&bank);
    CHECK(CLOSE(frame.l, ts_audition_attack_gain(1u, 4u)) &&
          CLOSE(frame.r, 0.0f));

    prepare_voice(&bank, &stereo, 2.5, 1.0);
    bank.voices[0].looping = 1;
    bank.voices[0].range_first = 0u;
    bank.voices[0].range_last = 4u;
    bank.voices[0].crossfade_frames = 1u;
    frame = ts_note_bank_read_stereo(&bank);
    CHECK(isfinite(frame.l) && isfinite(frame.r));
    CHECK(CLOSE((float)bank.voices[0].position, 3.5f));

    {
        TsInstrument instrument;
        TsTuning tuning = {60, 0.0f};
        TsNoteEvent event;
        memset(&instrument, 0, sizeof(instrument));
        instrument.current = stereo;
        instrument.tuning = tuning;
        ts_note_bank_init(&bank);
        ts_note_bank_set_attack_ms(&bank, 0);
        for (int voice = 0; voice < TS_MIDI_NOTE_VOICE_LIMIT; ++voice) {
            CHECK(ts_note_event_midi(&event, voice, 127, 0));
            CHECK(ts_note_bank_start_tuned_event(
                      &bank, &instrument, &tuning, TS_AUDITION_CURRENT,
                      &event, 0, 48000) == TS_NOTE_STARTED);
        }
        CHECK(ts_note_bank_count(&bank) == TS_MIDI_NOTE_VOICE_LIMIT);
        CHECK(ts_note_event_midi(&event, 100, 127, 0));
        CHECK(ts_note_bank_start_tuned_event(
                  &bank, &instrument, &tuning, TS_AUDITION_CURRENT,
                  &event, 0, 48000) == TS_NOTE_STARTED);
        CHECK(ts_note_bank_count(&bank) == TS_MIDI_NOTE_VOICE_LIMIT);
        CHECK(bank.voices[TS_NOTE_VOICE_LIMIT].midi_note == 100);
    }

    if (failures) return 1;
    puts("note bank stereo tests passed");
    return 0;
}
