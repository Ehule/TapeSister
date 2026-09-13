#ifndef TAPESISTER_PRISM_H
#define TAPESISTER_PRISM_H

#include "tapesister/sample.h"
#include <stddef.h>
#include <stdint.h>

enum { TS_PRISM_LENSES = 12, TS_PRISM_HOP = 64 };
typedef enum { TS_PRISM_SUPERSAW, TS_PRISM_ENSEMBLE, TS_PRISM_MODE_COUNT } TsPrismMode;
typedef struct {
    int enabled, mode, lenses;
    float spread, drift, focus, stereo, body, mix, output_db;
    /* Additive per-lens edits; lens zero remains the direct body anchor. */
    float pitch_offset[TS_PRISM_LENSES], pan_offset[TS_PRISM_LENSES];
} TsPrismControls;

/* Shared voicing, also used by the original FM Unison template. */
extern const float ts_prism_unison_cents[TS_PRISM_LENSES];

typedef struct { float cents, delay_ms, pan, level; } TsPrismLensView;
typedef struct {
    TsPrismLensView lens[TS_PRISM_LENSES];
    float wet;
    int valid;
} TsPrismView;
typedef struct {
    double phase, previous_phase;
    double ratio, ratio_target;
    float delay, delay_target;
    float level, level_target, pan, pan_target;
} TsPrismLens;
typedef struct {
    TsStereoFrame *history;
    size_t capacity, write;
    uint32_t sample_rate;
    uint64_t clock;
    double window_frames, window_target, previous_window;
    float window_fade, window_fade_step;
    float smoothing, wet, gain, gain_target;
    float period_difference[260];
    float hann[1025];
    TsPrismControls controls;
    TsPrismLens lens[TS_PRISM_LENSES];
} TsPrism;

void ts_prism_controls_default(TsPrismControls *controls);
void ts_prism_controls_sanitize(TsPrismControls *controls);
const char *ts_prism_mode_name(int mode);
/* Setup/free occur with the audio device paused, never in process(). */
int ts_prism_prepare(TsPrism *prism, uint32_t sample_rate);
void ts_prism_free(TsPrism *prism);
void ts_prism_set_controls(TsPrism *prism, const TsPrismControls *controls);
TsStereoFrame ts_prism_process(TsPrism *prism, TsStereoFrame input);
/* Audio owner only; the runtime publishes this through its atomic snapshot. */
TsPrismView ts_prism_view(const TsPrism *prism);
/* Static control diagram when no device is running. No invented animation. */
TsPrismView ts_prism_control_view(const TsPrismControls *controls);

#endif
