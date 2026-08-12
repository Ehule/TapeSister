#ifndef TAPESISTER_SAMPLE_H
#define TAPESISTER_SAMPLE_H

#include <stddef.h>
#include <stdint.h>

enum { TS_HISTORY_DEPTH = 24 };

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
    TS_SOURCE_IMPORTED
} TsSourceKind;

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
} TsProcessRecipe;

typedef struct {
    size_t crop_first;
    size_t crop_last;
    size_t selection_first;
    size_t selection_last;
    size_t view_first;
    size_t view_last;
    int has_selection;
    TsProcessRecipe process;
} TsEditSnapshot;

typedef struct {
    TsSample parent;
    TsSample current;
    TsSourceKind source_kind;
    TsGeneratorRecipe generator;
    TsProcessRecipe process;
    size_t crop_first;
    size_t crop_last;
    size_t selection_first;
    size_t selection_last;
    size_t view_first;
    size_t view_last;
    int has_selection;
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
void ts_instrument_set_selection(TsInstrument *instrument, size_t first, size_t last);
void ts_instrument_clear_selection(TsInstrument *instrument);
int ts_instrument_crop_selection(TsInstrument *instrument, char *error, size_t error_size);
int ts_instrument_zoom_selection(TsInstrument *instrument);
void ts_instrument_show_all(TsInstrument *instrument);
int ts_instrument_undo(TsInstrument *instrument, char *error, size_t error_size);
int ts_instrument_redo(TsInstrument *instrument, char *error, size_t error_size);
size_t ts_instrument_frame_from_view_x(const TsInstrument *instrument, int x, int width);
const char *ts_generator_name(TsGeneratorKind kind);

#endif
