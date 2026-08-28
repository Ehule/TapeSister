#ifndef TAPESISTER_INPUT_MONITOR_H
#define TAPESISTER_INPUT_MONITOR_H

#include <stddef.h>
#include <stdatomic.h>
#include <stdint.h>

#include "tapesister/sample.h"

enum {
    TS_INPUT_MONITOR_RING_FRAMES = 16384,
    TS_INPUT_MONITOR_PRIME_FRAMES = 128,
    TS_INPUT_MONITOR_MAX_PRIME_FRAMES = 4096,
    TS_INPUT_MONITOR_PRIME_DEVICE_BUFFERS = 4,
    TS_INPUT_MONITOR_FADE_FRAMES = 32,
    TS_INPUT_MONITOR_SERVO_INTERVAL_FRAMES = 64,
    TS_LIVE_WAVEFORM_COLUMNS = 576,
    TS_INPUT_DEVICE_CHANNEL_MAX = 8
};

#define TS_INPUT_ACTIVITY_THRESHOLD 0.001f

typedef enum {
    TS_INPUT_CHANNEL_MIX = 0,
    TS_INPUT_CHANNEL_LEFT = 1,
    TS_INPUT_CHANNEL_RIGHT = 2,
    TS_INPUT_CHANNEL_STEREO = 3
} TsInputChannelMode;

typedef struct {
    TsStereoFrame ring[TS_INPUT_MONITOR_RING_FRAMES];
    _Atomic uint32_t read_index;
    _Atomic uint32_t write_index;
    _Atomic uint32_t input_rate;
    _Atomic uint32_t prime_frames;
    _Atomic uint32_t reset_generation;
    _Atomic uint32_t underrun_count;
    _Atomic uint32_t dropped_frame_count;
    _Atomic uint32_t capture_callback_count;
    _Atomic uint32_t captured_frame_count;
    _Atomic uint32_t largest_capture_block_frames;
    _Atomic int32_t correction_ppm;
    _Atomic uint32_t level_q;
    _Atomic uint32_t peak_q;
    _Atomic int clipped;
    _Atomic int enabled;
    uint32_t consumer_generation;
    double consumer_phase;
    double consumer_ratio;
    TsStereoFrame consumer_sample;
    TsStereoFrame consumer_next_sample;
    TsStereoFrame consumer_last_sample;
    uint32_t consumer_servo_countdown;
    double consumer_occupancy_average;
    double consumer_servo_integral;
    float consumer_gain;
    int consumer_has_sample;
    int consumer_has_next_sample;
    int consumer_primed;
} TsInputMonitor;

typedef struct {
    uint32_t occupancy_frames;
    uint32_t prime_frames;
    uint32_t underrun_count;
    uint32_t dropped_frame_count;
    uint32_t reset_generation;
    uint32_t capture_callback_count;
    uint32_t captured_frame_count;
    uint32_t largest_capture_block_frames;
    int32_t correction_ppm;
} TsInputMonitorDiagnostics;

/* The callback publishes only a bounded bit mask. The UI consumes and holds
   those bits independently, so no timing or drawing work enters audio code. */
typedef struct {
    _Atomic uint32_t available_channels;
    _Atomic uint32_t pending_activity_mask;
} TsInputActivity;

typedef struct {
    float minimum[TS_LIVE_WAVEFORM_COLUMNS];
    float maximum[TS_LIVE_WAVEFORM_COLUMNS];
    size_t column_count;
    size_t write_column;
    size_t samples_per_column;
    size_t samples_in_column;
    size_t total_frames;
    float current_minimum;
    float current_maximum;
} TsLiveWaveform;

void ts_input_monitor_init(TsInputMonitor *monitor);
void ts_input_monitor_set_enabled(TsInputMonitor *monitor, int enabled,
                                  uint32_t input_rate);
uint32_t ts_input_monitor_recommended_prime_frames(
    uint32_t device_buffer_frames);
void ts_input_monitor_set_prime_frames(TsInputMonitor *monitor,
                                       uint32_t prime_frames);
int ts_input_monitor_get_diagnostics(const TsInputMonitor *monitor,
                                     TsInputMonitorDiagnostics *diagnostics);
int ts_input_monitor_enabled(const TsInputMonitor *monitor);
void ts_input_monitor_push(TsInputMonitor *monitor, float sample);
void ts_input_monitor_note_capture_block(TsInputMonitor *monitor,
                                         uint32_t frame_count);
float ts_input_monitor_read(TsInputMonitor *monitor, uint32_t output_rate);
void ts_input_monitor_push_frame(TsInputMonitor *monitor, TsStereoFrame sample);
TsStereoFrame ts_input_monitor_read_frame(TsInputMonitor *monitor,
                                          uint32_t output_rate);
void ts_input_monitor_publish_level(TsInputMonitor *monitor, float peak);
float ts_input_monitor_level(const TsInputMonitor *monitor);
float ts_input_monitor_take_peak(TsInputMonitor *monitor);
int ts_input_monitor_take_clip(TsInputMonitor *monitor);
void ts_input_monitor_reset_meter(TsInputMonitor *monitor);
int ts_input_channel_mode_valid(int mode);
const char *ts_input_channel_mode_name(int mode);
uint8_t ts_input_channel_record_channels(int mode);
uint8_t ts_input_capture_probe_request(uint8_t maximum, uint8_t attempt);
int ts_input_capture_probe_accepts(uint8_t requested, uint8_t obtained);
TsStereoFrame ts_input_channel_select(const float *device_frame,
                                       size_t device_channels, int mode);
uint32_t ts_input_activity_detect_frame(const float *device_frame,
                                        size_t device_channels);
void ts_input_activity_init(TsInputActivity *activity);
void ts_input_activity_set_available(TsInputActivity *activity,
                                     uint32_t channels);
uint32_t ts_input_activity_available(const TsInputActivity *activity);
void ts_input_activity_publish(TsInputActivity *activity, uint32_t mask);
uint32_t ts_input_activity_take(TsInputActivity *activity);

void ts_live_waveform_init(TsLiveWaveform *waveform, uint32_t sample_rate);
void ts_live_waveform_push(TsLiveWaveform *waveform,
                           const float *samples, size_t frames);
void ts_live_waveform_push_channels(TsLiveWaveform *waveform,
                                    const float *samples, size_t frames,
                                    uint8_t channels);
size_t ts_live_waveform_snapshot(const TsLiveWaveform *waveform,
                                 float *minimum, float *maximum,
                                 size_t capacity);

#endif
