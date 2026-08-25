#include "sister_test_helpers.h"
#include "tapesister/audio_mixer.h"

#include <stdio.h>

static int failures;
#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; \
} } while (0)
#define CLOSE(a,b) sister_close((a),(b),0.0002f)

int main(void)
{
    TsSisterRuntime runtime;
    TsSisterParameters parameters;
    TsSisterSourceFrames source = {0};
    TsSisterRuntimeFrame frame;
    TsSisterRoutingSnapshot snapshot;
    uint64_t clock;
    size_t write_position;
    float held_value;

    ts_sister_runtime_init(&runtime);
    CHECK(!runtime.enabled && runtime.machine.buffer.data == NULL);
    CHECK(!ts_sister_runtime_enable(&runtime, 0u, 2u, 2u, 0.1,
                                    NULL, 0u));
    CHECK(sister_test_enable(&runtime, 1000u, 2u, 0.1));
    CHECK(ts_sister_runtime_owns_direct_tile_bus(&runtime));
    parameters = runtime.parameters;
    parameters.head1_level = 1.0f;
    parameters.head1_time_ms = 1.0f;
    parameters.head2_level = 1.0f;
    parameters.head3_level = 1.0f;
    parameters.head1_feedback = 0.0f;
    parameters.head2_feedback = 0.0f;
    parameters.headroom = 0.5f;
    ts_sister_runtime_set_parameters(&runtime, &parameters);
    ts_sister_machine_reset(&runtime.machine);
    ts_sister_runtime_set_sources(&runtime, TS_SISTER_SOURCE_PREVIEW);
    source.preview = (TsStereoFrame){0.5f, -0.25f};

    clock = runtime.machine.master_clock;
    frame = ts_sister_runtime_process_frame(&runtime, &source);
    CHECK(runtime.machine.master_clock == clock + 1u);
    CHECK(runtime.processed_frames == 1u);
    for (int i = 0; i < 120; ++i)
        frame = ts_sister_runtime_process_frame(&runtime, &source);
    for (int tap = 0; tap < TS_SISTER_TAP_COUNT; ++tap)
        CHECK(sister_frame_finite(frame.tap[tap]));

    ts_sister_runtime_set_monitor(&runtime, 0);
    frame = ts_sister_runtime_process_frame(&runtime, &source);
    CHECK(CLOSE(frame.monitor_return.l, 0.0f));
    ts_sister_runtime_set_monitor(&runtime, 1);
    for (int i = 0; i < 20; ++i)
        frame = ts_sister_runtime_process_frame(&runtime, &source);
    CHECK(CLOSE(frame.monitor_return.l,
                frame.input.l + frame.tap[TS_SISTER_TAP_MIX].l));
    CHECK(CLOSE(frame.monitor_return.r,
                frame.input.r + frame.tap[TS_SISTER_TAP_MIX].r));

    parameters = runtime.parameters;
    parameters.monitor_dry = 0.25f;
    parameters.monitor_wet = 0.50f;
    ts_sister_runtime_set_parameters(&runtime, &parameters);
    for (int i = 0; i < 300; ++i)
        frame = ts_sister_runtime_process_frame(&runtime, &source);
    CHECK(CLOSE(frame.dry_monitor_gain, 0.25f));
    CHECK(CLOSE(frame.monitor_return.l,
                frame.input.l * 0.25f +
                frame.tap[TS_SISTER_TAP_MIX].l * 0.50f));
    CHECK(CLOSE(frame.monitor_return.r,
                frame.input.r * 0.25f +
                frame.tap[TS_SISTER_TAP_MIX].r * 0.50f));

    write_position = (size_t)(runtime.machine.master_clock %
                              runtime.machine.buffer.capacity_frames);
    ts_sister_runtime_set_hold(&runtime, 1);
    held_value = runtime.machine.buffer.data[write_position * 2u];
    source.preview = (TsStereoFrame){-0.9f, 0.9f};
    clock = runtime.machine.master_clock;
    (void)ts_sister_runtime_process_frame(&runtime, &source);
    CHECK(runtime.machine.master_clock == clock + 1u);
    CHECK(CLOSE(runtime.machine.buffer.data[write_position * 2u], held_value));
    ts_sister_runtime_set_hold(&runtime, 0);
    ts_sister_runtime_set_rolling(&runtime, 0);
    CHECK(!ts_sister_runtime_owns_direct_tile_bus(&runtime));
    write_position = (size_t)(runtime.machine.master_clock %
                              runtime.machine.buffer.capacity_frames);
    held_value = runtime.machine.buffer.data[write_position * 2u];
    (void)ts_sister_runtime_process_frame(&runtime, &source);
    CHECK(CLOSE(runtime.machine.buffer.data[write_position * 2u], held_value));
    CHECK(ts_sister_runtime_direct_tile_route(&runtime) > 0.0f &&
          ts_sister_runtime_direct_tile_route(&runtime) < 1.0f);
    for (int i = 1; i < 20; ++i)
        (void)ts_sister_runtime_process_frame(&runtime, &source);
    CHECK(CLOSE(ts_sister_runtime_direct_tile_route(&runtime), 0.0f));

    CHECK(ts_sister_runtime_get_snapshot(&runtime, &snapshot));
    CHECK(snapshot.enabled && snapshot.processed_frames == runtime.processed_frames);
    CHECK(snapshot.monitor_enabled && !snapshot.rolling && !snapshot.held);

    ts_sister_runtime_disable(&runtime);
    CHECK(!ts_sister_runtime_owns_direct_tile_bus(&runtime));
    CHECK(CLOSE(ts_sister_runtime_direct_tile_route(&runtime), 0.0f));
    frame = ts_sister_runtime_process_frame(&runtime, &source);
    CHECK(runtime.machine.buffer.data == NULL);
    CHECK(CLOSE(frame.tap[TS_SISTER_TAP_H1].l, 0.0f));
    CHECK(CLOSE(frame.dry_monitor_gain, 1.0f));

    {
        TsAudioMixer mixer;
        TsAudioBuses buses;
        TsStereoFrame output;
        ts_audio_mixer_init(&mixer);
        ts_audio_buses_clear(&buses);
        buses.legacy_preview = (TsStereoFrame){0.25f, -0.25f};
        output = ts_audio_mixer_render(&mixer, &buses);
        CHECK(CLOSE(output.l, 0.2f) && CLOSE(output.r, -0.2f));
    }
    ts_sister_runtime_free(&runtime);
    if (failures) return 1;
    puts("sister runtime tests passed");
    return 0;
}
