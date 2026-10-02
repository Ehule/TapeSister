#ifndef TAPESISTER_REALTIME_DIAGNOSTICS_H
#define TAPESISTER_REALTIME_DIAGNOSTICS_H

#include <stdatomic.h>
#include <stdint.h>

/* Bitfield describing the callback state associated with a timing sample. */
enum {
    TS_RT_CONFIG_SISTER = 1u << 0,
    TS_RT_CONFIG_TILES = 1u << 1,
    TS_RT_CONFIG_FM = 1u << 2,
    TS_RT_CONFIG_EXT = 1u << 3,
    TS_RT_CONFIG_PREVIEW = 1u << 4,
    TS_RT_CONFIG_H1 = 1u << 5,
    TS_RT_CONFIG_H2 = 1u << 6,
    TS_RT_CONFIG_H3 = 1u << 7,
    TS_RT_CONFIG_SOAK = 1u << 8,
    TS_RT_CONFIG_REVERB = 1u << 9,
    TS_RT_CONFIG_DELAY = 1u << 10,
    TS_RT_CONFIG_DISTORTION = 1u << 11,
    TS_RT_CONFIG_FX_FEEDBACK = 1u << 12,
    TS_RT_CONFIG_FALLOUT = 1u << 13,
    TS_RT_CONFIG_FILE_CAPTURE = 1u << 14,
    TS_RT_CONFIG_TAPEHEAD = 1u << 15
};

typedef struct {
    atomic_uint_least64_t callback_count;
    atomic_uint_least64_t frame_count;
    atomic_uint_least64_t elapsed_ticks;
    atomic_uint_least64_t worst_ticks;
    atomic_uint_least64_t deadline_overruns;
    atomic_uint_least64_t near_overruns;
    atomic_uint_least64_t counter_frequency;
    atomic_uint_least32_t sample_rate;
    atomic_uint_least32_t device_buffer_frames;
    atomic_uint_least32_t active_configuration;
    atomic_uint_least64_t recent_elapsed_ticks, recent_worst_ticks;
    atomic_uint_least64_t recent_frame_count, recent_callback_count;
    atomic_uint_least64_t recent_frequency;
    atomic_uint_least32_t recent_rate;
    atomic_uint_least32_t recent_revision;
    atomic_uint_least64_t start_gap_ticks, delayed_starts;
    atomic_uint_least64_t control_count, control_ticks, control_worst_ticks;
    atomic_uint_least64_t control_wait_worst_ticks, control_long_holds;
    atomic_uintptr_t control_worst_site;
    atomic_uint_least32_t stream_generation;
    /* Callback-owned accumulators. Reset only with callback/control exclusion. */
    uint64_t previous_start, previous_period, previous_frequency;
    uint64_t window_ticks, window_worst, window_frames, window_callbacks;
    uint32_t previous_rate, observed_generation;
} TsRealtimeDiagnostics;

typedef struct {
    uint64_t callback_count;
    uint64_t frame_count;
    uint64_t elapsed_ticks;
    uint64_t worst_ticks;
    uint64_t deadline_overruns;
    uint64_t near_overruns;
    uint64_t counter_frequency;
    uint32_t sample_rate;
    uint32_t device_buffer_frames;
    uint32_t active_configuration;
    double average_microseconds;
    double worst_microseconds;
    double deadline_microseconds;
    double recent_average_microseconds, recent_worst_microseconds;
    double recent_load_percent;
    double start_gap_microseconds, control_worst_microseconds;
    double control_wait_worst_microseconds;
    uint64_t delayed_starts, control_count, control_long_holds;
    const char *control_worst_site; /* Static caller name; report text only. */
    uint64_t recent_callback_count;
} TsRealtimeDiagnosticsSnapshot;

void ts_realtime_diagnostics_init(TsRealtimeDiagnostics *diagnostics);
/* UI reset: caller must exclude both audio and control writers. */
void ts_realtime_diagnostics_reset(TsRealtimeDiagnostics *diagnostics);
/* Thread-safe lifecycle notification; the next callback starts a fresh cadence. */
void ts_realtime_diagnostics_discontinuity(TsRealtimeDiagnostics *diagnostics);
int ts_realtime_diagnostics_is_lock_free(
    const TsRealtimeDiagnostics *diagnostics);

/* Callback-safe: bounded lock-free atomic counter updates only. Time is sampled
   by the caller so this helper never invokes a clock, allocator, logger or OS
   service from inside the audio callback. One serialized audio writer; a
   concurrent reader may take snapshots without excluding that writer. */
void ts_realtime_diagnostics_record(TsRealtimeDiagnostics *diagnostics,
                                    uint64_t elapsed_ticks,
                                    uint64_t counter_frequency,
                                    uint32_t sample_rate,
                                    uint32_t device_buffer_frames,
                                    uint32_t active_configuration);

/* Arrival gaps are observations, not proof of hardware dropouts. Native workers
   may batch callbacks. Ignore cadence across intentional pause/device changes. */
void ts_realtime_diagnostics_record_timed(TsRealtimeDiagnostics *diagnostics,
    uint64_t started_ticks, uint64_t elapsed_ticks, uint64_t counter_frequency,
    uint32_t sample_rate, uint32_t frames, uint32_t active_configuration);

/* Called before releasing the existing output/shared-ASIO device lock. Hold
   time starts AFTER acquisition; UI wait time is measured separately. Nested
   acquisitions must be combined by the caller into one outer hold. */
void ts_realtime_diagnostics_record_control(TsRealtimeDiagnostics *diagnostics,
    uint64_t hold_ticks, uint64_t wait_ticks, uint64_t counter_frequency,
    const char *static_site);

int ts_realtime_diagnostics_get(const TsRealtimeDiagnostics *diagnostics,
                                TsRealtimeDiagnosticsSnapshot *snapshot);

#endif
