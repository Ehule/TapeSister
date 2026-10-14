#ifndef TAPESISTER_MASTER_EQ_H
#define TAPESISTER_MASTER_EQ_H
#include "tapesister/sample.h"
#include <stdio.h>
#include "tapesister/eq_types.h"
typedef struct { double b0,b1,b2,a1,a2; } TsEqCoefficients;
typedef struct { TsEqCoefficients c; double z1[2],z2[2]; } TsEqFilter;
typedef struct {
    TsEqFilter current, next;
    TsEqCoefficients pending;
    unsigned fade, dirty, warm;
} TsEqStage;
/* Saved A/B setup; playback phase is deliberately not persisted. */
#define TS_EQ_MORPH_DEFAULT_MAX_SECONDS 240.0
#define TS_EQ_MORPH_MAX_SECONDS 14400.0
#define TS_EQ_MORPH_MIN_SECONDS .1
typedef struct {
    TsMasterEqControls state[2];
    unsigned captured;
    int loop;
    double seconds;
} TsEqMorph;
typedef struct {
    /* controls is the last explicit edit, live is the audible curve. Keeping
       them separate prevents the automation clock from dirtying the project. */
    TsMasterEqControls controls;
    TsMasterEqControls live;
    TsEqMorph morph;
    int morph_running, morph_reverse;
    double morph_position, morph_duration;
    uint64_t morph_frames, morph_total_frames;
    unsigned morph_tick;
    TsEqStage stage[TS_MASTER_EQ_BANDS];
    unsigned sample_rate, fade_frames, active_stages;
    double mix;
} TsMasterEq;

void ts_master_eq_default(TsMasterEqControls *c);
enum { TS_EQ_PRESET_FLAT, TS_EQ_PRESET_RUMBLE_CUT, TS_EQ_PRESET_LESS_BOOM,
       TS_EQ_PRESET_WARM, TS_EQ_PRESET_PRESENCE, TS_EQ_PRESET_AIR,
       TS_EQ_PRESET_SOFTEN_HIGHS, TS_EQ_PRESET_TELEPHONE, TS_EQ_PRESET_COUNT };
const char *ts_master_eq_preset_name(int preset);
int ts_master_eq_preset(TsMasterEqControls *c, int preset);
/* Matches band settings, independently of global bypass or temporary solo. */
int ts_master_eq_preset_match(const TsMasterEqControls *c);
void ts_master_eq_sanitize(TsMasterEqControls *c);
int ts_master_eq_band_active(const TsMasterEqControls *c, int band);
void ts_master_eq_toggle_band(TsMasterEqControls *c, int band);
void ts_master_eq_init(TsMasterEq *eq);
/* UI/device thread, with the output device locked. Never allocate. */
void ts_master_eq_prepare(TsMasterEq *eq, unsigned rate);
void ts_master_eq_set(TsMasterEq *eq, const TsMasterEqControls *c);
void ts_eq_morph_default(TsEqMorph *m);
void ts_eq_morph_sanitize(TsEqMorph *m);
void ts_master_eq_morph_set(TsMasterEq *eq, const TsEqMorph *m);
void ts_master_eq_morph_capture(TsMasterEq *eq, int state);
int ts_master_eq_morph_recall(TsMasterEq *eq, int state);
int ts_master_eq_morph_start(TsMasterEq *eq);
void ts_master_eq_morph_stop(TsMasterEq *eq);
/* Audio thread; no allocation, lock, or wall clock. */
int ts_eq_morph_write(FILE *file, const TsEqMorph *m);
int ts_eq_morph_read(TsEqMorph *m, const char *key, const char *value);
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
