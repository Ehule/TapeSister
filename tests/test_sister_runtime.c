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
    frame = (TsSisterRuntimeFrame){0};
    frame.tap[TS_SISTER_TAP_H1] = (TsStereoFrame){0.125f, -0.25f};
    {
        TsStereoFrame final_output = {0.75f, -0.50f};
        TsStereoFrame captured = ts_sister_runtime_file_capture_frame(
            &frame, TS_SISTER_TAP_MIX, final_output);
        CHECK(CLOSE(captured.l, final_output.l));
        CHECK(CLOSE(captured.r, final_output.r));
        captured = ts_sister_runtime_file_capture_frame(
            &frame, TS_SISTER_TAP_H1, final_output);
        CHECK(CLOSE(captured.l, frame.tap[TS_SISTER_TAP_H1].l));
        CHECK(CLOSE(captured.r, frame.tap[TS_SISTER_TAP_H1].r));
    }
    CHECK(!runtime.enabled && runtime.machine.buffer.data == NULL);
    CHECK(!ts_sister_runtime_enable(&runtime, 0u, 2u, 2u, 0.1,
                                    NULL, 0u));
    CHECK(ts_sister_runtime_enable(&runtime, 1000u, 2u, 2u, 0.1,
                                   NULL, 0u));
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

    /* The separate mouse-launch tile bus follows the TILES insert without
       becoming a Sister keyboard-performance voice. */
    ts_sister_runtime_set_sources(&runtime, TS_SISTER_SOURCE_TILES);
    source.preview = (TsStereoFrame){0.0f, 0.0f};
    source.tiles = (TsStereoFrame){0.4f, -0.2f};
    for (int i = 0; i < 20; ++i)
        frame = ts_sister_runtime_process_frame(&runtime, &source);
    CHECK(frame.input.l > 0.1f && frame.input.r < -0.05f);
    ts_sister_runtime_set_sources(&runtime, TS_SISTER_SOURCE_PREVIEW);
    source.tiles = (TsStereoFrame){0.0f, 0.0f};
    source.preview = (TsStereoFrame){0.5f, -0.25f};
    for (int i = 0; i < 20; ++i)
        frame = ts_sister_runtime_process_frame(&runtime, &source);

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
    (void)ts_sister_runtime_process_output(&runtime, frame.monitor_return);
    (void)ts_sister_runtime_process_output(&runtime, frame.monitor_return);
    CHECK(ts_sister_runtime_get_snapshot(&runtime, &snapshot));
    CHECK(snapshot.output_level[0] > 0.0f &&
          snapshot.output_level[1] > 0.0f);
    CHECK(snapshot.output_peak_hold[0] >= snapshot.output_level[0] &&
          snapshot.output_peak_hold[1] >= snapshot.output_level[1]);
    source.preview = (TsStereoFrame){4.0f, -4.0f};
    (void)ts_sister_runtime_process_frame(&runtime, &source);
    (void)ts_sister_runtime_process_output(
        &runtime, (TsStereoFrame){4.0f, -4.0f});
    CHECK(ts_sister_runtime_get_snapshot(&runtime, &snapshot));
    CHECK(snapshot.output_clip[0] && snapshot.output_clip[1]);
    source.preview = (TsStereoFrame){0.5f, -0.25f};

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

    runtime.fallout.lfo_phase = 0.25;
    runtime.fallout.rise_value = 0.50f;
    runtime.fallout.rise_one_shot_complete = 1;
    (void)ts_sister_runtime_process_frame(&runtime, &source);

    CHECK(ts_sister_runtime_get_snapshot(&runtime, &snapshot));
    CHECK(snapshot.enabled && snapshot.processed_frames == runtime.processed_frames);
    CHECK(snapshot.monitor_enabled && !snapshot.rolling && !snapshot.held);
    CHECK(CLOSE(snapshot.fallout_lfo_phase, 0.25f));
    CHECK(CLOSE(snapshot.fallout_rise_phase, 0.50f));
    CHECK(snapshot.fallout_rise_complete);

    /* The UI snapshot carries the identity and direction of the exact ramps
       selected by the shared progress displays. */
    parameters = runtime.parameters;
    parameters.fx.transition = ts_sister_fx_transition_normalized(1000.0f);
    parameters.fx.slot[0].enabled = 0;
    parameters.fx.fallout.component_transition =
        ts_sister_fallout_transition_normalized(1000.0f);
    parameters.fx.fallout.master_transition =
        ts_sister_fallout_transition_normalized(1000.0f);
    parameters.fx.fallout.enabled = 1;
    parameters.fx.fallout.mix = 0.0f;
    ts_sister_runtime_set_parameters(&runtime, &parameters);
    (void)ts_sister_runtime_process_frame(&runtime, &source);
    CHECK(ts_sister_runtime_get_snapshot(&runtime, &snapshot));
    CHECK(snapshot.fx_transition_active &&
          snapshot.fx_transition_source ==
              TS_SISTER_FX_TRANSITION_SLOT_1 &&
          !snapshot.fx_transition_target_enabled);
    CHECK(snapshot.fallout_master_transition_active &&
          snapshot.fallout_master_transition_target_enabled);

    {
        uint64_t published_revision = snapshot.revision;
        uint64_t published_frames = snapshot.processed_frames;
        ts_sister_runtime_begin_audio_block(&runtime);
        for (int i = 0; i < 64; ++i)
            (void)ts_sister_runtime_process_frame(&runtime, &source);
        CHECK(ts_sister_runtime_get_snapshot(&runtime, &snapshot));
        CHECK(snapshot.revision == published_revision);
        CHECK(snapshot.processed_frames == published_frames);
        ts_sister_runtime_end_audio_block(&runtime);
        CHECK(ts_sister_runtime_get_snapshot(&runtime, &snapshot));
        CHECK(snapshot.revision == published_revision + 2u);
        CHECK(snapshot.processed_frames == published_frames + 64u);
    }

    {
        TsSisterFalloutControls target;
        parameters = runtime.parameters;
        parameters.fx.fallout.enabled = 1;
        parameters.fx.fallout.transition =
            ts_sister_fallout_transition_normalized(10.0f);
        parameters.fx.fallout.mix = 0.8f;
        ts_sister_runtime_set_parameters(&runtime, &parameters);
        for (int frame_index = 0; frame_index < 10; ++frame_index)
            (void)ts_sister_runtime_process_frame(&runtime, &source);
        target = runtime.parameters.fx.fallout;
        target.enabled = 0;
        target.mix = 0.2f;
        target.noise_type = TS_SISTER_FALLOUT_NOISE_BROWN;
        ts_sister_runtime_recall_fallout_preset(&runtime, &target);
        CHECK(runtime.parameters.fx.fallout.enabled == 1);
        CHECK(CLOSE(runtime.parameters.fx.fallout.mix, 0.2f));
        CHECK(CLOSE(runtime.fallout.controls.mix, 0.8f));
        for (int frame_index = 0; frame_index < 5; ++frame_index)
            (void)ts_sister_runtime_process_frame(&runtime, &source);
        CHECK(CLOSE(runtime.fallout.controls.mix, 0.2f));
        CHECK(runtime.fallout.controls.enabled == 1);
    }

    ts_sister_runtime_disable(&runtime);
    CHECK(!ts_sister_runtime_owns_direct_tile_bus(&runtime));
    CHECK(CLOSE(ts_sister_runtime_direct_tile_route(&runtime), 0.0f));
    frame = ts_sister_runtime_process_frame(&runtime, &source);
    CHECK(runtime.machine.buffer.data == NULL);
    CHECK(CLOSE(frame.tap[TS_SISTER_TAP_H1].l, 0.0f));
    CHECK(CLOSE(frame.dry_monitor_gain, 1.0f));

    /* A saved OFF state applied while audio is stopped must be the first live
       state after restart, not a one-hour fade from an internal ON default. */
    parameters = runtime.parameters;
    parameters.fx.enabled = 0;
    parameters.fx.master_transition =
        ts_sister_fx_transition_normalized(3600000.0f);
    parameters.fx.fallout.enabled = 0;
    parameters.fx.fallout.master_transition =
        ts_sister_fallout_transition_normalized(3600000.0f);
    ts_sister_runtime_set_parameters(&runtime, &parameters);
    CHECK(ts_sister_runtime_enable(&runtime, 1000u, 2u, 2u, 0.1,
                                   NULL, 0u));
    CHECK(runtime.post_fx.controls.enabled == 0);
    CHECK(ts_sister_post_fx_master_engage(&runtime.post_fx) == 0.0f);
    CHECK(runtime.post_fx.master_engage.remaining == 0u);
    CHECK(runtime.fallout.controls.enabled == 0);
    CHECK(ts_sister_fallout_engage(&runtime.fallout) == 0.0f);
    CHECK(runtime.fallout.engage_remaining == 0u);
    ts_sister_runtime_disable(&runtime);

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
