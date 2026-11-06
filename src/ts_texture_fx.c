/* All-pass coefficient construction adapted from SunVox psynth_dsp.cpp.
   Copyright (c) 2007-2025 Alexander Zolotov, WarmPlace.ru. MIT license;
   full notice in third_party/sunvox/LICENSE. Network, controls and resonator
   implementation are TapeSister's own. */
#include "tapesister/texture_fx.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define PI 3.14159265358979323846
static float unit(float x) { return !isfinite(x) || x < 0 ? 0 : x > 1 ? 1 : x; }
static float follow(float x, float y, float k) { return x + (y - x) * k; }
static TsStereoFrame finish(TsStereoFrame in, TsStereoFrame wet, float mix, float gain, float *direct) {
    *direct = 1 - mix;
    return ts_stereo_frame_sanitize(
        (TsStereoFrame){(in.l * (1 - mix) + wet.l * mix) * gain, (in.r * (1 - mix) + wet.r * mix) * gain});
}
void ts_dispersion_init(TsDispersion *s, unsigned rate) {
    memset(s, 0, sizeof(*s));
    s->rate = rate;
    s->gain = s->gain_target = 1;
    s->smooth = 1 - expf(-1.f / (.02f * rate));
}
TsStereoFrame ts_dispersion_process(TsDispersion *s, TsStereoFrame in, float a, float b, float c, float mix,
                                    float gain, float gate, float *direct) {
    in = ts_stereo_frame_sanitize(in);
    float k = s->smooth;
    s->a = follow(s->a, unit(a), k);
    s->b = follow(s->b, unit(b), k);
    s->c = follow(s->c, unit(c), k);
    s->mix = follow(s->mix, unit(mix) * unit(gate), k);
    if (s->gain_db != gain) {
        s->gain_db = gain;
        s->gain_target = powf(10, gain * .05f);
    }
    s->gain = follow(s->gain, 1 + (s->gain_target - 1) * unit(gate), k);
    if (!s->ready || !s->clock--) {
        s->clock = s->rate / 500;
        s->ready = 1;
        double center = 80 * pow(100., s->a), q = .5 + 7.5 * s->b;
        for (int ch = 0; ch < 2; ++ch)
            for (int j = 0; j < 8; ++j) {
                double hz = center * pow(2., ((j - 3.5) / 7. + (ch ? .15 : -.15)) * s->c * 2.);
                if (hz > s->rate * .42)
                    hz = s->rate * .42;
                double w = 2 * PI * hz / s->rate, alpha = sin(w) / (2 * q);
                s->b0[ch][j] = (1 - alpha) / (1 + alpha);
                s->b1[ch][j] = -2 * cos(w) / (1 + alpha);
            }
    }
    TsStereoFrame wet = in;
    for (int ch = 0; ch < 2; ++ch) {
        double x = ch ? in.r : in.l;
        for (int j = 0; j < 8; ++j) {
            double *z = s->z[ch][j], v = s->b0[ch][j] * x + z[0];
            z[0] = s->b1[ch][j] * (x - v) + z[1];
            z[1] = x - s->b0[ch][j] * v;
            if (!isfinite(v) || fabs(v) > 1e12) {
                z[0] = z[1] = 0;
                v = 0;
            }
            if (fabs(z[0]) < 1e-25)
                z[0] = 0;
            if (fabs(z[1]) < 1e-25)
                z[1] = 0;
            x = v;
        }
        if (ch)
            wet.r = (float)x;
        else
            wet.l = (float)x;
    }
    if (!mix || !gate) {
        *direct = 1;
        return in;
    }
    TsStereoFrame out = finish(in, wet, s->mix, s->gain, direct);
    *direct *= s->gain;
    return out;
}
float ts_resonator_note(float a) { return 36 + unit(a) * 48; }
int ts_resonator_init(TsResonator *s, unsigned rate) {
    memset(s, 0, sizeof(*s));
    s->rate = rate;
    s->gain = s->gain_target = 1;
    s->smooth = 1 - expf(-1.f / (.02f * rate));
    s->a = .25f;
    s->b = .5f;
    s->c = .5f;
    s->capacity = (size_t)ceil(rate / 65.0) + 8;
    s->data = calloc(s->capacity * 12, sizeof(float));
    return s->data != NULL;
}
void ts_resonator_free(TsResonator *s) {
    free(s->data);
    memset(s, 0, sizeof(*s));
}
TsStereoFrame ts_resonator_process(TsResonator *s, TsStereoFrame in, float a, float b, float c, float mix,
                                   float gain, float gate, float *direct) {
    in = ts_stereo_frame_sanitize(in);
    if (!s->data) {
        *direct = 1;
        return in;
    }
    float k = s->smooth;
    s->a = follow(s->a, unit(a), k);
    s->b = follow(s->b, unit(b), k);
    s->c = follow(s->c, unit(c), k);
    s->mix = follow(s->mix, unit(mix) * unit(gate), k);
    if (s->gain_db != gain) {
        s->gain_db = gain;
        s->gain_target = powf(10, gain * .05f);
    }
    s->gain = follow(s->gain, 1 + (s->gain_target - 1) * unit(gate), k);
    if (!s->ready || !s->clock--) {
        s->clock = s->rate / 500;
        s->ready = 1;
        static const float interval[] = {0, 7, 12, 19, 24, 31};
        for (int j = 0; j < 6; ++j)
            s->delay[j] = s->rate / (440 * pow(2., (ts_resonator_note(s->a) + interval[j] - 69) / 12.));
    }
    TsStereoFrame wet = {0, 0};
    /* The six-voice average and .35 excitation previously lost ~17 dB for
       broadband input. Fixed wet makeup preserves the feedback law and
       dynamics; it does not normalize the input or increase recirculation. */
    float feedback = .15f + .835f * s->b, damping = .03f + .92f * (1 - s->c);
    for (int j = 0; j < 6; ++j) {
        double p = s->position - s->delay[j];
        while (p < 0)
            p += s->capacity;
        size_t i = (size_t)p, i2 = (i + 1) % s->capacity;
        float fraction = (float)(p - i);
        for (int ch = 0; ch < 2; ++ch) {
            float *buf = s->data + (j * 2 + ch) * s->capacity;
            float v = buf[i] + (buf[i2] - buf[i]) * fraction;
            s->damp[j][ch] += damping * (v - s->damp[j][ch]);
            float write = (ch ? in.r : in.l) * .35f + s->damp[j][ch] * feedback;
            buf[s->position] = isfinite(write) ? write / (1 + fabsf(write) * .1f) : 0;
            if (ch)
                wet.r += v * (4.f / 6);
            else
                wet.l += v * (4.f / 6);
        }
    }
    s->position = (s->position + 1) % s->capacity;
    if (!mix || !gate) {
        *direct = 1;
        return in;
    }
    TsStereoFrame out = finish(in, wet, s->mix, s->gain, direct);
    *direct *= s->gain;
    return out;
}
