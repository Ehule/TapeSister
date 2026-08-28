#ifndef TAPESISTER_SISTER_FALLOUT_H
#define TAPESISTER_SISTER_FALLOUT_H

#include "tapesister/sample.h"

#include <stddef.h>
#include <stdint.h>

typedef struct {
    int enabled;
    float mix;
    float feedback;
    float noise;
    int drop_enabled;
    float drop_rate;
    int pan_enabled;
    float pan_rate;
    int skip_enabled;
    float skip_span;
    float skip_rate;
    int bit_enabled;
    float bit_quality;
    float bit_resolution;
    float bit_rate;
    int pitch_enabled;
    float pitch;
    float pitch_ramp;
    float pitch_rate;
} TsSisterFalloutControls;

typedef struct {
    TsStereoFrame output;
    /* Effect-only signal before MIX and before the Master FX chain. */
    TsStereoFrame wet;
} TsSisterFalloutResult;

typedef struct {
    float *buffer;
    size_t capacity_frames;
    size_t valid_frames;
    uint64_t write_clock;
    double read_clock;
    double old_read_clock;
    double loop_start;
    double loop_length;
    uint64_t next_skip;
    uint64_t next_pitch;
    uint64_t next_drop;
    uint64_t next_pan;
    uint64_t next_bit;
    uint32_t skip_fade_remaining;
    uint32_t skip_fade_total;
    uint32_t sample_rate;
    uint32_t prng;
    float engage;
    float engage_step;
    uint32_t engage_remaining;
    float playback_rate;
    float playback_target;
    float playback_step;
    uint32_t playback_remaining;
    float drop_gain;
    float drop_target;
    float drop_step;
    uint32_t drop_remaining;
    float pan;
    float pan_target;
    float pan_step;
    uint32_t pan_remaining;
    float held[2];
    float bit_quality_current;
    uint32_t hold_remaining;
    float noise_state[2];
    float noise_cutoff;
    TsSisterFalloutControls controls;
    int active;
    int ready;
} TsSisterFalloutEngine;

void ts_sister_fallout_controls_default(TsSisterFalloutControls *controls);
void ts_sister_fallout_controls_sanitize(TsSisterFalloutControls *controls);
int ts_sister_fallout_init(TsSisterFalloutEngine *engine,
                           uint32_t sample_rate);
int ts_sister_fallout_reconfigure(TsSisterFalloutEngine *engine,
                                  uint32_t sample_rate);
void ts_sister_fallout_free(TsSisterFalloutEngine *engine);
void ts_sister_fallout_clear(TsSisterFalloutEngine *engine);
void ts_sister_fallout_seed(TsSisterFalloutEngine *engine, uint32_t seed);
void ts_sister_fallout_set_controls(
    TsSisterFalloutEngine *engine, const TsSisterFalloutControls *controls);
TsSisterFalloutResult ts_sister_fallout_process(
    TsSisterFalloutEngine *engine, TsStereoFrame input);
size_t ts_sister_fallout_memory_bytes(const TsSisterFalloutEngine *engine);

#endif
