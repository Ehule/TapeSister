#include "tapesister/config.h"
#include "tapesister/sample.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(expr) do { if (!(expr)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
    ++failures; \
} } while (0)

static int occupied_count(const TsInstrument *instrument)
{
    int count = 0;
    for (int slot = 0; slot < TS_BANK_SLOT_COUNT; ++slot)
        if (instrument->bank[slot].occupied) ++count;
    return count;
}

static void config_tests(void)
{
    char error[160];
    TsConfig config;
    TsConfig loaded;
    FILE *file;
    ts_config_init(&config);
    CHECK(config.chain_stamp_crossfade_ms ==
          TS_CHAIN_STAMP_CROSSFADE_MS_DEFAULT);
    config.chain_stamp_crossfade_ms = 7;
    CHECK(ts_config_save(&config, "test-chain-stamp.ini",
                         error, sizeof(error)));
    ts_config_init(&loaded);
    CHECK(ts_config_load(&loaded, "test-chain-stamp.ini",
                         error, sizeof(error)));
    CHECK(loaded.chain_stamp_crossfade_ms == 7);
    remove("test-chain-stamp.ini");

    file = fopen("test-chain-stamp.ini", "wb");
    CHECK(file != NULL);
    if (file != NULL) {
        fputs("chain_stamp_crossfade_ms=-99\n", file);
        fclose(file);
    }
    CHECK(ts_config_load(&loaded, "test-chain-stamp.ini",
                         error, sizeof(error)));
    CHECK(loaded.chain_stamp_crossfade_ms ==
          TS_CHAIN_STAMP_CROSSFADE_MS_MIN);
    file = fopen("test-chain-stamp.ini", "wb");
    CHECK(file != NULL);
    if (file != NULL) {
        fputs("chain_stamp_crossfade_ms=999\n", file);
        fclose(file);
    }
    CHECK(ts_config_load(&loaded, "test-chain-stamp.ini",
                         error, sizeof(error)));
    CHECK(loaded.chain_stamp_crossfade_ms ==
          TS_CHAIN_STAMP_CROSSFADE_MS_MAX);
    remove("test-chain-stamp.ini");
}

static void four_mode_matrix_tests(void)
{
    char error[160];
    TsInstrument instrument;
    size_t first;
    size_t last;
    int destination = -1;
    ts_instrument_init(&instrument);
    CHECK(ts_instrument_bank_clear_all(&instrument, error, sizeof(error)));
    CHECK(ts_instrument_select_bank(&instrument, 0, error, sizeof(error)));
    CHECK(ts_instrument_create_selected(&instrument, 0x330001u,
                                        error, sizeof(error)));
    instrument.family_mutation = 0.58f;

    CHECK(!instrument.has_selection);
    CHECK(ts_instrument_vary_selected(&instrument, 0, &destination,
                                      error, sizeof(error)));
    CHECK(destination == 0 && instrument.selected_slot == 0 &&
          occupied_count(&instrument) == 1);

    CHECK(ts_instrument_vary_selected(&instrument, 1, &destination,
                                      error, sizeof(error)));
    CHECK(destination == 1 && instrument.selected_slot == 1 &&
          occupied_count(&instrument) == 2);

    first = instrument.current.frames / 4u;
    last = first + 512u;
    ts_instrument_set_selection(&instrument, first, last);
    CHECK(ts_instrument_stamp_vary(&instrument, error, sizeof(error)));
    CHECK(instrument.selected_slot == 1 && occupied_count(&instrument) == 2 &&
          instrument.selection_first == first &&
          instrument.selection_last == last);

    CHECK(ts_instrument_stamp_vary_chained(&instrument, 3,
                                           error, sizeof(error)));
    CHECK(instrument.selected_slot == 1 && occupied_count(&instrument) == 2 &&
          instrument.selection_first > first &&
          instrument.selection_last - instrument.selection_first == 512u);
    ts_instrument_free(&instrument);
}

