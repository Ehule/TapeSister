#include "sister_test_helpers.h"

#include <math.h>
#include <stdio.h>

static TsSisterParameters routing_parameters(uint32_t rate)
{
    TsSisterParameters p;
    ts_sister_parameters_default(&p, rate);
    p.head1_level = 1.0f;
    p.head1_time_ms = 1.0f;
    p.head1_feedback = 0.0f;
    p.head2_level = 1.0f;
    p.head2_scrub = 0.2f;
    p.head2_rate_index = 7;
    p.head2_feedback = 0.0f;
    p.head3_level = 1.0f;
    p.head3_span = 0.2f;
    p.head3_rate_index = 7;
    p.headroom = 1.0f;
    p.width = 1.0f;
    p.decorrelation_enabled = 0;
    p.filter_type = TS_SISTER_FILTER_BYPASS;
    p.mix_output_gain = 1.0f;
    p.soak = 1.0f;
    p.bleed = 1.0f;
    return p;
}

static void fill_stereo(TsSisterMachine *machine, TsStereoFrame value)
{
    for (size_t i = 0u; i < machine->buffer.capacity_frames; ++i)
        assert(ts_sister_buffer_write(&machine->buffer, i, value));
}

static TsSisterOutput settle_machine(TsSisterMachine *machine, int frames)
{
    TsSisterOutput output = {0};
    ts_sister_machine_set_rolling(machine, 0);
    for (int i = 0; i < frames; ++i)
        output = ts_sister_machine_process_frame(
            machine, sister_silence(), sister_silence());
    return output;
}

static void head_and_mix_placement(void)
{
    TsSisterMachine machine;
    TsSisterParameters p = routing_parameters(1000u);
    TsSisterOutput output;
    assert(p.soak_targets == TS_SISTER_EFFECT_TARGET_MIX);
    assert(ts_sister_machine_init(&machine, 1000u, 2u, 0.1));
    assert(machine.soak_weave[0].delay_l != machine.soak_weave[1].delay_l);
    assert(machine.soak_weave[1].delay_l != machine.soak_weave[2].delay_l);

    p.soak_targets = TS_SISTER_EFFECT_TARGET_H1;
    sister_configure_immediate(&machine, &p);
    fill_stereo(&machine, (TsStereoFrame){0.8f, 0.0f});
    output = settle_machine(&machine, 200);
    assert(output.head[0].r > 0.2f);
    assert(output.head[1].r == 0.0f && output.head[2].r == 0.0f);

    p.soak_targets = TS_SISTER_EFFECT_TARGET_H1 |
                     TS_SISTER_EFFECT_TARGET_H3;
    sister_configure_immediate(&machine, &p);
    fill_stereo(&machine, (TsStereoFrame){0.8f, 0.0f});
    output = settle_machine(&machine, 200);
    assert(output.head[0].r > 0.2f && output.head[2].r > 0.2f);
    assert(output.head[1].r == 0.0f);
    assert(fabsf(output.head[0].r - output.head[2].r) > 0.01f);

    p.soak_targets = TS_SISTER_EFFECT_TARGET_MIX;
    sister_configure_immediate(&machine, &p);
    fill_stereo(&machine, (TsStereoFrame){0.8f, 0.0f});
    output = settle_machine(&machine, 200);
    assert(output.head[0].r == 0.0f && output.head[1].r == 0.0f &&
           output.head[2].r == 0.0f);
    assert(output.mix.r > 0.2f);

    p.soak_targets = TS_SISTER_EFFECT_TARGET_MIX |
                     TS_SISTER_EFFECT_TARGET_H1;
    sister_configure_immediate(&machine, &p);
    assert(machine.parameters.soak_targets == TS_SISTER_EFFECT_TARGET_MIX);
    fill_stereo(&machine, (TsStereoFrame){0.8f, 0.0f});
    output = settle_machine(&machine, 200);
    assert(output.head[0].r == 0.0f && output.mix.r > 0.2f);
    ts_sister_machine_free(&machine);
}

static void feedback_contract(void)
{
    TsSisterMachine machine;
    TsSisterParameters p = routing_parameters(1000u);
    TsSisterOutput output;
    p.head1_feedback = 0.8f;
    p.head2_level = p.head3_level = 0.0f;
    assert(ts_sister_machine_init(&machine, 1000u, 2u, 0.1));

    p.soak_targets = TS_SISTER_EFFECT_TARGET_H1;
    sister_configure_immediate(&machine, &p);
    fill_stereo(&machine, (TsStereoFrame){0.8f, 0.0f});
    output = settle_machine(&machine, 200);
    assert(output.write.r > 0.05f);

    p.soak_targets = TS_SISTER_EFFECT_TARGET_H3;
    sister_configure_immediate(&machine, &p);
    fill_stereo(&machine, (TsStereoFrame){0.8f, 0.0f});
    output = settle_machine(&machine, 200);
    assert(output.write.r == 0.0f);

    p.soak_targets = TS_SISTER_EFFECT_TARGET_MIX;
    sister_configure_immediate(&machine, &p);
    fill_stereo(&machine, (TsStereoFrame){0.8f, 0.0f});
    output = settle_machine(&machine, 200);
    assert(output.mix.r > 0.2f);
    assert(output.write.r == 0.0f); /* no hidden MIX-to-write return */
    ts_sister_machine_free(&machine);
}

