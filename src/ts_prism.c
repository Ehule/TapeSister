#include "tapesister/prism.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

const float ts_prism_unison_cents[TS_PRISM_LENSES] =
    {0, -7, 7, -12, 12, -19, 19, -26, 26, -1200, -1207, -1193,
     -3, 3, -10, 10, -16, 16, -23, 23, -31, 31, -1203, -1197};

static float bounded(float x, float lo, float hi, float fallback)
{
    return !isfinite(x) ? fallback : x < lo ? lo : x > hi ? hi : x;
}

void ts_prism_controls_default(TsPrismControls *p)
{
    if (p) *p = (TsPrismControls){.mode=TS_PRISM_SUPERSAW,.lenses=TS_PRISM_BASE_LENSES,
        .spread=.5f,.drift=.15f,.stereo=.8f,.body=.5f,.mix=.8f,.dry_level=1,.color=.5f,
        .drift_rate=.1f,.morph_seconds=5,.seq_rate=2};
}

static void patch_sanitize(TsPrismPatch *p)
{
    if (!p) return;
    p->drift_rate = bounded(p->drift_rate, .005f, 40, .1f);
    p->group_octave = p->group_octave < -3 ? -3 : p->group_octave > 3 ? 3 : p->group_octave;
    if (p->snap < 0 || p->snap > 3) p->snap=0;
    p->enabled = !!p->enabled;
    if (p->mode < 0 || p->mode >= TS_PRISM_MODE_COUNT) p->mode = TS_PRISM_SUPERSAW;
    if (p->input_shape < 0 || p->input_shape >= TS_PRISM_SHAPE_COUNT) p->input_shape = TS_PRISM_BICONVEX;
    if (p->output_shape < 0 || p->output_shape >= TS_PRISM_SHAPE_COUNT) p->output_shape = TS_PRISM_BICONVEX;
    if (p->lenses < 2) p->lenses = 2;
    if (p->lenses > TS_PRISM_LENSES) p->lenses = TS_PRISM_LENSES;
#define UNIT(member, fallback) p->member = bounded(p->member, 0, 1, fallback)
    UNIT(focus, 0);
    UNIT(stereo, .8f); UNIT(mix, .8f); UNIT(color, .5f);
#undef UNIT
    p->spread = bounded(p->spread, 0, 2, .5f);
    p->drift = bounded(p->drift, 0, 2, 0);
    p->body = bounded(p->body, 0, 3, .5f);
    p->dry_level = bounded(p->dry_level, 0, 2, 1);
    p->mute_mask &= (1 << TS_PRISM_LENSES) - 1;
    p->solo_mask &= (1 << TS_PRISM_LENSES) - 1;
    p->output_db = bounded(p->output_db, -12, 12, 0);
    for (int i = 0; i < TS_PRISM_LENSES; ++i) {
        p->octave_offset[i] = p->octave_offset[i] < -3 ? -3 : p->octave_offset[i] > 3 ? 3 : p->octave_offset[i];
        p->trim_db[i] = bounded(p->trim_db[i], -24, 12, 0);
        p->pitch_offset[i] = bounded(p->pitch_offset[i], -1200, 1200, 0);
        p->pan_offset[i] = bounded(p->pan_offset[i], -4, 4, 0);
    }
}

void ts_prism_controls_sanitize(TsPrismControls *p)
{
    if (!p) return;
    TsPrismPatch patch; memcpy(&patch,p,sizeof(patch)); patch_sanitize(&patch); memcpy(p,&patch,sizeof(patch));
    p->captured &= 3;
    if (p->captured & 1) patch_sanitize(&p->a);
    if (p->captured & 2) patch_sanitize(&p->b);
    p->morph_enabled = !!p->morph_enabled && p->captured == 3;
    p->morph=bounded(p->morph,0,1,0);
    p->morph_seconds=bounded(p->morph_seconds,.05f,120,5);
    p->morph_target=!!p->morph_target;
    p->seq_enabled=!!p->seq_enabled;
    p->seq_rate=bounded(p->seq_rate,.05f,32,2);
    if(p->seq_count<0)p->seq_count=0;
    if(p->seq_count>TS_PRISM_LENSES)p->seq_count=TS_PRISM_LENSES;
    unsigned seen=0; int count=0;
    for(int i=0;i<p->seq_count;++i) {
        int lens=p->sequence[i];
        if(lens<0 || lens>=TS_PRISM_LENSES || (seen&(1u<<lens)))continue;
        seen|=1u<<lens;p->sequence[count++]=lens;
    }
    p->seq_count=count;p->locks &= 63;
}

