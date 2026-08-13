#ifndef TAPESISTER_SAMPLE_H
#define TAPESISTER_SAMPLE_H

#include <stddef.h>
#include <stdint.h>

enum {
    TS_HISTORY_DEPTH = 24,
    TS_SAMPLE_EDIT_DEPTH = 64,
    TS_POST_EDIT_DEPTH = 64,
    TS_BANK_SLOT_COUNT = 16,
    TS_KEYBOARD_BASE_NOTE = 48
};

typedef struct {
    int root_note;
    float fine_tune_cents;
} TsTuning;

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

typedef enum {
    TS_FILTER_LOWPASS = 0,
    TS_FILTER_HIGHPASS,
    TS_FILTER_BANDPASS,
    TS_FILTER_MODE_COUNT
} TsFilterMode;

typedef enum {
    TS_SHAPER_TAPE = 0,
    TS_SHAPER_CLIP,
    TS_SHAPER_FOLD,
    TS_SHAPER_MODE_COUNT
} TsShaperMode;

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
    int filter_enabled;
    TsFilterMode filter_mode;
    float filter_cutoff_hz;
    float filter_resonance;
    int shaper_enabled;
    TsShaperMode shaper_mode;
    float shaper_drive;
    float shaper_mix;
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

typedef enum {
    TS_LOOP_FORWARD = 0,
    TS_LOOP_REVERSE,
    TS_LOOP_PING_PONG,
    TS_LOOP_MODE_COUNT
} TsLoopMode;

typedef enum {
    TS_POST_COPY_MIX = 0,
    TS_POST_COPY_OVERWRITE,
    TS_POST_MOVE_MIX,
    TS_POST_MOVE_OVERWRITE,
    TS_POST_REVERSE,
    TS_POST_NORMALIZE,
    TS_POST_GAIN,
    TS_POST_FADE_IN,
    TS_POST_FADE_OUT,
    TS_POST_CROP
} TsPostEditKind;

typedef struct {
    TsPostEditKind kind;
    size_t first;
    size_t last;
    int64_t destination;
    float amount;
    uint32_t crossfade_frames;
} TsPostEdit;

typedef enum {
    TS_BANK_CAPTURE_ROOT = 0,
    TS_BANK_CAPTURE_CURRENT,
    TS_BANK_CAPTURE_SELECTION,
    TS_BANK_CAPTURE_LOOP
} TsBankCaptureKind;

typedef struct {
    TsSample sample;
    TsTuning tuning;
    size_t loop_first;
    size_t loop_last;
    float loop_crossfade_ms;
    TsBankCaptureKind capture_kind;
    TsLoopMode loop_mode;
    int has_loop;
    int occupied;
} TsBankSlot;

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
    TsLoopMode loop_mode;
    int has_selection;
    int has_loop;
    TsTuning tuning;
    TsProcessRecipe process;
    TsSampleEdit sample_edits[TS_SAMPLE_EDIT_DEPTH];
    int sample_edit_count;
    TsPostEdit post_edits[TS_POST_EDIT_DEPTH];
    int post_edit_count;
} TsEditSnapshot;

typedef struct {
    TsSample parent;
    TsSample current;
    TsSourceKind source_kind;
    TsGeneratorRecipe generator;
    TsProcessRecipe process;
    uint32_t generation;
    uint64_t ancestor_hash;
    TsTuning tuning;
    size_t crop_first;
    size_t crop_last;
    size_t selection_first;
    size_t selection_last;
    size_t view_first;
    size_t view_last;
    size_t loop_first;
    size_t loop_last;
    float loop_crossfade_ms;
    TsLoopMode loop_mode;
    int has_selection;
    int has_loop;
    TsSampleEdit sample_edits[TS_SAMPLE_EDIT_DEPTH];
    int sample_edit_count;
    TsPostEdit post_edits[TS_POST_EDIT_DEPTH];
    int post_edit_count;
    TsEditSnapshot undo[TS_HISTORY_DEPTH];
    TsEditSnapshot redo[TS_HISTORY_DEPTH];
    int undo_count;
    int redo_count;
    TsBankSlot bank[TS_BANK_SLOT_COUNT];
} TsInstrument;

void ts_sample_init(TsSample *sample);
void ts_sample_free(TsSample *sample);
int ts_sample_clone(TsSample *destination, const TsSample *source, char *error, size_t error_size);
int ts_sample_load_wav(TsSample *sample, const char *path, char *error, size_t error_size);
int ts_sample_load_wav_tuned(TsSample *sample, TsTuning *tuning, const char *path,
                             char *error, size_t error_size);
int ts_sample_save_wav16(const TsSample *sample, const char *path, char *error, size_t error_size);
int ts_sample_save_wav16_tuned(const TsSample *sample, const TsTuning *tuning,
                               const char *path, char *error, size_t error_size);
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
int ts_instrument_set_process_and_tuning(TsInstrument *instrument,
                                         const TsProcessRecipe *process,
                                         const TsTuning *tuning,
                                         char *error, size_t error_size);
int ts_instrument_set_tuning(TsInstrument *instrument, int root_note,
                             float fine_tune_cents, char *error, size_t error_size);
double ts_tuning_frequency(const TsTuning *tuning);
double ts_tuning_note_pitch(const TsTuning *tuning, int keyboard_note);
const char *ts_midi_note_name(int note, char *name, size_t size);
int ts_instrument_suggest_pitch(const TsInstrument *instrument, TsTuning *suggestion,
                                float *confidence, char *error, size_t error_size);
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
int ts_instrument_set_loop_mode(TsInstrument *instrument, TsLoopMode mode,
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
const char *ts_filter_mode_name(TsFilterMode mode);
const char *ts_shaper_mode_name(TsShaperMode mode);
const char *ts_sample_edit_name(TsSampleEditKind kind);
void ts_process_recipe_reset(TsProcessRecipe *process);
int ts_instrument_save_recipe(const TsInstrument *instrument, const char *path,
                              char *error, size_t error_size);
int ts_instrument_load_recipe(TsInstrument *instrument, const char *path,
                              char *error, size_t error_size);
void ts_instrument_begin_loop_drag(TsInstrument *instrument);
int ts_instrument_move_loop_endpoint(TsInstrument *instrument, int endpoint, size_t frame);
int ts_instrument_bank_capture(TsInstrument *instrument, int slot,
                               TsBankCaptureKind kind, char *error, size_t error_size);
int ts_instrument_bank_clear(TsInstrument *instrument, int slot,
                             char *error, size_t error_size);
int ts_instrument_bank_rename(TsInstrument *instrument, int slot, const char *name,
                              char *error, size_t error_size);
int ts_instrument_set_bank_as_current(TsInstrument *instrument, int slot,
                                      char *error, size_t error_size);
int64_t ts_sample_snap_tape_destination(const TsSample *sample, int64_t target,
                                        size_t source_frames);
int ts_instrument_apply_tape_drag(TsInstrument *instrument, TsPostEditKind kind,
                                  size_t first, size_t last, int64_t destination,
                                  char *error, size_t error_size);
int ts_instrument_bank_count(const TsInstrument *instrument);
int ts_instrument_bank_first_empty(const TsInstrument *instrument);
int ts_instrument_export_bank(const TsInstrument *instrument, const char *folder,
                              char *error, size_t error_size);
const char *ts_bank_capture_name(TsBankCaptureKind kind);
const char *ts_loop_mode_name(TsLoopMode mode);

#endif
