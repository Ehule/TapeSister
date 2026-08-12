#ifndef TAPESISTER_SAMPLE_H
#define TAPESISTER_SAMPLE_H

#include <stddef.h>
#include <stdint.h>

enum { TS_HISTORY_DEPTH = 24, TS_SAMPLE_EDIT_DEPTH = 64 };

typedef struct {
    float *data;
    size_t frames;
    uint32_t sample_rate;
    char name[128];
} TsSample;

typedef enum {
    TS_GENERATOR_TONAL = 0,
    TS_GENERATOR_METALLIC,
    TS_GENERATOR_NOISE,
    TS_GENERATOR_PULSE,
    TS_GENERATOR_COUNT
} TsGeneratorKind;

typedef enum {
    TS_SOURCE_NONE = 0,
    TS_SOURCE_GENERATED,
    TS_SOURCE_IMPORTED,
    TS_SOURCE_COMMITTED
} TsSourceKind;

typedef enum {
    TS_NOISE_WHITE = 0,
    TS_NOISE_PINK,
    TS_NOISE_BROWN,
    TS_NOISE_METALLIC,
    TS_NOISE_COLOR_COUNT
} TsNoiseColor;

typedef struct {
    uint32_t seed;
    TsGeneratorKind kind;
    float seconds;
    float frequency;
} TsGeneratorRecipe;

typedef struct {
    uint32_t seed;
    float body;
    float edge;
    float drift;
    int noise_enabled;
    float noise_amount;
    TsNoiseColor noise_color;
    int delay_enabled;
    float delay_seconds;
    float delay_feedback;
    float delay_damping;
    float delay_mix;
    int reverb_enabled;
    float reverb_decay;
    float reverb_damping;
    float reverb_mix;
} TsProcessRecipe;

typedef enum {
    TS_SAMPLE_EDIT_REVERSE = 0,
    TS_SAMPLE_EDIT_NORMALIZE,
    TS_SAMPLE_EDIT_GAIN,
    TS_SAMPLE_EDIT_FADE_IN,
    TS_SAMPLE_EDIT_FADE_OUT
} TsSampleEditKind;

typedef struct {
    TsSampleEditKind kind;
    size_t first;
    size_t last;
    float amount;
} TsSampleEdit;

typedef struct {
    size_t crop_first;
    size_t crop_last;
    size_t selection_first;
    size_t selection_last;
    size_t view_first;
    size_t view_last;
    size_t loop_first;
    size_t loop_last;
    float loop_crossfade_ms;
    int has_selection;
    int has_loop;
    TsProcessRecipe process;
    TsSampleEdit sample_edits[TS_SAMPLE_EDIT_DEPTH];
    int sample_edit_count;
} TsEditSnapshot;

typedef struct {
    TsSample parent;
    TsSample current;
    TsSourceKind source_kind;
    TsGeneratorRecipe generator;
    TsProcessRecipe process;
    uint32_t generation;
    uint64_t ancestor_hash;
    size_t crop_first;
    size_t crop_last;
    size_t selection_first;
    size_t selection_last;
    size_t view_first;
    size_t view_last;
    size_t loop_first;
    size_t loop_last;
    float loop_crossfade_ms;
    int has_selection;
    int has_loop;
    TsSampleEdit sample_edits[TS_SAMPLE_EDIT_DEPTH];
    int sample_edit_count;
    TsEditSnapshot undo[TS_HISTORY_DEPTH];
    TsEditSnapshot redo[TS_HISTORY_DEPTH];
    int undo_count;
    int redo_count;
} TsInstrument;

void ts_sample_init(TsSample *sample);
void ts_sample_free(TsSample *sample);
int ts_sample_clone(TsSample *destination, const TsSample *source, char *error, size_t error_size);
int ts_sample_load_wav(TsSample *sample, const char *path, char *error, size_t error_size);
int ts_sample_save_wav16(const TsSample *sample, const char *path, char *error, size_t error_size);
int ts_sample_generate(TsSample *sample, const TsGeneratorRecipe *recipe, char *error, size_t error_size);
int ts_sample_process(TsSample *sample, const TsSample *parent, size_t first, size_t last,
                      const TsProcessRecipe *recipe, char *error, size_t error_size);
float ts_sample_peak(const TsSample *sample);
uint64_t ts_sample_hash(const TsSample *sample);

void ts_instrument_init(TsInstrument *instrument);
void ts_instrument_free(TsInstrument *instrument);
int ts_instrument_generate(TsInstrument *instrument, TsGeneratorKind kind, uint32_t seed,
                           char *error, size_t error_size);
int ts_instrument_load_wav(TsInstrument *instrument, const char *path,
                           char *error, size_t error_size);
int ts_instrument_reseed(TsInstrument *instrument, char *error, size_t error_size);
int ts_instrument_set_process(TsInstrument *instrument, const TsProcessRecipe *process,
                              char *error, size_t error_size);
int ts_instrument_reset_current(TsInstrument *instrument, char *error, size_t error_size);
int ts_instrument_commit_current(TsInstrument *instrument, char *error, size_t error_size);
void ts_instrument_set_selection(TsInstrument *instrument, size_t first, size_t last);
void ts_instrument_set_selection_snapped(TsInstrument *instrument, size_t first, size_t last);
void ts_instrument_clear_selection(TsInstrument *instrument);
size_t ts_sample_nearest_zero_crossing(const TsSample *sample, size_t frame);
int ts_instrument_set_loop_from_selection(TsInstrument *instrument,
                                          char *error, size_t error_size);
int ts_instrument_clear_loop(TsInstrument *instrument, char *error, size_t error_size);
int ts_instrument_set_loop_crossfade(TsInstrument *instrument, float milliseconds,
                                     char *error, size_t error_size);
int ts_instrument_crop_selection(TsInstrument *instrument, char *error, size_t error_size);
int ts_instrument_apply_sample_edit(TsInstrument *instrument, TsSampleEditKind kind,
                                    float amount, char *error, size_t error_size);
int ts_instrument_zoom_selection(TsInstrument *instrument);
int ts_instrument_zoom_view(TsInstrument *instrument, size_t anchor_frame,
                            float anchor_ratio, float scale);
int ts_instrument_pan_view(TsInstrument *instrument, ptrdiff_t frames);
void ts_instrument_show_all(TsInstrument *instrument);
int ts_instrument_undo(TsInstrument *instrument, char *error, size_t error_size);
int ts_instrument_redo(TsInstrument *instrument, char *error, size_t error_size);
size_t ts_instrument_frame_from_view_x(const TsInstrument *instrument, int x, int width);
const char *ts_generator_name(TsGeneratorKind kind);
const char *ts_noise_color_name(TsNoiseColor color);
const char *ts_sample_edit_name(TsSampleEditKind kind);
void ts_process_recipe_reset(TsProcessRecipe *process);
int ts_instrument_save_recipe(const TsInstrument *instrument, const char *path,
                              char *error, size_t error_size);
int ts_instrument_load_recipe(TsInstrument *instrument, const char *path,
                              char *error, size_t error_size);
void ts_instrument_begin_loop_drag(TsInstrument *instrument);
int ts_instrument_move_loop_endpoint(TsInstrument *instrument, int endpoint, size_t frame);

#endif
