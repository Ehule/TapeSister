#include "tapesister/prism.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

const float ts_prism_unison_cents[TS_PRISM_LENSES] =
    {0, -7, 7, -12, 12, -19, 19, -26, 26, -1200, -1207, -1193};

static float bounded(float x, float lo, float hi, float fallback)
{
    return !isfinite(x) ? fallback : x < lo ? lo : x > hi ? hi : x;
}

void ts_prism_controls_default(TsPrismControls *p)
{
    if (p) *p = (TsPrismControls){0, TS_PRISM_SUPERSAW, 12,
                                 .5f, .15f, 0, .8f, .5f, .8f, 0};
}

void ts_prism_controls_sanitize(TsPrismControls *p)
{
    if (!p) return;
    p->enabled = !!p->enabled;
    if (p->mode < 0 || p->mode >= TS_PRISM_MODE_COUNT) p->mode = TS_PRISM_SUPERSAW;
    if (p->lenses < 2) p->lenses = 2;
    if (p->lenses > TS_PRISM_LENSES) p->lenses = TS_PRISM_LENSES;
#define UNIT(member, fallback) p->member = bounded(p->member, 0, 1, fallback)
    UNIT(spread, .5f); UNIT(drift, 0); UNIT(focus, 0);
    UNIT(stereo, .8f); UNIT(body, .5f); UNIT(mix, .8f);
#undef UNIT
    p->output_db = bounded(p->output_db, -12, 6, 0);
}

const char *ts_prism_mode_name(int mode)
{
    return mode == TS_PRISM_ENSEMBLE ? "ENSEMBLE" : "SUPERSAW";
}

/* Stable lens identities: count changes never reassign the surviving pitches
   or pans. Body lenses are the last three, exactly as in FM Unison. Focus
   contracts fine pitch/time differences, retaining intentional octave bands. */
static TsPrismLensView geometry(const TsPrismControls *p, int i, double seconds)
{
    TsPrismLensView v = {0};
    float fine = ts_prism_unison_cents[i];
    float octave = 0, divergence = 1 - p->focus;
    if (p->mode == TS_PRISM_SUPERSAW && i >= 9) {
        octave = -1200;
        fine += 1200;
    } else if (p->mode == TS_PRISM_ENSEMBLE) {
        fine = i == 0 ? 0 : (i & 1 ? -1.f : 1.f) * (2.f + i * .65f);
    }
    float wander = i == 0 ? 0 : (float)(
        .7 * sin(seconds * (.29 + .031 * i) + 1.71 * i) +
        .3 * sin(seconds * (.13 + .017 * i) + .93 * i));
    v.cents = octave + (fine * 2 * p->spread + wander * 5 * p->drift) * divergence;
    v.delay_ms = i == 0 ? 0 : (p->mode == TS_PRISM_ENSEMBLE ?
        2.f + i * .6f : .2f + (i % 5) * .3f) * divergence;
    v.pan = i == 0 ? 0 : (i & 1 ? -1.f : 1.f) *
        (.3f + .65f * ((i + 1) / 2) / 6.f) * p->stereo;
    if (octave) v.pan *= .25f; /* Keep sub voices near the center. */
    v.level = i >= p->lenses ? 0 : i == 0 ? 1 + 2 * p->body :
        octave ? .3f + .7f * p->body : 1.f;
    return v;
}

TsPrismView ts_prism_control_view(const TsPrismControls *controls)
{
    TsPrismView v = {0};
    if (!controls) return v;
    TsPrismControls p = *controls;
    ts_prism_controls_sanitize(&p);
    for (int i = 0; i < TS_PRISM_LENSES; ++i) v.lens[i] = geometry(&p, i, 0);
    v.wet = p.enabled ? p.mix : 0;
    return v;
}

