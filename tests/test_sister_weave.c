#include "tapesister/sister_effects.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define TEST_TWO_PI 6.283185307179586476925286766559

static int closef_test(float a, float b, float tolerance)
{
    return fabsf(a - b) <= tolerance;
}

static void target_policy(void)
{
    uint8_t mask = TS_SISTER_EFFECT_TARGET_MIX;
    assert(ts_sister_effect_targets_sanitize(0xffu) ==
           TS_SISTER_EFFECT_TARGET_MIX);
    mask = ts_sister_effect_targets_toggle(mask,
                                           TS_SISTER_EFFECT_TARGET_H1);
    assert(mask == TS_SISTER_EFFECT_TARGET_H1);
    mask = ts_sister_effect_targets_toggle(mask,
                                           TS_SISTER_EFFECT_TARGET_H3);
    assert(mask == (TS_SISTER_EFFECT_TARGET_H1 |
                    TS_SISTER_EFFECT_TARGET_H3));
    mask = ts_sister_effect_targets_toggle(mask,
                                           TS_SISTER_EFFECT_TARGET_H1);
    assert(mask == TS_SISTER_EFFECT_TARGET_H3);
    mask = ts_sister_effect_targets_toggle(mask,
                                           TS_SISTER_EFFECT_TARGET_MIX);
    assert(mask == TS_SISTER_EFFECT_TARGET_MIX);
    mask = ts_sister_effect_targets_toggle(mask,
                                           TS_SISTER_EFFECT_TARGET_MIX);
    assert(mask == 0u);
    assert(ts_sister_effect_targets_toggle(mask, 0x80u) == 0u);
    assert(ts_sister_weave_rate_hz(0.0f) == TS_SISTER_WEAVE_RATE_MIN_HZ);
    assert(closef_test(ts_sister_weave_rate_hz(1.0f),
                       TS_SISTER_WEAVE_RATE_MAX_HZ, 1.0e-6f));
    assert(ts_sister_weave_rate_hz(0.5f) > TS_SISTER_WEAVE_RATE_MIN_HZ);
}

static void exact_zero_and_mono(uint32_t sample_rate)
{
    TsSisterWeaveState state;
    assert(ts_sister_weave_init(&state, sample_rate, 0.0));
    ts_sister_weave_set(&state, 0.0f, 1.0f, 1);
    ts_sister_weave_reset(&state);
    for (int i = 0; i < 4096; ++i) {
        TsStereoFrame input = {(float)(i % 23) * 0.03125f - 0.3f,
                               (float)(i % 17) * -0.027f + 0.2f};
        TsStereoFrame output = ts_sister_weave_process(&state, input, 0);
        assert(output.l == input.l && output.r == input.r);
    }
    ts_sister_weave_set(&state, 1.0f, 1.0f, 1);
    ts_sister_weave_reset(&state);
    for (int i = 0; i < 4096; ++i) {
        TsStereoFrame input = {(float)(i % 31) / 31.0f,
                               (float)(i % 31) / 31.0f};
        TsStereoFrame output = ts_sister_weave_process(&state, input, 1);
        assert(output.l == input.l && output.r == input.r);
    }
    assert(state.write_index == 0u);
    ts_sister_weave_free(&state);
}

static int first_cross_frame(uint32_t sample_rate)
{
    TsSisterWeaveState state;
    int first = -1;
    assert(ts_sister_weave_init(&state, sample_rate, 0.0));
    ts_sister_weave_set(&state, 1.0f, 0.0f, 1);
    ts_sister_weave_reset(&state);
    for (int i = 0; i < (int)(sample_rate / 10u); ++i) {
        TsStereoFrame input = i == 0 ? (TsStereoFrame){1.0f, 0.0f} :
                                      (TsStereoFrame){0.0f, 0.0f};
        TsStereoFrame output = ts_sister_weave_process(&state, input, 0);
        if (first < 0 && fabsf(output.r) > 1.0e-7f) first = i;
    }
    ts_sister_weave_free(&state);
    return first;
}

