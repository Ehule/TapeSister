#include "sister_test_helpers.h"

#include <stdio.h>

static void snapshot_metadata_and_revision(void)
{
    TsSisterMachine machine;
    TsSisterParameters parameters;
    TsSisterSnapshot snapshot;
    TsSisterSnapshot next;
    TsStereoFrame input[32];
    TsStereoFrame sidechain[32];
    TsSisterOutput output[32];
    size_t i;
    assert(ts_sister_machine_init(&machine, 2000u, 2u, 0.500));
    ts_sister_parameters_default(&parameters, 2000u);
    parameters.head1_level = parameters.head2_level = parameters.head3_level = 1.0f;
    parameters.head1_time_ms = 1.0f;
    parameters.head2_scrub = 0.25f;
    parameters.head3_span = 0.25f;
    parameters.headroom = 1.0f;
    parameters.duck_enabled = 1;
    parameters.duck_sensitivity = 0.5f;
    sister_configure_immediate(&machine, &parameters);
    for (i = 0u; i < machine.buffer.capacity_frames; ++i)
        assert(ts_sister_buffer_write(&machine.buffer, i,
                                      (TsStereoFrame){1.0f, 0.5f}));
    ts_sister_machine_set_rolling(&machine, 0);
    ts_sister_machine_set_hold(&machine, 1);
    for (i = 0u; i < 32u; ++i) {
        input[i] = sister_silence();
        sidechain[i] = (TsStereoFrame){1.0f, 0.0f};
    }
    ts_sister_machine_process_block(&machine, input, sidechain, output, 32u);
    assert(ts_sister_machine_get_snapshot(&machine, &snapshot));
    assert(snapshot.revision > 0u && (snapshot.revision & 1u) == 0u);
    assert(snapshot.master_clock == 32u);
    assert(snapshot.write_position == 32u);
    assert(snapshot.rolling == 0 && snapshot.held == 1);
    assert(snapshot.clear_state == TS_SISTER_CLEAR_IDLE);
    assert(snapshot.buffer_channels == 2u);
    assert(snapshot.sample_rate == 2000u);
    assert(fabs(snapshot.duration_seconds - 0.5) < 1e-12);
    assert(snapshot.write_normalized >= 0.0f && snapshot.write_normalized < 1.0f);
    assert(snapshot.duck_gain < 1.0f);
    assert(snapshot.overload_count > 0u);
    assert(snapshot.mix_peak <= 1.0f);
    for (i = 0u; i < TS_SISTER_HEAD_COUNT; ++i) {
        assert(snapshot.head_position[i] >= 0.0);
        assert(snapshot.head_position[i] < (double)machine.buffer.capacity_frames);
        assert(snapshot.head_normalized[i] >= 0.0f && snapshot.head_normalized[i] < 1.0f);
        assert(snapshot.head_peak[i] >= 0.0f);
        assert(sister_close(snapshot.head_normalized[i],
                            (float)(snapshot.head_position[i] /
                                    (double)machine.buffer.capacity_frames), 1e-6f));
    }
    ts_sister_machine_process_frame(&machine, sister_silence(), sister_silence());
    assert(ts_sister_machine_get_snapshot(&machine, &next));
    assert(next.revision > snapshot.revision);
    assert(next.master_clock == snapshot.master_clock + 1u);
    snapshot = next;
    ts_sister_machine_begin_audio_block(&machine);
    for (i = 0u; i < 64u; ++i)
        (void)ts_sister_machine_process_frame(
            &machine, sister_silence(), sister_silence());
    assert(ts_sister_machine_get_snapshot(&machine, &next));
    assert(next.revision == snapshot.revision);
    assert(next.master_clock == snapshot.master_clock);
    ts_sister_machine_end_audio_block(&machine);
    assert(ts_sister_machine_get_snapshot(&machine, &next));
    assert(next.revision == snapshot.revision + 2u);
    assert(next.master_clock == snapshot.master_clock + 64u);
    ts_sister_machine_free(&machine);
}

static void coherent_reads_and_transport_states(void)
{
    TsSisterMachine machine;
    TsSisterSnapshot snapshot;
    size_t i;
    assert(ts_sister_machine_init(&machine, 1000u, 1u, 0.010));
    for (i = 0u; i < 10000u; ++i) {
        ts_sister_machine_process_frame(&machine, sister_silence(), sister_silence());
        assert(ts_sister_machine_get_snapshot(&machine, &snapshot));
        assert(snapshot.write_position == snapshot.master_clock %
                                          machine.buffer.capacity_frames);
        assert(snapshot.buffer_channels == 1u);
        assert(snapshot.sample_rate == 1000u);
        assert((snapshot.revision & 1u) == 0u);
    }
    assert(ts_sister_machine_request_clear(&machine));
    ts_sister_machine_process_frame(&machine, sister_silence(), sister_silence());
    assert(ts_sister_machine_get_snapshot(&machine, &snapshot));
    assert(snapshot.clear_state == TS_SISTER_CLEAR_FADE_OUT ||
           snapshot.clear_state == TS_SISTER_CLEAR_WAITING);
    ts_sister_machine_free(&machine);
}

int main(void)
{
    snapshot_metadata_and_revision();
    coherent_reads_and_transport_states();
    puts("sister snapshot tests passed");
    return 0;
}
