#include "tapesister/audition.h"
#include "tapesister/config.h"
#include "tapesister/sample.h"
#include "tapesister/ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "Drone test failed at line %d: %s\n", \
                __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

static void make_sample(TsSample *sample, const float *data, size_t frames,
                        uint32_t sample_rate, const char *name)
{
    ts_sample_init(sample);
    sample->data = malloc(frames * sizeof(*sample->data));
    CHECK(sample->data != NULL);
    if (sample->data != NULL)
        memcpy(sample->data, data, frames * sizeof(*sample->data));
    sample->frames = frames;
    sample->sample_rate = sample_rate;
    snprintf(sample->name, sizeof(sample->name), "%s", name);
}

static int samples_equal(const TsSample *left, const TsSample *right)
{
    return left->frames == right->frames &&
           left->sample_rate == right->sample_rate &&
           left->data != NULL && right->data != NULL &&
           memcmp(left->data, right->data,
                  left->frames * sizeof(*left->data)) == 0;
}

static void test_processing_core(void)
{
    static const float data[16] = {
        0.20f, 0.40f, 0.60f, 0.80f, 0.60f, 0.40f, 0.20f, 0.10f,
        0.00f, -0.10f, -0.20f, -0.40f, -0.60f, -0.80f, -0.60f, -0.40f
    };
    TsSample source;
    TsSample drone;
    uint64_t source_hash;
    size_t split = 0;
    size_t overlap = 0;
    char error[160];
    make_sample(&source, data, 16, 1000, "DETERMINISTIC");
    ts_sample_init(&drone);
    source_hash = ts_sample_hash(&source);

    CHECK(ts_sample_make_drone(&drone, &source, 0, source.frames, 4,
                               &split, &overlap, error, sizeof(error)));
    CHECK(split == 8);
    CHECK(overlap == 4);
    CHECK(drone.frames == 12);
    CHECK(drone.sample_rate == 1000);
    CHECK(ts_sample_hash(&source) == source_hash);
    CHECK(drone.data[0] == source.data[split]);
    CHECK(drone.data[drone.frames - 1u] == source.data[split - 1u]);
    CHECK(fabsf((drone.data[drone.frames - 1u] - drone.data[0]) -
                (source.data[split - 1u] - source.data[split])) < 0.000001f);
    for (size_t i = 0; i < drone.frames; ++i)
        CHECK(fabsf(drone.data[i]) <= 0.800001f);
    for (size_t i = 0; i < 4; ++i)
        CHECK(drone.data[i] == source.data[split + i]);
    for (size_t i = 0; i < 4; ++i)
        CHECK(drone.data[8u + i] == source.data[4u + i]);

    CHECK(ts_sample_make_drone_at_split(&drone, &source, 0, source.frames,
                                        8, 2, &overlap,
                                        error, sizeof(error)));
    CHECK(overlap == 2);
    CHECK(drone.frames == 14);
    CHECK(drone.data[0] == source.data[8]);
    CHECK(drone.data[drone.frames - 1u] == source.data[7]);
    CHECK(ts_sample_make_drone_at_split(&drone, &source, 0, source.frames,
                                        8, 1000, &overlap,
                                        error, sizeof(error)));
    CHECK(overlap == 4);
    CHECK(drone.frames == 12);

    CHECK(ts_sample_make_drone(&drone, &source, 1, 14, 1000,
                               &split, &overlap, error, sizeof(error)));
    CHECK(overlap == 3);
    CHECK(drone.frames == 10);
    CHECK(overlap <= (14u - 1u) / 4u);
    CHECK(ts_sample_make_drone(&drone, &source, 0, 12, 4,
                               &split, &overlap, error, sizeof(error)));
    CHECK(drone.frames == 9);
    CHECK(ts_sample_make_drone(&drone, &source, 4, 16, 4,
                               &split, &overlap, error, sizeof(error)));
    CHECK(drone.frames == 9);

    CHECK(!ts_sample_make_drone(&drone, &source, 4, 5, 50,
                                &split, &overlap, error, sizeof(error)));
    CHECK(!ts_sample_make_drone(&drone, &source, 4, 4, 50,
                                &split, &overlap, error, sizeof(error)));
    CHECK(!ts_sample_make_drone(&drone, &source, 4, 7, 50,
                                &split, &overlap, error, sizeof(error)));
    CHECK(!ts_sample_make_drone(&drone, &source, 0, 8, -1,
                                &split, &overlap, error, sizeof(error)));

    ts_sample_free(&drone);
    ts_sample_free(&source);
}