static void fx_return_is_wet_return_not_master(void)
{
    TsSisterMachine machine;
    TsSisterPostFxEngine post_fx = {0};
    TsSisterParameters p = routing_parameters(1000u);
    TsSisterOutput output = {0};
    assert(ts_sister_machine_init(&machine, 1000u, 2u, 0.1));
    assert(ts_sister_post_fx_init(&post_fx, 1000u));
    p.head2_level = 0.0f;
    p.head3_level = 0.0f;
    p.soak_targets = 0u;
    p.fx.reverb_targets = 0u;
    p.fx.delay_targets = 0u;
    p.fx.distortion_targets = TS_SISTER_EFFECT_TARGET_MIX;
    p.fx.distortion_drive = 1.0f;
    p.fx.distortion_tone = 0.5f;
    p.fx.distortion_mix = 1.0f;
    p.fx_return_gain = 0.0f;
    sister_configure_immediate(&machine, &p);
    ts_sister_post_fx_set_controls(&post_fx, &p.fx);
    fill_stereo(&machine, (TsStereoFrame){0.25f, -0.25f});
    ts_sister_machine_set_rolling(&machine, 0);
    for (int i = 0; i < 200; ++i)
        output = ts_sister_machine_process_frame_with_fx(
            &machine, &post_fx, sister_silence(), sister_silence(),
            sister_silence());
    /* FX RETURN at zero bypasses the insert; it is not the master output. */
    assert(sister_peak(output.mix) > 0.1f);
    assert(sister_close(output.post_fx.l, output.head[0].l, 0.00001f));
    assert(sister_close(output.post_fx.r, output.head[0].r, 0.00001f));
    ts_sister_post_fx_free(&post_fx);
    ts_sister_machine_free(&machine);
}

static TsSisterRuntimeFrame run_runtime(TsSisterRuntime *runtime, int frames,
                                        TsStereoFrame value)
{
    TsSisterRuntimeFrame frame = {0};
    TsSisterSourceFrames source = {0};
    for (int i = 0; i < frames; ++i) {
        float sign = (i % 32) < 16 ? 1.0f : -1.0f;
        source.preview = (TsStereoFrame){value.l * sign, value.r * sign};
        frame = ts_sister_runtime_process_frame(runtime, &source);
    }
    return frame;
}

static void capture_dry_and_mono_contract(void)
{
    TsSisterRuntime runtime;
    TsInstrument instrument;
    TsSisterParameters p;
    TsSisterRuntimeFrame frame;
    char error[160];
    assert(sister_test_make_tiles(&instrument, 1, 2, 1000u, 32u));
    assert(sister_test_enable(&runtime, 1000u, 2u, 0.1));
    p = routing_parameters(1000u);
    p.head2_level = p.head3_level = 0.0f;
    p.soak_targets = TS_SISTER_EFFECT_TARGET_H1;
    p.monitor_dry = 1.0f;
    p.monitor_wet = 0.0f;
    ts_sister_runtime_set_parameters(&runtime, &p);
    ts_sister_machine_reset(&runtime.machine);
    ts_sister_runtime_set_sources(&runtime, TS_SISTER_SOURCE_PREVIEW);
    ts_sister_runtime_set_monitor(&runtime, 1);
    frame = run_runtime(&runtime, 500, (TsStereoFrame){0.7f, 0.0f});
    assert(fabsf(frame.tap[TS_SISTER_TAP_H1].r) > 0.1f);
    assert(frame.monitor_return.l == frame.input.l &&
           frame.monitor_return.r == frame.input.r);

    assert(ts_sister_runtime_arm_capture(
        &runtime, &instrument, 1, 4u, 1000u, 2u, TS_SISTER_TAP_H1, 0u,
        error, sizeof(error)));
    assert(ts_sister_runtime_trigger_capture(&runtime, error, sizeof(error)));
    frame = run_runtime(&runtime, 1, (TsStereoFrame){0.7f, 0.0f});
    assert(runtime.capture.buffer[0] == frame.tap[TS_SISTER_TAP_H1].l);
    assert(runtime.capture.buffer[1] == frame.tap[TS_SISTER_TAP_H1].r);
    assert(ts_sister_runtime_cancel_capture(&runtime));

    p.soak_targets = TS_SISTER_EFFECT_TARGET_MIX;
    ts_sister_runtime_set_parameters(&runtime, &p);
    ts_sister_machine_reset(&runtime.machine);
    frame = run_runtime(&runtime, 500, (TsStereoFrame){0.7f, 0.0f});
    assert(frame.tap[TS_SISTER_TAP_H1].r == 0.0f);
    assert(fabsf(frame.tap[TS_SISTER_TAP_MIX].r) > 0.1f);
    assert(ts_sister_runtime_arm_capture(
        &runtime, &instrument, 1, 4u, 1000u, 1u, TS_SISTER_TAP_MIX, 0u,
        error, sizeof(error)));
    assert(ts_sister_runtime_trigger_capture(&runtime, error, sizeof(error)));
    frame = run_runtime(&runtime, 1, (TsStereoFrame){0.7f, 0.0f});
    assert(runtime.capture.buffer[0] ==
           ts_stereo_frame_fold_mono(frame.tap[TS_SISTER_TAP_MIX]));
    assert(ts_sister_runtime_cancel_capture(&runtime));
    ts_sister_runtime_free(&runtime);
    ts_instrument_free(&instrument);

    assert(sister_test_enable(&runtime, 1000u, 1u, 0.1));
    p = routing_parameters(1000u);
    p.soak_targets = TS_SISTER_EFFECT_TARGET_H1 |
                     TS_SISTER_EFFECT_TARGET_H2 |
                     TS_SISTER_EFFECT_TARGET_H3;
    ts_sister_runtime_set_parameters(&runtime, &p);
    ts_sister_machine_reset(&runtime.machine);
    ts_sister_runtime_set_sources(&runtime, TS_SISTER_SOURCE_PREVIEW);
    frame = run_runtime(&runtime, 200, (TsStereoFrame){0.4f, 0.4f});
    assert(frame.tap[TS_SISTER_TAP_H1].l == frame.tap[TS_SISTER_TAP_H1].r);
    assert(frame.tap[TS_SISTER_TAP_H2].l == frame.tap[TS_SISTER_TAP_H2].r);
    assert(frame.tap[TS_SISTER_TAP_H3].l == frame.tap[TS_SISTER_TAP_H3].r);
    ts_sister_runtime_free(&runtime);
}