int ts_prism_prepare(TsPrism *p, uint32_t rate)
{
    if (!p || rate == 0 || rate > 768000) return 0;
    if (p->history && p->sample_rate == rate) return 1;
    size_t capacity = (size_t)ceil(rate * .080) + 4;
    if (capacity < 16) capacity = 16;
    TsStereoFrame *history = calloc(capacity, sizeof(*history));
    if (!history) return 0;
    TsPrismControls controls = p->controls;
    free(p->history);
    memset(p, 0, sizeof(*p));
    p->history = history;
    p->capacity = capacity;
    p->sample_rate = rate;
    p->window_frames = p->window_target = fmax(4, rate * .040);
    p->previous_window = p->window_frames;
    p->window_fade = 1;
    p->window_fade_step = 1.f / fmaxf(1, rate * .040f);
    p->smoothing = 1 - expf(-1.f / (.020f * rate));
    p->gain = p->gain_target = 1;
    p->controls = controls;
    ts_prism_controls_sanitize(&p->controls);
    for (int i = 0; i < TS_PRISM_LENSES; ++i) {
        p->lens[i].phase = fmod(.21 + i * .61803398875, 1);
        p->lens[i].ratio = p->lens[i].ratio_target = 1;
    }
    for (int i = 0; i <= 1024; ++i)
        p->hann[i] = (float)(.5 - .5 * cos(6.283185307179586 * i / 1024));
    return 1;
}

void ts_prism_free(TsPrism *p)
{
    if (!p) return;
    free(p->history);
    memset(p, 0, sizeof(*p));
}

void ts_prism_set_controls(TsPrism *p, const TsPrismControls *controls)
{
    if (!p || !controls) return;
    p->controls = *controls;
    ts_prism_controls_sanitize(&p->controls);
}

static TsStereoFrame read_delay(const TsPrism *p, float delay)
{
    double at = (double)p->write - delay;
    if (at < 0) at += p->capacity;
    size_t a = (size_t)at;
    size_t b = a + 1 == p->capacity ? 0 : a + 1;
    float t = (float)(at - a);
    return (TsStereoFrame){p->history[a].l + t * (p->history[b].l - p->history[a].l),
                           p->history[a].r + t * (p->history[b].r - p->history[a].r)};
}

static TsStereoFrame read_lens(const TsPrism *p, double phase, double span, float base)
{
    float position = (float)(phase * 1024);
    int index = (int)position;
    if (index > 1023) index = 1023;
    float hann = p->hann[index] + (position - index) *
        (p->hann[index + 1] - p->hann[index]);
    double other = phase < .5 ? phase + .5 : phase - .5;
    TsStereoFrame a = read_delay(p, (float)(base + phase * span));
    TsStereoFrame b = read_delay(p, (float)(base + other * span));
    return (TsStereoFrame){a.l * hann + b.l * (1 - hann),
                           a.r * hann + b.r * (1 - hann)};
}

static double advance_phase(double phase, double ratio, double span)
{
    phase += (1 - ratio) / span;
    if (phase < 0) phase += 1;
    if (phase >= 1) phase -= 1;
    return phase;
}

/* A bounded, shared YIN-style period estimate. Synchronizing the crossfade
   span to an even number of source periods avoids the fixed-window sideband
   grid that otherwise makes octave-down tones miss their intended pitch.
   This analysis affects read geometry only: the audio remains full stereo.
   Aperiodic material keeps the last span; it never restarts or zeros a reader. */
