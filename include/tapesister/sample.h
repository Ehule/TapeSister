#ifndef TAPESISTER_SAMPLE_H
#define TAPESISTER_SAMPLE_H

#include <stddef.h>
#include <stdint.h>

enum {
    TS_HISTORY_DEPTH = 20,
    TS_LEGACY_HISTORY_DEPTH = 24,
    TS_SAMPLE_EDIT_DEPTH = 64,
    TS_POST_EDIT_DEPTH = 64,
    TS_AUDIO_PATCH_DEPTH = 64,
    TS_BANK_SLOT_COUNT = 16,
    TS_KEYBOARD_BASE_NOTE = 60,
    TS_CANVAS_MIN_FRAMES = 2,
    TS_CANVAS_MAX_FRAMES = 100000000,
    TS_DEFAULT_CANVAS_FRAMES = 44100,
    TS_DEFAULT_CANVAS_RATE = 44100,
    TS_GRID_DIVISION_MIN = 2,
    TS_GRID_DIVISION_MAX = 64,
    TS_GRID_DIVISION_DEFAULT = 16
};

/* A non-bank performance source used by Capture-to-New-Tile. */
enum { TS_CAPTURE_SOURCE_SYNTH = -2 };

typedef enum {
    TS_GRID_SNAP_OFF = 0,
    TS_GRID_SNAP_ALL,
    TS_GRID_SNAP_MOVE_ONLY,
    TS_GRID_SNAP_MODE_COUNT
} TsGridSnapMode;

typedef struct {
    int root_note;
    float fine_tune_cents;
} TsTuning;

typedef struct {
    float *data;
    size_t frames;
    uint32_t sample_rate;
    char name[128];
    uint32_t visual_revision;
} TsSample;

typedef enum {
    TS_GENERATOR_TONAL = 0,
    TS_GENERATOR_METALLIC,
    TS_GENERATOR_NOISE,
    TS_GENERATOR_PULSE,
    TS_GENERATOR_FM,
    TS_GENERATOR_COUNT
} TsGeneratorKind;

enum {
    TS_FM_OPERATOR_COUNT = 6,
    TS_FM_STRUCTURE_COUNT = 10,
    TS_FM_RATIO_FAMILY_COUNT = 8,
    TS_FM_WAVEFORM_COUNT = 10,
    TS_FM_LFO_TYPE_COUNT = 12,
    TS_FM_INTERACTION_COUNT = 8,
    TS_FM_PITCH_SCALE_COUNT = 5,
    TS_FM_PAGE_COUNT = 7
};

typedef enum {
    TS_FM_PAGE_PITCH = 0,
    TS_FM_PAGE_WAVE,
    TS_FM_PAGE_LFO_RATE,
    TS_FM_PAGE_LFO_DEPTH,
    TS_FM_PAGE_LFO_TYPE,
    TS_FM_PAGE_FILTER,
    TS_FM_PAGE_STRUCTURE
} TsFmPage;

typedef enum {
    TS_FM_WAVE_SINE = 0,
    TS_FM_WAVE_TRIANGLE,
    TS_FM_WAVE_SAW,
    TS_FM_WAVE_SQUARE,
    TS_FM_WAVE_PULSE,
    TS_FM_WAVE_RECTIFIED,
    TS_FM_WAVE_FOLDED,
    TS_FM_WAVE_STEPPED,
    TS_FM_WAVE_DIGITAL,
    TS_FM_WAVE_NOISE
} TsFmWaveform;

typedef enum {
    TS_FM_LFO_OFF = 0,
    TS_FM_LFO_PITCH_SINE,
    TS_FM_LFO_PITCH_TRIANGLE,
    TS_FM_LFO_PITCH_SQUARE,
    TS_FM_LFO_PITCH_RAMP,
    TS_FM_LFO_AMP_SINE,
    TS_FM_LFO_AMP_SQUARE,
    TS_FM_LFO_INDEX_SINE,
    TS_FM_LFO_INDEX_RANDOM,
    TS_FM_LFO_FILTER_SINE,
    TS_FM_LFO_FILTER_RANDOM,
    TS_FM_LFO_STEP_AMP
} TsFmLfoType;

