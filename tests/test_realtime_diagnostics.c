#include "tapesister/realtime_diagnostics.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void timed_windows(void)
{
    const unsigned rates[] = {44100, 48000, 96000};
    for (unsigned r = 0; r < 3; ++r) {
        TsRealtimeDiagnostics d; TsRealtimeDiagnosticsSnapshot s;
        ts_realtime_diagnostics_init(&d);
        unsigned rate = rates[r], frames = rate / 100;
        for (unsigned i = 0; i < 25; ++i)
            ts_realtime_diagnostics_record_timed(&d, i * 10000u, 2500, 1000000, rate, frames, 0);
        assert(ts_realtime_diagnostics_get(&d, &s));
        assert(s.recent_callback_count == 25 && s.delayed_starts == 0);
        assert(fabs(s.recent_load_percent - 25) < .001);
        assert(s.recent_average_microseconds == 2500 && s.recent_worst_microseconds == 2500);
        ts_realtime_diagnostics_record_control(&d, 1000, 50000, 1000000, "short edit");
        ts_realtime_diagnostics_record_control(&d, 12000, 100, 1000000, "long edit");
        ts_realtime_diagnostics_get(&d, &s);
        assert(s.control_count == 2 && s.control_long_holds == 1);
        assert(s.control_worst_microseconds == 12000 && s.control_wait_worst_microseconds == 50000);
        assert(!strcmp(s.control_worst_site, "long edit"));
        ts_realtime_diagnostics_record_timed(&d, 270000, 500, 1000000, rate, frames, 0);
        ts_realtime_diagnostics_get(&d, &s);
        assert(s.delayed_starts == 1 && s.start_gap_microseconds == 20000);
        /* Intentional pauses/reopenings and backwards clocks must not become stalls. */
        ts_realtime_diagnostics_discontinuity(&d);
        ts_realtime_diagnostics_record_timed(&d, 10000000, 500, 1000000, rate, frames, 0);
        ts_realtime_diagnostics_record_timed(&d, 1, 500, 1000000, rate, frames, 0);
        ts_realtime_diagnostics_get(&d, &s);
        assert(s.delayed_starts == 1 && s.recent_callback_count == 0);
        ts_realtime_diagnostics_reset(&d);
        ts_realtime_diagnostics_get(&d, &s);
        assert(!s.callback_count && !s.control_count && !s.delayed_starts && !s.control_worst_site);
        assert(!s.recent_callback_count && s.recent_load_percent == 0);
    }
}

static void cadence_edges(void)
{
    TsRealtimeDiagnostics d; TsRealtimeDiagnosticsSnapshot s;
    ts_realtime_diagnostics_init(&d);
    /* Varying block sizes use the previous block's duration for arrival. Tick
       zero is a valid first callback; 25% excess is tolerated, >25% is counted. */
    ts_realtime_diagnostics_record_timed(&d, 0, 10, 1000000, 48000, 480, 0);
    ts_realtime_diagnostics_record_timed(&d, 12500, 10, 1000000, 48000, 960, 0);
    ts_realtime_diagnostics_record_timed(&d, 37501, 10, 1000000, 48000, 480, 0);
    ts_realtime_diagnostics_get(&d, &s);
    assert(s.delayed_starts == 1 && s.start_gap_microseconds == 5001);
    ts_realtime_diagnostics_record_timed(&d, 999999, 10, 1000000, 96000, 480, 0);
    ts_realtime_diagnostics_get(&d, &s);
    assert(s.delayed_starts == 1 && !s.recent_callback_count);
    ts_realtime_diagnostics_record_timed(&d, 999999, 10, 0, 96000, 480, 0);
    ts_realtime_diagnostics_record_timed(&d, 999999, 10, 1000000, 0, 480, 0);
    ts_realtime_diagnostics_record_timed(&d, 999999, 10, 1000000, 96000, 0, 0);
    ts_realtime_diagnostics_get(&d, &s);
    assert(s.callback_count == 4);
    assert(!ts_realtime_diagnostics_get(NULL, &s));
    assert(!ts_realtime_diagnostics_get(&d, NULL));
}

int main(void)
{
    timed_windows();
    cadence_edges();
    TsRealtimeDiagnostics diagnostics;
    TsRealtimeDiagnosticsSnapshot snapshot;
    ts_realtime_diagnostics_init(&diagnostics);
    assert(ts_realtime_diagnostics_is_lock_free(&diagnostics) == 0 ||
           ts_realtime_diagnostics_is_lock_free(&diagnostics) == 1);
    ts_realtime_diagnostics_record(&diagnostics, 100u, 1000000u,
                                   48000u, 480u,
                                   TS_RT_CONFIG_SISTER | TS_RT_CONFIG_H1);
    ts_realtime_diagnostics_record(&diagnostics, 9500u, 1000000u,
                                   48000u, 480u,
                                   TS_RT_CONFIG_SISTER | TS_RT_CONFIG_H1);
    ts_realtime_diagnostics_record(&diagnostics, 10000u, 1000000u,
                                   48000u, 480u,
                                   TS_RT_CONFIG_SISTER | TS_RT_CONFIG_H1);
    assert(ts_realtime_diagnostics_get(&diagnostics, &snapshot));
    assert(snapshot.callback_count == 3u);
    assert(snapshot.frame_count == 1440u);
    assert(snapshot.elapsed_ticks == 19600u);
    assert(snapshot.worst_ticks == 10000u);
    assert(snapshot.near_overruns == 1u);
    assert(snapshot.deadline_overruns == 1u);
    assert(snapshot.sample_rate == 48000u);
    assert(snapshot.device_buffer_frames == 480u);
    assert(snapshot.active_configuration ==
           (TS_RT_CONFIG_SISTER | TS_RT_CONFIG_H1));
    assert(fabs(snapshot.average_microseconds - 6533.333333) < 0.01);
    assert(fabs(snapshot.worst_microseconds - 10000.0) < 0.01);
    assert(fabs(snapshot.deadline_microseconds - 10000.0) < 0.01);
    puts("realtime diagnostic counter tests passed");
    return 0;
}
