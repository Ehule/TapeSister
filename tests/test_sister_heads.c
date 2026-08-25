#include "sister_test_helpers.h"

#include <stdio.h>

static void default_contract(void)
{
    TsSisterParameters parameters;
    ts_sister_parameters_default(&parameters, 48000u);
    assert(parameters.head1_level == 0.45f);
    assert(parameters.head1_time_ms == 500.0f);
    assert(parameters.head1_feedback == 0.25f);
    assert(parameters.head2_level == 0.0f && parameters.head2_scrub == 0.5f);
    assert(parameters.head2_rate_index == 7);
    assert(parameters.head3_level == 0.0f && parameters.head3_span == 0.5f);
    assert(parameters.head3_rate_index == 7);
    assert(parameters.wow == 0.0f && parameters.drop == 0.0f);
    assert(!parameters.duck_enabled && !parameters.decorrelation_enabled);
    assert(parameters.width == 1.0f);
    assert(parameters.filter_type == TS_SISTER_FILTER_BYPASS);
    assert(parameters.input_gain == 1.0f);
    assert(parameters.mix_output_gain == 1.0f);
    assert(parameters.clear_ms == 20.0f);
    ts_sister_parameters_kafka_start(&parameters, 48000u);
    assert(parameters.filter_type == TS_SISTER_FILTER_LOWPASS);
    assert(sister_close(parameters.filter_cutoff_hz, 9479.2939453125f, 1e-4f));
    assert(sister_close(parameters.filter_gain_db, 2.0f, 1e-6f));
    assert(sister_close(parameters.filter_q, 0.90030003f, 1e-6f));
}

static void h1_delay_for_rate(uint32_t sample_rate)
{
    TsSisterMachine machine;
    TsSisterParameters parameters;
    TsSisterOutput output;
    size_t write_position;
    double expected_position;
    float expected;
    assert(ts_sister_machine_init(&machine, sample_rate, 2u, 0.020));
    ts_sister_parameters_default(&parameters, sample_rate);
    parameters.head1_level = 1.0f;
    parameters.head1_time_ms = 5.0f;
    parameters.head1_feedback = 0.0f;
    parameters.head2_level = 0.0f;
    parameters.head3_level = 0.0f;
    sister_configure_immediate(&machine, &parameters);
    sister_fill_buffer(&machine, 1.0f, 0.5f);
    ts_sister_machine_set_rolling(&machine, 0);
    machine.master_clock = machine.buffer.capacity_frames / 2u;
    write_position = (size_t)(machine.master_clock % machine.buffer.capacity_frames);
    expected_position = (double)write_position - (double)sample_rate * 0.005;
    expected = (float)(expected_position / (double)machine.buffer.capacity_frames);
    output = ts_sister_machine_process_frame(&machine, sister_silence(), sister_silence());
    assert(sister_close(output.head[0].l, expected, 2e-4f));
    assert(sister_close(output.head[0].r, expected * 0.5f, 2e-4f));
    ts_sister_machine_free(&machine);
}

static void minimum_delay_and_jump_crossfade(void)
{
    TsSisterMachine machine;
    TsSisterParameters parameters;
    TsSisterOutput first;
    size_t write_position;
    float old_expected;
    assert(ts_sister_machine_init(&machine, 1000u, 2u, 0.100));
    ts_sister_parameters_default(&parameters, 1000u);
    parameters.head1_level = 1.0f;
    parameters.head1_time_ms = 0.0f;
    parameters.head2_level = parameters.head3_level = 0.0f;
    sister_configure_immediate(&machine, &parameters);
    assert(machine.head[0].current_delay_frames == 1.0);
    sister_fill_buffer(&machine, 1.0f, 1.0f);
    ts_sister_machine_set_rolling(&machine, 0);
    machine.master_clock = 50u;
    parameters.head1_time_ms = 20.0f;
    ts_sister_machine_set_parameters(&machine, &parameters);
    assert(machine.head[0].jump_remaining == 15u);
    write_position = 50u;
    old_expected = (float)(write_position - 1u) /
                   (float)machine.buffer.capacity_frames;
    first = ts_sister_machine_process_frame(&machine, sister_silence(), sister_silence());
    assert(sister_close(first.head[0].l, old_expected, 2e-4f));
    while (machine.head[0].jump_remaining > 0u)
        first = ts_sister_machine_process_frame(&machine, sister_silence(), sister_silence());
    assert(sister_frame_finite(first.head[0]));
    ts_sister_machine_free(&machine);
}