static void transport_and_reconfigure(void)
{
    TsSisterMachine machine;
    TsSisterParameters p = routing_parameters(44100u);
    double phase;
    size_t old_delay_frames;
    assert(ts_sister_machine_init(&machine, 44100u, 2u, 0.1));
    p.soak_targets = 0u;
    sister_configure_immediate(&machine, &p);
    ts_sister_machine_set_rolling(&machine, 0);
    ts_sister_machine_set_hold(&machine, 1);
    phase = machine.soak_weave[0].phase;
    for (int i = 0; i < 100; ++i)
        (void)ts_sister_machine_process_frame(
            &machine, sister_silence(), sister_silence());
    assert(machine.soak_weave[0].phase != phase);
    old_delay_frames = machine.soak_weave[0].delay_frames;
    assert(ts_sister_machine_reconfigure(&machine, 96000u, 2u, 0.1));
    assert(machine.soak_weave[0].delay_frames > old_delay_frames);
    for (size_t i = 0u; i < machine.soak_weave[0].delay_frames; ++i)
        assert(machine.soak_weave[0].delay_l[i] == 0.0f &&
               machine.soak_weave[0].delay_r[i] == 0.0f);
    ts_sister_machine_free(&machine);
}

static void extreme_safety_and_cancellation(void)
{
    TsSisterMachine machine;
    TsSisterParameters p = routing_parameters(48000u);
    TsSisterOutput output = {0};
    assert(ts_sister_machine_init(&machine, 48000u, 2u, 0.03));
    p.head1_feedback = 1.0f;
    p.head2_feedback = 1.0f;
    p.mix_output_gain = 4.0f;
    p.soak_targets = TS_SISTER_EFFECT_TARGET_MIX;
    sister_configure_immediate(&machine, &p);
    for (size_t i = 0u; i < machine.buffer.capacity_frames; ++i) {
        float sign = (i & 1u) != 0u ? 1.0f : -1.0f;
        assert(ts_sister_buffer_write(
            &machine.buffer, i, (TsStereoFrame){sign, -sign}));
    }
    ts_sister_machine_set_rolling(&machine, 0);
    for (int i = 0; i < 10000; ++i) {
        output = ts_sister_machine_process_frame(
            &machine, (TsStereoFrame){INFINITY, NAN}, sister_silence());
        assert(sister_frame_finite(output.mix));
        assert(sister_peak(output.mix) <= 1.0f);
        assert(isfinite(ts_stereo_frame_fold_mono(output.mix)));
        assert(sister_frame_finite(output.write));
    }
    assert(machine.overload_count > 0u);
    ts_sister_machine_free(&machine);
}

int main(void)
{
    head_and_mix_placement();
    feedback_contract();
    fx_return_is_wet_return_not_master();
    capture_dry_and_mono_contract();
    transport_and_reconfigure();
    extreme_safety_and_cancellation();
    puts("sister effect routing tests passed");
    return 0;
}