typedef enum {
    TS_FM_INTERACTION_PHASE = 0,
    TS_FM_INTERACTION_ADD,
    TS_FM_INTERACTION_RING,
    TS_FM_INTERACTION_MULTIPLY,
    TS_FM_INTERACTION_SUBTRACT,
    TS_FM_INTERACTION_FOLD,
    TS_FM_INTERACTION_DIGITAL,
    TS_FM_INTERACTION_CROSS
} TsFmInteraction;

typedef enum {
    TS_FM_PITCH_SCALE_CHROMATIC = 0,
    TS_FM_PITCH_SCALE_MAJOR,
    TS_FM_PITCH_SCALE_MINOR,
    TS_FM_PITCH_SCALE_PENTATONIC,
    TS_FM_PITCH_SCALE_WHOLE_TONE
} TsFmPitchScale;

enum {
    TS_FM_MUTATE_PITCH = 1u << 0,
    TS_FM_MUTATE_WAVE = 1u << 1,
    TS_FM_MUTATE_LFO = 1u << 2,
    TS_FM_MUTATE_FILTER = 1u << 3,
    TS_FM_MUTATE_STRUCTURE = 1u << 4,
    TS_FM_MUTATE_ALL = (1u << 5) - 1u
};

typedef struct {
    uint32_t genome_version;
    int drone_mode;
    int extreme_mode;
    int pitch_lock;
    int pitch_root;
    int pitch_scale;
    int structure;
    int ratio_family;
    float depth;
    float shape;
    float feedback;
    float transient_mix;
    float ratios[TS_FM_OPERATOR_COUNT];
    uint32_t active_mask;
    uint32_t mutation_mask;
    int waveforms[TS_FM_OPERATOR_COUNT];
    float lfo_rates[TS_FM_OPERATOR_COUNT];
    float lfo_depths[TS_FM_OPERATOR_COUNT];
    int lfo_types[TS_FM_OPERATOR_COUNT];
    int filter_mode;
    float filter_cutoff_hz;
    float filter_resonance;
    float filter_attack_seconds;
    float filter_release_seconds;
    float filter_envelope_amount;
    int interaction;
    float interaction_mix;
} TsFmPatch;

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
    TsFmPatch fm_patch;
    int has_fm_patch;
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
    TS_POST_CROP,
    TS_POST_ROTATE,
    TS_POST_WARP,
    TS_POST_SMEAR,
    TS_POST_TEAR,
    TS_POST_DELETE,
    TS_POST_PATCH_REPLACE,
    TS_POST_PATCH_FIT,
    TS_POST_PATCH_STRETCH_EXPAND,
    TS_POST_PATCH_STRETCH_CONTRACT,
    TS_POST_CANVAS_LEFT_RESIZE,
    TS_POST_CANVAS_RIGHT_RESIZE,
    /* A rendered Transform is accepted as a stable editable-material checkpoint.
       Unlike ordinary Paste patches, this checkpoint is replayed before the native
       process stage so BODY/EDGE/DRIFT and the native effects remain live. */
    TS_POST_MATERIAL_REPLACE
} TsPostEditKind;

typedef struct {
    TsPostEditKind kind;
    size_t first;
    size_t last;
    int64_t destination;
    float amount;
    uint32_t crossfade_frames;
    uint32_t patch_index;
} TsPostEdit;

typedef struct {
    TsSample sample;
    TsGeneratorRecipe generator;
    int has_generator;
} TsAudioPatch;

typedef enum {
    TS_BANK_CAPTURE_ROOT = 0,
    TS_BANK_CAPTURE_CURRENT,
    TS_BANK_CAPTURE_SELECTION,
    TS_BANK_CAPTURE_LOOP,
    TS_BANK_CAPTURE_PERFORMANCE
} TsBankCaptureKind;

