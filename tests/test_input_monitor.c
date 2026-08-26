#include "tapesister/input_monitor.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>

static int failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

static void test_monitor_enable_meter_and_dry_ring(void)
{
    TsInputMonitor monitor;
    float heard = 0.0f;
    ts_input_monitor_init(&monitor);
    ts_input_monitor_publish_level(&monitor, 0.2f);
    CHECK(ts_input_monitor_level(&monitor) > 0.19f);
    ts_input_monitor_push(&monitor, 0.75f);
    CHECK(ts_input_monitor_read(&monitor, 48000) == 0.0f);
    ts_input_monitor_set_enabled(&monitor, 1, 48000);
    for (int frame = 0; frame < 256; ++frame)
        ts_input_monitor_push(&monitor, frame == 0 ? 0.5f : 0.25f);
    for (int frame = 0; frame < 64; ++frame)
        heard += fabsf(ts_input_monitor_read(&monitor, 48000));
    CHECK(heard > 10.0f);
    ts_input_monitor_publish_level(&monitor, 0.5f);
    ts_input_monitor_publish_level(&monitor, 1.05f);
    CHECK(ts_input_monitor_level(&monitor) > 1.04f);
    CHECK(ts_input_monitor_take_peak(&monitor) > 1.04f);
    CHECK(ts_input_monitor_take_peak(&monitor) == 0.0f);
    CHECK(ts_input_monitor_take_clip(&monitor));
    CHECK(!ts_input_monitor_take_clip(&monitor));
    ts_input_monitor_set_enabled(&monitor, 0, 48000);
    CHECK(!ts_input_monitor_enabled(&monitor));
    CHECK(ts_input_monitor_read(&monitor, 48000) == 0.0f);
}

static void test_rate_conversion_and_bounded_waveform(void)
{
    TsInputMonitor monitor;
    TsLiveWaveform waveform;
    float minimum[TS_LIVE_WAVEFORM_COLUMNS];
    float maximum[TS_LIVE_WAVEFORM_COLUMNS];
    float *block = (float *)malloc(48000u * sizeof(*block));
    size_t columns;
    CHECK(block != NULL);
    if (block == NULL) return;
    for (size_t frame = 0; frame < 48000u; ++frame)
        block[frame] = frame & 1u ? 0.8f : -0.6f;
    ts_input_monitor_init(&monitor);
    ts_input_monitor_set_enabled(&monitor, 1, 44100);
    for (int frame = 0; frame < 512; ++frame)
        ts_input_monitor_push(&monitor, 0.3f);
    for (int frame = 0; frame < 256; ++frame)
        CHECK(isfinite(ts_input_monitor_read(&monitor, 48000)));

    ts_live_waveform_init(&waveform, 48000);
    for (int second = 0; second < 20; ++second)
        ts_live_waveform_push(&waveform, block, 48000u);
    columns = ts_live_waveform_snapshot(&waveform, minimum, maximum,
                                        TS_LIVE_WAVEFORM_COLUMNS);
    CHECK(columns == TS_LIVE_WAVEFORM_COLUMNS);
    CHECK(waveform.total_frames == 960000u);
    CHECK(minimum[0] < -0.59f && maximum[0] > 0.79f);
    free(block);
}

