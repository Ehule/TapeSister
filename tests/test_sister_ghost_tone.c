#include "sister_test_helpers.h"

#include <stdio.h>

static float run_pattern(float ghost, float frequency, int right_only)
{
    TsSisterMachine machine;
    TsSisterParameters p;
    double energy = 0.0;
    size_t n;
    assert(ts_sister_machine_init(&machine, 48000u, 2u, 0.010));
    ts_sister_parameters_default(&p, 48000u);
    p.head1_level = p.head2_level = p.head3_level = 0.0f;
    p.head1_feedback = p.head2_feedback = 0.0f;
    p.write_erase = 0.0f;
    p.ghost_tone = ghost;
    sister_configure_immediate(&machine, &p);
    n = machine.buffer.capacity_frames;
    for (size_t i = 0u; i < n; ++i) {
        float value = 0.35f * sinf(2.0f * 3.14159265358979323846f *
                                  frequency * (float)i / 48000.0f);
        assert(ts_sister_buffer_write(&machine.buffer, i,
            right_only ? (TsStereoFrame){0.0f, value} :
                         (TsStereoFrame){value, value}));
    }
    for (int revolution = 0; revolution < 3; ++revolution)
        for (size_t i = 0u; i < n; ++i)
            (void)ts_sister_machine_process_frame(
                &machine, sister_silence(), sister_silence());
    for (size_t i = 0u; i < n; ++i) {
        float value = machine.buffer.data[i * 2u + (right_only ? 1u : 0u)];
        energy += (double)value * value;
        if (right_only) assert(fabsf(machine.buffer.data[i * 2u]) < 0.000001f);
    }
    ts_sister_machine_free(&machine);
    return (float)sqrt(energy / (double)n);
}

int main(void)
{
    TsSisterMachine a, b;
    TsSisterParameters p;
    TsSisterOutput oa, ob;
    float bypass, dark_high, dark_low, right_high;
    assert(ts_sister_machine_init(&a, 48000u, 2u, 0.010));
    assert(ts_sister_machine_init(&b, 48000u, 2u, 0.010));
    ts_sister_parameters_default(&p, 48000u);
    p.head1_level = p.head2_level = p.head3_level = 0.0f;
    p.write_erase = 0.35f;
    p.ghost_tone = 0.0f;
    sister_configure_immediate(&a, &p);
    sister_configure_immediate(&b, &p);
    assert(ts_sister_buffer_write(&a.buffer, 0u, (TsStereoFrame){0.4f, -0.2f}));
    assert(ts_sister_buffer_write(&b.buffer, 0u, (TsStereoFrame){0.4f, -0.2f}));
    oa = ts_sister_machine_process_frame(&a, sister_silence(), sister_silence());
    ob = ts_sister_machine_process_frame(&b, sister_silence(), sister_silence());
    assert(memcmp(&oa.write, &ob.write, sizeof(oa.write)) == 0);
    ts_sister_machine_free(&a);
    ts_sister_machine_free(&b);

    bypass = run_pattern(0.0f, 12000.0f, 0);
    dark_high = run_pattern(0.75f, 12000.0f, 0);
    dark_low = run_pattern(0.75f, 120.0f, 0);
    right_high = run_pattern(0.75f, 12000.0f, 1);
    assert(dark_high < bypass * 0.5f);
    assert(dark_low > dark_high * 1.5f);
    assert(right_high > 0.0f && isfinite(right_high));
    assert(ts_sister_ghost_cutoff_hz(0.0f, 48000u) == 20000.0f);
    assert(ts_sister_ghost_cutoff_hz(1.0f, 48000u) > 249.0f);

    assert(ts_sister_machine_init(&a, 48000u, 2u, 0.010));
    ts_sister_parameters_default(&p, 48000u);
    p.head1_level = p.head2_level = p.head3_level = 0.0f;
    p.write_erase = 1.0f;
    p.ghost_tone = 1.0f;
    p.head1_feedback = p.head2_feedback = 1.0f;
    sister_configure_immediate(&a, &p);
    assert(ts_sister_buffer_write(&a.buffer, 0u,
                                  (TsStereoFrame){0.8f, -0.6f}));
    oa = ts_sister_machine_process_frame(
        &a, sister_silence(), sister_silence());
    assert(fabsf(oa.write.l) < 0.0001f && fabsf(oa.write.r) < 0.0001f);
    for (int i = 0; i < 1000; ++i) {
        oa = ts_sister_machine_process_frame(
            &a, (TsStereoFrame){1000.0f, -1000.0f}, sister_silence());
        assert(sister_frame_finite(oa.write));
    }
    assert(ts_sister_machine_clear_offline(&a));
    assert(a.ghost_lowpass_state[0] == 0.0f &&
           a.ghost_lowpass_state[1] == 0.0f);
    assert(ts_sister_machine_reconfigure(&a, 44100u, 2u, 0.010));
    assert(a.ghost_lowpass_state[0] == 0.0f &&
           a.ghost_lowpass_state[1] == 0.0f);
    ts_sister_machine_free(&a);
    puts("sister Ghost Tone tests passed");
    return 0;
}
