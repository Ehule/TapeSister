#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/keyboard_sequence.h"
#include "tapesister/sister_project_state.h"
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

static void test_volume_lfo(void)
{
    prepare(TS_KEYBOARD_SEQUENCE_UP);
    TsKeyboardSequenceSettings s = sequence.settings;
    s.count = 1; s.notes[0] = 60; s.seconds = 10;
    ts_keyboard_sequence_set(&sequence, &s);
    render(10);
    assert(fabs(ts_keyboard_sequence_read(&sequence, 1000).l - .3) < .00001);
    double elapsed = sequence.elapsed;
    s.volume = .25; ts_keyboard_sequence_set(&sequence, &s);
    assert(sequence.elapsed == elapsed && sequence.running && sequence.current_note == 60);
    render(10);
    assert(fabs(ts_keyboard_sequence_read(&sequence, 1000).l - .075) < .00001);
    /* Full-range edits, including mute, ramp for at most 5 ms at any rate. */
    s.volume = 2; ts_keyboard_sequence_set(&sequence, &s);
    float previous = sequence.effective_gain;
    for (int i = 0; i < 240; ++i) {
        ts_keyboard_sequence_read(&sequence, 48000);
        assert(fabs(sequence.effective_gain - previous) <= 2.0 / 240 + .00001);
        previous = sequence.effective_gain;
    }
    assert(fabs(previous - 2) < .00001);
    assert(fabs(ts_keyboard_sequence_read(&sequence, 48000).l - .6) < .00001);
    s.volume = 0; ts_keyboard_sequence_set(&sequence, &s);
    render(6); assert(render(100) == 0 && sequence.running);
    s.volume = 1; s.lfo_enabled = 1; s.lfo_depth = 1; s.lfo_seconds = 4;
    ts_keyboard_sequence_set(&sequence, &s); ts_keyboard_sequence_reset(&sequence);
    /* Audio-clock sine: peak at start, silence halfway, no dependence on steps. */
    render(2000); render(1); assert(sequence.effective_gain < .00001);
    elapsed = sequence.elapsed;
    s.lfo_depth = .5; ts_keyboard_sequence_set(&sequence, &s);
    assert(sequence.elapsed == elapsed && sequence.lfo_phase > .5);
    render(6); assert(fabs(sequence.effective_gain - .5) < .0001);
    s.lfo_enabled = 0; ts_keyboard_sequence_set(&sequence, &s);
    render(6); assert(sequence.effective_gain == 1);
    s.lfo_enabled = 1; s.lfo_depth = 1; ts_keyboard_sequence_set(&sequence, &s);
    render(6); ts_keyboard_sequence_reset(&sequence);
    previous = sequence.effective_gain;
    ts_keyboard_sequence_read(&sequence, 48000);
    assert(fabs(sequence.effective_gain - previous) <= 2.0 / 240 + .00001);
    /* Changing device rate preserves phase; long cycles stay well resolved. */
    double phase = sequence.lfo_phase;
    s.lfo_seconds = 3600; ts_keyboard_sequence_set(&sequence, &s);
    ts_keyboard_sequence_read(&sequence, 192000);
    assert(fabs(sequence.lfo_phase - phase - 1.0 / (3600 * 192000)) < 1e-12);
    s.volume = NAN; s.lfo_depth = INFINITY; s.lfo_seconds = NAN;
    ts_keyboard_sequence_set(&sequence, &s);
    assert(sequence.settings.volume == 1 && sequence.settings.lfo_depth == .5 && sequence.settings.lfo_seconds == 4);
    s.volume = -1; s.lfo_depth = 2; s.lfo_seconds = 0;
    ts_keyboard_sequence_set(&sequence, &s);
    assert(sequence.settings.volume == 0 && sequence.settings.lfo_depth == 1 && sequence.settings.lfo_seconds == .05);
    render(10); assert(render(40) == 0);
}

