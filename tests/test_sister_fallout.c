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
    controls.rise_mode = (TsSisterFalloutRiseMode)99;
    controls.rise_targets = UINT32_MAX;
    ts_sister_fallout_controls_sanitize(&controls);
    assert(controls.mix == 0.0f);
    assert(controls.feedback == 0.0f);
    assert(controls.bit_quality == 0.01f);
    assert(controls.pitch == 1.0f);
    assert(controls.noise_type == TS_SISTER_FALLOUT_NOISE_WHITE);
    assert(controls.lfo_targets == TS_SISTER_FALLOUT_LFO_ALL);
    assert(controls.rise_mode == TS_SISTER_FALLOUT_RISE_ONE_SHOT);
    assert(controls.rise_targets == TS_SISTER_FALLOUT_LFO_ALL);
    assert(ts_sister_fallout_init(&engine, 44100u));
    assert(ts_sister_fallout_memory_bytes(&engine) ==
           (size_t)44100u * 20u * 2u * sizeof(float));
    ts_sister_fallout_free(&engine);
}

static void test_transition_noise_and_centered_lfo(void)
{
    TsSisterFalloutEngine engine;
    TsSisterFalloutControls controls;
    TsSisterFalloutTransitionStatus status;
    TsStereoFrame silence = {0.0f, 0.0f};
    float signatures[TS_SISTER_FALLOUT_NOISE_COUNT] = {0};
    assert(fabsf(ts_sister_fallout_transition_ms(0.0f) - 10.0f) < 0.001f);
    assert(fabsf(ts_sister_fallout_transition_ms(1.0f) - 3600000.0f) < 1.0f);
    assert(fabsf(ts_sister_fallout_transition_normalized(10.0f)) < 0.0001f);
    assert(fabsf(ts_sister_fallout_transition_normalized(3600000.0f) - 1.0f) <
           0.0001f);
    assert(fabsf(ts_sister_fallout_lfo_hz(0.0f) - 1.0f / 3600.0f) <
           0.000001f);
    assert(fabsf(ts_sister_fallout_lfo_hz(1.0f) - 10.0f) < 0.001f);
    assert(fabsf(ts_sister_fallout_rise_seconds(0.0f) - 1.0f) < 0.001f);
    assert(fabsf(ts_sister_fallout_rise_seconds(1.0f) - 14400.0f) < 0.1f);
    assert(fabsf(ts_sister_fallout_rise_normalized(1.0f)) < 0.0001f);
    assert(fabsf(ts_sister_fallout_rise_normalized(14400.0f) - 1.0f) <
           0.0001f);
    assert(fabsf(ts_sister_fallout_rise_modulate(0.0f, 1.0f, 0.5f) -
                 0.5f) < 0.0001f);
    assert(fabsf(ts_sister_fallout_rise_modulate(0.5f, 0.5f, 1.0f) -
                 0.75f) < 0.0001f);
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
    controls.master_transition =
        ts_sister_fallout_transition_normalized(60000.0f);
    assert(ts_sister_fallout_init(&engine, 1000u));
    ts_sister_fallout_set_controls(&engine, &controls);
    status = ts_sister_fallout_master_transition_status(&engine);
    assert(status.active && status.progress == 0.0f &&
           status.source == TS_SISTER_FALLOUT_TRANSITION_MASTER &&
           status.target_enabled);
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
    controls.mix = 1.0f;
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
    controls.mix = 0.0f;
    ts_sister_fallout_set_controls(&engine, &controls);
    for (int frame = 0; frame < 25; ++frame)
        (void)ts_sister_fallout_process(&engine, silence);
    assert(ts_sister_fallout_feedback_amount(&engine) == 0.0f);
    ts_sister_fallout_free(&engine);

    /* RISE moves the center; the LFO then oscillates around that center. */
    ts_sister_fallout_controls_default(&controls);
    controls.enabled = 1;
    controls.mix = 1.0f;
    controls.feedback = 0.25f;
    controls.rise_length = 0.0f;
    controls.rise_intensity = 1.0f;
    controls.rise_targets = TS_SISTER_FALLOUT_LFO_FEEDBACK;
    controls.lfo_intensity = 0.4f;
    controls.lfo_targets = TS_SISTER_FALLOUT_LFO_FEEDBACK;
    assert(ts_sister_fallout_init(&engine, 1000u));
    ts_sister_fallout_set_controls(&engine, &controls);
    engine.rise_phase = 0.5;
    engine.rise_smoothed = 0.5f;
    engine.lfo_phase = 0.25;
    (void)ts_sister_fallout_process(&engine, silence);
    /* RISE center 0.625; LFO excursion is .4 * .375 = .15. */
    assert(fabsf(ts_sister_fallout_feedback_amount(&engine) - 0.775f) <
           0.001f);
    engine.rise_phase = 0.999;
    engine.rise_smoothed = 0.999f;
    engine.lfo_phase = 0.0;
    (void)ts_sister_fallout_process(&engine, silence);
    assert(ts_sister_fallout_feedback_amount(&engine) > 0.999f);
    assert(engine.rise_smoothed > 0.999f);
    (void)ts_sister_fallout_process(&engine, silence);
    assert(engine.rise_smoothed > 0.89f && engine.rise_smoothed < 0.91f);
    assert(engine.rise_one_shot_complete);
    for (int frame = 0; frame < 9; ++frame)
        (void)ts_sister_fallout_process(&engine, silence);
    assert(engine.rise_smoothed < 0.0001f);
    controls.rise_targets ^= TS_SISTER_FALLOUT_LFO_NOISE;
    ts_sister_fallout_set_controls(&engine, &controls);
    /* Adding a destination after a completed one-shot re-arms the shared
       clock so the new destination receives a meaningful sweep. */
    assert(!engine.rise_one_shot_complete && engine.rise_phase == 0.0);
    engine.rise_one_shot_complete = 1;
    engine.rise_phase = 0.75;
    ++controls.rise_retrigger;
    ts_sister_fallout_set_controls(&engine, &controls);
    assert(!engine.rise_one_shot_complete && engine.rise_phase == 0.0);
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
    parameters.fx.fallout.master_transition =
        ts_sister_fallout_transition_normalized(10.0f);
    ts_sister_runtime_set_parameters(&runtime, &parameters);
    for (int i = 0; i < 10; ++i)
        (void)ts_sister_runtime_process_frame(&runtime, &sources);
    assert(ts_sister_fallout_engage(&runtime.fallout) == 0.0f);
    assert(runtime.fallout_feedback_current == 0.0f);
    assert(runtime.fallout_feedback_previous.l == 0.0f);
    assert(runtime.fallout_feedback_previous.r == 0.0f);
    ts_sister_runtime_free(&runtime);
}

