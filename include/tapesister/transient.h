#ifndef TAPESISTER_TRANSIENT_H
#define TAPESISTER_TRANSIENT_H

#include "tapesister/sample.h"

/* Independent, stereo-linked envelope shaping. No audio delay, allocation,
   threshold, or shared history. response scales the detector's time constants. */
typedef struct { float attack_db, sustain_db, response; } TsTransientControls;
typedef struct {
    TsTransientControls controls;
    unsigned rate;
    double smooth, attack_db, sustain_db;
    double coefficient[5], target_coefficient[5];
    double attack_fast, attack_slow, sustain_fast, sustain_slow;
    float attack_activity, sustain_activity, gain_db;
} TsTransient;

void ts_transient_init(TsTransient *state, unsigned sample_rate);
void ts_transient_set(TsTransient *state, TsTransientControls controls);
/* Recall before processing; does not turn a restored value into a live edit. */
void ts_transient_sync(TsTransient *state, TsTransientControls controls);
TsStereoFrame ts_transient_process(TsTransient *state, TsStereoFrame input);

#endif
