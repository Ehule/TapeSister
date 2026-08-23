#include "sister_test_helpers.h"

#include <stdio.h>

static TsSisterParameters h1_only(uint32_t sample_rate)
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

static void mono_and_true_stereo(void)
{
    TsSisterMachine mono;
    TsSisterMachine stereo;
    TsSisterParameters parameters = h1_only(1000u);
    TsSisterOutput output;
    assert(ts_sister_machine_init(&mono, 1000u, 1u, 0.100));
    sister_configure_immediate(&mono, &parameters);
    fill_constant(&mono, (TsStereoFrame){0.2f, 1.0f});
    ts_sister_machine_set_rolling(&mono, 0);
    output = ts_sister_machine_process_frame(&mono, sister_silence(), sister_silence());
    assert(sister_close(output.head[0].l, 0.6f, 1e-6f));
    assert(output.head[0].l == output.head[0].r);
    ts_sister_machine_free(&mono);

    assert(ts_sister_machine_init(&stereo, 1000u, 2u, 0.100));
    sister_configure_immediate(&stereo, &parameters);
    fill_constant(&stereo, (TsStereoFrame){0.8f, 0.0f});
    ts_sister_machine_set_rolling(&stereo, 0);
    output = ts_sister_machine_process_frame(&stereo, sister_silence(), sister_silence());
    assert(sister_close(output.head[0].l, 0.8f, 1e-6f));
    assert(output.head[0].r == 0.0f);
    ts_sister_machine_free(&stereo);
}

static void decorrelation_delay_for_rate(uint32_t sample_rate)
{
    TsSisterMachine machine;
    TsSisterParameters parameters = h1_only(sample_rate);
    TsSisterOutput output;
    size_t delay_frames = (size_t)ceil((double)sample_rate * 0.020);
    size_t i;
    parameters.decorrelation_enabled = 1;
    assert(ts_sister_machine_init(&machine, sample_rate, 2u, 0.100));
    sister_configure_immediate(&machine, &parameters);
    fill_constant(&machine, (TsStereoFrame){1.0f, 1.0f});
    ts_sister_machine_set_rolling(&machine, 0);
    output = ts_sister_machine_process_frame(&machine, sister_silence(), sister_silence());
    assert(output.head[0].l > 0.99f && output.head[0].r == 0.0f);
    for (i = 1u; i < delay_frames; ++i) {
        output = ts_sister_machine_process_frame(&machine, sister_silence(), sister_silence());
        assert(output.head[0].r == 0.0f);
    }
    output = ts_sister_machine_process_frame(&machine, sister_silence(), sister_silence());
    assert(output.head[0].r > 0.9f);
    assert(machine.decorrelator[0].delay_frames == delay_frames);
    ts_sister_machine_free(&machine);
}

static void bypass_and_width(void)
{
    TsSisterMachine machine;
    TsSisterParameters parameters = h1_only(1000u);
    TsSisterOutput output;
    float previous;
    size_t i;
    parameters.decorrelation_enabled = 1;
    assert(ts_sister_machine_init(&machine, 1000u, 2u, 0.100));
    sister_configure_immediate(&machine, &parameters);
    fill_constant(&machine, (TsStereoFrame){1.0f, 1.0f});
    ts_sister_machine_set_rolling(&machine, 0);
    for (i = 0u; i < 25u; ++i)
        output = ts_sister_machine_process_frame(&machine, sister_silence(), sister_silence());
    assert(output.head[0].r > 0.9f);
    fill_constant(&machine, (TsStereoFrame){1.0f, 0.0f});
    parameters.decorrelation_enabled = 0;
    ts_sister_machine_set_parameters(&machine, &parameters);
    previous = output.head[0].r;
    for (i = 0u; i < 20u; ++i) {
        output = ts_sister_machine_process_frame(&machine, sister_silence(), sister_silence());
        assert(fabsf(output.head[0].r - previous) < 0.1f);
        previous = output.head[0].r;
    }
    assert(output.head[0].r < 0.01f);

    parameters.width = 0.0f;
    sister_configure_immediate(&machine, &parameters);
    fill_constant(&machine, (TsStereoFrame){1.0f, 0.0f});
    ts_sister_machine_set_rolling(&machine, 0);
    output = ts_sister_machine_process_frame(&machine, sister_silence(), sister_silence());
    assert(sister_close(output.head[0].l, 0.5f, 1e-6f));
    assert(output.head[0].l == output.head[0].r);
    parameters.width = 0.5f;
    sister_configure_immediate(&machine, &parameters);
    fill_constant(&machine, (TsStereoFrame){1.0f, 0.0f});
    ts_sister_machine_set_rolling(&machine, 0);
    output = ts_sister_machine_process_frame(&machine, sister_silence(), sister_silence());
    assert(sister_close(output.head[0].l, 0.75f, 1e-6f));
    assert(sister_close(output.head[0].r, 0.25f, 1e-6f));
    parameters.width = 1.0f;
    sister_configure_immediate(&machine, &parameters);
    fill_constant(&machine, (TsStereoFrame){1.0f, 0.0f});
    ts_sister_machine_set_rolling(&machine, 0);
    output = ts_sister_machine_process_frame(&machine, sister_silence(), sister_silence());
    assert(output.head[0].l == 1.0f && output.head[0].r == 0.0f);
    ts_sister_machine_free(&machine);
}

static void balance_preserving_safety(void)
{
    TsSisterMachine machine;
    TsSisterParameters parameters;
    TsSisterOutput output;
    assert(ts_sister_machine_init(&machine, 1000u, 2u, 0.100));
    ts_sister_parameters_default(&parameters, 1000u);
    parameters.head1_level = parameters.head2_level = parameters.head3_level = 1.0f;
    parameters.head1_time_ms = 1.0f;
    parameters.head2_scrub = 0.2f;
    parameters.head3_span = 0.2f;
    parameters.headroom = 1.0f;
    sister_configure_immediate(&machine, &parameters);
    fill_constant(&machine, (TsStereoFrame){1.0f, 0.5f});
    ts_sister_machine_set_rolling(&machine, 0);
    output = ts_sister_machine_process_frame(&machine, sister_silence(), sister_silence());
    assert(sister_close(output.mix.l, 1.0f, 1e-5f));
    assert(sister_close(output.mix.r, 0.5f, 1e-5f));
    assert(machine.overload_count > 0u);
    ts_sister_machine_free(&machine);
}

int main(void)
{
    mono_and_true_stereo();
    decorrelation_delay_for_rate(1000u);
    decorrelation_delay_for_rate(2000u);
    bypass_and_width();
    balance_preserving_safety();
    puts("sister stereo tests passed");
    return 0;
}