void ts_prism_reset_lenses(TsPrismControls *p)
{
    if (!p) return;
    memset(p->pitch_offset, 0, sizeof(p->pitch_offset));
    memset(p->pan_offset, 0, sizeof(p->pan_offset));
    memset(p->trim_db, 0, sizeof(p->trim_db));
    memset(p->octave_offset, 0, sizeof(p->octave_offset));
    p->mute_mask = p->solo_mask = 0;
}

/* Keep manual faders and mute/solo outside the normalization reference. Their
   gain changes must remain audible even when only one lens is being heard. */
static float lens_weight(const TsPrismPatch *p, int i)
{
    return i >= p->lenses ? 0 : i == 0 ? 1 + 2 * p->body :
        (p->mode == TS_PRISM_SUPERSAW && ts_prism_unison_cents[i] < -1000) ? .3f + .7f * p->body : 1;
}

const char *ts_prism_mode_name(int mode)
{
    static const char *const names[]={"SUPERSAW","ENSEMBLE","HARMONIC","FIFTHS","OCTAVES","CLUSTER","31-TET"};
    return names[mode>=0 && mode<TS_PRISM_MODE_COUNT ? mode : 0];
}

const char *ts_prism_shape_name(int shape)
{
    static const char *const names[] = {
        "BI-CONVEX", "PLANO-CONVEX", "MENISCUS +",
        "BI-CONCAVE", "PLANO-CONCAVE", "MENISCUS -"
    };
    return names[shape >= 0 && shape < TS_PRISM_SHAPE_COUNT ? shape : 0];
}

const char *ts_prism_shape_color_name(int shape)
{
    static const char *const names[] = {"CLEAR", "WARM", "PHASE", "DRIVE", "HOLLOW", "PHASE+DRIVE"};
    return names[shape >= 0 && shape < TS_PRISM_SHAPE_COUNT ? shape : 0];
}

/* Musical interval maps, composed into one reader ratio. These do not claim
   to simulate physical optics. Zero remains the unshifted wet body anchor. */
static float shape_map(float value, int shape)
{
    switch (shape) {
    case TS_PRISM_PLANO_CONVEX: return value * .65f;
    case TS_PRISM_MENISCUS_POSITIVE: return value * (value >= 0 ? 1.25f : .75f);
    case TS_PRISM_BICONCAVE: return -value;
    case TS_PRISM_PLANO_CONCAVE: return -value * .65f;
    case TS_PRISM_MENISCUS_NEGATIVE: return -value * (value >= 0 ? .75f : 1.25f);
    default: return value;
    }
}

/* Stable lens identities: count changes never reassign the surviving pitches
   or pans. The original twelve retain FM's voicing; 23/24 add two body lenses. Focus
   contracts fine pitch/time differences, retaining intentional octave bands. */
