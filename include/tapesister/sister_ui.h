#ifndef TAPESISTER_SISTER_UI_H
#define TAPESISTER_SISTER_UI_H

#include "tapesister/config.h"
#include "tapesister/palette.h"
#include "tapesister/performance_recorder.h"
#include "tapesister/sister_runtime.h"
#include "tapesister/ui.h"

enum { TS_SISTER_UI_WIDTH = 640, TS_SISTER_UI_HEIGHT = 400 };
enum { TS_SISTER_UI_FX_REC_X = 538, TS_SISTER_UI_FX_REC_Y = 328,
       TS_SISTER_UI_FX_REC_W = 92, TS_SISTER_UI_FX_REC_H = 22 };
enum { TS_PRISM_SUSTAIN_X = 396, TS_PRISM_SUSTAIN_W = 82,
       TS_PRISM_REC_X = 484, TS_PRISM_REC_W = 74,
       TS_PRISM_FOOTER_Y = 350, TS_PRISM_FOOTER_H = 17 };
enum { TS_PRISM_MATRIX_X=16, TS_PRISM_MATRIX_Y=114,
       TS_PRISM_MATRIX_DX=76, TS_PRISM_MATRIX_DY=25,
       TS_PRISM_MATRIX_W=72, TS_PRISM_MATRIX_H=22 };
typedef enum { TS_PRISM_CAPTURE_EMPTY, TS_PRISM_CAPTURE_READY,
               TS_PRISM_CAPTURE_EDITING, TS_PRISM_CAPTURE_DIRTY } TsPrismCaptureVisual;
enum { TS_PRISM_ZOYA_INTRO_MS = 3600 };
typedef struct {
    uint32_t started_ms, elapsed_ms;
    uint32_t last_ms, ambient_ms, coherent_ms;
    float apparition;
    int apparition_spent;
    float spread, drift, focus, body, color, mix, dry;
    int initialized, enabled, visible, introducing;
} TsPrismZoyaVisual;

typedef enum {
    TS_SISTER_UI_ACTION_NONE = 0,
    TS_SISTER_UI_ACTION_POWER,
    TS_SISTER_UI_ACTION_ROLL,
    TS_SISTER_UI_ACTION_HOLD,
    TS_SISTER_UI_ACTION_CLEAR,
    TS_SISTER_UI_ACTION_MONITOR,
    TS_SISTER_UI_ACTION_LIMITER_TOGGLE,
    TS_SISTER_UI_ACTION_MASTER_OUTPUT,
    TS_SISTER_UI_ACTION_WAVE_MODE,
    TS_SISTER_UI_ACTION_SOURCE_TILES,
    TS_SISTER_UI_ACTION_SOURCE_FM,
    TS_SISTER_UI_ACTION_SOURCE_EXT,
    TS_SISTER_UI_ACTION_SOURCE_PREVIEW,
    TS_SISTER_UI_ACTION_SOURCE_TAPEHEAD,
    TS_SISTER_UI_ACTION_TAPEHEAD_SONG,
    TS_SISTER_UI_ACTION_TAPEHEAD_PATTERN,
    TS_SISTER_UI_ACTION_TAP,
    TS_SISTER_UI_ACTION_CAPTURE_FORMAT,
    TS_SISTER_UI_ACTION_DESTINATION,
    TS_SISTER_UI_ACTION_CAPTURE,
    TS_SISTER_UI_ACTION_OVERDUB,
    TS_SISTER_UI_ACTION_PARAMETER,
    TS_SISTER_UI_ACTION_EFFECT_TARGET,
    TS_SISTER_UI_ACTION_FX_TOGGLE,
    TS_SISTER_UI_ACTION_FX_SLOT_TOGGLE,
    TS_SISTER_UI_ACTION_FX_SLOT_TYPE,
    TS_SISTER_UI_ACTION_FX_SLOT_PLACEMENT,
    TS_SISTER_UI_ACTION_FX_SLOT_MOVE,
    TS_SISTER_UI_ACTION_FALLOUT_TOGGLE,
    TS_SISTER_UI_ACTION_FALLOUT_LFO_DIALOG,
    TS_SISTER_UI_ACTION_FALLOUT_LFO_TARGET,
    TS_SISTER_UI_ACTION_FALLOUT_RISE_TARGET,
    TS_SISTER_UI_ACTION_FALLOUT_RISE_MODE,
    TS_SISTER_UI_ACTION_FALLOUT_RISE_RETRIGGER,
    TS_SISTER_UI_ACTION_PAGE,
    TS_SISTER_UI_ACTION_PRESET_PREVIOUS,
    TS_SISTER_UI_ACTION_PRESET_NEXT,
    TS_SISTER_UI_ACTION_PRESET_MANAGE,
    TS_SISTER_UI_ACTION_PRESET_SAVE_AS,
    TS_SISTER_UI_ACTION_PRESET_OVERWRITE,
    TS_SISTER_UI_ACTION_PRESET_RENAME,
    TS_SISTER_UI_ACTION_PRESET_DELETE,
    TS_SISTER_UI_ACTION_PRESET_CONFIRM,
    TS_SISTER_UI_ACTION_PRESET_CANCEL,
    TS_SISTER_UI_ACTION_RECORD_FILE,
    TS_SISTER_UI_ACTION_PRISM_TOGGLE,
    TS_SISTER_UI_ACTION_PRISM_MODE
} TsSisterUiAction;

