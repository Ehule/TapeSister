#ifndef TAPESISTER_MASTER_EQ_H
#define TAPESISTER_MASTER_EQ_H
#include "tapesister/sample.h"
#include <stdio.h>
#define TS_MASTER_EQ_BANDS 5
#define TS_MASTER_EQ_MIN_HZ 20.0f
#define TS_MASTER_EQ_MAX_HZ 20000.0f
#define TS_MASTER_EQ_MIN_Q .3f
#define TS_MASTER_EQ_MAX_Q 8.0f

typedef enum { TS_EQ_BELL, TS_EQ_LOW_SHELF, TS_EQ_HIGH_SHELF,
    TS_EQ_HIGH_PASS, TS_EQ_LOW_PASS, TS_EQ_NOTCH, TS_EQ_TYPE_COUNT } TsEqType;
typedef struct { int enabled, type; float frequency, gain_db, q; } TsEqBand;
typedef struct {
    int enabled;
    TsEqBand band[TS_MASTER_EQ_BANDS];
    int solo_band; /* 0: all enabled bands; 1..5: only that band, retaining bypass states. */
} TsMasterEqControls;
typedef struct { double b0,b1,b2,a1,a2; } TsEqCoefficients;
typedef struct { TsEqCoefficients c; double z1[2],z2[2]; } TsEqFilter;
typedef struct {
    TsEqFilter current, next;
    TsEqCoefficients pending;
    unsigned fade, dirty;
} TsEqStage;
typedef struct {
    TsMasterEqControls controls;
    TsEqStage stage[TS_MASTER_EQ_BANDS];
    unsigned sample_rate, fade_frames;
    double mix;
} TsMasterEq;

void ts_master_eq_default(TsMasterEqControls *c);
void ts_master_eq_sanitize(TsMasterEqControls *c);
int ts_master_eq_band_active(const TsMasterEqControls *c, int band);
void ts_master_eq_toggle_band(TsMasterEqControls *c, int band);
void ts_master_eq_init(TsMasterEq *eq);
/* UI/device thread, with the output device locked. Never allocate. */
void ts_master_eq_prepare(TsMasterEq *eq, unsigned rate);
void ts_master_eq_set(TsMasterEq *eq, const TsMasterEqControls *c);
TsStereoFrame ts_master_eq_process(TsMasterEq *eq, TsStereoFrame input);
TsEqCoefficients ts_master_eq_coefficients(TsEqBand band, unsigned rate);
double ts_master_eq_response_db(const TsMasterEqControls *c, unsigned rate, double hz);
float ts_master_eq_max_hz(unsigned rate);
const char *ts_master_eq_type_name(int type);
int ts_master_eq_has_gain(int type);
/* Frequency / Gain / Q normalized controls shared by UI and MIDI. */
float ts_master_eq_normalized(const TsEqBand *band, int control, unsigned rate);
void ts_master_eq_set_normalized(TsEqBand *band, int control, float value, unsigned rate);
/* Shared explicit text extension for INI/session and project sidecars. */
int ts_master_eq_write(FILE *file, const TsMasterEqControls *c);
int ts_master_eq_read(TsMasterEqControls *c, const char *key, const char *value);
#endif
