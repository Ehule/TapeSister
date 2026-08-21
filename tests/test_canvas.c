#include "tapesister/sample.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CANVAS CHECK FAILED: %s (line %d)\n", \
                #condition, __LINE__); \
        ++failures; \
    } \
} while (0)

static void make_canvas(TsInstrument *instrument, size_t frames, uint32_t rate,
                        int silence)
{
    char error[160];
    ts_instrument_init(instrument);
    CHECK(ts_instrument_activate_silence(instrument, frames, rate,
                                         error, sizeof(error)));
    for (size_t i = 0; i < frames; ++i) {
        float value = silence ? 0.0f : (float)(i + 1u) / (float)(frames + 1u);
        instrument->current.data[i] = value;
        instrument->parent.data[i] = value;
    }
}

static void test_double_and_history(void)
{
    TsInstrument instrument;
    TsInstrument full_view;
    float before[16];
    char error[160];
    make_canvas(&instrument, 16, 48000, 0);
    memcpy(before, instrument.current.data, sizeof(before));
    ts_instrument_set_selection(&instrument, 3, 11);
    instrument.view_first = 2; instrument.view_last = 14;
    instrument.has_playhead = 1; instrument.playhead_frame = 6;
    instrument.has_loop = 1; instrument.loop_first = 4; instrument.loop_last = 12;
    CHECK(instrument.grid_divisions == 16 && !instrument.grid_snap);
    CHECK(ts_instrument_double_canvas(&instrument, error, sizeof(error)));
    CHECK(instrument.current.frames == 32 && instrument.undo_count == 1);
    CHECK(memcmp(instrument.current.data, before, sizeof(before)) == 0);
    for (size_t i = 16; i < 32; ++i) CHECK(instrument.current.data[i] == 0.0f);
    CHECK(instrument.selection_first == 3 && instrument.selection_last == 11);
    CHECK(instrument.view_first == 20 && instrument.view_last == 32);
    CHECK(instrument.playhead_frame == 6);
    CHECK(instrument.loop_first == 4 && instrument.loop_last == 12);
    CHECK(instrument.grid_divisions == 32);
    CHECK(ts_instrument_undo(&instrument, error, sizeof(error)));
    CHECK(instrument.current.frames == 16 &&
          memcmp(instrument.current.data, before, sizeof(before)) == 0);
    CHECK(instrument.selection_first == 3 && instrument.selection_last == 11);
    CHECK(instrument.view_first == 2 && instrument.view_last == 14);
    CHECK(instrument.playhead_frame == 6 && instrument.grid_divisions == 16);
    CHECK(ts_instrument_redo(&instrument, error, sizeof(error)));
    CHECK(instrument.current.frames == 32 && instrument.grid_divisions == 32);
    ts_instrument_free(&instrument);

    make_canvas(&full_view, 16, 48000, 1);
    CHECK(full_view.view_first == 0 && full_view.view_last == 16);
    CHECK(ts_instrument_double_canvas(&full_view, error, sizeof(error)));
    CHECK(full_view.view_first == 0 && full_view.view_last == 32);
    ts_instrument_free(&full_view);
}

