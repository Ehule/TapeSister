#include "tapesister/realtime_diagnostics.h"

#include <stddef.h>

#define COUNTERS(X) \
    X(callback_count) X(frame_count) X(elapsed_ticks) X(worst_ticks) \
    X(deadline_overruns) X(near_overruns) X(counter_frequency) \
    X(sample_rate) X(device_buffer_frames) X(active_configuration) \
    X(recent_elapsed_ticks) X(recent_worst_ticks) X(recent_frame_count) \
    X(recent_callback_count) X(recent_frequency) X(recent_rate) \
    X(recent_revision) X(start_gap_ticks) X(delayed_starts) \
    X(control_count) X(control_ticks) X(control_worst_ticks) \
    X(control_wait_worst_ticks) X(control_long_holds) X(control_worst_site) \
    X(stream_generation)

static void reset_window(TsRealtimeDiagnostics *d)
{
    d->previous_start = d->previous_period = d->previous_frequency = 0;
    d->previous_rate = d->observed_generation = 0;
    d->window_ticks = d->window_worst = d->window_frames = d->window_callbacks = 0;
}

void ts_realtime_diagnostics_init(TsRealtimeDiagnostics *d)
{
    if (!d) return;
#define INIT(field) atomic_init(&d->field, 0);
    COUNTERS(INIT)
#undef INIT
    reset_window(d);
}

void ts_realtime_diagnostics_reset(TsRealtimeDiagnostics *d)
{
    if (!d) return;
#define RESET(field) atomic_store_explicit(&d->field, 0, memory_order_relaxed);
    COUNTERS(RESET)
#undef RESET
    reset_window(d);
}

int ts_realtime_diagnostics_is_lock_free(const TsRealtimeDiagnostics *d)
{
    if (!d) return 0;
#define CHECK(field) if (!atomic_is_lock_free(&d->field)) return 0;
    COUNTERS(CHECK)
#undef CHECK
    return 1;
}

void ts_realtime_diagnostics_discontinuity(TsRealtimeDiagnostics *d)
{
    if (d) atomic_fetch_add_explicit(&d->stream_generation, 1, memory_order_relaxed);
}

static uint64_t period_ticks(uint64_t frequency, uint32_t rate, uint32_t frames)
{
    return rate ? (frequency / rate) * frames + (frequency % rate) * frames / rate : 0;
}

/* Each maximum has one writer (audio callback, or serialized device-lock
   holder). Readers only load; reset requires writer exclusion. No retry loops. */
static void maximum(atomic_uint_least64_t *value, uint64_t sample)
{
    if (sample > atomic_load_explicit(value, memory_order_relaxed))
        atomic_store_explicit(value, sample, memory_order_relaxed);
}

void ts_realtime_diagnostics_record(TsRealtimeDiagnostics *d,
    uint64_t elapsed, uint64_t frequency, uint32_t rate, uint32_t frames,
    uint32_t configuration)
{
    if (!d || !frequency || !rate || !frames) return;
    atomic_fetch_add_explicit(&d->callback_count, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&d->frame_count, frames, memory_order_relaxed);
    atomic_fetch_add_explicit(&d->elapsed_ticks, elapsed, memory_order_relaxed);
    maximum(&d->worst_ticks, elapsed);
    uint64_t deadline = period_ticks(frequency, rate, frames);
    if (deadline) {
        if (elapsed >= deadline)
            atomic_fetch_add_explicit(&d->deadline_overruns, 1, memory_order_relaxed);
        else if (elapsed >= deadline - deadline / 10)
            atomic_fetch_add_explicit(&d->near_overruns, 1, memory_order_relaxed);
    }
    atomic_store_explicit(&d->counter_frequency, frequency, memory_order_relaxed);
    atomic_store_explicit(&d->sample_rate, rate, memory_order_relaxed);
    atomic_store_explicit(&d->device_buffer_frames, frames, memory_order_relaxed);
    atomic_store_explicit(&d->active_configuration, configuration, memory_order_relaxed);
}

void ts_realtime_diagnostics_record_timed(TsRealtimeDiagnostics *d,
    uint64_t started, uint64_t elapsed, uint64_t frequency, uint32_t rate,
    uint32_t frames, uint32_t configuration)
{
    if (!d || !frequency || !rate || !frames) return;
    uint32_t generation = atomic_load_explicit(&d->stream_generation, memory_order_relaxed);
    if (generation != d->observed_generation || frequency != d->previous_frequency ||
        rate != d->previous_rate || started < d->previous_start) {
        reset_window(d);
        atomic_fetch_add(&d->recent_revision, 1);
        atomic_store(&d->recent_callback_count, 0);
        atomic_fetch_add(&d->recent_revision, 1);
        d->observed_generation = generation;
    }
    if (d->previous_period && started >= d->previous_start) {
        uint64_t interval = started - d->previous_start;
        if (interval > d->previous_period) {
            uint64_t excess = interval - d->previous_period;
            maximum(&d->start_gap_ticks, excess);
            uint64_t tolerance = d->previous_period / 4;
            if (tolerance < frequency / 1000) tolerance = frequency / 1000;
            if (excess > tolerance)
                atomic_fetch_add_explicit(&d->delayed_starts, 1, memory_order_relaxed);
        }
    }
    d->previous_start = started;
    d->previous_period = period_ticks(frequency, rate, frames);
    d->previous_frequency = frequency;
    d->previous_rate = rate;
    ts_realtime_diagnostics_record(d, elapsed, frequency, rate, frames, configuration);
    d->window_ticks += elapsed;
    d->window_frames += frames;
    ++d->window_callbacks;
    if (elapsed > d->window_worst) d->window_worst = elapsed;
    /* A quarter second of audio per publication, independent of buffer size. */
    if (d->window_frames >= rate / 4u) {
        atomic_fetch_add(&d->recent_revision, 1);
        atomic_store(&d->recent_elapsed_ticks, d->window_ticks);
        atomic_store(&d->recent_worst_ticks, d->window_worst);
        atomic_store(&d->recent_frame_count, d->window_frames);
        atomic_store(&d->recent_callback_count, d->window_callbacks);
        atomic_store(&d->recent_frequency, frequency);
        atomic_store(&d->recent_rate, rate);
        atomic_fetch_add(&d->recent_revision, 1);
        d->window_ticks = d->window_frames = d->window_worst = d->window_callbacks = 0;
    }
}