static void test_master_gates_and_modulation_disconnect(void)
{
    TsSisterFalloutEngine engine;
    TsSisterFalloutControls controls;
    TsStereoFrame silence = {0.0f, 0.0f};
    ts_sister_fallout_controls_default(&controls);
    controls.enabled = 1;
    controls.pitch_enabled = 1;
    controls.pitch = 0.5f;
    controls.pitch_ramp = 0.0f;
    controls.pitch_rate = 0.0f;
    controls.lfo_intensity = 1.0f;
    controls.lfo_targets = TS_SISTER_FALLOUT_LFO_PITCH;
    assert(ts_sister_fallout_init(&engine, 1000u));
    ts_sister_fallout_set_controls(&engine, &controls);
    engine.lfo_phase = 0.25;
    (void)ts_sister_fallout_process(&engine, silence);
    assert(engine.playback_target == 3.0f);

    /* Disconnecting PITCH modulation re-arms its event immediately and the
       saved 0.5 center selects the 0.5x member of the ratio family. */
    controls.lfo_targets = 0u;
    engine.next_pitch = UINT64_MAX;
    ts_sister_fallout_set_controls(&engine, &controls);
    assert(engine.next_pitch == engine.write_clock);
    for (int frame = 0; frame < 21; ++frame)
        (void)ts_sister_fallout_process(&engine, silence);
    assert(engine.playback_target == 0.5f);

    /* The panel switch is a true master gate. OFF cannot be defeated by a
       remembered modulation assignment and returns to unity click-free. */
    controls.pitch_enabled = 0;
    controls.lfo_targets = TS_SISTER_FALLOUT_LFO_ALL;
    controls.rise_targets = TS_SISTER_FALLOUT_LFO_ALL;
    engine.drop_gain = engine.drop_target = 0.25f;
    engine.pan = engine.pan_target = 0.9f;
    engine.hold_remaining = 5u;
    engine.skip_fade_remaining = engine.skip_fade_total = 5u;
    ts_sister_fallout_set_controls(&engine, &controls);
    for (int frame = 0; frame < 25; ++frame)
        (void)ts_sister_fallout_process(&engine, silence);
    assert(engine.playback_target == 1.0f &&
           fabsf(engine.playback_rate - 1.0f) < 0.0001f);
    assert(engine.drop_gain == 1.0f);
    assert(engine.pan == 0.5f);
    assert(engine.hold_remaining == 0u);
    assert(engine.skip_fade_remaining == 0u);
    ts_sister_fallout_free(&engine);
}

