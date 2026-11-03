#ifndef TAPESISTER_DARK_MATTER_H
#define TAPESISTER_DARK_MATTER_H

#include "tapesister/sample.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

enum { TS_DARK_MATTER_BANKS = 4, TS_SISTER_MEMORY_TAPE = 0, TS_SISTER_MEMORY_DARK = 1 };
/* Separate IDs/locks leave every existing MIDI target and preset bit intact. */
enum { TS_DARK_PARAM_MODE = 3000, TS_DARK_PARAM_FEEDBACK, TS_DARK_PARAM_BLUR,
       TS_DARK_PARAM_DRIFT, TS_DARK_PARAM_REPLACE, TS_DARK_PARAM_DRIVE,
       TS_DARK_PARAM_DAMP, TS_DARK_PARAM_REVERSE, TS_DARK_PARAM_END };
typedef struct {
    float feedback, blur, drift, replace, drive, damp;
    int reverse;
    unsigned locks;
} TsDarkMatterControls;
typedef struct {
    TsStereoFrame *data;
    size_t capacity, length, write;
    double phase, reverse_phase;
    uint32_t random;
    float chaos, chaos_target, pan, peak;
    TsStereoFrame low, last, handoff, dc_x, dc_y;
    uint32_t fade_remaining;
} TsDarkMatterBank;
typedef struct {
    TsStereoFrame *storage;
    size_t storage_frames;
    uint32_t sample_rate, clock, fade_frames;
    TsDarkMatterBank bank[TS_DARK_MATTER_BANKS];
    TsDarkMatterControls controls;
    float smooth[6], smoothing, reverse_mix, envelope;
    float attack, release, damp_coefficient, damp_target, dc_radius;
    TsStereoFrame dc_x, dc_y;
    float seconds;
} TsDarkMatter;

void ts_dark_matter_controls_default(TsDarkMatterControls *c);
void ts_dark_matter_controls_sanitize(TsDarkMatterControls *c);
int ts_dark_matter_controls_read(TsDarkMatterControls *c, const char *text);
int ts_dark_matter_controls_write(FILE *file, const char *key, const TsDarkMatterControls *c);
int ts_dark_matter_init(TsDarkMatter *d, uint32_t rate, float seconds);
void ts_dark_matter_free(TsDarkMatter *d);
void ts_dark_matter_clear(TsDarkMatter *d);
void ts_dark_matter_seed(TsDarkMatter *d, uint32_t seed);
/* Control thread only: calculates coefficients and bounded prime lengths. */
void ts_dark_matter_set(TsDarkMatter *d, const TsDarkMatterControls *c, float seconds);
/* All reads precede all writes. No allocation, locks, I/O or memory clearing. */
void ts_dark_matter_process(TsDarkMatter *d, TsStereoFrame input, int write_enabled,
                           int mono, TsStereoFrame heads[3]);
#endif
