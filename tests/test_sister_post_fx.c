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

static void direct_set_controls(TsSisterPostFxEngine *engine,
                                const TsSisterFxControls *controls)
{
    ts_sister_post_fx_set_controls(engine, controls);
}

static void direct_sync_controls(TsSisterPostFxEngine *engine,
                                 const TsSisterFxControls *controls)
{
    ts_sister_post_fx_sync_controls(engine, controls);
}

static void legacy_set_controls(TsSisterPostFxEngine *engine,
                                TsSisterFxControls *controls)
{
    ts_sister_fx_controls_migrate_legacy(controls);
    direct_set_controls(engine, controls);
}

static void legacy_sync_controls(TsSisterPostFxEngine *engine,
                                 TsSisterFxControls *controls)
{
    ts_sister_fx_controls_migrate_legacy(controls);
    ts_sister_post_fx_sync_controls(engine, controls);
}

#define ts_sister_post_fx_set_controls legacy_set_controls
#define ts_sister_post_fx_sync_controls legacy_sync_controls

static void test_defaults_and_identity(void)
{
    TsSisterPostFxEngine engine = {0};
    TsSisterFxControls controls;
    ts_sister_fx_controls_default(&controls);
    assert(controls.reverb_targets == TS_SISTER_EFFECT_TARGET_MIX);
    assert(controls.delay_targets == TS_SISTER_EFFECT_TARGET_MIX);
    assert(controls.distortion_targets == TS_SISTER_EFFECT_TARGET_MIX);
    assert(controls.grain_targets == TS_SISTER_EFFECT_TARGET_MIX);
    assert(controls.reverb_mix == 0.0f);
    assert(controls.delay_mix == 0.0f);
    assert(controls.distortion_mix == 0.0f);
    assert(controls.grain_mix == 0.0f);
    assert(controls.reverb_gain_db == 0.0f);
    assert(controls.delay_gain_db == 0.0f);
    assert(controls.distortion_gain_db == 0.0f);
    assert(controls.grain_gain_db == 0.0f);
    assert(controls.grain_pitch == 0.5f);
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

static void test_explicit_slots_override_stale_legacy_fields(void)
{
    TsSisterPostFxEngine engine = {0};
    TsSisterFxControls controls;
    assert(ts_sister_post_fx_init(&engine, 48000u));
    ts_sister_fx_controls_default(&controls);
    /* Current files may retain old named fields for downgrade/debugging.
       They must never replace an explicitly stored pedalboard. */
    controls.distortion_enabled = 0;
    controls.distortion_mix = 1.0f;
    controls.distortion_targets = TS_SISTER_EFFECT_TARGET_H2;
    direct_sync_controls(&engine, &controls);
    assert(engine.controls.slot[0].type == TS_SISTER_FX_DISTORTION);
    assert(engine.controls.slot[0].enabled == 1);
    assert(engine.controls.slot[0].placement == TS_SISTER_FX_PLACE_POST);
    assert(engine.controls.slot[0].mix == 0.0f);
    ts_sister_post_fx_free(&engine);
}

static void test_grain_cloud_ranges_pitch_and_width(void)
{
    TsSisterPostFxEngine engine = {0};
    TsSisterFxControls controls;
    double wet_energy = 0.0;
    double stereo_difference = 0.0;
    size_t maximum_voices = 0u;
    int heard = 0;

    assert(fabsf(ts_sister_grain_size_ms(0.0f) - 8.0f) < 0.001f);
    assert(fabsf(ts_sister_grain_size_ms(1.0f) - 1000.0f) < 0.01f);
    assert(fabsf(ts_sister_grain_density_hz(0.0f) - 0.25f) < 0.001f);
    assert(fabsf(ts_sister_grain_density_hz(1.0f) - 120.0f) < 0.001f);
    assert(fabsf(ts_sister_grain_pitch_semitones(0.0f) + 24.0f) < 0.001f);
    assert(fabsf(ts_sister_grain_pitch_semitones(0.5f)) < 0.001f);
    assert(fabsf(ts_sister_grain_pitch_semitones(1.0f) - 24.0f) < 0.001f);

    assert(ts_sister_post_fx_init(&engine, 48000u));
    ts_sister_fx_controls_default(&controls);
    controls.reverb_targets = 0u;
    controls.delay_targets = 0u;
    controls.distortion_targets = 0u;
    controls.grain_size = 0.34f;
    controls.grain_density = 1.0f;
    controls.grain_pitch = 0.5f;
    controls.grain_mix = 1.0f;
    ts_sister_post_fx_sync_controls(&engine, &controls);
    for (int frame = 0; frame < 144000; ++frame) {
        TsStereoFrame input = {
            0.28f * sinf((float)frame * 0.031f),
            0.19f * cosf((float)frame * 0.027f)
        };
        TsStereoFrame output = ts_sister_post_fx_process(&engine, 3u, input, 0);
        size_t voices = 0u;
        assert_finite(output);
        for (size_t voice = 0u; voice < TS_SISTER_GRAIN_VOICES; ++voice)
            voices += engine.grain[1][4].voice[voice].active != 0;
        if (voices > maximum_voices) maximum_voices = voices;
        assert(voices <= TS_SISTER_GRAIN_VOICES);
        if (frame > 12000) {
            wet_energy += output.l * output.l + output.r * output.r;
            stereo_difference += fabs((double)output.l - output.r);
            heard |= fabsf(output.l) + fabsf(output.r) > 1.0e-5f;
        }
    }
    assert(heard);
    assert(wet_energy > 1.0);
    assert(stereo_difference > 10.0);
    assert(maximum_voices > 0u);
    ts_sister_post_fx_free(&engine);

    memset(&engine, 0, sizeof(engine));
    assert(ts_sister_post_fx_init(&engine, 1000u));
    ts_sister_fx_controls_default(&controls);
    controls.reverb_targets = 0u;
    controls.delay_targets = 0u;
    controls.distortion_targets = 0u;
    controls.grain_mix = 0.0f;
    controls.grain_size = 0.0f;
    controls.grain_density = 1.0f;
    controls.grain_pitch = 1.0f;
    ts_sister_post_fx_sync_controls(&engine, &controls);
    for (int frame = 0; frame < 1000; ++frame)
        (void)ts_sister_post_fx_process(&engine, 3u,
            (TsStereoFrame){0.2f, -0.1f}, 0);
    controls.grain_mix = 1.0f;
    ts_sister_post_fx_set_controls(&engine, &controls);
    for (int frame = 0; frame < 200; ++frame)
        (void)ts_sister_post_fx_process(&engine, 3u,
            (TsStereoFrame){0.2f, -0.1f}, 0);
    {
        int found = 0;
        for (size_t voice = 0u; voice < TS_SISTER_GRAIN_VOICES; ++voice)
            if (engine.grain[1][4].voice[voice].active) {
                assert(fabs(engine.grain[1][4].voice[voice].read_step - 4.0) <
                       0.001);
                found = 1;
            }
        assert(found);
    }
    ts_sister_post_fx_free(&engine);
}

static void test_post_mix_makeup_gain_and_bypass(void)
{
    TsSisterPostFxEngine engine = {0};
    TsSisterFxControls controls;
    const TsStereoFrame input = {0.10f, -0.05f};
    TsStereoFrame output = {0.0f, 0.0f};

    assert(ts_sister_post_fx_init(&engine, 1000u));
    ts_sister_fx_controls_default(&controls);
    controls.transition = ts_sister_fx_transition_normalized(10.0f);
    controls.distortion_gain_db = 12.0f;
    ts_sister_post_fx_sync_controls(&engine, &controls);
    for (int frame = 0; frame < 500; ++frame)
        output = ts_sister_post_fx_process(&engine, 3u, input, 0);
    /* Gain follows Mix, so it remains useful at exact dry and feeds the next
       processor in the chain. +12 dB is approximately 3.981x. */
    assert(output.l > 0.397f && output.l < 0.399f);
    assert(output.r < -0.198f && output.r > -0.200f);

    controls.distortion_enabled = 0;
    ts_sister_post_fx_set_controls(&engine, &controls);
    for (int frame = 0; frame < 10; ++frame)
        output = ts_sister_post_fx_process(&engine, 3u, input, 0);
    output = ts_sister_post_fx_process(&engine, 3u, input, 0);
    assert(output.l == input.l);
    assert(output.r == input.r);
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
        parameters.fx.slot[0].parameter_a = 0.9f;
        parameters.fx.slot[0].parameter_b = 0.5f;
        parameters.fx.slot[0].mix = 1.0f;
        parameters.fx.slot[0].placement = TS_SISTER_FX_PLACE_POST;
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

static void test_slot_placement_handoff(void)
{
    TsSisterPostFxEngine engine = {0};
    TsSisterFxControls controls;
    assert(ts_sister_post_fx_init(&engine, 48000u));
    ts_sister_fx_controls_default(&controls);
    controls.distortion_mix = 1.0f;
    controls.distortion_targets = TS_SISTER_EFFECT_TARGET_H1;
    ts_sister_post_fx_set_controls(&engine, &controls);
    assert(engine.slot[0].has_pending);
    assert(engine.slot[0].active.placement == TS_SISTER_FX_PLACE_POST);
    assert(engine.slot[0].pending.placement == TS_SISTER_FX_PLACE_H1);
    for (int frame = 0; frame < 800; ++frame)
        for (size_t target = 0u; target < TS_SISTER_EFFECT_PROCESSOR_COUNT;
             ++target)
            (void)ts_sister_post_fx_process(&engine, target,
                (TsStereoFrame){0.0f, 0.0f}, 0);
    assert(!engine.slot[0].has_pending);
    assert(engine.slot[0].active.placement == TS_SISTER_FX_PLACE_H1);
    controls.distortion_targets = TS_SISTER_EFFECT_TARGET_MIX;
    ts_sister_post_fx_set_controls(&engine, &controls);
    assert(engine.slot[0].has_pending);
    for (int frame = 0; frame < 300; ++frame)
        for (size_t target = 0u; target < TS_SISTER_EFFECT_PROCESSOR_COUNT;
             ++target)
            (void)ts_sister_post_fx_process(&engine, target,
                (TsStereoFrame){0.0f, 0.0f}, 0);
    assert(engine.slot[0].has_pending);
    for (int frame = 0; frame < 500; ++frame)
        for (size_t target = 0u; target < TS_SISTER_EFFECT_PROCESSOR_COUNT;
             ++target)
            (void)ts_sister_post_fx_process(&engine, target,
                (TsStereoFrame){0.0f, 0.0f}, 0);
    assert(!engine.slot[0].has_pending);
    assert(engine.slot[0].active.placement == TS_SISTER_FX_PLACE_POST);
    ts_sister_post_fx_free(&engine);
}

static void test_rapid_sweeps_finite(void)
{
    TsSisterPostFxEngine engine = {0};
    TsSisterFxControls controls;
    assert(ts_sister_post_fx_init(&engine, 48000u));
    ts_sister_fx_controls_default(&controls);
    controls.reverb_mix = controls.delay_mix = controls.distortion_mix = 1.0f;
    controls.grain_mix = 1.0f;
    controls.delay_feedback = 1.0f;
    controls.distortion_drive = 1.0f;
    controls.master_feedback = 1.0f;
    for (int i = 0; i < 200000; ++i) {
        if ((i % 97) == 0) {
            controls.delay_time = (float)((i / 97) % 101) / 100.0f;
            controls.reverb_decay = 1.0f - controls.delay_time;
            controls.reverb_size =
                (float)((i / 97) % 101) / 100.0f;
            controls.grain_size = controls.delay_time;
            controls.grain_density = 1.0f - controls.delay_time;
            controls.grain_pitch = controls.reverb_decay;
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
        float before = engine.delay[2][4].delay_current;
        controls.delay_time = 1.0f;
        ts_sister_post_fx_set_controls(&engine, &controls);
        current = ts_sister_post_fx_process(&engine, 3u,
            (TsStereoFrame){0.65f * sinf(2500.0f * 0.071f), 0.0f}, 0);
        assert_finite(current);
        assert(engine.delay[2][4].delay_current > before);
        assert(engine.delay[2][4].delay_current - before <= 0.5001f);
    }
    for (int frame = 0; frame < 50; ++frame) {
        float before = engine.delay[2][4].delay_current;
        previous = ts_sister_post_fx_process(&engine, 3u,
            (TsStereoFrame){0.65f * sinf((float)(2501 + frame) * 0.071f),
                            0.0f}, 0);
        assert_finite(previous);
        assert(engine.delay[2][4].delay_current >= before);
        assert(engine.delay[2][4].delay_current - before <= 0.5001f);
    }
    {
        float before = engine.delay[2][4].delay_current;
        controls.delay_time = 0.0f;
        ts_sister_post_fx_set_controls(&engine, &controls);
        current = ts_sister_post_fx_process(&engine, 3u,
            (TsStereoFrame){0.65f * sinf(2551.0f * 0.071f), 0.0f}, 0);
        assert_finite(current);
        assert(engine.delay[2][4].delay_current < before);
        assert(before - engine.delay[2][4].delay_current <= 1.0001f);
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
           controls.delay_enabled && controls.distortion_enabled &&
           controls.grain_enabled);
    controls.transition = ts_sister_fx_transition_normalized(1000.0f);
    controls.distortion_mix = 1.0f;
    controls.distortion_enabled = 0;
    ts_sister_post_fx_set_controls(&engine, &controls);
    assert(engine.slot[0].engage.remaining == 1000u);
    status = ts_sister_post_fx_transition_status(&engine);
    assert(status.active && status.progress == 0.0f &&
           status.source == TS_SISTER_FX_TRANSITION_SLOT_1 &&
           !status.target_enabled);
    for (int frame = 0; frame < 500; ++frame)
        assert_finite(ts_sister_post_fx_process(&engine, 3u,
            (TsStereoFrame){0.3f, -0.2f}, 0));
    assert(fabsf(engine.slot[0].engage.current - 0.5f) < 0.002f);
    status = ts_sister_post_fx_transition_status(&engine);
    assert(status.active && status.progress > 0.499f &&
           status.progress < 0.501f &&
           status.source == TS_SISTER_FX_TRANSITION_SLOT_1);
    controls.distortion_enabled = 1;
    ts_sister_post_fx_set_controls(&engine, &controls);
    assert(engine.slot[0].engage.remaining == 1000u);
    status = ts_sister_post_fx_transition_status(&engine);
    assert(status.source == TS_SISTER_FX_TRANSITION_SLOT_1 &&
           status.target_enabled && status.progress == 0.0f);
    for (int frame = 0; frame < 1000; ++frame)
        assert_finite(ts_sister_post_fx_process(&engine, 3u,
            (TsStereoFrame){0.3f, -0.2f}, 0));
    assert(engine.slot[0].engage.current == 1.0f);

    controls.reverb_enabled = controls.delay_enabled = controls.grain_enabled = 0;
    controls.enabled = 0;
    controls.transition = ts_sister_fx_transition_normalized(1000.0f);
    controls.master_transition =
        ts_sister_fx_transition_normalized(1000.0f);
    ts_sister_post_fx_set_controls(&engine, &controls);
    assert(engine.master_engage.remaining == 1000u);
    assert(engine.slot[3].engage.remaining == 1000u);
    assert(engine.slot[2].engage.remaining == 1000u);
    assert(engine.slot[1].engage.remaining == 1000u);
    status = ts_sister_post_fx_transition_status(&engine);
    assert(status.source == TS_SISTER_FX_TRANSITION_MASTER &&
           !status.target_enabled && status.progress == 0.0f);
    for (int frame = 0; frame < 1000; ++frame)
        assert_finite(ts_sister_post_fx_process(&engine, 3u,
            (TsStereoFrame){0.1f, -0.1f}, 0));
    assert(ts_sister_post_fx_master_engage(&engine) == 0.0f);
    assert(engine.slot[3].engage.current == 0.0f);
    assert(engine.slot[2].engage.current == 0.0f);
    assert(engine.slot[1].engage.current == 0.0f);
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
        assert(status.source == TS_SISTER_FX_TRANSITION_SLOT_1 &&
               !status.target_enabled);
        for (int frame = 0; frame < 1000; ++frame)
            (void)ts_sister_post_fx_process(&engine, 3u, input, 0);
        controls.distortion_enabled = 1;
        ts_sister_post_fx_set_controls(&engine, &controls);
        status = ts_sister_post_fx_transition_status(&engine);
        assert(status.source == TS_SISTER_FX_TRANSITION_SLOT_1 &&
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
    controls.grain_enabled = 1;
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
    assert(engine.slot[3].engage.current == 1.0f);
    assert(engine.slot[2].engage.current == 1.0f);
    assert(engine.slot[0].engage.current == 1.0f);
    assert(engine.slot[1].engage.current == 1.0f);
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
    assert(engine.slot[0].engage.current == 1.0f);

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
        controls.grain_enabled = (cycle & 8) != 0;
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

static TsStereoFrame process_pedalboard_frame(TsSisterPostFxEngine *engine,
                                              TsStereoFrame input)
{
    TsStereoFrame output = ts_sister_post_fx_process_pre(engine, input, 0);
    for (size_t location = 0u;
         location < TS_SISTER_EFFECT_PROCESSOR_COUNT; ++location)
        output = ts_sister_post_fx_process(engine, location, output, 0);
    return output;
}

static void test_four_slot_pedalboard(void)
{
    TsSisterPostFxEngine first = {0};
    TsSisterPostFxEngine second = {0};
    TsSisterFxControls controls;
    TsSisterFxControls swapped;
    TsStereoFrame a = {0.0f, 0.0f};
    TsStereoFrame b = {0.0f, 0.0f};
    TsSisterFxTransitionStatus status;

    ts_sister_fx_controls_default(&controls);
    assert(controls.slot[0].type == TS_SISTER_FX_DISTORTION);
    assert(controls.slot[1].type == TS_SISTER_FX_GRAIN);
    assert(controls.slot[2].type == TS_SISTER_FX_DELAY);
    assert(controls.slot[3].type == TS_SISTER_FX_REVERB);
    for (size_t slot = 0u; slot < TS_SISTER_FX_SLOT_COUNT; ++slot)
        assert(controls.slot[slot].placement == TS_SISTER_FX_PLACE_POST);

    assert(ts_sister_post_fx_init(&first, 1000u));
    assert(ts_sister_post_fx_init(&second, 1000u));
    for (size_t slot = 0u; slot < TS_SISTER_FX_SLOT_COUNT; ++slot) {
        controls.slot[slot].type = TS_SISTER_FX_EMPTY;
        controls.slot[slot].enabled = 1;
        controls.slot[slot].placement = TS_SISTER_FX_PLACE_POST;
    }
    controls.slot[0] = (TsSisterFxSlotControls){
        TS_SISTER_FX_DISTORTION, 1, TS_SISTER_FX_PLACE_POST,
        12.0f, 0.32f, 0.25f, 0.5f, 1.0f
    };
    controls.slot[1] = (TsSisterFxSlotControls){
        TS_SISTER_FX_DISTORTION, 1, TS_SISTER_FX_PLACE_POST,
        -12.0f, 0.92f, 0.86f, 0.5f, 1.0f
    };
    swapped = controls;
    {
        TsSisterFxSlotControls temporary = swapped.slot[0];
        swapped.slot[0] = swapped.slot[1];
        swapped.slot[1] = temporary;
    }
    direct_sync_controls(&first, &controls);
    direct_sync_controls(&second, &swapped);
    for (int frame = 0; frame < 2000; ++frame) {
        TsStereoFrame input = {
            0.28f * sinf((float)frame * 0.071f),
            -0.19f * cosf((float)frame * 0.043f)
        };
        a = process_pedalboard_frame(&first, input);
        b = process_pedalboard_frame(&second, input);
        assert_finite(a);
        assert_finite(b);
    }
    assert(fabsf(a.l - b.l) + fabsf(a.r - b.r) > 0.001f);

    controls.transition = ts_sister_fx_transition_normalized(1000.0f);
    controls.slot[0].placement = TS_SISTER_FX_PLACE_PRE;
    direct_set_controls(&first, &controls);
    status = ts_sister_post_fx_effect_transition_status(&first);
    assert(status.active && status.topology &&
           status.source == TS_SISTER_FX_TRANSITION_SLOT_1);
    for (int frame = 0; frame < 500; ++frame)
        assert_finite(process_pedalboard_frame(
            &first, (TsStereoFrame){0.2f, -0.1f}));
    status = ts_sister_post_fx_effect_transition_status(&first);
    assert(status.active && status.topology && status.progress > 0.49f &&
           status.progress < 0.51f);
    for (int frame = 0; frame < 500; ++frame)
        (void)process_pedalboard_frame(
            &first, (TsStereoFrame){0.2f, -0.1f});
    assert(!first.slot[0].has_pending);
    assert(first.slot[0].active.placement == TS_SISTER_FX_PLACE_PRE);
    ts_sister_post_fx_free(&first);
    ts_sister_post_fx_free(&second);

    memset(&first, 0, sizeof(first));
    assert(ts_sister_post_fx_init(&first, 1000u));
    ts_sister_fx_controls_default(&controls);
    for (size_t slot = 0u; slot < 3u; ++slot) {
        controls.slot[slot] = (TsSisterFxSlotControls){
            TS_SISTER_FX_GRAIN, 1, TS_SISTER_FX_PLACE_POST,
            0.0f, 0.18f + 0.12f * (float)slot, 1.0f,
            0.5f + 0.08f * (float)slot, 1.0f
        };
    }
    controls.slot[3] = (TsSisterFxSlotControls){
        TS_SISTER_FX_REVERB, 1, TS_SISTER_FX_PLACE_POST,
        0.0f, 0.72f, 0.62f, 0.5f, 0.55f
    };
    direct_sync_controls(&first, &controls);
    for (int frame = 0; frame < 12000; ++frame) {
        TsStereoFrame output = process_pedalboard_frame(&first,
            (TsStereoFrame){0.18f * sinf((float)frame * 0.061f),
                            0.14f * cosf((float)frame * 0.047f)});
        assert_finite(output);
    }
    for (size_t slot = 0u; slot < 3u; ++slot) {
        size_t voices = 0u;
        for (size_t voice = 0u; voice < TS_SISTER_GRAIN_VOICES; ++voice)
            voices += first.grain[slot][4].voice[voice].active != 0;
        assert(voices > 0u && voices <= TS_SISTER_GRAIN_VOICES);
    }
    ts_sister_post_fx_free(&first);
}

static void test_pre_is_recorded_post_is_not(void)
{
    TsSisterRuntime pre;
    TsSisterRuntime post;
    TsSisterParameters parameters;
    TsSisterSourceFrames source = {0};
    TsSisterRuntimeFrame pre_frame;
    TsSisterRuntimeFrame post_frame;
    char error[128];
    ts_sister_runtime_init(&pre);
    ts_sister_runtime_init(&post);
    assert(ts_sister_runtime_enable(&pre, 1000u, 2u, 2u, 0.1,
                                    error, sizeof(error)));
    assert(ts_sister_runtime_enable(&post, 1000u, 2u, 2u, 0.1,
                                    error, sizeof(error)));
    parameters = pre.parameters;
    parameters.headroom = 1.0f;
    for (size_t slot = 0u; slot < TS_SISTER_FX_SLOT_COUNT; ++slot)
        parameters.fx.slot[slot].type = TS_SISTER_FX_EMPTY;
    parameters.fx.slot[0] = (TsSisterFxSlotControls){
        TS_SISTER_FX_DISTORTION, 1, TS_SISTER_FX_PLACE_PRE,
        0.0f, 1.0f, 0.8f, 0.5f, 1.0f
    };
    ts_sister_runtime_set_parameters(&pre, &parameters);
    parameters.fx.slot[0].placement = TS_SISTER_FX_PLACE_POST;
    ts_sister_runtime_set_parameters(&post, &parameters);
    ts_sister_runtime_set_sources(&pre, TS_SISTER_SOURCE_PREVIEW);
    ts_sister_runtime_set_sources(&post, TS_SISTER_SOURCE_PREVIEW);
    for (int frame = 0; frame < 30; ++frame) {
        (void)ts_sister_runtime_process_frame(&pre, &source);
        (void)ts_sister_runtime_process_frame(&post, &source);
    }
    source.preview = (TsStereoFrame){0.42f, -0.31f};
    pre_frame = ts_sister_runtime_process_frame(&pre, &source);
    post_frame = ts_sister_runtime_process_frame(&post, &source);
    assert(fabsf(pre.machine.last_output.write.l -
                 post.machine.last_output.write.l) > 0.01f);
    assert(fabsf(pre_frame.input.l - post_frame.input.l) > 0.01f);
    assert(fabsf(post.machine.last_output.write.l - source.preview.l) < 0.01f);
    ts_sister_runtime_free(&pre);
    ts_sister_runtime_free(&post);
}

int main(void)
{
    test_defaults_and_identity();
    test_explicit_slots_override_stale_legacy_fields();
    test_post_mix_makeup_gain_and_bypass();
    test_grain_cloud_ranges_pitch_and_width();
    test_delay_length_and_stereo();
    test_equal_power_chain_makeup();
    test_tape_feedback_tail();
    test_reverb_space_and_distortion();
    test_reverb_level_density_and_width();
    test_targets_mono_and_ordinary();
    test_slot_placement_handoff();
    test_rapid_sweeps_finite();
    test_interrupted_wheel_handoffs();
    test_master_feedback_causality();
    test_timed_performance_bypasses();
    test_master_gate_restore_and_live_retime();
    test_master_zero_is_absolute_return_valve();
    test_four_slot_pedalboard();
    test_pre_is_recorded_post_is_not();
    puts("sister post-effects tests passed");
    return 0;
}