static void test_rise_reset_declicks_both_modes(void)
{
    TsStereoFrame silence = {0.0f, 0.0f};
    for (int mode = TS_SISTER_FALLOUT_RISE_SAW;
         mode <= TS_SISTER_FALLOUT_RISE_ONE_SHOT; ++mode) {
        TsSisterFalloutEngine engine;
        TsSisterFalloutControls controls;
        ts_sister_fallout_controls_default(&controls);
        controls.enabled = 1;
        controls.rise_mode = (TsSisterFalloutRiseMode)mode;
        controls.rise_length = 0.0f;
        controls.rise_intensity = 1.0f;
        controls.rise_targets = TS_SISTER_FALLOUT_LFO_MIX;
        assert(ts_sister_fallout_init(&engine, 1000u));
        ts_sister_fallout_set_controls(&engine, &controls);
        engine.rise_phase = 0.999;
        engine.rise_smoothed = 0.999f;
        (void)ts_sister_fallout_process(&engine, silence);
        assert(engine.rise_smoothed > 0.999f);
        (void)ts_sister_fallout_process(&engine, silence);
        assert(engine.rise_smoothed > 0.89f && engine.rise_smoothed < 0.91f);
        for (int frame = 0; frame < 9; ++frame)
            (void)ts_sister_fallout_process(&engine, silence);
        assert(engine.rise_smoothed < 0.011f);
        ts_sister_fallout_free(&engine);
    }
}

static void test_preset_transition_uses_current_transition(void)
{
    TsSisterFalloutEngine engine;
    TsSisterFalloutControls current;
    TsSisterFalloutControls target;
    TsStereoFrame silence = {0.0f, 0.0f};
    ts_sister_fallout_controls_default(&current);
    current.enabled = 1;
    current.transition = ts_sister_fallout_transition_normalized(10.0f);
    assert(ts_sister_fallout_init(&engine, 1000u));
    ts_sister_fallout_set_controls(&engine, &current);
    for (int frame = 0; frame < 10; ++frame)
        (void)ts_sister_fallout_process(&engine, silence);
    assert(ts_sister_fallout_engage(&engine) > 0.999f);

    target = current;
    target.transition = ts_sister_fallout_transition_normalized(5000.0f);
    target.mix = 0.2f;
    target.noise_type = TS_SISTER_FALLOUT_NOISE_BROWN;
    target.rise_mode = TS_SISTER_FALLOUT_RISE_SAW;
    target.rise_targets = TS_SISTER_FALLOUT_LFO_MIX;
    ts_sister_fallout_recall_preset(&engine, &target);
    assert(engine.preset_transition_stage == 1);
    for (int frame = 0; frame < 4; ++frame)
        (void)ts_sister_fallout_process(&engine, silence);
    assert(engine.controls.mix == current.mix);
    (void)ts_sister_fallout_process(&engine, silence);
    assert(engine.preset_transition_stage == 2);
    assert(ts_sister_fallout_engage(&engine) < 0.0001f);
    assert(engine.controls.enabled == 1);
    assert(fabsf(engine.controls.mix - 0.2f) < 0.0001f);
    assert(engine.controls.noise_type == TS_SISTER_FALLOUT_NOISE_BROWN);
    assert(engine.controls.rise_mode == TS_SISTER_FALLOUT_RISE_SAW);
    assert(engine.rise_phase < 0.002);
    for (int frame = 0; frame < 5; ++frame)
        (void)ts_sister_fallout_process(&engine, silence);
    assert(engine.preset_transition_stage == 0);
    assert(ts_sister_fallout_engage(&engine) > 0.999f);
    ts_sister_fallout_free(&engine);

    ts_sister_fallout_controls_default(&current);
    assert(ts_sister_fallout_init(&engine, 1000u));
    ts_sister_fallout_set_controls(&engine, &current);
    target = current;
    target.enabled = 1;
    target.noise = 0.75f;
    ts_sister_fallout_recall_preset(&engine, &target);
    assert(engine.controls.enabled == 0);
    assert(fabsf(engine.controls.noise - 0.75f) < 0.0001f);
    assert(engine.preset_transition_stage == 0);
    ts_sister_fallout_free(&engine);
}

