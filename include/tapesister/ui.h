#ifndef TAPESISTER_UI_H
#define TAPESISTER_UI_H

#include <stdint.h>
#include "tapesister/audition.h"
#include "tapesister/browser.h"
#include "tapesister/config.h"
#include "tapesister/recipe.h"
#include "tapesister/sample.h"

enum { TS_UI_WIDTH = 640, TS_UI_HEIGHT = 400 };
enum { TS_WAVE_X = 20, TS_WAVE_Y = 64, TS_WAVE_W = 600, TS_WAVE_H = 134 };
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
    TS_UI_BANK_ACTION_RENAME,
    TS_UI_BANK_ACTION_CLEAR,
    TS_UI_BANK_ACTION_INVALID
} TsUiBankAction;

typedef struct {
    uint32_t pixels[TS_UI_WIDTH * TS_UI_HEIGHT];
} TsFramebuffer;

typedef struct {
    uint32_t active_notes;
    int mouse_note;
    int selecting;
    int commit_armed;
    int bank_clear_armed;
    int playback_active;
    int text_cursor_visible;
    int show_keyboard;
    int show_recipes;
    int bank_view_slot;
    int playhead_bank_slot;
    int renaming_bank_slot;
    int renaming_recipe_slot;
    int export_choice_open;
    int exit_confirm_open;
    int exit_has_unsaved;
    uint64_t saved_state_hash;
    int config_open;
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
    TsRecipeBank recipes;
    TsTuning pitch_suggestion;
    float pitch_confidence;
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
const TsTuning *ts_ui_audition_tuning(const TsUiState *ui,
                                      const TsInstrument *instrument);
const TsTuning *ts_ui_display_tuning(const TsUiState *ui,
                                     const TsInstrument *instrument);
void ts_ui_render(TsFramebuffer *fb, const TsUiState *ui, const TsInstrument *instrument);
int ts_ui_write_ppm(const TsFramebuffer *fb, const char *path);
int ts_ui_key_from_point(int x, int y);
int ts_ui_bank_slot_from_point(int x, int y);
int ts_ui_recipe_slot_from_point(int x, int y);
TsUiBankAction ts_ui_bank_action(int right_button, unsigned modifiers);
int ts_ui_tape_action(int right_button, unsigned modifiers, TsPostEditKind *kind);
void ts_ui_reset_parent_view(TsUiState *ui, size_t frames);
int ts_ui_zoom_parent_view(TsUiState *ui, size_t frames, size_t anchor,
                           float anchor_ratio, float scale);
int ts_ui_pan_parent_view(TsUiState *ui, size_t frames, ptrdiff_t amount);
size_t ts_ui_parent_frame_from_x(const TsUiState *ui, size_t frames, int x, int width);

#endif
