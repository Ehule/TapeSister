#include "sister_test_helpers.h"
#include "tapesister/sister_ui.h"

#include <stdio.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main(void)
{
    TsSisterRuntime runtime;
    TsInstrument instrument;
    TsSisterRoutingSnapshot routing;
    TsSisterUiModel model;
    TsConfig config;
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
    ts_sister_runtime_free(&runtime);
    ts_instrument_free(&instrument);
    puts("Sister source UI tests passed");
    return failures != 0;
}
