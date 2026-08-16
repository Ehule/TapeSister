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
    TsInstrument bank, all, serial, restored;
    TsTearGesture tear;
    TsNoteBank notes;
    TsAuditionPlan plan;
    char error[160];
    int failures = 0;
    uint64_t current_hash;
    uint64_t slot0_hash;
    uint64_t slot5_hash;

    CHECK(ts_ui_bank_action(0, 0) == TS_UI_BANK_ACTION_AUDITION);
    CHECK(ts_ui_bank_action(0, TS_UI_BANK_MOD_SHIFT) == TS_UI_BANK_ACTION_CAPTURE_CURRENT);
    CHECK(ts_ui_bank_action(0, TS_UI_BANK_MOD_ALT) == TS_UI_BANK_ACTION_CAPTURE_LOOP);
    CHECK(ts_ui_bank_action(0, TS_UI_BANK_MOD_CTRL) == TS_UI_BANK_ACTION_CAPTURE_SELECTION);
    CHECK(ts_ui_bank_action(1, 0) == TS_UI_BANK_ACTION_RENAME);
    CHECK(ts_ui_bank_action(1, TS_UI_BANK_MOD_SHIFT) == TS_UI_BANK_ACTION_CLEAR);
    CHECK(ts_ui_bank_action(0, TS_UI_BANK_MOD_SHIFT | TS_UI_BANK_MOD_CTRL) ==
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
    CHECK(!ts_ui_execute_bank_action(&bank, 2, TS_UI_BANK_ACTION_AUDITION,
                                     error, sizeof(error)));
    CHECK(bank.selected_slot == 0 && ts_sample_hash(&bank.current) == slot0_hash);

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
    CHECK(ts_instrument_bank_clear_all(&all, error, sizeof(error)));
    CHECK(ts_instrument_bank_count(&all) == 0);
    for (int slot = 0; slot < TS_BANK_SLOT_COUNT; ++slot)
        CHECK(!all.bank[slot].occupied);
    CHECK(ts_instrument_create_selected(&all, 0x434c4541u, error, sizeof(error)));
    CHECK(all.bank[0].occupied && all.current.data != NULL);

    ts_instrument_init(&serial); ts_instrument_init(&restored);
    CHECK(ts_instrument_generate(&serial, TS_GENERATOR_PULSE, 0x42414e4bu,
                                 error, sizeof(error)));
    CHECK(ts_instrument_bank_rename(&serial, 0, "INDEPENDENT ONE",
                                    error, sizeof(error)));
    CHECK(ts_ui_execute_bank_action(&serial, 7, TS_UI_BANK_ACTION_CAPTURE_CURRENT,
                                    error, sizeof(error)));
    CHECK(ts_instrument_bank_rename(&serial, 7, "INDEPENDENT EIGHT",
                                    error, sizeof(error)));
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
          strcmp(restored.bank[0].sample.name, "INDEPENDENT ONE") == 0 &&
          strcmp(restored.bank[7].sample.name, "INDEPENDENT EIGHT") == 0 &&
          ts_sample_hash(&restored.bank[0].sample) == slot0_hash &&
          ts_sample_hash(&restored.bank[7].sample) == slot5_hash);
    remove("test-bank-independent.tsr");

    ts_instrument_free(&bank);
    ts_instrument_free(&all);
    ts_instrument_free(&serial);
    ts_instrument_free(&restored);
    if (failures) return 1;
    puts("TapeSister independent Bank command regression tests passed");
    return 0;
}