static void scroll_transaction_tests(void)
{
    char error[160];
    TsInstrument scroll;
    TsInstrument loaded;
    TsSample before;
    TsSample after_first;
    TsSample after_second;
    TsGeneratorRecipe first_recipe;
    TsGeneratorRecipe second_recipe;
    TsFmPatch first_patch;
    TsFmPatch expected_second_patch;
    size_t original_frames;
    size_t stamp_frames = 1024u;
    size_t fade_frames;
    size_t first_destination;
    size_t first_next;
    size_t second_next;
    size_t view_span;
    uint64_t other_hash;
    uint32_t expected_second_seed;
    int undo_before;
    int occupied_before;
    ts_instrument_init(&scroll);
    ts_instrument_init(&loaded);
    ts_sample_init(&before);
    ts_sample_init(&after_first);
    ts_sample_init(&after_second);
    CHECK(ts_instrument_bank_clear_all(&scroll, error, sizeof(error)));
    CHECK(ts_instrument_select_bank(&scroll, 0, error, sizeof(error)));
    CHECK(ts_instrument_create_selected(&scroll, 0x5c7011u,
                                        error, sizeof(error)));
    CHECK(scroll.current.frames > stamp_frames * 2u);
    scroll.family_mutation = 0.72f;
    CHECK(ts_instrument_copy_selected(&scroll, 1, error, sizeof(error)));
    other_hash = ts_sample_hash(&scroll.current);
    CHECK(ts_instrument_select_bank(&scroll, 0, error, sizeof(error)));
    original_frames = scroll.current.frames;
    first_destination = original_frames - stamp_frames;
    ts_instrument_set_selection(&scroll, first_destination, original_frames);
    scroll.view_first = original_frames - stamp_frames * 2u;
    scroll.view_last = original_frames;
    view_span = scroll.view_last - scroll.view_first;
    fade_frames = ((size_t)scroll.current.sample_rate * 3u + 500u) / 1000u;
    if (fade_frames > stamp_frames / 4u) fade_frames = stamp_frames / 4u;
    if (fade_frames > 65536u) fade_frames = 65536u;
    first_next = original_frames - fade_frames;
    CHECK(ts_sample_clone(&before, &scroll.current, error, sizeof(error)));
    undo_before = scroll.undo_count;
    occupied_before = occupied_count(&scroll);

    CHECK(ts_instrument_stamp_vary_chained(&scroll, 3,
                                           error, sizeof(error)));
    CHECK(scroll.selected_slot == 0 && scroll.undo_count == undo_before + 1);
    CHECK(scroll.current.frames == original_frames + stamp_frames - fade_frames);
    CHECK(scroll.has_selection && scroll.selection_first == first_next &&
          scroll.selection_last == first_next + stamp_frames &&
          scroll.selection_last == scroll.current.frames);
    CHECK(scroll.view_last - scroll.view_first == view_span &&
          scroll.view_last == scroll.selection_last);
    CHECK(scroll.post_edit_count == 2 &&
          scroll.post_edits[0].kind == TS_POST_PATCH_FIT &&
          scroll.post_edits[0].crossfade_frames == fade_frames &&
          scroll.post_edits[1].kind == TS_POST_CANVAS_RIGHT_RESIZE &&
          scroll.post_edits[1].destination ==
              (int64_t)(stamp_frames - fade_frames));
    CHECK(memcmp(scroll.current.data, before.data,
                 first_destination * sizeof(*before.data)) == 0);
    CHECK(memcmp(scroll.current.data + first_destination,
                 before.data + first_destination,
                 stamp_frames * sizeof(*before.data)) != 0);
    if (fade_frames > 0u) {
        float allowed = 2.0f / (float)(fade_frames + 1u) + 0.00001f;
        CHECK(fabsf(scroll.current.data[first_destination] -
                    before.data[first_destination]) <= allowed);
        CHECK(fabsf(scroll.current.data[original_frames - 1u] -
                    before.data[original_frames - 1u]) <= allowed);
    }
    for (size_t frame = 0u; frame < scroll.current.frames; ++frame)
        CHECK(isfinite(scroll.current.data[frame]));
    first_recipe =
        scroll.bank[0].patches[scroll.bank[0].patch_count - 1].generator;
    CHECK(ts_sample_hash(&scroll.bank[1].sample) == other_hash &&
          occupied_count(&scroll) == occupied_before);
    CHECK(ts_sample_clone(&after_first, &scroll.current,
                          error, sizeof(error)));

    expected_second_seed = (first_recipe.seed ^
        ((scroll.family_sequence + 1u) * 1664525u + 1013904223u)) *
        1664525u + 1013904223u;
    ts_fm_patch_from_recipe(&first_recipe, &first_patch);
    ts_fm_patch_vary(&first_patch, expected_second_seed,
                     scroll.family_mutation, &expected_second_patch);
    CHECK(ts_instrument_stamp_vary_chained(&scroll, 3,
                                           error, sizeof(error)));
    second_next = first_next + stamp_frames - fade_frames;
    CHECK(scroll.current.frames == original_frames +
          (stamp_frames - fade_frames) * 2u);
    CHECK(scroll.selection_first == second_next &&
          scroll.selection_last == second_next + stamp_frames &&
          scroll.selection_last == scroll.current.frames);
    CHECK(scroll.post_edit_count == 4 &&
          scroll.post_edits[2].kind == TS_POST_PATCH_FIT &&
          scroll.post_edits[2].first == first_next &&
          scroll.post_edits[2].last == first_next + stamp_frames &&
          scroll.post_edits[2].crossfade_frames == fade_frames &&
          scroll.post_edits[3].kind == TS_POST_CANVAS_RIGHT_RESIZE);
    second_recipe =
        scroll.bank[0].patches[scroll.bank[0].patch_count - 1].generator;
    CHECK(second_recipe.seed == expected_second_seed &&
          second_recipe.has_fm_patch &&
          memcmp(&second_recipe.fm_patch, &expected_second_patch,
                 sizeof(expected_second_patch)) == 0);
    CHECK(ts_sample_hash(&scroll.current) != ts_sample_hash(&after_first) &&
          ts_sample_hash(&scroll.bank[1].sample) == other_hash);
    if (fade_frames > 0u) {
        float allowed = 2.0f / (float)(fade_frames + 1u) + 0.00001f;
        float old_jump = fabsf(after_first.data[first_next] -
                                after_first.data[first_next - 1u]);
        float new_jump = fabsf(scroll.current.data[first_next] -
                                scroll.current.data[first_next - 1u]);
        CHECK(new_jump <= old_jump + allowed);
        CHECK(fabsf(scroll.current.data[first_next + stamp_frames - 1u] -
                    scroll.current.data[first_next + stamp_frames]) <= allowed);
    }
    for (size_t frame = 0u; frame < scroll.current.frames; ++frame)
        CHECK(isfinite(scroll.current.data[frame]));
    CHECK(ts_sample_clone(&after_second, &scroll.current,
                          error, sizeof(error)));

    CHECK(ts_instrument_undo(&scroll, error, sizeof(error)) &&
          ts_sample_hash(&scroll.current) == ts_sample_hash(&after_first) &&
          scroll.selection_first == first_next);
    CHECK(ts_instrument_undo(&scroll, error, sizeof(error)) &&
          scroll.current.frames == original_frames &&
          ts_sample_hash(&scroll.current) == ts_sample_hash(&before) &&
          scroll.selection_first == first_destination &&
          scroll.selection_last == original_frames);
    CHECK(ts_instrument_redo(&scroll, error, sizeof(error)) &&
          ts_sample_hash(&scroll.current) == ts_sample_hash(&after_first));
    CHECK(ts_instrument_redo(&scroll, error, sizeof(error)) &&
          ts_sample_hash(&scroll.current) == ts_sample_hash(&after_second) &&
          scroll.selection_first == second_next);

    CHECK(ts_instrument_save_recipe(&scroll, "test-chain-stamps.tsr",
                                    error, sizeof(error)));
    CHECK(ts_instrument_load_recipe(&loaded, "test-chain-stamps.tsr",
                                    error, sizeof(error)));
    CHECK(loaded.current.frames == scroll.current.frames &&
          ts_sample_hash(&loaded.current) == ts_sample_hash(&scroll.current) &&
          loaded.selection_first == scroll.selection_first &&
          loaded.selection_last == scroll.selection_last &&
          loaded.post_edits[2].crossfade_frames == fade_frames);
    CHECK(ts_instrument_undo(&loaded, error, sizeof(error)) &&
          ts_sample_hash(&loaded.current) == ts_sample_hash(&after_first));
    CHECK(ts_instrument_redo(&loaded, error, sizeof(error)) &&
          ts_sample_hash(&loaded.current) == ts_sample_hash(&after_second));
    remove("test-chain-stamps.tsr");
    ts_sample_free(&after_second);
    ts_sample_free(&after_first);
    ts_sample_free(&before);
    ts_instrument_free(&loaded);
    ts_instrument_free(&scroll);
}

