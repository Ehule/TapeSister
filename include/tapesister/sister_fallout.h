#ifndef TAPESISTER_SISTER_FALLOUT_H
#define TAPESISTER_SISTER_FALLOUT_H

#include "tapesister/sample.h"

#include <stddef.h>
#include <stdint.h>

#define TS_SISTER_FALLOUT_TARGET_COUNT 13u

typedef enum {
    TS_SISTER_FALLOUT_NOISE_WHITE = 0,
    TS_SISTER_FALLOUT_NOISE_PINK,
    TS_SISTER_FALLOUT_NOISE_BROWN,
    TS_SISTER_FALLOUT_NOISE_BLUE,
    TS_SISTER_FALLOUT_NOISE_COUNT
} TsSisterFalloutNoiseType;

typedef enum {
    TS_SISTER_FALLOUT_RISE_SAW = 0,
    TS_SISTER_FALLOUT_RISE_ONE_SHOT,
    TS_SISTER_FALLOUT_RISE_MODE_COUNT
} TsSisterFalloutRiseMode;

typedef enum {
    TS_SISTER_FALLOUT_LFO_MIX = 1u << 0,
    TS_SISTER_FALLOUT_LFO_FEEDBACK = 1u << 1,
    TS_SISTER_FALLOUT_LFO_NOISE = 1u << 2,
    TS_SISTER_FALLOUT_LFO_DROP_RATE = 1u << 3,
    TS_SISTER_FALLOUT_LFO_PAN_RATE = 1u << 4,
    TS_SISTER_FALLOUT_LFO_SKIP_SPAN = 1u << 5,
    TS_SISTER_FALLOUT_LFO_SKIP_RATE = 1u << 6,
    TS_SISTER_FALLOUT_LFO_BIT_QUALITY = 1u << 7,
    TS_SISTER_FALLOUT_LFO_BIT_RESOLUTION = 1u << 8,
    TS_SISTER_FALLOUT_LFO_BIT_RATE = 1u << 9,
    TS_SISTER_FALLOUT_LFO_PITCH = 1u << 10,
    TS_SISTER_FALLOUT_LFO_PITCH_RAMP = 1u << 11,
    TS_SISTER_FALLOUT_LFO_PITCH_RATE = 1u << 12,
    TS_SISTER_FALLOUT_LFO_ALL = (1u << 13) - 1u
} TsSisterFalloutLfoTarget;

typedef struct {
    int enabled;
    float mix;
    float feedback;
    float noise;
    TsSisterFalloutNoiseType noise_type;
    /* Logarithmic 10 ms..60 min preset-recall crossfade. */
    float transition;
    /* Logarithmic 10 ms..60 min component activation ramp. */
    float component_transition;
    /* Independent logarithmic 10 ms..60 min Fallout master gate ramp. */
    float master_transition;
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
    /* Logarithmic 1 cycle/hour..10 Hz sine rate. */
    float lfo_rate;
    float lfo_intensity;
    uint32_t lfo_targets;
    TsSisterFalloutRiseMode rise_mode;
    /* Logarithmic 1 second..4 hour rise length. */
    float rise_length;
    float rise_intensity;
    uint32_t rise_targets;
    /* UI edge counter: selecting 1-SHOT again re-arms it. */
    uint32_t rise_retrigger;
} TsSisterFalloutControls;

typedef struct {
    float current;
    float target;
    float step;
    uint32_t remaining;
    uint32_t total;
} TsSisterFalloutRamp;

typedef enum {
    TS_SISTER_FALLOUT_TRANSITION_NONE = 0,
    TS_SISTER_FALLOUT_TRANSITION_MASTER,
    TS_SISTER_FALLOUT_TRANSITION_DROP,
    TS_SISTER_FALLOUT_TRANSITION_PAN,
    TS_SISTER_FALLOUT_TRANSITION_SKIP,
    TS_SISTER_FALLOUT_TRANSITION_BIT,
    TS_SISTER_FALLOUT_TRANSITION_PITCH
} TsSisterFalloutTransitionSource;

