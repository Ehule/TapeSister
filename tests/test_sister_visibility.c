#include "sister_test_helpers.h"
#include "tapesister/sister_ui.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    TsSisterRuntime runtime;
    TsSisterUiModel model;
    TsConfig config;
    TsInstrument instrument;
    TsNoteEvent note;
    float *buffer;
    uint64_t clock;
    ts_config_init(&config);
    ts_sister_ui_model_init(&model, &config);
    ts_sister_runtime_init(&runtime);

    ts_sister_ui_model_show(&model);
    ts_sister_ui_model_hide(&model);
    ts_sister_ui_model_show(&model);
    assert(!runtime.enabled && runtime.machine.buffer.data == NULL);

    assert(sister_test_make_tiles(&instrument, 1, 1, 1000u, 64u));
    assert(sister_test_enable(&runtime, 1000u, 2u, 0.1));
    ts_sister_runtime_set_sources(&runtime,
        TS_SISTER_SOURCE_TILES | TS_SISTER_SOURCE_FM | TS_SISTER_SOURCE_EXT);
    assert(ts_sister_runtime_set_source_slot(&runtime, &instrument, 0, 1));
    assert(ts_note_event_qwerty(&note, 0, TS_KEYBOARD_BASE_NOTE));
    assert(ts_sister_runtime_note_on(&runtime, &instrument, &note, 0, 1000) == 1);
    buffer = runtime.machine.buffer.data;
    clock = runtime.machine.master_clock;
    for (int cycle = 0; cycle < 32; ++cycle) {
        ts_sister_ui_model_hide(&model);
        (void)ts_sister_runtime_process_frame(&runtime, NULL);
        ts_sister_ui_model_show(&model);
        assert(runtime.enabled && runtime.machine.buffer.data == buffer);
    }
    assert(runtime.machine.master_clock > clock);
    assert(ts_performance_count(&runtime.performance) > 0);

    ts_sister_runtime_set_rolling(&runtime, 0);
    ts_sister_runtime_set_hold(&runtime, 1);
    ts_sister_runtime_set_monitor(&runtime, 1);
    clock = runtime.machine.master_clock;
    ts_sister_ui_model_hide(&model);
    ts_sister_ui_model_show(&model);
    assert(!runtime.rolling && runtime.held && runtime.monitor_enabled);
    assert(runtime.source_switches ==
        (TS_SISTER_SOURCE_TILES | TS_SISTER_SOURCE_FM | TS_SISTER_SOURCE_EXT));
    assert(runtime.machine.buffer.data == buffer &&
           runtime.machine.master_clock == clock);

    ts_sister_runtime_disable(&runtime);
    ts_sister_ui_model_hide(&model);
    ts_sister_ui_model_show(&model);
    assert(!runtime.enabled && runtime.machine.buffer.data == NULL);
    assert(runtime.monitor_enabled); /* POWER no longer rewrites monitor state. */
    ts_sister_runtime_free(&runtime);
    ts_instrument_free(&instrument);
    puts("sister visibility tests passed");
    return 0;
}