typedef enum {
    TS_SISTER_UI_DEST_CURRENT = 0,
    TS_SISTER_UI_DEST_NEXT_EMPTY,
    TS_SISTER_UI_DEST_FILE
} TsSisterUiDestinationMode;

typedef enum {
    TS_SISTER_UI_PARAM_H1_LEVEL = 0,
    TS_SISTER_UI_PARAM_H1_TIME,
    TS_SISTER_UI_PARAM_H1_FEEDBACK,
    TS_SISTER_UI_PARAM_H2_LEVEL,
    TS_SISTER_UI_PARAM_H2_SCRUB,
    TS_SISTER_UI_PARAM_H2_RATE,
    TS_SISTER_UI_PARAM_H2_FEEDBACK,
    TS_SISTER_UI_PARAM_H3_LEVEL,
    TS_SISTER_UI_PARAM_H3_SPAN,
    TS_SISTER_UI_PARAM_H3_RATE,
    TS_SISTER_UI_PARAM_WOW,
    TS_SISTER_UI_PARAM_DROP,
    TS_SISTER_UI_PARAM_DUCK,
    TS_SISTER_UI_PARAM_DECORRELATE,
    TS_SISTER_UI_PARAM_WIDTH,
    TS_SISTER_UI_PARAM_FILTER_TYPE,
    TS_SISTER_UI_PARAM_FILTER_CUTOFF,
    TS_SISTER_UI_PARAM_FILTER_Q,
    TS_SISTER_UI_PARAM_FILTER_GAIN,
    TS_SISTER_UI_PARAM_INPUT_GAIN,
    TS_SISTER_UI_PARAM_TILES_GAIN,
    TS_SISTER_UI_PARAM_FM_GAIN,
    TS_SISTER_UI_PARAM_EXT_GAIN,
    TS_SISTER_UI_PARAM_PREVIEW_GAIN,
    TS_SISTER_UI_PARAM_FX_RETURN_GAIN,
    TS_SISTER_UI_PARAM_MONITOR_DRY,
    TS_SISTER_UI_PARAM_MONITOR_WET,
    TS_SISTER_UI_PARAM_MIX_OUTPUT,
    TS_SISTER_UI_PARAM_WRITE_ERASE,
    TS_SISTER_UI_PARAM_GHOST_TONE,
    TS_SISTER_UI_PARAM_SOAK,
    TS_SISTER_UI_PARAM_BLEED,
    TS_SISTER_UI_PARAM_REVERB_TYPE,
    TS_SISTER_UI_PARAM_REVERB_MIX,
    TS_SISTER_UI_PARAM_REVERB_DECAY,
    TS_SISTER_UI_PARAM_DELAY_TIME,
    TS_SISTER_UI_PARAM_DELAY_FEEDBACK,
    TS_SISTER_UI_PARAM_DELAY_MIX,
    TS_SISTER_UI_PARAM_DISTORTION_DRIVE,
    TS_SISTER_UI_PARAM_DISTORTION_TONE,
    TS_SISTER_UI_PARAM_DISTORTION_MIX,
    TS_SISTER_UI_PARAM_FX_TRANSITION,
    TS_SISTER_UI_PARAM_MASTER_FX_FEEDBACK,
    TS_SISTER_UI_PARAM_FALLOUT_MIX,
    TS_SISTER_UI_PARAM_FALLOUT_FEEDBACK,
    TS_SISTER_UI_PARAM_FALLOUT_NOISE,
    TS_SISTER_UI_PARAM_FALLOUT_DROP_RATE,
    TS_SISTER_UI_PARAM_FALLOUT_PAN_RATE,
    TS_SISTER_UI_PARAM_FALLOUT_SKIP_SPAN,
    TS_SISTER_UI_PARAM_FALLOUT_SKIP_RATE,
    TS_SISTER_UI_PARAM_FALLOUT_BIT_QUALITY,
    TS_SISTER_UI_PARAM_FALLOUT_BIT_RESOLUTION,
    TS_SISTER_UI_PARAM_FALLOUT_BIT_RATE,
    TS_SISTER_UI_PARAM_FALLOUT_PITCH,
    TS_SISTER_UI_PARAM_FALLOUT_PITCH_RAMP,
    TS_SISTER_UI_PARAM_FALLOUT_PITCH_RATE,
    TS_SISTER_UI_PARAM_FALLOUT_TRANSITION,
    TS_SISTER_UI_PARAM_FALLOUT_COMPONENT_TRANSITION,
    TS_SISTER_UI_PARAM_FALLOUT_LFO_RATE,
    TS_SISTER_UI_PARAM_FALLOUT_LFO_INTENSITY,
    TS_SISTER_UI_PARAM_FALLOUT_RISE_LENGTH,
    TS_SISTER_UI_PARAM_FALLOUT_RISE_INTENSITY,
    TS_SISTER_UI_PARAM_BUFFER_SECONDS,
    /* Appended so every established preset-lock bit retains its identity. */
    TS_SISTER_UI_PARAM_REVERB_GAIN,
    TS_SISTER_UI_PARAM_DELAY_GAIN,
    TS_SISTER_UI_PARAM_DISTORTION_GAIN,
    TS_SISTER_UI_PARAM_GRAIN_GAIN,
    TS_SISTER_UI_PARAM_GRAIN_SIZE,
    TS_SISTER_UI_PARAM_GRAIN_DENSITY,
    TS_SISTER_UI_PARAM_GRAIN_PITCH,
    TS_SISTER_UI_PARAM_GRAIN_MIX,
    TS_SISTER_UI_PARAM_SLOT1_GAIN,
    TS_SISTER_UI_PARAM_SLOT1_A,
    TS_SISTER_UI_PARAM_SLOT1_B,
    TS_SISTER_UI_PARAM_SLOT1_C,
    TS_SISTER_UI_PARAM_SLOT1_MIX,
    TS_SISTER_UI_PARAM_SLOT2_GAIN,
    TS_SISTER_UI_PARAM_SLOT2_A,
    TS_SISTER_UI_PARAM_SLOT2_B,
    TS_SISTER_UI_PARAM_SLOT2_C,
    TS_SISTER_UI_PARAM_SLOT2_MIX,
    TS_SISTER_UI_PARAM_SLOT3_GAIN,
    TS_SISTER_UI_PARAM_SLOT3_A,
    TS_SISTER_UI_PARAM_SLOT3_B,
    TS_SISTER_UI_PARAM_SLOT3_C,
    TS_SISTER_UI_PARAM_SLOT3_MIX,
    TS_SISTER_UI_PARAM_SLOT4_GAIN,
    TS_SISTER_UI_PARAM_SLOT4_A,
    TS_SISTER_UI_PARAM_SLOT4_B,
    TS_SISTER_UI_PARAM_SLOT4_C,
    TS_SISTER_UI_PARAM_SLOT4_MIX,
    TS_SISTER_UI_PARAM_TAPEHEAD_GAIN,
    TS_SISTER_UI_PARAM_PRISM_LENSES,
    TS_SISTER_UI_PARAM_PRISM_SPREAD,
    TS_SISTER_UI_PARAM_PRISM_DRIFT,
    TS_SISTER_UI_PARAM_PRISM_FOCUS,
    TS_SISTER_UI_PARAM_PRISM_STEREO,
    TS_SISTER_UI_PARAM_PRISM_BODY,
    TS_SISTER_UI_PARAM_PRISM_MIX,
    TS_SISTER_UI_PARAM_PRISM_OUTPUT,
    TS_SISTER_UI_PARAM_PRISM_DRY,
    TS_SISTER_UI_PARAM_PRISM_COLOR,
    TS_SISTER_UI_PARAM_PRISM_RATE,
    TS_SISTER_UI_PARAM_PRISM_OCTAVE,
    TS_SISTER_UI_PARAM_PRISM_MORPH,
    TS_SISTER_UI_PARAM_PRISM_TIME,
    TS_SISTER_UI_PARAM_PRISM_SEQ_RATE,
    TS_SISTER_UI_PARAM_COUNT,
    /* These clocks are intentionally not preset-lock bits: the established
       63 lock indices remain stable in existing preset files. */
    TS_SISTER_UI_PARAM_MASTER_FX_TRANSITION = 1000,
    TS_SISTER_UI_PARAM_FALLOUT_MASTER_TRANSITION,
    /* Global session control: deliberately outside preset-lock storage. */
    TS_SISTER_UI_PARAM_MASTER_OUTPUT
} TsSisterUiParameter;