static void channel_migration_and_rate_independence(void)
{
    TsSisterWeaveState state;
    float right_peak = 0.0f;
    float left_peak = 0.0f;
    int f441 = first_cross_frame(44100u);
    int f480 = first_cross_frame(48000u);
    int f960 = first_cross_frame(96000u);
    assert(f441 > 0 && f480 > 0 && f960 > 0);
    assert(fabs((double)f441 / 44100.0 - (double)f480 / 48000.0) < 0.0001);
    assert(fabs((double)f480 / 48000.0 - (double)f960 / 96000.0) < 0.0001);

    assert(ts_sister_weave_init(&state, 1000u, 0.0));
    ts_sister_weave_set(&state, 1.0f, 0.0f, 1);
    ts_sister_weave_reset(&state);
    for (int i = 0; i < 64; ++i) {
        TsStereoFrame input = i == 0 ? (TsStereoFrame){1.0f, 0.0f} :
                                      (TsStereoFrame){0.0f, 0.0f};
        TsStereoFrame output = ts_sister_weave_process(&state, input, 0);
        if (fabsf(output.r) > right_peak) right_peak = fabsf(output.r);
    }
    assert(right_peak > 0.25f);
    ts_sister_weave_reset(&state);
    for (int i = 0; i < 64; ++i) {
        TsStereoFrame input = i == 0 ? (TsStereoFrame){0.0f, 1.0f} :
                                      (TsStereoFrame){0.0f, 0.0f};
        TsStereoFrame output = ts_sister_weave_process(&state, input, 0);
        if (fabsf(output.l) > left_peak) left_peak = fabsf(output.l);
    }
    assert(left_peak > 0.25f);
    ts_sister_weave_free(&state);
}

static void fractional_interpolation(void)
{
    TsSisterWeaveState state;
    TsStereoFrame output;
    double delay_frames;
    double position;
    size_t first;
    float fraction;
    float delayed;
    float amount;
    assert(ts_sister_weave_init(&state, 1000u, 0.0));
    ts_sister_weave_set(&state, 1.0f, 0.0f, 1);
    ts_sister_weave_reset(&state);
    state.write_index = state.delay_frames - 2u;
    for (size_t i = 0u; i < state.delay_frames; ++i)
        state.delay_l[i] = (float)i;
    output = ts_sister_weave_process(&state, (TsStereoFrame){0.0f, 0.0f}, 0);
    delay_frames = TS_SISTER_WEAVE_DELAY_MIN_MS +
        (TS_SISTER_WEAVE_DELAY_MAX_MS - TS_SISTER_WEAVE_DELAY_MIN_MS) *
        (0.5 + 0.5 * sin(state.phase + TEST_TWO_PI / 3.0));
    position = (double)(state.delay_frames - 2u) - delay_frames;
    while (position < 0.0) position += (double)state.delay_frames;
    first = (size_t)floor(position);
    fraction = (float)(position - (double)first);
    delayed = state.delay_l[first] +
              (state.delay_l[(first + 1u) % state.delay_frames] -
               state.delay_l[first]) * fraction;
    amount = 0.78f + 0.22f * (float)sin(
        state.phase + TEST_TWO_PI * 0.25 + TEST_TWO_PI / 6.0);
    assert(fraction > 0.01f && fraction < 0.99f);
    assert(closef_test(output.r, delayed * amount, 1.0e-4f));
    ts_sister_weave_free(&state);
}

