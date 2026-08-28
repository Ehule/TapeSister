#ifndef TAPESISTER_UI_H
#define TAPESISTER_UI_H

#include <stdint.h>
#include "tapesister/audition.h"
#include "tapesister/browser.h"
#include "tapesister/capture.h"
#include "tapesister/config.h"
#include "tapesister/palette.h"
#include "tapesister/recipe.h"
#include "tapesister/sample.h"
#include "tapesister/transform.h"
#include "tapesister/dsp_recipe.h"
#include "tapesister/exchange.h"

enum { TS_UI_WIDTH = 640, TS_UI_HEIGHT = 400 };
enum { TS_UI_INPUT_LED_X = 263, TS_UI_INPUT_LED_Y = 12,
       TS_UI_INPUT_LED_W = 2, TS_UI_INPUT_LED_H = 9,
       TS_UI_INPUT_LED_STEP_X = 3 };
#define TS_UI_INPUT_ACTIVITY_HOLD_MS 140u
enum { TS_WAVE_X = 20, TS_WAVE_Y = 64, TS_WAVE_W = 600, TS_WAVE_H = 134 };
enum { TS_MODAL_PANEL_X = 10, TS_MODAL_PANEL_Y = 40,
       TS_MODAL_PANEL_W = 620, TS_MODAL_PANEL_H = 164 };
enum { TS_DRONE_WAVE_X = 20, TS_DRONE_WAVE_Y = 77,
       TS_DRONE_WAVE_W = 600, TS_DRONE_WAVE_H = 70 };
enum { TS_TRANSFORM_WAVE_X = 20, TS_TRANSFORM_WAVE_Y = 62,
       TS_TRANSFORM_WAVE_W = 600, TS_TRANSFORM_WAVE_H = 58 };
enum { TS_CONFIG_FIELD_X = 20, TS_CONFIG_FIELD_Y = 63,
       TS_CONFIG_FIELD_W = 600, TS_CONFIG_FIELD_H = 19,
       TS_CONFIG_FIELD_STEP_Y = 28 };
enum { TS_PALETTE_SWATCH_X = 20, TS_PALETTE_SWATCH_Y = 57,
       TS_PALETTE_SWATCH_W = 82, TS_PALETTE_SWATCH_H = 13,
       TS_PALETTE_SWATCH_COLUMNS = 7, TS_PALETTE_SWATCH_STEP_X = 86,
       TS_PALETTE_SWATCH_STEP_Y = 16 };
enum { TS_PALETTE_SLIDER_X = 20, TS_PALETTE_SLIDER_Y = 102,
       TS_PALETTE_SLIDER_W = 220, TS_PALETTE_SLIDER_H = 13,
       TS_PALETTE_SLIDER_STEP_Y = 16,
       TS_PALETTE_CONTRAST_X = 250, TS_PALETTE_CONTRAST_W = 170,
       TS_PALETTE_ACTION_Y = 174 };
enum { TS_CONFIG_ACTION_Y = 196 };
enum { TS_PALETTE_TAPEHEAD_X = 432, TS_PALETTE_TAPEHEAD_Y = 171,
       TS_PALETTE_TAPEHEAD_W = 7, TS_PALETTE_TAPEHEAD_H = 8,
       TS_PALETTE_TAPEHEAD_STEP_X = 9 };
enum {
    TS_BROWSER_LIST_X = 58,
    TS_BROWSER_LIST_Y = 87,
    TS_BROWSER_LIST_W = 494,
    TS_BROWSER_ROW_H = 17,
    TS_BROWSER_SCROLL_X = 558,
    TS_BROWSER_SCROLL_W = 18,
    TS_BROWSER_SCROLL_H = TS_BROWSER_VISIBLE_ROWS * TS_BROWSER_ROW_H
};

typedef enum {
    TS_FX_EDIT = 0,
    TS_FX_TUNE,
    TS_FX_NOISE,
    TS_FX_SHAPE,
    TS_FX_FAMILY,
    TS_FX_DELAY,
    TS_FX_SPACE,
    TS_FX_LOOP
} TsFxPage;

