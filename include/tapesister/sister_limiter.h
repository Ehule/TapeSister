#ifndef TAPESISTER_SISTER_LIMITER_H
#define TAPESISTER_SISTER_LIMITER_H

#include "tapesister/sample.h"

#include <stddef.h>
#include <stdint.h>

enum {
    TS_SISTER_LIMITER_DEFAULT_ENABLED = 1
};

#define TS_SISTER_LIMITER_DEFAULT_CEILING_DB (-1.0f)
#define TS_SISTER_LIMITER_DEFAULT_LOOKAHEAD_MS (1.0f)
#define TS_SISTER_LIMITER_DEFAULT_RELEASE_MS (120.0f)
#define TS_SISTER_LIMITER_CEILING_DB_MIN (-12.0f)
#define TS_SISTER_LIMITER_CEILING_DB_MAX (0.0f)
#define TS_SISTER_LIMITER_LOOKAHEAD_MS_MIN (0.1f)
#define TS_SISTER_LIMITER_LOOKAHEAD_MS_MAX (10.0f)
#define TS_SISTER_LIMITER_RELEASE_MS_MIN (10.0f)
#define TS_SISTER_LIMITER_RELEASE_MS_MAX (2000.0f)

typedef struct {
    TsStereoFrame *delay;
    size_t delay_frames;
    size_t delay_position;
    uint32_t sample_rate;
    uint64_t processed_frames;
    uint64_t hold_until_frame;
    float ceiling_db;
    float ceiling_linear;
    float lookahead_ms;
    float release_ms;
    float release_coefficient;
    float gain;
    float applied_gain;
    float enabled_mix;
    float enabled_step;
    uint32_t enabled_ramp_remaining;
    int enabled;
    int ready;
} TsSisterLimiter;

void ts_sister_limiter_init(TsSisterLimiter *limiter);
void ts_sister_limiter_free(TsSisterLimiter *limiter);
int ts_sister_limiter_reconfigure(TsSisterLimiter *limiter,
                                  uint32_t sample_rate);
void ts_sister_limiter_set_controls(TsSisterLimiter *limiter, int enabled,
                                    float ceiling_db, float lookahead_ms,
                                    float release_ms);
void ts_sister_limiter_set_enabled(TsSisterLimiter *limiter, int enabled);
float ts_sister_limiter_gain_reduction_db(const TsSisterLimiter *limiter);
TsStereoFrame ts_sister_limiter_process(TsSisterLimiter *limiter,
                                        TsStereoFrame input,
                                        float *gain_reduction_db,
                                        float *input_peak);

#endif