static void test_left_and_right_edges(void)
{
    TsInstrument left;
    TsInstrument right;
    float before[12];
    char error[160];
    make_canvas(&left, 12, 44100, 0);
    memcpy(before, left.current.data, sizeof(before));
    ts_instrument_set_selection(&left, 2, 9);
    left.view_first = 1; left.view_last = 11;
    left.has_playhead = 1; left.playhead_frame = 4;
    left.has_loop = 1; left.loop_first = 3; left.loop_last = 10;
    CHECK(ts_instrument_resize_canvas(&left, 1, 5, error, sizeof(error)));
    CHECK(left.current.frames == 17);
    for (size_t i = 0; i < 5; ++i) CHECK(left.current.data[i] == 0.0f);
    CHECK(memcmp(left.current.data + 5, before, sizeof(before)) == 0);
    CHECK(left.selection_first == 7 && left.selection_last == 14);
    CHECK(left.view_first == 0 && left.view_last == 10);
    CHECK(left.playhead_frame == 9);
    CHECK(left.loop_first == 8 && left.loop_last == 15);
    CHECK(ts_instrument_resize_canvas(&left, 1, -5, error, sizeof(error)));
    CHECK(left.current.frames == 12 && memcmp(left.current.data, before, sizeof(before)) == 0);

    make_canvas(&right, 12, 32000, 1);
    ts_instrument_set_selection(&right, 5, 12);
    right.view_first = 2; right.view_last = 12;
    right.has_playhead = 1; right.playhead_frame = 11;
    right.has_loop = 1; right.loop_first = 6; right.loop_last = 12;
    CHECK(ts_instrument_resize_canvas(&right, 2, -4, error, sizeof(error)));
    CHECK(right.current.frames == 8);
    CHECK(right.selection_first == 5 && right.selection_last == 8);
    CHECK(right.view_first == 0 && right.view_last == 8);
    CHECK(right.playhead_frame == 7);
    CHECK(right.loop_first == 6 && right.loop_last == 8);
    CHECK(ts_instrument_resize_canvas(&right, 2, 4, error, sizeof(error)));
    CHECK(right.current.frames == 12);
    for (size_t i = 8; i < 12; ++i) CHECK(right.current.data[i] == 0.0f);
    ts_instrument_free(&right);
    ts_instrument_free(&left);
}

static void test_half_zero_snap_and_minimum(void)
{
    TsInstrument instrument;
    char error[160];
    make_canvas(&instrument, 16, 44100, 0);
    instrument.current.data[6] = 0.0f;
    instrument.parent.data[6] = 0.0f;
    CHECK(ts_instrument_half_canvas(&instrument, error, sizeof(error)));
    CHECK(instrument.current.frames == 7);
    CHECK(instrument.grid_divisions == 16);
    ts_instrument_free(&instrument);

    make_canvas(&instrument, 16, 44100, 1);
    CHECK(ts_instrument_half_canvas(&instrument, error, sizeof(error)));
    CHECK(instrument.current.frames == 8);
    CHECK(instrument.grid_divisions == 8);
    ts_instrument_free(&instrument);

    make_canvas(&instrument, TS_CANVAS_MIN_FRAMES, 1000, 1);
    CHECK(!ts_instrument_half_canvas(&instrument, error, sizeof(error)));
    CHECK(instrument.current.frames == TS_CANVAS_MIN_FRAMES);
    CHECK(!ts_instrument_resize_canvas(&instrument, 2, -1, error, sizeof(error)));
    CHECK(instrument.current.frames == TS_CANVAS_MIN_FRAMES);
    CHECK(!ts_instrument_resize_canvas(&instrument, 2, INT64_MAX,
                                       error, sizeof(error)));
    CHECK(instrument.current.frames == TS_CANVAS_MIN_FRAMES);
    ts_instrument_free(&instrument);
}