typedef enum {
    TS_UI_SLIDER_NONE = 0,
    TS_UI_SLIDER_BODY,
    TS_UI_SLIDER_EDGE,
    TS_UI_SLIDER_DRIFT,
    TS_UI_SLIDER_TUNE_FINE,
    TS_UI_SLIDER_TUNE_REFERENCE_VOLUME,
    TS_UI_SLIDER_NOISE_AMOUNT,
    TS_UI_SLIDER_FILTER_CUTOFF,
    TS_UI_SLIDER_FILTER_RESONANCE,
    TS_UI_SLIDER_SHAPER_DRIVE,
    TS_UI_SLIDER_SHAPER_MIX,
    TS_UI_SLIDER_VARIATION_RANGE,
    TS_UI_SLIDER_DELAY_TIME,
    TS_UI_SLIDER_DELAY_FEEDBACK,
    TS_UI_SLIDER_DELAY_DAMPING,
    TS_UI_SLIDER_DELAY_MIX,
    TS_UI_SLIDER_REVERB_DECAY,
    TS_UI_SLIDER_REVERB_DAMPING,
    TS_UI_SLIDER_REVERB_MIX,
    TS_UI_SLIDER_LOOP_CROSSFADE
} TsUiSlider;

typedef enum {
    TS_UI_TUNE_ACTION_NONE = 0,
    TS_UI_TUNE_ACTION_MATERIAL_SEMITONE_DOWN,
    TS_UI_TUNE_ACTION_MATERIAL_SEMITONE_UP,
    TS_UI_TUNE_ACTION_MATERIAL_CENT_DOWN,
    TS_UI_TUNE_ACTION_MATERIAL_CENT_UP,
    TS_UI_TUNE_ACTION_REFERENCE_DOWN,
    TS_UI_TUNE_ACTION_REFERENCE_UP,
    TS_UI_TUNE_ACTION_REFERENCE_TONE,
    TS_UI_TUNE_ACTION_DETECT_OR_MATCH
} TsUiTuneAction;

enum {
    TS_UI_BANK_MOD_SHIFT = 1u << 0,
    TS_UI_BANK_MOD_CTRL = 1u << 1,
    TS_UI_BANK_MOD_ALT = 1u << 2
};

typedef enum {
    TS_UI_BANK_ACTION_AUDITION = 0,
    TS_UI_BANK_ACTION_CAPTURE_CURRENT,
    TS_UI_BANK_ACTION_CAPTURE_LOOP,
    TS_UI_BANK_ACTION_CAPTURE_SELECTION,
    TS_UI_BANK_ACTION_CLONE,
    TS_UI_BANK_ACTION_RENAME,
    TS_UI_BANK_ACTION_CLEAR,
    TS_UI_BANK_ACTION_TOGGLE_LOCK,
    TS_UI_BANK_ACTION_INVALID
} TsUiBankAction;

typedef enum {
    TS_UI_CONFIG_ACTION_NONE = 0,
    TS_UI_CONFIG_ACTION_SAVE,
    TS_UI_CONFIG_ACTION_USE_CWD,
    TS_UI_CONFIG_ACTION_PALETTE,
    TS_UI_CONFIG_ACTION_CANCEL
} TsUiConfigAction;

typedef enum {
    TS_UI_PALETTE_ACTION_NONE = 0,
    TS_UI_PALETTE_ACTION_LOAD_SHARED,
    TS_UI_PALETTE_ACTION_SAVE_SHARED,
    TS_UI_PALETTE_ACTION_RESET,
    TS_UI_PALETTE_ACTION_DONE,
    TS_UI_PALETTE_ACTION_CANCEL
} TsUiPaletteAction;

typedef enum {
    TS_UI_LOAD_SELECTION_NONE = 0,
    TS_UI_LOAD_SELECTION_PASTE,
    TS_UI_LOAD_SELECTION_FIT,
    TS_UI_LOAD_SELECTION_CANCEL
} TsUiLoadSelectionAction;

typedef enum {
    TS_UI_EXCHANGE_NONE = 0,
    TS_UI_EXCHANGE_SEND,
    TS_UI_EXCHANGE_RECEIVE
} TsUiExchangeDialog;