static void test_rates_and_fallback(void)
{
    float flat[1000];
    TsSample source;
    TsSample drone;
    size_t split;
    size_t overlap;
    char error[160];
    for (size_t i = 0; i < 1000; ++i) flat[i] = 0.25f + (float)i / 10000.0f;
    flat[487] = 0.0001f;
    make_sample(&source, flat, 1000, 48000, "NO CROSSING");
    ts_sample_init(&drone);
    CHECK(ts_sample_make_drone(&drone, &source, 0, 1000, 1,
                               &split, &overlap, error, sizeof(error)));
    CHECK(overlap == 48);
    CHECK(split == 487);
    CHECK(drone.frames == 952);
    CHECK(ts_sample_make_drone(&drone, &source, 0, 1000, 1000,
                               &split, &overlap, error, sizeof(error)));
    CHECK(overlap == 250);
    CHECK(drone.frames == 750);
    ts_sample_free(&drone);
    ts_sample_free(&source);
}

static void test_preview_is_nondestructive(void)
{
    TsInstrument instrument;
    TsSample drone;
    uint64_t before_hash;
    size_t before_first;
    size_t before_last;
    int before_undo;
    size_t split;
    size_t overlap;
    int direction = 1;
    double position = 0.0;
    char error[160];
    ts_instrument_init(&instrument);
    ts_sample_init(&drone);
    CHECK(ts_instrument_generate(&instrument, TS_GENERATOR_METALLIC,
                                 0x44524f4eu, error, sizeof(error)));
    ts_instrument_set_selection(&instrument, 1000, 9000);
    before_hash = ts_sample_hash(&instrument.current);
    before_first = instrument.selection_first;
    before_last = instrument.selection_last;
    before_undo = instrument.undo_count;
    CHECK(ts_sample_make_drone(&drone, &instrument.current,
                               before_first, before_last, 50,
                               &split, &overlap, error, sizeof(error)));
    for (int i = 0; i < 6; ++i) {
        float heard = ts_audition_read_looped_mode(
            &drone, position, 0, drone.frames, 0, TS_LOOP_FORWARD);
        CHECK(isfinite(heard));
        position += (double)drone.frames + 0.25;
        position = ts_audition_loop_position(position, 0, drone.frames, 0,
                                             TS_LOOP_FORWARD, &direction);
    }
    CHECK(ts_sample_hash(&instrument.current) == before_hash);
    CHECK(instrument.selection_first == before_first);
    CHECK(instrument.selection_last == before_last);
    CHECK(instrument.undo_count == before_undo);
    ts_sample_free(&drone);
    ts_instrument_free(&instrument);
}

static void test_replace_and_history(void)
{
    TsInstrument instrument;
    TsSample before;
    TsSample drone;
    uint64_t result_hash;
    size_t first;
    size_t last;
    size_t split;
    size_t overlap;
    char error[160];
    ts_instrument_init(&instrument);
    ts_sample_init(&before);
    ts_sample_init(&drone);
    CHECK(ts_instrument_generate(&instrument, TS_GENERATOR_TONAL,
                                 0x52504c43u, error, sizeof(error)));
    first = instrument.current.frames / 4u;
    last = instrument.current.frames * 3u / 4u;
    instrument.view_first = first / 2u;
    instrument.view_last = last + first / 2u;
    ts_instrument_set_selection(&instrument, first, last);
    CHECK(ts_sample_clone(&before, &instrument.current, error, sizeof(error)));
    CHECK(ts_sample_make_drone(&drone, &instrument.current, first, last, 50,
                               &split, &overlap, error, sizeof(error)));
    CHECK(ts_instrument_replace_selection_with_drone(
              &instrument, &drone, error, sizeof(error)));
    CHECK(instrument.current.frames == before.frames - overlap);
    CHECK(instrument.has_selection && instrument.selection_first == first);
    CHECK(instrument.selection_last == first + drone.frames);
    CHECK(memcmp(instrument.current.data, before.data,
                 first * sizeof(*before.data)) == 0);
    CHECK(memcmp(instrument.current.data + first, drone.data,
                 drone.frames * sizeof(*drone.data)) == 0);
    CHECK(memcmp(instrument.current.data + first + drone.frames,
                 before.data + last,
                 (before.frames - last) * sizeof(*before.data)) == 0);
    CHECK(instrument.view_first <= first &&
          instrument.view_last >= first + drone.frames);
    result_hash = ts_sample_hash(&instrument.current);
    CHECK(ts_instrument_undo(&instrument, error, sizeof(error)));
    CHECK(samples_equal(&instrument.current, &before));
    CHECK(instrument.selection_first == first && instrument.selection_last == last);
    CHECK(ts_instrument_redo(&instrument, error, sizeof(error)));
    CHECK(ts_sample_hash(&instrument.current) == result_hash);
    CHECK(instrument.selection_first == first &&
          instrument.selection_last == first + drone.frames);
    ts_sample_free(&drone);
    ts_sample_free(&before);
    ts_instrument_free(&instrument);
}

