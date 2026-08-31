#ifndef TAPESISTER_SISTER_POST_FX_H
#define TAPESISTER_SISTER_POST_FX_H

#include "tapesister/sample.h"
#include "tapesister/sister_effects.h"
#include "tapesister/sister_fallout.h"

#include <stddef.h>
#include <stdint.h>

typedef enum {
    TS_SISTER_REVERB_HALL = 0,
    TS_SISTER_REVERB_PLATE,
    TS_SISTER_REVERB_SPRING,
    TS_SISTER_REVERB_CATHEDRAL,
    TS_SISTER_REVERB_TYPE_COUNT
} TsSisterReverbType;

typedef struct {
    int enabled;
    int reverb_enabled;
    int delay_enabled;
    int distortion_enabled;
    int grain_enabled;
    /* Logarithmic 10 ms..60 min individual-effect switch ramp. */
    float transition;
    /* Independent logarithmic 10 ms..60 min Master FX gate ramp. */
    float master_transition;
    TsSisterReverbType reverb_type;
    /* Continuous room scale. reverb_type remains only for loading older
       presets and projects; the live instrument is one wide-range space. */
    float reverb_size;
    float reverb_mix;
    float reverb_decay;
    /* Post-mix makeup gain in decibels (-12..+12). */
    float reverb_gain_db;
    uint8_t reverb_targets;
    float delay_time;
    float delay_feedback;
    float delay_mix;
    float delay_gain_db;
    uint8_t delay_targets;
    float distortion_drive;
    float distortion_tone;
    float distortion_mix;
    float distortion_gain_db;
    uint8_t distortion_targets;
    float grain_size;
    float grain_density;
    float grain_pitch;
    float grain_mix;
    float grain_gain_db;
    uint8_t grain_targets;
    float master_feedback;
    TsSisterFalloutControls fallout;
} TsSisterFxControls;

enum {
    TS_SISTER_REVERB_LINES = 8,
    TS_SISTER_DELAY_TAPS = 4,
    TS_SISTER_GRAIN_VOICES = 24
};

typedef struct {
    TsStereoFrame previous;
    TsStereoFrame from;
    uint32_t remaining;
    uint32_t total;
    int initialized;
} TsSisterFxReadHandoff;

typedef struct {
    float *data;
    size_t capacity_frames;
    size_t write_index;
    float old_delay_frames;
    float new_delay_frames;
    uint32_t transition_remaining;
    uint32_t transition_total;
    float damping[2];
} TsSisterReverbLine;

typedef struct {
    TsSisterReverbLine line[TS_SISTER_REVERB_LINES];
    uint32_t sample_rate;
    float size_current;
    float mix_current;
    float decay_current;
    float gain_current;
    float gain_target;
    float route_current;
    float modulation_sin[TS_SISTER_REVERB_LINES];
    float modulation_cos[TS_SISTER_REVERB_LINES];
    float modulation_step_sin[TS_SISTER_REVERB_LINES];
    float modulation_step_cos[TS_SISTER_REVERB_LINES];
    uint32_t modulation_renormalize;
    int has_history;
    TsSisterFxReadHandoff read_handoff;
} TsSisterReverbState;

typedef struct {
    float *data;
    size_t capacity_frames;
    size_t write_index;
    float delay_current;
    float delay_target;
    float tap_tone[TS_SISTER_DELAY_TAPS][2];
    float feedback_tone[2];
    float wow_sin;
    float wow_cos;
    float wow_step_sin;
    float wow_step_cos;
    float flutter_sin;
    float flutter_cos;
    float flutter_step_sin;
    float flutter_step_cos;
    uint32_t modulation_renormalize;
    float follow_coefficient;
    float tap_alpha[TS_SISTER_DELAY_TAPS];
    float feedback_alpha_bright;
    float feedback_alpha_dark;
    float feedback_current;
    float mix_current;
    float gain_current;
    float gain_target;
    float route_current;
    int has_history;
} TsSisterDelayState;

typedef struct {
    float previous_input[2];
    float tone_state[2];
    float dc_x1[2];
    float dc_y1[2];
    float drive_current;
    float tone_current;
    float mix_current;
    float gain_current;
    float gain_target;
    float route_current;
} TsSisterDistortionState;

typedef struct {
    double read_position;
    double read_step;
    uint32_t age;
    uint32_t total;
    float pan_l;
    float pan_r;
    int active;
} TsSisterGrainVoice;