#define TS_SISTER_UI_SLOT_PARAMETER(slot, field) \
    (TS_SISTER_UI_PARAM_SLOT1_GAIN + (int)(slot) * 5 + (int)(field))

typedef enum {
    TS_SISTER_UI_FX_MASTER = 0,
    TS_SISTER_UI_FX_REVERB,
    TS_SISTER_UI_FX_DELAY,
    TS_SISTER_UI_FX_DISTORTION,
    TS_SISTER_UI_FX_GRAIN
} TsSisterUiFxToggle;

typedef enum {
    TS_SISTER_UI_FALLOUT_POWER = 0,
    TS_SISTER_UI_FALLOUT_NOISE_TYPE,
    TS_SISTER_UI_FALLOUT_DROP,
    TS_SISTER_UI_FALLOUT_PAN,
    TS_SISTER_UI_FALLOUT_SKIP,
    TS_SISTER_UI_FALLOUT_BIT,
    TS_SISTER_UI_FALLOUT_PITCH
} TsSisterUiFalloutToggle;

_Static_assert(TS_SISTER_UI_PARAM_COUNT <= 128,
               "Sister parameter locks require two 64-bit masks");

#define TS_SISTER_UI_PARAMETER_BIT(parameter) \
    (UINT64_C(1) << (unsigned)(parameter))