static void test_copy_and_history(void)
{
    TsInstrument instrument;
    TsInstrument restored;
    TsSample drone;
    uint64_t source_hash;
    uint64_t drone_hash;
    TsTuning source_tuning;
    TsTuning source_audible_tuning;
    size_t source_first;
    size_t source_last;
    size_t split;
    size_t overlap;
    int destination = -1;
    char error[160];
    ts_instrument_init(&instrument);
    ts_instrument_init(&restored);
    ts_sample_init(&drone);
    CHECK(ts_instrument_generate(&instrument, TS_GENERATOR_NOISE,
                                 0x434f5059u, error, sizeof(error)));
    source_first = instrument.current.frames / 5u;
    source_last = instrument.current.frames * 4u / 5u;
    ts_instrument_set_selection(&instrument, source_first, source_last);
    CHECK(ts_instrument_set_tuning(&instrument, 41, 17.0f,
                                   error, sizeof(error)));
    CHECK(ts_instrument_set_audible_tuning(&instrument, 53, -9.0f,
                                           error, sizeof(error)));
    source_tuning = instrument.tuning;
    source_audible_tuning = instrument.audible_tuning;
    source_hash = ts_sample_hash(&instrument.current);
    CHECK(ts_sample_make_drone(&drone, &instrument.current,
                               source_first, source_last, 50,
                               &split, &overlap, error, sizeof(error)));
    drone_hash = ts_sample_hash(&drone);
    CHECK(ts_instrument_copy_drone_to_new_tile(
              &instrument, &drone, &destination, error, sizeof(error)));
    CHECK(destination == 1 && instrument.selected_slot == 1);
    CHECK(instrument.bank[0].sample.data != instrument.bank[1].sample.data);
    CHECK(ts_sample_hash(&instrument.bank[0].sample) == source_hash);
    CHECK(ts_sample_hash(&instrument.current) == drone_hash);
    CHECK(instrument.has_selection && instrument.selection_first == 0);
    CHECK(instrument.selection_last == drone.frames);
    CHECK(instrument.has_loop && instrument.loop_first == 0 &&
          instrument.loop_last == drone.frames &&
          instrument.loop_mode == TS_LOOP_FORWARD &&
          instrument.loop_crossfade_ms == 0.0f);
    CHECK(instrument.tuning.root_note == source_tuning.root_note &&
          fabsf(instrument.tuning.fine_tune_cents -
                source_tuning.fine_tune_cents) < 0.0001f);
    CHECK(instrument.audible_tuning.root_note ==
          source_audible_tuning.root_note &&
          fabsf(instrument.audible_tuning.fine_tune_cents -
                source_audible_tuning.fine_tune_cents) < 0.0001f);
    CHECK(ts_instrument_undo(&instrument, error, sizeof(error)));
    CHECK(instrument.current.frames == drone.frames);
    CHECK(ts_sample_peak(&instrument.current) == 0.0f);
    CHECK(ts_instrument_redo(&instrument, error, sizeof(error)));
    CHECK(ts_sample_hash(&instrument.current) == drone_hash);
    CHECK(instrument.has_loop && instrument.loop_last == drone.frames);
    CHECK(ts_instrument_save_recipe(&instrument, "test-drone.tsr",
                                    error, sizeof(error)));
    CHECK(ts_instrument_load_recipe(&restored, "test-drone.tsr",
                                    error, sizeof(error)));
    CHECK(restored.bank[destination].occupied);
    CHECK(ts_sample_hash(&restored.bank[destination].sample) == drone_hash);
    CHECK(restored.bank[destination].has_loop &&
          restored.bank[destination].loop_first == 0 &&
          restored.bank[destination].loop_last == drone.frames &&
          restored.bank[destination].loop_crossfade_ms == 0.0f);
    CHECK(ts_instrument_select_bank(&instrument, 0, error, sizeof(error)));
    CHECK(ts_sample_hash(&instrument.current) == source_hash);
    CHECK(instrument.selection_first == source_first);
    CHECK(instrument.selection_last == source_last);
    remove("test-drone.tsr");
    ts_sample_free(&drone);
    ts_instrument_free(&restored);
    ts_instrument_free(&instrument);
}

