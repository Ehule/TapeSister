#include "sister_test_helpers.h"
#include "tapesister/sister_ui.h"
#include "tapesister/ui.h"

#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main(void)
{
    TsSisterRuntime runtime;
    TsInstrument instrument;
    TsSisterRoutingSnapshot routing;
    TsSisterUiModel model;
    TsConfig config;
    TsFramebuffer fb;
    TsPalette palette;
    char status[160];
    uint16_t mask;
    uint64_t protected_hash;
    CHECK(sister_test_make_tiles(&instrument, 4, 2, 1000u, 32u));
    CHECK(sister_test_enable(&runtime, 1000u, 2u, 0.1));
    CHECK(ts_sister_runtime_set_source_slot(&runtime, &instrument, 0, 1));
    CHECK(ts_sister_runtime_set_source_slot(&runtime, &instrument, 3, 1));
    CHECK(ts_sister_runtime_get_snapshot(&runtime, &routing));
    ts_config_init(&config);
    ts_sister_ui_model_init(&model, &config);
    ts_sister_ui_model_update(&model, &routing, NULL, NULL, NULL);
    CHECK(model.routing.source_mask == 0x9u);
    CHECK(ts_sister_ui_hit_test(12, 195).action == TS_SISTER_UI_ACTION_SOURCE_TILES);
    CHECK(ts_sister_ui_hit_test(240, 195).action == TS_SISTER_UI_ACTION_SOURCE_PREVIEW);
    CHECK(ts_sister_runtime_set_page(&runtime, 1u, &instrument));
    CHECK(ts_sister_runtime_source_mask(&runtime) == 0u);
    CHECK(ts_sister_runtime_set_page(&runtime, 0u, &instrument));
    CHECK(ts_sister_runtime_source_mask(&runtime) == 0x9u);

    /* Normal Sample Bank Shift-click is occupancy-sensitive and never makes
       source membership synonymous with active-canvas selection. */
    ts_sister_runtime_clear_source_mask(&runtime);
    CHECK(instrument.selected_slot == 0);
    protected_hash = ts_sample_hash(&instrument.bank[1].sample);
    instrument.bank[1].locked = 1;
    CHECK(ts_sister_runtime_shift_sample_tile(
              &runtime, &instrument, 1, status, sizeof(status)) ==
          TS_SISTER_TILE_SHIFT_SOURCE_ADDED);
    CHECK(instrument.selected_slot == 0);
    CHECK(instrument.bank[1].locked);
    CHECK(ts_sample_hash(&instrument.bank[1].sample) == protected_hash);
    CHECK(ts_sister_runtime_shift_sample_tile(
              &runtime, &instrument, 1, status, sizeof(status)) ==
          TS_SISTER_TILE_SHIFT_SOURCE_REMOVED);
    CHECK(ts_sample_hash(&instrument.bank[1].sample) == protected_hash);
    CHECK(ts_sister_runtime_shift_sample_tile(
              &runtime, &instrument, 2, status, sizeof(status)) ==
          TS_SISTER_TILE_SHIFT_SOURCE_ADDED);
    mask = ts_sister_runtime_source_mask(&runtime);
    CHECK(ts_ui_execute_bank_action(&instrument, 3,
                                    TS_UI_BANK_ACTION_AUDITION,
                                    status, sizeof(status)));
    CHECK(instrument.selected_slot == 3);
    CHECK(ts_sister_runtime_source_mask(&runtime) == mask);
    CHECK(ts_sister_runtime_shift_sample_tile(
              &runtime, &instrument, 6, status, sizeof(status)) ==
          TS_SISTER_TILE_SHIFT_COPIED);
    CHECK(instrument.bank[6].occupied);
    CHECK((ts_sister_runtime_source_mask(&runtime) & (1u << 6)) == 0u);
    protected_hash = ts_sample_hash(&instrument.bank[6].sample);
    CHECK(ts_sister_runtime_shift_sample_tile(
              &runtime, &instrument, 6, status, sizeof(status)) ==
          TS_SISTER_TILE_SHIFT_SOURCE_ADDED);
    CHECK(ts_sample_hash(&instrument.bank[6].sample) == protected_hash);
    {
        TsInstrument empty;
        ts_instrument_init(&empty);
        CHECK(ts_sister_runtime_shift_sample_tile(
                  &runtime, &empty, 7, status, sizeof(status)) ==
              TS_SISTER_TILE_SHIFT_FAILED);
        CHECK(strstr(status, "No Current") != NULL);
        CHECK(!empty.bank[7].occupied);
        ts_instrument_free(&empty);
    }

    ts_sister_runtime_set_sources(&runtime, TS_SISTER_SOURCE_TILES);
    CHECK(ts_sister_runtime_tiles_insert_active(&runtime));
    mask = ts_sister_runtime_source_mask(&runtime);
    ts_sister_runtime_set_sources(&runtime, 0u);
    CHECK(!ts_sister_runtime_tiles_insert_active(&runtime));
    CHECK(ts_sister_runtime_source_mask(&runtime) == mask);
    ts_sister_runtime_set_sources(&runtime, TS_SISTER_SOURCE_TILES);
    CHECK(ts_sister_runtime_source_mask(&runtime) == mask);

    /* Deterministic pixels for ordinary, active, source, and combined state.
       The last combined pass follows a simulated protected-tile dim pass, so
       both borders prove they are restored after dimming. */
    ts_palette_default(&palette);
    palette.colors[TS_PALETTE_ACTIVE_TILE] = 0xff112233u;
    palette.colors[TS_PALETTE_SISTER_SOURCE_HORIZONTAL] = 0xff445566u;
    palette.colors[TS_PALETTE_SISTER_SOURCE_VERTICAL] = 0xff778899u;
    memset(&fb, 0, sizeof(fb));
    ts_ui_draw_tile_state_borders(&fb, 0, 0, 0, &palette);
    CHECK(fb.pixels[328 * TS_UI_WIDTH + 8] == 0u);
    ts_ui_draw_tile_state_borders(&fb, 0, 1, 0, &palette);
    CHECK(fb.pixels[328 * TS_UI_WIDTH + 8] == 0xff112233u);
    memset(&fb, 0x22, sizeof(fb));
    ts_ui_draw_tile_state_borders(&fb, 0, 0, 1, &palette);
    CHECK(fb.pixels[328 * TS_UI_WIDTH + 20] == 0xff445566u);
    CHECK(fb.pixels[340 * TS_UI_WIDTH + 8] == 0xff778899u);
    memset(&fb, 0x11, sizeof(fb));
    ts_ui_draw_tile_state_borders(&fb, 15, 1, 1, &palette);
    CHECK(fb.pixels[353 * TS_UI_WIDTH + 559] == 0xff445566u);
    CHECK(fb.pixels[365 * TS_UI_WIDTH + 547] == 0xff778899u);
    CHECK(fb.pixels[356 * TS_UI_WIDTH + 560] == 0xff112233u);
    ts_sister_runtime_free(&runtime);
    ts_instrument_free(&instrument);
    puts("Sister source UI tests passed");
    return failures != 0;
}
