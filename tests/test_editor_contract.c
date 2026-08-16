#include "tapesister/note_bank.h"
#include "tapesister/ui.h"

#include <math.h>
#include <stdio.h>

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
    TsInstrument instrument;
    TsNoteBank notes;
    TsAuditionPlan plan;
    char error[160];
    uint64_t tile_hash[4];

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
    CONTRACT("bank_right_click_routes_to_rename",
             ts_ui_bank_action(1, 0) == TS_UI_BANK_ACTION_RENAME);
    CONTRACT("bank_shift_right_click_routes_to_clear",
             ts_ui_bank_action(1, TS_UI_BANK_MOD_SHIFT) == TS_UI_BANK_ACTION_CLEAR);
    CONTRACT("unsupported_bank_modifier_combo_is_rejected",
             ts_ui_bank_action(0, TS_UI_BANK_MOD_SHIFT | TS_UI_BANK_MOD_ALT) ==
             TS_UI_BANK_ACTION_INVALID);

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
             fabsf(instrument.process.edge - 0.57f) < 0.0001f &&
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
             fabsf(instrument.process.edge - 0.57f) < 0.0001f &&
             instrument.post_edit_count == 2);
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