typedef enum {
    TS_UI_EXCHANGE_ACTION_NONE = 0,
    TS_UI_EXCHANGE_ACTION_SEND_ONE_INSTRUMENT,
    TS_UI_EXCHANGE_ACTION_SEND_SEPARATE_INSTRUMENTS,
    TS_UI_EXCHANGE_ACTION_SEND_ALL_PAGES,
    TS_UI_EXCHANGE_ACTION_CHECK_INBOX,
    TS_UI_EXCHANGE_ACTION_TOGGLE_NEW_INSTANCE,
    TS_UI_EXCHANGE_ACTION_IMPORT,
    TS_UI_EXCHANGE_ACTION_LATER
} TsUiExchangeAction;

typedef enum {
    TS_UI_DRONE_ACTION_NONE = 0,
    TS_UI_DRONE_ACTION_PREVIEW,
    TS_UI_DRONE_ACTION_STOP,
    TS_UI_DRONE_ACTION_COPY,
    TS_UI_DRONE_ACTION_REPLACE,
    TS_UI_DRONE_ACTION_CANCEL
} TsUiDroneAction;

typedef enum {
    TS_UI_TRANSFORM_ACTION_NONE = 0,
    TS_UI_TRANSFORM_ACTION_RECIPE,
    TS_UI_TRANSFORM_ACTION_SELECTION,
    TS_UI_TRANSFORM_ACTION_WHOLE,
    TS_UI_TRANSFORM_ACTION_RENDER,
    TS_UI_TRANSFORM_ACTION_APPLY,
    TS_UI_TRANSFORM_ACTION_AUDITION,
    TS_UI_TRANSFORM_ACTION_SAVE,
    TS_UI_TRANSFORM_ACTION_BACK
} TsUiTransformAction;

typedef enum {
    TS_UI_FM_ACTION_NONE = 0,
    TS_UI_FM_ACTION_RANDOMIZE,
    TS_UI_FM_ACTION_APPLY,
    TS_UI_FM_ACTION_AUDITION,
    TS_UI_FM_ACTION_HOLD,
    TS_UI_FM_ACTION_DRONE,
    TS_UI_FM_ACTION_EXTREME,
    TS_UI_FM_ACTION_CHAIN,
    TS_UI_FM_ACTION_BANK_MAKER,
    TS_UI_FM_ACTION_PITCH_LOCK,
    TS_UI_FM_ACTION_PITCH_ROOT,
    TS_UI_FM_ACTION_PITCH_SCALE,
    TS_UI_FM_ACTION_APPLY_PITCHES,
    TS_UI_FM_ACTION_BANK_REPLACE,
    TS_UI_FM_ACTION_BANK_NEW_PAGE,
    TS_UI_FM_ACTION_BANK_CANCEL,
    TS_UI_FM_ACTION_OVERWRITE,
    TS_UI_FM_ACTION_NEW_PAGE,
    TS_UI_FM_ACTION_CANCEL_FULL,
    TS_UI_FM_ACTION_OUTPUT_TRIM,
    TS_UI_FM_ACTION_BACK
} TsUiFmAction;

typedef enum {
    TS_TRANSFORM_BACKEND_CDP = 0,
    TS_TRANSFORM_BACKEND_DSP
} TsTransformBackend;

typedef enum {
    TS_UI_WAVE_ACTION_NONE = 0,
    TS_UI_WAVE_ACTION_PLAY_ALL,
    TS_UI_WAVE_ACTION_PLAY_SELECTION,
    TS_UI_WAVE_ACTION_PLAY_VIEW,
    TS_UI_WAVE_ACTION_CROP,
    TS_UI_WAVE_ACTION_ZOOM_SELECTION,
    TS_UI_WAVE_ACTION_SELECT_ALL,
    TS_UI_WAVE_ACTION_SELECT_WAVE,
    TS_UI_WAVE_ACTION_SHOW_ALL,
    TS_UI_WAVE_ACTION_CLEAR_ALL,
    TS_UI_WAVE_ACTION_CYCLE_PANEL
} TsUiWaveAction;

typedef enum {
    TS_UI_CANVAS_ACTION_NONE = 0,
    TS_UI_CANVAS_ACTION_HALF,
    TS_UI_CANVAS_ACTION_DOUBLE,
    TS_UI_CANVAS_ACTION_GRID_COARSER,
    TS_UI_CANVAS_ACTION_GRID_FINER,
    TS_UI_CANVAS_ACTION_GRID_SNAP
} TsUiCanvasAction;