static void test_grid_and_hierarchical_snap(void)
{
    TsInstrument instrument;
    TsInstrument tape;
    size_t target;
    char error[160];
    make_canvas(&instrument, 64, 44100, 0);
    instrument.current.data[14] = instrument.parent.data[14] = 0.0f;
    instrument.current.data[20] = instrument.parent.data[20] = 0.0f;
    CHECK(ts_instrument_set_grid_divisions(&instrument, 8));
    target = ts_instrument_grid_target(&instrument, 18);
    CHECK(target == 16);
    CHECK(ts_instrument_grid_target(&instrument, 20) == 24);
    CHECK(ts_instrument_grid_target(&instrument, instrument.current.frames) ==
          instrument.current.frames);
    instrument.view_first = 12; instrument.view_last = 28;
    CHECK(ts_instrument_grid_target(&instrument, 18) == target);
    CHECK(ts_instrument_toggle_grid_snap(&instrument));
    CHECK(ts_instrument_resolve_boundary(&instrument, 18) == 15);
    ts_instrument_set_selection_snapped(&instrument, 18, 64);
    CHECK(instrument.selection_first == 15 && instrument.selection_last == 64);
    CHECK(ts_instrument_toggle_grid_snap(&instrument));
    CHECK(instrument.grid_snap == TS_GRID_SNAP_MOVE_ONLY);
    CHECK(ts_instrument_grid_moves_snap(&instrument));
    CHECK(ts_instrument_resolve_boundary(&instrument, 18) == 20);
    CHECK(ts_instrument_cycle_grid_divisions(&instrument, -1) &&
          instrument.grid_divisions == 4);
    CHECK(ts_instrument_set_grid_divisions(&instrument, TS_GRID_DIVISION_MIN));
    CHECK(!ts_instrument_cycle_grid_divisions(&instrument, -1));
    CHECK(ts_instrument_set_grid_divisions(&instrument, TS_GRID_DIVISION_MAX));
    CHECK(!ts_instrument_cycle_grid_divisions(&instrument, 1));
    CHECK(!ts_instrument_set_grid_divisions(&instrument, 3));
    CHECK(ts_instrument_toggle_grid_snap(&instrument));
    CHECK(instrument.grid_snap == TS_GRID_SNAP_OFF);
    (void)error;
    ts_instrument_free(&instrument);

    make_canvas(&instrument, 17, 44100, 0);
    instrument.current.data[3] = instrument.parent.data[3] = 0.01f;
    instrument.current.data[9] = instrument.parent.data[9] = 0.02f;
    CHECK(ts_instrument_resolve_boundary(&instrument, 12) == 3);
    CHECK(ts_sample_nearest_zero_crossing_in_range(
              &instrument.current, 10, 3, 12) == 3);
    ts_instrument_free(&instrument);

    make_canvas(&tape, 64, 44100, 0);
    for (size_t i = 0; i < tape.current.frames; ++i)
        tape.current.data[i] = tape.parent.data[i] = i & 1u ? -0.5f : 0.5f;
    CHECK(ts_instrument_set_grid_divisions(&tape, 8));
    CHECK(ts_instrument_toggle_grid_snap(&tape));
    ts_instrument_set_selection(&tape, 4, 8);
    CHECK(ts_instrument_apply_tape_drag(
              &tape, TS_POST_COPY_OVERWRITE, 4, 8, 18,
              error, sizeof(error)));
    CHECK(tape.selection_first == 16 && tape.selection_last == 20);
    tape.has_loop = 1;
    tape.loop_first = 8;
    tape.loop_last = 40;
    CHECK(ts_instrument_move_loop_endpoint(&tape, 1, 18) == 1);
    CHECK(tape.loop_first == 16 && tape.loop_last == 40);
    CHECK(ts_instrument_toggle_grid_snap(&tape));
    CHECK(tape.grid_snap == TS_GRID_SNAP_MOVE_ONLY);
    ts_instrument_set_selection_snapped(&tape, 17, 27);
    CHECK(tape.selection_first == 17 && tape.selection_last == 27);
    CHECK(ts_instrument_apply_tape_drag(
              &tape, TS_POST_COPY_OVERWRITE, 17, 27, 34,
              error, sizeof(error)));
    CHECK(tape.selection_first == 32 && tape.selection_last == 42);
    tape.loop_first = 8;
    tape.loop_last = 40;
    CHECK(ts_instrument_move_loop_endpoint(&tape, 1, 18) == 1);
    CHECK(tape.loop_first == 18 && tape.loop_last == 40);
    ts_instrument_free(&tape);
}

