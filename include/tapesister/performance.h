#ifndef TAPESISTER_PERFORMANCE_H
#define TAPESISTER_PERFORMANCE_H

#include <stddef.h>
#include <stdint.h>

#include "tapesister/audition.h"
#include "tapesister/sample.h"

#define TS_PERFORMANCE_VOICE_LIMIT (TS_BANK_SLOT_COUNT * 24)

typedef struct {
    const TsSample *sample;
    double position;
    double step;
    size_t range_first;
    size_t range_last;
    size_t crossfade_frames;
    TsLoopMode loop_mode;
    int note;
    int midi_note;
    int source_slot;
    int looping;
    int direction;
    int latched;
    int releasing;
    int active;
} TsPerformanceVoice;

typedef struct {
    TsPerformanceVoice voices[TS_PERFORMANCE_VOICE_LIMIT];
} TsPerformanceBank;

void ts_performance_init(TsPerformanceBank *bank);
void ts_performance_clear(TsPerformanceBank *bank);
void ts_performance_release_sources_after_pass(TsPerformanceBank *bank,
                                               uint16_t source_mask);
void ts_performance_release_after_pass(TsPerformanceBank *bank);
void ts_performance_release(TsPerformanceBank *bank, int note);
int ts_performance_trigger_group(TsPerformanceBank *bank,
                                 const TsInstrument *instrument,
                                 uint16_t source_mask,
                                 int note,
                                 int keyboard_base_note,
                                 int latched,
                                 int output_rate);
int ts_performance_trigger_staged(TsPerformanceBank *bank,
                                  const TsInstrument *instrument,
                                  uint16_t source_mask,
                                  uint32_t staged_notes,
                                  int keyboard_base_note,
                                  int output_rate);
float ts_performance_read(TsPerformanceBank *bank, float *raw_mix);
void ts_performance_sync(TsPerformanceBank *bank,
                         const TsInstrument *instrument,
                         int output_rate);
int ts_performance_count(const TsPerformanceBank *bank);
uint32_t ts_performance_visible_mask(const TsPerformanceBank *bank,
                                     int keyboard_base_note);
int ts_performance_source_count(uint16_t source_mask);
float ts_performance_peak_scale(float *samples, size_t frames, float safe_peak);

#endif
