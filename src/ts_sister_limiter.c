#include "tapesister/sister_limiter.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static float limiter_clamp(float value, float minimum, float maximum,
                           float fallback)
{
    if (!isfinite(value)) value = fallback;
    if (value < minimum) value = minimum;
    if (value > maximum) value = maximum;
    return value;
}

static float frame_peak(TsStereoFrame frame)
{
    float left;
    float right;
    frame = ts_stereo_frame_sanitize(frame);
    left = fabsf(frame.l);
    right = fabsf(frame.r);
    return left > right ? left : right;
}

static void limiter_reset_audio(TsSisterLimiter *limiter)
{
    if (limiter == NULL) return;
    if (limiter->delay != NULL && limiter->delay_frames > 0u)
        memset(limiter->delay, 0,
               limiter->delay_capacity * sizeof(*limiter->delay));
    limiter->delay_position = 0u;
    limiter->live_delay = limiter->delay_frames;
    limiter->delay_step = 0;limiter->delay_ramp = 0;
    limiter->processed_frames = 0u;
    limiter->hold_until_frame = 0u;
    limiter->gain = 1.0f;
    limiter->applied_gain = 1.0f;
    limiter->enabled_mix = limiter->enabled ? 1.0f : 0.0f;
    limiter->enabled_step = 0.0f;
    limiter->enabled_ramp_remaining = 0u;
}

static void limiter_update_coefficients(TsSisterLimiter *limiter)
{
    if (limiter == NULL) return;
    limiter->ceiling_linear = powf(10.0f, limiter->ceiling_db / 20.0f);
    limiter->release_coefficient = limiter->sample_rate > 0u ?
        1.0f - expf(-1.0f / (limiter->release_ms * 0.001f *
                            (float)limiter->sample_rate)) : 1.0f;
}

void ts_sister_limiter_init(TsSisterLimiter *limiter)
{
    if (limiter == NULL) return;
    memset(limiter, 0, sizeof(*limiter));
    limiter->enabled = TS_SISTER_LIMITER_DEFAULT_ENABLED;
    limiter->ceiling_db = TS_SISTER_LIMITER_DEFAULT_CEILING_DB;
    limiter->lookahead_ms = TS_SISTER_LIMITER_DEFAULT_LOOKAHEAD_MS;
    limiter->release_ms = TS_SISTER_LIMITER_DEFAULT_RELEASE_MS;
    limiter->gain = 1.0f;
    limiter->applied_gain = 1.0f;
    limiter->enabled_mix = 1.0f;
    limiter_update_coefficients(limiter);
}

void ts_sister_limiter_free(TsSisterLimiter *limiter)
{
    if (limiter == NULL) return;
    free(limiter->delay);
    limiter->delay = NULL;
    limiter->delay_frames = limiter->delay_capacity = 0u;
    limiter->delay_position = 0u;
    limiter->sample_rate = 0u;
    limiter->ready = 0;
}

int ts_sister_limiter_reconfigure(TsSisterLimiter *limiter,
                                  uint32_t sample_rate)
{
    TsStereoFrame *delay;
    size_t frames, capacity;
    if (limiter == NULL || sample_rate == 0u) return 0;
    frames = (size_t)lrintf(limiter->lookahead_ms * 0.001f *
                            (float)sample_rate);
    if (frames < 1u) frames = 1u;
    capacity = (size_t)ceilf(TS_SISTER_LIMITER_LOOKAHEAD_MS_MAX * .001f * sample_rate) + 1u;
    if (limiter->ready && limiter->sample_rate == sample_rate &&
        limiter->delay_capacity == capacity) {
        limiter->delay_frames = frames;
        limiter_update_coefficients(limiter);
        limiter_reset_audio(limiter);
        return 1;
    }
    delay = (TsStereoFrame *)calloc(capacity, sizeof(*delay));
    if (delay == NULL) return 0;
    free(limiter->delay);
    limiter->delay = delay;
    limiter->delay_frames = frames;limiter->delay_capacity = capacity;
    limiter->sample_rate = sample_rate;
    limiter->ready = 1;
    limiter_update_coefficients(limiter);
    limiter_reset_audio(limiter);
    return 1;
}

