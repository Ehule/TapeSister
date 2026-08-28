#include "tapesister/realtime_diagnostics.h"

#include <stddef.h>

void ts_realtime_diagnostics_init(TsRealtimeDiagnostics *diagnostics)
{
    if (diagnostics == NULL) return;
    atomic_init(&diagnostics->callback_count, 0u);
    atomic_init(&diagnostics->frame_count, 0u);
    atomic_init(&diagnostics->elapsed_ticks, 0u);
    atomic_init(&diagnostics->worst_ticks, 0u);
    atomic_init(&diagnostics->deadline_overruns, 0u);
    atomic_init(&diagnostics->near_overruns, 0u);
    atomic_init(&diagnostics->counter_frequency, 0u);
    atomic_init(&diagnostics->sample_rate, 0u);
    atomic_init(&diagnostics->device_buffer_frames, 0u);
    atomic_init(&diagnostics->active_configuration, 0u);
}

int ts_realtime_diagnostics_is_lock_free(
    const TsRealtimeDiagnostics *diagnostics)
{
    if (diagnostics == NULL) return 0;
    return atomic_is_lock_free(&diagnostics->callback_count) &&
           atomic_is_lock_free(&diagnostics->frame_count) &&
           atomic_is_lock_free(&diagnostics->elapsed_ticks) &&
           atomic_is_lock_free(&diagnostics->worst_ticks) &&
           atomic_is_lock_free(&diagnostics->deadline_overruns) &&
           atomic_is_lock_free(&diagnostics->near_overruns) &&
           atomic_is_lock_free(&diagnostics->counter_frequency) &&
           atomic_is_lock_free(&diagnostics->sample_rate) &&
           atomic_is_lock_free(&diagnostics->device_buffer_frames) &&
           atomic_is_lock_free(&diagnostics->active_configuration);
}

void ts_realtime_diagnostics_record(TsRealtimeDiagnostics *diagnostics,
                                    uint64_t elapsed_ticks,
                                    uint64_t counter_frequency,
                                    uint32_t sample_rate,
                                    uint32_t device_buffer_frames,
                                    uint32_t active_configuration)
{
    uint64_t observed;
    uint64_t deadline;
    if (diagnostics == NULL || counter_frequency == 0u || sample_rate == 0u ||
        device_buffer_frames == 0u) return;
    atomic_fetch_add_explicit(&diagnostics->callback_count, 1u,
                              memory_order_relaxed);
    atomic_fetch_add_explicit(&diagnostics->frame_count, device_buffer_frames,
                              memory_order_relaxed);
    atomic_fetch_add_explicit(&diagnostics->elapsed_ticks, elapsed_ticks,
                              memory_order_relaxed);
    observed = atomic_load_explicit(&diagnostics->worst_ticks,
                                    memory_order_relaxed);
    while (elapsed_ticks > observed &&
           !atomic_compare_exchange_weak_explicit(
               &diagnostics->worst_ticks, &observed, elapsed_ticks,
               memory_order_relaxed, memory_order_relaxed)) {
    }
    /* Divide first to keep the callback arithmetic bounded on long uptimes. */
    deadline = (counter_frequency / sample_rate) * device_buffer_frames;
    deadline += (counter_frequency % sample_rate) * device_buffer_frames /
                sample_rate;
    if (deadline > 0u) {
        if (elapsed_ticks >= deadline)
            atomic_fetch_add_explicit(&diagnostics->deadline_overruns, 1u,
                                      memory_order_relaxed);
        else if (elapsed_ticks >= deadline - deadline / 10u)
            atomic_fetch_add_explicit(&diagnostics->near_overruns, 1u,
                                      memory_order_relaxed);
    }
    atomic_store_explicit(&diagnostics->counter_frequency, counter_frequency,
                          memory_order_relaxed);
    atomic_store_explicit(&diagnostics->sample_rate, sample_rate,
                          memory_order_relaxed);
    atomic_store_explicit(&diagnostics->device_buffer_frames,
                          device_buffer_frames, memory_order_relaxed);
    atomic_store_explicit(&diagnostics->active_configuration,
                          active_configuration, memory_order_relaxed);
}

int ts_realtime_diagnostics_get(const TsRealtimeDiagnostics *diagnostics,
                                TsRealtimeDiagnosticsSnapshot *snapshot)
{
    double tick_microseconds;
    if (diagnostics == NULL || snapshot == NULL) return 0;
    snapshot->callback_count = atomic_load_explicit(
        &diagnostics->callback_count, memory_order_relaxed);
    snapshot->frame_count = atomic_load_explicit(
        &diagnostics->frame_count, memory_order_relaxed);
    snapshot->elapsed_ticks = atomic_load_explicit(
        &diagnostics->elapsed_ticks, memory_order_relaxed);
    snapshot->worst_ticks = atomic_load_explicit(
        &diagnostics->worst_ticks, memory_order_relaxed);
    snapshot->deadline_overruns = atomic_load_explicit(
        &diagnostics->deadline_overruns, memory_order_relaxed);
    snapshot->near_overruns = atomic_load_explicit(
        &diagnostics->near_overruns, memory_order_relaxed);
    snapshot->counter_frequency = atomic_load_explicit(
        &diagnostics->counter_frequency, memory_order_relaxed);
    snapshot->sample_rate = atomic_load_explicit(
        &diagnostics->sample_rate, memory_order_relaxed);
    snapshot->device_buffer_frames = atomic_load_explicit(
        &diagnostics->device_buffer_frames, memory_order_relaxed);
    snapshot->active_configuration = atomic_load_explicit(
        &diagnostics->active_configuration, memory_order_relaxed);
    tick_microseconds = snapshot->counter_frequency > 0u ?
        1000000.0 / (double)snapshot->counter_frequency : 0.0;
    snapshot->average_microseconds = snapshot->callback_count > 0u ?
        (double)snapshot->elapsed_ticks * tick_microseconds /
        (double)snapshot->callback_count : 0.0;
    snapshot->worst_microseconds =
        (double)snapshot->worst_ticks * tick_microseconds;
    snapshot->deadline_microseconds = snapshot->sample_rate > 0u ?
        (double)snapshot->device_buffer_frames * 1000000.0 /
        (double)snapshot->sample_rate : 0.0;
    return 1;
}