static void zero_overlap_and_stress_tests(void)
{
    char error[160];
    TsInstrument instrument;
    size_t original_frames;
    size_t stamp_frames = 512u;
    ts_instrument_init(&instrument);
    CHECK(ts_instrument_bank_clear_all(&instrument, error, sizeof(error)));
    CHECK(ts_instrument_select_bank(&instrument, 0, error, sizeof(error)));
    CHECK(ts_instrument_create_selected(&instrument, 0x0c0ffeeu,
                                        error, sizeof(error)));
    instrument.family_mutation = 0.5f;
    original_frames = instrument.current.frames;
    ts_instrument_set_selection(&instrument,
                                original_frames - stamp_frames,
                                original_frames);
    CHECK(ts_instrument_stamp_vary_chained(&instrument, 0,
                                           error, sizeof(error)));
    CHECK(instrument.current.frames == original_frames + stamp_frames &&
          instrument.selection_first == original_frames &&
          instrument.selection_last == original_frames + stamp_frames &&
          instrument.post_edits[0].crossfade_frames == 0u);
    ts_instrument_free(&instrument);

    ts_instrument_init(&instrument);
    CHECK(ts_instrument_bank_clear_all(&instrument, error, sizeof(error)));
    CHECK(ts_instrument_select_bank(&instrument, 0, error, sizeof(error)));
    CHECK(ts_instrument_activate_silence(&instrument, 4096u, 48000u,
                                        error, sizeof(error)));
    stamp_frames = 256u;
    ts_instrument_set_selection(&instrument, 0u, stamp_frames);
    CHECK(ts_instrument_stamp_create(&instrument, 0x510011u,
                                     error, sizeof(error)));
    instrument.family_mutation = 0.64f;
    for (int stamp = 0; stamp < 50; ++stamp) {
        uint32_t previous_seed = instrument.bank[0]
            .patches[instrument.bank[0].patch_count - 1].generator.seed;
        const TsAudioPatch *latest;
        CHECK(ts_instrument_stamp_vary_chained(&instrument, 3,
                                               error, sizeof(error)));
        CHECK(instrument.selection_last - instrument.selection_first ==
              stamp_frames);
        CHECK(instrument.post_edit_count <= TS_POST_EDIT_DEPTH &&
              instrument.bank[0].patch_count <= TS_AUDIO_PATCH_DEPTH);
        latest = &instrument.bank[0]
                     .patches[instrument.bank[0].patch_count - 1];
        CHECK(latest->has_generator && latest->generator.seed != previous_seed);
    }
    CHECK(instrument.current.frames > 4096u &&
          instrument.post_edit_count < TS_POST_EDIT_DEPTH);
    for (size_t frame = 0u; frame < instrument.current.frames; ++frame)
        CHECK(isfinite(instrument.current.data[frame]));
    {
        uint64_t final_hash = ts_sample_hash(&instrument.current);
        CHECK(ts_instrument_undo(&instrument, error, sizeof(error)) &&
              ts_sample_hash(&instrument.current) != final_hash);
        CHECK(ts_instrument_redo(&instrument, error, sizeof(error)) &&
              ts_sample_hash(&instrument.current) == final_hash);
    }
    ts_instrument_free(&instrument);
}

int main(void)
{
    config_tests();
    four_mode_matrix_tests();
    scroll_transaction_tests();
    zero_overlap_and_stress_tests();
    if (failures != 0) {
        fprintf(stderr, "%d chain stamp test(s) failed\n", failures);
        return 1;
    }
    puts("TapeSister sequential selection chain stamp tests passed");
    return 0;
}
