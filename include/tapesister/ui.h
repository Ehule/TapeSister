#ifndef TAPESISTER_UI_H
#define TAPESISTER_UI_H

#include <stdint.h>
#include "tapesister/audition.h"
#include "tapesister/browser.h"
#include "tapesister/config.h"
#include "tapesister/palette.h"
#include "tapesister/recipe.h"
#include "tapesister/sample.h"

enum { TS_UI_WIDTH = 640, TS_UI_HEIGHT = 400 };
enum { TS_WAVE_X = 20, TS_WAVE_Y = 64, TS_WAVE_W = 600, TS_WAVE_H = 134 };
enum { TS_MODAL_PANEL_X = 10, TS_MODAL_PANEL_Y = 40,
       TS_MODAL_PANEL_W = 620, TS_MODAL_PANEL_H = 164 };
enum { TS_CONFIG_FIELD_X = 20, TS_CONFIG_FIELD_Y = 63,
       TS_CONFIG_FIELD_W = 600, TS_CONFIG_FIELD_H = 19,
       TS_CONFIG_FIELD_STEP_Y = 31 };
enum { TS_PALETTE_SWATCH_X = 20, TS_PALETTE_SWATCH_Y = 57,
       TS_PALETTE_SWATCH_W = 82, TS_PALETTE_SWATCH_H = 13,
       TS_PALETTE_SWATCH_COLUMNS = 7, TS_PALETTE_SWATCH_STEP_X = 86,
       TS_PALETTE_SWATCH_STEP_Y = 16 };
enum { TS_PALETTE_SLIDER_X = 20, TS_PALETTE_SLIDER_Y = 102,
       TS_PALETTE_SLIDER_W = 220, TS_PALETTE_SLIDER_H = 13,
       TS_PALETTE_SLIDER_STEP_Y = 16,
       TS_PALETTE_CONTRAST_X = 250, TS_PALETTE_CONTRAST_W = 170,
       TS_PALETTE_ACTION_Y = 174 };
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
    TS_UI_PALETTE_ACTION_IMPORT_TAPEHEAD,
    TS_UI_PALETTE_ACTION_SAVE_TAPESISTER,
    TS_UI_PALETTE_ACTION_EXPORT_TAPEHEAD,
    TS_UI_PALETTE_ACTION_RESET,
    TS_UI_PALETTE_ACTION_DONE,
    TS_UI_PALETTE_ACTION_CANCEL
} TsUiPaletteAction;

typedef struct {
    uint32_t pixels[TS_UI_WIDTH * TS_UI_HEIGHT];
} TsFramebuffer;

typedef struct {
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
    int show_recipes;
    int show_ingredients;
    int workbench_loop_active;
    int bank_view_slot;
    int load_bank_slot;
    int playhead_bank_slot;
    int renaming_bank_slot;
    int renaming_recipe_slot;
    int export_choice_open;
    int exit_confirm_open;
    int exit_has_unsaved;
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
    TsPostEditKind tape_drag_kind;
    TsFxPage fx_page;
    TsAuditionSource audition_source;
    TsAuditionSource playhead_source;
    TsBrowser browser;
    TsConfig config;
    TsConfig config_before_edit;
    TsPalette palette;
    TsPalette palette_before_edit;
    TsRecipeBank recipes;
    TsTuning pitch_suggestion;
    float pitch_confidence;
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
    int has_pitch_suggestion;
    size_t selection_anchor;
    size_t tape_source_first;
    size_t tape_source_last;
    size_t tape_grab_offset;
    int64_t tape_destination;
    size_t playhead_frame;
    size_t playhead_frames;
    size_t parent_view_first;
    size_t parent_view_last;
    char bank_rename[128];
    size_t bank_rename_cursor;
    char recipe_rename[TS_RECIPE_NAME_MAX + 1];
    size_t recipe_rename_cursor;
    char status[160];
} TsUiState;

void ts_ui_init(TsUiState *ui);
int ts_ui_request_startup_welcome(TsUiState *ui, int splash_complete,
                                  int audio_ready);
const TsTuning *ts_ui_audition_tuning(const TsUiState *ui,
                                      const TsInstrument *instrument);
const TsTuning *ts_ui_display_tuning(const TsUiState *ui,
                                     const TsInstrument *instrument);
void ts_ui_render(TsFramebuffer *fb, const TsUiState *ui, const TsInstrument *instrument);
int ts_ui_write_ppm(const TsFramebuffer *fb, const char *path);
int ts_ui_key_from_point(int x, int y);
int ts_ui_config_field_from_point(int x, int y);
size_t ts_ui_config_cursor_from_point(const TsUiState *ui,
                                      TsConfigField field, int x);
TsUiConfigAction ts_ui_config_action_from_point(int x, int y);
int ts_ui_palette_entry_from_point(int x, int y);
int ts_ui_palette_channel_from_point(int x, int y, int *value);
TsUiPaletteAction ts_ui_palette_action_from_point(int x, int y);
int ts_ui_palette_cycle_entry(int entry, int amount);
int ts_ui_palette_cycle_channel(int channel, int amount);
TsConfigField ts_ui_config_cycle_field(TsConfigField field, int amount);
void ts_ui_begin_palette_edit(TsUiState *ui);
void ts_ui_finish_palette_edit(TsUiState *ui, int cancel);
int ts_ui_bank_slot_from_point(int x, int y);
int ts_ui_recipe_slot_from_point(int x, int y);
TsUiBankAction ts_ui_bank_action(int right_button, unsigned modifiers);
int ts_ui_execute_bank_action(TsInstrument *instrument, int slot,
                              TsUiBankAction action,
                              char *error, size_t error_size);
int ts_ui_tape_action(int right_button, unsigned modifiers, TsPostEditKind *kind);
void ts_ui_cycle_panel(TsUiState *ui);
int ts_ui_transform_auto_audition_allowed(const TsUiState *ui);
void ts_ui_reset_parent_view(TsUiState *ui, size_t frames);
int ts_ui_zoom_parent_view(TsUiState *ui, size_t frames, size_t anchor,
                           float anchor_ratio, float scale);
int ts_ui_pan_parent_view(TsUiState *ui, size_t frames, ptrdiff_t amount);
size_t ts_ui_parent_frame_from_x(const TsUiState *ui, size_t frames, int x, int width);

#endif
