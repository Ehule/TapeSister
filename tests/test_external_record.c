#include "tapesister/capture.h"
#include "tapesister/config.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        ++failures; \
    } \
} while (0)

static void test_threshold_preroll_and_autostop(void)
{
    TsExternalRecorder recorder;
    char error[160];
    int result = 0;
    ts_external_recorder_init(&recorder);
    CHECK(ts_external_recorder_arm(&recorder, 0, 1000, -20,
                                   3, 4, 2, 2,
                                   error, sizeof(error)));
    CHECK(recorder.state == TS_EXTERNAL_CAPTURE_ARMED);
    CHECK(recorder.pre_roll_capacity == 3u);
    CHECK(ts_external_recorder_write_sample(&recorder, 0.01f) == 0);
    CHECK(ts_external_recorder_write_sample(&recorder, 0.02f) == 0);
    CHECK(ts_external_recorder_write_sample(&recorder, 0.03f) == 0);
    result = ts_external_recorder_write_sample(&recorder, 0.2f);
    CHECK(result == 2);
    CHECK(recorder.state == TS_EXTERNAL_CAPTURE_RECORDING);
    CHECK(recorder.recorded_frames == 3u);
    CHECK(fabsf(recorder.buffer[0] - 0.02f) < 0.0001f);
    CHECK(fabsf(recorder.buffer[1] - 0.03f) < 0.0001f);
    CHECK(fabsf(recorder.buffer[2] - 0.2f) < 0.0001f);
    CHECK(ts_external_recorder_write_sample(&recorder, 0.2f) == 0);
    for (int i = 0; i < 5; ++i)
        CHECK(ts_external_recorder_write_sample(&recorder, 0.0f) == 0);
    CHECK(ts_external_recorder_write_sample(&recorder, 0.0f) == 1);
    CHECK(recorder.state == TS_EXTERNAL_CAPTURE_COMPLETED);
    CHECK(recorder.stopped_early);
    ts_external_recorder_free(&recorder);
}

static void test_manual_stop_cancel_and_chain(void)
{
    TsExternalRecorder recorder;
    char error[160];
    ts_external_recorder_init(&recorder);
    CHECK(ts_external_recorder_arm(&recorder, 5, 48000, -30,
                                   180, 650, 180, 20,
                                   error, sizeof(error)));
    CHECK(ts_external_recorder_cancel(&recorder));
    CHECK(recorder.state == TS_EXTERNAL_CAPTURE_CANCELED);
    ts_external_recorder_free(&recorder);

    ts_external_recorder_init(&recorder);
    CHECK(ts_external_recorder_arm(&recorder, 5, 1000, -20,
                                   0, 100, 0, 2,
                                   error, sizeof(error)));
    CHECK(ts_external_recorder_write_sample(&recorder, 0.2f) == 2);
    CHECK(ts_external_recorder_write_sample(&recorder, 0.1f) == 0);
    CHECK(ts_external_recorder_stop(&recorder, error, sizeof(error)));
    CHECK(recorder.state == TS_EXTERNAL_CAPTURE_COMPLETED);
    CHECK(ts_external_next_chain_slot(5) == 6);
    CHECK(ts_external_next_chain_slot(15) == -1);
    ts_external_recorder_free(&recorder);
}

static void test_immediate_recording(void)
{
    TsExternalRecorder r;char error[160];ts_external_recorder_init(&r);
    CHECK(!ts_external_recorder_start_manual(&r));
    CHECK(ts_external_recorder_arm_channels(&r,0,1000,2,-20,5,2,1,1,error,sizeof(error)));
    CHECK(ts_external_recorder_start_manual(&r));
    CHECK(r.state==TS_EXTERNAL_CAPTURE_RECORDING && !r.recorded_frames);
    /* Leading/inter-note silence is retained, even beyond the trigger mode's
       silence timeout. Capacity still bounds a manual take. */
    for(int i=0;i<500;++i)CHECK(ts_external_recorder_write_frame(&r,(TsStereoFrame){0})==0);
    for(int i=500;i<999;++i)CHECK(ts_external_recorder_write_frame(&r,(TsStereoFrame){.1f,-.2f})==0);
    CHECK(ts_external_recorder_write_frame(&r,(TsStereoFrame){.1f,-.2f})==1);
    CHECK(r.state==TS_EXTERNAL_CAPTURE_COMPLETED && r.recorded_frames==1000);
    CHECK(r.buffer[998]==0 && r.buffer[999]==0 && r.buffer[1000]==.1f && r.buffer[1001]==-.2f);
    CHECK(!ts_external_recorder_start_manual(&r));ts_external_recorder_free(&r);
}

static void test_config_defaults(void)
{
    TsConfig config;
    ts_config_init(&config);
    CHECK(config.record_input_device[0] == '\0');
    CHECK(config.record_input_channel == TS_RECORD_INPUT_CHANNEL_DEFAULT);
    CHECK(config.record_threshold_db == TS_RECORD_THRESHOLD_DB_DEFAULT);
    CHECK(config.record_preroll_ms == TS_RECORD_PREROLL_MS_DEFAULT);
    CHECK(config.record_silence_ms == TS_RECORD_SILENCE_MS_DEFAULT);
    CHECK(config.record_tail_ms == TS_RECORD_TAIL_MS_DEFAULT);
    CHECK(config.record_max_seconds == TS_RECORD_MAX_SECONDS_DEFAULT);
}

int main(void)
{
    test_threshold_preroll_and_autostop();
    test_manual_stop_cancel_and_chain();
    test_immediate_recording();
    test_config_defaults();
    if (failures != 0) {
        fprintf(stderr, "%d external recording test(s) failed\n", failures);
        return 1;
    }
    printf("external recording tests passed\n");
    return 0;
}