static void test_rolling_history_and_checkpoint(void)
{
    TsInstrument instrument;
    TsInstrument restored;
    TsSample clipboard;
    uint64_t newest;
    size_t origin = 0;
    char error[160];
    int undone = 0;
    ts_sample_init(&clipboard);
    make_canvas(&instrument, 128, 44100, 0);
    for (int edit = 0; edit < TS_SAMPLE_EDIT_DEPTH + 8; ++edit)
        CHECK(ts_instrument_apply_sample_edit(
                  &instrument, TS_SAMPLE_EDIT_GAIN, 0.995f,
                  error, sizeof(error)));
    newest = ts_sample_hash(&instrument.current);
    CHECK(instrument.undo_count == TS_HISTORY_DEPTH);
    CHECK(instrument.undo_count <= 20 && instrument.redo_count == 0);
    CHECK(instrument.sample_edit_count < TS_SAMPLE_EDIT_DEPTH);
    CHECK(instrument.bank[instrument.selected_slot].patch_count <=
          TS_AUDIO_PATCH_DEPTH);
    while (ts_instrument_undo(&instrument, error, sizeof(error))) ++undone;
    CHECK(undone == TS_HISTORY_DEPTH);
    while (ts_instrument_redo(&instrument, error, sizeof(error))) { }
    CHECK(ts_sample_hash(&instrument.current) == newest);
    ts_instrument_set_selection(&instrument, 0, 8);
    CHECK(ts_instrument_copy_selection(&instrument, &clipboard, &origin,
                                       error, sizeof(error)));
    ts_instrument_set_selection(&instrument, 32, 40);
    for (int edit = 0; edit < TS_AUDIO_PATCH_DEPTH + 8; ++edit)
        CHECK(ts_instrument_paste(&instrument, &clipboard, origin, 1,
                                  error, sizeof(error)));
    newest = ts_sample_hash(&instrument.current);
    CHECK(instrument.undo_count == TS_HISTORY_DEPTH);
    CHECK(instrument.bank[instrument.selected_slot].patch_count <
          TS_AUDIO_PATCH_DEPTH);
    CHECK(ts_instrument_save_recipe(&instrument, "test-history.tsr",
                                    error, sizeof(error)));
    ts_instrument_init(&restored);
    CHECK(ts_instrument_load_recipe(&restored, "test-history.tsr",
                                    error, sizeof(error)));
    CHECK(restored.undo_count == TS_HISTORY_DEPTH &&
          ts_sample_hash(&restored.current) == newest);
    CHECK(ts_instrument_undo(&restored, error, sizeof(error)));
    CHECK(ts_instrument_redo(&restored, error, sizeof(error)) &&
          ts_sample_hash(&restored.current) == newest);
    remove("test-history.tsr");
    ts_instrument_free(&restored);
    ts_sample_free(&clipboard);
    ts_instrument_free(&instrument);
}

static void test_frozen_gesture_commit_cancel(void)
{
    TsInstrument instrument;
    TsCanvasGesture gesture;
    uint64_t before_hash;
    char error[160];
    make_canvas(&instrument, 20, 22050, 1);
    before_hash = ts_sample_hash(&instrument.current);
    instrument.view_first = 3u;
    instrument.view_last = 13u;
    ts_canvas_gesture_init(&gesture);
    CHECK(ts_instrument_canvas_gesture_begin(&instrument, &gesture, 2,
                                             error, sizeof(error)));
    CHECK(instrument.view_first == 10u && instrument.view_last == 20u);
    CHECK(ts_instrument_canvas_gesture_preview(&instrument, &gesture, 4,
                                               error, sizeof(error)));
    CHECK(instrument.current.frames == 24 && instrument.undo_count == 0);
    CHECK(instrument.view_first == 14u && instrument.view_last == 24u);
    CHECK(ts_instrument_canvas_gesture_preview(&instrument, &gesture, 8,
                                               error, sizeof(error)));
    CHECK(instrument.current.frames == 28 && instrument.undo_count == 0);
    CHECK(instrument.view_first == 18u && instrument.view_last == 28u);
    CHECK(ts_instrument_canvas_gesture_cancel(&instrument, &gesture,
                                              error, sizeof(error)));
    CHECK(instrument.current.frames == 20 &&
          ts_sample_hash(&instrument.current) == before_hash &&
          instrument.undo_count == 0);
    CHECK(instrument.view_first == 3u && instrument.view_last == 13u);
    CHECK(ts_instrument_canvas_gesture_begin(&instrument, &gesture, 1,
                                             error, sizeof(error)));
    CHECK(instrument.view_first == 0u && instrument.view_last == 10u);
    CHECK(ts_instrument_canvas_gesture_preview(&instrument, &gesture, 6,
                                               error, sizeof(error)));
    CHECK(ts_instrument_canvas_gesture_commit(&instrument, &gesture,
                                              error, sizeof(error)));
    CHECK(instrument.current.frames == 26 && instrument.undo_count == 1);
    CHECK(instrument.view_first == 0u && instrument.view_last == 10u);
    CHECK(ts_instrument_undo(&instrument, error, sizeof(error)) &&
          instrument.current.frames == 20);
    CHECK(instrument.view_first == 3u && instrument.view_last == 13u);
    ts_instrument_free(&instrument);
}