static void test_no_destination(void)
{
    TsInstrument instrument;
    TsSample drone;
    uint64_t source_hash;
    size_t split;
    size_t overlap;
    int destination;
    char error[160];
    ts_instrument_init(&instrument);
    ts_sample_init(&drone);
    CHECK(ts_instrument_generate(&instrument, TS_GENERATOR_PULSE,
                                 0x46554c4cu, error, sizeof(error)));
    ts_instrument_set_selection(&instrument, 100, 5000);
    CHECK(ts_sample_make_drone(&drone, &instrument.current, 100, 5000, 50,
                               &split, &overlap, error, sizeof(error)));
    for (int slot = 1; slot < TS_BANK_SLOT_COUNT; ++slot)
        CHECK(ts_instrument_copy_selected(&instrument, slot, error, sizeof(error)));
    CHECK(ts_instrument_select_bank(&instrument, 0, error, sizeof(error)));
    source_hash = ts_sample_hash(&instrument.current);
    destination = -1;
    CHECK(!ts_instrument_copy_drone_to_new_tile(
              &instrument, &drone, &destination, error, sizeof(error)));
    CHECK(destination == -1);
    CHECK(instrument.selected_slot == 0);
    CHECK(ts_sample_hash(&instrument.current) == source_hash);
    ts_sample_free(&drone);
    ts_instrument_free(&instrument);
}

static void test_config(void)
{
    TsConfig config;
    TsConfig loaded;
    char error[160];
    FILE *file;
    ts_config_init(&config);
    CHECK(config.drone_crossfade_ms == 50);
    config.drone_crossfade_ms = 73;
    CHECK(ts_config_save(&config, "test-drone.ini", error, sizeof(error)));
    CHECK(ts_config_load(&loaded, "test-drone.ini", error, sizeof(error)));
    CHECK(loaded.drone_crossfade_ms == 73);
    file = fopen("test-drone.ini", "wb");
    CHECK(file != NULL);
    if (file != NULL) {
        fputs("drone_crossfade_ms=-900\n", file);
        fclose(file);
    }
    CHECK(ts_config_load(&loaded, "test-drone.ini", error, sizeof(error)));
    CHECK(loaded.drone_crossfade_ms == TS_DRONE_CROSSFADE_MS_MIN);
    file = fopen("test-drone.ini", "wb");
    CHECK(file != NULL);
    if (file != NULL) {
        fputs("drone_crossfade_ms=90000\n", file);
        fclose(file);
    }
    CHECK(ts_config_load(&loaded, "test-drone.ini", error, sizeof(error)));
    CHECK(loaded.drone_crossfade_ms == TS_DRONE_CROSSFADE_MS_MAX);
    file = fopen("test-drone.ini", "wb");
    CHECK(file != NULL);
    if (file != NULL) {
        fputs("drone_crossfade_ms=broken\n", file);
        fclose(file);
    }
    CHECK(!ts_config_load(&loaded, "test-drone.ini", error, sizeof(error)));
    remove("test-drone.ini");
}

