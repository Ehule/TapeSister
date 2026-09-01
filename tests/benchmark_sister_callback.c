#include "sister_test_helpers.h"
#include "tapesister/realtime_diagnostics.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static uint64_t monotonicish_ns(void)
{
    struct timespec value;
    if (timespec_get(&value, TIME_UTC) != TIME_UTC) return 0u;
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) +
           (uint64_t)value.tv_nsec;
}

int main(int argc, char **argv)
{
    uint64_t total_frames = UINT64_C(2000000);
    uint32_t callback_frames = 256u;
    TsSisterRuntime runtime;
    TsSisterParameters p;
    TsSisterSourceFrames source = {0};
    TsRealtimeDiagnostics diagnostics;
    TsRealtimeDiagnosticsSnapshot snapshot;
    uint32_t random = UINT32_C(0x6d2b79f5);
    uint64_t completed = 0u;
    double checksum = 0.0;
    char error[160];
    if (argc > 1) total_frames = strtoull(argv[1], NULL, 10);
    if (argc > 2) callback_frames = (uint32_t)strtoul(argv[2], NULL, 10);
    if (total_frames == 0u || callback_frames == 0u) return 2;
    total_frames = total_frames / callback_frames * callback_frames;
    if (total_frames == 0u) return 2;
    ts_sister_runtime_init(&runtime);
    /* Establish routes before POWER so base/current compare the same settled
       topology instead of including PR11's deliberate live handoff ramp. */
    ts_sister_runtime_set_sources(&runtime, TS_SISTER_SOURCE_FM |
                                            TS_SISTER_SOURCE_EXT |
                                            TS_SISTER_SOURCE_PREVIEW);
    assert(ts_sister_runtime_enable(&runtime, 48000u, 2u, 2u, 60.0,
                                    error, sizeof(error)));
    p = runtime.parameters;
    p.head1_level = p.head2_level = p.head3_level = 0.7f;
    p.head1_feedback = p.head2_feedback = 0.75f;
    p.soak = 0.7f;
    p.soak_targets = TS_SISTER_EFFECT_TARGET_H1 |
                     TS_SISTER_EFFECT_TARGET_H2 |
                     TS_SISTER_EFFECT_TARGET_H3;
    for (size_t slot = 0u; slot < 3u; ++slot) {
        p.fx.slot[slot] = (TsSisterFxSlotControls){
            TS_SISTER_FX_GRAIN, 1, TS_SISTER_FX_PLACE_HEADS,
            6.0f, 1.0f, 1.0f, 0.25f + 0.25f * (float)slot, 0.80f
        };
    }
    p.fx.slot[3] = (TsSisterFxSlotControls){
        TS_SISTER_FX_REVERB, 1, TS_SISTER_FX_PLACE_HEADS,
        6.0f, 1.0f, 1.0f, 0.5f, 0.80f
    };
    p.fx.master_feedback = 0.7f;
    p.fx.fallout.enabled = 1;
    p.fx.fallout.mix = 0.75f;
    p.fx.fallout.feedback = 0.70f;
    p.fx.fallout.noise = 0.50f;
    p.fx.fallout.drop_enabled = 1;
    p.fx.fallout.drop_rate = 0.0f;
    p.fx.fallout.pan_enabled = 1;
    p.fx.fallout.pan_rate = 0.0f;
    p.fx.fallout.skip_enabled = 1;
    p.fx.fallout.skip_span = 0.50f;
    p.fx.fallout.skip_rate = 0.0f;
    p.fx.fallout.bit_enabled = 1;
    p.fx.fallout.bit_quality = 0.50f;
    p.fx.fallout.bit_resolution = 0.50f;
    p.fx.fallout.bit_rate = 0.0f;
    p.fx.fallout.pitch_enabled = 1;
    p.fx.fallout.pitch = 0.50f;
    p.fx.fallout.pitch_ramp = 0.0f;
    p.fx.fallout.pitch_rate = 0.0f;
    p.fx.fallout.lfo_rate = 1.0f;
    p.fx.fallout.lfo_intensity = 1.0f;
    p.fx.fallout.lfo_targets = TS_SISTER_FALLOUT_LFO_ALL;
    p.fx.fallout.rise_mode = TS_SISTER_FALLOUT_RISE_SAW;
    p.fx.fallout.rise_length = 0.0f;
    p.fx.fallout.rise_intensity = 1.0f;
    p.fx.fallout.rise_targets = TS_SISTER_FALLOUT_LFO_ALL;
    ts_sister_runtime_set_parameters(&runtime, &p);
    ts_realtime_diagnostics_init(&diagnostics);
    while (completed < total_frames) {
        uint32_t frames = callback_frames;
        uint64_t started;
        uint64_t elapsed;
        if ((uint64_t)frames > total_frames - completed)
            frames = (uint32_t)(total_frames - completed);
        started = monotonicish_ns();
        ts_sister_runtime_begin_audio_block(&runtime);
        for (uint32_t i = 0u; i < frames; ++i) {
            TsSisterRuntimeFrame frame;
            random ^= random << 13;
            random ^= random >> 17;
            random ^= random << 5;
            source.external.l = (float)(int32_t)random /
                                (float)INT32_MAX * 0.12f;
            source.external.r = -source.external.l * 0.61f;
            source.fm = (TsStereoFrame){source.external.r * 0.7f,
                                         source.external.l * 0.5f};
            source.preview = (TsStereoFrame){0.03f, -0.02f};
            frame = ts_sister_runtime_process_frame(&runtime, &source);
            checksum += frame.tap[TS_SISTER_TAP_MIX].l * 0.61803398875 +
                        frame.tap[TS_SISTER_TAP_MIX].r * 0.38196601125;
        }
        ts_sister_runtime_end_audio_block(&runtime);
        elapsed = monotonicish_ns() - started;
        ts_realtime_diagnostics_record(
            &diagnostics, elapsed, UINT64_C(1000000000), 48000u, frames,
            TS_RT_CONFIG_SISTER | TS_RT_CONFIG_FM | TS_RT_CONFIG_EXT |
            TS_RT_CONFIG_PREVIEW | TS_RT_CONFIG_H1 | TS_RT_CONFIG_H2 |
            TS_RT_CONFIG_H3 | TS_RT_CONFIG_SOAK | TS_RT_CONFIG_REVERB |
            TS_RT_CONFIG_DELAY | TS_RT_CONFIG_DISTORTION |
            TS_RT_CONFIG_FX_FEEDBACK | TS_RT_CONFIG_FALLOUT);
        completed += frames;
    }
    assert(ts_realtime_diagnostics_get(&diagnostics, &snapshot));
    printf("callback-benchmark frames=%" PRIu64 " callbacks=%" PRIu64
           " block=%u avg_us=%.3f worst_us=%.3f deadline_us=%.3f "
           "near=%" PRIu64 " overruns=%" PRIu64
           " ns_per_frame=%.3f checksum=%.12f config=0x%04x\n",
           snapshot.frame_count, snapshot.callback_count, callback_frames,
           snapshot.average_microseconds, snapshot.worst_microseconds,
           snapshot.deadline_microseconds, snapshot.near_overruns,
           snapshot.deadline_overruns,
           snapshot.frame_count > 0u ?
               (double)snapshot.elapsed_ticks / snapshot.frame_count : 0.0,
           checksum, snapshot.active_configuration);
    ts_sister_runtime_free(&runtime);
    return 0;
}