typedef enum {
    TS_UI_LOOP_START = 0,
    TS_UI_LOOP_LOCK_START,
    TS_UI_LOOP_LOCKED,
    TS_UI_LOOP_LOCK_RELEASE
} TsUiLoopCommand;

typedef enum {
    TS_UI_PANEL_SAMPLE_TILES = 0,
    TS_UI_PANEL_KEYBOARD,
    TS_UI_PANEL_CDP,
    TS_UI_PANEL_DSP
} TsUiPanel;

typedef enum {
    TS_UI_WAVEFORM_MAIN = 0,
    TS_UI_WAVEFORM_TRANSFORM,
    TS_UI_WAVEFORM_DRONE,
    TS_UI_WAVEFORM_COUNT
} TsUiWaveformKind;

typedef struct {
    uint32_t pixels[TS_UI_WIDTH * TS_UI_HEIGHT];
} TsFramebuffer;

/* High-resolution wheels, touchpads, and queued SDL wheel events can keep
   emitting after the pointer crosses a parameter or application window. */
#define TS_UI_WHEEL_HANDOFF_QUIET_MS 120u

typedef struct {
    int target;
    uint32_t last_event_ms;
    int active;
    int suppress_until_quiet;
} TsUiWheelGuard;

/* SDL motion events report a button mask, but that mask can outlive the
   window that received the corresponding button-down. Require an explicit
   local press before motion is allowed to edit a value. */
typedef struct {
    int target;
    uint32_t button_mask;
    int active;
} TsUiPointerDrag;

