#include "sister_test_helpers.h"

#include <stdio.h>

static void configure_modulation(TsSisterMachine *machine, uint32_t rate,
                                 float wow, float drop)
{
    TsSisterParameters parameters;
    ts_sister_parameters_default(&parameters, rate);
    parameters.head1_level = 0.0f;
    parameters.head2_level = 1.0f;
    parameters.head2_scrub = 0.3f;
    parameters.head3_level = 1.0f;
    parameters.head3_span = 0.6f;
    parameters.wow = wow;
    parameters.drop = drop;
    sister_configure_immediate(machine, &parameters);
    sister_fill_buffer(machine, 0.8f, 0.4f);
    ts_sister_machine_set_rolling(machine, 0);
}

static void deterministic_wow_and_drop(void)
{
    TsSisterMachine a;
    TsSisterMachine b;
    TsSisterOutput out_a;
    TsSisterOutput out_b;
    size_t i;
    int saw_independent_drop = 0;
    assert(ts_sister_machine_init(&a, 1000u, 2u, 1.0));
    assert(ts_sister_machine_init(&b, 1000u, 2u, 1.0));
    configure_modulation(&a, 1000u, 10.0f, 100.0f);
    configure_modulation(&b, 1000u, 10.0f, 100.0f);
    ts_sister_machine_seed(&a, 123456u);
    ts_sister_machine_seed(&b, 123456u);
    for (i = 0u; i < 1000u; ++i) {
        out_a = ts_sister_machine_process_frame(&a, sister_silence(), sister_silence());
        out_b = ts_sister_machine_process_frame(&b, sister_silence(), sister_silence());
        assert(out_a.head[1].l == out_b.head[1].l);
        assert(out_a.head[2].r == out_b.head[2].r);
        assert(a.wow_state == b.wow_state);
        assert(a.drop[0].current == b.drop[0].current);
        assert(a.drop[1].current == b.drop[1].current);
        assert(a.drop[0].current >= 0.0f && a.drop[0].current <= 1.5f);
        assert(a.drop[1].current >= 0.0f && a.drop[1].current <= 1.5f);
        if (a.drop[0].current != a.drop[1].current) saw_independent_drop = 1;
    }
    assert(saw_independent_drop);
    ts_sister_machine_free(&a);
    ts_sister_machine_free(&b);
}

static void zero_identity_and_shared_wow(void)
{
    TsSisterMachine machine;
    TsSisterParameters parameters;
    TsSisterOutput output;
    double rate;
    double phase_before_h2;
    double phase_before_h3;
    double wow_h2;
    double wow_h3;
    size_t i;
    assert(ts_sister_machine_init(&machine, 1000u, 2u, 1.0));
    configure_modulation(&machine, 1000u, 0.0f, 0.0f);
    for (i = 0u; i < 300u; ++i)
        output = ts_sister_machine_process_frame(&machine, sister_silence(), sister_silence());
    assert(machine.wow_state == 0.0f);
    assert(machine.drop[0].current == 1.0f && machine.drop[1].current == 1.0f);
    assert(sister_frame_finite(output.mix));

    parameters = machine.parameters;
    parameters.wow = 10.0f;
    ts_sister_machine_set_parameters(&machine, &parameters);
    ts_sister_machine_seed(&machine, 77u);
    for (i = 0u; i < 200u; ++i)
        output = ts_sister_machine_process_frame(&machine, sister_silence(), sister_silence());
    rate = ts_sister_rate_value(machine.parameters.head2_rate_index);
    phase_before_h2 = ts_sister_positive_modulo(machine.head[1].phase - rate,
                                                machine.buffer.capacity_frames);
    rate = ts_sister_rate_value(machine.parameters.head3_rate_index);
    phase_before_h3 = ts_sister_positive_modulo(machine.head[2].phase - rate,
                                                machine.buffer.capacity_frames);
    wow_h2 = machine.last_head_position[1] - phase_before_h2;
    wow_h3 = machine.last_head_position[2] - phase_before_h3;
    if (wow_h2 > (double)machine.buffer.capacity_frames * 0.5)
        wow_h2 -= (double)machine.buffer.capacity_frames;
    if (wow_h2 < -(double)machine.buffer.capacity_frames * 0.5)
        wow_h2 += (double)machine.buffer.capacity_frames;
    if (wow_h3 > (double)machine.buffer.capacity_frames * 0.5)
        wow_h3 -= (double)machine.buffer.capacity_frames;
    if (wow_h3 < -(double)machine.buffer.capacity_frames * 0.5)
        wow_h3 += (double)machine.buffer.capacity_frames;
    assert(fabs(wow_h2 - wow_h3) < 1e-6);
    ts_sister_machine_free(&machine);
}

static void cadence_and_sample_rate_independence(void)
{
    TsSisterMachine low;
    TsSisterMachine high;
    assert(ts_sister_machine_init(&low, 1000u, 2u, 1.0));
    assert(ts_sister_machine_init(&high, 2000u, 2u, 1.0));
    configure_modulation(&low, 1000u, 0.0f, 100.0f);
    configure_modulation(&high, 2000u, 0.0f, 100.0f);
    ts_sister_machine_seed(&low, 999u);
    ts_sister_machine_seed(&high, 999u);
    ts_sister_machine_process_frame(&low, sister_silence(), sister_silence());
    ts_sister_machine_process_frame(&high, sister_silence(), sister_silence());
    assert(low.drop[0].next_event_clock == 100u);
    assert(high.drop[0].next_event_clock == 200u);
    assert(low.drop[0].gain.remaining == 4u);
    assert(high.drop[0].gain.remaining == 9u);
    assert(sister_close(low.drop[0].gain.target,
                        high.drop[0].gain.target, 1e-7f));
    ts_sister_machine_free(&low);
    ts_sister_machine_free(&high);
}

int main(void)
{
    deterministic_wow_and_drop();
    zero_identity_and_shared_wow();
    cadence_and_sample_rate_independence();
    puts("sister modulation tests passed");
    return 0;
}