static void test_slots_and_projects(void)
{
    prepare(TS_KEYBOARD_SEQUENCE_ORDER); render(12);
    TsKeyboardSequenceSettings first = sequence.settings;
    assert(ts_keyboard_sequence_select_slot(&sequence, 15));
    assert(!sequence.running && sequence.settings.count == 0); /* Empty stops. */
    TsKeyboardSequenceSettings last = sequence.settings;
    last.count = 3; last.notes[0] = 72; last.notes[1] = 65; last.notes[2] = 69;
    last.mode = TS_KEYBOARD_SEQUENCE_ORDER; last.seconds = 1.234567890123;
    last.gate = .31; last.loop = 0; last.volume = .42;
    last.lfo_enabled = 1; last.lfo_seconds = 17.5; last.lfo_depth = .75;
    ts_keyboard_sequence_set(&sequence, &last);
    assert(!sequence.running && ts_keyboard_sequence_play(&sequence)); render(10);
    assert(sequence.current_note == 72);
    assert(ts_keyboard_sequence_select_slot(&sequence, 0) && sequence.running); render(1);
    assert(sequence.current_note == 67 && sequence.elapsed < .002);
    assert(!memcmp(&sequence.settings, &first, sizeof(first)));
    double elapsed = sequence.elapsed;
    assert(ts_keyboard_sequence_select_slot(&sequence, 0) && sequence.elapsed == elapsed);
    assert(!ts_keyboard_sequence_select_slot(&sequence, 16) && sequence.running);
    assert(ts_keyboard_sequence_select_slot(&sequence, 15) && sequence.running); render(1);
    assert(sequence.current_note == 72 && sequence.lfo_phase < .001);
    assert(!memcmp(&sequence.settings, &last, sizeof(last)));

    TsSisterProjectState saved, loaded;
    ts_sister_project_state_init(&saved, 1000);
    saved.keyboard_sequence = ts_keyboard_sequence_export(&sequence);
    char error[160]; int present = 0;
    const char *path = "test-arp-state.ini";
    assert(ts_sister_project_state_save_file(&saved, path, error, sizeof(error)));
    assert(sequence.running); /* Saving does not affect execution. */
    assert(ts_sister_project_state_load_file(&loaded, path, 48000, &present, error, sizeof(error)) && present);
    assert(saved.keyboard_sequence.selected == loaded.keyboard_sequence.selected);
    for (int i = 0; i < TS_KEYBOARD_SEQUENCE_SLOTS; ++i) {
        const TsKeyboardSequenceSettings *a = &saved.keyboard_sequence.slot[i], *b = &loaded.keyboard_sequence.slot[i];
        assert(a->count == b->count && !memcmp(a->notes, b->notes, sizeof(a->notes)));
        assert(a->mode == b->mode && a->loop == b->loop && a->seconds == b->seconds && a->gate == b->gate);
        assert(a->volume == b->volume && a->lfo_enabled == b->lfo_enabled && a->lfo_seconds == b->lfo_seconds && a->lfo_depth == b->lfo_depth);
    }
    ts_keyboard_sequence_set_bank(&sequence, &loaded.keyboard_sequence);
    assert(!sequence.running && sequence.current_note == -1 && sequence.elapsed == 0 && sequence.lfo_phase == 0);
    assert(render(30) == 0 && sequence.bank.selected == 15);
    assert(ts_keyboard_sequence_select_slot(&sequence, 0) && !sequence.running);
    assert(!memcmp(&sequence.settings, &first, sizeof(first)));
    /* CLEAR affects only the current slot. */
    TsKeyboardSequenceSettings empty = sequence.settings; empty.count = 0;
    ts_keyboard_sequence_set(&sequence, &empty);
    assert(sequence.bank.slot[0].count == 0 && sequence.bank.slot[15].count == 3);

    FILE *file = fopen(path, "wb"); assert(file);
    fputs("TapeSister Sister Project State\nVersion=25\nPageCount=1\nActivePage=0\n", file); fclose(file);
    assert(ts_sister_project_state_load_file(&loaded, path, 48000, &present, error, sizeof(error)));
    for (int i = 0; i < TS_KEYBOARD_SEQUENCE_SLOTS; ++i)
        assert(loaded.keyboard_sequence.slot[i].count == 0 && loaded.keyboard_sequence.slot[i].volume == 1);
    assert(loaded.keyboard_sequence.selected == 0);
    const char *bad[] = {"Arp.Slot.0.Notes=60,", "Arp.Slot.2.Volume=nan", "Arp.Slot.0.Notes=99999999999999999999",
                         "Arp.Slot.0.Gate=.5garbage", "Arp.SelectedSlot=999999999999999999999999"};
    for (size_t i = 0; i < sizeof(bad)/sizeof(*bad); ++i) {
        file = fopen(path, "wb"); assert(file);
        fprintf(file, "TapeSister Sister Project State\nVersion=26\nPageCount=1\n%s\n", bad[i]); fclose(file);
        assert(!ts_sister_project_state_load_file(&loaded, path, 48000, &present, error, sizeof(error)));
    }
    TsKeyboardSequenceBank bank; ts_keyboard_sequence_bank_default(&bank);
    assert(ts_keyboard_sequence_bank_read(&bank, "Arp.Slot.0.Notes", "67,60,67,64") == 1);
    assert(ts_keyboard_sequence_bank_read(&bank, "Arp.Slot.0.StepSeconds", "0") == 1);
    assert(ts_keyboard_sequence_bank_read(&bank, "Arp.SelectedSlot", "99") == 1);
    assert(ts_keyboard_sequence_bank_read(&bank, "Arp.Slot.20.Notes", "60") == 0);
    ts_keyboard_sequence_bank_sanitize(&bank);
    assert(bank.selected == 0 && bank.slot[0].count == 3 && bank.slot[0].notes[1] == 60);
    assert(bank.slot[0].seconds == TS_KEYBOARD_SEQUENCE_MIN_SECONDS);
    remove(path);
}

int main(void)
{
    for (int i = 0; i < 256; ++i) { data[2*i] = .3f; data[2*i+1] = -.3f; }
    source.sample = (TsSample){data, 256u, 1000u, "STEREO", 0u, 2u};
    source.count = 1;
    source.voices[0] = (TsNoteVoice){.sample=&source.sample, .range_last=256,
        .step=1, .gain=1, .active=1, .looping=1, .direction=1};
    test_volume_lfo();
    test_slots_and_projects();
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