typedef struct {
    uint64_t waveform_revisions[TS_UI_WAVEFORM_COUNT];
    TsUiWheelGuard wheel_guard;
    uint32_t active_notes;
    int mouse_note;
    int selecting;
    int bank_clear_armed;
    int playback_active;
    int startup_welcome_installed;
    int startup_welcome_autoplay;
    int startup_welcome_playback_requested;
    int text_cursor_visible;
    int show_keyboard;
    int keyboard_octave;
    int keyboard_base_note;
    int show_recipes;
    int show_ingredients;
    int cdp_page;
    int dsp_page;
    TsCdpRecipeValues cdp_presets[TS_CDP_FACTORY_RECIPE_COUNT];
    TsDspRecipeValues dsp_presets[TS_DSP_FACTORY_RECIPE_COUNT];
    TsCaptureState capture_state;
    int capture_overdub;
    int external_record_bank;
    int sample_page;
    int sample_page_count;
    int monitor_enabled;
    TsRecordSource record_source;
    int input_meter_active;
    int input_clipping;
    float input_level;
    float input_peak;
    float input_threshold;
    uint32_t input_sample_rate;
    uint8_t input_available_channels;
    uint8_t input_activity_mask;
    size_t input_wave_columns;
    float input_wave_minimum[TS_WAVE_W];
    float input_wave_maximum[TS_WAVE_W];
    int capture_destination_slot;
    int capture_source_slot;
    int capture_channels;
    int sister_enabled;
    int sister_rolling;
    int sister_held;
    int sister_monitor_enabled;
    int sister_capture_active;
    int sister_warning;
    uint16_t sister_source_mask;
    size_t capture_recorded_frames;
    size_t capture_capacity_frames;
    uint32_t staged_notes;
    uint32_t overlay_until_ms;
    int workbench_loop_active;
    int workbench_loop_persistent;
    int bank_view_slot;
    int load_bank_slot;
    int playhead_bank_slot;
    int renaming_bank_slot;
    int renaming_recipe_slot;
    int export_choice_open;
    int overdub_confirm_open;
    int overdub_confirm_slot;
    int file_busy;
    int file_busy_phase;
    char file_busy_label[24];
    TsUiExchangeDialog exchange_dialog;
    TsExchangeLayout exchange_layout;
    int exchange_item_count;
    int exchange_force_new_instance;
    int load_selection_choice_open;
    int drone_open;
    int drone_preview_active;
    int drone_crossfade_dragging;
    int drone_crossfade_drag_start_x;
    int transform_open;
    int fm_open;
    TsFmPage fm_page;
    TsFmPatch fm_patch;
    const TsSample *fm_preview_sample;
    int fm_held_notes;
    int fm_full_choice_open;
    int fm_bank_choice_open;
    int fm_output_dragging;
    char fm_message[96];
    int transform_rendering;
    int transform_preview_available;
    int transform_preview_active;
    int transform_runtime_available;
    TsTransformBackend transform_backend;
    int transform_recipe_index;
    int transform_dsp_slot;
    int transform_selection_dragging;
    int transform_selection_drag_mode;
    size_t transform_selection_anchor;
    size_t transform_selection_length;
    size_t transform_selection_grab;
    TsTransformScope transform_scope;
    TsCdpRecipeValues transform_values;
    TsDspRecipeValues transform_dsp_values;
    const TsSample *transform_preview_sample;
    size_t transform_preview_first;
    size_t transform_preview_last;
    TsCdpSafetyStatus transform_safety;
    char transform_message[96];
    int exit_confirm_open;
    int exit_has_unsaved;
    int exit_choice;
    int exit_after_save;
    uint64_t saved_state_hash;
    int config_open;
    int palette_open;
    int palette_entry;
    int palette_channel;
    TsConfigField config_field;
    size_t config_cursor;
    int dragging_loop_endpoint;
    int loop_drag_started;
    int tape_dragging;
    int tape_drag_button;
    int wave_pointer_pending;
    int wave_pointer_button;
    int wave_pointer_start_x;
    int selecting_button;
    int amplitude_draw_mode;
    int amplitude_draw_dragging;
    int amplitude_last_x;
    size_t amplitude_last_frame;
    float amplitude_last_gain;
    int amplitude_profile_first_x;
    int amplitude_profile_last_x;
    float amplitude_profile[TS_WAVE_W];
    uint8_t amplitude_profile_set[TS_WAVE_W];
    float amplitude_polyline_base[TS_WAVE_W];
    uint8_t amplitude_polyline_base_set[TS_WAVE_W];
    int amplitude_polyline_mode;
    int amplitude_polyline_anchor_x;
    size_t amplitude_polyline_anchor_frame;
    float amplitude_polyline_anchor_gain;
    int amplitude_polyline_cursor_x;
    size_t amplitude_polyline_cursor_frame;
    float amplitude_polyline_cursor_gain;
    TsAmplitudeGesture amplitude_gesture;
    TsMaterialMacroGesture material_macro_gesture;
    float material_macro_amount;
    uint32_t material_macro_last_audition_ms;
    int material_macro_dragging;
    int material_macro_wheel_active;
    TsPostEditKind tape_drag_kind;
    TsFxPage fx_page;
    TsAuditionSource audition_source;
    TsAuditionSource playhead_source;
    TsBrowser browser;
    TsConfig config;
    TsConfig config_before_edit;
    TsPalette palette;
    TsPalette palette_before_edit;
    TsPalette palette_suggestions;
    TsRecipeBank recipes;
    TsTuning pitch_suggestion;
    TsTuning tune_reference;
    float pitch_confidence;
    int tune_reference_active;
    float warp_amount;
    TsWarpGesture warp_gesture;
    uint32_t warp_last_audition_ms;
    int warp_dragging;
    int warp_wheel_active;
    float smear_amount;
    TsSmearGesture smear_gesture;
    uint32_t smear_last_audition_ms;
    int smear_dragging;
    int smear_wheel_active;
    float tear_amount;
    TsTearGesture tear_gesture;
    uint32_t tear_last_audition_ms;
    int tear_dragging;
    int tear_wheel_active;
    TsStretchGesture stretch_gesture;
    int stretch_wheel_active;
    float stretch_wheel_semitones;
    int has_stretch_readout;
    float stretch_pitch_semitones;
    float stretch_duration_ratio;
    TsCanvasGesture canvas_gesture;
    int canvas_capture_raw_x;
    int canvas_capture_raw_y;
    int canvas_drag_logical_x;
    size_t canvas_drag_start_frames;
    float drone_effective_crossfade_ms;
    const TsSample *drone_preview_sample;
    size_t drone_source_first;
    size_t drone_source_last;
    size_t drone_split_frame;
    size_t drone_output_frames;
    size_t drone_overlap_frames;
    size_t drone_crossfade_drag_start_frames;
    uint64_t drone_source_hash;
    int drone_source_slot;
    int has_pitch_suggestion;
    size_t selection_anchor;
    size_t tape_source_first;
    size_t tape_source_last;
    size_t tape_grab_offset;
    int64_t tape_destination;
    size_t playhead_frame;
    size_t playhead_frames;
    const TsSample *playhead_sample;
    size_t parent_view_first;
    size_t parent_view_last;
    char bank_rename[128];
    size_t bank_rename_cursor;
    char recipe_rename[TS_RECIPE_NAME_MAX + 1];
    size_t recipe_rename_cursor;
    char load_selection_name[128];
    char exchange_name[96];
    char overlay[80];
    char status[160];
} TsUiState;

