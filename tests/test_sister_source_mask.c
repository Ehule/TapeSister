#include "sister_test_helpers.h"

#include <stdio.h>

static int failures;
#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; \
} } while (0)

int main(void)
{
    TsSisterRuntime runtime;
    TsInstrument instrument;
    TsNoteEvent qwerty;
    TsNoteEvent midi;
    char error[160];

    CHECK(sister_test_make_tiles(&instrument, 2, 2, 1000u, 32u));
    CHECK(sister_test_enable(&runtime, 1000u, 2u, 0.1));
    ts_sister_runtime_set_sources(&runtime, TS_SISTER_SOURCE_TILES);
    instrument.bank[1].locked = 1;
    CHECK(ts_sister_runtime_set_source_slot(&runtime, &instrument, 0, 1));
    CHECK(ts_sister_runtime_set_source_slot(&runtime, &instrument, 1, 1));
    CHECK(ts_sister_runtime_source_mask(&runtime) == 0x3u);
    CHECK(!ts_sister_runtime_set_source_slot(&runtime, &instrument, 2, 1));

    CHECK(ts_note_event_qwerty(&qwerty, 0, TS_KEYBOARD_BASE_NOTE));
    CHECK(ts_sister_runtime_note_on(&runtime, &instrument, &qwerty, 0,
                                    1000) == 2);
    CHECK(ts_performance_count(&runtime.performance) == 2);
    CHECK(ts_sister_runtime_note_on(&runtime, &instrument, &qwerty, 0,
                                    1000) == 2);
    CHECK(ts_performance_count(&runtime.performance) == 2);
    ts_sister_runtime_note_off(&runtime, &qwerty);
    CHECK(ts_performance_count(&runtime.performance) == 2);
    for (int frame = 0; frame < 40; ++frame)
        (void)ts_sister_runtime_process_frame(&runtime, NULL);
    CHECK(ts_performance_count(&runtime.performance) == 0);

    CHECK(ts_note_event_midi(&midi, 64, 63, 4));
    CHECK(ts_sister_runtime_note_on(&runtime, &instrument, &midi, 0,
                                    1000) == 2);
    CHECK(runtime.performance.voices[0].gain > 0.0f &&
          runtime.performance.voices[0].gain < 1.0f);
    ts_sister_runtime_release_midi_channel(&runtime, 4);
    CHECK(ts_performance_count(&runtime.performance) == 0);

    CHECK(ts_sister_runtime_set_page(&runtime, 1u, &instrument));
    CHECK(ts_sister_runtime_source_mask(&runtime) == 0u);
    CHECK(ts_sister_runtime_set_source_slot(&runtime, &instrument, 1, 1));
    CHECK(ts_sister_runtime_set_page(&runtime, 0u, &instrument));
    CHECK(ts_sister_runtime_source_mask(&runtime) == 0x3u);
    CHECK(ts_sister_runtime_set_page(&runtime, 1u, &instrument));
    CHECK(ts_sister_runtime_source_mask(&runtime) == 0x2u);

    CHECK(ts_sister_runtime_note_on(&runtime, &instrument, &qwerty, 0,
                                    1000) == 1);
    ts_sister_runtime_prepare_slot_replacement(&runtime, 1);
    CHECK(ts_performance_count(&runtime.performance) == 0);
    CHECK(ts_sister_runtime_source_mask(&runtime) == 0x2u);
    instrument.bank[1].locked = 0;
    CHECK(ts_instrument_bank_clear(&instrument, 1, error, sizeof(error)));
    CHECK(ts_sister_runtime_validate_source_mask(&runtime, &instrument) == 0u);

    ts_sister_runtime_clear_source_mask(&runtime);
    CHECK(ts_sister_runtime_source_mask(&runtime) == 0u);
    ts_sister_runtime_panic(&runtime);
    CHECK(ts_performance_count(&runtime.performance) == 0);

    ts_sister_runtime_project_close(&runtime);
    CHECK(ts_sister_runtime_set_page(&runtime, 0u, &instrument));
    CHECK(ts_sister_runtime_source_mask(&runtime) == 0u);
    ts_sister_runtime_free(&runtime);
    ts_instrument_free(&instrument);

    if (failures) return 1;
    puts("sister source-mask tests passed");
    return 0;
}