static void test_ui_contract(void)
{
    TsInstrument instrument;
    TsSample drone;
    TsSample muted;
    TsUiState closed;
    TsUiState open;
    TsFramebuffer before;
    TsFramebuffer after;
    TsFramebuffer muted_frame;
    size_t split = 0;
    size_t overlap = 0;
    size_t region_differences = 0;
    char error[160];
    ts_instrument_init(&instrument);
    ts_sample_init(&drone);
    ts_sample_init(&muted);
    ts_ui_init(&closed);
    CHECK(ts_instrument_generate(&instrument, TS_GENERATOR_METALLIC,
                                 0x55494354u, error, sizeof(error)));
    ts_instrument_set_selection(&instrument, 1000, 5000);
    open = closed;
    CHECK(ts_sample_make_drone(&drone, &instrument.current, 1000, 5000, 50,
                               &split, &overlap, error, sizeof(error)));
    CHECK(ts_sample_clone(&muted, &drone, error, sizeof(error)));
    if (muted.data != NULL)
        memset(muted.data, 0, muted.frames * sizeof(*muted.data));
    open.drone_open = 1;
    open.drone_preview_active = 1;
    open.drone_preview_sample = &drone;
    open.drone_effective_crossfade_ms =
        (float)((double)overlap * 1000.0 / drone.sample_rate);
    open.drone_source_first = 1000;
    open.drone_source_last = 5000;
    open.drone_split_frame = split;
    open.drone_output_frames = drone.frames;
    open.drone_overlap_frames = overlap;
    ts_ui_render(&before, &closed, &instrument);
    ts_ui_render(&after, &open, &instrument);
    open.drone_preview_sample = &muted;
    ts_ui_render(&muted_frame, &open, &instrument);
    CHECK(ts_ui_drone_action_from_point(60, 170) == TS_UI_DRONE_ACTION_PREVIEW);
    CHECK(ts_ui_drone_action_from_point(160, 170) == TS_UI_DRONE_ACTION_STOP);
    CHECK(ts_ui_drone_action_from_point(250, 170) == TS_UI_DRONE_ACTION_COPY);
    CHECK(ts_ui_drone_action_from_point(390, 170) == TS_UI_DRONE_ACTION_REPLACE);
    CHECK(ts_ui_drone_action_from_point(500, 170) == TS_UI_DRONE_ACTION_CANCEL);
    CHECK(ts_ui_drone_action_from_point(250, 205) == TS_UI_DRONE_ACTION_NONE);
    CHECK(ts_ui_drone_waveform_contains(TS_DRONE_WAVE_X, TS_DRONE_WAVE_Y));
    CHECK(!ts_ui_drone_waveform_contains(TS_DRONE_WAVE_X, 205));
    {
        size_t right_frames = open.drone_source_last - open.drone_split_frame;
        int first_x = TS_DRONE_WAVE_X +
            (int)((right_frames - overlap) * TS_DRONE_WAVE_W / drone.frames);
        int last_x = TS_DRONE_WAVE_X +
            (int)(right_frames * TS_DRONE_WAVE_W / drone.frames);
        CHECK(ts_ui_drone_crossfade_handle_from_point(
                  &open, first_x, TS_DRONE_WAVE_Y + 10) == 1);
        CHECK(ts_ui_drone_crossfade_handle_from_point(
                  &open, last_x, TS_DRONE_WAVE_Y + 10) == 2);
        CHECK(ts_ui_drone_crossfade_handle_from_point(&open, 5, 5) == 0);
    }
    for (int y = TS_DRONE_WAVE_Y; y < TS_DRONE_WAVE_Y + TS_DRONE_WAVE_H; ++y)
        for (int x = TS_DRONE_WAVE_X; x < TS_DRONE_WAVE_X + TS_DRONE_WAVE_W; ++x)
            if (after.pixels[y * TS_UI_WIDTH + x] !=
                muted_frame.pixels[y * TS_UI_WIDTH + x])
                ++region_differences;
    CHECK(region_differences > 20u);
    for (int y = 205; y < TS_UI_HEIGHT; ++y) {
        for (int x = 0; x < TS_UI_WIDTH; ++x) {
            if (y < 229 && x >= 330 && x < 402) continue;
            CHECK(before.pixels[y * TS_UI_WIDTH + x] ==
                  after.pixels[y * TS_UI_WIDTH + x]);
        }
    }
    ts_sample_free(&muted);
    ts_sample_free(&drone);
    ts_instrument_free(&instrument);
}

int main(void)
{
    test_processing_core();
    test_rates_and_fallback();
    test_preview_is_nondestructive();
    test_replace_and_history();
    test_copy_and_history();
    test_no_destination();
    test_config();
    test_ui_contract();
    if (failures != 0) {
        fprintf(stderr, "%d Drone Maker checks failed\n", failures);
        return 1;
    }
    puts("TapeSister Drone Maker DSP, state, history, config, and UI tests passed");
    return 0;
}