typedef struct {
    TsSisterUiAction action;
    int index;
    float normalized;
} TsSisterUiHit;

typedef enum {
    TS_SISTER_UI_POWER_VISUAL_NONE = 0,
    TS_SISTER_UI_POWER_VISUAL_ON,
    TS_SISTER_UI_POWER_VISUAL_OFF
} TsSisterUiPowerVisual;

typedef struct {
    int visible;
    int capture_channels;
    int capture_overdub;
    TsPerformanceFileState file_capture_state;
    uint32_t file_capture_sample_rate;
    uint64_t file_capture_frames;
    uint64_t file_capture_dropped_frames;
    TsSisterTap selected_tap;
    TsSisterUiDestinationMode destination_mode;
    TsWaveformDisplayMode waveform_mode;
    TsSisterRoutingSnapshot routing;
    TsSisterSnapshot engine;
    TsSisterWaveSnapshot waveform;
    TsSisterParameters parameters;
    uint64_t parameter_locks;
    uint64_t parameter_locks_high;
    int destination_slot;
    char status[128];
    char preset_name[48];
    char preset_edit_name[48];
    size_t preset_edit_cursor;
    int text_cursor_visible;
    int keyboard_sustain;
    TsSisterUiPowerVisual power_visual;
    uint32_t power_visual_elapsed_ms;
    uint8_t magnetic_phase;
    int preset_factory;
    int preset_modified;
    size_t preset_position;
    size_t preset_count;
    int preset_manage_open;
    int preset_editing;
    int preset_confirmation;
    int fx_page;
    int prism_panel, prism_seq_edit, prism_preset_index, prism_preset_count;
    int prism_factory_index; /* Last browsed factory sound; -1 before browsing. */
    int prism_matrix_previous_panel, prism_matrix_edit;
    char prism_preset_name[32];
    int prism_selected; /* One-based lens identity; zero means no selection. */
    int prism_edit_endpoint; /* One-based recalled A-L state; UI only. */
    int prism_browse[2]; /* One-based pending letters; zero keeps the assigned end. */
    int prism_hover_valid, prism_hover_x, prism_hover_y;
    TsPrismZoyaPose prism_zoya_pose; /* INI preference; never sent to audio or saved in presets. */
    TsPrismZoyaVisual prism_zoya; /* Event-thread visual state, never saved or sent to audio. */
    int fallout_lfo_open;
    int midi_learn_active;
    int midi_activity;
    int tapehead_song_playing;
    int tapehead_pattern_playing;
    char midi_learn_pending[TS_MIDI_TARGET_ID_MAX];
    const TsMidiMap *midi_map;
} TsSisterUiModel;

