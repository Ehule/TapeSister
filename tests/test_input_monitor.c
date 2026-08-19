#include "tapesister/input_monitor.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

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

int main(void)
{
    test_monitor_enable_meter_and_dry_ring();
    test_rate_conversion_and_bounded_waveform();
    if (failures != 0) {
        fprintf(stderr, "%d input monitor test(s) failed\n", failures);
        return 1;
    }
    puts("input monitor and bounded live waveform tests passed");
    return 0;
}
