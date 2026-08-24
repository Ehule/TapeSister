#include "tapesister/sister_effects.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define TS_WEAVE_TWO_PI 6.283185307179586476925286766559
#define TS_WEAVE_DIRECTION_OFFSET (TS_WEAVE_TWO_PI / 3.0)
#define TS_WEAVE_AMOUNT_OFFSET (TS_WEAVE_TWO_PI / 6.0)

static float clamp_unit(float value)
{
    if (!isfinite(value)) return 0.0f;
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static float flush_tiny(float value)
{
    if (!isfinite(value)) return 0.0f;
    return fabsf(value) < 1.0e-20f ? 0.0f : value;
}

static float approach(float current, float target, float seconds,
                      uint32_t sample_rate)
{
    float coefficient;
    if (!isfinite(target)) target = 0.0f;
    if (!isfinite(current)) current = target;
    if (sample_rate == 0u || seconds <= 0.0f) return target;
    coefficient = 1.0f - expf(-1.0f / (seconds * (float)sample_rate));
    current += (target - current) * coefficient;
    if (fabsf(target - current) < 0.000001f) current = target;
    if (fabsf(current) < 1.0e-20f) current = 0.0f;
    return current;
}

uint8_t ts_sister_effect_targets_sanitize(uint8_t mask)
{
    mask &= TS_SISTER_EFFECT_TARGET_ALL;
    if ((mask & TS_SISTER_EFFECT_TARGET_MIX) != 0u)
        return TS_SISTER_EFFECT_TARGET_MIX;
    return mask & TS_SISTER_EFFECT_TARGET_HEADS;
}

uint8_t ts_sister_effect_targets_toggle(uint8_t mask, uint8_t target)
{
    mask = ts_sister_effect_targets_sanitize(mask);
    if (target == TS_SISTER_EFFECT_TARGET_MIX)
        return (mask & target) != 0u ? 0u : target;
    if (target != TS_SISTER_EFFECT_TARGET_H1 &&
        target != TS_SISTER_EFFECT_TARGET_H2 &&
        target != TS_SISTER_EFFECT_TARGET_H3)
        return mask;
    mask &= TS_SISTER_EFFECT_TARGET_HEADS;
    return (uint8_t)(mask ^ target);
}

int ts_sister_effect_target_enabled(uint8_t mask, uint8_t target)
{
    return (ts_sister_effect_targets_sanitize(mask) & target) != 0u;
}

float ts_sister_weave_rate_hz(float bleed)
{
    bleed = clamp_unit(bleed);
    return TS_SISTER_WEAVE_RATE_MIN_HZ *
           powf(TS_SISTER_WEAVE_RATE_MAX_HZ /
                TS_SISTER_WEAVE_RATE_MIN_HZ, bleed);
}

int ts_sister_weave_init(TsSisterWeaveState *state, uint32_t sample_rate,
                         double phase_offset_cycles)
{
    size_t delay_frames;
    float *left;
    float *right;
    if (state == NULL || sample_rate == 0u ||
        !isfinite(phase_offset_cycles)) return 0;
    delay_frames = (size_t)ceil((double)sample_rate *
                               TS_SISTER_WEAVE_DELAY_MAX_MS / 1000.0) + 3u;
    if (delay_frames < 4u) delay_frames = 4u;
    left = calloc(delay_frames, sizeof(*left));
    right = calloc(delay_frames, sizeof(*right));
    if (left == NULL || right == NULL) {
        free(left);
        free(right);
        return 0;
    }
    memset(state, 0, sizeof(*state));
    state->delay_l = left;
    state->delay_r = right;
    state->delay_frames = delay_frames;
    state->sample_rate = sample_rate;
    state->phase_offset = fmod(phase_offset_cycles, 1.0) * TS_WEAVE_TWO_PI;
    if (state->phase_offset < 0.0) state->phase_offset += TS_WEAVE_TWO_PI;
    state->phase = state->phase_offset;
    state->rate_current_hz = TS_SISTER_WEAVE_RATE_MIN_HZ;
    state->rate_target_hz = TS_SISTER_WEAVE_RATE_MIN_HZ;
    return 1;
}

void ts_sister_weave_free(TsSisterWeaveState *state)
{
    if (state == NULL) return;
    free(state->delay_l);
    free(state->delay_r);
    memset(state, 0, sizeof(*state));
}

void ts_sister_weave_reset(TsSisterWeaveState *state)
{
    if (state == NULL) return;
    if (state->delay_l != NULL)
        memset(state->delay_l, 0,
               state->delay_frames * sizeof(*state->delay_l));
    if (state->delay_r != NULL)
        memset(state->delay_r, 0,
               state->delay_frames * sizeof(*state->delay_r));
    state->write_index = 0u;
    state->phase = state->phase_offset;
    state->soak_current = state->soak_target;
    state->rate_current_hz = state->rate_target_hz;
    state->route_current = state->route_target;
}

void ts_sister_weave_set(TsSisterWeaveState *state, float soak, float bleed,
                         int routed)
{
    if (state == NULL) return;
    state->soak_target = clamp_unit(soak);
    state->rate_target_hz = ts_sister_weave_rate_hz(bleed);
    state->route_target = routed != 0 ? 1.0f : 0.0f;
}

static float fractional_read(const float *delay, size_t frames,
                             size_t write_index, double delay_frames)
{
    double position;
    size_t first;
    size_t second;
    float fraction;
    if (delay == NULL || frames < 2u || !isfinite(delay_frames)) return 0.0f;
    if (delay_frames < 1.0) delay_frames = 1.0;
    if (delay_frames > (double)(frames - 2u))
        delay_frames = (double)(frames - 2u);
    position = (double)write_index - delay_frames;
    while (position < 0.0) position += (double)frames;
    while (position >= (double)frames) position -= (double)frames;
    first = (size_t)floor(position);
    second = (first + 1u) % frames;
    fraction = (float)(position - (double)first);
    return delay[first] + (delay[second] - delay[first]) * fraction;
}

TsStereoFrame ts_sister_weave_process(TsSisterWeaveState *state,
                                      TsStereoFrame input,
                                      int explicit_mono)
{
    TsStereoFrame woven;
    TsStereoFrame dry;
    float soak;
    float route;
    float rate;
    float amount_rl;
    float amount_lr;
    double delay_min;
    double delay_span;
    double delay_rl;
    double delay_lr;
    float delayed_r;
    float delayed_l;
    input = ts_stereo_frame_sanitize(input);
    dry = input;
    if (state == NULL || state->delay_l == NULL || state->delay_r == NULL ||
        state->delay_frames < 4u || state->sample_rate == 0u)
        return input;

    soak = approach(state->soak_current, state->soak_target, 0.020f,
                    state->sample_rate);
    route = approach(state->route_current, state->route_target, 0.010f,
                     state->sample_rate);
    rate = approach(state->rate_current_hz, state->rate_target_hz, 0.050f,
                    state->sample_rate);
    state->soak_current = clamp_unit(soak);
    state->route_current = clamp_unit(route);
    state->rate_current_hz = isfinite(rate) && rate > 0.0f ? rate :
                             TS_SISTER_WEAVE_RATE_MIN_HZ;

    /* The master oscillator is hardware-like: it advances under ROLL, HOLD,
       MONITOR off, zero SOAK and zero targets. Explicit mono skips all delay
       history while remaining exact dual mono. */
    state->phase += TS_WEAVE_TWO_PI * (double)state->rate_current_hz /
                    (double)state->sample_rate;
    if (!isfinite(state->phase)) state->phase = state->phase_offset;
    while (state->phase >= TS_WEAVE_TWO_PI) state->phase -= TS_WEAVE_TWO_PI;
    while (state->phase < 0.0) state->phase += TS_WEAVE_TWO_PI;
    if (explicit_mono) return dry;
    if (state->soak_current == 0.0f && state->soak_target == 0.0f)
        return dry;
    input.l = flush_tiny(input.l);
    input.r = flush_tiny(input.r);

    state->delay_l[state->write_index] = input.l;
    state->delay_r[state->write_index] = input.r;
    if (state->route_current == 0.0f && state->route_target == 0.0f) {
        state->write_index = (state->write_index + 1u) % state->delay_frames;
        return dry;
    }
    delay_min = (double)state->sample_rate *
                TS_SISTER_WEAVE_DELAY_MIN_MS / 1000.0;
    delay_span = (double)state->sample_rate *
                 (TS_SISTER_WEAVE_DELAY_MAX_MS -
                  TS_SISTER_WEAVE_DELAY_MIN_MS) / 1000.0;
    delay_rl = delay_min + delay_span *
               (0.5 + 0.5 * sin(state->phase));
    delay_lr = delay_min + delay_span *
               (0.5 + 0.5 * sin(state->phase +
                                TS_WEAVE_DIRECTION_OFFSET));
    delayed_r = fractional_read(state->delay_r, state->delay_frames,
                                state->write_index, delay_rl);
    delayed_l = fractional_read(state->delay_l, state->delay_frames,
                                state->write_index, delay_lr);
    state->write_index = (state->write_index + 1u) % state->delay_frames;

    amount_rl = state->soak_current *
                (0.78f + 0.22f * (float)sin(state->phase +
                                                   TS_WEAVE_TWO_PI * 0.25));
    amount_lr = state->soak_current *
                (0.78f + 0.22f * (float)sin(state->phase +
                                                   TS_WEAVE_TWO_PI * 0.25 +
                                                   TS_WEAVE_AMOUNT_OFFSET));
    amount_rl = clamp_unit(amount_rl);
    amount_lr = clamp_unit(amount_lr);
    woven.l = input.l + (delayed_r - input.l) * amount_rl;
    woven.r = input.r + (delayed_l - input.r) * amount_lr;
    woven = ts_stereo_frame_sanitize(woven);
    if (state->route_current == 0.0f || state->soak_current == 0.0f)
        return dry;
    woven.l = dry.l + (woven.l - dry.l) * state->route_current;
    woven.r = dry.r + (woven.r - dry.r) * state->route_current;
    woven = ts_stereo_frame_sanitize(woven);
    woven.l = flush_tiny(woven.l);
    woven.r = flush_tiny(woven.r);
    return woven;
}