/* Shared optical coordinates for rendering and direct manipulation. */
enum { TS_PRISM_STRIP_X = 32, TS_PRISM_STRIP_Y = 72, TS_PRISM_STRIP_STEP = 24,
       TS_PRISM_STRIP_W = 22, TS_PRISM_STRIP_H = 11 };
int ts_sister_ui_prism_strip_hit(int x, int y);
void ts_sister_ui_prism_point(TsPrismLensView ray, int *x, int *y);
void ts_sister_ui_prism_point_f(TsPrismLensView ray, float *x, float *y);
float ts_sister_ui_prism_y(float cents);
float ts_sister_ui_prism_pitch_at_y(float y);
int ts_sister_ui_prism_hit(const TsSisterUiModel *model, int x, int y);
TsPrismCaptureVisual ts_sister_ui_prism_capture_visual(const TsSisterUiModel *model, int endpoint);
int ts_sister_ui_prism_state(const TsSisterUiModel *model, int endpoint);
const char *ts_sister_ui_prism_edit_status(const TsSisterUiModel *model);
void ts_sister_ui_prism_help(const TsSisterUiModel *model, int x, int y, char *help, size_t size);
int ts_sister_ui_prism_matrix_cell(int x,int y);
int ts_sister_ui_prism_assigned(const TsSisterUiModel *model,int side);
void ts_sister_ui_prism_zoya_tick(TsPrismZoyaVisual *visual, uint32_t now_ms, int enabled, int visible);
void ts_sister_ui_prism_nebula_update(TsSisterUiModel *model, uint32_t now_ms,
                                     int visible, const TsPrismPatch *matrix_pair, float matrix_morph);

int ts_sister_ui_parameter_lockable(int parameter);
int ts_sister_ui_parameter_locked(const TsSisterUiModel *model,
                                  int parameter);
int ts_sister_ui_parameter_lock_toggle(TsSisterUiModel *model,
                                       int parameter);
void ts_sister_ui_migrate_legacy_effect_locks(uint64_t *locks,
                                              uint64_t *locks_high);

void ts_sister_ui_model_init(TsSisterUiModel *model, const TsConfig *config);
void ts_sister_ui_set_capture_channels(TsSisterUiModel *model,
                                       TsConfig *config, int channels);
void ts_sister_ui_model_show(TsSisterUiModel *model);
void ts_sister_ui_model_hide(TsSisterUiModel *model);
void ts_sister_ui_model_update(TsSisterUiModel *model,
                               const TsSisterRoutingSnapshot *routing,
                               const TsSisterSnapshot *engine,
                               const TsSisterWaveSnapshot *waveform,
                               const TsSisterParameters *parameters);
TsSisterUiHit ts_sister_ui_hit_test(int x, int y);
TsSisterUiHit ts_sister_ui_hit_test_model(const TsSisterUiModel *model,
                                          int x, int y);
int ts_sister_ui_event_point(int event_x, int event_y,
                             int *logical_x, int *logical_y);
int ts_sister_ui_window_point(int raw_x, int raw_y,
                              int window_width, int window_height,
                              int output_width, int output_height,
                              int *logical_x, int *logical_y);
int ts_sister_ui_midi_target(TsSisterUiHit hit, char *target,
                             size_t target_size);
void ts_sister_ui_render(TsFramebuffer *framebuffer,
                         const TsSisterUiModel *model,
                         const TsPalette *palette);

#endif
