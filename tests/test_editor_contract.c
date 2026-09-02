#include "tapesister/note_bank.h"
#include "tapesister/ui.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CONTRACT(name, condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "EDITOR CONTRACT FAILED: %s (line %d)\n", name, __LINE__); \
        ++failures; \
    } \
} while (0)

static int execute(TsInstrument *instrument, int slot, TsUiBankAction action,
                   char *error, size_t error_size)
{
    return ts_ui_execute_bank_action(instrument, slot, action, error, error_size);
}

int main(void)
{
    CONTRACT("master_limiter_hit",
             ts_ui_master_limiter_contains(TS_UI_MASTER_LIMITER_X,
                                           TS_UI_MASTER_LIMITER_Y));
    CONTRACT("master_limiter_left_boundary",
             !ts_ui_master_limiter_contains(TS_UI_MASTER_LIMITER_X - 1,
                                            TS_UI_MASTER_LIMITER_Y));
    CONTRACT("master_output_hit",
             ts_ui_master_output_contains(TS_UI_MASTER_OUTPUT_X + 20,
                                          TS_UI_MASTER_OUTPUT_Y + 10));
    CONTRACT("master_output_slider_hit",
             ts_ui_slider_from_point(NULL, TS_UI_MASTER_OUTPUT_X + 20,
                                     TS_UI_MASTER_OUTPUT_Y + 10) ==
                 TS_UI_SLIDER_MASTER_OUTPUT);
    CONTRACT("master_output_zero",
             ts_ui_master_output_normalized_from_x(
                 TS_UI_MASTER_OUTPUT_X) == 0.0f);
    CONTRACT("master_output_unity",
             ts_ui_master_output_normalized_from_x(
                 TS_UI_MASTER_OUTPUT_X + TS_UI_MASTER_OUTPUT_W) == 1.0f);
    {
        TsInstrument portal_instrument;
        TsUiState portal_ui;
        TsFramebuffer portal_fb;
        uint32_t rolling_color;
        ts_instrument_init(&portal_instrument);
        ts_ui_init(&portal_ui);
        portal_ui.sister_enabled = 1;
        portal_ui.sister_rolling = 1;
        portal_ui.sister_monitor_enabled = 1;
        rolling_color =
            portal_ui.palette.colors[TS_PALETTE_STEREO_WAVE_LEFT];
        ts_ui_render(&portal_fb, &portal_ui, &portal_instrument);
        CONTRACT("portal_roll_marker_has_centered_tip",
                 portal_fb.pixels[21 * TS_UI_WIDTH + 144] == rolling_color);
        CONTRACT("portal_roll_marker_aligns_with_subtitle_top",
                 portal_fb.pixels[18 * TS_UI_WIDTH + 141] == rolling_color);
        CONTRACT("portal_roll_marker_has_lower_half",
                 portal_fb.pixels[24 * TS_UI_WIDTH + 141] == rolling_color);
        CONTRACT("portal_roll_marker_clears_subtitle_bottom",
                 portal_fb.pixels[25 * TS_UI_WIDTH + 144] != rolling_color);
        CONTRACT("portal_monitor_marker_aligns_with_subtitle_top",
                 portal_fb.pixels[18 * TS_UI_WIDTH + 148] ==
                 portal_ui.palette.colors[TS_PALETTE_STEREO_WAVE_SUM]);
        portal_ui.sister_held = 1;
        ts_ui_render(&portal_fb, &portal_ui, &portal_instrument);
        CONTRACT("portal_hold_marker_aligns_with_subtitle_top",
                 portal_fb.pixels[18 * TS_UI_WIDTH + 141] ==
                 portal_ui.palette.colors[TS_PALETTE_PATTERN_TUNING]);
        CONTRACT("portal_hold_marker_aligns_with_subtitle_bottom",
                 portal_fb.pixels[24 * TS_UI_WIDTH + 145] ==
                 portal_ui.palette.colors[TS_PALETTE_PATTERN_TUNING]);
        CONTRACT("portal_hold_marker_clears_subtitle_bottom",
                 portal_fb.pixels[25 * TS_UI_WIDTH + 145] !=
                 portal_ui.palette.colors[TS_PALETTE_PATTERN_TUNING]);
        ts_instrument_free(&portal_instrument);
    }
    TsInstrument instrument;
    TsNoteBank notes;
    TsAuditionPlan plan;
    char error[160];
    uint64_t tile_hash[4];

    {
        TsInstrument locked;
        TsUiState locked_ui;
        TsSample clipboard;
        size_t clipboard_origin = 0;
        ts_instrument_init(&locked);
        ts_ui_init(&locked_ui);
        ts_sample_init(&clipboard);
        locked_ui.workbench_loop_active = 1;
        locked_ui.workbench_loop_persistent = 1;
        CONTRACT("loop_lock_fixture_activates",
                 ts_instrument_activate_silence(&locked, 256, 44100,
                                                error, sizeof(error)));
        for (size_t frame = 0; frame < locked.current.frames; ++frame) {
            float value = frame & 1u ? -0.25f : 0.25f;
            locked.current.data[frame] = value;
            locked.parent.data[frame] = value;
        }
        locked.view_first = 16;
        locked.view_last = 192;
        ts_instrument_set_selection(&locked, 32, 96);
        CONTRACT("loop_lock_selection_range_follows_selection",
                 ts_audition_plan(&locked, TS_AUDITION_CURRENT,
                                  TS_AUDITION_WORKBENCH_LOOP, &plan) &&
                 plan.first == 32 && plan.last == 96);
        CONTRACT("loop_lock_survives_transform",
                 ts_instrument_apply_sample_edit(
                     &locked, TS_SAMPLE_EDIT_REVERSE, 1.0f,
                     error, sizeof(error)) &&
                 ts_ui_loop_command(&locked_ui, 0) == TS_UI_LOOP_LOCKED);
        CONTRACT("loop_lock_survives_copy",
                 ts_instrument_copy_selection(&locked, &clipboard,
                                              &clipboard_origin,
                                              error, sizeof(error)) &&
                 locked_ui.workbench_loop_persistent);
        CONTRACT("loop_lock_survives_paste",
                 ts_instrument_paste(&locked, &clipboard, clipboard_origin, 0,
                                     error, sizeof(error)) &&
                 locked_ui.workbench_loop_persistent);
        CONTRACT("loop_lock_survives_multiply",
                 ts_instrument_double_canvas(&locked, error, sizeof(error)) &&
                 locked_ui.workbench_loop_persistent);
        CONTRACT("loop_lock_survives_undo",
                 ts_instrument_undo(&locked, error, sizeof(error)) &&
                 locked_ui.workbench_loop_persistent);
        CONTRACT("loop_lock_survives_redo",
                 ts_instrument_redo(&locked, error, sizeof(error)) &&
                 locked_ui.workbench_loop_persistent);
        CONTRACT("loop_lock_survives_capture",
                 ts_instrument_bank_capture(&locked, 1, TS_BANK_CAPTURE_CURRENT,
                                            error, sizeof(error)) &&
                 locked_ui.workbench_loop_persistent);
        ts_instrument_clear_selection(&locked);
        CONTRACT("loop_lock_view_range_follows_visible_waveform",
                 ts_audition_plan(&locked, TS_AUDITION_CURRENT,
                                  TS_AUDITION_WORKBENCH_LOOP, &plan) &&
                 plan.first == locked.view_first && plan.last == locked.view_last);
        CONTRACT("loop_lock_survives_selection_clear",
                 locked_ui.workbench_loop_persistent &&
                 !ts_ui_loop_transport_can_stop(&locked_ui, 0));
        CONTRACT("loop_lock_survives_create",
                 ts_instrument_create_selected(&locked, 0x4c4f434bu,
                                               error, sizeof(error)) &&
                 locked_ui.workbench_loop_persistent);
        CONTRACT("loop_lock_survives_vary",
                 ts_instrument_vary_selected(&locked, 0, NULL,
                                             error, sizeof(error)) &&
                 locked_ui.workbench_loop_persistent);
        CONTRACT("plain_loop_cannot_release_lock",
                 ts_ui_loop_command(&locked_ui, 0) == TS_UI_LOOP_LOCKED);
        CONTRACT("only_shift_loop_requests_release",
                 ts_ui_loop_command(&locked_ui, 1) == TS_UI_LOOP_LOCK_RELEASE);
        ts_sample_free(&clipboard);
        ts_instrument_free(&locked);
    }

    CONTRACT("bank_plain_click_routes_to_select_play",
             ts_ui_bank_action(0, 0) == TS_UI_BANK_ACTION_AUDITION);
    CONTRACT("bank_shift_click_routes_to_full_capture",
             ts_ui_bank_action(0, TS_UI_BANK_MOD_SHIFT) ==
             TS_UI_BANK_ACTION_CAPTURE_CURRENT);
    CONTRACT("bank_alt_click_routes_to_loop_capture",
             ts_ui_bank_action(0, TS_UI_BANK_MOD_ALT) ==
             TS_UI_BANK_ACTION_CAPTURE_LOOP);
    CONTRACT("bank_ctrl_click_routes_to_selection_capture",
             ts_ui_bank_action(0, TS_UI_BANK_MOD_CTRL) ==
             TS_UI_BANK_ACTION_CAPTURE_SELECTION);
    CONTRACT("bank_ctrl_shift_click_routes_to_clone",
             ts_ui_bank_action(0, TS_UI_BANK_MOD_CTRL | TS_UI_BANK_MOD_SHIFT) ==
             TS_UI_BANK_ACTION_CLONE);
    CONTRACT("bank_ctrl_alt_click_routes_to_protection_toggle",
             ts_ui_bank_action(0, TS_UI_BANK_MOD_CTRL | TS_UI_BANK_MOD_ALT) ==
             TS_UI_BANK_ACTION_TOGGLE_LOCK);
    CONTRACT("bank_right_click_routes_to_rename",
             ts_ui_bank_action(1, 0) == TS_UI_BANK_ACTION_RENAME);
    CONTRACT("bank_shift_right_click_routes_to_clear",
             ts_ui_bank_action(1, TS_UI_BANK_MOD_SHIFT) == TS_UI_BANK_ACTION_CLEAR);
    CONTRACT("unsupported_bank_modifier_combo_is_rejected",
             ts_ui_bank_action(0, TS_UI_BANK_MOD_SHIFT | TS_UI_BANK_MOD_ALT) ==
             TS_UI_BANK_ACTION_INVALID);
    CONTRACT("wave_toolbar_play_all_hitbox",
             ts_ui_wave_action_from_point(20, 300) == TS_UI_WAVE_ACTION_PLAY_ALL);
    CONTRACT("wave_toolbar_play_selection_hitbox",
             ts_ui_wave_action_from_point(80, 300) ==
             TS_UI_WAVE_ACTION_PLAY_SELECTION);
    CONTRACT("wave_toolbar_play_view_hitbox",
             ts_ui_wave_action_from_point(150, 300) == TS_UI_WAVE_ACTION_PLAY_VIEW);
    CONTRACT("wave_toolbar_crop_hitbox",
             ts_ui_wave_action_from_point(220, 300) == TS_UI_WAVE_ACTION_CROP);
    CONTRACT("wave_toolbar_zoom_selection_hitbox",
             ts_ui_wave_action_from_point(250, 300) ==
             TS_UI_WAVE_ACTION_ZOOM_SELECTION);
    CONTRACT("wave_toolbar_select_all_hitbox",
             ts_ui_wave_action_from_point(320, 300) ==
             TS_UI_WAVE_ACTION_SELECT_ALL);
    CONTRACT("wave_toolbar_select_wave_hitbox",
             ts_ui_wave_action_from_point(380, 300) ==
             TS_UI_WAVE_ACTION_SELECT_WAVE);
    CONTRACT("wave_toolbar_show_all_hitbox",
             ts_ui_wave_action_from_point(440, 300) == TS_UI_WAVE_ACTION_SHOW_ALL);
    CONTRACT("wave_toolbar_clear_all_hitbox",
             ts_ui_wave_action_from_point(520, 300) == TS_UI_WAVE_ACTION_CLEAR_ALL);
    CONTRACT("wave_toolbar_panel_hitbox",
             ts_ui_wave_action_from_point(600, 300) ==
             TS_UI_WAVE_ACTION_CYCLE_PANEL);
    CONTRACT("waveform_mode_compact_button_hitbox",
             ts_ui_waveform_mode_contains(610, 50));
    CONTRACT("waveform_mode_former_wide_hitbox_is_inert",
             !ts_ui_waveform_mode_contains(540, 50));
    CONTRACT("wave_toolbar_gap_is_inert",
             ts_ui_wave_action_from_point(70, 300) == TS_UI_WAVE_ACTION_NONE);
    CONTRACT("wave_toolbar_stops_above_lower_panel",
             ts_ui_wave_action_from_point(320, 318) == TS_UI_WAVE_ACTION_NONE);
    CONTRACT("canvas_half_hitbox",
             ts_ui_canvas_action_from_point(30, 190) ==
             TS_UI_CANVAS_ACTION_HALF);
    CONTRACT("canvas_double_hitbox",
             ts_ui_canvas_action_from_point(60, 190) ==
             TS_UI_CANVAS_ACTION_DOUBLE);
    CONTRACT("canvas_grid_coarser_hitbox",
             ts_ui_canvas_action_from_point(460, 190) ==
             TS_UI_CANVAS_ACTION_GRID_COARSER);
    CONTRACT("canvas_division_readout_is_informational",
             ts_ui_canvas_action_from_point(500, 190) ==
             TS_UI_CANVAS_ACTION_NONE);
    CONTRACT("canvas_grid_finer_hitbox",
             ts_ui_canvas_action_from_point(545, 190) ==
             TS_UI_CANVAS_ACTION_GRID_FINER);
    CONTRACT("canvas_grid_snap_hitbox",
             ts_ui_canvas_action_from_point(590, 190) ==
             TS_UI_CANVAS_ACTION_GRID_SNAP);
    CONTRACT("canvas_controls_do_not_cover_lower_controls",
             ts_ui_canvas_action_from_point(30, 205) ==
             TS_UI_CANVAS_ACTION_NONE);
    CONTRACT("canvas_left_edge_handle_hitbox",
             ts_ui_canvas_edge_from_point(25, 120) == 1);
    CONTRACT("canvas_right_edge_handle_hitbox",
             ts_ui_canvas_edge_from_point(612, 120) == 2);
    CONTRACT("canvas_handle_gap_is_inert",
             ts_ui_canvas_edge_from_point(320, 100) == 0);
    CONTRACT("canvas_handles_stop_before_control_strip",
             ts_ui_canvas_edge_from_point(25, 190) == 0);
    CONTRACT("amplitude_draw_toggle_is_top_right_of_waveform",
             ts_ui_amplitude_draw_toggle_contains(580, 75));
    CONTRACT("amplitude_draw_toggle_rejects_waveform_body",
             !ts_ui_amplitude_draw_toggle_contains(320, 120));
    CONTRACT("amplitude_draw_accepts_left_frame_gutter",
             ts_ui_amplitude_draw_start_contains(TS_WAVE_X - 4, 120) &&
             ts_ui_amplitude_draw_local_x(TS_WAVE_X - 4) == 0);
    CONTRACT("amplitude_draw_left_frame_snaps_to_exact_edge",
             ts_ui_amplitude_draw_local_x(TS_WAVE_X + 3) == 0);
    CONTRACT("amplitude_draw_accepts_right_frame_gutter",
             ts_ui_amplitude_draw_start_contains(
                 TS_WAVE_X + TS_WAVE_W + 4, 120) &&
             ts_ui_amplitude_draw_local_x(
                 TS_WAVE_X + TS_WAVE_W + 4) == TS_WAVE_W - 1);
    CONTRACT("amplitude_draw_right_frame_snaps_to_exact_edge",
             ts_ui_amplitude_draw_local_x(
                 TS_WAVE_X + TS_WAVE_W - 4) == TS_WAVE_W - 1);
    CONTRACT("amplitude_draw_gutter_does_not_extend_canvas_hit_area",
             !ts_ui_amplitude_draw_start_contains(
                 TS_WAVE_X - 7, 120));

    {
        TsInstrument visual;
        TsUiState visual_ui;
        TsFramebuffer visual_fb;
        int waveform_y = TS_WAVE_Y + TS_WAVE_H / 2 -
                         (int)(0.5f * (TS_WAVE_H / 2 - 6));
        ts_instrument_init(&visual);
        ts_ui_init(&visual_ui);
        CONTRACT("selection_render_fixture_activates",
                 ts_instrument_activate_silence(&visual, 1200, 44100,
                                                error, sizeof(error)));
        for (size_t frame = 0; frame < visual.current.frames; ++frame)
            visual.current.data[frame] = 0.5f;
        ts_instrument_set_selection(&visual, 1, 1199);
        ts_ui_render(&visual_fb, &visual_ui, &visual);
        CONTRACT("selection_waveform_color_covers_straddling_left_pixel",
                 visual_fb.pixels[waveform_y * TS_UI_WIDTH + TS_WAVE_X] ==
                 visual_ui.palette.colors[TS_PALETTE_TEXT_ON_BLOCK]);
        ts_instrument_free(&visual);
    }

    {
        TsInstrument visual;
        TsUiState visual_ui;
        TsFramebuffer visual_fb;
        const char *readout = "CANVAS 1.000 S (+0.500 S)";
        int draw_x = TS_WAVE_X + TS_WAVE_W - 54;
        int readout_width = (int)strlen(readout) * 6 - 1;
        int readout_x = draw_x - 8 - readout_width;
        int final_unit_x = readout_x + 23 * 6;
        ts_instrument_init(&visual);
        ts_ui_init(&visual_ui);
        CONTRACT("canvas_readout_fixture_activates",
                 ts_instrument_activate_silence(&visual, 48000, 48000,
                                                error, sizeof(error)));
        visual_ui.canvas_gesture.active = 1;
        visual_ui.canvas_drag_start_frames = 24000;
        ts_ui_render(&visual_fb, &visual_ui, &visual);
        CONTRACT("canvas_resize_readout_ends_before_draw_tile",
                 final_unit_x + 5 < draw_x);
        CONTRACT("canvas_resize_readout_keeps_final_unit_visible",
                 visual_fb.pixels[(TS_WAVE_Y + 5) * TS_UI_WIDTH +
                                  final_unit_x + 1] ==
                 visual_ui.palette.colors[TS_PALETTE_PATTERN_EFFECT]);
        ts_instrument_free(&visual);
    }

    {
        TsInstrument visual;
        TsUiState visual_ui;
        TsFramebuffer visual_fb;
        int lane_height = TS_WAVE_H / 2;
        int left_middle = TS_WAVE_Y + lane_height / 2;
        int right_middle = TS_WAVE_Y + lane_height +
                           (TS_WAVE_H - lane_height) / 2;
        int left_y = left_middle - (int)lrintf(0.6f *
                                               (lane_height / 2 - 4));
        int right_y = right_middle - (int)lrintf(0.6f *
            ((TS_WAVE_H - lane_height) / 2 - 4));
        int x = TS_WAVE_X + TS_WAVE_W / 2;
        ts_instrument_init(&visual);
        ts_ui_init(&visual_ui);
        CONTRACT("stereo_lane_fixture_activates",
                 ts_instrument_activate_silence_channels(
                     &visual, 1200, 44100, 2u, error, sizeof(error)));
        for (size_t frame = 0; frame < visual.current.frames; ++frame) {
            visual.current.data[frame * 2u] = 0.6f;
            visual.current.data[frame * 2u + 1u] = 0.6f;
        }
        visual_ui.config.waveform_display_mode = TS_WAVEFORM_DISPLAY_STEREO;
        ts_ui_render(&visual_fb, &visual_ui, &visual);
        CONTRACT("stereo_canvas_left_uses_upper_lane",
                 visual_fb.pixels[left_y * TS_UI_WIDTH + x] ==
                 visual_ui.palette.colors[TS_PALETTE_STEREO_WAVE_LEFT]);
        CONTRACT("stereo_canvas_right_uses_lower_lane",
                 visual_fb.pixels[right_y * TS_UI_WIDTH + x] ==
                 visual_ui.palette.colors[TS_PALETTE_STEREO_WAVE_RIGHT]);
        ts_instrument_free(&visual);
    }

    {
        TsInstrument visual;
        TsUiState visual_ui;
        TsFramebuffer full_fb;
        TsFramebuffer zoom_fb;
        TsFramebuffer snap_fb;
        size_t target = 500;
        int y = TS_WAVE_Y + 35;
        int full_x = TS_WAVE_X + (int)(target * TS_WAVE_W / 1600u);
        int zoom_x = TS_WAVE_X + (int)((target - 400u) * TS_WAVE_W / 800u);
        uint32_t full_grid;
        ts_instrument_init(&visual);
        ts_ui_init(&visual_ui);
        CONTRACT("grid_render_fixture_activates",
                 ts_instrument_activate_silence(&visual, 1600, 44100,
                                                error, sizeof(error)));
        CONTRACT("grid_division_fixture_sets_div16",
                 visual.grid_divisions == 16u);
        ts_ui_render(&full_fb, &visual_ui, &visual);
        full_grid = full_fb.pixels[y * TS_UI_WIDTH + full_x];
        CONTRACT("grid_target_is_visible_in_full_canvas",
                 full_grid != full_fb.pixels[y * TS_UI_WIDTH + full_x + 2]);
        visual.view_first = 400;
        visual.view_last = 1200;
        ts_ui_render(&zoom_fb, &visual_ui, &visual);
        CONTRACT("zoomed_grid_uses_same_full_canvas_target",
                 zoom_fb.pixels[y * TS_UI_WIDTH + zoom_x] == full_grid);
        CONTRACT("zoomed_grid_does_not_stay_at_old_screen_coordinate",
                 zoom_fb.pixels[y * TS_UI_WIDTH + full_x] != full_grid);
        CONTRACT("grid_snap_toggle_activates",
                 ts_instrument_toggle_grid_snap(&visual));
        ts_ui_render(&snap_fb, &visual_ui, &visual);
        CONTRACT("snap_on_renders_grid_more_prominently",
                 snap_fb.pixels[y * TS_UI_WIDTH + zoom_x] != full_grid);
        CONTRACT("canvas_left_handle_renders_inside_wave_panel",
                 snap_fb.pixels[120 * TS_UI_WIDTH + 23] ==
                 visual_ui.palette.colors[TS_PALETTE_PATTERN_EFFECT]);
        ts_instrument_free(&visual);
    }

    ts_instrument_init(&instrument);
    CONTRACT("bank_clear_all_accepts_empty_instrument",
             ts_instrument_bank_clear_all(&instrument, error, sizeof(error)));
    CONTRACT("bank_empty_click_selects_create_destination",
             execute(&instrument, 0, TS_UI_BANK_ACTION_AUDITION,
                     error, sizeof(error)) &&
             instrument.selected_slot == 0 && !instrument.bank[0].occupied);
    CONTRACT("bank_empty_click_does_not_create_a_play_plan",
             !ts_bank_audition_plan(&instrument, 0, &plan));
    CONTRACT("post_clear_all_create_uses_selected_destination",
             ts_instrument_create_selected(&instrument, 0x434f4e30u,
                                           error, sizeof(error)) &&
             instrument.selected_slot == 0 && instrument.bank[0].occupied);
    instrument.view_first = 100; instrument.view_last = 900;
    ts_instrument_set_selection(&instrument, 160, 760);
    instrument.has_loop = 1; instrument.loop_first = 200; instrument.loop_last = 700;
    instrument.loop_mode = TS_LOOP_REVERSE;
    {
        TsProcessRecipe process = instrument.process;
        process.edge = 0.57f;
        CONTRACT("tile_process_edit_is_accepted",
                 ts_instrument_set_process(&instrument, &process, error, sizeof(error)));
    }
    CONTRACT("tile_post_edit_is_accepted",
             ts_instrument_apply_tear(&instrument, 0.55f, error, sizeof(error)));
    tile_hash[0] = ts_sample_hash(&instrument.current);

    CONTRACT("bank_clone_is_independent",
             execute(&instrument, 1, TS_UI_BANK_ACTION_CLONE, error, sizeof(error)) &&
             instrument.selected_slot == 1 && instrument.bank[1].occupied &&
             instrument.bank[0].sample.data != instrument.bank[1].sample.data &&
             instrument.bank[0].edit_parent.data != instrument.bank[1].edit_parent.data);
    CONTRACT("bank_clone_restores_complete_editor_state",
             instrument.view_first == 100 && instrument.view_last == 900 &&
             instrument.has_selection && instrument.selection_first == 160 &&
             instrument.selection_last == 760 && instrument.has_loop &&
             instrument.loop_first == 200 && instrument.loop_last == 700 &&
             instrument.loop_mode == TS_LOOP_REVERSE &&
             fabsf(instrument.process.edge) < 0.0001f &&
             instrument.post_edit_count == 1);

    CONTRACT("warp_is_scoped_to_active_tile",
             ts_instrument_apply_warp(&instrument, 0.73f, error, sizeof(error)) &&
             ts_sample_hash(&instrument.current) != tile_hash[0] &&
             ts_sample_hash(&instrument.bank[0].sample) == tile_hash[0]);
    tile_hash[1] = ts_sample_hash(&instrument.current);

    CONTRACT("bank_occupied_click_switches_edit_owner",
             execute(&instrument, 0, TS_UI_BANK_ACTION_AUDITION,
                     error, sizeof(error)) &&
             instrument.selected_slot == 0 &&
             ts_sample_hash(&instrument.current) == tile_hash[0]);
    CONTRACT("second_clone_starts_from_selected_owner",
             execute(&instrument, 2, TS_UI_BANK_ACTION_CLONE, error, sizeof(error)) &&
             instrument.selected_slot == 2);
    CONTRACT("smear_is_scoped_to_active_tile",
             ts_instrument_apply_smear(&instrument, 0.68f, error, sizeof(error)) &&
             ts_sample_hash(&instrument.current) != tile_hash[0] &&
             ts_sample_hash(&instrument.bank[0].sample) == tile_hash[0] &&
             ts_sample_hash(&instrument.bank[1].sample) == tile_hash[1]);
    tile_hash[2] = ts_sample_hash(&instrument.current);

    CONTRACT("third_clone_starts_from_selected_owner",
             execute(&instrument, 0, TS_UI_BANK_ACTION_AUDITION,
                     error, sizeof(error)) &&
             execute(&instrument, 3, TS_UI_BANK_ACTION_CLONE, error, sizeof(error)));
    CONTRACT("tear_is_scoped_to_active_tile",
             ts_instrument_apply_tear(&instrument, 0.92f, error, sizeof(error)) &&
             ts_sample_hash(&instrument.current) != tile_hash[0] &&
             ts_sample_hash(&instrument.bank[0].sample) == tile_hash[0] &&
             ts_sample_hash(&instrument.bank[1].sample) == tile_hash[1] &&
             ts_sample_hash(&instrument.bank[2].sample) == tile_hash[2]);
    tile_hash[3] = ts_sample_hash(&instrument.current);

    CONTRACT("three_tile_switch_restores_audio_and_editor_state",
             execute(&instrument, 2, TS_UI_BANK_ACTION_AUDITION,
                     error, sizeof(error)) &&
             ts_sample_hash(&instrument.current) == tile_hash[2] &&
             instrument.view_first == 100 && instrument.view_last == 900 &&
             instrument.has_selection && instrument.has_loop &&
             fabsf(instrument.process.edge) < 0.0001f &&
             instrument.post_edit_count == 1);
    CONTRACT("bank_occupied_click_produces_selected_tile_play_plan",
             ts_bank_audition_plan(&instrument, 2, &plan) &&
             plan.sample == &instrument.bank[2].sample &&
             ts_sample_hash(plan.sample) == ts_sample_hash(&instrument.current));

    ts_note_bank_init(&notes);
    CONTRACT("laptop_keyboard_uses_selected_tile_current",
             ts_note_bank_start(&notes, &instrument, TS_AUDITION_CURRENT,
                                0, 0, 44100) == TS_NOTE_STARTED &&
             ts_note_bank_display_voice(&notes) != NULL &&
             ts_note_bank_display_voice(&notes)->sample == &instrument.current);
    ts_note_bank_clear(&notes);
    CONTRACT("clickable_keyboard_uses_selected_tile_current",
             ts_note_bank_start(&notes, &instrument, TS_AUDITION_CURRENT,
                                7, 1, 44100) == TS_NOTE_STARTED &&
             ts_note_bank_display_voice(&notes) != NULL &&
             ts_note_bank_display_voice(&notes)->sample == &instrument.current);

    CONTRACT("sample_metadata_loop_plan_is_distinct",
             ts_audition_plan(&instrument, TS_AUDITION_CURRENT,
                              TS_AUDITION_LOOP, &plan) &&
             plan.first == instrument.loop_first && plan.last == instrument.loop_last);
    CONTRACT("workbench_loop_selection_has_priority",
             ts_audition_plan(&instrument, TS_AUDITION_CURRENT,
                              TS_AUDITION_WORKBENCH_LOOP, &plan) &&
             plan.first == instrument.selection_first &&
             plan.last == instrument.selection_last);
    for (int mode = TS_LOOP_FORWARD; mode < TS_LOOP_MODE_COUNT; ++mode) {
        int direction = mode == TS_LOOP_REVERSE ? -1 : 1;
        float value = ts_audition_read_looped_mode(
            &instrument.current, (double)instrument.loop_first,
            instrument.loop_first, instrument.loop_last, 0, (TsLoopMode)mode);
        (void)ts_audition_loop_position((double)instrument.loop_first,
                                        instrument.loop_first, instrument.loop_last,
                                        0, (TsLoopMode)mode, &direction);
        CONTRACT("forward_reverse_pingpong_loop_read_is_finite", isfinite(value));
    }

    CONTRACT("active_bank_clear_leaves_selected_empty_destination",
             execute(&instrument, instrument.selected_slot, TS_UI_BANK_ACTION_CLEAR,
                     error, sizeof(error)) && instrument.selected_slot == 2 &&
             !instrument.bank[2].occupied && instrument.parent.data == NULL &&
             instrument.current.data == NULL);

    CONTRACT("slot_01_has_no_clear_privilege",
             execute(&instrument, 0, TS_UI_BANK_ACTION_CLEAR, error, sizeof(error)) &&
             !instrument.bank[0].occupied);
    CONTRACT("clear_all_empties_all_16_peer_tiles",
             ts_instrument_bank_clear_all(&instrument, error, sizeof(error)) &&
             ts_instrument_bank_count(&instrument) == 0);
    for (int slot = 0; slot < TS_BANK_SLOT_COUNT; ++slot)
        CONTRACT("every_bank_tile_is_empty_after_clear_all", !instrument.bank[slot].occupied);

    ts_instrument_free(&instrument);
    if (failures != 0) return 1;
    puts("TapeSister protected editor interaction contract passed");
    (void)tile_hash[3];
    return 0;
}
