#ifndef TAPESISTER_SISTER_EFFECTS_H
#define TAPESISTER_SISTER_EFFECTS_H

#include "tapesister/sample.h"

#include <stddef.h>
#include <stdint.h>

enum {
    TS_SISTER_EFFECT_TARGET_H1 = 1u << 0,
    TS_SISTER_EFFECT_TARGET_H2 = 1u << 1,
    TS_SISTER_EFFECT_TARGET_H3 = 1u << 2,
    TS_SISTER_EFFECT_TARGET_MIX = 1u << 3,
    TS_SISTER_EFFECT_TARGET_HEADS = TS_SISTER_EFFECT_TARGET_H1 |
                                     TS_SISTER_EFFECT_TARGET_H2 |
                                     TS_SISTER_EFFECT_TARGET_H3,
    TS_SISTER_EFFECT_TARGET_ALL = TS_SISTER_EFFECT_TARGET_HEADS |
                                   TS_SISTER_EFFECT_TARGET_MIX,
    TS_SISTER_EFFECT_PROCESSOR_COUNT = 4
};

#define TS_SISTER_WEAVE_DELAY_MIN_MS 0.75f
#define TS_SISTER_WEAVE_DELAY_MAX_MS 18.0f
#define TS_SISTER_WEAVE_RATE_MIN_HZ 0.003f
#define TS_SISTER_WEAVE_RATE_MAX_HZ 3.0f

/* Generic PR8 target policy shared by Soak/Bleed and PR9 effects. */
uint8_t ts_sister_effect_targets_sanitize(uint8_t mask);
uint8_t ts_sister_effect_targets_toggle(uint8_t mask, uint8_t target);
int ts_sister_effect_target_enabled(uint8_t mask, uint8_t target);

float ts_sister_weave_rate_hz(float bleed);

typedef struct {
    float *delay_l;
    float *delay_r;
    size_t delay_frames;
    size_t write_index;
    uint32_t sample_rate;
    double phase;
    double phase_offset;
    float soak_current;
    float soak_target;
    float rate_current_hz;
    float rate_target_hz;
    float route_current;
    float route_target;
} TsSisterWeaveState;

int ts_sister_weave_init(TsSisterWeaveState *state, uint32_t sample_rate,
                         double phase_offset_cycles);
void ts_sister_weave_free(TsSisterWeaveState *state);
void ts_sister_weave_reset(TsSisterWeaveState *state);
void ts_sister_weave_set(TsSisterWeaveState *state, float soak, float bleed,
                         int routed);
TsStereoFrame ts_sister_weave_process(TsSisterWeaveState *state,
                                      TsStereoFrame input,
                                      int explicit_mono);

#endif
