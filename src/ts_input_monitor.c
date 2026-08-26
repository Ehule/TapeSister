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

int ts_input_channel_mode_valid(int mode)
{
    return mode >= TS_INPUT_CHANNEL_MIX && mode <= TS_INPUT_CHANNEL_STEREO;
}

const char *ts_input_channel_mode_name(int mode)
{
    if (mode == TS_INPUT_CHANNEL_LEFT) return "LEFT";
    if (mode == TS_INPUT_CHANNEL_RIGHT) return "RIGHT";
    if (mode == TS_INPUT_CHANNEL_STEREO) return "STEREO";
    return "MIX";
}

uint8_t ts_input_channel_record_channels(int mode)
{
    return mode == TS_INPUT_CHANNEL_STEREO ? 2u : 1u;
}

TsStereoFrame ts_input_channel_select(const float *device_frame,
                                       size_t device_channels, int mode)
{
    TsStereoFrame result = {0.0f, 0.0f};
    float mono = 0.0f;
    if (device_frame == NULL || device_channels == 0u ||
        !ts_input_channel_mode_valid(mode)) return result;
    /* SDL exposes the negotiated count. Common capture code rejects layouts
       above eight; retain a defensive bound for direct callers and tests. */
    if (device_channels > TS_INPUT_DEVICE_CHANNEL_MAX)
        device_channels = TS_INPUT_DEVICE_CHANNEL_MAX;
    if (mode == TS_INPUT_CHANNEL_STEREO) {
        size_t left_count = 0u;
        size_t right_count = 0u;
        if (device_channels == 1u)
            return ts_stereo_frame_from_mono(
                isfinite(device_frame[0]) ? device_frame[0] : 0.0f);
        /* Preserve hardware stereo pairs: 1/2, 3/4, ... . Averaging each
           side gives deterministic headroom and leaves ordinary stereo at
           unity gain. */
        for (size_t channel = 0u; channel < device_channels; ++channel) {
            float value = isfinite(device_frame[channel]) ?
                          device_frame[channel] : 0.0f;
            if ((channel & 1u) == 0u) {
                result.l += value;
                ++left_count;
            } else {
                result.r += value;
                ++right_count;
            }
        }
        if (left_count > 0u) result.l /= (float)left_count;
        if (right_count > 0u) result.r /= (float)right_count;
        return ts_stereo_frame_sanitize(result);
    }
    if (mode == TS_INPUT_CHANNEL_MIX) {
        for (size_t channel = 0u; channel < device_channels; ++channel) {
            float value = isfinite(device_frame[channel]) ?
                          device_frame[channel] : 0.0f;
            mono += value;
        }
        mono /= (float)device_channels;
    } else if (mode == TS_INPUT_CHANNEL_RIGHT) {
        size_t channel = device_channels > 1u ? 1u : 0u;
        mono = device_frame[channel];
    } else {
        mono = device_frame[0];
    }
    return ts_stereo_frame_from_mono(mono);
}

uint32_t ts_input_activity_detect_frame(const float *device_frame,
                                        size_t device_channels)
{
    uint32_t mask = 0u;
    if (device_frame == NULL || device_channels == 0u) return 0u;
    if (device_channels > TS_INPUT_DEVICE_CHANNEL_MAX)
        device_channels = TS_INPUT_DEVICE_CHANNEL_MAX;
    for (size_t channel = 0u; channel < device_channels; ++channel) {
        float value = device_frame[channel];
        if (isfinite(value) && fabsf(value) >= TS_INPUT_ACTIVITY_THRESHOLD)
            mask |= UINT32_C(1) << channel;
    }
    return mask;
}

void ts_input_activity_init(TsInputActivity *activity)
{
    if (activity == NULL) return;
    atomic_init(&activity->available_channels, 0u);
    atomic_init(&activity->pending_activity_mask, 0u);
}

void ts_input_activity_set_available(TsInputActivity *activity,
                                     uint32_t channels)
{
    if (activity == NULL) return;
    if (channels > TS_INPUT_DEVICE_CHANNEL_MAX) channels = 0u;
    atomic_store_explicit(&activity->pending_activity_mask, 0u,
                          memory_order_release);
    atomic_store_explicit(&activity->available_channels, channels,
                          memory_order_release);
}

uint32_t ts_input_activity_available(const TsInputActivity *activity)
{
    if (activity == NULL) return 0u;
    return atomic_load_explicit(&activity->available_channels,
                                memory_order_acquire);
}

