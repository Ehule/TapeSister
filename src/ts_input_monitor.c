#include "tapesister/input_monitor.h"

#include <math.h>
#include <string.h>

static uint32_t level_to_q(float level)
{
    if (!isfinite(level) || level < 0.0f) level = 0.0f;
    if (level > 4.0f) level = 4.0f;
    return (uint32_t)lrintf(level * 1000000.0f);
}

static float q_to_level(uint32_t value)
{
    return (float)value / 1000000.0f;
}

void ts_input_monitor_init(TsInputMonitor *monitor)
{
    if (monitor == NULL) return;
    memset(monitor->ring, 0, sizeof(monitor->ring));
    atomic_init(&monitor->read_index, 0u);
    atomic_init(&monitor->write_index, 0u);
    atomic_init(&monitor->input_rate, 0u);
    atomic_init(&monitor->reset_generation, 1u);
    atomic_init(&monitor->level_q, 0u);
    atomic_init(&monitor->peak_q, 0u);
    atomic_init(&monitor->clipped, 0);
    atomic_init(&monitor->enabled, 0);
    monitor->consumer_generation = 0u;
    monitor->consumer_phase = 0.0;
    monitor->consumer_sample = 0.0f;
    monitor->consumer_has_sample = 0;
    monitor->consumer_primed = 0;
}

void ts_input_monitor_set_enabled(TsInputMonitor *monitor, int enabled,
                                  uint32_t input_rate)
{
    uint32_t generation;
    if (monitor == NULL) return;
    atomic_store_explicit(&monitor->enabled, 0, memory_order_release);
    atomic_store_explicit(&monitor->read_index, 0u, memory_order_release);
    atomic_store_explicit(&monitor->write_index, 0u, memory_order_release);
    atomic_store_explicit(&monitor->input_rate, input_rate, memory_order_release);
    generation = atomic_load_explicit(&monitor->reset_generation,
                                      memory_order_relaxed) + 1u;
    atomic_store_explicit(&monitor->reset_generation, generation,
                          memory_order_release);
    if (enabled)
        atomic_store_explicit(&monitor->enabled, 1, memory_order_release);
}

int ts_input_monitor_enabled(const TsInputMonitor *monitor)
{
    return monitor != NULL &&
           atomic_load_explicit(&monitor->enabled, memory_order_acquire) != 0;
}

void ts_input_monitor_push(TsInputMonitor *monitor, float sample)
{
    uint32_t read_at;
    uint32_t write_at;
    if (monitor == NULL || !ts_input_monitor_enabled(monitor)) return;
    if (!isfinite(sample)) sample = 0.0f;
    write_at = atomic_load_explicit(&monitor->write_index, memory_order_relaxed);
    read_at = atomic_load_explicit(&monitor->read_index, memory_order_acquire);
    if (write_at - read_at >= TS_INPUT_MONITOR_RING_FRAMES) return;
    monitor->ring[write_at & (TS_INPUT_MONITOR_RING_FRAMES - 1u)] = sample;
    atomic_store_explicit(&monitor->write_index, write_at + 1u,
                          memory_order_release);
}

static int monitor_pop(TsInputMonitor *monitor, float *sample)
{
    uint32_t read_at = atomic_load_explicit(&monitor->read_index,
                                            memory_order_relaxed);
    uint32_t write_at = atomic_load_explicit(&monitor->write_index,
                                             memory_order_acquire);
    if (read_at == write_at) return 0;
    *sample = monitor->ring[read_at & (TS_INPUT_MONITOR_RING_FRAMES - 1u)];
    atomic_store_explicit(&monitor->read_index, read_at + 1u,
                          memory_order_release);
    return 1;
}

float ts_input_monitor_read(TsInputMonitor *monitor, uint32_t output_rate)
{
    uint32_t generation;
    uint32_t read_at;
    uint32_t write_at;
    uint32_t input_rate;
    double ratio;
    float output;
    if (monitor == NULL || output_rate == 0u ||
        !ts_input_monitor_enabled(monitor)) return 0.0f;
    generation = atomic_load_explicit(&monitor->reset_generation,
                                      memory_order_acquire);
    if (monitor->consumer_generation != generation) {
        monitor->consumer_generation = generation;
        monitor->consumer_phase = 0.0;
        monitor->consumer_sample = 0.0f;
        monitor->consumer_has_sample = 0;
        monitor->consumer_primed = 0;
    }
    read_at = atomic_load_explicit(&monitor->read_index, memory_order_relaxed);
    write_at = atomic_load_explicit(&monitor->write_index, memory_order_acquire);
    if (!monitor->consumer_primed) {
        if (write_at - read_at < TS_INPUT_MONITOR_PRIME_FRAMES) return 0.0f;
        monitor->consumer_primed = 1;
    }
    if (!monitor->consumer_has_sample) {
        if (!monitor_pop(monitor, &monitor->consumer_sample)) {
            monitor->consumer_primed = 0;
            return 0.0f;
        }
        monitor->consumer_has_sample = 1;
    }
    output = monitor->consumer_sample;
    input_rate = atomic_load_explicit(&monitor->input_rate, memory_order_acquire);
    ratio = input_rate > 0u ? (double)input_rate / (double)output_rate : 1.0;
    monitor->consumer_phase += ratio;
    while (monitor->consumer_phase >= 1.0) {
        float next;
        monitor->consumer_phase -= 1.0;
        if (!monitor_pop(monitor, &next)) {
            monitor->consumer_has_sample = 0;
            monitor->consumer_primed = 0;
            break;
        }
        monitor->consumer_sample = next;
    }
    return output;
}

