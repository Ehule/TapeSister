#include "tapesister/sister_post_fx.h"
#include "tapesister/sister_runtime.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void assert_finite(TsStereoFrame frame)
{
    assert(isfinite(frame.l));
    assert(isfinite(frame.r));
    assert(fabsf(frame.l) <= 2.0f);
    assert(fabsf(frame.r) <= 2.0f);
}

static void test_defaults_and_identity(void)
{
    TsSisterPostFxEngine engine = {0};
    TsSisterFxControls controls;
    ts_sister_fx_controls_default(&controls);
    assert(controls.reverb_targets == TS_SISTER_EFFECT_TARGET_MIX);
    assert(controls.delay_targets == TS_SISTER_EFFECT_TARGET_MIX);
    assert(controls.distortion_targets == TS_SISTER_EFFECT_TARGET_MIX);
    assert(controls.reverb_mix == 0.0f);
    assert(controls.delay_mix == 0.0f);
    assert(controls.distortion_mix == 0.0f);
    assert(controls.master_feedback == 0.0f);
    assert(ts_sister_post_fx_init(&engine, 48000u));
    ts_sister_post_fx_set_controls(&engine, &controls);
    for (int i = 0; i < 2000; ++i) {
        TsStereoFrame input = {(float)sin(i * 0.071), (float)cos(i * 0.047)};
        TsStereoFrame output = ts_sister_post_fx_process(
            &engine, TS_SISTER_HEAD_COUNT, input, 0);
        assert(output.l == input.l);
        assert(output.r == input.r);
    }
    assert(ts_sister_post_fx_memory_bytes(&engine) > 3000000u);
    ts_sister_post_fx_free(&engine);
}

static void test_delay_length_and_stereo(void)
{
    const uint32_t rates[] = {44100u, 48000u, 96000u};
    for (size_t rate_index = 0u; rate_index < 3u; ++rate_index) {
        TsSisterPostFxEngine engine = {0};
        TsSisterFxControls controls;
        uint32_t rate = rates[rate_index];
        size_t expected = (size_t)lrintf(0.008f * rate);
        float peak = 0.0f;
        size_t peak_at = 0u;
        assert(ts_sister_post_fx_init(&engine, rate));
        ts_sister_fx_controls_default(&controls);
        controls.delay_time = 0.0f;
        controls.delay_feedback = 0.0f;
        controls.delay_mix = 1.0f;
        controls.reverb_targets = 0u;
        controls.distortion_targets = 0u;
        ts_sister_post_fx_set_controls(&engine, &controls);
        for (size_t i = 0u; i < rate / 10u; ++i)
            (void)ts_sister_post_fx_process(&engine, 3u,
                (TsStereoFrame){0.0f, 0.0f}, 0);
        for (size_t i = 0u; i < expected + 8u; ++i) {
            TsStereoFrame input = {i == 0u ? 1.0f : 0.0f, 0.0f};
            TsStereoFrame output = ts_sister_post_fx_process(&engine, 3u,
                                                              input, 0);
            assert_finite(output);
            assert(fabsf(output.r) < 1.0e-7f);
            if (fabsf(output.l) > peak) {
                peak = fabsf(output.l);
                peak_at = i;
            }
        }
        /* Feedback safety must not halve an ordinary one-shot echo. */
        assert(peak > 0.70f);
        assert(peak_at + 1u >= expected && peak_at <= expected + 1u);
        ts_sister_post_fx_free(&engine);
    }
}

static void test_equal_power_chain_makeup(void)
{
    TsSisterPostFxEngine engine = {0};
    TsSisterFxControls controls;
    TsStereoFrame output;
    assert(ts_sister_post_fx_init(&engine, 48000u));
    ts_sister_fx_controls_default(&controls);
    controls.distortion_targets = 0u;
    controls.delay_feedback = 0.0f;
    controls.delay_mix = 0.5f;
    controls.reverb_mix = 0.5f;
    ts_sister_post_fx_set_controls(&engine, &controls);
    for (int i = 0; i < 5000; ++i)
        (void)ts_sister_post_fx_process(&engine, 3u,
            (TsStereoFrame){0.0f, 0.0f}, 0);
    output = ts_sister_post_fx_process(&engine, 3u,
        (TsStereoFrame){0.8f, -0.4f}, 0);
    assert(output.l > 0.38f);
    assert(output.r < -0.19f);
    ts_sister_post_fx_free(&engine);
}

