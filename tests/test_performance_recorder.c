#include "tapesister/performance_recorder.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static uint32_t read_u32(const unsigned char *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8u |
           (uint32_t)bytes[2] << 16u | (uint32_t)bytes[3] << 24u;
}

static float read_f32(const unsigned char *bytes)
{
    uint32_t bits = read_u32(bytes);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

static void finish(TsPerformanceRecorder *recorder)
{
    assert(ts_performance_recorder_request_stop(recorder));
    while (ts_performance_recorder_pump(recorder, 2u)) {}
    assert(ts_performance_recorder_state(recorder) ==
           TS_PERFORMANCE_FILE_COMPLETED);
}

static void standard_stereo_wav(void)
{
    static const char path[] = "test-performance-stereo.wav";
    TsPerformanceRecorder recorder;
    unsigned char bytes[112];
    char error[160];
    FILE *file;
    ts_performance_recorder_init(&recorder);
    assert(ts_performance_recorder_start(
        &recorder, path, 48000u, 2u, 8u, error, sizeof(error)));
    assert(ts_performance_recorder_push_frame(
        &recorder, (TsStereoFrame){0.25f, -0.5f}));
    assert(ts_performance_recorder_push_frame(
        &recorder, (TsStereoFrame){0.75f, -1.0f}));
    assert(ts_performance_recorder_pump(&recorder, 1u));
    assert(ts_performance_recorder_push_frame(
        &recorder, (TsStereoFrame){1.25f, -1.25f}));
    assert(ts_performance_recorder_frames(&recorder) == 3u);
    finish(&recorder);
    file = fopen(path, "rb");
    assert(file != NULL);
    assert(fread(bytes, 1u, sizeof(bytes), file) == 104u);
    assert(fclose(file) == 0);
    assert(memcmp(bytes, "RIFF", 4u) == 0);
    assert(read_u32(bytes + 4u) == 96u);
    assert(memcmp(bytes + 12u, "JUNK", 4u) == 0);
    assert(memcmp(bytes + 48u, "fmt ", 4u) == 0);
    assert(bytes[56] == 3u && bytes[58] == 2u);
    assert(memcmp(bytes + 72u, "data", 4u) == 0);
    assert(read_u32(bytes + 76u) == 24u);
    assert(fabsf(read_f32(bytes + 80u) - 0.25f) < 0.000001f);
    assert(fabsf(read_f32(bytes + 84u) + 0.5f) < 0.000001f);
    assert(fabsf(read_f32(bytes + 96u) - 1.25f) < 0.000001f);
    assert(fabsf(read_f32(bytes + 100u) + 1.25f) < 0.000001f);
    ts_performance_recorder_free(&recorder);
    remove(path);
}

static void bounded_queue_never_blocks(void)
{
    static const char path[] = "test-performance-overflow.wav";
    TsPerformanceRecorder recorder;
    char error[160];
    ts_performance_recorder_init(&recorder);
    assert(ts_performance_recorder_start(
        &recorder, path, 48000u, 2u, 2u, error, sizeof(error)));
    assert(ts_performance_recorder_push_frame(
        &recorder, (TsStereoFrame){0.1f, 0.2f}));
    assert(ts_performance_recorder_push_frame(
        &recorder, (TsStereoFrame){0.3f, 0.4f}));
    assert(!ts_performance_recorder_push_frame(
        &recorder, (TsStereoFrame){0.5f, 0.6f}));
    assert(ts_performance_recorder_dropped(&recorder) == 1u);
    finish(&recorder);
    assert(ts_performance_recorder_frames(&recorder) == 2u);
    ts_performance_recorder_free(&recorder);
    remove(path);
}

static void mono_is_linked_sum(void)
{
    static const char path[] = "test-performance-mono.wav";
    TsPerformanceRecorder recorder;
    unsigned char bytes[84];
    char error[160];
    FILE *file;
    ts_performance_recorder_init(&recorder);
    assert(ts_performance_recorder_start(
        &recorder, path, 44100u, 1u, 4u, error, sizeof(error)));
    assert(ts_performance_recorder_push_frame(
        &recorder, (TsStereoFrame){0.75f, -0.25f}));
    finish(&recorder);
    file = fopen(path, "rb");
    assert(file != NULL);
    assert(fread(bytes, 1u, sizeof(bytes), file) == sizeof(bytes));
    assert(fclose(file) == 0);
    assert(bytes[58] == 1u && read_u32(bytes + 76u) == 4u);
    assert(fabsf(read_f32(bytes + 80u) - 0.25f) < 0.000001f);
    ts_performance_recorder_free(&recorder);
    remove(path);
}

static void rf64_boundary_is_unbounded(void)
{
    uint64_t last_riff_stereo = (UINT32_MAX - 72u) / 8u;
    uint64_t last_riff_mono = (UINT32_MAX - 72u) / 4u;
    assert(!ts_performance_recorder_uses_rf64(last_riff_stereo, 2u));
    assert(ts_performance_recorder_uses_rf64(last_riff_stereo + 1u, 2u));
    assert(!ts_performance_recorder_uses_rf64(last_riff_mono, 1u));
    assert(ts_performance_recorder_uses_rf64(last_riff_mono + 1u, 1u));
}

static void million_frame_streaming_soak(void)
{
    static const char path[] = "test-performance-million.wav";
    TsPerformanceRecorder recorder;
    char error[160];
    ts_performance_recorder_init(&recorder);
    assert(ts_performance_recorder_start(
        &recorder, path, 48000u, 2u, 4096u, error, sizeof(error)));
    for (uint64_t frame = 0u; frame < UINT64_C(1000000); ++frame) {
        float value = (float)((int)(frame % 2001u) - 1000) / 1000.0f;
        assert(ts_performance_recorder_push_frame(
            &recorder, (TsStereoFrame){value, -value}));
        if ((frame & 255u) == 255u)
            assert(ts_performance_recorder_pump(&recorder, 256u));
    }
    assert(ts_performance_recorder_dropped(&recorder) == 0u);
    finish(&recorder);
    assert(ts_performance_recorder_frames(&recorder) == UINT64_C(1000000));
    ts_performance_recorder_free(&recorder);
    remove(path);
}

int main(void)
{
    standard_stereo_wav();
    bounded_queue_never_blocks();
    mono_is_linked_sum();
    rf64_boundary_is_unbounded();
    million_frame_streaming_soak();
    puts("Performance file recorder tests passed");
    return 0;
}