void ts_ui_init(TsUiState *ui);
void ts_ui_update_input_activity(TsUiState *ui,
                                 uint32_t hold_until_ms[8],
                                 uint32_t now_ms,
                                 uint32_t available_channels,
                                 uint32_t detected_mask);
void ts_ui_waveform_cache_invalidate(TsUiState *ui, TsUiWaveformKind kind);
void ts_ui_waveform_cache_reset_counters(void);
uint64_t ts_ui_waveform_cache_rebuild_count(TsUiWaveformKind kind);
int ts_ui_request_startup_welcome(TsUiState *ui, int splash_complete,
                                  int audio_ready);
const TsTuning *ts_ui_audition_tuning(const TsUiState *ui,
                                      const TsInstrument *instrument);
const TsTuning *ts_ui_display_tuning(const TsUiState *ui,
                                     const TsInstrument *instrument);
void ts_ui_render(TsFramebuffer *fb, const TsUiState *ui, const TsInstrument *instrument);
int ts_ui_foreground_panel_open(const TsUiState *ui);
void ts_ui_draw_tile_state_borders(TsFramebuffer *fb, int slot,
                                   int active, int sister_source,
                                   const TsPalette *palette);
int ts_ui_write_ppm(const TsFramebuffer *fb, const char *path);
int ts_ui_key_from_point(int x, int y);
int ts_ui_keyboard_base_note(const TsUiState *ui);
size_t ts_ui_right_drag_playhead_frame(size_t anchor, size_t pointer,
                                       size_t selection_first,
                                       size_t selection_last,
                                       size_t sample_frames);
int ts_ui_keyboard_set_octave(TsUiState *ui, int octave);
int ts_ui_keyboard_cycle_octave(TsUiState *ui, int amount);
int ts_ui_keyboard_shift_semitone(TsUiState *ui, int amount);
int ts_ui_config_field_from_point(int x, int y);
size_t ts_ui_config_cursor_from_point(const TsUiState *ui,
                                      TsConfigField field, int x);
TsUiConfigAction ts_ui_config_action_from_point(int x, int y);
int ts_ui_palette_entry_from_point(int x, int y);
int ts_ui_palette_tapehead_swatch_from_point(int x, int y);
int ts_ui_palette_channel_from_point(int x, int y, int *value);
TsUiPaletteAction ts_ui_palette_action_from_point(int x, int y);
TsUiLoadSelectionAction ts_ui_load_selection_action_from_point(int x, int y);
TsUiExchangeAction ts_ui_exchange_action_from_point(TsUiExchangeDialog dialog,
                                                     int x, int y);