static void track_period(TsPrism *p)
{
    if (p->sample_rate < 8000 || p->clock < p->sample_rate / 12) return;
    unsigned stride = (p->sample_rate + 11999) / 12000;
    int maximum = (int)(p->sample_rate / (55 * stride));
    int minimum = (int)(p->sample_rate / (1400 * stride));
    if (minimum < 2) minimum = 2;
    if (maximum > 258) maximum = 258;
    float signal[520], power[2] = {0};
    for (int i = 0; i < 2 * maximum + 2; ++i) {
        size_t at = (p->write + p->capacity - (size_t)i * stride) % p->capacity;
        power[0] += p->history[at].l * p->history[at].l;
        power[1] += p->history[at].r * p->history[at].r;
    }
    if (fmaxf(power[0], power[1]) < .00001f) return;
    for (int i = 0; i < 2 * maximum + 2; ++i) {
        size_t at = (p->write + p->capacity - (size_t)i * stride) % p->capacity;
        signal[i] = power[0] >= power[1] ? p->history[at].l : p->history[at].r;
    }
    double cumulative = 0;
    p->period_difference[0] = 1;
    for (int lag = 1; lag <= maximum; ++lag) {
        double difference = 0;
        for (int j = 0; j < maximum; ++j) {
            double delta = signal[j] - signal[j + lag];
            difference += delta * delta;
        }
        cumulative += difference;
        p->period_difference[lag] = cumulative > 1e-15 ?
            (float)(difference * lag / cumulative) : 1;
    }
    for (int lag = minimum; lag < maximum - 1; ++lag) {
        if (p->period_difference[lag] > .12f) continue;
        while (lag < maximum - 1 &&
               p->period_difference[lag + 1] < p->period_difference[lag]) ++lag;
        double a = p->period_difference[lag - 1], b = p->period_difference[lag];
        double c = p->period_difference[lag + 1], denominator = a - 2 * b + c;
        double fraction = denominator > 1e-12 ? .5 * (a - c) / denominator : 0;
        double period = (lag + fraction) * stride;
        /* Refine at full rate around the coarse trough. Without this step,
           decimation's sub-sample bias is audible in sustained upper notes. */
        int center = (int)lrint(period), best = center;
        double best_error = 1e30, errors[3] = {0};
        for (int candidate = center - (int)stride; candidate <= center + (int)stride; ++candidate) {
            if (candidate < 2) continue;
            double error = 0;
            for (int j = 0; j < maximum; ++j) {
                size_t at = (p->write + p->capacity - (size_t)j * stride) % p->capacity;
                size_t behind = (at + p->capacity - candidate) % p->capacity;
                double delta = power[0] >= power[1] ?
                    p->history[at].l - p->history[behind].l : p->history[at].r - p->history[behind].r;
                error += delta * delta;
            }
            if (error < best_error) {best_error = error; best = candidate;}
        }
        for (int k = 0; k < 3; ++k) for (int j = 0; j < maximum; ++j) {
            size_t at = (p->write + p->capacity - (size_t)j * stride) % p->capacity;
            size_t behind = (at + p->capacity - (best + k - 1)) % p->capacity;
            double delta = power[0] >= power[1] ?
                p->history[at].l - p->history[behind].l : p->history[at].r - p->history[behind].r;
            errors[k] += delta * delta;
        }
        denominator = errors[0] - 2 * errors[1] + errors[2];
        period = best + (denominator > 1e-12 ? .5 * (errors[0] - errors[2]) / denominator : 0);
        double multiples = fmax(1, floor(p->sample_rate * .040 / (2 * period) + .5));
        p->window_target = fmin(p->sample_rate * .060, 2 * multiples * period);
        break;
    }
}

