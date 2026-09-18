#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/keyboard_sequence.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static TsKeyboardSequence sequence;
static TsKeyboardSequenceSource source;
static float data[512];

static double render(int frames)
{
    double energy = 0;
    for (int i = 0; i < frames; ++i) {
        TsStereoFrame f = ts_keyboard_sequence_read(&sequence, 1000);
        assert(isfinite(f.l) && isfinite(f.r));
        assert(fabs(f.l + f.r) < .00001); /* Preserve opposite stereo channels. */
        energy += f.l * f.l + f.r * f.r;
    }
    return energy;
}

static void prepare(int mode)
{
    ts_keyboard_sequence_init(&sequence);
    TsKeyboardSequenceSettings s = sequence.settings;
    s.notes[0] = 67; s.notes[1] = 60; s.notes[2] = 64; s.count = 3;
    s.seconds = .04; s.gate = 1; s.mode = mode;
    ts_keyboard_sequence_set(&sequence, &s);
    ts_keyboard_sequence_source(&sequence, &source);
    assert(ts_keyboard_sequence_play(&sequence));
}

int main(void)
{
    for (int i = 0; i < 256; ++i) { data[2*i] = .3f; data[2*i+1] = -.3f; }
    source.sample = (TsSample){data, 256u, 1000u, "STEREO", 0u, 2u};
    source.count = 1;
    source.voices[0] = (TsNoteVoice){.sample=&source.sample, .range_last=256,
        .step=1, .gain=1, .active=1, .looping=1, .direction=1};
    const int expected[][8] = {
        {60,64,67,60,64,67,60,64}, {67,64,60,67,64,60,67,64},
        {60,64,67,64,60,64,67,64}, {67,60,64,67,60,64,67,60}
    };
    for (int mode = 0; mode < 4; ++mode) {
        prepare(mode);
        for (int step = 0; step < 8; ++step) {
            assert(render(1) >= 0);
            assert(sequence.current_note == expected[mode][step]);
            assert(render(39) > 0);
        }
        ts_keyboard_sequence_stop(&sequence);
        render(6); assert(render(40) == 0);
    }
    prepare(TS_KEYBOARD_SEQUENCE_RANDOM);
    for (int i = 0; i < 50; ++i) {
        render(1); assert(sequence.current_note == 60 || sequence.current_note == 64 || sequence.current_note == 67);
        render(39);
    }
    prepare(TS_KEYBOARD_SEQUENCE_UP_DOWN);
    TsKeyboardSequenceSettings s = sequence.settings; s.loop = 0;
    ts_keyboard_sequence_set(&sequence, &s); render(160);
    assert(sequence.running); render(1); assert(!sequence.running); render(6); assert(render(80) == 0);

    prepare(TS_KEYBOARD_SEQUENCE_UP);
    s = sequence.settings; s.gate = .5;
    ts_keyboard_sequence_set(&sequence, &s); assert(render(15) > 0);
    render(6); assert(render(19) == 0); render(1); assert(sequence.current_note == 64);
    assert(render(10) > 0);
    /* A shortened live timer completes one phase, never a burst of missed notes. */
    s = sequence.settings; s.seconds = 240; ts_keyboard_sequence_set(&sequence, &s);
    sequence.elapsed = 123; s.seconds = .03; ts_keyboard_sequence_set(&sequence, &s);
    render(1); assert(sequence.current_note == 67 && sequence.elapsed < .002);
    s.seconds = 3600; ts_keyboard_sequence_set(&sequence, &s);
    sequence.elapsed = 3599.998; render(2); assert(sequence.current_note == 67);
    render(1); assert(sequence.current_note == 60);
    /* Removing the active pitch replaces it on the next audio frame. */
    assert(ts_keyboard_sequence_toggle(&sequence, 60)); render(1); assert(sequence.current_note != 60);
    assert(ts_keyboard_sequence_toggle(&sequence, 64)); assert(ts_keyboard_sequence_toggle(&sequence, 67));
    assert(!sequence.running); render(6); assert(render(100) == 0);
    for (int i = 0; i < 24; ++i) assert(ts_keyboard_sequence_toggle(&sequence, 60+i));
    assert(!ts_keyboard_sequence_toggle(&sequence, 90));
    assert(ts_keyboard_sequence_mask(&sequence.settings, 60) == 0xffffffu);
    assert(ts_keyboard_sequence_mask(&sequence.settings, 72) == 0xfffu);
    assert(ts_keyboard_sequence_play(&sequence)); render(1);
    int pitch = sequence.current_note;
    s = sequence.settings; s.gate = .7; ts_keyboard_sequence_set(&sequence, &s);
    render(1); assert(sequence.current_note == pitch);
    ts_keyboard_sequence_source(&sequence, NULL); render(6); assert(render(40) == 0);
    assert(!ts_keyboard_sequence_play(&sequence));
    /* A stopped one-shot is revived independently on every step. */
    source.voices[0].looping = 0; source.voices[0].range_last = 4;
    prepare(TS_KEYBOARD_SEQUENCE_UP); render(40); assert(!sequence.voices[0].active);
    render(1); assert(sequence.voices[0].active && sequence.current_note == 64);
    puts("Keyboard sequence clock, ordering, edits, gate, stereo, and release tests passed.");
    return 0;
}
