#ifndef TAPESISTER_TEXTURE_FX_H
#define TAPESISTER_TEXTURE_FX_H
#include "tapesister/sample.h"
#include <stddef.h>
/* Eight stereo all-pass sections. Coefficients change on a bounded control
   clock; all audio histories and delay storage belong to this instance. */
typedef struct {
    double z[2][8][2], b0[2][8], b1[2][8];
    float a, b, c, mix, gain, smooth, gain_db, gain_target;
    unsigned clock, rate;
    int ready;
} TsDispersion;
typedef struct {
    float *data;
    size_t capacity, position;
    float damp[6][2], a, b, c, mix, gain, smooth, gain_db, gain_target;
    double delay[6];
    unsigned clock, rate;
    int ready;
} TsResonator;
void ts_dispersion_init(TsDispersion *, unsigned);
TsStereoFrame ts_dispersion_process(TsDispersion *, TsStereoFrame, float, float, float, float, float, float,
                                    float *);
int ts_resonator_init(TsResonator *, unsigned);
void ts_resonator_free(TsResonator *);
TsStereoFrame ts_resonator_process(TsResonator *, TsStereoFrame, float, float, float, float, float, float,
                                   float *);
float ts_resonator_note(float);
#endif
