#include "tapesister/pr13.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static float clampf13(float value, float low, float high)
{
    return value < low ? low : value > high ? high : value;
}

static void set_error13(char *error, size_t size, const char *message)
{
    if (error != NULL && size > 0) snprintf(error, size, "%s", message);
}

static float peak13(const float *data, size_t frames)
{
    float peak = 0.0f;
    for (size_t i = 0; i < frames; ++i) {
        float value = fabsf(data[i]);
        if (value > peak) peak = value;
    }
    return peak;
}

static void match_peak13(float *data, size_t frames, float target)
{
    float peak = peak13(data, frames);
    if (peak <= 0.0000001f || target <= 0.0000001f) return;
    {
        float gain = target / peak;
        for (size_t i = 0; i < frames; ++i)
            data[i] = clampf13(data[i] * gain, -1.0f, 1.0f);
    }
}

int ts_pr13_apply_body_edge_drift(TsSample *sample, float body, float edge,
                                  float drift, char *error, size_t error_size)
{
    float *source;
    const size_t frames = sample != NULL ? sample->frames : 0;
    if (sample == NULL || sample->data == NULL || frames == 0) {
        set_error13(error, error_size, "No sample for Body/Edge/Drift processing");
        return 0;
    }
    source = (float *)malloc(frames * sizeof(float));
    if (source == NULL) {
        set_error13(error, error_size, "Out of memory while applying Body/Edge/Drift");
        return 0;
    }
    body = clampf13(body, 0.0f, 1.0f);
    edge = clampf13(edge, 0.0f, 1.0f);
    drift = clampf13(drift, 0.0f, 1.0f);

    if (body > 0.0001f) {
        float low = 0.0f;
        float envelope = 0.0f;
        float previous = sample->data[0];
        float sub_sign = 1.0f;
        float target;
        memcpy(source, sample->data, frames * sizeof(float));
        target = peak13(source, frames);
        for (size_t i = 0; i < frames; ++i) {
            const float x = source[i];
            const float absolute = fabsf(x);
            float compression;
            float sub;
            float wet;
            low += (x - low) * 0.018f;
            if (absolute > envelope) envelope += (absolute - envelope) * 0.22f;
            else envelope += (absolute - envelope) * 0.0035f;
            compression = envelope > 0.28f ?
                1.0f / (1.0f + (envelope - 0.28f) * (1.6f + body * 3.4f)) : 1.0f;
            if (previous <= 0.0f && x > 0.0f) sub_sign = -sub_sign;
            sub = fabsf(low) * sub_sign;
            wet = x * compression * 0.72f + low * 0.22f + sub * 0.28f +
                  x * x * x * 0.12f;
            sample->data[i] = x * (1.0f - body) + wet * body;
            previous = x;
        }
        match_peak13(sample->data, frames, target);
    }

    if (edge > 0.0001f) {
        float slow = 0.0f;
        float previous;
        float target;
        memcpy(source, sample->data, frames * sizeof(float));
        target = peak13(source, frames);
        previous = source[0];
        for (size_t i = 0; i < frames; ++i) {
            const float x = source[i];
            const float derivative = x - previous;
            float detail;
            float attack;
            slow += (x - slow) * 0.075f;
            detail = x - slow;
            attack = clampf13(fabsf(derivative) * 8.0f, 0.0f, 1.0f);
            sample->data[i] = x + edge *
                (detail * (0.72f + attack * 1.15f) + derivative * 0.62f);
            previous = x;
        }
        match_peak13(sample->data, frames, target);
    }

    {
        const float bipolar = (drift - 0.5f) * 2.0f;
        if (fabsf(bipolar) > 0.0001f && frames > 1) {
            long long shift = llround((double)bipolar * 0.95 * (double)frames);
            memcpy(source, sample->data, frames * sizeof(float));
            for (size_t i = 0; i < frames; ++i) {
                long long destination = (long long)i + shift;
                destination %= (long long)frames;
                if (destination < 0) destination += (long long)frames;
                sample->data[(size_t)destination] = source[i];
            }
        }
    }

    free(source);
    set_error13(error, error_size, "");
    return 1;
}
