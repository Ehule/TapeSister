#ifndef TAPESISTER_PERFORMANCE_H
#define TAPESISTER_PERFORMANCE_H

#include <stddef.h>
#include <stdatomic.h>
#include <stdint.h>

#include "tapesister/audition.h"
#include "tapesister/note_event.h"
#include "tapesister/sample.h"

#define TS_PERFORMANCE_VOICE_LIMIT (TS_BANK_SLOT_COUNT * 24)

enum {
    TS_TILE_FADE_MS_MIN = 0,
    TS_TILE_FADE_MS_MAX = 30000,
    TS_TILE_FADE_MS_DEFAULT = 0
};

typedef enum {
    TS_PERFORMANCE_TILE_FAILED = 0,
    TS_PERFORMANCE_TILE_STARTED,
    TS_PERFORMANCE_TILE_RELEASING,
    TS_PERFORMANCE_TILE_RESUMED
} TsPerformanceTileResult;

typedef struct TsPerformanceGeneration {
    TsSample sample;
    const float *source_data;
    uint32_t source_visual_revision;
    uint64_t source_hash;
    uint64_t id;
    atomic_uint readers;
    int source_slot;
    struct TsPerformanceGeneration *next_retired;
} TsPerformanceGeneration;

typedef struct {
    const TsSample *sample;
    TsPerformanceGeneration *generation;
    TsPerformanceGeneration *pending_generation;
    TsPerformanceGeneration *transition_generation;
    double position;
    double step;
    double transition_position;
    double transition_step;
    size_t range_first;
    size_t range_last;
    size_t crossfade_frames;
    size_t transition_range_first;
    size_t transition_range_last;
    size_t transition_crossfade_frames;
    size_t transition_frame;
    size_t transition_frames;
    size_t pending_range_first;
    size_t pending_range_last;
    size_t pending_crossfade_frames;
    size_t pending_transition_frames;
    size_t attack_frame;
    size_t attack_frames;
    size_t tile_fade_frames;
    size_t tile_ramp_remaining;
    TsLoopMode loop_mode;
    TsLoopMode transition_loop_mode;
    TsLoopMode pending_loop_mode;
    TsNoteOrigin origin;
    uint64_t group_id;
    int note;
    int midi_note;
    int channel;
    int source_slot;
    float gain;
    float group_gain;
    float tile_gain;
    float tile_gain_step;
    double pending_step;
    int looping;
    int direction;
    int transition_direction;
    int pending_direction;
    int latched;
    int releasing;
    int tile_launched;
    int active;
} TsPerformanceVoice;

typedef struct {
    TsPerformanceVoice voices[TS_PERFORMANCE_VOICE_LIMIT];
    TsPerformanceGeneration *slot_generations[TS_BANK_SLOT_COUNT];
    TsPerformanceGeneration *retired_generations;
    uint64_t next_generation_id;
    uint64_t next_group_id;
    int attack_ms;
} TsPerformanceBank;

void ts_performance_init(TsPerformanceBank *bank);
void ts_performance_clear(TsPerformanceBank *bank);
void ts_performance_free(TsPerformanceBank *bank);
void ts_performance_collect_retired(TsPerformanceBank *bank);
void ts_performance_set_attack_ms(TsPerformanceBank *bank, int milliseconds);
void ts_performance_release_sources_after_pass(TsPerformanceBank *bank,
                                               uint16_t source_mask);
void ts_performance_release_after_pass(TsPerformanceBank *bank);
void ts_performance_release(TsPerformanceBank *bank, int note);
void ts_performance_release_event(TsPerformanceBank *bank,
                                  const TsNoteEvent *event);
void ts_performance_release_midi_channel(TsPerformanceBank *bank, int channel);
void ts_performance_stop_sources(TsPerformanceBank *bank,
                                 uint16_t source_mask);
TsPerformanceTileResult ts_performance_toggle_tile(
    TsPerformanceBank *bank, const TsInstrument *instrument, int source_slot,
    int fade_ms, int output_rate);
void ts_performance_fade_all_tiles(TsPerformanceBank *bank);
uint16_t ts_performance_tile_mask(const TsPerformanceBank *bank);
const TsPerformanceVoice *ts_performance_tile_display_voice(
    const TsPerformanceBank *bank, int source_slot);
int ts_performance_trigger_group(TsPerformanceBank *bank,
                                 const TsInstrument *instrument,
                                 uint16_t source_mask,
                                 int note,
                                 int keyboard_base_note,
                                 int latched,
                                 int output_rate);
int ts_performance_trigger_group_event(TsPerformanceBank *bank,
                                       const TsInstrument *instrument,
                                       uint16_t source_mask,
                                       const TsNoteEvent *event,
                                       int latched,
                                       int output_rate);
int ts_performance_trigger_staged(TsPerformanceBank *bank,
                                  const TsInstrument *instrument,
                                  uint16_t source_mask,
                                  uint32_t staged_notes,
                                  int keyboard_base_note,
                                  int output_rate);
float ts_performance_read(TsPerformanceBank *bank, float *raw_mix);
TsStereoFrame ts_performance_read_stereo(TsPerformanceBank *bank,
                                         TsStereoFrame *raw_mix);
void ts_performance_sync(TsPerformanceBank *bank,
                         const TsInstrument *instrument,
                         int output_rate);
int ts_performance_prepare_sync(TsPerformanceBank *bank,
                                const TsInstrument *instrument);
int ts_performance_count(const TsPerformanceBank *bank);
uint32_t ts_performance_visible_mask(const TsPerformanceBank *bank,
                                     int keyboard_base_note);
int ts_performance_source_count(uint16_t source_mask);
float ts_performance_peak_scale(float *samples, size_t frames, float safe_peak);
float ts_performance_peak_scale_channels(float *samples, size_t frames,
                                         uint8_t channels, float safe_peak);

#endif