static TsPrismLensView geometry(const TsPrismPatch *p, int i, double seconds)
{
    TsPrismLensView v = {0};
    float fine = ts_prism_unison_cents[i];
    float octave = 0, divergence = 1 - p->focus;
    if (p->mode == TS_PRISM_SUPERSAW && fine < -1000) {
        octave = -1200;
        fine += 1200;
    } else if (p->mode == TS_PRISM_ENSEMBLE) {
        fine = i == 0 ? 0 : (i & 1 ? -1.f : 1.f) * (2.f + i * .65f);
    }
    if (p->mode >= TS_PRISM_HARMONIC) {
        float sign=i&1 ? 1.f : -1.f;
        int step=(i+1)/2;
        fine=0;
        if(p->mode==TS_PRISM_HARMONIC)octave=i ? sign*1200*log2f(1+step*.5f) : 0;
        if(p->mode==TS_PRISM_FIFTHS)octave=i ? sign*(step%2 ? 701.955f : 1200)*(1+(step-1)/4) : 0;
        if(p->mode==TS_PRISM_OCTAVES)octave=i ? sign*1200*(1+(step-1)%3) : 0;
        if(p->mode==TS_PRISM_CLUSTER)fine=i ? sign*step*18 : 0;
        if(p->mode==TS_PRISM_MICRO)octave=i ? sign*step*(1200.f/31) : 0;
    }
    float wander = i == 0 ? 0 : (float)(
        .7 * sin(seconds * (.29 + .031 * i) + 1.71 * i) +
        .3 * sin(seconds * (.13 + .017 * i) + .93 * i));
    /* Preserve the first half of each dial, then open the upper range. */
    float wide = fmaxf(0, 2 * p->spread - 1);
    float wild = fmaxf(0, 2 * p->drift - 1);
    float detune = 2 * p->spread + 6 * wide * wide;
    float drift_cents = 5 * p->drift + 20 * wild * wild;
    float shaped_octave = shape_map(octave, p->input_shape);
    float shaped_fine = shape_map(octave + fine * detune, p->input_shape) - shaped_octave;
    v.refraction_cents = shaped_octave + (shaped_fine + p->pitch_offset[i] +
                                           wander * drift_cents) * divergence;
    float transpose = 1200.f * (p->group_octave + p->octave_offset[i]);
    v.cents = shape_map(v.refraction_cents, p->output_shape) + transpose;
    v.refraction_cents += transpose;
    v.delay_ms = i == 0 ? 0 : (p->mode == TS_PRISM_ENSEMBLE ?
        2.f + i * .6f : .2f + (i % 5) * .3f) * divergence;
    v.pan = i == 0 ? 0 : (i & 1 ? -1.f : 1.f) *
        (.3f + .65f * ((i + 1) / 2) / 6.f) * p->stereo;
    /* Interleave new pan positions without moving any original voice. */
    if (i >= TS_PRISM_BASE_LENSES)
        v.pan = (i & 1 ? 1.f : -1.f) *
            (.3f + .65f * (((i - TS_PRISM_BASE_LENSES) / 2) + .5f) / 6.f) * p->stereo;
    if (octave) v.pan *= .25f; /* Keep sub voices near the center. */
    /* Let explicit edits reach hard pan through narrowing glass. Stock pans
       keep their mapping; clamp the audible result, not the glass input. */
    v.pan += p->pan_offset[i];
    v.refraction_pan = bounded(v.pan, -1, 1, 0);
    v.pan = bounded(shape_map(v.pan, p->output_shape), -1, 1, 0);
    int solo = p->solo_mask & ((1 << p->lenses) - 1);
    int audible = !(p->mute_mask & (1 << i)) && (!solo || (solo & (1 << i)));
    v.level = audible ? lens_weight(p, i) * powf(10, p->trim_db[i] / 20) : 0;
    return v;
}

static TsPrismPatch prism_patch(const TsPrismControls *c)
{
    TsPrismPatch p; memcpy(&p,c,sizeof(p)); return p;
}
static float lerp(float a,float b,float t) { return a+(b-a)*t; }
static TsPrismLensView morph_geometry(const TsPrismPatch *a,const TsPrismPatch *b,int i,double time,float t)
{
    if(a==b)return geometry(a,i,time);
    TsPrismLensView x=geometry(a,i,time),y=geometry(b,i,time);
    x.cents=lerp(x.cents,y.cents,t);x.refraction_cents=lerp(x.refraction_cents,y.refraction_cents,t);
    x.pan=lerp(x.pan,y.pan,t);x.refraction_pan=lerp(x.refraction_pan,y.refraction_pan,t);
    x.delay_ms=lerp(x.delay_ms,y.delay_ms,t);x.level=lerp(x.level,y.level,t);
    return x;
}

