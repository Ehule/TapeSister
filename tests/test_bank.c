#include "tapesister/audition.h"
#include "tapesister/note_bank.h"
#include "tapesister/ui.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "BANK CHECK FAILED line %d: %s\n", __LINE__, #x); ++failures; } } while (0)

static void setup(TsInstrument *instrument)
{
    char error[160];
    enum { FRAMES = 1024 };
    ts_instrument_init(instrument);
    instrument->parent.data = malloc(FRAMES * sizeof(float));
    instrument->parent.frames = FRAMES;
    instrument->parent.sample_rate = 44100;
    for (size_t i = 0; i < FRAMES; ++i)
        instrument->parent.data[i] = 0.7f * sinf((float)i * 0.071f) +
                                     0.2f * cosf((float)i * 0.193f);
    if (!ts_sample_clone(&instrument->current, &instrument->parent,
                         error, sizeof(error))) abort();
    instrument->crop_last = FRAMES;
    instrument->view_last = FRAMES;
}

int main(void)
{
    TsInstrument bank, all, destinations, clone_reset, serial, restored, empty_restored;
    TsInstrument blank, blank_restored;
    TsTearGesture tear;
    TsNoteBank notes;
    TsAuditionPlan plan;
    char error[160];
    int failures = 0;
    uint64_t current_hash;
    uint64_t slot0_hash;
    uint64_t slot5_hash;
    uint64_t slot7_before_tear;

    CHECK(ts_ui_bank_action(0, 0) == TS_UI_BANK_ACTION_AUDITION);
    CHECK(ts_ui_bank_action(0, TS_UI_BANK_MOD_SHIFT) == TS_UI_BANK_ACTION_CAPTURE_CURRENT);
    CHECK(ts_ui_bank_action(0, TS_UI_BANK_MOD_ALT) == TS_UI_BANK_ACTION_CAPTURE_LOOP);
    CHECK(ts_ui_bank_action(0, TS_UI_BANK_MOD_CTRL) == TS_UI_BANK_ACTION_CAPTURE_SELECTION);
    CHECK(ts_ui_bank_action(0, TS_UI_BANK_MOD_SHIFT | TS_UI_BANK_MOD_CTRL) ==
          TS_UI_BANK_ACTION_CLONE);
    CHECK(ts_ui_bank_action(0, TS_UI_BANK_MOD_CTRL | TS_UI_BANK_MOD_ALT) ==
          TS_UI_BANK_ACTION_TOGGLE_LOCK);
    CHECK(ts_ui_bank_action(1, 0) == TS_UI_BANK_ACTION_RENAME);
    CHECK(ts_ui_bank_action(1, TS_UI_BANK_MOD_SHIFT) == TS_UI_BANK_ACTION_CLEAR);
    CHECK(ts_ui_bank_action(0, TS_UI_BANK_MOD_SHIFT | TS_UI_BANK_MOD_ALT) ==
          TS_UI_BANK_ACTION_INVALID);

    setup(&bank);
    current_hash = ts_sample_hash(&bank.current);
    CHECK(ts_ui_execute_bank_action(&bank, 0, TS_UI_BANK_ACTION_CAPTURE_CURRENT,
                                    error, sizeof(error)));
    slot0_hash = ts_sample_hash(&bank.bank[0].sample);
    CHECK(slot0_hash == current_hash && bank.bank[0].sample.frames == bank.current.frames);

    ts_instrument_set_selection(&bank, 700, bank.current.frames);
    CHECK(ts_ui_execute_bank_action(&bank, 3, TS_UI_BANK_ACTION_CAPTURE_SELECTION,
                                    error, sizeof(error)));
    CHECK(bank.bank[3].sample.frames == bank.current.frames - 700u);
    CHECK(memcmp(bank.bank[3].sample.data, bank.current.data + 700,
                 bank.bank[3].sample.frames * sizeof(float)) == 0);

    CHECK(!ts_ui_execute_bank_action(&bank, 4, TS_UI_BANK_ACTION_CAPTURE_LOOP,
                                     error, sizeof(error)));
    bank.has_loop = 1; bank.loop_first = 100; bank.loop_last = 420;
    bank.loop_mode = TS_LOOP_REVERSE; bank.loop_crossfade_ms = 7.0f;
    CHECK(ts_ui_execute_bank_action(&bank, 4, TS_UI_BANK_ACTION_CAPTURE_LOOP,
                                    error, sizeof(error)));
    CHECK(bank.bank[4].sample.frames == 320u && bank.bank[4].has_loop &&
          bank.bank[4].loop_first == 0 && bank.bank[4].loop_last == 320u &&
          bank.bank[4].loop_mode == TS_LOOP_REVERSE && bank.selected_slot == 0);

    bank.current.data[0] += 0.125f;
    CHECK(ts_ui_execute_bank_action(&bank, 5, TS_UI_BANK_ACTION_CAPTURE_CURRENT,
                                    error, sizeof(error)));
    slot5_hash = ts_sample_hash(&bank.bank[5].sample);
    CHECK(slot5_hash != slot0_hash && bank.selected_slot == 0);
    CHECK(ts_ui_execute_bank_action(&bank, 0, TS_UI_BANK_ACTION_RENAME,
                                    error, sizeof(error)));
    CHECK(ts_instrument_bank_rename(&bank, 0, "ONE", error, sizeof(error)));
    CHECK(ts_instrument_bank_rename(&bank, 5, "SIX", error, sizeof(error)));
    CHECK(strcmp(bank.bank[0].sample.name, "ONE") == 0 &&
          strcmp(bank.bank[5].sample.name, "SIX") == 0);

    CHECK(ts_ui_execute_bank_action(&bank, 0, TS_UI_BANK_ACTION_AUDITION,
                                    error, sizeof(error)));
    CHECK(ts_bank_audition_plan(&bank, 0, &plan));
    CHECK(plan.sample == &bank.bank[0].sample && bank.selected_slot == 0 &&
          ts_sample_hash(&bank.current) == slot0_hash);
    CHECK(ts_sample_hash(&bank.bank[5].sample) == slot5_hash);
    CHECK(ts_ui_execute_bank_action(&bank, 0, TS_UI_BANK_ACTION_TOGGLE_LOCK,
                                    error, sizeof(error)));
    CHECK(ts_instrument_bank_is_locked(&bank, 0));
    CHECK(!ts_ui_execute_bank_action(&bank, 0, TS_UI_BANK_ACTION_CLEAR,
                                     error, sizeof(error)));
    CHECK(bank.bank[0].occupied);
    CHECK(ts_ui_execute_bank_action(&bank, 0, TS_UI_BANK_ACTION_TOGGLE_LOCK,
                                    error, sizeof(error)));
    CHECK(!ts_instrument_bank_is_locked(&bank, 0));
    CHECK(ts_ui_execute_bank_action(&bank, 2, TS_UI_BANK_ACTION_AUDITION,
                                    error, sizeof(error)));
    CHECK(bank.selected_slot == 2 && !bank.bank[2].occupied &&
          bank.parent.data == NULL && bank.current.data == NULL &&
          ts_sample_hash(&bank.bank[0].sample) == slot0_hash);

    CHECK(ts_ui_execute_bank_action(&bank, 0, TS_UI_BANK_ACTION_CLEAR,
                                    error, sizeof(error)));
    CHECK(!bank.bank[0].occupied && bank.bank[3].occupied &&
          bank.bank[4].occupied && bank.bank[5].occupied);
    CHECK(ts_sample_hash(&bank.bank[5].sample) == slot5_hash &&
          strcmp(bank.bank[5].sample.name, "SIX") == 0);
    CHECK(ts_ui_execute_bank_action(&bank, 5, TS_UI_BANK_ACTION_CLEAR,
                                    error, sizeof(error)));
    CHECK(!bank.bank[5].occupied && bank.bank[3].occupied && bank.bank[4].occupied);
    CHECK(!ts_ui_execute_bank_action(&bank, 2, TS_UI_BANK_ACTION_INVALID,
                                     error, sizeof(error)));

    setup(&all);
    for (int slot = 0; slot < TS_BANK_SLOT_COUNT; ++slot) {
        all.current.data[0] = (float)slot / 16.0f;
        CHECK(ts_ui_execute_bank_action(&all, slot, TS_UI_BANK_ACTION_CAPTURE_CURRENT,
                                        error, sizeof(error)));
        CHECK(all.bank[slot].occupied);
    }
    CHECK(ts_instrument_bank_count(&all) == TS_BANK_SLOT_COUNT);
    CHECK(ts_instrument_bank_set_locked(&all, 3, 1, error, sizeof(error)));
    CHECK(ts_instrument_bank_clear_all(&all, error, sizeof(error)));
    CHECK(ts_instrument_bank_count(&all) == 1);
    CHECK(all.bank[3].occupied && all.bank[3].locked && all.selected_slot == 3);
    CHECK(ts_instrument_bank_set_locked(&all, 3, 0, error, sizeof(error)));
    CHECK(ts_instrument_bank_clear_all(&all, error, sizeof(error)));
    CHECK(ts_instrument_bank_count(&all) == 0);
    CHECK(ts_instrument_create_selected(&all, 0x434c4541u, error, sizeof(error)));
    CHECK(all.bank[0].occupied && all.current.data != NULL);

    ts_instrument_init(&destinations);
    CHECK(ts_instrument_generate(&destinations, TS_GENERATOR_PULSE, 0x44535441u,
                                 error, sizeof(error)));
    ts_instrument_set_selection(&destinations, 20, 300);
    CHECK(ts_instrument_apply_sample_edit(&destinations, TS_SAMPLE_EDIT_GAIN, 0.6f,
                                          error, sizeof(error)));
    slot0_hash = ts_sample_hash(&destinations.current);
    CHECK(ts_ui_execute_bank_action(&destinations, 4, TS_UI_BANK_ACTION_AUDITION,
                                    error, sizeof(error)));
    CHECK(destinations.selected_slot == 4 && !destinations.bank[4].occupied &&
          destinations.current.data == NULL && destinations.parent.data == NULL &&
          ts_sample_hash(&destinations.bank[0].sample) == slot0_hash &&
          destinations.bank[0].edit.sample_edit_count == 1);
    CHECK(ts_instrument_create_selected(&destinations, 0x44535442u,
                                        error, sizeof(error)));
    CHECK(destinations.selected_slot == 4 && destinations.bank[4].occupied &&
          destinations.current.data != NULL &&
          ts_sample_hash(&destinations.bank[0].sample) == slot0_hash);
    slot5_hash = ts_sample_hash(&destinations.bank[4].sample);

    CHECK(ts_sample_save_wav16(&destinations.bank[0].sample, "test-bank-load.wav",
                               error, sizeof(error)));
    CHECK(ts_ui_execute_bank_action(&destinations, 6, TS_UI_BANK_ACTION_AUDITION,
                                    error, sizeof(error)));
    CHECK(destinations.selected_slot == 6 && !destinations.bank[6].occupied &&
          destinations.current.data == NULL);
    CHECK(ts_instrument_load_wav(&destinations, "test-bank-load.wav",
                                 error, sizeof(error)));
    CHECK(destinations.selected_slot == 6 && destinations.bank[6].occupied &&
          ts_sample_hash(&destinations.current) ==
              ts_sample_hash(&destinations.bank[6].sample) &&
          destinations.current.frames == destinations.bank[0].sample.frames &&
          ts_sample_hash(&destinations.bank[0].sample) == slot0_hash &&
          ts_sample_hash(&destinations.bank[4].sample) == slot5_hash);

    CHECK(ts_ui_execute_bank_action(&destinations, 6, TS_UI_BANK_ACTION_CLEAR,
                                    error, sizeof(error)));
    CHECK(destinations.selected_slot == 6 && !destinations.bank[6].occupied &&
          destinations.parent.data == NULL && destinations.current.data == NULL &&
          ts_sample_hash(&destinations.bank[0].sample) == slot0_hash &&
          ts_sample_hash(&destinations.bank[4].sample) == slot5_hash);
    CHECK(ts_instrument_create_selected(&destinations, 0x44535443u,
                                        error, sizeof(error)));
    CHECK(destinations.selected_slot == 6 && destinations.bank[6].occupied &&
          destinations.current.data != NULL &&
          ts_sample_hash(&destinations.bank[0].sample) == slot0_hash &&
          ts_sample_hash(&destinations.bank[4].sample) == slot5_hash);
    remove("test-bank-load.wav");

    {
        TsInstrument crop_sync;
        TsFmSeedSequence stamp_sequence;
        uint64_t cropped_hash;
        ts_instrument_init(&crop_sync);
        CHECK(ts_ui_execute_bank_action(
            &crop_sync, 3, TS_UI_BANK_ACTION_AUDITION,
            error, sizeof(error)));
        CHECK(ts_instrument_activate_silence(
            &crop_sync, 4096u, 8000u, error, sizeof(error)));
        ts_instrument_set_selection(&crop_sync, 1024u, 2048u);
        ts_fm_seed_sequence_init(
            &stamp_sequence, UINT64_C(0x0123456789abcdef));
        CHECK(ts_instrument_stamp_create_fresh(
            &crop_sync, &stamp_sequence, NULL, error, sizeof(error)));
        CHECK(crop_sync.post_edit_count == 1);
        CHECK(ts_instrument_stamp_vary(
            &crop_sync, error, sizeof(error)));
        CHECK(crop_sync.post_edit_count == 2);
        CHECK(ts_instrument_crop_selection(
            &crop_sync, error, sizeof(error)));
        cropped_hash = ts_sample_hash(&crop_sync.current);
        CHECK(crop_sync.current.frames == 1024u &&
              !crop_sync.has_selection && crop_sync.post_edit_count == 3);
        CHECK(crop_sync.bank[3].sample.frames == crop_sync.current.frames &&
              ts_sample_hash(&crop_sync.bank[3].sample) == cropped_hash);
        CHECK(ts_ui_execute_bank_action(
            &crop_sync, 3, TS_UI_BANK_ACTION_AUDITION,
            error, sizeof(error)));
        CHECK(crop_sync.current.frames == 1024u &&
              ts_sample_hash(&crop_sync.current) == cropped_hash &&
              !crop_sync.has_selection && crop_sync.post_edit_count == 3);
        CHECK(ts_instrument_undo(&crop_sync, error, sizeof(error)) &&
              crop_sync.current.frames == 4096u &&
              crop_sync.has_selection &&
              crop_sync.selection_first == 1024u &&
              crop_sync.selection_last == 2048u);
        CHECK(ts_instrument_redo(&crop_sync, error, sizeof(error)) &&
              crop_sync.current.frames == 1024u &&
              ts_sample_hash(&crop_sync.current) == cropped_hash &&
              !crop_sync.has_selection);
        ts_instrument_free(&crop_sync);
    }

    ts_instrument_init(&clone_reset);
    CHECK(ts_instrument_generate(&clone_reset, TS_GENERATOR_PULSE, 0x434c4f4eu,
                                 error, sizeof(error)));
    clone_reset.view_first = 100; clone_reset.view_last = 700;
    ts_instrument_set_selection(&clone_reset, 150, 650);
    clone_reset.has_loop = 1; clone_reset.loop_first = 180; clone_reset.loop_last = 620;
    clone_reset.loop_mode = TS_LOOP_PING_PONG;
    {
        TsProcessRecipe clone_process = clone_reset.process;
        clone_process.edge = 0.64f;
        CHECK(ts_instrument_set_process(&clone_reset, &clone_process,
                                        error, sizeof(error)));
    }
    CHECK(ts_instrument_apply_tear(&clone_reset, 0.62f, error, sizeof(error)));
    CHECK(ts_instrument_apply_sample_edit(&clone_reset, TS_SAMPLE_EDIT_GAIN, 0.8f,
                                          error, sizeof(error)));
    CHECK(ts_instrument_undo(&clone_reset, error, sizeof(error)));
    slot0_hash = ts_sample_hash(&clone_reset.current);
    CHECK(clone_reset.post_edit_count == 1 && clone_reset.undo_count > 0 &&
          clone_reset.redo_count == 1);
    CHECK(ts_ui_execute_bank_action(&clone_reset, 5, TS_UI_BANK_ACTION_CLONE,
                                    error, sizeof(error)));
    CHECK(clone_reset.selected_slot == 5 && clone_reset.bank[0].occupied &&
          clone_reset.bank[5].occupied &&
          ts_sample_hash(&clone_reset.bank[0].sample) == slot0_hash &&
          ts_sample_hash(&clone_reset.bank[5].sample) == slot0_hash &&
          clone_reset.bank[0].sample.data != clone_reset.bank[5].sample.data &&
          clone_reset.bank[0].edit_parent.data != clone_reset.bank[5].edit_parent.data &&
          clone_reset.bank[0].undo != clone_reset.bank[5].undo &&
          clone_reset.bank[0].redo != clone_reset.bank[5].redo);
    CHECK(clone_reset.view_first == 100 && clone_reset.view_last == 700 &&
          clone_reset.has_selection && clone_reset.selection_first == 150 &&
          clone_reset.selection_last == 650 && clone_reset.has_loop &&
          clone_reset.loop_first == 180 && clone_reset.loop_last == 620 &&
          clone_reset.loop_mode == TS_LOOP_PING_PONG &&
          fabsf(clone_reset.process.edge) < 0.0001f &&
          clone_reset.post_edit_count == 1 && clone_reset.redo_count == 1);
    CHECK(ts_instrument_apply_tear(&clone_reset, 0.91f, error, sizeof(error)));
    slot5_hash = ts_sample_hash(&clone_reset.current);
    CHECK(slot5_hash != slot0_hash &&
          ts_sample_hash(&clone_reset.bank[0].sample) == slot0_hash);
    {
        int undo_before_reset = clone_reset.undo_count;
        CHECK(ts_instrument_reset_current(&clone_reset, error, sizeof(error)));
        CHECK(clone_reset.undo_count == undo_before_reset + 1 &&
              ts_sample_hash(&clone_reset.current) ==
                  ts_sample_hash(&clone_reset.parent) &&
              ts_sample_hash(&clone_reset.bank[5].sample) ==
                  ts_sample_hash(&clone_reset.current) &&
              clone_reset.view_first == 0 &&
              clone_reset.view_last == clone_reset.current.frames &&
              !clone_reset.has_selection && !clone_reset.has_loop &&
              clone_reset.post_edit_count == 0);
        CHECK(ts_instrument_undo(&clone_reset, error, sizeof(error)) &&
              ts_sample_hash(&clone_reset.current) == slot5_hash &&
              clone_reset.view_first == 100 && clone_reset.view_last == 700 &&
              clone_reset.has_selection && clone_reset.selection_first == 150 &&
              clone_reset.selection_last == 650 && clone_reset.post_edit_count == 1);
        CHECK(ts_instrument_redo(&clone_reset, error, sizeof(error)) &&
              ts_sample_hash(&clone_reset.current) ==
                  ts_sample_hash(&clone_reset.parent) &&
              clone_reset.view_first == 0 &&
              clone_reset.view_last == clone_reset.current.frames &&
              !clone_reset.has_selection && clone_reset.post_edit_count == 0);
    }
    CHECK(ts_instrument_select_bank(&clone_reset, 0, error, sizeof(error)) &&
          ts_sample_hash(&clone_reset.current) == slot0_hash &&
          clone_reset.view_first == 100 && clone_reset.view_last == 700 &&
          clone_reset.has_selection && clone_reset.post_edit_count == 1);
    CHECK(ts_instrument_select_bank(&clone_reset, 5, error, sizeof(error)) &&
          ts_sample_hash(&clone_reset.current) == ts_sample_hash(&clone_reset.parent) &&
          clone_reset.view_first == 0 &&
          clone_reset.view_last == clone_reset.current.frames &&
          !clone_reset.has_selection && clone_reset.post_edit_count == 0);

    ts_instrument_init(&serial); ts_instrument_init(&restored);
    ts_instrument_init(&empty_restored);
    ts_instrument_init(&blank); ts_instrument_init(&blank_restored);
    CHECK(ts_instrument_generate(&serial, TS_GENERATOR_PULSE, 0x42414e4bu,
                                 error, sizeof(error)));
    CHECK(ts_instrument_bank_rename(&serial, 0, "INDEPENDENT ONE",
                                    error, sizeof(error)));
    CHECK(ts_ui_execute_bank_action(&serial, 7, TS_UI_BANK_ACTION_CAPTURE_CURRENT,
                                    error, sizeof(error)));
    CHECK(ts_instrument_bank_rename(&serial, 7, "INDEPENDENT EIGHT",
                                    error, sizeof(error)));
    CHECK(ts_instrument_bank_set_locked(&serial, 7, 1, error, sizeof(error)));
    serial.view_first = 20; serial.view_last = 300;
    ts_instrument_set_selection(&serial, 40, 280);
    serial.has_loop = 1; serial.loop_first = 60; serial.loop_last = 240;
    CHECK(ts_instrument_apply_sample_edit(&serial, TS_SAMPLE_EDIT_GAIN, 0.8f,
                                          error, sizeof(error)));
    slot0_hash = ts_sample_hash(&serial.current);

    CHECK(ts_ui_execute_bank_action(&serial, 7, TS_UI_BANK_ACTION_AUDITION,
                                    error, sizeof(error)));
    CHECK(serial.selected_slot == 7 &&
          ts_sample_hash(&serial.current) == ts_sample_hash(&serial.bank[7].sample));
    CHECK(serial.view_first == 0 && serial.view_last == serial.current.frames &&
          !serial.has_selection && !serial.has_loop);
    serial.view_first = 100; serial.view_last = 600;
    ts_instrument_set_selection(&serial, 150, 700);
    serial.has_loop = 1; serial.loop_first = 180; serial.loop_last = 620;
    CHECK(ts_instrument_apply_sample_edit(&serial, TS_SAMPLE_EDIT_GAIN, 0.35f,
                                          error, sizeof(error)));
    slot5_hash = ts_sample_hash(&serial.current);
    slot7_before_tear = slot5_hash;
    CHECK(slot5_hash != slot0_hash);

    CHECK(ts_ui_execute_bank_action(&serial, 0, TS_UI_BANK_ACTION_AUDITION,
                                    error, sizeof(error)));
    CHECK(serial.selected_slot == 0 && ts_sample_hash(&serial.current) == slot0_hash &&
          serial.view_first == 20 && serial.view_last == 300 &&
          serial.has_selection && serial.selection_first == 40 &&
          serial.selection_last == 280 && serial.has_loop &&
          serial.loop_first == 60 && serial.loop_last == 240);
    CHECK(ts_sample_hash(&serial.bank[7].sample) == slot5_hash);

    CHECK(ts_ui_execute_bank_action(&serial, 7, TS_UI_BANK_ACTION_AUDITION,
                                    error, sizeof(error)));
    CHECK(serial.selected_slot == 7 && ts_sample_hash(&serial.current) == slot5_hash &&
          serial.view_first == 100 && serial.view_last == 600 &&
          serial.has_selection && serial.selection_first == 150 &&
          serial.selection_last == 700 && serial.has_loop &&
          serial.loop_first == 180 && serial.loop_last == 620);

    ts_instrument_clear_selection(&serial);
    ts_tear_gesture_init(&tear);
    CHECK(ts_instrument_tear_gesture_begin(&serial, &tear, error, sizeof(error)));
    CHECK(ts_instrument_tear_gesture_preview(&serial, &tear, 0.8f,
                                             error, sizeof(error)));
    CHECK(ts_instrument_tear_gesture_commit(&serial, &tear, error, sizeof(error)));
    {
        uint64_t transformed = ts_sample_hash(&serial.current);
        CHECK(transformed != slot5_hash && serial.selected_slot == 7 &&
              ts_sample_hash(&serial.bank[7].sample) == transformed &&
              ts_sample_hash(&serial.bank[0].sample) == slot0_hash);
        CHECK(ts_instrument_undo(&serial, error, sizeof(error)) &&
              ts_sample_hash(&serial.current) == slot5_hash &&
              ts_sample_hash(&serial.bank[0].sample) == slot0_hash);
        CHECK(ts_instrument_redo(&serial, error, sizeof(error)) &&
              ts_sample_hash(&serial.current) == transformed &&
              ts_sample_hash(&serial.bank[0].sample) == slot0_hash);
        slot5_hash = transformed;
    }

    ts_note_bank_init(&notes);
    CHECK(ts_note_bank_start(&notes, &serial, TS_AUDITION_CURRENT,
                             0, 0, 44100) == TS_NOTE_STARTED);
    CHECK(ts_note_bank_display_voice(&notes) != NULL &&
          ts_note_bank_display_voice(&notes)->sample == &serial.current &&
          ts_sample_hash(ts_note_bank_display_voice(&notes)->sample) == slot5_hash);
    CHECK(ts_instrument_bank_rename(&serial, 0, "INDEPENDENT ONE",
                                    error, sizeof(error)));
    CHECK(ts_instrument_bank_rename(&serial, 7, "INDEPENDENT EIGHT",
                                    error, sizeof(error)));
    CHECK(ts_instrument_save_recipe(&serial, "test-bank-independent.tsr",
                                    error, sizeof(error)));
    CHECK(ts_instrument_load_recipe(&restored, "test-bank-independent.tsr",
                                    error, sizeof(error)));
    CHECK(restored.selected_slot == 7 && restored.bank[0].occupied &&
          restored.bank[7].occupied &&
          restored.bank[7].locked && !restored.bank[0].locked &&
          strcmp(restored.bank[0].sample.name, "INDEPENDENT ONE") == 0 &&
          strcmp(restored.bank[7].sample.name, "INDEPENDENT EIGHT") == 0 &&
          ts_sample_hash(&restored.bank[0].sample) == slot0_hash &&
          ts_sample_hash(&restored.bank[7].sample) == slot5_hash);
    CHECK(restored.view_first == 100 && restored.view_last == 600 &&
          !restored.has_selection && restored.has_loop &&
          restored.loop_first == 180 && restored.loop_last == 620 &&
          restored.undo_count > 0);
    CHECK(ts_instrument_undo(&restored, error, sizeof(error)) &&
          ts_sample_hash(&restored.current) == slot7_before_tear);
    CHECK(ts_instrument_redo(&restored, error, sizeof(error)) &&
          ts_sample_hash(&restored.current) == slot5_hash);
    CHECK(ts_instrument_select_bank(&restored, 0, error, sizeof(error)) &&
          ts_sample_hash(&restored.current) == slot0_hash &&
          restored.view_first == 20 && restored.view_last == 300 &&
          restored.has_selection && restored.selection_first == 40 &&
          restored.selection_last == 280 && restored.has_loop &&
          restored.loop_first == 60 && restored.loop_last == 240 &&
          restored.undo_count > 0);
    CHECK(ts_instrument_select_bank(&restored, 7, error, sizeof(error)) &&
          ts_sample_hash(&restored.current) == slot5_hash &&
          restored.view_first == 100 && restored.view_last == 600 &&
          !restored.has_selection && restored.has_loop &&
          restored.loop_first == 180 && restored.loop_last == 620);
    remove("test-bank-independent.tsr");
    CHECK(ts_instrument_select_bank(&restored, 3, error, sizeof(error)) &&
          restored.selected_slot == 3 && !restored.bank[3].occupied &&
          restored.parent.data == NULL && restored.current.data == NULL);
    CHECK(ts_instrument_save_recipe(&restored, "test-bank-independent.tsr",
                                    error, sizeof(error)));
    CHECK(ts_instrument_load_recipe(&empty_restored, "test-bank-independent.tsr",
                                    error, sizeof(error)));
    CHECK(empty_restored.selected_slot == 3 &&
          !empty_restored.bank[3].occupied &&
          empty_restored.parent.data == NULL && empty_restored.current.data == NULL &&
          empty_restored.bank[0].occupied && empty_restored.bank[7].occupied &&
          ts_sample_hash(&empty_restored.bank[0].sample) == slot0_hash &&
          ts_sample_hash(&empty_restored.bank[7].sample) == slot5_hash);
    remove("test-bank-independent.tsr");
    CHECK(ts_instrument_save_recipe(&blank, "test-bank-independent.tsr",
                                    error, sizeof(error)));
    CHECK(ts_instrument_load_recipe(&blank_restored, "test-bank-independent.tsr",
                                    error, sizeof(error)));
    CHECK(blank_restored.selected_slot == 0 &&
          blank_restored.parent.data == NULL && blank_restored.current.data == NULL);
    for (int i = 0; i < TS_BANK_SLOT_COUNT; ++i)
        CHECK(!blank_restored.bank[i].occupied);
    remove("test-bank-independent.tsr");

    ts_instrument_free(&bank);
    ts_instrument_free(&all);
    ts_instrument_free(&destinations);
    ts_instrument_free(&clone_reset);
    ts_instrument_free(&serial);
    ts_instrument_free(&restored);
    ts_instrument_free(&empty_restored);
    ts_instrument_free(&blank);
    ts_instrument_free(&blank_restored);
    if (failures) return 1;
    puts("TapeSister independent Bank command regression tests passed");
    return 0;
}