static void near_swap_stagger_and_continuity(void)
{
    TsSisterWeaveState states[3];
    TsStereoFrame previous = {0.0f, 0.0f};
    TsStereoFrame last[3];
    float best_right = 0.0f;
    float paired_left = 1.0f;
    double phase_before;
    for (int i = 0; i < 3; ++i) {
        assert(ts_sister_weave_init(&states[i], 1000u, (double)i / 3.0));
        ts_sister_weave_set(&states[i], 1.0f, 1.0f, 1);
        ts_sister_weave_reset(&states[i]);
    }
    for (int frame = 0; frame < 5000; ++frame) {
        for (int i = 0; i < 3; ++i)
            last[i] = ts_sister_weave_process(
                &states[i], (TsStereoFrame){1.0f, 0.0f}, 0);
        if (last[0].r > best_right) {
            best_right = last[0].r;
            paired_left = last[0].l;
        }
    }
    assert(best_right > 0.94f && paired_left < 0.30f);
    assert(fabsf(last[0].l - last[0].r) > 0.05f);
    assert(fabsf(last[0].r - last[1].r) > 0.01f ||
           fabsf(last[1].r - last[2].r) > 0.01f);

    phase_before = states[0].phase;
    ts_sister_weave_set(&states[0], 1.0f, 0.0f, 1);
    assert(states[0].phase == phase_before);
    for (int frame = 0; frame < 64; ++frame)
        previous = ts_sister_weave_process(
            &states[0], (TsStereoFrame){0.6f, -0.4f}, 0);
    for (int frame = 0; frame < 2000; ++frame) {
        TsStereoFrame output;
        ts_sister_weave_set(&states[0], (frame & 1) ? 1.0f : 0.0f,
                            (frame & 2) ? 1.0f : 0.0f, 1);
        output = ts_sister_weave_process(
            &states[0], (TsStereoFrame){0.6f, -0.4f}, 0);
        assert(isfinite(output.l) && isfinite(output.r));
        assert(fabsf(output.l - previous.l) < 0.10f);
        assert(fabsf(output.r - previous.r) < 0.10f);
        previous = output;
    }
    ts_sister_weave_set(&states[0], 1.0f, 1.0f, 0);
    for (int frame = 0; frame < 1000; ++frame)
        previous = ts_sister_weave_process(
            &states[0], (TsStereoFrame){0.6f, -0.4f}, 0);
    assert(previous.l == 0.6f && previous.r == -0.4f);
    for (int i = 0; i < 3; ++i) ts_sister_weave_free(&states[i]);
}

static void deterministic_and_recovery(void)
{
    TsSisterWeaveState a;
    TsSisterWeaveState b;
    assert(ts_sister_weave_init(&a, 48000u, 1.0 / 3.0));
    assert(ts_sister_weave_init(&b, 48000u, 1.0 / 3.0));
    ts_sister_weave_set(&a, 0.83f, 0.67f, 1);
    ts_sister_weave_set(&b, 0.83f, 0.67f, 1);
    ts_sister_weave_reset(&a);
    ts_sister_weave_reset(&b);
    for (int i = 0; i < 100000; ++i) {
        TsStereoFrame input = {(float)(i % 29) / 31.0f,
                               -(float)(i % 37) / 41.0f};
        TsStereoFrame oa = ts_sister_weave_process(&a, input, 0);
        TsStereoFrame ob = ts_sister_weave_process(&b, input, 0);
        assert(memcmp(&oa, &ob, sizeof(oa)) == 0);
        assert(isfinite(oa.l) && isfinite(oa.r));
    }
    (void)ts_sister_weave_process(
        &a, (TsStereoFrame){NAN, INFINITY}, 0);
    ts_sister_weave_free(&a);
    assert(a.delay_l == NULL && a.delay_r == NULL && a.delay_frames == 0u);
    assert(ts_sister_weave_init(&a, 96000u, 0.0));
    for (size_t i = 0u; i < a.delay_frames; ++i)
        assert(a.delay_l[i] == 0.0f && a.delay_r[i] == 0.0f);
    ts_sister_weave_free(&a);
    ts_sister_weave_free(&b);
}

int main(void)
{
    target_policy();
    exact_zero_and_mono(44100u);
    exact_zero_and_mono(48000u);
    exact_zero_and_mono(96000u);
    channel_migration_and_rate_independence();
    fractional_interpolation();
    near_swap_stagger_and_continuity();
    deterministic_and_recovery();
    puts("sister weave tests passed");
    return 0;
}
