#include "tapesister/sister_fallout.h"
#include "tapesister/sister_runtime.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static int same_frame(TsStereoFrame a, TsStereoFrame b)
{
    return a.l == b.l && a.r == b.r;
}

static void test_defaults_are_true_bypass(void)
{
    TsSisterFalloutEngine engine;
    TsSisterFalloutControls controls;
    TsStereoFrame input = {0.25f, -0.50f};
    ts_sister_fallout_controls_default(&controls);
    assert(controls.enabled == 0);
    assert(ts_sister_fallout_init(&engine, 48000u));
    ts_sister_fallout_set_controls(&engine, &controls);
    for (int i = 0; i < 128; ++i) {
        TsSisterFalloutResult out = ts_sister_fallout_process(&engine, input);
        assert(same_frame(out.output, input));
        assert(out.wet.l == 0.0f && out.wet.r == 0.0f);
    }
    assert(engine.write_clock == 0u);
    assert(engine.active == 0);
    ts_sister_fallout_free(&engine);
}

static void test_deterministic_effect_and_cold_reenable(void)
{
    TsSisterFalloutEngine a, b;
    TsSisterFalloutControls controls;
    TsStereoFrame silence = {0.0f, 0.0f};
    int changed = 0;
    ts_sister_fallout_controls_default(&controls);
    controls.enabled = 1;
    controls.mix = 1.0f;
    controls.noise = 0.35f;
    controls.drop_enabled = 1;
    controls.pan_enabled = 1;
    controls.skip_enabled = 1;
    controls.bit_enabled = 1;
    controls.pitch_enabled = 1;
    assert(ts_sister_fallout_init(&a, 48000u));
    assert(ts_sister_fallout_init(&b, 48000u));
    ts_sister_fallout_seed(&a, 123456u);
    ts_sister_fallout_seed(&b, 123456u);
    ts_sister_fallout_set_controls(&a, &controls);
    ts_sister_fallout_set_controls(&b, &controls);
    for (int i = 0; i < 12000; ++i) {
        TsStereoFrame input = {
            sinf((float)i * 0.013f) * 0.4f,
            cosf((float)i * 0.017f) * 0.3f
        };
        TsSisterFalloutResult x = ts_sister_fallout_process(&a, input);
        TsSisterFalloutResult y = ts_sister_fallout_process(&b, input);
        assert(isfinite(x.output.l) && isfinite(x.output.r));
        assert(same_frame(x.output, y.output));
        if (fabsf(x.output.l - input.l) > 0.001f ||
            fabsf(x.output.r - input.r) > 0.001f) changed = 1;
    }
    assert(changed);

    controls.enabled = 0;
    ts_sister_fallout_set_controls(&a, &controls);
    for (int i = 0; i < 1000; ++i)
        (void)ts_sister_fallout_process(&a, silence);
    assert(a.active == 0);
    assert(a.valid_frames == 0u);
    assert(same_frame(ts_sister_fallout_process(&a, silence).output, silence));

    controls.enabled = 1;
    controls.noise = 0.0f;
    ts_sister_fallout_set_controls(&a, &controls);
    for (int i = 0; i < 1000; ++i) {
        TsSisterFalloutResult out = ts_sister_fallout_process(&a, silence);
        assert(fabsf(out.output.l) < 0.000001f);
        assert(fabsf(out.output.r) < 0.000001f);
    }
    ts_sister_fallout_free(&a);
    ts_sister_fallout_free(&b);
}

static void test_sanitize_and_memory(void)
{
    TsSisterFalloutEngine engine;
    TsSisterFalloutControls controls;
    ts_sister_fallout_controls_default(&controls);
    controls.mix = INFINITY;
    controls.feedback = -4.0f;
    controls.bit_quality = NAN;
    controls.pitch = 12.0f;
    controls.noise_type = (TsSisterFalloutNoiseType)99;
    controls.lfo_targets = UINT32_MAX;
    ts_sister_fallout_controls_sanitize(&controls);
    assert(controls.mix == 0.0f);
    assert(controls.feedback == 0.0f);
    assert(controls.bit_quality == 0.01f);
    assert(controls.pitch == 1.0f);
    assert(controls.noise_type == TS_SISTER_FALLOUT_NOISE_WHITE);
    assert(controls.lfo_targets == TS_SISTER_FALLOUT_LFO_ALL);
    assert(ts_sister_fallout_init(&engine, 44100u));
    assert(ts_sister_fallout_memory_bytes(&engine) ==
           (size_t)44100u * 20u * 2u * sizeof(float));
    ts_sister_fallout_free(&engine);
}