static double reverb_signature(TsSisterReverbType type)
{
    TsSisterPostFxEngine engine = {0};
    TsSisterFxControls controls;
    double signature = 0.0;
    assert(ts_sister_post_fx_init(&engine, 48000u));
    ts_sister_fx_controls_default(&controls);
    controls.reverb_type = type;
    controls.reverb_mix = 1.0f;
    controls.reverb_decay = 0.62f;
    controls.delay_targets = 0u;
    controls.distortion_targets = 0u;
    ts_sister_post_fx_set_controls(&engine, &controls);
    for (int i = 0; i < 4000; ++i)
        (void)ts_sister_post_fx_process(&engine, 3u,
            (TsStereoFrame){0.0f, 0.0f}, 0);
    for (int i = 0; i < 18000; ++i) {
        TsStereoFrame input = {i == 0 ? 1.0f : 0.0f, 0.0f};
        TsStereoFrame output = ts_sister_post_fx_process(&engine, 3u, input, 0);
        assert_finite(output);
        signature += (double)fabsf(output.l) * (double)((i % 31) + 1);
        signature += (double)fabsf(output.r) * (double)((i % 37) + 3);
    }
    ts_sister_post_fx_free(&engine);
    return signature;
}

static void test_reverb_types_and_distortion(void)
{
    double signatures[TS_SISTER_REVERB_TYPE_COUNT];
    TsSisterPostFxEngine engine = {0};
    TsSisterFxControls controls;
    double difference = 0.0;
    for (int type = 0; type < TS_SISTER_REVERB_TYPE_COUNT; ++type)
        signatures[type] = reverb_signature((TsSisterReverbType)type);
    for (int a = 0; a < TS_SISTER_REVERB_TYPE_COUNT; ++a)
        for (int b = a + 1; b < TS_SISTER_REVERB_TYPE_COUNT; ++b)
            assert(fabs(signatures[a] - signatures[b]) > 0.01);

    assert(ts_sister_post_fx_init(&engine, 48000u));
    ts_sister_fx_controls_default(&controls);
    controls.reverb_targets = 0u;
    controls.delay_targets = 0u;
    controls.distortion_mix = 1.0f;
    controls.distortion_drive = 1.0f;
    controls.distortion_tone = 1.0f;
    ts_sister_post_fx_set_controls(&engine, &controls);
    for (int i = 0; i < 4000; ++i) {
        TsStereoFrame input = {0.72f * sinf((float)i * 0.09f), 0.0f};
        TsStereoFrame output = ts_sister_post_fx_process(&engine, 3u, input, 0);
        assert_finite(output);
        assert(fabsf(output.r) < 1.0e-7f);
        difference += fabs((double)output.l - input.l);
    }
    assert(difference > 100.0);
    ts_sister_post_fx_free(&engine);
}

static void test_targets_mono_and_ordinary(void)
{
    TsSisterRuntime runtime;
    TsSisterFxControls controls;
    char error[128];
    ts_sister_runtime_init(&runtime);
    assert(ts_sister_runtime_reconfigure(&runtime, 48000u, 2u,
                                         error, sizeof(error)));
    assert(!runtime.enabled);
    assert(!runtime.machine.buffer.data);
    assert(runtime.post_fx.ready);
    assert(ts_sister_runtime_process_ordinary_post_fx(
        &runtime, (TsStereoFrame){0.25f, -0.5f}).l == 0.25f);
    controls = runtime.parameters.fx;
    controls.delay_targets = ts_sister_effect_targets_toggle(
        controls.delay_targets, TS_SISTER_EFFECT_TARGET_H1);
    assert(controls.delay_targets == TS_SISTER_EFFECT_TARGET_H1);
    controls.delay_targets = ts_sister_effect_targets_toggle(
        controls.delay_targets, TS_SISTER_EFFECT_TARGET_H3);
    assert(controls.delay_targets == (TS_SISTER_EFFECT_TARGET_H1 |
                                      TS_SISTER_EFFECT_TARGET_H3));
    controls.delay_targets = ts_sister_effect_targets_toggle(
        controls.delay_targets, TS_SISTER_EFFECT_TARGET_MIX);
    assert(controls.delay_targets == TS_SISTER_EFFECT_TARGET_MIX);
    {
        TsStereoFrame mono = ts_sister_post_fx_process(&runtime.post_fx, 3u,
            (TsStereoFrame){0.3f, -0.7f}, 1);
        assert(mono.l == 0.3f && mono.r == 0.3f);
    }
    ts_sister_runtime_free(&runtime);
}