static void test_elastic_ring_diagnostics_and_recovery(void)
{
    TsInputMonitor monitor;
    TsInputMonitorDiagnostics diagnostics;
    TsStereoFrame previous = {0.0f, 0.0f};
    int saw_underrun = 0;

    CHECK(ts_input_monitor_recommended_prime_frames(0u) ==
          TS_INPUT_MONITOR_PRIME_FRAMES);
    CHECK(ts_input_monitor_recommended_prime_frames(256u) == 512u);
    CHECK(ts_input_monitor_recommended_prime_frames(512u) == 1024u);
    CHECK(ts_input_monitor_recommended_prime_frames(1024u) == 2048u);

    ts_input_monitor_init(&monitor);
    CHECK(atomic_is_lock_free(&monitor.underrun_count));
    CHECK(atomic_is_lock_free(&monitor.dropped_frame_count));
    ts_input_monitor_set_prime_frames(&monitor, 512u);
    ts_input_monitor_set_enabled(&monitor, 1, 48000u);
    for (int frame = 0; frame < 511; ++frame)
        ts_input_monitor_push_frame(
            &monitor, (TsStereoFrame){0.5f, -0.25f});
    CHECK(ts_input_monitor_read_frame(&monitor, 48000u).l == 0.0f);
    ts_input_monitor_push_frame(&monitor, (TsStereoFrame){0.5f, -0.25f});
    previous = ts_input_monitor_read_frame(&monitor, 48000u);
    CHECK(previous.l > 0.0f && previous.r < 0.0f);

    for (int frame = 0; frame < 1024; ++frame) {
        TsStereoFrame output = ts_input_monitor_read_frame(&monitor, 48000u);
        CHECK(isfinite(output.l) && isfinite(output.r));
        CHECK(fabsf(output.l - previous.l) <= 0.51f);
        previous = output;
        CHECK(ts_input_monitor_get_diagnostics(&monitor, &diagnostics));
        if (diagnostics.underrun_count != 0u) {
            saw_underrun = 1;
            break;
        }
    }
    CHECK(saw_underrun);
    for (int frame = 0; frame < TS_INPUT_MONITOR_FADE_FRAMES + 2; ++frame)
        previous = ts_input_monitor_read_frame(&monitor, 48000u);
    CHECK(fabsf(previous.l) < 0.0001f && fabsf(previous.r) < 0.0001f);

    for (int frame = 0; frame < 512; ++frame)
        ts_input_monitor_push_frame(
            &monitor, (TsStereoFrame){-0.4f, 0.2f});
    for (int frame = 0; frame < TS_INPUT_MONITOR_FADE_FRAMES; ++frame) {
        TsStereoFrame output = ts_input_monitor_read_frame(&monitor, 48000u);
        CHECK(isfinite(output.l) && isfinite(output.r));
        previous = output;
    }
    CHECK(previous.l < -0.35f && previous.r > 0.17f);

    ts_input_monitor_set_enabled(&monitor, 0, 48000u);
    ts_input_monitor_set_enabled(&monitor, 1, 48000u);
    for (uint32_t frame = 0u;
         frame < TS_INPUT_MONITOR_RING_FRAMES + 7u; ++frame)
        ts_input_monitor_push_frame(
            &monitor, (TsStereoFrame){0.1f, -0.1f});
    CHECK(ts_input_monitor_get_diagnostics(&monitor, &diagnostics));
    CHECK(diagnostics.occupancy_frames == TS_INPUT_MONITOR_RING_FRAMES);
    CHECK(diagnostics.dropped_frame_count == 7u);
    CHECK(diagnostics.prime_frames == 512u);
}

static void test_elastic_ring_tracks_small_clock_drift(void)
{
    TsInputMonitor monitor;
    TsInputMonitorDiagnostics diagnostics;
    uint32_t produced = 0u;

    ts_input_monitor_init(&monitor);
    ts_input_monitor_set_prime_frames(&monitor, 512u);
    ts_input_monitor_set_enabled(&monitor, 1, 48000u);
    for (uint32_t frame = 0u; frame < 1024u; ++frame) {
        ts_input_monitor_push_frame(
            &monitor, (TsStereoFrame){0.2f, -0.1f});
        ++produced;
    }
    /* Simulate a capture clock running slightly faster than output. The
       occasional extra frame must be absorbed without filling the ring. */
    for (uint32_t block = 0u; block < 4000u; ++block) {
        uint32_t input_frames = 256u + (block % 80u == 0u ? 1u : 0u);
        for (uint32_t frame = 0u; frame < input_frames; ++frame) {
            ts_input_monitor_push_frame(
                &monitor, (TsStereoFrame){0.2f, -0.1f});
            ++produced;
        }
        for (uint32_t frame = 0u; frame < 256u; ++frame) {
            TsStereoFrame output =
                ts_input_monitor_read_frame(&monitor, 48000u);
            CHECK(isfinite(output.l) && isfinite(output.r));
        }
    }
    CHECK(produced > 1000000u);
    CHECK(ts_input_monitor_get_diagnostics(&monitor, &diagnostics));
    CHECK(diagnostics.underrun_count == 0u);
    CHECK(diagnostics.dropped_frame_count == 0u);
    CHECK(diagnostics.occupancy_frames > 128u);
    CHECK(diagnostics.occupancy_frames < 1024u);

    ts_input_monitor_set_enabled(&monitor, 0, 48000u);
    ts_input_monitor_set_enabled(&monitor, 1, 48000u);
    for (uint32_t frame = 0u; frame < 1024u; ++frame)
        ts_input_monitor_push_frame(
            &monitor, (TsStereoFrame){-0.2f, 0.1f});
    /* Repeat with the capture clock slightly slower. */
    for (uint32_t block = 0u; block < 4000u; ++block) {
        uint32_t input_frames = 256u - (block % 80u == 0u ? 1u : 0u);
        for (uint32_t frame = 0u; frame < input_frames; ++frame)
            ts_input_monitor_push_frame(
                &monitor, (TsStereoFrame){-0.2f, 0.1f});
        for (uint32_t frame = 0u; frame < 256u; ++frame) {
            TsStereoFrame output =
                ts_input_monitor_read_frame(&monitor, 48000u);
            CHECK(isfinite(output.l) && isfinite(output.r));
        }
    }
    CHECK(ts_input_monitor_get_diagnostics(&monitor, &diagnostics));
    CHECK(diagnostics.underrun_count == 0u);
    CHECK(diagnostics.dropped_frame_count == 0u);
    CHECK(diagnostics.occupancy_frames > 128u);
    CHECK(diagnostics.occupancy_frames < 1024u);
}

