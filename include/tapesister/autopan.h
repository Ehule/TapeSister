#ifndef TAPESISTER_AUTOPAN_H
#define TAPESISTER_AUTOPAN_H
#include "tapesister/sample.h"
#include <stdint.h>
typedef enum {
    TS_AUTOPAN_SINE, TS_AUTOPAN_TRIANGLE, TS_AUTOPAN_SQUARE,
    TS_AUTOPAN_SAW, TS_AUTOPAN_RANDOM_STEP, TS_AUTOPAN_RANDOM_SMOOTH
} TsAutoPanWave;
typedef enum { TS_AUTOPAN_BALANCE, TS_AUTOPAN_DUAL_TREMOLO } TsAutoPanMode;
typedef struct {
    float rate_hz;       /* 0.01..100 Hz */
    float depth;         /* 0..1 */
    float center;        /* -1..1 */
    float phase_offset;  /* 0..1 cycle, dual tremolo only */
    TsAutoPanWave wave;
    TsAutoPanMode mode;
} TsAutoPanControls;
typedef struct {
    TsAutoPanControls controls;
    double phase,step;
    float random_a,random_b;
    uint32_t random_state;
    unsigned sample_rate;
} TsAutoPan;
void ts_autopan_default(TsAutoPanControls *controls);
void ts_autopan_init(TsAutoPan *pan,unsigned sample_rate,uint32_t seed);
void ts_autopan_set(TsAutoPan *pan,const TsAutoPanControls *controls);
void ts_autopan_reset_phase(TsAutoPan *pan);
void ts_autopan_process_span(TsAutoPan *pan,const TsStereoFrame *in,TsStereoFrame *out,unsigned frames);
#endif
