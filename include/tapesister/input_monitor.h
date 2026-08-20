#ifndef TAPESISTER_INPUT_MONITOR_H
#define TAPESISTER_INPUT_MONITOR_H

#include <stddef.h>
#include <stdatomic.h>
#include <stdint.h>

enum {
    TS_INPUT_MONITOR_RING_FRAMES = 16384,
    TS_INPUT_MONITOR_PRIME_FRAMES = 128,
    TS_LIVE_WAVEFORM_COLUMNS = 576
};

typedef struct {
    float ring[TS_INPUT_MONITOR_RING_FRAMES];
    _Atomic uint32_t read_index;
    _Atomic uint32_t write_index;
    _Atomic uint32_t input_rate;
    _Atomic uint32_t reset_generation;
    _Atomic uint32_t level_q;
    _Atomic uint32_t peak_q;
    _Atomic int clipped;
    _Atomic int enabled;
    uint32_t consumer_generation;
    double consumer_phase;
    float consumer_sample;
    int consumer_has_sample;
    int consumer_primed;
} TsInputMonitor;

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
int ts_input_monitor_enabled(const TsInputMonitor *monitor);
void ts_input_monitor_push(TsInputMonitor *monitor, float sample);
float ts_input_monitor_read(TsInputMonitor *monitor, uint32_t output_rate);
void ts_input_monitor_publish_level(TsInputMonitor *monitor, float peak);
float ts_input_monitor_level(const TsInputMonitor *monitor);
float ts_input_monitor_take_peak(TsInputMonitor *monitor);
int ts_input_monitor_take_clip(TsInputMonitor *monitor);
void ts_input_monitor_reset_meter(TsInputMonitor *monitor);

void ts_live_waveform_init(TsLiveWaveform *waveform, uint32_t sample_rate);
void ts_live_waveform_push(TsLiveWaveform *waveform,
                           const float *samples, size_t frames);
size_t ts_live_waveform_snapshot(const TsLiveWaveform *waveform,
                                 float *minimum, float *maximum,
                                 size_t capacity);

#endif
