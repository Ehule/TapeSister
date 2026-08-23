#include "sister_test_helpers.h"
#include "tapesister/sister_ui.h"

#include <stdio.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main(void)
{
    TsSisterRuntime runtime;
    TsInstrument instrument;
    char error[160];
    CHECK(sister_test_make_tiles(&instrument, 1, 2, 1000u, 32u));
    CHECK(sister_test_enable(&runtime, 1000u, 2u, 0.1));
    CHECK(ts_sister_runtime_find_destination(&runtime, &instrument, 0) == 1);
    CHECK(ts_sister_ui_hit_test(12, 374).action == TS_SISTER_UI_ACTION_TAP);
    CHECK(ts_sister_ui_hit_test(78, 374).action == TS_SISTER_UI_ACTION_CAPTURE_FORMAT);
    CHECK(ts_sister_ui_hit_test(130, 374).action == TS_SISTER_UI_ACTION_DESTINATION);
    CHECK(ts_sister_runtime_arm_capture(&runtime, &instrument, 1, 16u, 1000u,
                                        2u, TS_SISTER_TAP_H2, 0u,
                                        error, sizeof(error)));
    CHECK(runtime.capture.channels == 2u);
    CHECK(runtime.selected_tap == TS_SISTER_TAP_H2);
    CHECK(ts_sister_runtime_trigger_capture(&runtime, error, sizeof(error)));
    CHECK(ts_sister_runtime_cancel_capture(&runtime));
    ts_sister_runtime_free(&runtime);
    ts_instrument_free(&instrument);
    puts("Sister capture UI tests passed");
    return failures != 0;
}
