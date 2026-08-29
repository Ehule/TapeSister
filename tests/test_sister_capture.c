#include "sister_test_helpers.h"

#include <math.h>
#include <stdio.h>

static int failures;
#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; \
} } while (0)
#define CLOSE(a,b) sister_close((a),(b),0.001f)

static void configure_fast_head(TsSisterRuntime *runtime)
{
    TsSisterParameters parameters = runtime->parameters;
    parameters.head1_level = 1.0f;
    parameters.head1_time_ms = 1.0f;
    parameters.head1_feedback = 0.0f;
    parameters.head2_level = 0.0f;
    parameters.head3_level = 0.0f;
    parameters.headroom = 1.0f;
    ts_sister_runtime_set_parameters(runtime, &parameters);
    ts_sister_machine_reset(&runtime->machine);
    ts_sister_runtime_set_sources(runtime, TS_SISTER_SOURCE_PREVIEW);
}

static void record_frames(TsSisterRuntime *runtime, int frames,
                          TsStereoFrame value)
{
    TsSisterSourceFrames source = {0};
    source.preview = value;
    for (int frame = 0; frame < frames; ++frame)
        (void)ts_sister_runtime_process_frame(runtime, &source);
}

int main(void)
{
    TsSisterRuntime runtime;
    TsInstrument instrument;
    TsSisterRoutingSnapshot snapshot;
    char error[160];

    CHECK(sister_test_make_tiles(&instrument, 1, 3, 1000u, 32u));
    CHECK(sister_test_enable(&runtime, 1000u, 2u, 0.1));
    configure_fast_head(&runtime);
    CHECK(ts_sister_runtime_find_destination(&runtime, &instrument, 0) == 1);

    {
        TsCaptureRecorder prepared;
        float *prepared_buffer;
        ts_capture_init(&prepared);
        CHECK(ts_sister_runtime_validate_capture_target(
            &runtime, &instrument, 1, 0u, 0, error, sizeof(error)));
        CHECK(ts_capture_arm_channels(&prepared, 1, 8u, 1000u, 2u,
                                      error, sizeof(error)));
        CHECK(ts_capture_set_source(&prepared, TS_CAPTURE_SOURCE_SISTER,
                                    error, sizeof(error)));
        prepared_buffer = prepared.buffer;
        CHECK(ts_sister_runtime_install_prepared_capture(
            &runtime, &instrument, &prepared, TS_SISTER_TAP_H2, 0u,
            error, sizeof(error)));
        CHECK(runtime.capture.buffer == prepared_buffer);
        CHECK(runtime.capture.state == TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER);
        CHECK(runtime.selected_tap == TS_SISTER_TAP_H2);
        CHECK(prepared.buffer == NULL && prepared.state == TS_CAPTURE_IDLE);
        CHECK(ts_sister_runtime_cancel_capture(&runtime));
        ts_capture_free(&prepared);
    }

    for (int tap = 0; tap < TS_SISTER_TAP_COUNT; ++tap) {
        CHECK(ts_sister_runtime_arm_capture(
            &runtime, &instrument, 1, 4u, 1000u, 2u,
            (TsSisterTap)tap, 0u, error, sizeof(error)));
        CHECK(runtime.selected_tap == (TsSisterTap)tap);
        CHECK(ts_sister_runtime_cancel_capture(&runtime));
    }

    CHECK(ts_sister_runtime_set_source_slot(&runtime, &instrument, 0, 1));
    CHECK(!ts_sister_runtime_arm_capture(
        &runtime, &instrument, 0, 16u, 1000u, 2u,
        TS_SISTER_TAP_MIX, 0u, error, sizeof(error)));
    CHECK(runtime.destination_status ==
          TS_SISTER_DESTINATION_SOURCE_CONFLICT);
    ts_sister_runtime_clear_source_mask(&runtime);
    CHECK(!ts_sister_runtime_arm_capture(
        &runtime, &instrument, 2, 16u, 1000u, 2u,
        TS_SISTER_TAP_MIX, (uint16_t)(1u << 2), error, sizeof(error)));
    CHECK(runtime.destination_status ==
          TS_SISTER_DESTINATION_SOURCE_CONFLICT);

    instrument.bank[2].locked = 1;
    CHECK(!ts_sister_runtime_arm_capture(
        &runtime, &instrument, 2, 16u, 1000u, 2u,
        TS_SISTER_TAP_MIX, 0u, error, sizeof(error)));
    CHECK(runtime.destination_status == TS_SISTER_DESTINATION_LOCKED);
    instrument.bank[2].locked = 0;
    CHECK(!ts_sister_runtime_arm_capture(
        &runtime, &instrument, 0, 16u, 1000u, 2u,
        TS_SISTER_TAP_MIX, 0u, error, sizeof(error)));
    CHECK(runtime.destination_status == TS_SISTER_DESTINATION_OCCUPIED);

    ts_sister_runtime_set_monitor(&runtime, 0);
    CHECK(ts_sister_runtime_arm_capture(
        &runtime, &instrument, 1, 16u, 1000u, 2u,
        TS_SISTER_TAP_MIX, 0u, error, sizeof(error)));
    CHECK(ts_sister_runtime_trigger_capture(&runtime, error, sizeof(error)));
    record_frames(&runtime, 16, (TsStereoFrame){0.8f, -0.2f});
    CHECK(runtime.capture.state == TS_CAPTURE_COMPLETED);
    CHECK(runtime.capture.channels == 2u);
    CHECK(ts_sister_runtime_get_snapshot(&runtime, &snapshot));
    CHECK(!snapshot.monitor_enabled && snapshot.capture_state ==
          TS_CAPTURE_COMPLETED);
    CHECK(snapshot.capture_recorded_frames == 16u &&
          snapshot.capture_capacity_frames == 16u);
    {
        float before_l = runtime.capture.buffer[4];
        float before_r = runtime.capture.buffer[5];
        CHECK(ts_sister_runtime_commit_capture(&runtime, &instrument, 1,
                                               error, sizeof(error)));
        CHECK(instrument.bank[1].capture_kind == TS_BANK_CAPTURE_SISTER_MIX);
        CHECK(instrument.bank[1].sample.channels == 2u);
        CHECK(instrument.bank[1].parent_slot == TS_CAPTURE_SOURCE_SISTER);
        CHECK(instrument.bank[1].sample.frames == 16u);
        if (fabsf(before_l) > 0.0001f)
            CHECK(CLOSE(instrument.bank[1].sample.data[5] /
                        instrument.bank[1].sample.data[4],
                        before_r / before_l));
    }
    CHECK(ts_instrument_undo(&instrument, error, sizeof(error)));
    CHECK(ts_instrument_redo(&instrument, error, sizeof(error)));

    CHECK(ts_sister_runtime_arm_capture(
        &runtime, &instrument, 2, 8u, 1000u, 1u,
        TS_SISTER_TAP_H1, 0u, error, sizeof(error)));
    CHECK(ts_sister_runtime_trigger_capture(&runtime, error, sizeof(error)));
    record_frames(&runtime, 8, (TsStereoFrame){0.6f, 0.2f});
    CHECK(ts_sister_runtime_commit_capture(&runtime, &instrument, 1,
                                           error, sizeof(error)));
    CHECK(instrument.bank[2].sample.channels == 1u);
    CHECK(instrument.bank[2].capture_kind == TS_BANK_CAPTURE_SISTER_H1);

    CHECK(ts_sister_runtime_arm_overdub(
        &runtime, &instrument, 2, 4u, 1000u,
        TS_SISTER_TAP_H1, 0u, error, sizeof(error)));
    CHECK(runtime.capture.channels == 1u);
    CHECK(ts_sister_runtime_cancel_capture(&runtime));

    {
        float *old_pointer = instrument.bank[1].sample.data;
        CHECK(ts_sister_runtime_arm_overdub(
            &runtime, &instrument, 1, 8u, 1000u,
            TS_SISTER_TAP_H2, 0u, error, sizeof(error)));
        CHECK(runtime.capture.channels == 2u);
        CHECK(ts_sister_runtime_trigger_capture(&runtime, error, sizeof(error)));
        record_frames(&runtime, 8, (TsStereoFrame){0.2f, 0.1f});
        CHECK(ts_sister_runtime_commit_capture(&runtime, &instrument, 1,
                                               error, sizeof(error)));
        CHECK(instrument.bank[1].sample.data != old_pointer);
        CHECK(instrument.bank[1].sample.channels == 2u);
        CHECK(instrument.bank[1].capture_kind == TS_BANK_CAPTURE_SISTER_H2);
    }

    CHECK(ts_sister_runtime_arm_overdub(
        &runtime, &instrument, 1, 4u, 1000u,
        TS_SISTER_TAP_H3, 0u, error, sizeof(error)));
    CHECK(ts_sister_runtime_trigger_capture(&runtime, error, sizeof(error)));
    record_frames(&runtime, 4, (TsStereoFrame){0.1f, 0.1f});
    instrument.bank[1].sample.data[0] += 0.01f;
    CHECK(!ts_sister_runtime_commit_capture(&runtime, &instrument, 1,
                                            error, sizeof(error)));
    CHECK(runtime.destination_status == TS_SISTER_DESTINATION_STALE);
    ts_capture_free(&runtime.capture);

    CHECK(ts_sister_runtime_find_destination(&runtime, &instrument, 0) == 3);
    CHECK(ts_sister_runtime_arm_capture(
        &runtime, &instrument, 3, 40u, 1000u, 2u,
        TS_SISTER_TAP_H3, 0u, error, sizeof(error)));
    CHECK(ts_sister_runtime_trigger_capture(&runtime, error, sizeof(error)));
    record_frames(&runtime, 40, (TsStereoFrame){0.4f, -0.1f});
    CHECK(ts_sister_runtime_commit_capture(&runtime, &instrument, 1,
                                           error, sizeof(error)));
    CHECK(instrument.bank[3].sample.frames == 40u);
    CHECK(instrument.bank[3].capture_kind == TS_BANK_CAPTURE_SISTER_H3);
    CHECK(strcmp(ts_bank_capture_name(TS_BANK_CAPTURE_SISTER_MIX),
                 "SISTER MIX") == 0);
    CHECK(strcmp(ts_bank_capture_name(TS_BANK_CAPTURE_SISTER_H1),
                 "SISTER H1") == 0);
    CHECK(strcmp(ts_bank_capture_name(TS_BANK_CAPTURE_SISTER_H2),
                 "SISTER H2") == 0);
    CHECK(strcmp(ts_bank_capture_name(TS_BANK_CAPTURE_SISTER_H3),
                 "SISTER H3") == 0);

    ts_sister_runtime_free(&runtime);
    ts_instrument_free(&instrument);
    if (failures) return 1;
    puts("sister capture tests passed");
    return 0;
}
