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
    TsSisterReverbType reverb_type;
    float reverb_mix;
    float reverb_decay;
    uint8_t reverb_targets;
    float delay_time;
    float delay_feedback;
    float delay_mix;
    uint8_t delay_targets;
    float distortion_drive;
    float distortion_tone;
    float distortion_mix;
    uint8_t distortion_targets;
    float master_feedback;
    TsSisterFalloutControls fallout;
} TsSisterFxControls;

enum {
    TS_SISTER_REVERB_LINES = 4
};

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
    float spring_state[2];
    float spring_previous[2];
    TsSisterReverbType type;
    TsSisterReverbType old_type;
    uint32_t sample_rate;
    float mix_current;
    float decay_current;
    float route_current;
    float type_blend;
    int has_history;
} TsSisterReverbState;

typedef struct {
    float *data;
    size_t capacity_frames;
    size_t write_index;
    float delay_current;
    float delay_old;
    float delay_target;
    uint32_t transition_remaining;
    uint32_t transition_total;
    float feedback_current;
    float mix_current;
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
    float route_current;
} TsSisterDistortionState;

typedef struct {
    uint8_t active_mask;
    uint8_t pending_mask;
    uint32_t handoff_remaining;
} TsSisterFxTargetState;

typedef struct {
    TsSisterReverbState reverb[TS_SISTER_EFFECT_PROCESSOR_COUNT];
    TsSisterDelayState delay[TS_SISTER_EFFECT_PROCESSOR_COUNT];
    TsSisterDistortionState distortion[TS_SISTER_EFFECT_PROCESSOR_COUNT];
    TsSisterFxControls controls;
    TsSisterFxTargetState reverb_target;
    TsSisterFxTargetState delay_target;
    TsSisterFxTargetState distortion_target;
    uint32_t sample_rate;
    int ready;
} TsSisterPostFxEngine;

const char *ts_sister_reverb_type_name(TsSisterReverbType type);
void ts_sister_fx_controls_default(TsSisterFxControls *controls);
void ts_sister_fx_controls_sanitize(TsSisterFxControls *controls);
float ts_sister_delay_time_ms(float normalized);
float ts_sister_reverb_decay_seconds(TsSisterReverbType type,
                                     float normalized);

int ts_sister_post_fx_init(TsSisterPostFxEngine *engine,
                           uint32_t sample_rate);
void ts_sister_post_fx_free(TsSisterPostFxEngine *engine);
int ts_sister_post_fx_reconfigure(TsSisterPostFxEngine *engine,
                                  uint32_t sample_rate);
void ts_sister_post_fx_set_controls(TsSisterPostFxEngine *engine,
                                    const TsSisterFxControls *controls);
TsStereoFrame ts_sister_post_fx_process(TsSisterPostFxEngine *engine,
                                        size_t target_index,
                                        TsStereoFrame input,
                                        int explicit_mono);
size_t ts_sister_post_fx_memory_bytes(const TsSisterPostFxEngine *engine);

#endif