static void test_transition_noise_and_centered_lfo(void)
{
    TsSisterFalloutEngine engine;
    TsSisterFalloutControls controls;
    TsStereoFrame silence = {0.0f, 0.0f};
    float signatures[TS_SISTER_FALLOUT_NOISE_COUNT] = {0};
    assert(fabsf(ts_sister_fallout_transition_ms(0.0f) - 10.0f) < 0.001f);
    assert(fabsf(ts_sister_fallout_transition_ms(1.0f) - 60000.0f) < 0.1f);
    assert(fabsf(ts_sister_fallout_transition_normalized(10.0f)) < 0.0001f);
    assert(fabsf(ts_sister_fallout_transition_normalized(60000.0f) - 1.0f) <
           0.0001f);
    assert(fabsf(ts_sister_fallout_lfo_hz(0.0f) - 1.0f / 3600.0f) <
           0.000001f);
    assert(fabsf(ts_sister_fallout_lfo_hz(1.0f) - 10.0f) < 0.001f);
    /* A 0..10 control centered at 5 travels 4..6 at 20% and 0..10 at 100%. */
    assert(fabsf(ts_sister_fallout_lfo_modulate(0.5f, 0.2f, -1.0f) -
                 0.4f) < 0.0001f);
    assert(fabsf(ts_sister_fallout_lfo_modulate(0.5f, 0.2f, 1.0f) -
                 0.6f) < 0.0001f);
    assert(fabsf(ts_sister_fallout_lfo_modulate(0.5f, 1.0f, -1.0f)) <
           0.0001f);
    assert(fabsf(ts_sister_fallout_lfo_modulate(0.5f, 1.0f, 1.0f) -
                 1.0f) < 0.0001f);
    /* Near an edge, depth remains centered instead of flattening at a limit. */
    assert(fabsf(ts_sister_fallout_lfo_modulate(0.8f, 1.0f, -1.0f) -
                 0.6f) < 0.0001f);

    ts_sister_fallout_controls_default(&controls);
    controls.enabled = 1;
    controls.transition = 1.0f;
    assert(ts_sister_fallout_init(&engine, 1000u));
    ts_sister_fallout_set_controls(&engine, &controls);
    for (int i = 0; i < 1000; ++i)
        (void)ts_sister_fallout_process(&engine, silence);
    assert(ts_sister_fallout_engage(&engine) > 0.015f);
    assert(ts_sister_fallout_engage(&engine) < 0.018f);
    ts_sister_fallout_free(&engine);

    for (int type = 0; type < TS_SISTER_FALLOUT_NOISE_COUNT; ++type) {
        ts_sister_fallout_controls_default(&controls);
        controls.enabled = 1;
        controls.mix = 1.0f;
        controls.noise = 1.0f;
        controls.noise_type = (TsSisterFalloutNoiseType)type;
        assert(ts_sister_fallout_init(&engine, 1000u));
        ts_sister_fallout_seed(&engine, 1234u);
        ts_sister_fallout_set_controls(&engine, &controls);
        for (int i = 0; i < 2000; ++i) {
            TsSisterFalloutResult out =
                ts_sister_fallout_process(&engine, silence);
            signatures[type] += out.output.l * (float)((i % 17) + 1);
        }
        assert(isfinite(signatures[type]));
        assert(strcmp(ts_sister_fallout_noise_type_name(
                          (TsSisterFalloutNoiseType)type), "") != 0);
        ts_sister_fallout_free(&engine);
    }
    for (int a = 0; a < TS_SISTER_FALLOUT_NOISE_COUNT; ++a)
        for (int b = a + 1; b < TS_SISTER_FALLOUT_NOISE_COUNT; ++b)
            assert(fabsf(signatures[a] - signatures[b]) > 0.001f);

    ts_sister_fallout_controls_default(&controls);
    controls.enabled = 1;
    controls.feedback = 0.5f;
    controls.lfo_intensity = 1.0f;
    controls.lfo_targets = TS_SISTER_FALLOUT_LFO_FEEDBACK;
    assert(ts_sister_fallout_init(&engine, 1000u));
    ts_sister_fallout_set_controls(&engine, &controls);
    engine.lfo_phase = 0.25;
    (void)ts_sister_fallout_process(&engine, silence);
    assert(ts_sister_fallout_feedback_amount(&engine) > 0.999f);
    assert(engine.controls.feedback == 0.5f);
    engine.lfo_phase = 0.75;
    (void)ts_sister_fallout_process(&engine, silence);
    assert(ts_sister_fallout_feedback_amount(&engine) < 0.001f);
    assert(engine.controls.feedback == 0.5f);
    ts_sister_fallout_free(&engine);
}

static void test_runtime_feedback_is_wet_only_and_causal(void)
{
    TsSisterRuntime runtime;
    TsSisterParameters parameters;
    TsSisterSourceFrames sources = {0};
    char error[160];
    int observed = 0;
    ts_sister_runtime_init(&runtime);
    assert(ts_sister_runtime_enable(&runtime, 1000u, 2u, 2u, 5.0,
                                    error, sizeof(error)));
    ts_sister_runtime_set_sources(&runtime, TS_SISTER_SOURCE_EXT);
    parameters = runtime.parameters;
    parameters.head1_level = 1.0f;
    parameters.head1_time_ms = 8.0f;
    parameters.headroom = 1.0f;
    parameters.fx.fallout.enabled = 1;
    parameters.fx.fallout.mix = 1.0f;
    parameters.fx.fallout.feedback = 0.8f;
    parameters.fx.fallout.noise = 0.2f;
    ts_sister_runtime_set_parameters(&runtime, &parameters);
    for (int i = 0; i < 600; ++i) {
        sources.external.l = sinf((float)i * 0.09f) * 0.3f;
        sources.external.r = cosf((float)i * 0.07f) * 0.3f;
        (void)ts_sister_runtime_process_frame(&runtime, &sources);
        if (fabsf(runtime.fallout_feedback_previous.l) > 0.0001f ||
            fabsf(runtime.fallout_feedback_previous.r) > 0.0001f)
            observed = 1;
    }
    assert(observed);
    parameters.fx.fallout.enabled = 0;
    ts_sister_runtime_set_parameters(&runtime, &parameters);
    for (int i = 0; i < 1000; ++i)
        (void)ts_sister_runtime_process_frame(&runtime, &sources);
    assert(runtime.fallout_feedback_current == 0.0f);
    assert(runtime.fallout_feedback_previous.l == 0.0f);
    assert(runtime.fallout_feedback_previous.r == 0.0f);
    ts_sister_runtime_free(&runtime);
}

int main(void)
{
    test_defaults_are_true_bypass();
    test_deterministic_effect_and_cold_reenable();
    test_sanitize_and_memory();
    test_transition_noise_and_centered_lfo();
    test_runtime_feedback_is_wet_only_and_causal();
    puts("Sister Fallout tests passed");
    return 0;
}