TsUiDroneAction ts_ui_drone_action_from_point(int x, int y);
TsUiTransformAction ts_ui_transform_action_from_point(int x, int y);
int ts_ui_fm_button_from_point(int x, int y);
TsFmPage ts_ui_fm_page_from_point(int x, int y);
int ts_ui_fm_control_from_point(int x, int y);
int ts_ui_fm_voice_from_point(int x, int y);
uint32_t ts_ui_fm_mutation_from_point(int x, int y);
TsUiFmAction ts_ui_fm_action_from_point(int x, int y);
TsUiFmAction ts_ui_fm_bank_action_from_point(int x, int y);
TsUiFmAction ts_ui_fm_full_action_from_point(int x, int y);
int ts_ui_fm_range_contains(int x, int y);
int ts_ui_fm_pitch_root_contains(int x, int y);
int ts_ui_fm_pitch_scale_contains(int x, int y);
int ts_ui_transform_control_from_point(int x, int y);
int ts_ui_transform_mix_contains(int x, int y);
int ts_ui_transform_waveform_contains(int x, int y);
int ts_ui_amplitude_draw_toggle_contains(int x, int y);
int ts_ui_amplitude_draw_start_contains(int x, int y);
int ts_ui_amplitude_draw_local_x(int x);
TsUiTuneAction ts_ui_tune_action_from_point(int x, int y);
TsUiWaveAction ts_ui_wave_action_from_point(int x, int y);
TsUiCanvasAction ts_ui_canvas_action_from_point(int x, int y);
int ts_ui_capture_button_from_point(int x, int y);
int ts_ui_capture_channels_button_from_point(int x, int y);
int ts_ui_overdub_button_from_point(int x, int y);
int ts_ui_new_page_button_from_point(int x, int y);
int ts_ui_record_keep_button_from_point(int x, int y);
int ts_ui_monitor_button_from_point(int x, int y);
int ts_ui_logo_contains(int x, int y);
int ts_ui_fm_background_click_allowed(const TsUiState *ui, int x, int y);
int ts_ui_wheel_guard_accept(TsUiWheelGuard *guard, int target,
                             uint32_t now_ms);
void ts_ui_wheel_guard_interrupt(TsUiWheelGuard *guard, uint32_t now_ms);
void ts_ui_pointer_drag_begin(TsUiPointerDrag *drag, int target,
                              uint32_t button_mask);
void ts_ui_pointer_drag_cancel(TsUiPointerDrag *drag);
int ts_ui_pointer_drag_accept_motion(TsUiPointerDrag *drag, int target,
                                     uint32_t button_state);
int ts_ui_waveform_mode_contains(int x, int y);
int ts_ui_record_source_button_from_point(int x, int y);
int ts_ui_canvas_edge_from_point(int x, int y);
int ts_ui_drone_waveform_contains(int x, int y);
int ts_ui_drone_crossfade_handle_from_point(const TsUiState *ui, int x, int y);
TsUiSlider ts_ui_slider_from_point(const TsUiState *ui, int x, int y);
int ts_ui_palette_cycle_entry(int entry, int amount);
int ts_ui_palette_cycle_channel(int channel, int amount);
TsConfigField ts_ui_config_cycle_field(TsConfigField field, int amount);
void ts_ui_begin_palette_edit(TsUiState *ui);
void ts_ui_finish_palette_edit(TsUiState *ui, int cancel);
int ts_ui_bank_slot_from_point(int x, int y);
int ts_ui_recipe_slot_from_point(int x, int y);
int ts_ui_cdp_slot_from_point(int x, int y);
int ts_ui_cdp_page_from_point(int x, int y);
int ts_ui_dsp_page_from_point(int x, int y);
TsUiBankAction ts_ui_bank_action(int right_button, unsigned modifiers);
int ts_ui_execute_bank_action(TsInstrument *instrument, int slot,
                              TsUiBankAction action,
                              char *error, size_t error_size);
int ts_ui_tape_action(int right_button, unsigned modifiers, TsPostEditKind *kind);
void ts_ui_cycle_panel(TsUiState *ui);
TsUiPanel ts_ui_panel(const TsUiState *ui);
void ts_ui_select_panel(TsUiState *ui, TsUiPanel panel);
int ts_ui_transform_auto_audition_allowed(const TsUiState *ui);
TsUiLoopCommand ts_ui_loop_command(const TsUiState *ui, int shift_pressed);
int ts_ui_loop_transport_can_stop(const TsUiState *ui, int force);
int ts_ui_space_plays_selection(const TsInstrument *instrument);
void ts_ui_reset_parent_view(TsUiState *ui, size_t frames);
int ts_ui_zoom_parent_view(TsUiState *ui, size_t frames, size_t anchor,
                           float anchor_ratio, float scale);
int ts_ui_pan_parent_view(TsUiState *ui, size_t frames, ptrdiff_t amount);
size_t ts_ui_parent_frame_from_x(const TsUiState *ui, size_t frames, int x, int width);

#endif
