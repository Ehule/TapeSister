#include "tapesister/sister_fallout.h"
#include "tapesister/sister_runtime.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

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
    ts_sister_fallout_controls_sanitize(&controls);
    assert(controls.mix == 0.0f);
    assert(controls.feedback == 0.0f);
    assert(controls.bit_quality == 0.01f);
    assert(controls.pitch == 1.0f);
    assert(ts_sister_fallout_init(&engine, 44100u));
    assert(ts_sister_fallout_memory_bytes(&engine) ==
           (size_t)44100u * 20u * 2u * sizeof(float));
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
    test_runtime_feedback_is_wet_only_and_causal();
    puts("Sister Fallout tests passed");
    return 0;
}
