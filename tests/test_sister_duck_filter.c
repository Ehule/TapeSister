#include "sister_test_helpers.h"

#include <stdio.h>

static TsSisterParameters base_parameters(uint32_t sample_rate)
{
    TsSisterParameters parameters;
    ts_sister_parameters_default(&parameters, sample_rate);
    parameters.head1_level = 1.0f;
    parameters.head1_time_ms = 1.0f;
    parameters.head1_feedback = 0.0f;
    parameters.head2_level = 0.0f;
    parameters.head3_level = 0.0f;
    parameters.headroom = 1.0f;
    return parameters;
}

static void fill_constant(TsSisterMachine *machine, TsStereoFrame value)
{
    size_t i;
    for (i = 0u; i < machine->buffer.capacity_frames; ++i)
        assert(ts_sister_buffer_write(&machine->buffer, i, value));
}

static float run_duck_side(TsStereoFrame sidechain)
{
    TsSisterMachine machine;
    TsSisterParameters parameters = base_parameters(1000u);
    size_t i;
    parameters.duck_enabled = 1;
    parameters.duck_mode = TS_SISTER_DUCK_SAFE;
    parameters.duck_sensitivity = 0.5f;
    assert(ts_sister_machine_init(&machine, 1000u, 2u, 1.0));
    sister_configure_immediate(&machine, &parameters);
    fill_constant(&machine, (TsStereoFrame){0.5f, 0.5f});
    ts_sister_machine_set_rolling(&machine, 0);
    for (i = 0u; i < 1000u; ++i)
        ts_sister_machine_process_frame(&machine, sister_silence(), sidechain);
    {
        float gain = machine.duck_gain;
        ts_sister_machine_free(&machine);
        return gain;
    }
}

static void duck_modes(void)
{
    TsSisterMachine machine;
    TsSisterParameters parameters = base_parameters(1000u);
    TsSisterOutput baseline;
    TsSisterOutput loud;
    float left_gain;
    float right_gain;
    size_t i;
    assert(ts_sister_machine_init(&machine, 1000u, 2u, 1.0));
    sister_configure_immediate(&machine, &parameters);
    fill_constant(&machine, (TsStereoFrame){0.5f, 0.25f});
    ts_sister_machine_set_rolling(&machine, 0);
    baseline = ts_sister_machine_process_frame(&machine, sister_silence(), sister_silence());
    loud = ts_sister_machine_process_frame(&machine, sister_silence(),
                                            (TsStereoFrame){100.0f, -100.0f});
    assert(baseline.mix.l == loud.mix.l && baseline.mix.r == loud.mix.r);
    assert(machine.duck_gain == 1.0f);
    ts_sister_machine_free(&machine);

    left_gain = run_duck_side((TsStereoFrame){1.0f, 0.0f});
    right_gain = run_duck_side((TsStereoFrame){0.0f, 1.0f});
    assert(left_gain < 0.5f);
    assert(sister_close(left_gain, right_gain, 1e-6f));

    assert(ts_sister_machine_init(&machine, 1000u, 2u, 1.0));
    parameters.duck_enabled = 1;
    parameters.duck_mode = TS_SISTER_DUCK_SAFE;
    parameters.duck_sensitivity = 0.5f;
    sister_configure_immediate(&machine, &parameters);
    fill_constant(&machine, (TsStereoFrame){0.5f, 0.5f});
    ts_sister_machine_set_rolling(&machine, 0);
    for (i = 0u; i < 1000u; ++i)
        loud = ts_sister_machine_process_frame(&machine, sister_silence(),
                                                (TsStereoFrame){1.0f, 1.0f});
    assert(machine.duck_gain < 0.5f);
    for (i = 0u; i < 5000u; ++i)
        loud = ts_sister_machine_process_frame(&machine, sister_silence(), sister_silence());
    assert(machine.duck_gain > 0.9f);
    parameters.duck_mode = TS_SISTER_DUCK_KAFKA_BIAS;
    parameters.duck_sensitivity = 0.1f;
    sister_configure_immediate(&machine, &parameters);
    ts_sister_machine_set_rolling(&machine, 0);
    for (i = 0u; i < 1000u; ++i)
        loud = ts_sister_machine_process_frame(&machine, sister_silence(), sister_silence());
    assert(machine.duck_gain < 0.1f);
    assert(sister_frame_finite(loud.mix));
    ts_sister_machine_free(&machine);
}

static void filter_types_bounds_and_state(void)
{
    TsSisterMachine machine;
    TsSisterParameters parameters = base_parameters(2000u);
    TsSisterOutput output;
    int type;
    size_t i;
    assert(ts_sister_machine_init(&machine, 2000u, 2u, 1.0));
    sister_configure_immediate(&machine, &parameters);
    fill_constant(&machine, (TsStereoFrame){0.8f, 0.0f});
    ts_sister_machine_set_rolling(&machine, 0);
    output = ts_sister_machine_process_frame(&machine, sister_silence(), sister_silence());
    assert(sister_close(output.mix.l, 0.8f, 1e-6f));
    assert(output.mix.r == 0.0f);
    for (type = TS_SISTER_FILTER_LOWPASS;
         type < TS_SISTER_FILTER_TYPE_COUNT; ++type) {
        parameters.filter_type = (TsSisterFilterType)type;
        parameters.filter_cutoff_hz = 300.0f;
        parameters.filter_q = 0.8f;
        parameters.filter_gain_db = 6.0f;
        sister_configure_immediate(&machine, &parameters);
        fill_constant(&machine, (TsStereoFrame){0.8f, 0.0f});
        ts_sister_machine_set_rolling(&machine, 0);
        for (i = 0u; i < 200u; ++i) {
            output = ts_sister_machine_process_frame(&machine,
                                                     sister_silence(), sister_silence());
            assert(sister_frame_finite(output.mix));
            assert(output.mix.r == 0.0f);
        }
    }
    parameters.filter_type = TS_SISTER_FILTER_LOWPASS;
    parameters.filter_cutoff_hz = -100.0f;
    parameters.filter_q = 0.0f;
    parameters.filter_gain_db = 100.0f;
    ts_sister_machine_set_parameters(&machine, &parameters);
    assert(machine.parameters.filter_cutoff_hz == 10.0f);
    assert(machine.parameters.filter_q == 0.1f);
    assert(machine.parameters.filter_gain_db == 24.0f);
    assert(machine.filter_ramp_remaining == 40u);
    machine.filter_state[0].x1 = NAN;
    output = ts_sister_machine_process_frame(&machine, sister_silence(), sister_silence());
    assert(sister_frame_finite(output.mix));
    assert(machine.filter_state[0].x1 == 0.0f);
    assert(machine.overload_count > 0u);
    ts_sister_machine_free(&machine);
}

int main(void)
{
    duck_modes();
    filter_types_bounds_and_state();
    puts("sister duck/filter tests passed");
    return 0;
}
