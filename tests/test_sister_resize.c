#include "sister_test_helpers.h"

#include <stdio.h>

static double machine_head_age(const TsSisterMachine *machine, size_t head)
{
    double write = (double)(machine->master_clock %
                            machine->buffer.storage_frames);
    return ts_sister_positive_modulo(write - machine->head[head].phase,
                                     machine->buffer.storage_frames);
}

static void process_silence(TsSisterMachine *machine, size_t frames)
{
    TsStereoFrame silence = sister_silence();
    for (size_t i = 0u; i < frames; ++i)
        (void)ts_sister_machine_process_frame(machine, silence, silence);
}

static void fixed_store_and_rate_scaling(void)
{
    static const uint32_t rates[] = {44100u, 48000u, 96000u};
    for (size_t i = 0u; i < sizeof(rates) / sizeof(rates[0]); ++i) {
        TsSisterBuffer buffer;
        assert(ts_sister_buffer_init(&buffer, rates[i], 2u, 5.0));
        assert(buffer.capacity_frames == (size_t)rates[i] * 5u);
        assert(buffer.storage_frames == (size_t)rates[i] * 60u + 2u);
        assert(buffer.valid_history_frames == buffer.capacity_frames);
        ts_sister_buffer_free(&buffer);
    }
}

static void grow_shrink_and_coalescing(void)
{
    TsSisterMachine machine;
    TsSisterParameters parameters;
    TsSisterSnapshot snapshot;
    float *store;
    double h2_age;
    double weave_phase;
    assert(ts_sister_machine_init(&machine, 1000u, 2u, 10.0));
    store = machine.buffer.data;
    ts_sister_parameters_default(&parameters, 1000u);
    parameters.buffer_seconds = 10.0f;
    parameters.head1_level = 0.0f;
    parameters.head2_level = 1.0f;
    parameters.head2_rate_index = 7;
    parameters.head3_level = 1.0f;
    parameters.head3_rate_index = 7;
    parameters.soak = 0.4f;
    sister_configure_immediate(&machine, &parameters);
    ts_sister_machine_set_rolling(&machine, 0);
    machine.head[1].phase = ts_sister_positive_modulo(-3000.25,
                                                       machine.buffer.storage_frames);
    machine.head[2].phase = ts_sister_positive_modulo(-8000.5,
                                                       machine.buffer.storage_frames);
    process_silence(&machine, 1u);
    h2_age = machine_head_age(&machine, 1u);
    weave_phase = machine.soak_weave[0].phase;

    assert(ts_sister_machine_request_duration(&machine, 15.0));
    process_silence(&machine, 24u);
    assert(machine.buffer.capacity_frames == 10000u);
    process_silence(&machine, 1u);
    assert(machine.buffer.capacity_frames == 15000u);
    assert(machine.buffer.data == store);
    assert(fabs(machine_head_age(&machine, 1u) - h2_age) < 0.001);
    assert(machine.buffer.valid_history_frames > 10000u &&
           machine.buffer.valid_history_frames < 15000u);
    assert(machine.soak_weave[0].phase != weave_phase);

    assert(ts_sister_machine_request_duration(&machine, 6.0));
    process_silence(&machine, 25u);
    assert(machine.buffer.capacity_frames == 6000u);
    assert(machine.buffer.valid_history_frames == 6000u);
    assert(machine_head_age(&machine, 1u) < 5999.001);
    assert(machine_head_age(&machine, 2u) < 5999.001);
    assert(machine.head[2].guard_total == 15u);
    assert(sister_frame_finite(machine.last_output.mix));

    assert(ts_sister_machine_request_duration(&machine, 10.0));
    process_silence(&machine, 25u);
    assert(machine.buffer.capacity_frames == 10000u);
    assert(machine.buffer.valid_history_frames > 6000u &&
           machine.buffer.valid_history_frames < 6100u);

    assert(ts_sister_machine_request_duration(&machine, 12.0));
    assert(ts_sister_machine_request_duration(&machine, 20.0));
    assert(ts_sister_machine_request_duration(&machine, 7.0));
    process_silence(&machine, 24u);
    assert(machine.buffer.capacity_frames == 10000u);
    process_silence(&machine, 1u);
    assert(machine.buffer.capacity_frames == 7000u);
    assert(machine.buffer.data == store);
    assert(ts_sister_machine_get_snapshot(&machine, &snapshot));
    assert(!snapshot.resize_pending);
    assert(fabs(snapshot.duration_seconds - 7.0) < 0.001);
    assert(fabs(snapshot.target_duration_seconds - 7.0) < 0.001);
    ts_sister_machine_free(&machine);
}

static void bounds_and_transport_states(void)
{
    TsSisterMachine machine;
    uint64_t clock;
    assert(ts_sister_machine_init(&machine, 1000u, 1u, 8.0));
    ts_sister_machine_set_hold(&machine, 1);
    ts_sister_machine_set_rolling(&machine, 0);
    clock = machine.master_clock;
    assert(ts_sister_machine_request_duration(&machine, -100.0));
    process_silence(&machine, 25u);
    assert(machine.buffer.capacity_frames == 5000u);
    assert(machine.held && !machine.rolling);
    assert(machine.master_clock == clock + 25u);
    assert(ts_sister_machine_request_duration(&machine, 1000.0));
    process_silence(&machine, 25u);
    assert(machine.buffer.capacity_frames == 60000u);
    assert(machine.held && !machine.rolling);
    assert(sister_frame_finite(machine.last_output.mix));
    ts_sister_machine_free(&machine);
}