static void test_running_preset_transition_retimes_in_place(void)
{
    TsSisterFalloutEngine engine;
    TsSisterFalloutControls current;
    TsSisterFalloutControls target;
    TsStereoFrame silence = {0.0f, 0.0f};
    int active = 0;
    float progress;
    ts_sister_fallout_controls_default(&current);
    current.enabled = 1;
    current.transition = ts_sister_fallout_transition_normalized(1000.0f);
    assert(ts_sister_fallout_init(&engine, 1000u));
    ts_sister_fallout_set_controls(&engine, &current);
    for (int frame = 0; frame < 10; ++frame)
        (void)ts_sister_fallout_process(&engine, silence);
    target = current;
    target.mix = 0.19f;
    ts_sister_fallout_recall_preset(&engine, &target);
    for (int frame = 0; frame < 250; ++frame)
        (void)ts_sister_fallout_process(&engine, silence);
    progress = ts_sister_fallout_preset_transition_progress(&engine, &active);
    assert(active && progress > 0.249f && progress < 0.251f);

    current.transition = ts_sister_fallout_transition_normalized(100.0f);
    current.component_transition =
        ts_sister_fallout_transition_normalized(250.0f);
    current.master_transition =
        ts_sister_fallout_transition_normalized(400.0f);
    ts_sister_fallout_set_controls(&engine, &current);
    assert(engine.preset_transition_stage == 1 &&
           engine.preset_transition_total == 100u &&
           engine.preset_gain_remaining == 25u);
    assert(engine.controls.component_transition ==
           current.component_transition);
    assert(engine.controls.master_transition == current.master_transition);
    progress = ts_sister_fallout_preset_transition_progress(&engine, &active);
    assert(active && progress > 0.249f && progress < 0.251f);
    for (int frame = 0; frame < 75; ++frame)
        (void)ts_sister_fallout_process(&engine, silence);
    assert(engine.preset_transition_stage == 0);
    assert(fabsf(engine.controls.mix - target.mix) < 0.0001f);
    ts_sister_fallout_free(&engine);
}