typedef struct {
    float progress;
    TsSisterFalloutTransitionSource source;
    int target_enabled;
    int active;
} TsSisterFalloutTransitionStatus;

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
    uint32_t engage_total;
    TsSisterFalloutRamp drop_engage;
    TsSisterFalloutRamp pan_engage;
    TsSisterFalloutRamp skip_engage;
    TsSisterFalloutRamp bit_engage;
    TsSisterFalloutRamp pitch_engage;
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
    float pink_state[2][3];
    float brown_state[2];
    float previous_white[2];
    double lfo_phase;
    float lfo_value;
    double rise_phase;
    float rise_value;
    float rise_smoothed;
    float feedback_modulated;
    float mix_modulated;
    int rise_one_shot_complete;
    TsSisterFalloutControls controls;
    /* Continuous panel values chase controls at audio rate.  Keeping this
       state separate lets rapid wheel retargets continue from the value that
       was actually heard instead of jumping to the newest UI value. */
    TsSisterFalloutControls smoothed_controls;
    float lfo_target_blend[TS_SISTER_FALLOUT_TARGET_COUNT];
    float lfo_target_step[TS_SISTER_FALLOUT_TARGET_COUNT];
    uint32_t lfo_target_remaining[TS_SISTER_FALLOUT_TARGET_COUNT];
    float rise_target_blend[TS_SISTER_FALLOUT_TARGET_COUNT];
    float rise_target_step[TS_SISTER_FALLOUT_TARGET_COUNT];
    uint32_t rise_target_remaining[TS_SISTER_FALLOUT_TARGET_COUNT];
    TsStereoFrame control_handoff_output;
    TsStereoFrame control_handoff_wet;
    TsStereoFrame previous_output;
    TsStereoFrame previous_wet;
    uint32_t control_handoff_total;
    uint32_t control_handoff_remaining;
    int previous_output_valid;
    TsSisterFalloutControls preset_target;
    float preset_gain;
    float preset_gain_step;
    uint32_t preset_gain_remaining;
    uint32_t preset_transition_total;
    uint32_t preset_second_frames;
    int preset_transition_stage;
    int preset_applying;
    int active;
    int ready;
} TsSisterFalloutEngine;

void ts_sister_fallout_controls_default(TsSisterFalloutControls *controls);
void ts_sister_fallout_controls_sanitize(TsSisterFalloutControls *controls);
const char *ts_sister_fallout_noise_type_name(TsSisterFalloutNoiseType type);
const char *ts_sister_fallout_rise_mode_name(TsSisterFalloutRiseMode mode);
float ts_sister_fallout_transition_ms(float normalized);
float ts_sister_fallout_transition_normalized(float milliseconds);
float ts_sister_fallout_lfo_hz(float normalized);
float ts_sister_fallout_lfo_normalized(float hz);
float ts_sister_fallout_rise_seconds(float normalized);
float ts_sister_fallout_rise_normalized(float seconds);
float ts_sister_fallout_lfo_modulate(float center, float intensity,
                                     float sine_value);
float ts_sister_fallout_rise_modulate(float start, float intensity,
                                      float ramp_value);
int ts_sister_fallout_init(TsSisterFalloutEngine *engine,
                           uint32_t sample_rate);
int ts_sister_fallout_reconfigure(TsSisterFalloutEngine *engine,
                                  uint32_t sample_rate);
void ts_sister_fallout_free(TsSisterFalloutEngine *engine);
void ts_sister_fallout_clear(TsSisterFalloutEngine *engine);
void ts_sister_fallout_seed(TsSisterFalloutEngine *engine, uint32_t seed);
void ts_sister_fallout_set_controls(
    TsSisterFalloutEngine *engine, const TsSisterFalloutControls *controls);
void ts_sister_fallout_sync_controls(
    TsSisterFalloutEngine *engine, const TsSisterFalloutControls *controls);
void ts_sister_fallout_recall_preset(
    TsSisterFalloutEngine *engine, const TsSisterFalloutControls *controls);
TsSisterFalloutResult ts_sister_fallout_process(
    TsSisterFalloutEngine *engine, TsStereoFrame input);
size_t ts_sister_fallout_memory_bytes(const TsSisterFalloutEngine *engine);
float ts_sister_fallout_engage(const TsSisterFalloutEngine *engine);
float ts_sister_fallout_feedback_amount(const TsSisterFalloutEngine *engine);
TsSisterFalloutTransitionStatus ts_sister_fallout_component_transition_status(
    const TsSisterFalloutEngine *engine);
TsSisterFalloutTransitionStatus ts_sister_fallout_master_transition_status(
    const TsSisterFalloutEngine *engine);
float ts_sister_fallout_component_transition_progress(
    const TsSisterFalloutEngine *engine, int *active);
float ts_sister_fallout_preset_transition_progress(
    const TsSisterFalloutEngine *engine, int *active);

#endif
