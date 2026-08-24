#include "sister_test_helpers.h"

#include <stdio.h>

static TsSisterParameters feedback_parameters(int head2)
{
    TsSisterParameters parameters;
    ts_sister_parameters_default(&parameters, 1000u);
    parameters.head1_level = head2 ? 0.0f : 1.0f;
    parameters.head1_time_ms = 10.0f;
    parameters.head1_feedback = head2 ? 0.0f : 0.5f;
    parameters.head2_level = head2 ? 1.0f : 0.0f;
    parameters.head2_scrub = 0.10f;
    parameters.head2_rate_index = 7;
    parameters.head2_feedback = head2 ? 0.5f : 0.0f;
    parameters.head3_level = 0.0f;
    parameters.headroom = 1.0f;
    return parameters;
}

static void impulse_decay(int head2)
{
    TsSisterMachine machine;
    TsSisterParameters parameters = feedback_parameters(head2);
    TsSisterOutput output;
    float first_echo = 0.0f;
    float second_echo = 0.0f;
    size_t i;
    assert(ts_sister_machine_init(&machine, 1000u, 2u, 0.100));
    sister_configure_immediate(&machine, &parameters);
    for (i = 0u; i < 40u; ++i) {
        TsStereoFrame input = i == 0u ? (TsStereoFrame){0.8f, -0.4f}
                                      : sister_silence();
        output = ts_sister_machine_process_frame(&machine, input, sister_silence());
        if (i >= 9u && i <= 11u) {
            float value = head2 ? output.head[1].l : output.head[0].l;
            if (fabsf(value) > fabsf(first_echo)) first_echo = value;
        }
        if (i >= 19u && i <= 22u) {
            float value = head2 ? output.head[1].l : output.head[0].l;
            if (fabsf(value) > fabsf(second_echo)) second_echo = value;
        }
    }
    assert(fabsf(first_echo) > 0.1f);
    assert(fabsf(second_echo) > 0.01f);
    assert(fabsf(second_echo) < fabsf(first_echo));
    ts_sister_machine_free(&machine);
}

static void clipping_saturation_and_dc(void)
{
    TsSisterMachine machine;
    TsSisterParameters parameters;
    TsSisterOutput output;
    size_t i;
    size_t written;
    assert(ts_sister_machine_init(&machine, 1000u, 2u, 0.100));
    parameters = feedback_parameters(0);
    parameters.head1_feedback = 1.0f;
    parameters.head2_level = 1.0f;
    parameters.head2_feedback = 1.0f;
    parameters.head2_scrub = 0.02f;
    sister_configure_immediate(&machine, &parameters);
    for (i = 0u; i < machine.buffer.capacity_frames; ++i)
        assert(ts_sister_buffer_write(&machine.buffer, i,
                                      (TsStereoFrame){10.0f, -10.0f}));
    output = ts_sister_machine_process_frame(&machine, sister_silence(), sister_silence());
    assert(machine.overload_count > 0u);
    assert(fabsf(machine.buffer.data[0]) <= 1.5f);
    assert(fabsf(machine.buffer.data[1]) <= 1.5f);
    assert(sister_frame_finite(output.mix));

    parameters.head1_feedback = 0.0f;
    parameters.head2_feedback = 0.0f;
    parameters.head1_level = parameters.head2_level = 0.0f;
    sister_configure_immediate(&machine, &parameters);
    for (i = 0u; i < 5000u; ++i)
        output = ts_sister_machine_process_frame(&machine,
                                                 (TsStereoFrame){0.5f, 0.5f},
                                                 sister_silence());
    written = (size_t)((machine.master_clock - 1u) % machine.buffer.capacity_frames);
    assert(fabsf(machine.buffer.data[written * 2u]) < 0.001f);
    assert(sister_frame_finite(output.mix));
    ts_sister_machine_free(&machine);
}

static void unity_stability_and_nonfinite_recovery(void)
{
    TsSisterMachine machine;
    TsSisterParameters parameters = feedback_parameters(0);
    TsSisterOutput output;
    size_t i;
    parameters.head1_time_ms = 1.0f;
    parameters.head1_feedback = 1.0f;
    assert(ts_sister_machine_init(&machine, 1000u, 2u, 0.020));
    sister_configure_immediate(&machine, &parameters);
    output = ts_sister_machine_process_frame(
        &machine, (TsStereoFrame){INFINITY, NAN}, (TsStereoFrame){NAN, INFINITY});
    assert(sister_frame_finite(output.mix));
    for (i = 0u; i < 100000u; ++i) {
        TsStereoFrame input = i == 0u ? (TsStereoFrame){1.0f, -1.0f}
                                      : sister_silence();
        output = ts_sister_machine_process_frame(&machine, input, sister_silence());
        assert(sister_frame_finite(output.head[0]));
        assert(sister_frame_finite(output.mix));
    }
    for (i = 0u; i < machine.buffer.capacity_frames * 2u; ++i)
        assert(isfinite(machine.buffer.data[i]));
    assert(machine.overload_count > 0u);
    ts_sister_machine_free(&machine);
}

static void write_erase_ghosting(void)
{
    TsSisterMachine machine;
    TsSisterParameters parameters;
    TsSisterOutput output;

    assert(ts_sister_machine_init(&machine, 1000u, 2u, 0.020));
    ts_sister_parameters_default(&parameters, 1000u);
    parameters.head1_level = 0.0f;
    parameters.head2_level = 0.0f;
    parameters.head3_level = 0.0f;
    parameters.head1_feedback = 0.0f;
    parameters.head2_feedback = 0.0f;

    parameters.write_erase = 1.0f;
    sister_configure_immediate(&machine, &parameters);
    assert(ts_sister_buffer_write(&machine.buffer, 0u,
                                  (TsStereoFrame){0.5f, -0.25f}));
    machine.master_clock = machine.buffer.capacity_frames;
    output = ts_sister_machine_process_frame(
        &machine, sister_silence(), sister_silence());
    assert(fabsf(output.write.l) < 0.0001f);
    assert(fabsf(output.write.r) < 0.0001f);

    parameters.write_erase = 0.2f;
    sister_configure_immediate(&machine, &parameters);
    assert(ts_sister_buffer_write(&machine.buffer, 0u,
                                  (TsStereoFrame){0.5f, -0.25f}));
    machine.master_clock = machine.buffer.capacity_frames;
    output = ts_sister_machine_process_frame(
        &machine, sister_silence(), sister_silence());
    assert(output.write.l > 0.30f);
    assert(output.write.r < -0.15f);
    assert(fabsf(output.write.l) > fabsf(output.write.r));
    assert(sister_frame_finite(output.write));
    ts_sister_machine_free(&machine);
}

int main(void)
{
    impulse_decay(0);
    impulse_decay(1);
    clipping_saturation_and_dc();
    unity_stability_and_nonfinite_recovery();
    write_erase_ghosting();
    puts("sister feedback tests passed");
    return 0;
}