static void test_running_modulation_targets_fade_to_shared_phase(void)
{
    TsSisterFalloutEngine engine;
    TsSisterFalloutControls controls;
    TsStereoFrame silence = {0.0f, 0.0f};
    double rise_phase;
    double lfo_phase;
    float value;
    ts_sister_fallout_controls_default(&controls);
    controls.enabled = 1;
    controls.mix = 1.0f;
    controls.feedback = 0.20f;
    controls.rise_mode = TS_SISTER_FALLOUT_RISE_SAW;
    controls.rise_length = ts_sister_fallout_rise_normalized(3600.0f);
    controls.rise_intensity = 1.0f;
    controls.lfo_rate = 0.0f;
    controls.lfo_intensity = 1.0f;
    assert(ts_sister_fallout_init(&engine, 1000u));
    ts_sister_fallout_set_controls(&engine, &controls);
    for (int frame = 0; frame < 20; ++frame)
        (void)ts_sister_fallout_process(&engine, silence);

    /* A destination inserted halfway through an hour-long shared RISE fades
       from its panel value to the current halfway value.  The clock is not
       restarted and reaches full catch-up after the 20 ms target blend. */
    engine.rise_phase = 0.5;
    engine.rise_smoothed = 0.5f;
    rise_phase = engine.rise_phase;
    controls.rise_targets = TS_SISTER_FALLOUT_LFO_FEEDBACK;
    ts_sister_fallout_set_controls(&engine, &controls);
    assert(engine.rise_phase == rise_phase);
    (void)ts_sister_fallout_process(&engine, silence);
    value = ts_sister_fallout_feedback_amount(&engine);
    assert(value > 0.215f && value < 0.225f);
    assert(engine.rise_target_blend[1] > 0.049f &&
           engine.rise_target_blend[1] < 0.051f);
    for (int frame = 1; frame < 20; ++frame)
        (void)ts_sister_fallout_process(&engine, silence);
    assert(engine.rise_phase > rise_phase);
    assert(engine.rise_phase < rise_phase + 0.00001);
    assert(ts_sister_fallout_feedback_amount(&engine) > 0.599f);

    /* Removal performs the inverse fade back to the saved panel center. */
    controls.rise_targets = 0u;
    ts_sister_fallout_set_controls(&engine, &controls);
    value = ts_sister_fallout_feedback_amount(&engine);
    (void)ts_sister_fallout_process(&engine, silence);
    assert(ts_sister_fallout_feedback_amount(&engine) < value);
    assert(ts_sister_fallout_feedback_amount(&engine) > 0.55f);
    for (int frame = 1; frame < 20; ++frame)
        (void)ts_sister_fallout_process(&engine, silence);
    assert(fabsf(ts_sister_fallout_feedback_amount(&engine) - 0.20f) <
           0.001f);

    /* LFO destinations use the same catch-up rule and preserve sine phase. */
    engine.lfo_phase = 0.25;
    controls.feedback = 0.50f;
    for (int frame = 0; frame < 20; ++frame) {
        ts_sister_fallout_set_controls(&engine, &controls);
        (void)ts_sister_fallout_process(&engine, silence);
    }
    lfo_phase = engine.lfo_phase;
    controls.lfo_targets = TS_SISTER_FALLOUT_LFO_FEEDBACK;
    ts_sister_fallout_set_controls(&engine, &controls);
    assert(engine.lfo_phase == lfo_phase);
    (void)ts_sister_fallout_process(&engine, silence);
    assert(ts_sister_fallout_feedback_amount(&engine) > 0.52f &&
           ts_sister_fallout_feedback_amount(&engine) < 0.53f);
    for (int frame = 1; frame < 20; ++frame)
        (void)ts_sister_fallout_process(&engine, silence);
    assert(ts_sister_fallout_feedback_amount(&engine) > 0.99f);
    ts_sister_fallout_free(&engine);
}

static void test_wheel_bursts_and_active_modulation_are_smoothed(void)
{
    TsSisterFalloutEngine engine;
    TsSisterFalloutControls controls;
    TsStereoFrame input = {0.25f, -0.20f};
    float previous_mix;
    float previous_feedback;
    float previous_bit_resolution;
    float previous_modulated;
    ts_sister_fallout_controls_default(&controls);
    controls.enabled = 1;
    controls.mix = 0.25f;
    controls.feedback = 0.25f;
    controls.noise = 0.0f;
    controls.bit_enabled = 1;
    controls.bit_resolution = 0.25f;
    controls.lfo_rate = 0.0f;
    controls.lfo_intensity = 0.5f;
    controls.lfo_targets = TS_SISTER_FALLOUT_LFO_FEEDBACK;
    controls.rise_mode = TS_SISTER_FALLOUT_RISE_SAW;
    controls.rise_length = ts_sister_fallout_rise_normalized(3600.0f);
    controls.rise_intensity = 0.5f;
    controls.rise_targets = TS_SISTER_FALLOUT_LFO_FEEDBACK;
    assert(ts_sister_fallout_init(&engine, 1000u));
    ts_sister_fallout_set_controls(&engine, &controls);
    for (int frame = 0; frame < 30; ++frame)
        (void)ts_sister_fallout_process(&engine, input);
    engine.lfo_phase = 0.25;
    engine.rise_phase = 0.5;
    engine.rise_smoothed = 0.5f;
    (void)ts_sister_fallout_process(&engine, input);
    previous_mix = engine.smoothed_controls.mix;
    previous_feedback = engine.smoothed_controls.feedback;
    previous_bit_resolution = engine.smoothed_controls.bit_resolution;
    previous_modulated = ts_sister_fallout_feedback_amount(&engine);

    /* Simulate a wheel event every audio frame, faster than any smoothing
       window.  Each retarget must continue from the actually rendered state. */
    for (int frame = 0; frame < 120; ++frame) {
        TsSisterFalloutResult out;
        float destination = (frame & 1) != 0 ? 1.0f : 0.0f;
        controls.mix = destination;
        controls.feedback = destination;
        controls.noise = destination;
        controls.bit_resolution = destination;
        controls.lfo_intensity = destination;
        controls.rise_intensity = 1.0f - destination;
        ts_sister_fallout_set_controls(&engine, &controls);
        out = ts_sister_fallout_process(&engine, input);
        assert(isfinite(out.output.l) && isfinite(out.output.r));
        assert(fabsf(engine.smoothed_controls.mix - previous_mix) <= 0.0501f);
        assert(fabsf(engine.smoothed_controls.feedback - previous_feedback) <=
               0.0501f);
        assert(fabsf(engine.smoothed_controls.bit_resolution -
                     previous_bit_resolution) <= 0.0501f);
        assert(fabsf(ts_sister_fallout_feedback_amount(&engine) -
                     previous_modulated) < 0.16f);
        previous_mix = engine.smoothed_controls.mix;
        previous_feedback = engine.smoothed_controls.feedback;
        previous_bit_resolution = engine.smoothed_controls.bit_resolution;
        previous_modulated = ts_sister_fallout_feedback_amount(&engine);
    }
    ts_sister_fallout_free(&engine);
}