typedef enum {
    TS_FAMILY_ROOT = 0,
    TS_FAMILY_CAPTURED,
    TS_FAMILY_CHILD,
    TS_FAMILY_COUSIN,
    TS_FAMILY_STRANGER,
    TS_FAMILY_RELATION_COUNT
} TsFamilyRelation;

enum {
    TS_FAMILY_LOCK_LOOP = 1u << 0,
    TS_FAMILY_LOCK_DURATION = 1u << 1,
    TS_FAMILY_LOCK_PITCH = 1u << 2,
    TS_FAMILY_LOCK_ENVELOPE = 1u << 3,
    TS_FAMILY_LOCK_SPECTRAL = 1u << 4,
    TS_FAMILY_LOCK_ALL = (1u << 5) - 1u
};

typedef struct {
    size_t crop_first;
    size_t crop_last;
    size_t selection_first;
    size_t selection_last;
    size_t playhead_frame;
    size_t view_first;
    size_t view_last;
    size_t loop_first;
    size_t loop_last;
    float loop_crossfade_ms;
    TsLoopMode loop_mode;
    int has_selection;
    int has_playhead;
    int has_loop;
    uint32_t grid_divisions;
    int grid_snap;
    TsTuning tuning;
    TsTuning audible_tuning;
    TsProcessRecipe process;
    size_t process_first;
    size_t process_last;
    int has_process_range;
    TsSampleEdit sample_edits[TS_SAMPLE_EDIT_DEPTH];
    int sample_edit_count;
    TsPostEdit post_edits[TS_POST_EDIT_DEPTH];
    int post_edit_count;
} TsEditSnapshot;

typedef struct {
    TsSample sample;
    TsSample edit_parent;
    TsTuning tuning;
    TsTuning audible_tuning;
    size_t loop_first;
    size_t loop_last;
    float loop_crossfade_ms;
    TsBankCaptureKind capture_kind;
    TsFamilyRelation relation;
    TsGeneratorRecipe generator;
    uint32_t lineage_seed;
    uint32_t lineage_locks;
    uint32_t trajectory_step;
    float lineage_mutation;
    int parent_slot;
    int has_generator;
    TsLoopMode loop_mode;
    int has_loop;
    int occupied;
    TsProcessRecipe process;
    TsEditSnapshot edit;
    TsEditSnapshot *undo;
    TsEditSnapshot *redo;
    int undo_count;
    int redo_count;
    TsAudioPatch *patches;
    int patch_count;
    int patch_capacity;
    int locked;
} TsBankSlot;

