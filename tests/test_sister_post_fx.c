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
    static const float ratios[TS_SISTER_DELAY_TAPS] = {
        0.29f, 0.47f, 0.71f, 1.0f
    };
    for (size_t rate_index = 0u; rate_index < 3u; ++rate_index) {
        TsSisterPostFxEngine engine = {0};
        TsSisterFxControls controls;
        uint32_t rate = rates[rate_index];
        size_t master_delay = (size_t)lrintf(0.008f * rate);
        float tap_peak[TS_SISTER_DELAY_TAPS] = {0.0f};
        double left_energy = 0.0;
        double right_energy = 0.0;
        double stereo_difference = 0.0;
        assert(ts_sister_post_fx_init(&engine, rate));
        ts_sister_fx_controls_default(&controls);
        controls.delay_time = 0.0f;
        controls.delay_feedback = 0.0f;
        controls.delay_mix = 1.0f;
        controls.reverb_targets = 0u;
        controls.distortion_targets = 0u;
        ts_sister_post_fx_sync_controls(&engine, &controls);
        for (size_t i = 0u; i < rate / 10u; ++i)
            (void)ts_sister_post_fx_process(&engine, 3u,
                (TsStereoFrame){0.0f, 0.0f}, 0);
        for (size_t i = 0u; i < master_delay + 64u; ++i) {
            TsStereoFrame input = {i == 0u ? 1.0f : 0.0f, 0.0f};
            TsStereoFrame output = ts_sister_post_fx_process(&engine, 3u,
                                                              input, 0);
            assert_finite(output);
            left_energy += output.l * output.l;
            right_energy += output.r * output.r;
            stereo_difference += fabs((double)output.l - (double)output.r);
            for (size_t tap = 0u; tap < TS_SISTER_DELAY_TAPS; ++tap) {
                size_t expected = (size_t)lrintf(
                    0.008f * ratios[tap] * rate);
                size_t distance = i > expected ? i - expected : expected - i;
                float magnitude = fabsf(output.l) + fabsf(output.r);
                if (distance <= 6u && magnitude > tap_peak[tap])
                    tap_peak[tap] = magnitude;
            }
        }
        for (size_t tap = 0u; tap < TS_SISTER_DELAY_TAPS; ++tap) {
            assert(tap_peak[tap] > 0.005f);
        }
        /* A one-sided source must become a real stereo head pattern, not four
           identical centered copies or a collapsed mono return. */
        assert(left_energy > 0.001);
        assert(right_energy > 0.001);
        assert(stereo_difference > 0.1);
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

static void test_tape_feedback_tail(void)
{
    TsSisterPostFxEngine engine = {0};
    TsSisterFxControls controls;
    double early_energy = 0.0;
    double late_energy = 0.0;
    double early_roughness = 0.0;
    double late_roughness = 0.0;
    float previous = 0.0f;

    assert(ts_sister_post_fx_init(&engine, 48000u));
    ts_sister_fx_controls_default(&controls);
    controls.reverb_targets = 0u;
    controls.distortion_targets = 0u;
    controls.delay_time = 0.0f;
    controls.delay_feedback = 0.92f;
    controls.delay_mix = 1.0f;
    ts_sister_post_fx_sync_controls(&engine, &controls);
    for (int frame = 0; frame < 4800; ++frame)
        (void)ts_sister_post_fx_process(&engine, 3u,
            (TsStereoFrame){0.0f, 0.0f}, 0);
    for (int frame = 0; frame < 4800; ++frame) {
        TsStereoFrame output = ts_sister_post_fx_process(&engine, 3u,
            (TsStereoFrame){frame == 0 ? 0.8f : 0.0f, 0.0f}, 0);
        float mono = (output.l + output.r) * 0.5f;
        double energy = (double)mono * mono;
        double difference = (double)mono - previous;
        assert_finite(output);
        if (frame >= 96 && frame < 720) {
            early_energy += energy;
            early_roughness += difference * difference;
        }
        if (frame >= 1920 && frame < 4320) {
            late_energy += energy;
            late_roughness += difference * difference;
        }
        previous = mono;
    }
    assert(early_energy > 1.0e-5);
    assert(late_energy > 1.0e-8);
    /* Every trip around the virtual tape loses high-frequency energy. */
    assert(late_roughness / late_energy <
           early_roughness / early_energy);
    ts_sister_post_fx_free(&engine);
}

static double reverb_signature(float size)
{
    TsSisterPostFxEngine engine = {0};
    TsSisterFxControls controls;
    double signature = 0.0;
    assert(ts_sister_post_fx_init(&engine, 48000u));
    ts_sister_fx_controls_default(&controls);
    controls.reverb_size = size;
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

static void test_reverb_space_and_distortion(void)
{
    static const float sizes[] = {0.08f, 0.35f, 0.68f, 0.96f};
    double signatures[sizeof(sizes) / sizeof(sizes[0])];
    TsSisterPostFxEngine engine = {0};
    TsSisterFxControls controls;
    double difference = 0.0;
    assert(fabsf(ts_sister_reverb_size_scale(0.0f) - 0.55f) < 1.0e-5f);
    assert(fabsf(ts_sister_reverb_size_scale(1.0f) - 3.50f) < 1.0e-5f);
    assert(fabsf(ts_sister_reverb_decay_seconds(0.0f) - 0.35f) < 1.0e-5f);
    assert(fabsf(ts_sister_reverb_decay_seconds(1.0f) - 120.0f) < 1.0e-3f);
    for (size_t size = 0u; size < sizeof(sizes) / sizeof(sizes[0]); ++size)
        signatures[size] = reverb_signature(sizes[size]);
    for (size_t a = 0u; a < sizeof(sizes) / sizeof(sizes[0]); ++a)
        for (size_t b = a + 1u; b < sizeof(sizes) / sizeof(sizes[0]); ++b)
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

static void test_reverb_level_density_and_width(void)
{
    TsSisterPostFxEngine engine = {0};
    TsSisterFxControls controls;
    double dry_energy = 0.0;
    double output_energy = 0.0;
    double wet_energy = 0.0;
    double stereo_difference = 0.0;
    size_t active_tail_frames = 0u;

    assert(ts_sister_post_fx_init(&engine, 48000u));
    ts_sister_fx_controls_default(&controls);
    controls.delay_targets = 0u;
    controls.distortion_targets = 0u;
    controls.reverb_size = 0.76f;
    controls.reverb_decay = 0.72f;
    controls.reverb_mix = 0.5f;
    ts_sister_post_fx_sync_controls(&engine, &controls);
    for (int frame = 0; frame < 144000; ++frame) {
        TsStereoFrame input = {
            0.25f * sinf((float)frame * 0.017f) +
            0.12f * sinf((float)frame * 0.043f),
            0.22f * sinf((float)frame * 0.019f) -
            0.10f * sinf((float)frame * 0.037f)
        };
        TsStereoFrame output = ts_sister_post_fx_process(
            &engine, 3u, input, 0);
        assert_finite(output);
        if (frame >= 48000) {
            dry_energy += input.l * input.l + input.r * input.r;
            output_energy += output.l * output.l + output.r * output.r;
        }
    }
    /* The middle of MIX must surround the source rather than behaving like a
       channel fader. A little energy lift is acceptable; collapse is not. */
    assert(output_energy > dry_energy * 0.78);
    assert(output_energy < dry_energy * 2.0);
    controls.reverb_mix = 1.0f;
    ts_sister_post_fx_set_controls(&engine, &controls);
    dry_energy = 0.0;
    output_energy = 0.0;
    for (int frame = 0; frame < 96000; ++frame) {
        TsStereoFrame input = {
            0.25f * sinf((float)frame * 0.017f) +
            0.12f * sinf((float)frame * 0.043f),
            0.22f * sinf((float)frame * 0.019f) -
            0.10f * sinf((float)frame * 0.037f)
        };
        TsStereoFrame output = ts_sister_post_fx_process(
            &engine, 3u, input, 0);
        assert_finite(output);
        if (frame >= 48000) {
            dry_energy += input.l * input.l + input.r * input.r;
            output_energy += output.l * output.l + output.r * output.r;
        }
    }
    assert(output_energy > dry_energy * 0.20);
    assert(output_energy < dry_energy * 2.0);
    ts_sister_post_fx_free(&engine);

    memset(&engine, 0, sizeof(engine));
    assert(ts_sister_post_fx_init(&engine, 48000u));
    ts_sister_fx_controls_default(&controls);
    controls.delay_targets = 0u;
    controls.distortion_targets = 0u;
    controls.reverb_size = 0.88f;
    controls.reverb_decay = 0.82f;
    controls.reverb_mix = 1.0f;
    ts_sister_post_fx_sync_controls(&engine, &controls);
    for (int frame = 0; frame < 96000; ++frame) {
        TsStereoFrame input = {frame == 0 ? 0.8f : 0.0f, 0.0f};
        TsStereoFrame output = ts_sister_post_fx_process(
            &engine, 3u, input, 0);
        assert_finite(output);
        if (frame >= 12000) {
            float magnitude = fabsf(output.l) + fabsf(output.r);
            wet_energy += output.l * output.l + output.r * output.r;
            stereo_difference += fabs((double)output.l - (double)output.r);
            if (magnitude > 1.0e-7f) ++active_tail_frames;
        }
    }
    assert(wet_energy > 1.0e-10);
    assert(stereo_difference > 0.01);
    assert(active_tail_frames > 50000u);
    ts_sister_post_fx_free(&engine);
}

static void test_targets_mono_and_ordinary(void)
{
    TsSisterRuntime runtime;
    TsSisterRuntime full_runtime;
    TsSisterRuntime half_runtime;
    TsSisterFxControls controls;
    char error[128];
    ts_sister_runtime_init(&runtime);
    ts_sister_runtime_init(&full_runtime);
    ts_sister_runtime_init(&half_runtime);
    assert(ts_sister_runtime_reconfigure(&runtime, 48000u, 2u,
                                         error, sizeof(error)));
    assert(ts_sister_runtime_reconfigure(&full_runtime, 48000u, 2u,
                                         error, sizeof(error)));
    assert(ts_sister_runtime_reconfigure(&half_runtime, 48000u, 2u,
                                         error, sizeof(error)));
    assert(!runtime.enabled);
    assert(!runtime.machine.buffer.data);
    assert(runtime.post_fx.ready);
    assert(ts_sister_runtime_process_ordinary_post_fx(
        &runtime, (TsStereoFrame){0.25f, -0.5f}).l == 0.25f);
    {
        TsSisterParameters parameters = runtime.parameters;
        TsStereoFrame full = {0.0f, 0.0f};
        TsStereoFrame half = {0.0f, 0.0f};
        TsStereoFrame output = {0.0f, 0.0f};
        parameters.fx.distortion_drive = 0.9f;
        parameters.fx.distortion_tone = 0.5f;
        parameters.fx.distortion_mix = 1.0f;
        parameters.fx.distortion_targets = TS_SISTER_EFFECT_TARGET_MIX;
        parameters.fx_return_gain = 0.0f;
        ts_sister_runtime_set_parameters(&runtime, &parameters);
        parameters.fx_return_gain = 1.0f;
        ts_sister_runtime_set_parameters(&full_runtime, &parameters);
        parameters.fx_return_gain = 0.5f;
        ts_sister_runtime_set_parameters(&half_runtime, &parameters);
        for (int i = 0; i < 4000; ++i) {
            output = ts_sister_runtime_process_ordinary_post_fx(
                &runtime, (TsStereoFrame){0.25f, -0.5f});
            full = ts_sister_runtime_process_ordinary_post_fx(
                &full_runtime, (TsStereoFrame){0.25f, -0.5f});
            half = ts_sister_runtime_process_ordinary_post_fx(
                &half_runtime, (TsStereoFrame){0.25f, -0.5f});
        }
        assert(output.l == 0.25f && output.r == -0.5f);
        assert(fabsf(full.l - 0.25f) > 0.01f ||
               fabsf(full.r + 0.5f) > 0.01f);
        assert(fabsf(half.l - (0.25f + (full.l - 0.25f) * 0.5f)) < 0.001f);
        assert(fabsf(half.r - (-0.5f + (full.r + 0.5f) * 0.5f)) < 0.001f);
    }
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
    ts_sister_runtime_free(&full_runtime);
    ts_sister_runtime_free(&half_runtime);
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
            controls.reverb_size =
                (float)((i / 97) % 101) / 100.0f;
            ts_sister_post_fx_set_controls(&engine, &controls);
        }
        assert_finite(ts_sister_post_fx_process(&engine, 3u,
            (TsStereoFrame){i == 0 ? 1.0f : 0.0f,
                            i == 13 ? -1.0f : 0.0f}, 0));
    }
    ts_sister_post_fx_free(&engine);
}

static void test_interrupted_wheel_handoffs(void)
{
    TsSisterPostFxEngine engine = {0};
    TsSisterFxControls controls;
    TsStereoFrame previous = {0.0f, 0.0f};
    TsStereoFrame current;

    assert(ts_sister_post_fx_init(&engine, 1000u));
    ts_sister_fx_controls_default(&controls);
    controls.reverb_targets = 0u;
    controls.distortion_targets = 0u;
    controls.delay_mix = 1.0f;
    controls.delay_feedback = 0.0f;
    controls.delay_time = 0.0f;
    ts_sister_post_fx_set_controls(&engine, &controls);
    for (int frame = 0; frame < 2500; ++frame)
        previous = ts_sister_post_fx_process(&engine, 3u,
            (TsStereoFrame){0.65f * sinf((float)frame * 0.071f), 0.0f}, 0);
    {
        float before = engine.delay[3].delay_current;
        controls.delay_time = 1.0f;
        ts_sister_post_fx_set_controls(&engine, &controls);
        current = ts_sister_post_fx_process(&engine, 3u,
            (TsStereoFrame){0.65f * sinf(2500.0f * 0.071f), 0.0f}, 0);
        assert_finite(current);
        assert(engine.delay[3].delay_current > before);
        assert(engine.delay[3].delay_current - before <= 0.5001f);
    }
    for (int frame = 0; frame < 50; ++frame) {
        float before = engine.delay[3].delay_current;
        previous = ts_sister_post_fx_process(&engine, 3u,
            (TsStereoFrame){0.65f * sinf((float)(2501 + frame) * 0.071f),
                            0.0f}, 0);
        assert_finite(previous);
        assert(engine.delay[3].delay_current >= before);
        assert(engine.delay[3].delay_current - before <= 0.5001f);
    }
    {
        float before = engine.delay[3].delay_current;
        controls.delay_time = 0.0f;
        ts_sister_post_fx_set_controls(&engine, &controls);
        current = ts_sister_post_fx_process(&engine, 3u,
            (TsStereoFrame){0.65f * sinf(2551.0f * 0.071f), 0.0f}, 0);
        assert_finite(current);
        assert(engine.delay[3].delay_current < before);
        assert(before - engine.delay[3].delay_current <= 1.0001f);
    }
    controls.delay_time = 1.0f;
    ts_sister_post_fx_set_controls(&engine, &controls);
    for (int frame = 0; frame < 5; ++frame)
        previous = ts_sister_post_fx_process(&engine, 3u,
            (TsStereoFrame){0.65f * sinf((float)(2552 + frame) * 0.071f),
                            0.0f}, 0);
    controls.delay_time = 0.5f;
    ts_sister_post_fx_set_controls(&engine, &controls);
    current = ts_sister_post_fx_process(&engine, 3u,
        (TsStereoFrame){0.65f * sinf(2557.0f * 0.071f), 0.0f}, 0);
    assert_finite(current);
    assert(fabsf(current.l - previous.l) < 1.5f);
    assert(fabsf(current.r - previous.r) < 1.5f);
    ts_sister_post_fx_free(&engine);

    memset(&engine, 0, sizeof(engine));
    assert(ts_sister_post_fx_init(&engine, 1000u));
    ts_sister_fx_controls_default(&controls);
    controls.delay_targets = 0u;
    controls.distortion_targets = 0u;
    controls.reverb_mix = 1.0f;
    controls.reverb_decay = 0.62f;
    ts_sister_post_fx_set_controls(&engine, &controls);
    for (int frame = 0; frame < 2500; ++frame)
        previous = ts_sister_post_fx_process(&engine, 3u,
            (TsStereoFrame){0.28f * sinf((float)frame * 0.043f),
                            0.19f * cosf((float)frame * 0.037f)}, 0);
    controls.reverb_size = 0.92f;
    ts_sister_post_fx_set_controls(&engine, &controls);
    for (int frame = 0; frame < 10; ++frame)
        previous = ts_sister_post_fx_process(&engine, 3u,
            (TsStereoFrame){0.28f * sinf((float)(2500 + frame) * 0.043f),
                            0.19f * cosf((float)(2500 + frame) * 0.037f)}, 0);
    controls.reverb_size = 0.18f;
    ts_sister_post_fx_set_controls(&engine, &controls);
    current = ts_sister_post_fx_process(&engine, 3u,
        (TsStereoFrame){0.28f * sinf(2510.0f * 0.043f),
                        0.19f * cosf(2510.0f * 0.037f)}, 0);
    assert(fabsf(current.l - previous.l) < 1.0e-5f);
    assert(fabsf(current.r - previous.r) < 1.0e-5f);
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
    parameters = runtime.parameters;
    parameters.fx.enabled = 0;
    parameters.fx.master_transition = ts_sister_fx_transition_normalized(10.0f);
    ts_sister_runtime_set_parameters(&runtime, &parameters);
    for (int frame = 0; frame < 10; ++frame)
        (void)ts_sister_runtime_process_frame(&runtime, &sources);
    assert(ts_sister_post_fx_master_engage(&runtime.post_fx) == 0.0f);
    assert(runtime.master_feedback_current == 0.0f);
    assert(runtime.master_feedback_previous.l == 0.0f);
    assert(runtime.master_feedback_previous.r == 0.0f);
    ts_sister_runtime_disable(&runtime);
    assert(runtime.master_feedback_previous.l == 0.0f);
    assert(runtime.master_feedback_previous.r == 0.0f);
    assert(!runtime.enabled && runtime.post_fx.ready);
    ts_sister_runtime_free(&runtime);
}

static void test_timed_performance_bypasses(void)
{
    TsSisterPostFxEngine engine = {0};
    TsSisterFxControls controls;
    TsSisterFxTransitionStatus status;
    assert(fabsf(ts_sister_fx_transition_ms(0.0f) - 10.0f) < 0.001f);
    assert(fabsf(ts_sister_fx_transition_ms(1.0f) - 3600000.0f) < 1.0f);
    assert(ts_sister_post_fx_init(&engine, 1000u));
    ts_sister_fx_controls_default(&controls);
    assert(controls.enabled && controls.reverb_enabled &&
           controls.delay_enabled && controls.distortion_enabled);
    controls.transition = ts_sister_fx_transition_normalized(1000.0f);
    controls.distortion_mix = 1.0f;
    controls.distortion_enabled = 0;
    ts_sister_post_fx_set_controls(&engine, &controls);
    assert(engine.distortion_engage.remaining == 1000u);
    status = ts_sister_post_fx_transition_status(&engine);
    assert(status.active && status.progress == 0.0f &&
           status.source == TS_SISTER_FX_TRANSITION_DISTORTION &&
           !status.target_enabled);
    for (int frame = 0; frame < 500; ++frame)
        assert_finite(ts_sister_post_fx_process(&engine, 3u,
            (TsStereoFrame){0.3f, -0.2f}, 0));
    assert(fabsf(engine.distortion_engage.current - 0.5f) < 0.002f);
    status = ts_sister_post_fx_transition_status(&engine);
    assert(status.active && status.progress > 0.499f &&
           status.progress < 0.501f &&
           status.source == TS_SISTER_FX_TRANSITION_DISTORTION);
    controls.distortion_enabled = 1;
    ts_sister_post_fx_set_controls(&engine, &controls);
    assert(engine.distortion_engage.remaining == 1000u);
    status = ts_sister_post_fx_transition_status(&engine);
    assert(status.source == TS_SISTER_FX_TRANSITION_DISTORTION &&
           status.target_enabled && status.progress == 0.0f);
    for (int frame = 0; frame < 1000; ++frame)
        assert_finite(ts_sister_post_fx_process(&engine, 3u,
            (TsStereoFrame){0.3f, -0.2f}, 0));
    assert(engine.distortion_engage.current == 1.0f);

    controls.reverb_enabled = controls.delay_enabled = 0;
    controls.enabled = 0;
    controls.transition = ts_sister_fx_transition_normalized(1000.0f);
    controls.master_transition =
        ts_sister_fx_transition_normalized(1000.0f);
    ts_sister_post_fx_set_controls(&engine, &controls);
    assert(engine.master_engage.remaining == 1000u);
    assert(engine.reverb_engage.remaining == 1000u);
    assert(engine.delay_engage.remaining == 1000u);
    status = ts_sister_post_fx_transition_status(&engine);
    assert(status.source == TS_SISTER_FX_TRANSITION_MASTER &&
           !status.target_enabled && status.progress == 0.0f);
    for (int frame = 0; frame < 1000; ++frame)
        assert_finite(ts_sister_post_fx_process(&engine, 3u,
            (TsStereoFrame){0.1f, -0.1f}, 0));
    assert(ts_sister_post_fx_master_engage(&engine) == 0.0f);
    assert(engine.reverb_engage.current == 0.0f);
    assert(engine.delay_engage.current == 0.0f);
    {
        TsStereoFrame input = {0.31f, -0.27f};
        TsStereoFrame output;
        int active = 0;
        assert(ts_sister_post_fx_transition_progress(&engine, &active) == 1.0f);
        assert(!active);
        status = ts_sister_post_fx_transition_status(&engine);
        assert(!status.active &&
               status.source == TS_SISTER_FX_TRANSITION_NONE);
        output = ts_sister_post_fx_process(&engine, 3u, input, 0);
        assert(output.l == input.l && output.r == input.r);
        controls.distortion_enabled = 0;
        ts_sister_post_fx_set_controls(&engine, &controls);
        status = ts_sister_post_fx_transition_status(&engine);
        assert(status.source == TS_SISTER_FX_TRANSITION_DISTORTION &&
               !status.target_enabled);
        for (int frame = 0; frame < 1000; ++frame)
            (void)ts_sister_post_fx_process(&engine, 3u, input, 0);
        controls.distortion_enabled = 1;
        ts_sister_post_fx_set_controls(&engine, &controls);
        status = ts_sister_post_fx_transition_status(&engine);
        assert(status.source == TS_SISTER_FX_TRANSITION_DISTORTION &&
               status.target_enabled);
        for (int frame = 0; frame < 1000; ++frame)
            (void)ts_sister_post_fx_process(&engine, 3u, input, 0);
        output = ts_sister_post_fx_process(&engine, 3u, input, 0);
        assert(output.l == input.l && output.r == input.r);
    }
    ts_sister_post_fx_free(&engine);
}

static void test_master_gate_restore_and_live_retime(void)
{
    TsSisterPostFxEngine engine = {0};
    TsSisterFxControls controls;
    TsSisterFxTransitionStatus status;
    TsStereoFrame input = {0.37f, -0.21f};
    TsStereoFrame output;

    assert(ts_sister_post_fx_init(&engine, 1000u));
    ts_sister_fx_controls_default(&controls);
    controls.enabled = 0;
    controls.reverb_enabled = 1;
    controls.delay_enabled = 1;
    controls.distortion_enabled = 1;
    controls.reverb_mix = 1.0f;
    controls.delay_mix = 1.0f;
    controls.distortion_mix = 1.0f;
    controls.distortion_drive = 1.0f;
    controls.transition = ts_sister_fx_transition_normalized(1000.0f);
    controls.master_transition =
        ts_sister_fx_transition_normalized(1000.0f);
    ts_sister_post_fx_sync_controls(&engine, &controls);

    /* Restoring an OFF preset must never leave the live master at the engine's
       default ON value. Individual effects remain armed behind the dry gate. */
    assert(engine.controls.enabled == 0);
    assert(ts_sister_post_fx_master_engage(&engine) == 0.0f);
    assert(engine.reverb_engage.current == 1.0f);
    assert(engine.delay_engage.current == 1.0f);
    assert(engine.distortion_engage.current == 1.0f);
    output = ts_sister_post_fx_process(&engine, 3u, input, 0);
    assert(output.l == input.l && output.r == input.r);

    /* Arming/disarming an individual processor behind Master OFF cannot leak
       audio, but the individual transition is still allowed to complete. */
    controls.distortion_enabled = 0;
    ts_sister_post_fx_set_controls(&engine, &controls);
    for (int frame = 0; frame < 1000; ++frame) {
        output = ts_sister_post_fx_process(&engine, 3u, input, 0);
        assert(output.l == input.l && output.r == input.r);
    }
    controls.distortion_enabled = 1;
    ts_sister_post_fx_set_controls(&engine, &controls);
    for (int frame = 0; frame < 1000; ++frame)
        (void)ts_sister_post_fx_process(&engine, 3u, input, 0);
    assert(engine.distortion_engage.current == 1.0f);

    /* Master is the sole audible gate for the already-armed chain. */
    controls.enabled = 1;
    ts_sister_post_fx_set_controls(&engine, &controls);
    for (int frame = 0; frame < 250; ++frame)
        (void)ts_sister_post_fx_process(&engine, 3u, input, 0);
    assert(fabsf(engine.master_engage.current - 0.25f) < 0.002f);
    status = ts_sister_post_fx_transition_status(&engine);
    assert(status.active && status.source == TS_SISTER_FX_TRANSITION_MASTER &&
           status.target_enabled && status.progress > 0.249f &&
           status.progress < 0.251f);

    /* A wheel edit to the shared duration retimes the active fade in place,
       preserving progress rather than requiring an off/on retrigger. */
    controls.transition = ts_sister_fx_transition_normalized(100.0f);
    ts_sister_post_fx_set_controls(&engine, &controls);
    assert(engine.master_engage.total == 1000u);
    assert(engine.master_engage.remaining == 750u);
    controls.master_transition = ts_sister_fx_transition_normalized(100.0f);
    ts_sister_post_fx_set_controls(&engine, &controls);
    assert(engine.master_engage.total == 100u);
    assert(engine.master_engage.remaining == 75u);
    status = ts_sister_post_fx_transition_status(&engine);
    assert(status.progress > 0.249f && status.progress < 0.251f);
    for (int frame = 0; frame < 75; ++frame)
        (void)ts_sister_post_fx_process(&engine, 3u, input, 0);
    assert(ts_sister_post_fx_master_engage(&engine) == 1.0f);
    assert(!ts_sister_post_fx_transition_status(&engine).active);
    ts_sister_post_fx_free(&engine);
}

static void test_master_zero_is_absolute_return_valve(void)
{
    TsSisterPostFxEngine engine = {0};
    TsSisterFxControls controls;

    assert(ts_sister_post_fx_init(&engine, 1000u));
    ts_sister_fx_controls_default(&controls);
    controls.reverb_mix = 1.0f;
    controls.reverb_decay = 1.0f;
    controls.delay_mix = 1.0f;
    controls.delay_feedback = 0.95f;
    controls.distortion_mix = 1.0f;
    controls.distortion_drive = 1.0f;
    controls.transition = ts_sister_fx_transition_normalized(1000.0f);
    controls.master_transition = ts_sister_fx_transition_normalized(10.0f);
    ts_sister_post_fx_sync_controls(&engine, &controls);

    /* Load every processor and target with non-zero history and tails. */
    for (int frame = 0; frame < 3000; ++frame) {
        for (size_t target = 0u;
             target < TS_SISTER_EFFECT_PROCESSOR_COUNT; ++target) {
            TsStereoFrame input = frame == 0 ?
                (TsStereoFrame){0.9f, -0.7f} :
                (TsStereoFrame){0.0f, 0.0f};
            (void)ts_sister_post_fx_process(&engine, target, input, 0);
        }
    }

    controls.enabled = 0;
    ts_sister_post_fx_set_controls(&engine, &controls);
    for (int frame = 0; frame < 10; ++frame)
        (void)ts_sister_post_fx_process(
            &engine, TS_SISTER_EFFECT_PROCESSOR_COUNT - 1u,
            (TsStereoFrame){0.13f, -0.09f}, 0);
    assert(ts_sister_post_fx_master_engage(&engine) == 0.0f);

    /* Individual automated faders remain independent behind the faucet. They
       may move, reverse, and leave active tails, but Master zero must return
       the input bit-for-bit on H1/H2/H3/MIX. */
    for (int cycle = 0; cycle < 200; ++cycle) {
        controls.reverb_enabled = (cycle & 1) != 0;
        controls.delay_enabled = (cycle & 2) != 0;
        controls.distortion_enabled = (cycle & 4) != 0;
        ts_sister_post_fx_set_controls(&engine, &controls);
        for (size_t target = 0u;
             target < TS_SISTER_EFFECT_PROCESSOR_COUNT; ++target) {
            TsStereoFrame input = {
                0.31f - (float)target * 0.03f,
                -0.27f + (float)target * 0.02f
            };
            TsStereoFrame output =
                ts_sister_post_fx_process(&engine, target, input, 0);
            assert(output.l == input.l && output.r == input.r);
        }
    }
    ts_sister_post_fx_free(&engine);
}

int main(void)
{
    test_defaults_and_identity();
    test_delay_length_and_stereo();
    test_equal_power_chain_makeup();
    test_tape_feedback_tail();
    test_reverb_space_and_distortion();
    test_reverb_level_density_and_width();
    test_targets_mono_and_ordinary();
    test_exclusive_target_handoff();
    test_rapid_sweeps_finite();
    test_interrupted_wheel_handoffs();
    test_master_feedback_causality();
    test_timed_performance_bypasses();
    test_master_gate_restore_and_live_retime();
    test_master_zero_is_absolute_return_valve();
    puts("sister post-effects tests passed");
    return 0;
}
