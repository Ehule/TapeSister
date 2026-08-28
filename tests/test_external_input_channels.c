#include "tapesister/capture.h"
#include "tapesister/input_monitor.h"
#include "tapesister/sample.h"

#include <math.h>
#include <stdio.h>

static int failures;
#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; \
} } while (0)
#define CLOSE(a,b) (fabsf((a) - (b)) < 0.0001f)

int main(void)
{
    const float device[] = {1.0f, -0.5f, 0.25f, 0.75f};
    const float hot_four[] = {1.0f, 1.0f, 1.0f, 1.0f};
    const float six[] = {0.9f, -0.9f, 0.6f, -0.6f, 0.3f, -0.3f};
    const float eight[] = {1.0f, -1.0f, 1.0f, -1.0f,
                           1.0f, -1.0f, 1.0f, -1.0f};
    const float interleaved[] = {
        0.8f, -0.8f, 0.4f, -0.4f,
        -0.2f, 0.2f, -0.6f, 0.6f
    };
    float sparse[TS_INPUT_DEVICE_CHANNEL_MAX] = {0.0f};
    const float one[] = {0.3f};
    const float two[] = {0.7f, -0.2f};
    TsStereoFrame frame;
    TsExternalRecorder recorder;
    TsInputMonitor monitor;
    char error[160];
    TsSample wav;

    frame = ts_input_channel_select(device, 4u, TS_INPUT_CHANNEL_MIX);
    CHECK(CLOSE(frame.l, 0.375f) && CLOSE(frame.r, 0.375f));
    CHECK(ts_input_activity_detect_frame(device, 4u) == 0x0fu);
    frame = ts_input_channel_select(hot_four, 4u, TS_INPUT_CHANNEL_MIX);
    CHECK(CLOSE(frame.l, 1.0f) && CLOSE(frame.r, 1.0f));
    frame = ts_input_channel_select(device, 4u, TS_INPUT_CHANNEL_LEFT);
    CHECK(CLOSE(frame.l, 1.0f) && CLOSE(frame.r, 1.0f));
    frame = ts_input_channel_select(device, 4u, TS_INPUT_CHANNEL_RIGHT);
    CHECK(CLOSE(frame.l, -0.5f) && CLOSE(frame.r, -0.5f));
    frame = ts_input_channel_select(device, 4u, TS_INPUT_CHANNEL_STEREO);
    CHECK(CLOSE(frame.l, 0.625f) && CLOSE(frame.r, 0.125f));
    frame = ts_input_channel_select(one, 1u, TS_INPUT_CHANNEL_RIGHT);
    CHECK(CLOSE(frame.l, 0.3f) && CLOSE(frame.r, 0.3f));
    frame = ts_input_channel_select(one, 1u, TS_INPUT_CHANNEL_MIX);
    CHECK(CLOSE(frame.l, 0.3f) && CLOSE(frame.r, 0.3f));
    frame = ts_input_channel_select(one, 1u, TS_INPUT_CHANNEL_STEREO);
    CHECK(CLOSE(frame.l, 0.3f) && CLOSE(frame.r, 0.3f));
    frame = ts_input_channel_select(two, 2u, TS_INPUT_CHANNEL_STEREO);
    CHECK(CLOSE(frame.l, 0.7f) && CLOSE(frame.r, -0.2f));
    frame = ts_input_channel_select(six, 6u, TS_INPUT_CHANNEL_STEREO);
    CHECK(CLOSE(frame.l, 0.6f) && CLOSE(frame.r, -0.6f));
    frame = ts_input_channel_select(eight, 8u, TS_INPUT_CHANNEL_STEREO);
    CHECK(CLOSE(frame.l, 1.0f) && CLOSE(frame.r, -1.0f));

    /* Activity is physical/pre-fold. Sparse channel 3 contributes to both
       sides in MIX, but only to stereo left. */
    sparse[2] = 0.8f;
    CHECK(ts_input_activity_detect_frame(sparse, 8u) == (1u << 2));
    frame = ts_input_channel_select(sparse, 8u, TS_INPUT_CHANNEL_MIX);
    CHECK(CLOSE(frame.l, 0.1f) && CLOSE(frame.r, 0.1f));
    frame = ts_input_channel_select(sparse, 8u, TS_INPUT_CHANNEL_STEREO);
    CHECK(CLOSE(frame.l, 0.2f) && CLOSE(frame.r, 0.0f));

    /* Sparse channel 6 contributes to both sides in MIX, but only to stereo
       right. Clearing channel 3 proves there is no retained/stale input. */
    sparse[2] = 0.0f;
    sparse[5] = -0.8f;
    CHECK(ts_input_activity_detect_frame(sparse, 8u) == (1u << 5));
    frame = ts_input_channel_select(sparse, 8u, TS_INPUT_CHANNEL_MIX);
    CHECK(CLOSE(frame.l, -0.1f) && CLOSE(frame.r, -0.1f));
    frame = ts_input_channel_select(sparse, 8u, TS_INPUT_CHANNEL_STEREO);
    CHECK(CLOSE(frame.l, 0.0f) && CLOSE(frame.r, -0.2f));
    sparse[5] = 0.0f;
    CHECK(ts_input_activity_detect_frame(sparse, 8u) == 0u);
    frame = ts_input_channel_select(sparse, 8u, TS_INPUT_CHANNEL_MIX);
    CHECK(CLOSE(frame.l, 0.0f) && CLOSE(frame.r, 0.0f));
    /* Each interleaved frame must use its own N-channel stride. */
    frame = ts_input_channel_select(interleaved, 4u, TS_INPUT_CHANNEL_STEREO);
    CHECK(CLOSE(frame.l, 0.6f) && CLOSE(frame.r, -0.6f));
    frame = ts_input_channel_select(interleaved + 4u, 4u,
                                    TS_INPUT_CHANNEL_STEREO);
    CHECK(CLOSE(frame.l, -0.4f) && CLOSE(frame.r, 0.4f));
    CHECK(ts_input_channel_record_channels(TS_INPUT_CHANNEL_MIX) == 1u);
    CHECK(ts_input_channel_record_channels(TS_INPUT_CHANNEL_STEREO) == 2u);

    ts_input_monitor_init(&monitor);
    ts_input_monitor_set_enabled(&monitor, 1, 48000u);
    for (int i = 0; i < TS_INPUT_MONITOR_PRIME_FRAMES; ++i)
        ts_input_monitor_push_frame(&monitor, (TsStereoFrame){0.75f, -0.25f});
    frame = ts_input_monitor_read_frame(&monitor, 48000u);
    CHECK(frame.l > 0.0f && frame.r < 0.0f);
    CHECK(CLOSE(frame.r / frame.l, -1.0f / 3.0f));

    ts_external_recorder_init(&recorder);
    CHECK(ts_external_recorder_arm_channels(&recorder, 0, 1000u, 2u,
                                            -60, 0, 100, 0, 1,
                                            error, sizeof(error)));
    CHECK(ts_external_recorder_write_frame(
              &recorder, (TsStereoFrame){0.5f, -0.25f}) == 2);
    CHECK(ts_external_recorder_write_frame(
              &recorder, (TsStereoFrame){0.5f, -0.25f}) == 0);
    CHECK(recorder.channels == 2u && recorder.recorded_frames == 2u);
    CHECK(CLOSE(recorder.buffer[0], 0.5f));
    CHECK(CLOSE(recorder.buffer[1], -0.25f));
    CHECK(ts_external_recorder_stop(&recorder, error, sizeof(error)));

    wav.data = recorder.buffer;
    wav.frames = recorder.recorded_frames;
    wav.sample_rate = recorder.sample_rate;
    wav.channels = recorder.channels;
    wav.visual_revision = 0u;
    snprintf(wav.name, sizeof(wav.name), "INPUT STEREO");
    CHECK(ts_sample_save_wav32f(&wav, "test-external-stereo.wav",
                                error, sizeof(error)));
    ts_sample_init(&wav);
    CHECK(ts_sample_load_wav(&wav, "test-external-stereo.wav",
                             error, sizeof(error)));
    CHECK(wav.channels == 2u && wav.frames == 2u);
    CHECK(CLOSE(wav.data[0], 0.5f) && CLOSE(wav.data[1], -0.25f));
    ts_sample_free(&wav);
    remove("test-external-stereo.wav");
    ts_external_recorder_free(&recorder);

    if (failures) return 1;
    puts("external input channel tests passed");
    return 0;
}