TsStereoFrame ts_prism_process(TsPrism *p, TsStereoFrame input)
{
    input = ts_stereo_frame_sanitize(input);
    if (!p || !p->history) return input;
    p->history[p->write] = input; /* Always prime: no stale audio on engagement. */
    const TsPrismControls *c = &p->controls;
    float wet_target = c->enabled ? c->mix : 0;
    /* Analysis is scheduled at roughly 47 Hz, regardless of device buffer
       size. A fixed upper bound keeps both memory and callback work bounded. */
    unsigned analysis_hop = (p->sample_rate / 47 / TS_PRISM_HOP + 1) * TS_PRISM_HOP;
    if ((c->enabled || p->wet > 0) && p->clock % analysis_hop == 0) track_period(p);
    /* A changing source period replaces the read geometry through a 40 ms
       crossfade. Slewing a delay span would bend the instrument's pitch. */
    if (p->window_fade >= 1 && fabs(p->window_target - p->window_frames) > .1) {
        p->previous_window = p->window_frames;
        p->window_frames = p->window_target;
        p->window_fade = 0;
        for (int i = 0; i < TS_PRISM_LENSES; ++i)
            p->lens[i].previous_phase = p->lens[i].phase;
    }
    if (p->window_fade < 1) p->window_fade = fminf(1, p->window_fade + p->window_fade_step);
    if ((p->clock % TS_PRISM_HOP) == 0) {
        p->gain_target = powf(10, c->output_db / 20);
        for (int i = 0; i < TS_PRISM_LENSES; ++i) {
            TsPrismLensView v = geometry(c, i, (double)p->clock / p->sample_rate);
            p->lens[i].ratio_target = exp2f(v.cents / 1200.f);
            p->lens[i].delay_target = v.delay_ms * p->sample_rate / 1000;
            p->lens[i].level_target = v.level;
            p->lens[i].pan_target = v.pan;
        }
    }
    p->wet += p->smoothing * (wet_target - p->wet);
    if (wet_target == 0 && p->wet < .000001f) p->wet = 0;
    /* Gain target is cached at control rate; no pow in the voice loop. */
    p->gain += p->smoothing * (p->gain_target - p->gain);
    TsStereoFrame sum = {0};
    float normalization = 0;
    for (int i = 0; i < TS_PRISM_LENSES; ++i) {
        TsPrismLens *v = &p->lens[i];
#define SMOOTH(field) v->field += p->smoothing * (v->field##_target - v->field)
        SMOOTH(ratio); SMOOTH(delay); SMOOTH(level); SMOOTH(pan);
#undef SMOOTH
        if (!v->level_target && v->level < .000001f) v->level = 0;
        /* Phase runs independently of bypass, GUI visibility and block size. */
        v->phase = advance_phase(v->phase, v->ratio, p->window_frames);
        if (p->window_fade < 1)
            v->previous_phase = advance_phase(v->previous_phase, v->ratio, p->previous_window);
        if (p->wet == 0 || v->level == 0) continue;
        TsStereoFrame lens = input;
        if (i != 0) {
            float base = 2 + p->sample_rate * .002f + v->delay;
            lens = read_lens(p, v->phase, p->window_frames, base);
            if (p->window_fade < 1) {
                TsStereoFrame old = read_lens(p, v->previous_phase, p->previous_window, base);
                float t = p->window_fade * p->window_fade * (3 - 2 * p->window_fade);
                lens.l = old.l + t * (lens.l - old.l);
                lens.r = old.r + t * (lens.r - old.r);
            }
        }
        /* Stereo balance retains the channels; no mono summing, phase flips,
           or artificial cross-channel signal. Center and bass stay solid. */
        sum.l += lens.l * v->level * (v->pan > 0 ? 1 - v->pan : 1);
        sum.r += lens.r * v->level * (v->pan < 0 ? 1 + v->pan : 1);
        normalization += v->level;
    }
    p->write = (p->write + 1) % p->capacity;
    ++p->clock;
    if (p->wet == 0) return input; /* Exact settled bypass, including output trim. */
    float gain = normalization > 0 ? p->gain / normalization : 0;
    return ts_stereo_frame_sanitize((TsStereoFrame){
        input.l * (1 - p->wet) + sum.l * gain * p->wet,
        input.r * (1 - p->wet) + sum.r * gain * p->wet});
}

TsPrismView ts_prism_view(const TsPrism *p)
{
    TsPrismView v = {0};
    if (!p || !p->history) return v;
    v.valid = 1;
    v.wet = p->wet;
    for (int i = 0; i < TS_PRISM_LENSES; ++i) {
        v.lens[i] = (TsPrismLensView){1200 * log2f(p->lens[i].ratio),
            p->lens[i].delay * 1000 / p->sample_rate,
            p->lens[i].pan, p->lens[i].level};
    }
    return v;
}
