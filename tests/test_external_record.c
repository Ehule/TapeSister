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


static void stream_feed(TsExternalRecorder *r,int frames,TsStereoFrame value)
{
    for(int i=0;i<frames;++i) {
        ts_external_recorder_write_frame(r,value);
        if(i%64==63)ts_performance_recorder_pump(r->stream,256);
    }
}
static void stream_finish(TsExternalRecorder *r)
{
    ts_performance_recorder_request_stop(r->stream);
    while(ts_performance_recorder_pump(r->stream,256)) {}
}
static void test_mosaic_stream(void)
{
    TsExternalRecorder r;char error[160];TsSample sample={0};ts_external_recorder_init(&r);
    /* Exact finite durations include silence and are independent of the
       unlimited timer. The preview ring is bounded even as the file grows. */
    CHECK(ts_external_recorder_arm_stream(&r,"test-mosaic-stream.wav",1000,2,70,1,-60,error,sizeof(error)));
    stream_feed(&r,69999,(TsStereoFrame){0});
    CHECK(r.state==TS_EXTERNAL_CAPTURE_RECORDING && r.recorded_frames==69999 && r.preview_capacity==65536);
    CHECK(ts_external_recorder_write_frame(&r,(TsStereoFrame){.25f,-.5f})==1);
    CHECK(r.recorded_frames==70000 && !r.silence_stopped);
    CHECK(!ts_external_recorder_write_sample(&r,1));stream_finish(&r);
    CHECK(ts_performance_recorder_load(r.stream,&sample,error,sizeof(error)));
    CHECK(sample.frames==70000 && sample.channels==2 && sample.data[139998]==.25f && sample.data[139999]==-.5f);
    ts_sample_free(&sample);ts_external_recorder_free(&r);remove("test-mosaic-stream.wav");
    /* Three minutes of a very quiet signal must survive; either stereo channel
       resets the full two-minute silence timer. Leading silence is counted too. */
    CHECK(ts_external_recorder_arm_stream(&r,"test-mosaic-stream.wav",100,2,0,120,-60,error,sizeof(error)));
    stream_feed(&r,18000,(TsStereoFrame){0,.002f});
    CHECK(r.state==TS_EXTERNAL_CAPTURE_RECORDING && r.quiet_frames==0);
    stream_feed(&r,11999,(TsStereoFrame){0});CHECK(r.state==TS_EXTERNAL_CAPTURE_RECORDING);
    stream_feed(&r,1,(TsStereoFrame){-.002f,0});CHECK(!r.quiet_frames);
    stream_feed(&r,12000,(TsStereoFrame){0});
    CHECK(r.state==TS_EXTERNAL_CAPTURE_COMPLETED && r.silence_stopped && r.recorded_frames==42000);
    stream_finish(&r);CHECK(ts_performance_recorder_load(r.stream,&sample,error,sizeof(error)));
    CHECK(sample.frames==42000);ts_sample_free(&sample);ts_external_recorder_free(&r);remove("test-mosaic-stream.wav");
    CHECK(ts_external_recorder_arm_stream(&r,"test-mosaic-stream.wav",100,1,0,120,-60,error,sizeof(error)));
    stream_feed(&r,12000,(TsStereoFrame){0});CHECK(r.silence_stopped && r.recorded_frames==12000);
    stream_finish(&r);ts_external_recorder_free(&r);remove("test-mosaic-stream.wav");
    /* Writer starvation stops at the first missing frame and retains the
       contiguous prefix, never silently dropping frames from a live take. */
    CHECK(ts_external_recorder_arm_stream(&r,"test-mosaic-stream.wav",100,2,0,0,-60,error,sizeof(error)));
    for(int i=0;i<201;++i)ts_external_recorder_write_frame(&r,(TsStereoFrame){.1f,-.2f});
    CHECK(r.state==TS_EXTERNAL_CAPTURE_COMPLETED && r.stream_overrun && r.recorded_frames==200);
    stream_finish(&r);CHECK(ts_performance_recorder_load(r.stream,&sample,error,sizeof(error)));
    CHECK(sample.frames==200);ts_sample_free(&sample);ts_external_recorder_free(&r);remove("test-mosaic-stream.wav");
}
static void test_long_config(void)
{
    TsConfig c,d;char error[160];ts_config_init(&c);
    CHECK(c.prism_morph_seconds==240 && c.prism_step_seconds==240);
    CHECK(c.mosaic_record_seconds==0 && c.mosaic_silence_seconds==120 && c.mosaic_silence_db==-60);
    c.prism_morph_seconds=3600;c.prism_step_seconds=1854;c.mosaic_record_seconds=133;
    c.mosaic_silence_db=-84;c.mosaic_silence_seconds=180;
    CHECK(ts_config_save(&c,"test-glacial.ini",error,sizeof(error)));
    CHECK(ts_config_load(&d,"test-glacial.ini",error,sizeof(error)));
    CHECK(d.prism_morph_seconds==3600 && d.prism_step_seconds==1854 && d.mosaic_record_seconds==133);
    CHECK(d.mosaic_silence_db==-84 && d.mosaic_silence_seconds==180 && d.record_max_seconds==20 && d.capture_max_seconds==20);
    FILE *f=fopen("test-glacial.ini","w");CHECK(f!=NULL);
    if(f){fputs("prism_morph_seconds=9000\nprism_step_seconds=-4\nmosaic_record_seconds=4\nmosaic_silence_db=-100\n",f);fclose(f);}
    CHECK(ts_config_load(&d,"test-glacial.ini",error,sizeof(error)));
    CHECK(d.prism_morph_seconds==3600 && d.prism_step_seconds==0 && d.mosaic_record_seconds==10 && d.mosaic_silence_db==-90);
    remove("test-glacial.ini");
    for(int seconds=10;seconds<=3600;++seconds)CHECK(ts_mosaic_record_length_seconds(ts_mosaic_record_length_normalized(seconds))==seconds);
    CHECK(ts_mosaic_record_length_seconds(1)==0);
}

int main(void)
{
    test_threshold_preroll_and_autostop();
    test_manual_stop_cancel_and_chain();
    test_immediate_recording();
    test_config_defaults();
    test_mosaic_stream();test_long_config();
    if (failures != 0) {
        fprintf(stderr, "%d external recording test(s) failed\n", failures);
        return 1;
    }
    printf("external recording tests passed\n");
    return 0;
}