static void test_every_modulation_destination_uses_target_blend(void)
{
    TsSisterFalloutEngine engine;
    TsSisterFalloutControls controls;
    TsStereoFrame silence = {0.0f, 0.0f};
    double lfo_phase;
    double rise_phase;
    ts_sister_fallout_controls_default(&controls);
    controls.enabled = 1;
    controls.rise_mode = TS_SISTER_FALLOUT_RISE_SAW;
    controls.rise_length = ts_sister_fallout_rise_normalized(3600.0f);
    assert(ts_sister_fallout_init(&engine, 1000u));
    ts_sister_fallout_set_controls(&engine, &controls);
    for (int frame = 0; frame < 20; ++frame)
        (void)ts_sister_fallout_process(&engine, silence);
    engine.lfo_phase = 0.375;
    engine.rise_phase = 0.5;
    engine.rise_smoothed = 0.5f;
    lfo_phase = engine.lfo_phase;
    rise_phase = engine.rise_phase;

    controls.lfo_targets = TS_SISTER_FALLOUT_LFO_ALL;
    controls.rise_targets = TS_SISTER_FALLOUT_LFO_ALL;
    ts_sister_fallout_set_controls(&engine, &controls);
    assert(engine.lfo_phase == lfo_phase);
    assert(engine.rise_phase == rise_phase);
    (void)ts_sister_fallout_process(&engine, silence);
    for (uint32_t slot = 0u; slot < TS_SISTER_FALLOUT_TARGET_COUNT; ++slot) {
        assert(engine.lfo_target_blend[slot] > 0.049f &&
               engine.lfo_target_blend[slot] < 0.051f);
        assert(engine.rise_target_blend[slot] > 0.049f &&
               engine.rise_target_blend[slot] < 0.051f);
    }
    for (int frame = 1; frame < 20; ++frame)
        (void)ts_sister_fallout_process(&engine, silence);
    for (uint32_t slot = 0u; slot < TS_SISTER_FALLOUT_TARGET_COUNT; ++slot) {
        assert(engine.lfo_target_blend[slot] == 1.0f);
        assert(engine.rise_target_blend[slot] == 1.0f);
    }

    controls.lfo_targets = 0u;
    controls.rise_targets = 0u;
    ts_sister_fallout_set_controls(&engine, &controls);
    (void)ts_sister_fallout_process(&engine, silence);
    for (uint32_t slot = 0u; slot < TS_SISTER_FALLOUT_TARGET_COUNT; ++slot) {
        assert(engine.lfo_target_blend[slot] > 0.949f &&
               engine.lfo_target_blend[slot] < 0.951f);
        assert(engine.rise_target_blend[slot] > 0.949f &&
               engine.rise_target_blend[slot] < 0.951f);
    }
    ts_sister_fallout_free(&engine);
}

