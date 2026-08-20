#ifndef TAPESISTER_NOTE_BANK_H
#define TAPESISTER_NOTE_BANK_H

#include <stdint.h>

#include "tapesister/audition.h"

#define TS_NOTE_VOICE_LIMIT 5

typedef struct {
    const TsSample *sample;
    double position;
    double step;
    double pitch;
    size_t range_first;
    size_t range_last;
    size_t crossfade_frames;
    TsAuditionSource source;
    TsLoopMode loop_mode;
    uint64_t serial;
    int note;
    int midi_note;
    int looping;
    int direction;
    int latched;
    int synth;
    int active;
} TsNoteVoice;

typedef struct {
    TsNoteVoice voices[TS_NOTE_VOICE_LIMIT];
    uint64_t next_serial;
} TsNoteBank;

typedef enum {
    TS_NOTE_START_FAILED = 0,
    TS_NOTE_STARTED,
    TS_NOTE_TOGGLED_OFF,
    TS_NOTE_LIMIT_REACHED
} TsNoteStartResult;

void ts_note_bank_init(TsNoteBank *bank);
void ts_note_bank_clear(TsNoteBank *bank);
void ts_note_bank_clear_latched(TsNoteBank *bank);
int ts_note_bank_latch_active_synth(TsNoteBank *bank);
int ts_note_bank_release_latched_synth(TsNoteBank *bank);
TsNoteStartResult ts_note_bank_start(TsNoteBank *bank, const TsInstrument *instrument,
                                     TsAuditionSource source, int note, int latched,
                                     int output_rate);
TsNoteStartResult ts_note_bank_start_tuned(TsNoteBank *bank,
                                           const TsInstrument *instrument,
                                           const TsTuning *tuning,
                                           TsAuditionSource source, int note,
                                           int latched, int output_rate);
TsNoteStartResult ts_note_bank_start_tuned_at(TsNoteBank *bank,
                                              const TsInstrument *instrument,
                                              const TsTuning *tuning,
                                              TsAuditionSource source, int note,
                                              int keyboard_base_note,
                                              int latched, int output_rate);
TsNoteStartResult ts_note_bank_start_sample(TsNoteBank *bank,
                                            const TsSample *sample,
                                            const TsTuning *tuning,
                                            int note, int keyboard_base_note,
                                            int latched, int output_rate);
void ts_note_bank_replace_sample(TsNoteBank *bank,
                                 const TsSample *old_sample,
                                 const TsSample *new_sample,
                                 int output_rate);
int ts_note_bank_start_staged_chord(TsNoteBank *bank,
                                    const TsInstrument *instrument,
                                    const TsTuning *tuning,
                                    TsAuditionSource source,
                                    uint32_t staged_notes,
                                    int keyboard_base_note,
                                    int output_rate);
void ts_note_bank_release(TsNoteBank *bank, int note);
void ts_note_bank_sync(TsNoteBank *bank, const TsInstrument *instrument, int output_rate);
void ts_note_bank_sync_tuned(TsNoteBank *bank, const TsInstrument *instrument,
                             const TsTuning *tuning, int output_rate);
void ts_note_bank_set_source(TsNoteBank *bank, const TsInstrument *instrument,
                             TsAuditionSource source, int output_rate);
void ts_note_bank_set_source_tuned(TsNoteBank *bank,
                                   const TsInstrument *instrument,
                                   const TsTuning *tuning,
                                   TsAuditionSource source, int output_rate);
float ts_note_bank_read(TsNoteBank *bank);
float ts_note_bank_read_split(TsNoteBank *bank, float *synth_output);
int ts_note_bank_count(const TsNoteBank *bank);
int ts_note_bank_synth_count(const TsNoteBank *bank);
int ts_note_bank_latched_synth_count(const TsNoteBank *bank);
uint32_t ts_note_bank_mask(const TsNoteBank *bank);
uint32_t ts_note_bank_visible_mask(const TsNoteBank *bank,
                                   int keyboard_base_note);
const TsNoteVoice *ts_note_bank_display_voice(const TsNoteBank *bank);

#endif