typedef struct {
    float *data;
    size_t capacity_frames;
    size_t write_index;
    size_t history_frames;
    TsSisterGrainVoice voice[TS_SISTER_GRAIN_VOICES];
    uint32_t sample_rate;
    uint32_t random_state;
    double spawn_phase;
    double spawn_threshold;
    float size_current;
    float density_current;
    float density_hz_current;
    float density_hz_target;
    float pitch_current;
    float mix_current;
    float gain_current;
    float gain_target;
    float route_current;
} TsSisterGrainState;

typedef struct {
    uint8_t active_mask;
    uint8_t pending_mask;
    uint32_t handoff_remaining;
} TsSisterFxTargetState;

typedef struct {
    float current;
    float target;
    float step;
    uint32_t remaining;
    uint32_t total;
} TsSisterFxRamp;

typedef enum {
    TS_SISTER_FX_TRANSITION_NONE = 0,
    TS_SISTER_FX_TRANSITION_MASTER,
    TS_SISTER_FX_TRANSITION_REVERB,
    TS_SISTER_FX_TRANSITION_DELAY,
    TS_SISTER_FX_TRANSITION_DISTORTION,
    TS_SISTER_FX_TRANSITION_GRAIN
} TsSisterFxTransitionSource;

typedef struct {
    float progress;
    TsSisterFxTransitionSource source;
    int target_enabled;
    int active;
} TsSisterFxTransitionStatus;

typedef struct {
    TsSisterReverbState reverb[TS_SISTER_EFFECT_PROCESSOR_COUNT];
    TsSisterDelayState delay[TS_SISTER_EFFECT_PROCESSOR_COUNT];
    TsSisterDistortionState distortion[TS_SISTER_EFFECT_PROCESSOR_COUNT];
    TsSisterGrainState grain[TS_SISTER_EFFECT_PROCESSOR_COUNT];
    TsSisterFxControls controls;
    TsSisterFxTargetState reverb_target;
    TsSisterFxTargetState delay_target;
    TsSisterFxTargetState distortion_target;
    TsSisterFxTargetState grain_target;
    TsSisterFxRamp master_engage;
    TsSisterFxRamp reverb_engage;
    TsSisterFxRamp delay_engage;
    TsSisterFxRamp distortion_engage;
    TsSisterFxRamp grain_engage;
    uint32_t sample_rate;
    int ready;
} TsSisterPostFxEngine;

const char *ts_sister_reverb_type_name(TsSisterReverbType type);
float ts_sister_reverb_legacy_size(TsSisterReverbType type);
void ts_sister_fx_controls_default(TsSisterFxControls *controls);
void ts_sister_fx_controls_sanitize(TsSisterFxControls *controls);
float ts_sister_delay_time_ms(float normalized);
float ts_sister_reverb_size_scale(float normalized);
float ts_sister_reverb_decay_seconds(float normalized);
float ts_sister_grain_size_ms(float normalized);
float ts_sister_grain_density_hz(float normalized);
float ts_sister_grain_pitch_semitones(float normalized);
float ts_sister_fx_transition_ms(float normalized);
float ts_sister_fx_transition_normalized(float milliseconds);
float ts_sister_post_fx_master_engage(const TsSisterPostFxEngine *engine);
TsSisterFxTransitionStatus ts_sister_post_fx_transition_status(
    const TsSisterPostFxEngine *engine);
TsSisterFxTransitionStatus ts_sister_post_fx_effect_transition_status(
    const TsSisterPostFxEngine *engine);
TsSisterFxTransitionStatus ts_sister_post_fx_master_transition_status(
    const TsSisterPostFxEngine *engine);
float ts_sister_post_fx_transition_progress(
    const TsSisterPostFxEngine *engine, int *active);

int ts_sister_post_fx_init(TsSisterPostFxEngine *engine,
                           uint32_t sample_rate);
void ts_sister_post_fx_free(TsSisterPostFxEngine *engine);
int ts_sister_post_fx_reconfigure(TsSisterPostFxEngine *engine,
                                  uint32_t sample_rate);
void ts_sister_post_fx_set_controls(TsSisterPostFxEngine *engine,
                                    const TsSisterFxControls *controls);
/* Applies a restored/cold-start state without manufacturing engage fades from
   the engine defaults. Live UI edits should continue to use set_controls(). */
void ts_sister_post_fx_sync_controls(TsSisterPostFxEngine *engine,
                                     const TsSisterFxControls *controls);
TsStereoFrame ts_sister_post_fx_process(TsSisterPostFxEngine *engine,
                                        size_t target_index,
                                        TsStereoFrame input,
                                        int explicit_mono);
size_t ts_sister_post_fx_memory_bytes(const TsSisterPostFxEngine *engine);

#endif
