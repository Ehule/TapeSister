#ifndef TAPESISTER_PRISM_H
#define TAPESISTER_PRISM_H

#include "tapesister/sample.h"
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

enum { TS_PRISM_BASE_LENSES = 12, TS_PRISM_LENSES = 24, TS_PRISM_HOP = 64,
       TS_PRISM_STATES = 12 };
typedef enum { TS_PRISM_SUPERSAW, TS_PRISM_ENSEMBLE, TS_PRISM_HARMONIC, TS_PRISM_FIFTHS,
    TS_PRISM_OCTAVES, TS_PRISM_CLUSTER, TS_PRISM_MICRO, TS_PRISM_MODE_COUNT } TsPrismMode;
typedef enum {
    TS_PRISM_BICONVEX, TS_PRISM_PLANO_CONVEX, TS_PRISM_MENISCUS_POSITIVE,
    TS_PRISM_BICONCAVE, TS_PRISM_PLANO_CONCAVE, TS_PRISM_MENISCUS_NEGATIVE,
    TS_PRISM_SHAPE_COUNT
} TsPrismShape;
#define TS_PRISM_PATCH_FIELDS \
int enabled, mode, lenses; \
float spread, drift, focus, stereo, body, mix, output_db; \
float pitch_offset[TS_PRISM_LENSES], pan_offset[TS_PRISM_LENSES]; \
float trim_db[TS_PRISM_LENSES], dry_level; \
int mute_mask, solo_mask; \
int input_shape, output_shape; \
float color; \
float drift_rate; \
int group_octave, octave_offset[TS_PRISM_LENSES], snap;
typedef struct { TS_PRISM_PATCH_FIELDS } TsPrismPatch;
enum { TS_PRISM_LOCK_PITCH=1, TS_PRISM_LOCK_MOTION=2, TS_PRISM_LOCK_GLASS=4,
       TS_PRISM_LOCK_MIX=8, TS_PRISM_LOCK_COUNT=16, TS_PRISM_LOCK_SEQUENCE=32 };
typedef struct {
    TS_PRISM_PATCH_FIELDS
    TsPrismPatch a, b;
    int captured, morph_enabled;
    float morph, morph_seconds;
    int morph_target;
    uint32_t morph_trigger;
    int seq_enabled, seq_count, sequence[TS_PRISM_LENSES];
    float seq_rate;
    uint32_t seq_reset, locks;
    TsPrismPatch extra[TS_PRISM_STATES-2]; /* C-L; legacy A/B fields stay intact. */
    int endpoint[2]; /* Bank letters assigned to the two ends of the fader. */
} TsPrismControls;
#undef TS_PRISM_PATCH_FIELDS

/* Capture/recall touches Prism only. Endpoint captures exclude the sequence. */
void ts_prism_capture(TsPrismControls *controls, int endpoint);
void ts_prism_recall(TsPrismControls *controls, int endpoint);
TsPrismPatch *ts_prism_state(TsPrismControls *controls, int state);
const TsPrismPatch *ts_prism_state_const(const TsPrismControls *controls, int state);
int ts_prism_pair_ready(const TsPrismControls *controls);
void ts_prism_copy_bank(TsPrismControls *destination, const TsPrismControls *source);
void ts_prism_sequence_toggle(TsPrismControls *controls, int lens);
void ts_prism_generate(TsPrismControls *controls, uint32_t seed, int vary);
float ts_prism_snap_pitch(float cents, int snap);
const char *ts_prism_snap_name(int snap);
/* Shared versioned text fields for Prism banks and Sister project/preset files. */
void ts_prism_write_extensions(FILE *file, const TsPrismControls *controls);
void ts_prism_write(FILE *file, const TsPrismControls *controls);
int ts_prism_read_field(TsPrismControls *controls, const char *key, const char *value);
enum { TS_PRISM_PRESETS = 32 };
typedef struct { char name[32]; TsPrismControls controls; } TsPrismPreset;
typedef struct { unsigned count; TsPrismPreset entries[TS_PRISM_PRESETS]; } TsPrismBank;
int ts_prism_bank_save(const TsPrismBank *bank, const char *path, char *error, size_t size);
int ts_prism_bank_load(TsPrismBank *bank, const char *path, char *error, size_t size);


/* The first BASE_LENSES entries also serve the unchanged FM Unison template. */
extern const float ts_prism_unison_cents[TS_PRISM_LENSES];

typedef struct {
    TsStereoFrame low, allpass_memory;
    float low_coefficient, allpass_coefficient;
} TsPrismGlass;
typedef struct {
    float cents, delay_ms, pan, level;
    float refraction_cents, refraction_pan; /* Before the output shape mapping. */
} TsPrismLensView;
typedef struct {
    TsPrismLensView lens[TS_PRISM_LENSES];
    float wet, dry;
    int valid;
    float morph;
    int seq_lens; /* One-based sounding step; zero means no eligible step. */
} TsPrismView;
typedef struct {
    double phase, previous_phase;
    double ratio, ratio_target;
    double refraction_ratio, refraction_ratio_target;
    float refraction_pan, refraction_pan_target;
    float delay, delay_target;
    float level, level_target, pan, pan_target;
    float weight, weight_target; /* Nominal energy reference, before manual mix. */
    TsPrismGlass glass[2];
} TsPrismLens;
typedef struct {
    TsStereoFrame *history;
    size_t capacity, write;
    uint32_t sample_rate;
    uint64_t clock;
    double window_frames, window_target, previous_window;
    float window_fade, window_fade_step;
    float pitch_smoothing;
    float smoothing, wet, gain, gain_target;
    double dry;
    double drift_time, sequence_phase;
    float morph_position, morph_start, morph_elapsed;
    uint32_t morph_seen, seq_seen;
    int seq_lens;
    float body_shift, wet_target, dry_target, glass_target[2][TS_PRISM_SHAPE_COUNT];
    float glass_mix[2][TS_PRISM_SHAPE_COUNT];
    float period_difference[260];
    float hann[1025];
    TsPrismControls controls;
    TsPrismLens lens[TS_PRISM_LENSES];
} TsPrism;

void ts_prism_controls_default(TsPrismControls *controls);
void ts_prism_controls_sanitize(TsPrismControls *controls);
const char *ts_prism_mode_name(int mode);
const char *ts_prism_shape_name(int shape);
const char *ts_prism_shape_color_name(int shape);
void ts_prism_reset_lenses(TsPrismControls *controls);
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