static void test_toggle_edges_restart_from_audible_output(void)
{
    TsSisterFalloutEngine engine;
    TsSisterFalloutControls controls;
    TsSisterFalloutResult previous;
    TsStereoFrame input = {0.31f, -0.27f};
    ts_sister_fallout_controls_default(&controls);
    controls.enabled = 1;
    controls.mix = 1.0f;
    assert(ts_sister_fallout_init(&engine, 1000u));
    ts_sister_fallout_set_controls(&engine, &controls);
    previous = ts_sister_fallout_process(&engine, input);
    for (int frame = 1; frame < 80; ++frame)
        previous = ts_sister_fallout_process(&engine, input);

    for (int edge = 0; edge < 24; ++edge) {
        TsSisterFalloutResult current;
        controls.drop_enabled = (edge & 1) != 0;
        controls.pan_enabled = (edge & 2) != 0;
        controls.skip_enabled = (edge & 4) != 0;
        controls.bit_enabled = (edge & 8) != 0;
        controls.pitch_enabled = (edge & 16) != 0;
        controls.noise_type = (TsSisterFalloutNoiseType)(
            edge % TS_SISTER_FALLOUT_NOISE_COUNT);
        ts_sister_fallout_set_controls(&engine, &controls);
        current = ts_sister_fallout_process(&engine, input);
        assert(fabsf(current.output.l - previous.output.l) < 0.000001f);
        assert(fabsf(current.output.r - previous.output.r) < 0.000001f);
        assert(fabsf(current.wet.l - previous.wet.l) < 0.000001f);
        assert(fabsf(current.wet.r - previous.wet.r) < 0.000001f);
        previous = current;
        for (int frame = 0; frame < 3; ++frame)
            previous = ts_sister_fallout_process(&engine, input);
    }
    ts_sister_fallout_free(&engine);
}

static void test_component_transition_progress_and_zero_mix_transparency(void)
{
    TsSisterFalloutEngine engine;
    TsSisterFalloutControls controls;
    TsSisterFalloutTransitionStatus status;
    TsStereoFrame input = {0.31f, -0.27f};
    int active = 0;
    float progress;
    ts_sister_fallout_controls_default(&controls);
    controls.enabled = 1;
    controls.mix = 0.0f;
    controls.feedback = 1.0f;
    controls.component_transition =
        ts_sister_fallout_transition_normalized(1000.0f);
    assert(ts_sister_fallout_init(&engine, 1000u));
    ts_sister_fallout_set_controls(&engine, &controls);
    for (int frame = 0; frame < 1000; ++frame) {
        TsSisterFalloutResult output =
            ts_sister_fallout_process(&engine, input);
        assert(output.output.l == input.l && output.output.r == input.r);
    }
    assert(ts_sister_fallout_feedback_amount(&engine) == 0.0f);

    controls.drop_enabled = 1;
    ts_sister_fallout_set_controls(&engine, &controls);
    status = ts_sister_fallout_component_transition_status(&engine);
    assert(status.active && status.progress == 0.0f &&
           status.source == TS_SISTER_FALLOUT_TRANSITION_DROP &&
           status.target_enabled);
    progress = ts_sister_fallout_component_transition_progress(
        &engine, &active);
    assert(active && progress == 0.0f);
    for (int frame = 0; frame < 500; ++frame)
        (void)ts_sister_fallout_process(&engine, input);
    progress = ts_sister_fallout_component_transition_progress(
        &engine, &active);
    assert(active && progress > 0.499f && progress < 0.501f);
    status = ts_sister_fallout_component_transition_status(&engine);
    assert(status.source == TS_SISTER_FALLOUT_TRANSITION_DROP &&
           status.target_enabled);

    /* A newer component takes over the shared display without restarting
       the older ramp. */
    controls.pitch_enabled = 1;
    ts_sister_fallout_set_controls(&engine, &controls);
    status = ts_sister_fallout_component_transition_status(&engine);
    assert(status.active && status.progress == 0.0f &&
           status.source == TS_SISTER_FALLOUT_TRANSITION_PITCH &&
           status.target_enabled && engine.drop_engage.remaining == 500u);

    /* A wheel edit updates its target immediately without restarting the
       minute-scale activation envelope. */
    controls.drop_rate = 0.91f;
    ts_sister_fallout_set_controls(&engine, &controls);
    assert(engine.controls.drop_rate == 0.91f);
    assert(engine.drop_engage.remaining == 500u);
    for (int frame = 0; frame < 500; ++frame)
        (void)ts_sister_fallout_process(&engine, input);
    status = ts_sister_fallout_component_transition_status(&engine);
    assert(status.active && status.progress > 0.499f &&
           status.progress < 0.501f &&
           status.source == TS_SISTER_FALLOUT_TRANSITION_PITCH &&
           status.target_enabled && engine.drop_engage.remaining == 0u);
    for (int frame = 0; frame < 500; ++frame)
        (void)ts_sister_fallout_process(&engine, input);
    progress = ts_sister_fallout_component_transition_progress(
        &engine, &active);
    assert(!active && progress == 1.0f);
    status = ts_sister_fallout_component_transition_status(&engine);
    assert(!status.active &&
           status.source == TS_SISTER_FALLOUT_TRANSITION_NONE);
    ts_sister_fallout_free(&engine);
}