static void scrub_span_rates_and_taps(void)
{
    TsSisterMachine machine;
    TsSisterParameters parameters;
    TsSisterOutput output;
    double h2_distance;
    double h3_distance;
    size_t i;
    assert(ts_sister_machine_init(&machine, 1000u, 2u, 10.0));
    ts_sister_parameters_default(&parameters, 1000u);
    parameters.head1_level = 0.0f;
    parameters.head2_level = 1.0f;
    parameters.head2_scrub = 1.0f;
    parameters.head2_rate_index = 7;
    parameters.head3_level = 1.0f;
    parameters.head3_span = 1.0f;
    parameters.head3_rate_index = 7;
    sister_configure_immediate(&machine, &parameters);
    assert(sister_close(machine.head[1].offset.current,
                        (float)(machine.buffer.capacity_frames - 1u), 1e-4f));
    assert(sister_close(machine.head[2].offset.current, 8000.0f, 1e-4f));
    for (i = 0u; i < TS_SISTER_RATE_COUNT; ++i) {
        static const double exact[] = {
            -2.0, -4.0 / 3.0, -1.0, -2.0 / 3.0, -0.5,
             0.5,  2.0 / 3.0,  1.0,  4.0 / 3.0,  2.0
        };
        assert(fabs(ts_sister_rate_value((int)i) - exact[i]) < 1e-12);
    }
    sister_fill_buffer(&machine, 0.8f, 0.3f);
    ts_sister_machine_set_rolling(&machine, 0);
    output = ts_sister_machine_process_frame(&machine, sister_silence(), sister_silence());
    assert(output.head[0].l == 0.0f && output.head[0].r == 0.0f);
    assert(sister_frame_finite(output.head[1]) && sister_frame_finite(output.head[2]));
    h2_distance = ts_sister_positive_modulo(
        (double)(machine.master_clock % machine.buffer.capacity_frames) -
        machine.head[1].phase, machine.buffer.capacity_frames);
    h3_distance = ts_sister_positive_modulo(
        (double)(machine.master_clock % machine.buffer.capacity_frames) -
        machine.head[2].phase, machine.buffer.capacity_frames);
    for (i = 0u; i < 100u; ++i)
        output = ts_sister_machine_process_frame(&machine, sister_silence(), sister_silence());
    assert(fabs(ts_sister_positive_modulo(
        (double)(machine.master_clock % machine.buffer.capacity_frames) -
        machine.head[1].phase, machine.buffer.capacity_frames) - h2_distance) < 1e-6);
    assert(fabs(ts_sister_positive_modulo(
        (double)(machine.master_clock % machine.buffer.capacity_frames) -
        machine.head[2].phase, machine.buffer.capacity_frames) - h3_distance) < 1e-6);

    parameters.head2_rate_index = 0;
    parameters.head3_rate_index = 9;
    sister_configure_immediate(&machine, &parameters);
    machine.head[1].phase = 0.5;
    machine.head[2].phase = (double)machine.buffer.storage_frames - 0.5;
    ts_sister_machine_set_rolling(&machine, 0);
    output = ts_sister_machine_process_frame(&machine, sister_silence(), sister_silence());
    assert(ts_sister_positive_modulo(
        (double)(machine.master_clock % machine.buffer.storage_frames) -
        machine.head[1].phase, machine.buffer.storage_frames) < 3.0);
    assert(ts_sister_positive_modulo(
        (double)(machine.master_clock % machine.buffer.storage_frames) -
        machine.head[2].phase, machine.buffer.storage_frames) < 3.0);
    assert(sister_frame_finite(output.mix));
    ts_sister_machine_free(&machine);
}

static void shared_position_and_write_guard(void)
{
    TsSisterMachine machine;
    TsSisterParameters parameters;
    TsSisterOutput output;
    size_t previous;
    assert(ts_sister_machine_init(&machine, 1000u, 2u, 0.100));
    ts_sister_parameters_default(&parameters, 1000u);
    parameters.head1_level = 0.0f;
    parameters.head2_level = 1.0f;
    parameters.head2_scrub = 0.0f;
    parameters.head3_level = 0.0f;
    sister_configure_immediate(&machine, &parameters);
    sister_fill_buffer(&machine, 0.5f, 1.0f);
    ts_sister_machine_set_rolling(&machine, 0);
    machine.master_clock = 50u;
    machine.head[1].phase = 50.0;
    previous = 49u;
    output = ts_sister_machine_process_frame(&machine, sister_silence(), sister_silence());
    assert(sister_close(output.head[1].l,
                        machine.buffer.data[previous * 2u], 1e-5f));
    assert(sister_close(output.head[1].r,
                        machine.buffer.data[previous * 2u + 1u], 1e-5f));
    assert(sister_close(output.head[1].r, output.head[1].l * 2.0f, 1e-4f));
    machine.master_clock = 60u;
    machine.head[1].phase = 60.5;
    machine.head[1].guard_initialized = 1;
    machine.head[1].previous_guard_difference = -0.5;
    machine.head[1].previous_read = (TsStereoFrame){0.125f, 0.25f};
    output = ts_sister_machine_process_frame(&machine, sister_silence(), sister_silence());
    assert(machine.head[1].guard_total == 10u);
    assert(machine.head[1].guard_remaining == 9u);
    assert(sister_close(output.head[1].l, 0.125f, 1e-6f));
    assert(sister_close(output.head[1].r, 0.25f, 1e-6f));
    ts_sister_machine_free(&machine);
}

int main(void)
{
    default_contract();
    h1_delay_for_rate(44100u);
    h1_delay_for_rate(48000u);
    h1_delay_for_rate(96000u);
    minimum_delay_and_jump_crossfade();
    scrub_span_rates_and_taps();
    shared_position_and_write_guard();
    puts("sister head tests passed");
    return 0;
}