typedef struct {
    TsSample parent;
    TsSample current;
    TsSourceKind source_kind;
    TsGeneratorRecipe generator;
    TsProcessRecipe process;
    TsFamilyRelation family_relation;
    float family_mutation;
    uint32_t family_locks;
    uint32_t family_sequence;
    int family_trajectory;
    int family_anchor_slot;
    int family_last_slot;
    int selected_slot;
    uint32_t generation;
    uint64_t ancestor_hash;
    TsTuning tuning;
    TsTuning audible_tuning;
    size_t process_first;
    size_t process_last;
    int has_process_range;
    size_t crop_first;
    size_t crop_last;
    size_t selection_first;
    size_t selection_last;
    size_t playhead_frame;
    size_t view_first;
    size_t view_last;
    size_t loop_first;
    size_t loop_last;
    float loop_crossfade_ms;
    TsLoopMode loop_mode;
    int has_selection;
    int has_playhead;
    int has_loop;
    uint32_t grid_divisions;
    int grid_snap;
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

typedef struct {
    TsEditSnapshot start;
    TsSample original;
    const float *owner_parent_data;
    uint32_t owner_generation;
    int owner_slot;
    float amount;
    int active;
} TsWarpGesture;

typedef TsWarpGesture TsSmearGesture;
typedef TsWarpGesture TsTearGesture;

typedef struct {
    TsEditSnapshot start;
    TsSample original;
    const float *owner_parent_data;
    uint32_t owner_generation;
    int owner_slot;
    size_t pivot;
    size_t target_first;
    size_t target_last;
    float requested_ratio;
    float actual_ratio;
    float pitch_semitones;
    int active;
} TsStretchGesture;

typedef struct {
    TsEditSnapshot start;
    TsSample original;
    const float *owner_parent_data;
    uint32_t owner_generation;
    int owner_slot;
    int edge;
    int64_t delta_frames;
    size_t focus_view_first;
    size_t focus_view_last;
    int active;
} TsCanvasGesture;

typedef struct {
    TsEditSnapshot start;
    TsSample original;
    const float *owner_parent_data;
    uint32_t owner_generation;
    int owner_slot;
    size_t first_frame;
    size_t last_frame;
    int has_profile;
    int active;
} TsAmplitudeGesture;

void ts_sample_init(TsSample *sample);
void ts_sample_free(TsSample *sample);
void ts_sample_touch(TsSample *sample);
int ts_sample_clone(TsSample *destination, const TsSample *source, char *error, size_t error_size);
int ts_sample_load_wav(TsSample *sample, const char *path, char *error, size_t error_size);
int ts_sample_load_wav_tuned(TsSample *sample, TsTuning *tuning, const char *path,
                             char *error, size_t error_size);
int ts_sample_load_wav_metadata(TsSample *sample, TsTuning *tuning,
                                int *has_loop, size_t *loop_first,
                                size_t *loop_last, TsLoopMode *loop_mode,
                                const char *path, char *error, size_t error_size);
int ts_sample_save_wav16(const TsSample *sample, const char *path, char *error, size_t error_size);
int ts_sample_save_wav32f(const TsSample *sample, const char *path,
                          char *error, size_t error_size);
int ts_sample_save_wav16_tuned(const TsSample *sample, const TsTuning *tuning,
                               const char *path, char *error, size_t error_size);
int ts_sample_save_wav16_tuned_looped(const TsSample *sample,
                                      const TsTuning *tuning,
                                      int has_loop, size_t loop_first,
                                      size_t loop_last, TsLoopMode loop_mode,
                                      const char *path,
                                      char *error, size_t error_size);
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
int ts_instrument_set_process_and_tunings(TsInstrument *instrument,
                                          const TsProcessRecipe *process,
                                          const TsTuning *tuning,
                                          const TsTuning *audible_tuning,
                                          char *error, size_t error_size);
int ts_instrument_set_tuning(TsInstrument *instrument, int root_note,
                             float fine_tune_cents, char *error, size_t error_size);
int ts_instrument_set_audible_tuning(TsInstrument *instrument, int root_note,
                                     float fine_tune_cents,
                                     char *error, size_t error_size);
double ts_tuning_frequency(const TsTuning *tuning);
double ts_tuning_note_pitch(const TsTuning *tuning, int keyboard_note);
const char *ts_midi_note_name(int note, char *name, size_t size);
int ts_instrument_suggest_pitch(const TsInstrument *instrument, TsTuning *suggestion,
                                float *confidence, char *error, size_t error_size);
int ts_instrument_reset_current(TsInstrument *instrument, char *error, size_t error_size);
int ts_instrument_commit_current(TsInstrument *instrument, char *error, size_t error_size);
void ts_instrument_set_selection(TsInstrument *instrument, size_t first, size_t last);
void ts_instrument_set_selection_snapped(TsInstrument *instrument, size_t first, size_t last);
size_t ts_instrument_grid_target(const TsInstrument *instrument, size_t frame);
size_t ts_instrument_resolve_boundary(const TsInstrument *instrument, size_t frame);
int ts_instrument_grid_moves_snap(const TsInstrument *instrument);
int ts_instrument_set_grid_divisions(TsInstrument *instrument, uint32_t divisions);
int ts_instrument_cycle_grid_divisions(TsInstrument *instrument, int direction);
int ts_instrument_toggle_grid_snap(TsInstrument *instrument);
int ts_instrument_select_all(TsInstrument *instrument);
int ts_instrument_select_wave(TsInstrument *instrument);
void ts_instrument_clear_selection(TsInstrument *instrument);
void ts_instrument_set_playhead(TsInstrument *instrument, size_t frame);
void ts_instrument_set_playhead_snapped(TsInstrument *instrument, size_t frame);
void ts_instrument_clear_playhead(TsInstrument *instrument);
int ts_instrument_reset_selection_playhead(TsInstrument *instrument);
int ts_instrument_resize_selection(TsInstrument *instrument, int endpoint,
                                   int expand, size_t crossing_count);
int ts_instrument_stretch_selection(TsInstrument *instrument, size_t pivot,
                                    float duration_ratio, float *pitch_semitones,
                                    char *error, size_t error_size);
void ts_stretch_gesture_init(TsStretchGesture *gesture);
int ts_instrument_stretch_gesture_begin(TsInstrument *instrument,
                                        TsStretchGesture *gesture, size_t pivot,
                                        char *error, size_t error_size);
int ts_instrument_stretch_gesture_preview(TsInstrument *instrument,
                                          TsStretchGesture *gesture,
                                          float duration_ratio,
                                          float *pitch_semitones,
                                          char *error, size_t error_size);
int ts_instrument_stretch_gesture_commit(TsInstrument *instrument,
                                         TsStretchGesture *gesture,
                                         char *error, size_t error_size);
int ts_instrument_stretch_gesture_cancel(TsInstrument *instrument,
                                         TsStretchGesture *gesture,
                                         char *error, size_t error_size);
int ts_instrument_resize_canvas(TsInstrument *instrument, int edge,
                                int64_t delta_frames,
                                char *error, size_t error_size);
int ts_instrument_double_canvas(TsInstrument *instrument,
                                char *error, size_t error_size);
int ts_instrument_half_canvas(TsInstrument *instrument,
                              char *error, size_t error_size);
void ts_canvas_gesture_init(TsCanvasGesture *gesture);
int ts_instrument_canvas_gesture_begin(TsInstrument *instrument,
                                       TsCanvasGesture *gesture, int edge,
                                       char *error, size_t error_size);
int ts_instrument_canvas_gesture_preview(TsInstrument *instrument,
                                         TsCanvasGesture *gesture,
                                         int64_t delta_frames,
                                         char *error, size_t error_size);
int ts_instrument_canvas_gesture_commit(TsInstrument *instrument,
                                        TsCanvasGesture *gesture,
                                        char *error, size_t error_size);
int ts_instrument_canvas_gesture_cancel(TsInstrument *instrument,
                                        TsCanvasGesture *gesture,
                                        char *error, size_t error_size);
void ts_amplitude_gesture_init(TsAmplitudeGesture *gesture);
int ts_instrument_amplitude_gesture_begin(TsInstrument *instrument,
                                          TsAmplitudeGesture *gesture,
                                          char *error, size_t error_size);
int ts_instrument_amplitude_gesture_preview(TsInstrument *instrument,
                                            TsAmplitudeGesture *gesture,
                                            size_t first, float first_gain,
                                            size_t last, float last_gain,
                                            char *error, size_t error_size);
int ts_instrument_amplitude_gesture_commit(TsInstrument *instrument,
                                           TsAmplitudeGesture *gesture,
                                           char *error, size_t error_size);
int ts_instrument_amplitude_gesture_cancel(TsInstrument *instrument,
                                           TsAmplitudeGesture *gesture,
                                           char *error, size_t error_size);
size_t ts_sample_nearest_zero_crossing(const TsSample *sample, size_t frame);
size_t ts_sample_nearest_zero_crossing_in_range(const TsSample *sample,
                                                size_t frame,
                                                size_t first, size_t last);
size_t ts_sample_zero_crossing_in_direction(const TsSample *sample, size_t frame,
                                            int direction, size_t count);
int ts_sample_make_drone(TsSample *destination, const TsSample *source,
                         size_t first, size_t last, int crossfade_ms,
                         size_t *split_frame, size_t *overlap_frames,
                         char *error, size_t error_size);
int ts_sample_make_drone_at_split(TsSample *destination, const TsSample *source,
                                  size_t first, size_t last, size_t split_frame,
                                  size_t requested_overlap_frames,
                                  size_t *effective_overlap_frames,
                                  char *error, size_t error_size);
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
int ts_instrument_rotate_zero_crossing(TsInstrument *instrument, int direction,
                                       size_t crossing_count,
                                       char *error, size_t error_size);
int ts_instrument_apply_warp(TsInstrument *instrument, float amount,
                             char *error, size_t error_size);
void ts_warp_gesture_init(TsWarpGesture *gesture);
int ts_instrument_warp_gesture_begin(TsInstrument *instrument,
                                     TsWarpGesture *gesture,
                                     char *error, size_t error_size);
int ts_instrument_warp_gesture_preview(TsInstrument *instrument,
                                       TsWarpGesture *gesture, float amount,
                                       char *error, size_t error_size);
int ts_instrument_warp_gesture_commit(TsInstrument *instrument,
                                      TsWarpGesture *gesture,
                                      char *error, size_t error_size);
int ts_instrument_warp_gesture_cancel(TsInstrument *instrument,
                                      TsWarpGesture *gesture,
                                      char *error, size_t error_size);
int ts_instrument_apply_smear(TsInstrument *instrument, float amount,
                              char *error, size_t error_size);
void ts_smear_gesture_init(TsSmearGesture *gesture);
int ts_instrument_smear_gesture_begin(TsInstrument *instrument,
                                      TsSmearGesture *gesture,
                                      char *error, size_t error_size);
int ts_instrument_smear_gesture_preview(TsInstrument *instrument,
                                        TsSmearGesture *gesture, float amount,
                                        char *error, size_t error_size);
int ts_instrument_smear_gesture_commit(TsInstrument *instrument,
                                       TsSmearGesture *gesture,
                                       char *error, size_t error_size);
int ts_instrument_smear_gesture_cancel(TsInstrument *instrument,
                                       TsSmearGesture *gesture,
                                       char *error, size_t error_size);
int ts_instrument_apply_tear(TsInstrument *instrument, float amount,
                             char *error, size_t error_size);
void ts_tear_gesture_init(TsTearGesture *gesture);
int ts_instrument_tear_gesture_begin(TsInstrument *instrument,
                                     TsTearGesture *gesture,
                                     char *error, size_t error_size);
int ts_instrument_tear_gesture_preview(TsInstrument *instrument,
                                       TsTearGesture *gesture, float amount,
                                       char *error, size_t error_size);
int ts_instrument_tear_gesture_commit(TsInstrument *instrument,
                                      TsTearGesture *gesture,
                                      char *error, size_t error_size);
int ts_instrument_tear_gesture_cancel(TsInstrument *instrument,
                                      TsTearGesture *gesture,
                                      char *error, size_t error_size);
int ts_instrument_zoom_selection(TsInstrument *instrument);
int ts_instrument_zoom_view(TsInstrument *instrument, size_t anchor_frame,
                            float anchor_ratio, float scale);
int ts_instrument_pan_view(TsInstrument *instrument, ptrdiff_t frames);
void ts_instrument_show_all(TsInstrument *instrument);
int ts_instrument_undo(TsInstrument *instrument, char *error, size_t error_size);
int ts_instrument_redo(TsInstrument *instrument, char *error, size_t error_size);
size_t ts_instrument_frame_from_view_x(const TsInstrument *instrument, int x, int width);
const char *ts_generator_name(TsGeneratorKind kind);
const char *ts_fm_structure_name(int structure);
const char *ts_fm_ratio_family_name(int family);
const char *ts_fm_waveform_name(int waveform);
const char *ts_fm_lfo_type_name(int type);
const char *ts_fm_interaction_name(int interaction);
const char *ts_fm_pitch_scale_name(int scale);
const char *ts_fm_page_name(TsFmPage page);
void ts_fm_patch_from_recipe(const TsGeneratorRecipe *recipe, TsFmPatch *patch);
void ts_fm_patch_vary(const TsFmPatch *source, uint32_t seed, float range,
                      TsFmPatch *varied);
float ts_fm_patch_distance(const TsFmPatch *source, const TsFmPatch *varied);
void ts_fm_patch_sanitize(TsFmPatch *patch);
float ts_fm_control_normalized(const TsFmPatch *patch, TsFmPage page, int control);
int ts_fm_set_control_normalized(TsFmPatch *patch, TsFmPage page, int control,
                                 float normalized);
int ts_fm_step_control(TsFmPatch *patch, TsFmPage page, int control,
                       int direction, int fine);
int ts_fm_step_pitch_root(TsFmPatch *patch, int direction);
int ts_fm_step_pitch_scale(TsFmPatch *patch, int direction);
int ts_fm_apply_pitch_scale(TsFmPatch *patch);
void ts_fm_control_format(const TsFmPatch *patch, TsFmPage page, int control,
                          char *label, size_t label_size,
                          char *value, size_t value_size);
int ts_fm_render_sample(TsSample *sample, const TsFmPatch *patch,
                        float seconds, float frequency, uint32_t sample_rate,
                        uint32_t seed, char *error, size_t error_size);
int ts_instrument_apply_fm_patch(TsInstrument *instrument,
                                 const TsFmPatch *patch,
                                 char *error, size_t error_size);
int ts_instrument_make_fm_bank(TsInstrument *instrument,
                               const TsFmPatch *patch,
                               char *error, size_t error_size);
int ts_instrument_clone(TsInstrument *destination,
                        const TsInstrument *source,
                        char *error, size_t error_size);
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
int ts_instrument_bank_is_blank_canvas(const TsInstrument *instrument, int slot);
int ts_instrument_capture_target_frames(const TsInstrument *instrument, int slot,
                                        uint32_t output_rate,
                                        size_t *capacity_frames,
                                        char *error, size_t error_size);
int ts_instrument_commit_capture(TsInstrument *instrument, int destination_slot,
                                 int source_slot, const float *captured,
                                 size_t recorded_frames, uint32_t capture_rate,
                                 int stopped_early,
                                 char *error, size_t error_size);
int ts_instrument_bank_clear(TsInstrument *instrument, int slot,
                             char *error, size_t error_size);
int ts_instrument_bank_clear_all(TsInstrument *instrument,
                                 char *error, size_t error_size);
int ts_instrument_bank_is_locked(const TsInstrument *instrument, int slot);
int ts_instrument_bank_set_locked(TsInstrument *instrument, int slot, int locked,
                                  char *error, size_t error_size);
int ts_instrument_bank_toggle_locked(TsInstrument *instrument, int slot,
                                     char *error, size_t error_size);
int ts_instrument_bank_rename(TsInstrument *instrument, int slot, const char *name,
                              char *error, size_t error_size);
int ts_instrument_bank_set_loop_full(TsInstrument *instrument, int slot,
                                     char *error, size_t error_size);
int ts_instrument_bank_clear_loop(TsInstrument *instrument, int slot,
                                  char *error, size_t error_size);
int ts_instrument_bank_set_loop_crossfade(TsInstrument *instrument, int slot,
                                          float milliseconds,
                                          char *error, size_t error_size);
int ts_instrument_bank_set_loop_mode(TsInstrument *instrument, int slot,
                                     TsLoopMode mode,
                                     char *error, size_t error_size);
int ts_instrument_bank_move_loop_endpoint(TsInstrument *instrument, int slot,
                                          int endpoint, size_t frame);
int ts_instrument_set_bank_as_current(TsInstrument *instrument, int slot,
                                      char *error, size_t error_size);
int ts_instrument_select_bank(TsInstrument *instrument, int slot,
                              char *error, size_t error_size);
int ts_instrument_create_selected(TsInstrument *instrument, uint32_t seed,
                                  char *error, size_t error_size);
int ts_instrument_activate_silence(TsInstrument *instrument, size_t frames,
                                   uint32_t sample_rate,
                                   char *error, size_t error_size);
int ts_instrument_vary_selected(TsInstrument *instrument, int chain,
                                int *destination_slot,
                                char *error, size_t error_size);
int ts_instrument_copy_selected(TsInstrument *instrument, int destination_slot,
                                char *error, size_t error_size);
int ts_instrument_copy_bank_slot_from(TsInstrument *destination,
                                      int destination_slot,
                                      TsInstrument *source, int source_slot,
                                      char *error, size_t error_size);
int ts_instrument_copy_selection(const TsInstrument *instrument,
                                 TsSample *clipboard, size_t *origin_first,
                                 char *error, size_t error_size);
int ts_instrument_cut_selection(TsInstrument *instrument,
                                TsSample *clipboard, size_t *origin_first,
                                char *error, size_t error_size);
int ts_instrument_cut_selection_mode(TsInstrument *instrument,
                                     TsSample *clipboard, size_t *origin_first,
                                     int crop_canvas,
                                     char *error, size_t error_size);
int ts_instrument_paste(TsInstrument *instrument, const TsSample *clipboard,
                        size_t origin_first, int fit_selection,
                        char *error, size_t error_size);
int ts_instrument_apply_rendered_replacement(TsInstrument *instrument,
                                             const TsSample *rendered,
                                             size_t first, size_t last,
                                             char *error, size_t error_size);
int ts_instrument_apply_pitch_shift(TsInstrument *instrument,
                                    float semitones,
                                    char *error, size_t error_size);
int ts_instrument_replace_selection_with_drone(TsInstrument *instrument,
                                                const TsSample *drone,
                                                char *error, size_t error_size);
int ts_instrument_copy_drone_to_new_tile(TsInstrument *instrument,
                                          const TsSample *drone,
                                          int *destination_slot,
                                          char *error, size_t error_size);
int ts_instrument_stamp_create(TsInstrument *instrument, uint32_t seed,
                               char *error, size_t error_size);
int ts_instrument_stamp_vary(TsInstrument *instrument,
                             char *error, size_t error_size);
int ts_instrument_stamp_vary_chained(TsInstrument *instrument,
                                     int crossfade_ms,
                                     char *error, size_t error_size);
int ts_instrument_generate_family_candidate(TsInstrument *instrument,
                                            int anchor_slot, int reseed,
                                            int *created_slot,
                                            char *error, size_t error_size);
int64_t ts_sample_snap_tape_destination(const TsSample *sample, int64_t target,
                                        size_t source_frames);
int ts_instrument_apply_tape_drag(TsInstrument *instrument, TsPostEditKind kind,
                                  size_t first, size_t last, int64_t destination,
                                  char *error, size_t error_size);
int ts_instrument_bank_count(const TsInstrument *instrument);
int ts_instrument_bank_first_empty(const TsInstrument *instrument);
int ts_instrument_bank_next_empty(const TsInstrument *instrument);
int ts_instrument_export_bank(const TsInstrument *instrument, const char *folder,
                              char *error, size_t error_size);
int ts_instrument_family_folder_name(const TsInstrument *instrument,
                                     char *name, size_t name_size);
int ts_instrument_next_family_path(const TsInstrument *instrument,
                                   const char *directory,
                                   char *path, size_t path_size,
                                   char *error, size_t error_size);
const char *ts_bank_capture_name(TsBankCaptureKind kind);
const char *ts_family_relation_name(TsFamilyRelation relation);
const char *ts_loop_mode_name(TsLoopMode mode);

#endif