void ts_sister_limiter_set_controls(TsSisterLimiter *limiter, int enabled,
                                    float ceiling_db, float lookahead_ms,
                                    float release_ms)
{
    uint32_t sample_rate;
    float old_lookahead;
    if (limiter == NULL) return;
    old_lookahead = limiter->lookahead_ms;
    limiter->ceiling_db = limiter_clamp(
        ceiling_db, TS_SISTER_LIMITER_CEILING_DB_MIN,
        TS_SISTER_LIMITER_CEILING_DB_MAX,
        TS_SISTER_LIMITER_DEFAULT_CEILING_DB);
    limiter->lookahead_ms = limiter_clamp(
        lookahead_ms, TS_SISTER_LIMITER_LOOKAHEAD_MS_MIN,
        TS_SISTER_LIMITER_LOOKAHEAD_MS_MAX,
        TS_SISTER_LIMITER_DEFAULT_LOOKAHEAD_MS);
    limiter->release_ms = limiter_clamp(
        release_ms, TS_SISTER_LIMITER_RELEASE_MS_MIN,
        TS_SISTER_LIMITER_RELEASE_MS_MAX,
        TS_SISTER_LIMITER_DEFAULT_RELEASE_MS);
    limiter->enabled = enabled != 0;
    limiter_update_coefficients(limiter);
    sample_rate = limiter->sample_rate;
    if (limiter->ready && fabsf(old_lookahead - limiter->lookahead_ms) >
                              0.0001f)
        (void)ts_sister_limiter_reconfigure(limiter, sample_rate);
    else
        limiter_reset_audio(limiter);
}

void ts_sister_limiter_set_enabled(TsSisterLimiter *limiter, int enabled)
{
    uint32_t frames;
    float target;
    if (limiter == NULL) return;
    enabled = enabled != 0;
    if (limiter->enabled == enabled) return;
    limiter->enabled = enabled;
    target = enabled ? 1.0f : 0.0f;
    frames = limiter->sample_rate > 0u ?
        (uint32_t)ceilf(0.010f * (float)limiter->sample_rate) : 0u;
    if (frames == 0u) {
        limiter->enabled_mix = target;
        limiter->enabled_step = 0.0f;
        limiter->enabled_ramp_remaining = 0u;
    } else {
        limiter->enabled_step = (target - limiter->enabled_mix) /
                                (float)frames;
        limiter->enabled_ramp_remaining = frames;
    }
}

void ts_sister_limiter_set_live_controls(TsSisterLimiter *l, int enabled,
    float ceiling_db, float lookahead_ms, float release_ms)
{
    if (!l) return;
    l->ceiling_db=limiter_clamp(ceiling_db,TS_SISTER_LIMITER_CEILING_DB_MIN,
        TS_SISTER_LIMITER_CEILING_DB_MAX,TS_SISTER_LIMITER_DEFAULT_CEILING_DB);
    l->release_ms=limiter_clamp(release_ms,TS_SISTER_LIMITER_RELEASE_MS_MIN,
        TS_SISTER_LIMITER_RELEASE_MS_MAX,TS_SISTER_LIMITER_DEFAULT_RELEASE_MS);
    l->lookahead_ms=limiter_clamp(lookahead_ms,TS_SISTER_LIMITER_LOOKAHEAD_MS_MIN,
        TS_SISTER_LIMITER_LOOKAHEAD_MS_MAX,TS_SISTER_LIMITER_DEFAULT_LOOKAHEAD_MS);
    size_t frames=(size_t)lrintf(l->lookahead_ms*.001f*l->sample_rate);
    if(frames<1)frames=1;
    if(l->ready && frames<l->delay_capacity && frames!=l->delay_frames) {
        l->delay_frames=frames;l->delay_ramp=(uint32_t)ceilf(.020f*l->sample_rate);
        if(!l->delay_ramp)l->delay_ramp=1;
        l->delay_step=((double)frames-l->live_delay)/l->delay_ramp;
    }
    limiter_update_coefficients(l);ts_sister_limiter_set_enabled(l,enabled);
}

float ts_sister_limiter_gain_reduction_db(const TsSisterLimiter *limiter)
{
    float gain;
    if (limiter == NULL) return 0.0f;
    gain = limiter->applied_gain;
    if (!isfinite(gain) || gain <= 0.000001f) return 120.0f;
    if (gain >= 1.0f) return 0.0f;
    return -20.0f * log10f(gain);
}