void ts_input_monitor_publish_level(TsInputMonitor *monitor, float peak)
{
    uint32_t value;
    uint32_t held;
    if (monitor == NULL) return;
    value = level_to_q(peak);
    atomic_store_explicit(&monitor->level_q, value, memory_order_release);
    held = atomic_load_explicit(&monitor->peak_q, memory_order_relaxed);
    while (value > held &&
           !atomic_compare_exchange_weak_explicit(
               &monitor->peak_q, &held, value,
               memory_order_release, memory_order_relaxed)) {}
    if (peak >= 0.999f)
        atomic_store_explicit(&monitor->clipped, 1, memory_order_release);
}

float ts_input_monitor_level(const TsInputMonitor *monitor)
{
    if (monitor == NULL) return 0.0f;
    return q_to_level(atomic_load_explicit(&monitor->level_q,
                                           memory_order_acquire));
}

float ts_input_monitor_take_peak(TsInputMonitor *monitor)
{
    if (monitor == NULL) return 0.0f;
    return q_to_level(atomic_exchange_explicit(&monitor->peak_q, 0u,
                                                memory_order_acq_rel));
}

int ts_input_monitor_take_clip(TsInputMonitor *monitor)
{
    if (monitor == NULL) return 0;
    return atomic_exchange_explicit(&monitor->clipped, 0,
                                    memory_order_acq_rel);
}

void ts_input_monitor_reset_meter(TsInputMonitor *monitor)
{
    if (monitor == NULL) return;
    atomic_store_explicit(&monitor->level_q, 0u, memory_order_release);
    atomic_store_explicit(&monitor->peak_q, 0u, memory_order_release);
    atomic_store_explicit(&monitor->clipped, 0, memory_order_release);
}

void ts_live_waveform_init(TsLiveWaveform *waveform, uint32_t sample_rate)
{
    size_t ten_seconds;
    if (waveform == NULL) return;
    memset(waveform, 0, sizeof(*waveform));
    ten_seconds = sample_rate > 0u ? (size_t)sample_rate * 10u : 480000u;
    waveform->samples_per_column =
        (ten_seconds + TS_LIVE_WAVEFORM_COLUMNS - 1u) /
        TS_LIVE_WAVEFORM_COLUMNS;
    if (waveform->samples_per_column == 0u)
        waveform->samples_per_column = 1u;
    waveform->current_minimum = 1.0f;
    waveform->current_maximum = -1.0f;
}

static void live_waveform_commit_column(TsLiveWaveform *waveform)
{
    size_t at = waveform->write_column;
    waveform->minimum[at] = waveform->current_minimum;
    waveform->maximum[at] = waveform->current_maximum;
    waveform->write_column = (at + 1u) % TS_LIVE_WAVEFORM_COLUMNS;
    if (waveform->column_count < TS_LIVE_WAVEFORM_COLUMNS)
        ++waveform->column_count;
    waveform->samples_in_column = 0u;
    waveform->current_minimum = 1.0f;
    waveform->current_maximum = -1.0f;
}

void ts_live_waveform_push(TsLiveWaveform *waveform,
                           const float *samples, size_t frames)
{
    if (waveform == NULL || samples == NULL) return;
    for (size_t frame = 0; frame < frames; ++frame) {
        float value = isfinite(samples[frame]) ? samples[frame] : 0.0f;
        if (value < waveform->current_minimum) waveform->current_minimum = value;
        if (value > waveform->current_maximum) waveform->current_maximum = value;
        ++waveform->samples_in_column;
        ++waveform->total_frames;
        if (waveform->samples_in_column >= waveform->samples_per_column)
            live_waveform_commit_column(waveform);
    }
}

size_t ts_live_waveform_snapshot(const TsLiveWaveform *waveform,
                                 float *minimum, float *maximum,
                                 size_t capacity)
{
    size_t count;
    size_t first;
    if (waveform == NULL || minimum == NULL || maximum == NULL || capacity == 0u)
        return 0u;
    count = waveform->column_count;
    if (count > capacity) count = capacity;
    first = waveform->column_count == TS_LIVE_WAVEFORM_COLUMNS ?
            waveform->write_column : 0u;
    for (size_t column = 0; column < count; ++column) {
        size_t source = (first + column) % TS_LIVE_WAVEFORM_COLUMNS;
        minimum[column] = waveform->minimum[source];
        maximum[column] = waveform->maximum[source];
    }
    if (waveform->samples_in_column > 0u && count < capacity) {
        minimum[count] = waveform->current_minimum;
        maximum[count] = waveform->current_maximum;
        ++count;
    }
    return count;
}
