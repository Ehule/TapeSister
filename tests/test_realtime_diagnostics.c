#include "tapesister/realtime_diagnostics.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

int main(void)
{
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