static void test_exclusive_target_handoff(void)
{
    TsSisterPostFxEngine engine = {0};
    TsSisterFxControls controls;
    assert(ts_sister_post_fx_init(&engine, 48000u));
    ts_sister_fx_controls_default(&controls);
    controls.distortion_mix = 1.0f;
    controls.distortion_targets = TS_SISTER_EFFECT_TARGET_H1;
    ts_sister_post_fx_set_controls(&engine, &controls);
    assert(engine.distortion_target.active_mask == 0u);
    assert(engine.distortion_target.pending_mask == TS_SISTER_EFFECT_TARGET_H1);
    for (int frame = 0; frame < 800; ++frame)
        for (size_t target = 0u; target < TS_SISTER_EFFECT_PROCESSOR_COUNT;
             ++target)
            (void)ts_sister_post_fx_process(&engine, target,
                (TsStereoFrame){0.0f, 0.0f}, 0);
    assert(engine.distortion_target.active_mask == TS_SISTER_EFFECT_TARGET_H1);
    controls.distortion_targets = TS_SISTER_EFFECT_TARGET_MIX;
    ts_sister_post_fx_set_controls(&engine, &controls);
    assert(engine.distortion_target.active_mask == 0u);
    for (int frame = 0; frame < 300; ++frame)
        for (size_t target = 0u; target < TS_SISTER_EFFECT_PROCESSOR_COUNT;
             ++target)
            (void)ts_sister_post_fx_process(&engine, target,
                (TsStereoFrame){0.0f, 0.0f}, 0);
    assert(engine.distortion_target.active_mask == 0u);
    for (int frame = 0; frame < 500; ++frame)
        for (size_t target = 0u; target < TS_SISTER_EFFECT_PROCESSOR_COUNT;
             ++target)
            (void)ts_sister_post_fx_process(&engine, target,
                (TsStereoFrame){0.0f, 0.0f}, 0);
    assert(engine.distortion_target.active_mask == TS_SISTER_EFFECT_TARGET_MIX);
    ts_sister_post_fx_free(&engine);
}

static void test_rapid_sweeps_finite(void)
{
    TsSisterPostFxEngine engine = {0};
    TsSisterFxControls controls;
    assert(ts_sister_post_fx_init(&engine, 48000u));
    ts_sister_fx_controls_default(&controls);
    controls.reverb_mix = controls.delay_mix = controls.distortion_mix = 1.0f;
    controls.delay_feedback = 1.0f;
    controls.distortion_drive = 1.0f;
    controls.master_feedback = 1.0f;
    for (int i = 0; i < 200000; ++i) {
        if ((i % 97) == 0) {
            controls.delay_time = (float)((i / 97) % 101) / 100.0f;
            controls.reverb_decay = 1.0f - controls.delay_time;
            controls.reverb_type = (TsSisterReverbType)((i / 97) % 4);
            ts_sister_post_fx_set_controls(&engine, &controls);
        }
        assert_finite(ts_sister_post_fx_process(&engine, 3u,
            (TsStereoFrame){i == 0 ? 1.0f : 0.0f,
                            i == 13 ? -1.0f : 0.0f}, 0));
    }
    ts_sister_post_fx_free(&engine);
}

static void test_master_feedback_causality(void)
{
    TsSisterRuntime runtime;
    TsSisterParameters parameters;
    TsSisterSourceFrames sources = {0};
    char error[128];
    float second_write_l;
    ts_sister_runtime_init(&runtime);
    assert(ts_sister_runtime_enable(&runtime, 1000u, 2u, 2u, 0.1,
                                    error, sizeof(error)));
    ts_sister_runtime_set_sources(&runtime, TS_SISTER_SOURCE_PREVIEW);
    parameters = runtime.parameters;
    parameters.head1_time_ms = 1.0f;
    parameters.head1_level = 1.0f;
    parameters.head1_feedback = 0.0f;
    parameters.head2_level = 0.0f;
    parameters.head2_feedback = 0.0f;
    parameters.head3_level = 0.0f;
    parameters.headroom = 1.0f;
    parameters.fx.master_feedback = 1.0f;
    ts_sister_runtime_set_parameters(&runtime, &parameters);
    for (int i = 0; i < 30; ++i)
        (void)ts_sister_runtime_process_frame(&runtime, &sources);
    sources.preview = (TsStereoFrame){0.8f, -0.4f};
    (void)ts_sister_runtime_process_frame(&runtime, &sources);
    assert(runtime.master_feedback_previous.l == 0.0f);
    sources.preview = (TsStereoFrame){0.0f, 0.0f};
    (void)ts_sister_runtime_process_frame(&runtime, &sources);
    second_write_l = runtime.machine.last_output.write.l;
    assert(fabsf(runtime.master_feedback_previous.l) > 0.01f);
    (void)ts_sister_runtime_process_frame(&runtime, &sources);
    assert(fabsf(runtime.machine.last_output.write.l) >
           fabsf(second_write_l) * 2.0f);
    assert(runtime.machine.last_output.write.l > 0.0f);
    assert(runtime.machine.last_output.write.r < 0.0f);
    ts_sister_runtime_disable(&runtime);
    assert(runtime.master_feedback_previous.l == 0.0f);
    assert(runtime.master_feedback_previous.r == 0.0f);
    assert(!runtime.enabled && runtime.post_fx.ready);
    ts_sister_runtime_free(&runtime);
}

int main(void)
{
    test_defaults_and_identity();
    test_delay_length_and_stereo();
    test_equal_power_chain_makeup();
    test_reverb_types_and_distortion();
    test_targets_mono_and_ordinary();
    test_exclusive_target_handoff();
    test_rapid_sweeps_finite();
    test_master_feedback_causality();
    puts("sister post-effects tests passed");
    return 0;
}