static void rapid_random_full_graph_stress(void)
{
    TsSisterRuntime runtime;
    TsSisterParameters parameters;
    TsSisterSourceFrames source = {0};
    TsSisterRuntimeFrame frame;
    float *rolling_store;
    float *delay_store;
    uint32_t random = UINT32_C(0x6d2b79f5);
    assert(sister_test_enable(&runtime, 1000u, 2u, 5.0));
    parameters = runtime.parameters;
    parameters.buffer_seconds = 5.0f;
    parameters.head1_level = parameters.head2_level = parameters.head3_level = 0.7f;
    parameters.head1_feedback = parameters.head2_feedback = 0.8f;
    parameters.soak = 0.65f;
    parameters.soak_targets = TS_SISTER_EFFECT_TARGET_H1 |
                              TS_SISTER_EFFECT_TARGET_H3;
    parameters.fx.distortion_drive = 0.9f;
    parameters.fx.distortion_mix = 0.75f;
    parameters.fx.distortion_targets = TS_SISTER_EFFECT_TARGET_H1;
    parameters.fx.delay_feedback = 0.95f;
    parameters.fx.delay_mix = 0.7f;
    parameters.fx.delay_targets = TS_SISTER_EFFECT_TARGET_H2;
    parameters.fx.reverb_mix = 0.7f;
    parameters.fx.reverb_decay = 0.9f;
    parameters.fx.reverb_targets = TS_SISTER_EFFECT_TARGET_H3;
    parameters.fx.master_feedback = 0.8f;
    ts_sister_runtime_set_parameters(&runtime, &parameters);
    ts_sister_runtime_set_sources(&runtime, TS_SISTER_SOURCE_FM);
    rolling_store = runtime.machine.buffer.data;
    delay_store = runtime.post_fx.delay[1].data;
    for (size_t i = 0u; i < 200000u; ++i) {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        source.fm.l = (float)(int32_t)random / (float)INT32_MAX * 0.2f;
        source.fm.r = -source.fm.l * 0.7f;
        if (i % 101u == 0u) {
            parameters = runtime.parameters;
            parameters.buffer_seconds = 5.0f + (float)(random % 56u);
            ts_sister_runtime_set_parameters(&runtime, &parameters);
        }
        if (i % 997u == 0u) {
            ts_sister_runtime_set_hold(&runtime, !runtime.held);
            ts_sister_runtime_set_rolling(&runtime, !runtime.rolling);
        }
        frame = ts_sister_runtime_process_frame(&runtime, &source);
        assert(sister_frame_finite(frame.tap[TS_SISTER_TAP_MIX]));
        assert(sister_frame_finite(runtime.master_feedback_previous));
        assert(runtime.machine.buffer.capacity_frames >= 5000u &&
               runtime.machine.buffer.capacity_frames <= 60000u);
        assert(runtime.machine.buffer.data == rolling_store);
        assert(runtime.post_fx.delay[1].data == delay_store);
    }
    assert(runtime.post_fx.delay[1].has_history);
    assert(runtime.post_fx.reverb[2].has_history);
    ts_sister_runtime_free(&runtime);
}

static void capture_and_note_continuity(void)
{
    TsSisterRuntime runtime;
    TsInstrument instrument;
    TsNoteEvent note;
    TsSisterParameters parameters;
    char error[160];
    assert(sister_test_make_tiles(&instrument, 1, 1, 1000u, 512u));
    assert(sister_test_enable(&runtime, 1000u, 2u, 5.0));
    parameters = runtime.parameters;
    parameters.buffer_seconds = 5.0f;
    parameters.head1_level = 1.0f;
    parameters.head1_time_ms = 1.0f;
    ts_sister_runtime_set_parameters(&runtime, &parameters);
    ts_sister_runtime_set_sources(&runtime, TS_SISTER_SOURCE_TILES);
    assert(ts_sister_runtime_set_source_slot(&runtime, &instrument, 0, 1));
    assert(ts_note_event_midi(&note, 60, 100, 1));
    assert(ts_sister_runtime_note_on(&runtime, &instrument, &note, 0, 1000) == 1);
    assert(ts_sister_runtime_arm_capture(
        &runtime, &instrument, 1, 200u, 1000u, 2u,
        TS_SISTER_TAP_H1, 0u, error, sizeof(error)));
    assert(ts_sister_runtime_trigger_capture(&runtime, error, sizeof(error)));
    for (size_t i = 0u; i < 200u; ++i) {
        if (i == 20u) {
            parameters = runtime.parameters;
            parameters.buffer_seconds = 6.0f;
            ts_sister_runtime_set_parameters(&runtime, &parameters);
        }
        (void)ts_sister_runtime_process_frame(&runtime, NULL);
    }
    assert(atomic_load_explicit(&runtime.capture.state, memory_order_relaxed) ==
           TS_CAPTURE_COMPLETED);
    assert(runtime.capture.recorded_frames == 200u);
    assert(runtime.machine.buffer.capacity_frames == 6000u);
    assert(ts_performance_count(&runtime.performance) > 0);
    ts_sister_runtime_note_off(&runtime, &note);
    ts_sister_runtime_free(&runtime);
    ts_instrument_free(&instrument);
}

int main(void)
{
    fixed_store_and_rate_scaling();
    grow_shrink_and_coalescing();
    bounds_and_transport_states();
    rapid_random_full_graph_stress();
    capture_and_note_continuity();
    puts("sister live-buffer resize tests passed");
    return 0;
}