TsPrismView ts_prism_control_view(const TsPrismControls *controls)
{
    TsPrismView v = {0};
    if (!controls) return v;
    TsPrismControls p = *controls;
    ts_prism_controls_sanitize(&p);
    TsPrismPatch patch=prism_patch(&p);
    for (int i = 0; i < TS_PRISM_LENSES; ++i) v.lens[i] = p.morph_enabled ? morph_geometry(&p.a,&p.b,i,0,p.morph) : geometry(&patch,i,0);
    v.wet = p.enabled ? p.mix : 0;
    v.dry = p.enabled ? (1 - p.mix) * p.dry_level : 1;
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
    p->dry = 1;
    p->controls = controls;
    ts_prism_controls_sanitize(&p->controls);
    for (int i = 0; i < TS_PRISM_LENSES; ++i) {
        p->lens[i].phase = fmod(.21 + i * .61803398875, 1);
        p->lens[i].ratio = p->lens[i].ratio_target = 1;
        p->lens[i].refraction_ratio = p->lens[i].refraction_ratio_target = 1;
        for (int stage = 0; stage < 2; ++stage) {
            TsPrismGlass *g = &p->lens[i].glass[stage];
            float cutoff = fminf(rate * .2f, 700.f + 190.f * i + stage * 450.f);
            g->low_coefficient = 1 - expf(-6.28318530718f * cutoff / rate);
            float tangent = tanf(3.14159265359f * fminf(rate * .2f, 180.f + 130.f * i + stage * 600.f) / rate);
            g->allpass_coefficient = (tangent - 1) / (tangent + 1);
        }
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
    phase -= floor(phase); /* Wide pitch edits can advance over a full cycle. */
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

static float glass_channel(float x, float *low, float *memory, const TsPrismGlass *g,
                            const float *mix)
{
    *low += g->low_coefficient * (x - *low);
    float phase = g->allpass_coefficient * x + *memory;
    *memory = x - g->allpass_coefficient * phase;
    /* Prevent silent filter tails from entering expensive subnormal arithmetic. */
    if (fabsf(*low) < 1e-20f) *low = 0;
    if (fabsf(*memory) < 1e-20f) *memory = 0;
    /* Normalized rational saturation stays finite at large internal levels. */
    float driven = 2 * x / (1 + fabsf(x));
    float phase_driven = 2.5f * phase / (1 + 1.5f * fabsf(phase));
    return x + mix[TS_PRISM_PLANO_CONVEX] * (*low - x) +
        mix[TS_PRISM_MENISCUS_POSITIVE] * (phase - x) +
        mix[TS_PRISM_BICONCAVE] * (driven - x) +
        mix[TS_PRISM_PLANO_CONCAVE] * (-*low) +
        mix[TS_PRISM_MENISCUS_NEGATIVE] * (phase_driven - x);
}

TsStereoFrame ts_prism_process(TsPrism *p, TsStereoFrame input)
{
    input = ts_stereo_frame_sanitize(input);
    if (!p || !p->history) return input;
    p->history[p->write] = input; /* Always prime: no stale audio on engagement. */
    const TsPrismControls *c = &p->controls;

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
        double hop=(double)TS_PRISM_HOP/p->sample_rate;
        TsPrismPatch patch=prism_patch(c);
        if(c->morph_trigger != p->morph_seen) {
            p->morph_seen=c->morph_trigger;p->morph_start=p->morph_position;p->morph_elapsed=0;
        }
        if(c->morph_enabled) {
            if(c->morph_trigger) {
                p->morph_position=lerp(p->morph_start,(float)c->morph_target,
                    fminf(1,p->morph_elapsed/c->morph_seconds));
                p->morph_elapsed+=(float)hop;
            } else p->morph_position=c->morph;
        } else p->morph_position=c->morph;
        float t=c->morph_enabled ? p->morph_position : 0;
        const TsPrismPatch *a=c->morph_enabled ? &c->a : &patch;
        const TsPrismPatch *b=c->morph_enabled ? &c->b : &patch;
        p->wet_target=c->enabled ? lerp(a->mix,b->mix,t) : 0;
        p->dry_target=c->enabled ? lerp((1-a->mix)*a->dry_level,(1-b->mix)*b->dry_level,t) : 1;
        p->gain_target=powf(10,lerp(a->output_db,b->output_db,t)/20);
        for(int stage=0;stage<2;++stage)for(int shape=1;shape<TS_PRISM_SHAPE_COUNT;++shape)
            p->glass_target[stage][shape]=lerp((stage?a->output_shape:a->input_shape)==shape ? a->color : 0,
                (stage?b->output_shape:b->input_shape)==shape ? b->color : 0,t);
        if(p->seq_seen!=c->seq_reset) { p->seq_seen=c->seq_reset;p->sequence_phase=0; }
        int eligible[TS_PRISM_LENSES],count=0;unsigned members=0;
        for(int j=0;j<c->seq_count;++j) {
            int i=c->sequence[j]; members|=1u<<i;
            if(lerp(lens_weight(a,i),lens_weight(b,i),t)>.001f)eligible[count++]=i;
        }
        p->seq_lens=c->seq_enabled && count ? eligible[(unsigned long long)p->sequence_phase%count]+1 : 0;
        for (int i = 0; i < TS_PRISM_LENSES; ++i) {
            TsPrismLensView v=morph_geometry(a,b,i,p->drift_time,t);
            if(c->seq_enabled && (members&(1u<<i)) && p->seq_lens!=i+1)v.level=0;
            p->lens[i].ratio_target = exp2f(v.cents / 1200.f);
            p->lens[i].refraction_ratio_target = exp2f(v.refraction_cents / 1200.f);
            p->lens[i].refraction_pan_target = v.refraction_pan;
            p->lens[i].delay_target = v.delay_ms * p->sample_rate / 1000;
            p->lens[i].level_target = v.level;
            p->lens[i].weight_target = lerp(lens_weight(a,i),lens_weight(b,i),t);
            p->lens[i].pan_target = v.pan;
        }
        float rate=lerp(a->drift_rate,b->drift_rate,t);
        p->pitch_smoothing=1-expf(-1.f/(p->sample_rate*fmaxf(.0005f,.02f/fmaxf(1,rate))));
        p->drift_time+=hop*rate/.1;
        if(c->seq_enabled)p->sequence_phase+=hop*c->seq_rate;
    }
    float wet_target=p->wet_target, dry_target=p->dry_target;
    p->dry += p->smoothing * (dry_target - p->dry);
    if (fabs(p->dry - dry_target) < .000001) p->dry = dry_target;
    p->wet += p->smoothing * (wet_target - p->wet);
    if (wet_target == 0 && p->wet < .000001f) p->wet = 0;
    /* Gain target is cached at control rate; no pow in the voice loop. */
    p->gain += p->smoothing * (p->gain_target - p->gain);
    for (int stage = 0; stage < 2; ++stage) for (int shape = 1; shape < TS_PRISM_SHAPE_COUNT; ++shape) {
        float target = p->glass_target[stage][shape];
        float *mix = &p->glass_mix[stage][shape];
        *mix += p->smoothing * (target - *mix);
        if (fabsf(*mix - target) < .000001f) *mix = target;
    }
    float body_target=fabs(p->lens[0].ratio_target-1)>.00001 ? 1 : 0;
    p->body_shift+=p->smoothing*(body_target-p->body_shift);
    TsStereoFrame sum = {0};
    float energy_l = 0, energy_r = 0;
    for (int i = 0; i < TS_PRISM_LENSES; ++i) {
        TsPrismLens *v = &p->lens[i];
#define SMOOTH(field) v->field += p->smoothing * (v->field##_target - v->field)
        v->ratio+=p->pitch_smoothing*(v->ratio_target-v->ratio);
        SMOOTH(delay); SMOOTH(level); SMOOTH(pan); SMOOTH(weight);
        v->refraction_ratio+=p->pitch_smoothing*(v->refraction_ratio_target-v->refraction_ratio);
        SMOOTH(refraction_pan);
#undef SMOOTH
        if (!v->level_target && v->level < .000001f) v->level = 0;
        if (!v->weight_target && v->weight < .000001f) v->weight = 0;
        /* Phase runs independently of bypass, GUI visibility and block size. */
        v->phase = advance_phase(v->phase, v->ratio, p->window_frames);
        if (p->window_fade < 1)
            v->previous_phase = advance_phase(v->previous_phase, v->ratio, p->previous_window);
        float balance_l = v->pan > 0 ? 1 - v->pan : 1;
        float balance_r = v->pan < 0 ? 1 + v->pan : 1;
        float reference_l = v->weight * balance_l, reference_r = v->weight * balance_r;
        energy_l += reference_l * reference_l;
        energy_r += reference_r * reference_r;
        if (p->wet == 0 || v->level == 0) continue;
        TsStereoFrame lens = input;
        if (i != 0 || p->body_shift > .000001f) {
            float base = 2 + p->sample_rate * .002f + v->delay;
            lens = read_lens(p, v->phase, p->window_frames, base);
            if (p->window_fade < 1) {
                TsStereoFrame old = read_lens(p, v->previous_phase, p->previous_window, base);
                float t = p->window_fade * p->window_fade * (3 - 2 * p->window_fade);
                lens.l = old.l + t * (lens.l - old.l);
                lens.r = old.r + t * (lens.r - old.r);
            }
            /* Both pitch maps share this reader; the two glass colors then
               run in series on each shifted voice, before its trim and pan. */
            if(i==0) { lens.l=lerp(input.l,lens.l,p->body_shift);lens.r=lerp(input.r,lens.r,p->body_shift); }
            for (int stage = 0; i!=0 && stage < 2; ++stage) {
                TsPrismGlass *g = &v->glass[stage];
                lens.l = glass_channel(lens.l, &g->low.l, &g->allpass_memory.l, g, p->glass_mix[stage]);
                lens.r = glass_channel(lens.r, &g->low.r, &g->allpass_memory.r, g, p->glass_mix[stage]);
            }
        }
        /* Stereo balance retains the channels; no mono summing, phase flips,
           or artificial cross-channel signal. Center and bass stay solid. */
        float left = v->level * balance_l;
        float right = v->level * balance_r;
        sum.l += lens.l * left;
        sum.r += lens.r * right;

    }
    p->write = (p->write + 1) % p->capacity;
    ++p->clock;
    if (p->wet == 0 && p->dry == 1) return input; /* Exact settled bypass, including output trim. */
    /* Root-sum-square compensation preserves decorrelated voice energy,
       including the actual stereo balance and smoothed lens fades. A floor
       of one avoids boosting the first samples of engagement. Correlated
       peaks intentionally reach the existing final linked limiter. */
    float gain_l = p->gain / sqrtf(fmaxf(1, energy_l));
    float gain_r = p->gain / sqrtf(fmaxf(1, energy_r));
    return ts_stereo_frame_sanitize((TsStereoFrame){
        input.l * p->dry + sum.l * gain_l * p->wet,
        input.r * p->dry + sum.r * gain_r * p->wet});
}

TsPrismView ts_prism_view(const TsPrism *p)
{
    TsPrismView v = {0};
    if (!p || !p->history) return v;
    v.valid = 1;
    v.morph=p->morph_position;v.seq_lens=p->seq_lens;
    v.wet = p->wet;
    v.dry = p->dry;
    for (int i = 0; i < TS_PRISM_LENSES; ++i) {
        v.lens[i] = (TsPrismLensView){1200 * log2f(p->lens[i].ratio),
            p->lens[i].delay * 1000 / p->sample_rate,
            p->lens[i].pan, p->lens[i].level,
            1200 * log2f(p->lens[i].refraction_ratio), p->lens[i].refraction_pan};
    }
    return v;
}