void ts_input_activity_publish(TsInputActivity *activity, uint32_t mask)
{
    uint32_t channels;
    uint32_t valid;
    if (activity == NULL || mask == 0u) return;
    channels = ts_input_activity_available(activity);
    valid = channels == 0u ? 0u : (UINT32_C(1) << channels) - 1u;
    mask &= valid;
    if (mask != 0u)
        (void)atomic_fetch_or_explicit(&activity->pending_activity_mask, mask,
                                       memory_order_release);
}

uint32_t ts_input_activity_take(TsInputActivity *activity)
{
    if (activity == NULL) return 0u;
    return atomic_exchange_explicit(&activity->pending_activity_mask, 0u,
                                    memory_order_acq_rel);
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
    monitor->consumer_sample = (TsStereoFrame){0.0f, 0.0f};
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
    ts_input_monitor_push_frame(monitor, ts_stereo_frame_from_mono(sample));
}

void ts_input_monitor_push_frame(TsInputMonitor *monitor, TsStereoFrame sample)
{
    uint32_t read_at;
    uint32_t write_at;
    if (monitor == NULL || !ts_input_monitor_enabled(monitor)) return;
    sample = ts_stereo_frame_sanitize(sample);
    write_at = atomic_load_explicit(&monitor->write_index, memory_order_relaxed);
    read_at = atomic_load_explicit(&monitor->read_index, memory_order_acquire);
    if (write_at - read_at >= TS_INPUT_MONITOR_RING_FRAMES) return;
    monitor->ring[write_at & (TS_INPUT_MONITOR_RING_FRAMES - 1u)] = sample;
    atomic_store_explicit(&monitor->write_index, write_at + 1u,
                          memory_order_release);
}

static int monitor_pop(TsInputMonitor *monitor, TsStereoFrame *sample)
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

TsStereoFrame ts_input_monitor_read_frame(TsInputMonitor *monitor,
                                          uint32_t output_rate)
{
    uint32_t generation;
    uint32_t read_at;
    uint32_t write_at;
    uint32_t input_rate;
    double ratio;
    TsStereoFrame output = {0.0f, 0.0f};
    if (monitor == NULL || output_rate == 0u ||
        !ts_input_monitor_enabled(monitor)) return output;
    generation = atomic_load_explicit(&monitor->reset_generation,
                                      memory_order_acquire);
    if (monitor->consumer_generation != generation) {
        monitor->consumer_generation = generation;
        monitor->consumer_phase = 0.0;
        monitor->consumer_sample = (TsStereoFrame){0.0f, 0.0f};
        monitor->consumer_has_sample = 0;
        monitor->consumer_primed = 0;
    }
    read_at = atomic_load_explicit(&monitor->read_index, memory_order_relaxed);
    write_at = atomic_load_explicit(&monitor->write_index, memory_order_acquire);
    if (!monitor->consumer_primed) {
        if (write_at - read_at < TS_INPUT_MONITOR_PRIME_FRAMES) return output;
        monitor->consumer_primed = 1;
    }
    if (!monitor->consumer_has_sample) {
        if (!monitor_pop(monitor, &monitor->consumer_sample)) {
            monitor->consumer_primed = 0;
            return output;
        }
        monitor->consumer_has_sample = 1;
    }
    output = monitor->consumer_sample;
    input_rate = atomic_load_explicit(&monitor->input_rate, memory_order_acquire);
    ratio = input_rate > 0u ? (double)input_rate / (double)output_rate : 1.0;
    monitor->consumer_phase += ratio;
    while (monitor->consumer_phase >= 1.0) {
        TsStereoFrame next;
        monitor->consumer_phase -= 1.0;
        if (!monitor_pop(monitor, &next)) {
            monitor->consumer_has_sample = 0;
            monitor->consumer_primed = 0;
            break;
        }
        monitor->consumer_sample = next;
    }
    return ts_stereo_frame_sanitize(output);
}

float ts_input_monitor_read(TsInputMonitor *monitor, uint32_t output_rate)
{
    return ts_stereo_frame_fold_mono(
        ts_input_monitor_read_frame(monitor, output_rate));
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
    ts_live_waveform_push_channels(waveform, samples, frames, 1u);
}

void ts_live_waveform_push_channels(TsLiveWaveform *waveform,
                                    const float *samples, size_t frames,
                                    uint8_t channels)
{
    if (waveform == NULL || samples == NULL ||
        (channels != 1u && channels != 2u)) return;
    for (size_t frame = 0; frame < frames; ++frame) {
        TsStereoFrame selected = channels == 2u ?
            (TsStereoFrame){samples[frame * 2u], samples[frame * 2u + 1u]} :
            ts_stereo_frame_from_mono(samples[frame]);
        float value = ts_stereo_frame_fold_mono(
            ts_stereo_frame_sanitize(selected));
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