static void test_independent_master_component_retime_and_restore(void)
{
    TsSisterFalloutEngine engine;
    TsSisterFalloutControls controls;
    TsSisterFalloutTransitionStatus status;
    TsStereoFrame input = {0.24f, -0.18f};
    ts_sister_fallout_controls_default(&controls);
    controls.enabled = 1;
    controls.mix = 1.0f;
    controls.master_transition =
        ts_sister_fallout_transition_normalized(1000.0f);
    controls.component_transition =
        ts_sister_fallout_transition_normalized(1000.0f);
    assert(ts_sister_fallout_init(&engine, 1000u));
    ts_sister_fallout_set_controls(&engine, &controls);
    for (int frame = 0; frame < 250; ++frame)
        (void)ts_sister_fallout_process(&engine, input);
    assert(engine.engage_remaining == 750u && engine.engage_total == 1000u);

    /* The parts clock cannot perturb a running master entrance. */
    controls.component_transition =
        ts_sister_fallout_transition_normalized(100.0f);
    ts_sister_fallout_set_controls(&engine, &controls);
    assert(engine.engage_remaining == 750u && engine.engage_total == 1000u);

    /* The master's own wheel edit preserves 25% progress and accelerates. */
    controls.master_transition =
        ts_sister_fallout_transition_normalized(100.0f);
    ts_sister_fallout_set_controls(&engine, &controls);
    assert(engine.engage_remaining == 75u && engine.engage_total == 100u);
    status = ts_sister_fallout_master_transition_status(&engine);
    assert(status.active && status.progress > 0.249f &&
           status.progress < 0.251f && status.target_enabled);
    for (int frame = 0; frame < 75; ++frame)
        (void)ts_sister_fallout_process(&engine, input);
    assert(ts_sister_fallout_engage(&engine) == 1.0f);

    controls.pitch_enabled = 1;
    ts_sister_fallout_set_controls(&engine, &controls);
    assert(engine.pitch_engage.remaining == 100u);
    for (int frame = 0; frame < 25; ++frame)
        (void)ts_sister_fallout_process(&engine, input);
    controls.master_transition =
        ts_sister_fallout_transition_normalized(1000.0f);
    ts_sister_fallout_set_controls(&engine, &controls);
    assert(engine.pitch_engage.remaining == 75u &&
           engine.pitch_engage.total == 100u);
    controls.component_transition =
        ts_sister_fallout_transition_normalized(40.0f);
    ts_sister_fallout_set_controls(&engine, &controls);
    assert(engine.pitch_engage.remaining == 30u &&
           engine.pitch_engage.total == 40u);

    controls.enabled = 0;
    controls.master_transition =
        ts_sister_fallout_transition_normalized(3600000.0f);
    ts_sister_fallout_sync_controls(&engine, &controls);
    assert(!engine.active && engine.engage == 0.0f &&
           engine.engage_remaining == 0u);
    ts_sister_fallout_free(&engine);
}

int main(void)
{
    test_defaults_are_true_bypass();
    test_deterministic_effect_and_cold_reenable();
    test_sanitize_and_memory();
    test_transition_noise_and_centered_lfo();
    test_master_gates_and_modulation_disconnect();
    test_rise_reset_declicks_both_modes();
    test_preset_transition_uses_current_transition();
    test_running_preset_transition_retimes_in_place();
    test_running_modulation_targets_fade_to_shared_phase();
    test_wheel_bursts_and_active_modulation_are_smoothed();
    test_every_modulation_destination_uses_target_blend();
    test_toggle_edges_restart_from_audible_output();
    test_component_transition_progress_and_zero_mix_transparency();
    test_independent_master_component_retime_and_restore();
    test_runtime_feedback_is_wet_only_and_causal();
    puts("Sister Fallout tests passed");
    return 0;
}