static void test_unsnapped_canvas_contraction_with_selection(void)
{
    TsInstrument instrument;
    TsCanvasGesture gesture;
    char error[160];
    make_canvas(&instrument, 64u, 44100u, 0);
    ts_canvas_gesture_init(&gesture);
    ts_instrument_set_selection(&instrument, 9u, 51u);
    CHECK(instrument.grid_snap == TS_GRID_SNAP_OFF);
    CHECK(ts_instrument_canvas_gesture_begin(
        &instrument, &gesture, 2, error, sizeof(error)));
    CHECK(ts_instrument_canvas_gesture_preview(
        &instrument, &gesture, -3, error, sizeof(error)));
    CHECK(instrument.current.frames == 61u &&
          instrument.selection_first == 9u &&
          instrument.selection_last == 51u);
    CHECK(ts_instrument_canvas_gesture_cancel(
        &instrument, &gesture, error, sizeof(error)));
    CHECK(instrument.current.frames == 64u &&
          instrument.selection_first == 9u &&
          instrument.selection_last == 51u);
    ts_instrument_free(&instrument);
}

static void test_tile_independence_and_roundtrip(void)
{
    TsInstrument instrument;
    TsInstrument restored;
    uint64_t source_hash;
    uint64_t resized_hash;
    char error[160];
    FILE *file;
    char magic[6] = {0};
    make_canvas(&instrument, 32, 48000, 0);
    CHECK(ts_instrument_resize_canvas(&instrument, 2, 8, error, sizeof(error)));
    source_hash = ts_sample_hash(&instrument.current);
    CHECK(ts_instrument_copy_selected(&instrument, 1, error, sizeof(error)));
    CHECK(instrument.selected_slot == 1);
    CHECK(ts_instrument_set_grid_divisions(&instrument, 32));
    CHECK(ts_instrument_toggle_grid_snap(&instrument));
    CHECK(ts_instrument_toggle_grid_snap(&instrument));
    CHECK(ts_instrument_select_bank(&instrument, 0, error, sizeof(error)) &&
          instrument.grid_divisions == 16 && !instrument.grid_snap);
    CHECK(ts_instrument_select_bank(&instrument, 1, error, sizeof(error)) &&
          instrument.grid_divisions == 32 &&
          instrument.grid_snap == TS_GRID_SNAP_MOVE_ONLY);
    CHECK(ts_instrument_resize_canvas(&instrument, 1, 5, error, sizeof(error)));
    resized_hash = ts_sample_hash(&instrument.current);
    CHECK(ts_sample_hash(&instrument.bank[0].sample) == source_hash);
    CHECK(ts_instrument_save_recipe(&instrument, "test-canvas.tsr",
                                    error, sizeof(error)));
    file = fopen("test-canvas.tsr", "rb");
    CHECK(file != NULL);
    if (file != NULL) {
        CHECK(fread(magic, 1, 5, file) == 5 && memcmp(magic, "TSR26", 5) == 0);
        fclose(file);
    }
    ts_instrument_init(&restored);
    CHECK(ts_instrument_load_recipe(&restored, "test-canvas.tsr",
                                    error, sizeof(error)));
    CHECK(restored.selected_slot == 1 &&
          ts_sample_hash(&restored.current) == resized_hash &&
          restored.grid_divisions == 32 &&
          restored.grid_snap == TS_GRID_SNAP_MOVE_ONLY);
    CHECK(ts_instrument_select_bank(&restored, 0, error, sizeof(error)) &&
          ts_sample_hash(&restored.current) == source_hash &&
          restored.grid_divisions == 16 && !restored.grid_snap);
    remove("test-canvas.tsr");
    ts_instrument_free(&restored);
    ts_instrument_free(&instrument);
}

int main(void)
{
    test_double_and_history();
    test_left_and_right_edges();
    test_half_zero_snap_and_minimum();
    test_grid_and_hierarchical_snap();
    test_frozen_gesture_commit_cancel();
    test_unsnapped_canvas_contraction_with_selection();
    test_rolling_history_and_checkpoint();
    test_tile_independence_and_roundtrip();
    if (failures != 0) {
        fprintf(stderr, "%d canvas/grid checks failed\n", failures);
        return 1;
    }
    puts("TapeSister audio canvas, grid snapping, history, and tile tests passed");
    return 0;
}