TsStereoFrame ts_sister_limiter_process(TsSisterLimiter *limiter,
                                        TsStereoFrame input,
                                        float *gain_reduction_db,
                                        float *input_peak)
{
    TsStereoFrame delayed;
    TsStereoFrame limited;
    TsStereoFrame output;
    float peak;
    float requested_gain = 1.0f;
    float applied_gain;
    input = ts_stereo_frame_sanitize(input);
    peak = frame_peak(input);
    if (input_peak != NULL) *input_peak = peak;
    if (limiter == NULL || !limiter->ready || limiter->delay == NULL ||
        limiter->delay_frames == 0u) {
        if (gain_reduction_db != NULL) *gain_reduction_db = 0.0f;
        return input;
    }

    /* Keep all lookahead history warm; the ordinary fixed tap stays cheap. */
    size_t hold=limiter->delay_frames;
    if(limiter->delay_ramp) {
        double read=(double)limiter->delay_position-limiter->live_delay;
        if(read<0)read+=limiter->delay_capacity;
        size_t first=(size_t)read,next=first+1u;
        if(next==limiter->delay_capacity)next=0;
        float fraction=(float)(read-first);
        delayed.l=limiter->delay[first].l+(limiter->delay[next].l-limiter->delay[first].l)*fraction;
        delayed.r=limiter->delay[first].r+(limiter->delay[next].r-limiter->delay[first].r)*fraction;
        if(limiter->live_delay>hold)hold=(size_t)ceil(limiter->live_delay);
        if(!--limiter->delay_ramp)limiter->live_delay=limiter->delay_frames;
        else limiter->live_delay+=limiter->delay_step;
    } else {
        size_t read=limiter->delay_position>=hold?limiter->delay_position-hold:
            limiter->delay_position+limiter->delay_capacity-hold;
        delayed=limiter->delay[read];
    }
    limiter->delay[limiter->delay_position] = input;
    if(++limiter->delay_position==limiter->delay_capacity)limiter->delay_position=0;

    if (peak > limiter->ceiling_linear && peak > 0.0f) {
        requested_gain = limiter->ceiling_linear / peak;
        limiter->hold_until_frame = limiter->processed_frames +
                                    hold;
    }
    if (requested_gain < limiter->gain)
        limiter->gain = requested_gain;
    else if (limiter->processed_frames > limiter->hold_until_frame)
        limiter->gain += (1.0f - limiter->gain) *
                         limiter->release_coefficient;
    if (!isfinite(limiter->gain) || limiter->gain < 0.0f)
        limiter->gain = 0.0f;
    if (limiter->gain > 1.0f) limiter->gain = 1.0f;

    if (limiter->enabled_ramp_remaining > 0u) {
        limiter->enabled_mix += limiter->enabled_step;
        --limiter->enabled_ramp_remaining;
        if (limiter->enabled_ramp_remaining == 0u)
            limiter->enabled_mix = limiter->enabled ? 1.0f : 0.0f;
    }
    limited.l = delayed.l * limiter->gain;
    limited.r = delayed.r * limiter->gain;
    output.l = delayed.l + (limited.l - delayed.l) * limiter->enabled_mix;
    output.r = delayed.r + (limited.r - delayed.r) * limiter->enabled_mix;
    applied_gain = 1.0f + (limiter->gain - 1.0f) * limiter->enabled_mix;
    limiter->applied_gain = applied_gain;

    /* The lookahead envelope normally enforces this ceiling. This final
       guard covers configuration/toggle transitions and floating-point dust. */
    if (limiter->enabled) {
        if (output.l > limiter->ceiling_linear)
            output.l = limiter->ceiling_linear;
        if (output.l < -limiter->ceiling_linear)
            output.l = -limiter->ceiling_linear;
        if (output.r > limiter->ceiling_linear)
            output.r = limiter->ceiling_linear;
        if (output.r < -limiter->ceiling_linear)
            output.r = -limiter->ceiling_linear;
    }
    if (gain_reduction_db != NULL)
        *gain_reduction_db = ts_sister_limiter_gain_reduction_db(limiter);
    ++limiter->processed_frames;
    return ts_stereo_frame_sanitize(output);
}
