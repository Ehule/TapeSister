#include "sister_test_helpers.h"

#include <stdio.h>

static int failures;
#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; \
} } while (0)
#define CLOSE(a,b) sister_close((a),(b),0.0002f)

int main(void)
{
    TsSisterRuntime runtime;
    TsInstrument instrument;
    TsSisterParameters parameters;
    TsSisterRuntimeFrame frame = {0};
    TsNoteEvent qwerty;
    TsNoteEvent midi;
    char error[160];
    int heard_generation = 0;

    CHECK(sister_test_make_tiles(&instrument, 1, 2, 1000u, 64u));
    CHECK(sister_test_enable(&runtime, 1000u, 2u, 0.2));
    parameters = runtime.parameters;
    parameters.head1_level = 1.0f;
    parameters.head1_time_ms = 1.0f;
    parameters.head1_feedback = 0.0f;
    parameters.head2_level = 0.0f;
    parameters.head3_level = 0.0f;
    parameters.headroom = 1.0f;
    ts_sister_runtime_set_parameters(&runtime, &parameters);
    ts_sister_machine_reset(&runtime.machine);
    ts_sister_runtime_set_sources(&runtime, TS_SISTER_SOURCE_TILES);
    CHECK(ts_sister_runtime_set_source_slot(&runtime, &instrument, 0, 1));
    CHECK(ts_note_event_qwerty(&qwerty, 0, TS_KEYBOARD_BASE_NOTE));
    CHECK(ts_sister_runtime_note_on(&runtime, &instrument, &qwerty, 0,
                                    1000) == 1);

    CHECK(ts_sister_runtime_arm_capture(
        &runtime, &instrument, 1, 24u, 1000u, 2u,
        TS_SISTER_TAP_MIX, 0u, error, sizeof(error)));
    CHECK(ts_sister_runtime_trigger_capture(&runtime, error, sizeof(error)));
    for (int i = 0; i < 24; ++i)
        frame = ts_sister_runtime_process_frame(&runtime, NULL);
    CHECK(runtime.capture.state == TS_CAPTURE_COMPLETED);
    CHECK(ts_sister_runtime_commit_capture(&runtime, &instrument, 1,
                                           error, sizeof(error)));
    CHECK((ts_sister_runtime_source_mask(&runtime) & (1u << 1)) == 0u);
    CHECK(instrument.bank[1].capture_kind == TS_BANK_CAPTURE_SISTER_MIX);

    CHECK(ts_sister_runtime_set_source_slot(&runtime, &instrument, 1, 1));
    CHECK(ts_sister_runtime_source_mask(&runtime) == 0x3u);
    ts_sister_runtime_note_off(&runtime, &qwerty);
    CHECK(ts_sister_runtime_note_on(&runtime, &instrument, &qwerty, 0,
                                    1000) == 2);
    for (int i = 0; i < 24; ++i) {
        frame = ts_sister_runtime_process_frame(&runtime, NULL);
        if (sister_peak(frame.input) > 0.0001f) heard_generation = 1;
    }
    CHECK(heard_generation);

    ts_sister_runtime_note_off(&runtime, &qwerty);
    CHECK(ts_note_event_midi(&midi, 67, 96, 2));
    CHECK(ts_sister_runtime_note_on(&runtime, &instrument, &midi, 0,
                                    1000) == 2);
    /* Both independently retriggered QWERTY one-shots survive key-up and
       overlap both new MIDI source voices; the shorter captured generations
       have already ended. */
    CHECK(ts_performance_count(&runtime.performance) == 4);
    ts_sister_runtime_note_off(&runtime, &midi);

    ts_sister_runtime_set_sources(&runtime, 0u);
    for (int sample = 0; sample < 20; ++sample)
        frame = ts_sister_runtime_process_frame(&runtime, NULL);
    CHECK(CLOSE(frame.input.l, 0.0f) && CLOSE(frame.input.r, 0.0f));
    ts_sister_runtime_panic(&runtime);
    CHECK(ts_performance_count(&runtime.performance) == 0);
    ts_sister_runtime_free(&runtime);
    ts_instrument_free(&instrument);

    if (failures) return 1;
    puts("sister recursion tests passed");
    return 0;
}