static void test_multichannel_activity_state(void)
{
    TsInputActivity activity;
    float frame[TS_INPUT_DEVICE_CHANNEL_MAX] = {0.0f};
    uint32_t mask = 0u;

    ts_input_activity_init(&activity);
    CHECK(atomic_is_lock_free(&activity.available_channels));
    CHECK(atomic_is_lock_free(&activity.pending_activity_mask));
    CHECK(ts_input_activity_available(&activity) == 0u);
    CHECK(ts_input_activity_detect_frame(frame, 8u) == 0u);

    for (size_t channel = 0u; channel < TS_INPUT_DEVICE_CHANNEL_MAX;
         ++channel) {
        frame[channel] = channel & 1u ? -0.01f : 0.01f;
        mask |= UINT32_C(1) << channel;
    }
    CHECK(ts_input_activity_detect_frame(frame, 8u) == mask);
    frame[3] = TS_INPUT_ACTIVITY_THRESHOLD * 0.5f;
    CHECK((ts_input_activity_detect_frame(frame, 8u) & (1u << 3)) == 0u);
    frame[3] = NAN;
    CHECK((ts_input_activity_detect_frame(frame, 8u) & (1u << 3)) == 0u);

    ts_input_activity_set_available(&activity, 4u);
    ts_input_activity_publish(&activity, mask);
    CHECK(ts_input_activity_take(&activity) == 0x0fu);
    CHECK(ts_input_activity_take(&activity) == 0u);
    ts_input_activity_publish(&activity, 1u);
    ts_input_activity_publish(&activity, 4u);
    CHECK(ts_input_activity_take(&activity) == 5u);
    ts_input_activity_publish(&activity, 1u);
    ts_input_activity_set_available(&activity, 6u);
    CHECK(ts_input_activity_available(&activity) == 6u);
    CHECK(ts_input_activity_take(&activity) == 0u);
    ts_input_activity_set_available(&activity, 8u);
    CHECK(ts_input_activity_available(&activity) == 8u);
    ts_input_activity_publish(&activity, 0xffu);
    CHECK(ts_input_activity_take(&activity) == 0xffu);
    /* The published bits are physical channels, independent of downstream
       MIX/STEREO selection. */
    ts_input_activity_publish(&activity, 1u << 2);
    CHECK(ts_input_activity_take(&activity) == (1u << 2));
    ts_input_activity_publish(&activity, 1u << 5);
    CHECK(ts_input_activity_take(&activity) == (1u << 5));
    ts_input_activity_set_available(&activity, 0u);
    CHECK(ts_input_activity_take(&activity) == 0u);
    ts_input_activity_set_available(&activity, 9u);
    CHECK(ts_input_activity_available(&activity) == 0u);
}

static void test_capture_channel_probe_policy(void)
{
    CHECK(ts_input_capture_probe_request(8u, 0u) == 8u);
    CHECK(ts_input_capture_probe_request(8u, 2u) == 6u);
    CHECK(ts_input_capture_probe_request(8u, 7u) == 1u);
    CHECK(ts_input_capture_probe_request(8u, 8u) == 0u);
    CHECK(ts_input_capture_probe_request(6u, 0u) == 6u);
    CHECK(ts_input_capture_probe_request(6u, 5u) == 1u);
    CHECK(ts_input_capture_probe_request(6u, 6u) == 0u);
    CHECK(ts_input_capture_probe_accepts(8u, 2u) == 0);
    CHECK(ts_input_capture_probe_accepts(7u, 6u) == 0);
    CHECK(ts_input_capture_probe_accepts(6u, 6u) == 1);
    CHECK(ts_input_capture_probe_accepts(2u, 2u) == 1);
    CHECK(ts_input_capture_probe_accepts(0u, 0u) == 0);
    CHECK(ts_input_capture_probe_accepts(9u, 9u) == 0);
}

int main(void)
{
    test_monitor_enable_meter_and_dry_ring();
    test_rate_conversion_and_bounded_waveform();
    test_elastic_ring_diagnostics_and_recovery();
    test_elastic_ring_tracks_small_clock_drift();
    test_multichannel_activity_state();
    test_capture_channel_probe_policy();
    if (failures != 0) {
        fprintf(stderr, "%d input monitor test(s) failed\n", failures);
        return 1;
    }
    puts("input monitor and bounded live waveform tests passed");
    return 0;
}
