#ifndef TAPESISTER_KEYBOARD_SEQUENCE_H
#define TAPESISTER_KEYBOARD_SEQUENCE_H

#include <stdio.h>

#include "tapesister/note_bank.h"
#include "tapesister/performance.h"

#define TS_KEYBOARD_SEQUENCE_NOTES 24
#define TS_KEYBOARD_SEQUENCE_SLOTS 16
#define TS_KEYBOARD_SEQUENCE_MIN_SECONDS 0.03
#define TS_KEYBOARD_SEQUENCE_MAX_SECONDS 3600.0
#define TS_KEYBOARD_SEQUENCE_LFO_MIN_SECONDS 0.05

typedef enum {
    TS_KEYBOARD_SEQUENCE_UP, TS_KEYBOARD_SEQUENCE_DOWN,
    TS_KEYBOARD_SEQUENCE_UP_DOWN, TS_KEYBOARD_SEQUENCE_ORDER,
    TS_KEYBOARD_SEQUENCE_RANDOM, TS_KEYBOARD_SEQUENCE_MODE_COUNT
} TsKeyboardSequenceMode;

typedef struct {
    int notes[TS_KEYBOARD_SEQUENCE_NOTES]; /* Absolute MIDI pitches, in click order. */
    int count, mode, loop;
    double seconds, gate;
    double volume, lfo_seconds, lfo_depth; /* ARP-only gain, cycle time, attenuation. */
    int lfo_enabled;
} TsKeyboardSequenceSettings;

typedef struct {
    TsKeyboardSequenceSettings slot[TS_KEYBOARD_SEQUENCE_SLOTS];
    int selected;
} TsKeyboardSequenceBank;

/* Prepared on the UI thread, immutable while published to the audio thread. */
typedef struct {
    TsSample sample;
    TsPerformanceBank *group;
    TsNoteVoice voices[TS_BANK_SLOT_COUNT]; /* Root-pitch templates, MIDI 60. */
    int count, fm;
} TsKeyboardSequenceSource;

typedef struct {
    TsKeyboardSequenceSettings settings;
    TsKeyboardSequenceBank bank;
    TsKeyboardSequenceSource *source;
    TsNoteVoice voices[TS_BANK_SLOT_COUNT];
    int order[TS_KEYBOARD_SEQUENCE_NOTES], order_count;
    int running, current_note, cursor, fresh;
    double elapsed;
    uint32_t random;
    TsStereoFrame last, fade_from;
    unsigned fade_remaining, fade_frames;
    double gain_current, lfo_phase;
    float effective_gain;
} TsKeyboardSequence;

void ts_keyboard_sequence_init(TsKeyboardSequence *sequence);
void ts_keyboard_sequence_settings_default(TsKeyboardSequenceSettings *settings);
void ts_keyboard_sequence_settings_sanitize(TsKeyboardSequenceSettings *settings);
void ts_keyboard_sequence_bank_default(TsKeyboardSequenceBank *bank);
void ts_keyboard_sequence_bank_sanitize(TsKeyboardSequenceBank *bank);
TsKeyboardSequenceBank ts_keyboard_sequence_export(const TsKeyboardSequence *sequence);
/* Replaces configuration and clears playback; source ownership is unchanged. */
void ts_keyboard_sequence_set_bank(TsKeyboardSequence *sequence, const TsKeyboardSequenceBank *bank);
int ts_keyboard_sequence_select_slot(TsKeyboardSequence *sequence, int slot);
/* Static project configuration only; no source pointers or transport state. */
int ts_keyboard_sequence_bank_write(FILE *file, const TsKeyboardSequenceBank *bank);
/* 1 = recognized, 0 = unknown key, -1 = malformed value. */
int ts_keyboard_sequence_bank_read(TsKeyboardSequenceBank *bank, const char *key, const char *value);
void ts_keyboard_sequence_stop(TsKeyboardSequence *sequence);
int ts_keyboard_sequence_play(TsKeyboardSequence *sequence);
void ts_keyboard_sequence_reset(TsKeyboardSequence *sequence);
/* Settings edits preserve the sounding pitch/elapsed phase if still selected. */
void ts_keyboard_sequence_set(TsKeyboardSequence *sequence,
                               const TsKeyboardSequenceSettings *settings);
int ts_keyboard_sequence_toggle(TsKeyboardSequence *sequence, int midi_note);
TsKeyboardSequenceSource *ts_keyboard_sequence_source(
    TsKeyboardSequence *sequence, TsKeyboardSequenceSource *source);
void ts_keyboard_sequence_source_free(TsKeyboardSequenceSource *source);
TsStereoFrame ts_keyboard_sequence_read(TsKeyboardSequence *sequence, int rate);
const char *ts_keyboard_sequence_mode_name(int mode);
uint32_t ts_keyboard_sequence_mask(const TsKeyboardSequenceSettings *settings,
                                   int base_note);

#endif