void ts_realtime_diagnostics_record_control(TsRealtimeDiagnostics *d,
    uint64_t hold, uint64_t wait, uint64_t frequency, const char *site)
{
    if (!d || !frequency) return;
    atomic_fetch_add_explicit(&d->control_count, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&d->control_ticks, hold, memory_order_relaxed);
    if (hold > atomic_load_explicit(&d->control_worst_ticks, memory_order_relaxed)) {
        atomic_store_explicit(&d->control_worst_site, (uintptr_t)site, memory_order_relaxed);
        atomic_store_explicit(&d->control_worst_ticks, hold, memory_order_relaxed);
    }
    maximum(&d->control_wait_worst_ticks, wait);
    uint32_t rate = atomic_load_explicit(&d->sample_rate, memory_order_relaxed);
    uint32_t frames = atomic_load_explicit(&d->device_buffer_frames, memory_order_relaxed);
    uint64_t budget = period_ticks(frequency, rate, frames);
    if (budget && hold >= budget)
        atomic_fetch_add_explicit(&d->control_long_holds, 1, memory_order_relaxed);
}

int ts_realtime_diagnostics_get(const TsRealtimeDiagnostics *d,
    TsRealtimeDiagnosticsSnapshot *s)
{
    if (!d || !s) return 0;
#define READ(field) s->field = atomic_load_explicit(&d->field, memory_order_relaxed)
    READ(callback_count); READ(frame_count); READ(elapsed_ticks); READ(worst_ticks);
    READ(deadline_overruns); READ(near_overruns); READ(counter_frequency);
    READ(sample_rate); READ(device_buffer_frames); READ(active_configuration);
    READ(delayed_starts); READ(control_count); READ(control_long_holds);
#undef READ
    double us = s->counter_frequency ? 1000000.0 / (double)s->counter_frequency : 0;
    s->average_microseconds = s->callback_count ? s->elapsed_ticks * us / s->callback_count : 0;
    s->worst_microseconds = s->worst_ticks * us;
    s->deadline_microseconds = s->sample_rate ? s->device_buffer_frames * 1000000.0 / s->sample_rate : 0;
    uint64_t recent_ticks = 0, recent_frames = 0, recent_worst = 0, recent_frequency = 0;
    uint32_t recent_rate = 0;
    s->recent_callback_count = 0;
    /* UI reader: at most three attempts; never block the audio writer. */
    for (int attempt = 0; attempt < 3; ++attempt) {
        uint32_t before = atomic_load(&d->recent_revision);
        if (before & 1u) continue;
        uint64_t ticks = atomic_load(&d->recent_elapsed_ticks);
        uint64_t frames = atomic_load(&d->recent_frame_count);
        uint64_t worst = atomic_load(&d->recent_worst_ticks);
        uint64_t callbacks = atomic_load(&d->recent_callback_count);
        uint64_t frequency = atomic_load(&d->recent_frequency);
        uint32_t rate = atomic_load(&d->recent_rate);
        if (before == atomic_load(&d->recent_revision)) {
            recent_ticks = ticks; recent_frames = frames; recent_worst = worst;
            s->recent_callback_count = callbacks;
            recent_frequency = frequency; recent_rate = rate;
            break;
        }
    }
    double recent_us = recent_frequency ? 1000000.0 / recent_frequency : 0;
    s->recent_average_microseconds = s->recent_callback_count ? recent_ticks * recent_us / s->recent_callback_count : 0;
    s->recent_worst_microseconds = s->recent_callback_count ? recent_worst * recent_us : 0;
    s->recent_load_percent = s->recent_callback_count && recent_frames && recent_frequency ?
        100.0 * ((double)recent_ticks / recent_frequency) * recent_rate / recent_frames : 0;
    s->start_gap_microseconds = atomic_load_explicit(&d->start_gap_ticks, memory_order_relaxed) * us;
    s->control_worst_microseconds = atomic_load_explicit(&d->control_worst_ticks, memory_order_relaxed) * us;
    s->control_wait_worst_microseconds = atomic_load_explicit(&d->control_wait_worst_ticks, memory_order_relaxed) * us;
    s->control_worst_site = (const char *)atomic_load_explicit(&d->control_worst_site, memory_order_relaxed);
    return 1;
}
