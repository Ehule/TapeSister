#include "tapesister/sample.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TS_FM_GENOME_VERSION 3u

static float ratio_maximum(const TsFmPatch *patch)
{
    return patch != NULL && patch->extreme_mode ? 64.0f : 16.0f;
}

static float depth_maximum(const TsFmPatch *patch)
{
    return patch != NULL && patch->extreme_mode ? 48.0f : 12.0f;
}

static float feedback_maximum(const TsFmPatch *patch)
{
    return patch != NULL && patch->extreme_mode ? 0.99f : 0.82f;
}

static float transient_maximum(const TsFmPatch *patch)
{
    return patch != NULL && patch->extreme_mode ? 1.0f : 0.60f;
}

static float lfo_rate_maximum(const TsFmPatch *patch)
{
    return patch != NULL && patch->extreme_mode ? 1000.0f : 160.0f;
}

static float lfo_depth_maximum(const TsFmPatch *patch)
{
    return patch != NULL && patch->extreme_mode ? 2.0f : 1.0f;
}

static float resonance_maximum(const TsFmPatch *patch)
{
    return patch != NULL && patch->extreme_mode ? 0.995f : 0.95f;
}

static float filter_envelope_maximum(const TsFmPatch *patch)
{
    return patch != NULL && patch->extreme_mode ? 2.0f : 1.0f;
}

static float clampf(float value, float low, float high)
{
    return value < low ? low : value > high ? high : value;
}

static uint32_t rng_next(uint32_t *state)
{
    uint32_t x = *state == 0u ? 0x6d2b79f5u : *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static float rng_unit(uint32_t *state)
{
    return (float)(rng_next(state) & 0x00ffffffu) / 16777215.0f;
}

static float rng_bipolar(uint32_t *state)
{
    return rng_unit(state) * 2.0f - 1.0f;
}

static int categorical(float normalized, int count)
{
    int value = (int)floorf(clampf(normalized, 0.0f, 1.0f) * (float)count);
    return value >= count ? count - 1 : value;
}

static float categorical_normalized(int value, int count)
{
    if (value < 0) value = 0;
    if (value >= count) value = count - 1;
    return count > 1 ? (float)value / (float)(count - 1) : 0.0f;
}

static float log_normalized(float value, float low, float high)
{
    value = clampf(value, low, high);
    return logf(value / low) / logf(high / low);
}

static float log_value(float normalized, float low, float high)
{
    return low * powf(high / low, clampf(normalized, 0.0f, 1.0f));
}

static const float ratio_families[TS_FM_RATIO_FAMILY_COUNT][TS_FM_OPERATOR_COUNT] = {
    {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f},
    {1.0f, 1.5f, 2.25f, 3.0f, 4.5f, 6.75f},
    {1.0f, 0.5f, 0.333333f, 0.25f, 0.2f, 1.5f},
    {1.0f, 1.006f, 0.994f, 2.01f, 1.99f, 3.03f},
    {1.0f, 1.414214f, 2.718282f, 3.141593f, 4.236068f, 0.618034f},
    {1.0f, 2.5f, 1.333333f, 3.75f, 0.75f, 5.125f},
    {1.0f, 1.25f, 1.6f, 2.1f, 2.8f, 3.6f},
    {1.0f, 0.666667f, 1.2f, 1.8f, 2.4f, 4.8f}
};

const char *ts_fm_structure_name(int structure)
{
    static const char *names[TS_FM_STRUCTURE_COUNT] = {
        "CHAIN", "BRANCH", "TWIN", "PARALLEL", "STRIKE",
        "CLUSTER", "BRAID", "CASCADE", "MIRROR", "SWARM"
    };
    return structure >= 0 && structure < TS_FM_STRUCTURE_COUNT ?
           names[structure] : "UNKNOWN";
}

const char *ts_fm_ratio_family_name(int family)
{
    static const char *names[TS_FM_RATIO_FAMILY_COUNT] = {
        "HARMONIC", "FIFTHS", "SUBHARMONIC", "CLUSTERED",
        "METALLIC", "MIXED", "STRETCHED", "UNDERTONE"
    };
    return family >= 0 && family < TS_FM_RATIO_FAMILY_COUNT ?
           names[family] : "UNKNOWN";
}

const char *ts_fm_waveform_name(int waveform)
{
    static const char *names[TS_FM_WAVEFORM_COUNT] = {
        "SINE", "TRI", "SAW", "SQUARE", "PULSE",
        "RECT", "FOLD", "STEP", "DIGITAL", "NOISE"
    };
    return waveform >= 0 && waveform < TS_FM_WAVEFORM_COUNT ?
           names[waveform] : "UNKNOWN";
}

const char *ts_fm_lfo_type_name(int type)
{
    static const char *names[TS_FM_LFO_TYPE_COUNT] = {
        "OFF", "PITCH SIN", "PITCH TRI", "PITCH SQR", "PITCH RAMP",
        "AMP SIN", "AMP SQR", "INDEX SIN", "INDEX RAND",
        "FILTER SIN", "FILTER RAND", "STEP AMP"
    };
    return type >= 0 && type < TS_FM_LFO_TYPE_COUNT ? names[type] : "UNKNOWN";
}

const char *ts_fm_interaction_name(int interaction)
{
    static const char *names[TS_FM_INTERACTION_COUNT] = {
        "PHASE", "ADD", "RING", "MULT", "SUB", "FOLD", "DIGITAL", "CROSS"
    };
    return interaction >= 0 && interaction < TS_FM_INTERACTION_COUNT ?
           names[interaction] : "UNKNOWN";
}

const char *ts_fm_page_name(TsFmPage page)
{
    static const char *names[TS_FM_PAGE_COUNT] = {
        "PITCH", "WAVE", "LFO RATE", "LFO DEPTH", "LFO TYPE", "FILTER", "STRUCTURE"
    };
    return (int)page >= 0 && (int)page < TS_FM_PAGE_COUNT ? names[page] : "UNKNOWN";
}

void ts_fm_patch_sanitize(TsFmPatch *patch)
{
    int legacy;
    float ratio_high;
    float depth_high;
    float feedback_high;
    float transient_high;
    float lfo_rate_high;
    float lfo_depth_high;
    float resonance_high;
    float envelope_high;
    if (patch == NULL) return;
    legacy = patch->genome_version < 2u || patch->genome_version > TS_FM_GENOME_VERSION;
    if (patch->genome_version < 3u || patch->genome_version > TS_FM_GENOME_VERSION) {
        patch->drone_mode = 0;
        patch->extreme_mode = 0;
    }
    patch->drone_mode = patch->drone_mode != 0;
    patch->extreme_mode = patch->extreme_mode != 0;
    patch->genome_version = TS_FM_GENOME_VERSION;
    ratio_high = ratio_maximum(patch);
    depth_high = depth_maximum(patch);
    feedback_high = feedback_maximum(patch);
    transient_high = transient_maximum(patch);
    lfo_rate_high = lfo_rate_maximum(patch);
    lfo_depth_high = lfo_depth_maximum(patch);
    resonance_high = resonance_maximum(patch);
    envelope_high = filter_envelope_maximum(patch);
    patch->structure = patch->structure < 0 ? 0 :
                       patch->structure >= TS_FM_STRUCTURE_COUNT ?
                       TS_FM_STRUCTURE_COUNT - 1 : patch->structure;
    patch->ratio_family = patch->ratio_family < 0 ? 0 :
                          patch->ratio_family >= TS_FM_RATIO_FAMILY_COUNT ?
                          TS_FM_RATIO_FAMILY_COUNT - 1 : patch->ratio_family;
    patch->depth = clampf(isfinite(patch->depth) ? patch->depth : 1.0f,
                          0.15f, depth_high);
    patch->shape = clampf(isfinite(patch->shape) ? patch->shape : 0.5f, 0.0f, 1.0f);
    patch->feedback = clampf(isfinite(patch->feedback) ? patch->feedback : 0.0f,
                             0.0f, feedback_high);
    patch->transient_mix = clampf(isfinite(patch->transient_mix) ?
                                  patch->transient_mix : 0.0f, 0.0f, transient_high);
    if (legacy) {
        patch->active_mask = (1u << TS_FM_OPERATOR_COUNT) - 1u;
        patch->mutation_mask = TS_FM_MUTATE_ALL;
        patch->filter_mode = TS_FILTER_LOWPASS;
        patch->filter_cutoff_hz = 16000.0f;
        patch->filter_resonance = 0.08f;
        patch->filter_attack_seconds = 0.012f;
        patch->filter_release_seconds = 0.35f;
        patch->filter_envelope_amount = 0.0f;
        patch->interaction = TS_FM_INTERACTION_PHASE;
        patch->interaction_mix = 1.0f;
        for (int voice = 0; voice < TS_FM_OPERATOR_COUNT; ++voice) {
            patch->waveforms[voice] = TS_FM_WAVE_SINE;
            patch->lfo_rates[voice] = 0.10f + 0.07f * (float)voice;
            patch->lfo_depths[voice] = 0.0f;
            patch->lfo_types[voice] = TS_FM_LFO_OFF;
        }
    }
    patch->active_mask &= (1u << TS_FM_OPERATOR_COUNT) - 1u;
    if (patch->active_mask == 0u && legacy)
        patch->active_mask = (1u << TS_FM_OPERATOR_COUNT) - 1u;
    patch->mutation_mask &= TS_FM_MUTATE_ALL;
    for (int voice = 0; voice < TS_FM_OPERATOR_COUNT; ++voice) {
        patch->ratios[voice] = clampf(isfinite(patch->ratios[voice]) ?
                                      patch->ratios[voice] : 1.0f, 0.05f, ratio_high);
        if (patch->waveforms[voice] < 0 ||
            patch->waveforms[voice] >= TS_FM_WAVEFORM_COUNT)
            patch->waveforms[voice] = TS_FM_WAVE_SINE;
        patch->lfo_rates[voice] = clampf(isfinite(patch->lfo_rates[voice]) ?
                                         patch->lfo_rates[voice] : 0.1f,
                                         0.03f, lfo_rate_high);
        patch->lfo_depths[voice] = clampf(isfinite(patch->lfo_depths[voice]) ?
                                          patch->lfo_depths[voice] : 0.0f,
                                          0.0f, lfo_depth_high);
        if (patch->lfo_types[voice] < 0 ||
            patch->lfo_types[voice] >= TS_FM_LFO_TYPE_COUNT)
            patch->lfo_types[voice] = TS_FM_LFO_OFF;
    }
    if (patch->filter_mode < TS_FILTER_LOWPASS ||
        patch->filter_mode >= TS_FILTER_MODE_COUNT)
        patch->filter_mode = TS_FILTER_LOWPASS;
    patch->filter_cutoff_hz = clampf(isfinite(patch->filter_cutoff_hz) ?
                                     patch->filter_cutoff_hz : 16000.0f,
                                     20.0f, 20000.0f);
    patch->filter_resonance = clampf(isfinite(patch->filter_resonance) ?
                                     patch->filter_resonance : 0.08f,
                                     0.0f, resonance_high);
    patch->filter_attack_seconds = clampf(isfinite(patch->filter_attack_seconds) ?
                                           patch->filter_attack_seconds : 0.012f,
                                           0.001f, 4.0f);
    patch->filter_release_seconds = clampf(isfinite(patch->filter_release_seconds) ?
                                            patch->filter_release_seconds : 0.35f,
                                            0.01f, 8.0f);
    patch->filter_envelope_amount = clampf(isfinite(patch->filter_envelope_amount) ?
                                            patch->filter_envelope_amount : 0.0f,
                                            -envelope_high, envelope_high);
    if (patch->interaction < 0 || patch->interaction >= TS_FM_INTERACTION_COUNT)
        patch->interaction = TS_FM_INTERACTION_PHASE;
    patch->interaction_mix = clampf(isfinite(patch->interaction_mix) ?
                                     patch->interaction_mix : 1.0f, 0.0f, 1.0f);
}

void ts_fm_patch_from_recipe(const TsGeneratorRecipe *recipe, TsFmPatch *patch)
{
    uint32_t rng;
    if (patch == NULL) return;
    memset(patch, 0, sizeof(*patch));
    if (recipe == NULL) return;
    if (recipe->kind == TS_GENERATOR_FM && recipe->has_fm_patch) {
        *patch = recipe->fm_patch;
        ts_fm_patch_sanitize(patch);
        return;
    }
    rng = recipe->seed ^ 0x464d3655u;
    patch->genome_version = TS_FM_GENOME_VERSION;
    patch->structure = (int)(rng_next(&rng) % TS_FM_STRUCTURE_COUNT);
    patch->ratio_family = (int)(rng_next(&rng) % TS_FM_RATIO_FAMILY_COUNT);
    patch->depth = 0.8f + rng_unit(&rng) * 7.2f;
    patch->shape = rng_unit(&rng);
    patch->feedback = rng_unit(&rng) * 0.72f;
    patch->transient_mix = rng_unit(&rng) * 0.42f;
    patch->active_mask = (1u << (2 + (rng_next(&rng) % 5u))) - 1u;
    patch->mutation_mask = TS_FM_MUTATE_ALL;
    patch->filter_mode = (int)(rng_next(&rng) % TS_FILTER_MODE_COUNT);
    patch->filter_cutoff_hz = log_value(rng_unit(&rng), 180.0f, 19000.0f);
    patch->filter_resonance = rng_unit(&rng) * 0.72f;
    patch->filter_attack_seconds = log_value(rng_unit(&rng), 0.001f, 1.2f);
    patch->filter_release_seconds = log_value(rng_unit(&rng), 0.04f, 5.0f);
    patch->filter_envelope_amount = rng_bipolar(&rng) * 0.8f;
    patch->interaction = (int)(rng_next(&rng) % TS_FM_INTERACTION_COUNT);
    patch->interaction_mix = 0.35f + rng_unit(&rng) * 0.65f;
    for (int voice = 0; voice < TS_FM_OPERATOR_COUNT; ++voice) {
        float spread = 1.0f + rng_bipolar(&rng) *
                       (patch->ratio_family == 3 ? 0.004f : 0.018f);
        patch->ratios[voice] = ratio_families[patch->ratio_family][voice] * spread;
        patch->waveforms[voice] = (int)(rng_next(&rng) % TS_FM_WAVEFORM_COUNT);
        patch->lfo_rates[voice] = log_value(rng_unit(&rng), 0.03f, 80.0f);
        patch->lfo_depths[voice] = rng_unit(&rng) * rng_unit(&rng);
        patch->lfo_types[voice] = rng_unit(&rng) < 0.36f ? TS_FM_LFO_OFF :
                                  (int)(1u + rng_next(&rng) %
                                        (TS_FM_LFO_TYPE_COUNT - 1u));
    }
    ts_fm_patch_sanitize(patch);
}

static int nearby_category(int current, int count, float amount, uint32_t *rng)
{
    int radius = 1 + (int)floorf(amount * (float)(count - 1));
    int delta = (int)(rng_next(rng) % (uint32_t)(radius * 2 + 1)) - radius;
    int result = current + delta;
    while (result < 0) result += count;
    return result % count;
}

void ts_fm_patch_vary(const TsFmPatch *source, uint32_t seed, float range,
                      TsFmPatch *varied)
{
    uint32_t rng = seed ^ 0x56415259u;
    float amount = clampf(range, 0.0f, 1.0f);
    TsFmPatch base;
    float ratio_high;
    float depth_high;
    float feedback_high;
    float transient_high;
    float lfo_rate_high;
    float lfo_depth_high;
    float resonance_high;
    float envelope_high;
    if (varied == NULL) return;
    memset(varied, 0, sizeof(*varied));
    if (source == NULL) return;
    *varied = *source;
    if (amount <= 0.0f) return;
    base = *source;
    ts_fm_patch_sanitize(&base);
    *varied = base;
    ratio_high = ratio_maximum(&base);
    depth_high = depth_maximum(&base);
    feedback_high = feedback_maximum(&base);
    transient_high = transient_maximum(&base);
    lfo_rate_high = lfo_rate_maximum(&base);
    lfo_depth_high = lfo_depth_maximum(&base);
    resonance_high = resonance_maximum(&base);
    envelope_high = filter_envelope_maximum(&base);
    if ((base.mutation_mask & TS_FM_MUTATE_PITCH) != 0u) {
        for (int voice = 0; voice < TS_FM_OPERATOR_COUNT; ++voice) {
            float reach = base.extreme_mode ? 2.4f : 0.9f;
            float step = rng_bipolar(&rng) * amount *
                         (amount < 0.35f ? 0.12f : reach);
            varied->ratios[voice] = clampf(base.ratios[voice] * exp2f(step),
                                           0.05f, ratio_high);
        }
        if (amount > 0.66f && rng_unit(&rng) < amount) {
            varied->ratio_family = nearby_category(base.ratio_family,
                                                    TS_FM_RATIO_FAMILY_COUNT,
                                                    amount, &rng);
            for (int voice = 0; voice < TS_FM_OPERATOR_COUNT; ++voice)
                varied->ratios[voice] = ratio_families[varied->ratio_family][voice] *
                    (1.0f + rng_bipolar(&rng) * 0.025f * amount);
        }
    }
    if ((base.mutation_mask & TS_FM_MUTATE_WAVE) != 0u) {
        for (int voice = 0; voice < TS_FM_OPERATOR_COUNT; ++voice) {
            float chance = amount * amount * (0.18f + 0.10f * (float)voice);
            if (rng_unit(&rng) < chance)
                varied->waveforms[voice] = nearby_category(
                    base.waveforms[voice], TS_FM_WAVEFORM_COUNT,
                    amount < 0.55f ? 0.08f : amount, &rng);
        }
    }
    if ((base.mutation_mask & TS_FM_MUTATE_LFO) != 0u) {
        for (int voice = 0; voice < TS_FM_OPERATOR_COUNT; ++voice) {
            varied->lfo_rates[voice] = clampf(base.lfo_rates[voice] *
                exp2f(rng_bipolar(&rng) * amount *
                      (base.extreme_mode ? 5.5f : 3.0f)), 0.03f, lfo_rate_high);
            varied->lfo_depths[voice] = clampf(base.lfo_depths[voice] +
                rng_bipolar(&rng) * amount *
                (base.extreme_mode ? 1.25f : 0.48f), 0.0f, lfo_depth_high);
            if (amount > 0.42f && rng_unit(&rng) < amount * 0.45f)
                varied->lfo_types[voice] = nearby_category(
                    base.lfo_types[voice], TS_FM_LFO_TYPE_COUNT,
                    amount < 0.7f ? 0.08f : amount, &rng);
        }
    }
    if ((base.mutation_mask & TS_FM_MUTATE_FILTER) != 0u) {
        varied->filter_cutoff_hz = clampf(base.filter_cutoff_hz *
            exp2f(rng_bipolar(&rng) * amount * 4.0f), 20.0f, 20000.0f);
        varied->filter_resonance = clampf(base.filter_resonance +
            rng_bipolar(&rng) * amount *
            (base.extreme_mode ? 0.95f : 0.55f), 0.0f, resonance_high);
        varied->filter_attack_seconds = clampf(base.filter_attack_seconds *
            exp2f(rng_bipolar(&rng) * amount * 3.0f), 0.001f, 4.0f);
        varied->filter_release_seconds = clampf(base.filter_release_seconds *
            exp2f(rng_bipolar(&rng) * amount * 3.0f), 0.01f, 8.0f);
        varied->filter_envelope_amount = clampf(base.filter_envelope_amount +
            rng_bipolar(&rng) * amount *
            (base.extreme_mode ? 1.8f : 0.8f), -envelope_high, envelope_high);
        if (amount > 0.72f && rng_unit(&rng) < amount * 0.5f)
            varied->filter_mode = nearby_category(base.filter_mode,
                                                   TS_FILTER_MODE_COUNT,
                                                   amount, &rng);
    }
    if ((base.mutation_mask & TS_FM_MUTATE_STRUCTURE) != 0u) {
        varied->depth = clampf(base.depth * exp2f(rng_bipolar(&rng) * amount * 1.7f),
                               0.15f, depth_high);
        varied->shape = clampf(base.shape + rng_bipolar(&rng) * amount * 0.6f, 0.0f, 1.0f);
        varied->feedback = clampf(base.feedback + rng_bipolar(&rng) * amount * 0.5f,
                                  0.0f, feedback_high);
        varied->transient_mix = clampf(base.transient_mix +
            rng_bipolar(&rng) * amount *
            (base.extreme_mode ? 0.9f : 0.35f), 0.0f, transient_high);
        varied->interaction_mix = clampf(base.interaction_mix +
            rng_bipolar(&rng) * amount * 0.55f, 0.0f, 1.0f);
        if (amount > 0.62f && rng_unit(&rng) < amount * 0.62f)
            varied->interaction = nearby_category(base.interaction,
                                                   TS_FM_INTERACTION_COUNT,
                                                   amount, &rng);
        if (amount > 0.74f && rng_unit(&rng) < amount * 0.55f)
            varied->structure = nearby_category(base.structure,
                                                 TS_FM_STRUCTURE_COUNT,
                                                 amount, &rng);
        if (amount > 0.84f && rng_unit(&rng) < amount * 0.40f) {
            unsigned voice = rng_next(&rng) % TS_FM_OPERATOR_COUNT;
            varied->active_mask ^= 1u << voice;
            if (varied->active_mask == 0u) varied->active_mask = 1u << voice;
        }
    }
    ts_fm_patch_sanitize(varied);
}

float ts_fm_patch_distance(const TsFmPatch *source, const TsFmPatch *varied)
{
    TsFmPatch a;
    TsFmPatch b;
    float distance = 0.0f;
    if (source == NULL || varied == NULL) return INFINITY;
    a = *source;
    b = *varied;
    ts_fm_patch_sanitize(&a);
    ts_fm_patch_sanitize(&b);
    distance += fabsf(a.depth - b.depth) / 11.85f;
    distance += fabsf(a.shape - b.shape);
    distance += fabsf(a.feedback - b.feedback) / 0.82f;
    distance += fabsf(a.transient_mix - b.transient_mix) / 0.60f;
    distance += fabsf(a.interaction_mix - b.interaction_mix);
    distance += a.structure == b.structure ? 0.0f : 1.5f;
    distance += a.ratio_family == b.ratio_family ? 0.0f : 1.0f;
    distance += a.interaction == b.interaction ? 0.0f : 1.0f;
    distance += a.filter_mode == b.filter_mode ? 0.0f : 0.7f;
    distance += a.active_mask == b.active_mask ? 0.0f : 1.0f;
    distance += fabsf(log2f(b.filter_cutoff_hz / a.filter_cutoff_hz)) / 10.0f;
    distance += fabsf(a.filter_resonance - b.filter_resonance);
    for (int voice = 0; voice < TS_FM_OPERATOR_COUNT; ++voice) {
        distance += fabsf(log2f(b.ratios[voice] / a.ratios[voice])) / 4.0f;
        distance += a.waveforms[voice] == b.waveforms[voice] ? 0.0f : 0.35f;
        distance += fabsf(log2f(b.lfo_rates[voice] / a.lfo_rates[voice])) / 12.0f;
        distance += fabsf(a.lfo_depths[voice] - b.lfo_depths[voice]) * 0.2f;
        distance += a.lfo_types[voice] == b.lfo_types[voice] ? 0.0f : 0.25f;
    }
    return distance;
}

float ts_fm_control_normalized(const TsFmPatch *patch, TsFmPage page, int control)
{
    TsFmPatch safe;
    if (patch == NULL || control < 0 || control >= TS_FM_OPERATOR_COUNT) return 0.0f;
    safe = *patch;
    ts_fm_patch_sanitize(&safe);
    switch (page) {
    case TS_FM_PAGE_PITCH:
        return log_normalized(safe.ratios[control], 0.05f, ratio_maximum(&safe));
    case TS_FM_PAGE_WAVE: return categorical_normalized(safe.waveforms[control], TS_FM_WAVEFORM_COUNT);
    case TS_FM_PAGE_LFO_RATE:
        return log_normalized(safe.lfo_rates[control], 0.03f,
                              lfo_rate_maximum(&safe));
    case TS_FM_PAGE_LFO_DEPTH:
        return safe.lfo_depths[control] / lfo_depth_maximum(&safe);
    case TS_FM_PAGE_LFO_TYPE: return categorical_normalized(safe.lfo_types[control], TS_FM_LFO_TYPE_COUNT);
    case TS_FM_PAGE_FILTER:
        if (control == 0) return log_normalized(safe.filter_cutoff_hz, 20.0f, 20000.0f);
        if (control == 1) return safe.filter_resonance / resonance_maximum(&safe);
        if (control == 2) return log_normalized(safe.filter_attack_seconds, 0.001f, 4.0f);
        if (control == 3) return log_normalized(safe.filter_release_seconds, 0.01f, 8.0f);
        if (control == 4) {
            float maximum = filter_envelope_maximum(&safe);
            return (safe.filter_envelope_amount / maximum + 1.0f) * 0.5f;
        }
        return categorical_normalized(safe.filter_mode, TS_FILTER_MODE_COUNT);
    case TS_FM_PAGE_STRUCTURE:
        if (control == 0) return categorical_normalized(safe.structure, TS_FM_STRUCTURE_COUNT);
        if (control == 1) return categorical_normalized(safe.interaction, TS_FM_INTERACTION_COUNT);
        if (control == 2)
            return log_normalized(safe.depth, 0.15f, depth_maximum(&safe));
        if (control == 3) return safe.feedback / feedback_maximum(&safe);
        if (control == 4) return safe.interaction_mix;
        return safe.transient_mix / transient_maximum(&safe);
    default: return 0.0f;
    }
}

int ts_fm_set_control_normalized(TsFmPatch *patch, TsFmPage page, int control,
                                 float normalized)
{
    if (patch == NULL || (int)page < 0 || (int)page >= TS_FM_PAGE_COUNT ||
        control < 0 || control >= TS_FM_OPERATOR_COUNT || !isfinite(normalized)) return 0;
    ts_fm_patch_sanitize(patch);
    normalized = clampf(normalized, 0.0f, 1.0f);
    switch (page) {
    case TS_FM_PAGE_PITCH:
        patch->ratios[control] = log_value(normalized, 0.05f,
                                           ratio_maximum(patch));
        break;
    case TS_FM_PAGE_WAVE: patch->waveforms[control] = categorical(normalized, TS_FM_WAVEFORM_COUNT); break;
    case TS_FM_PAGE_LFO_RATE:
        patch->lfo_rates[control] = log_value(normalized, 0.03f,
                                              lfo_rate_maximum(patch));
        break;
    case TS_FM_PAGE_LFO_DEPTH:
        patch->lfo_depths[control] = normalized * lfo_depth_maximum(patch);
        break;
    case TS_FM_PAGE_LFO_TYPE: patch->lfo_types[control] = categorical(normalized, TS_FM_LFO_TYPE_COUNT); break;
    case TS_FM_PAGE_FILTER:
        if (control == 0) patch->filter_cutoff_hz = log_value(normalized, 20.0f, 20000.0f);
        else if (control == 1)
            patch->filter_resonance = normalized * resonance_maximum(patch);
        else if (control == 2) patch->filter_attack_seconds = log_value(normalized, 0.001f, 4.0f);
        else if (control == 3) patch->filter_release_seconds = log_value(normalized, 0.01f, 8.0f);
        else if (control == 4) {
            float maximum = filter_envelope_maximum(patch);
            patch->filter_envelope_amount = (normalized * 2.0f - 1.0f) * maximum;
        }
        else patch->filter_mode = categorical(normalized, TS_FILTER_MODE_COUNT);
        break;
    case TS_FM_PAGE_STRUCTURE:
        if (control == 0) patch->structure = categorical(normalized, TS_FM_STRUCTURE_COUNT);
        else if (control == 1) patch->interaction = categorical(normalized, TS_FM_INTERACTION_COUNT);
        else if (control == 2)
            patch->depth = log_value(normalized, 0.15f, depth_maximum(patch));
        else if (control == 3)
            patch->feedback = normalized * feedback_maximum(patch);
        else if (control == 4) patch->interaction_mix = normalized;
        else patch->transient_mix = normalized * transient_maximum(patch);
        break;
    default: return 0;
    }
    return 1;
}

int ts_fm_step_control(TsFmPatch *patch, TsFmPage page, int control,
                       int direction, int fine)
{
    int *category = NULL;
    int count = 0;
    float normalized;
    float amount;
    if (patch == NULL || direction == 0 || control < 0 ||
        control >= TS_FM_OPERATOR_COUNT) return 0;
    ts_fm_patch_sanitize(patch);
    if (page == TS_FM_PAGE_WAVE) {
        category = &patch->waveforms[control];
        count = TS_FM_WAVEFORM_COUNT;
    } else if (page == TS_FM_PAGE_LFO_TYPE) {
        category = &patch->lfo_types[control];
        count = TS_FM_LFO_TYPE_COUNT;
    } else if (page == TS_FM_PAGE_FILTER && control == 5) {
        category = &patch->filter_mode;
        count = TS_FILTER_MODE_COUNT;
    } else if (page == TS_FM_PAGE_STRUCTURE && control == 0) {
        category = &patch->structure;
        count = TS_FM_STRUCTURE_COUNT;
    } else if (page == TS_FM_PAGE_STRUCTURE && control == 1) {
        category = &patch->interaction;
        count = TS_FM_INTERACTION_COUNT;
    }
    if (category != NULL) {
        int next = *category + (direction > 0 ? 1 : -1);
        if (next < 0) next = 0;
        if (next >= count) next = count - 1;
        if (next == *category) return 0;
        *category = next;
        return 1;
    }
    normalized = ts_fm_control_normalized(patch, page, control);
    amount = fine ? 0.01f : 0.05f;
    return ts_fm_set_control_normalized(
        patch, page, control,
        normalized + (direction > 0 ? amount : -amount));
}

void ts_fm_control_format(const TsFmPatch *patch, TsFmPage page, int control,
                          char *label, size_t label_size,
                          char *value, size_t value_size)
{
    TsFmPatch safe;
    if (label != NULL && label_size > 0u) label[0] = '\0';
    if (value != NULL && value_size > 0u) value[0] = '\0';
    if (patch == NULL || control < 0 || control >= TS_FM_OPERATOR_COUNT) return;
    safe = *patch;
    ts_fm_patch_sanitize(&safe);
    if (page <= TS_FM_PAGE_LFO_TYPE) {
        if (label != NULL) snprintf(label, label_size, "VOICE %d", control + 1);
        if (value == NULL) return;
        if (page == TS_FM_PAGE_PITCH) snprintf(value, value_size, "X%.3F", safe.ratios[control]);
        else if (page == TS_FM_PAGE_WAVE) snprintf(value, value_size, "%s", ts_fm_waveform_name(safe.waveforms[control]));
        else if (page == TS_FM_PAGE_LFO_RATE) snprintf(value, value_size, safe.lfo_rates[control] < 10.0f ? "%.2FHZ" : "%.1FHZ", safe.lfo_rates[control]);
        else if (page == TS_FM_PAGE_LFO_DEPTH)
            snprintf(value, value_size, "%d%%",
                     (int)lrintf(safe.lfo_depths[control] * 100.0f));
        else snprintf(value, value_size, "%s", ts_fm_lfo_type_name(safe.lfo_types[control]));
        return;
    }
    if (page == TS_FM_PAGE_FILTER) {
        static const char *labels[6] = {"CUTOFF", "RES", "ATTACK", "RELEASE", "ENV", "MODE"};
        if (label != NULL) snprintf(label, label_size, "%s", labels[control]);
        if (value == NULL) return;
        if (control == 0) snprintf(value, value_size, safe.filter_cutoff_hz >= 1000.0f ? "%.1FK" : "%.0FHZ", safe.filter_cutoff_hz >= 1000.0f ? safe.filter_cutoff_hz / 1000.0f : safe.filter_cutoff_hz);
        else if (control == 1)
            snprintf(value, value_size, "%d%%",
                     (int)lrintf(safe.filter_resonance /
                                 resonance_maximum(&safe) * 100.0f));
        else if (control == 2) snprintf(value, value_size, "%.3FS", safe.filter_attack_seconds);
        else if (control == 3) snprintf(value, value_size, "%.2FS", safe.filter_release_seconds);
        else if (control == 4) snprintf(value, value_size, "%+.0F%%", safe.filter_envelope_amount * 100.0f);
        else snprintf(value, value_size, "%s", ts_filter_mode_name((TsFilterMode)safe.filter_mode));
        return;
    }
    {
        static const char *labels[6] = {"TOPOLOGY", "MATH", "DEPTH", "FEEDBACK", "MIX", "ATTACK"};
        if (label != NULL) snprintf(label, label_size, "%s", labels[control]);
        if (value == NULL) return;
        if (control == 0) snprintf(value, value_size, "%s", ts_fm_structure_name(safe.structure));
        else if (control == 1) snprintf(value, value_size, "%s", ts_fm_interaction_name(safe.interaction));
        else if (control == 2) snprintf(value, value_size, "X%.2F", safe.depth);
        else if (control == 3)
            snprintf(value, value_size, "%d%%",
                     (int)lrintf(safe.feedback /
                                 feedback_maximum(&safe) * 100.0f));
        else if (control == 4) snprintf(value, value_size, "%d%%", (int)lrintf(safe.interaction_mix * 100.0f));
        else snprintf(value, value_size, "%d%%",
                      (int)lrintf(safe.transient_mix /
                                  transient_maximum(&safe) * 100.0f));
    }
}

static float poly_blep(float phase, float increment)
{
    if (increment <= 0.0f || increment >= 1.0f) return 0.0f;
    if (phase < increment) {
        float t = phase / increment;
        return t + t - t * t - 1.0f;
    }
    if (phase > 1.0f - increment) {
        float t = (phase - 1.0f) / increment;
        return t * t + t + t + 1.0f;
    }
    return 0.0f;
}

static float wrap_phase(float phase)
{
    phase -= floorf(phase);
    return phase < 0.0f ? phase + 1.0f : phase;
}

static float oscillator(int waveform, float phase, float increment, float noise)
{
    float value;
    phase = wrap_phase(phase);
    switch (waveform) {
    case TS_FM_WAVE_TRIANGLE: return 1.0f - 4.0f * fabsf(phase - 0.5f);
    case TS_FM_WAVE_SAW: return phase * 2.0f - 1.0f - poly_blep(phase, increment);
    case TS_FM_WAVE_SQUARE:
        value = phase < 0.5f ? 1.0f : -1.0f;
        value += poly_blep(phase, increment);
        value -= poly_blep(wrap_phase(phase + 0.5f), increment);
        return value;
    case TS_FM_WAVE_PULSE:
        value = phase < 0.2f ? 1.0f : -1.0f;
        value += poly_blep(phase, increment);
        value -= poly_blep(wrap_phase(phase + 0.8f), increment);
        return value;
    case TS_FM_WAVE_RECTIFIED: return fabsf(sinf((float)(2.0 * M_PI) * phase)) * 2.0f - 1.0f;
    case TS_FM_WAVE_FOLDED:
        value = sinf((float)(2.0 * M_PI) * phase) * 2.4f;
        return fabsf(fmodf(value + 3.0f, 4.0f) - 2.0f) - 1.0f;
    case TS_FM_WAVE_STEPPED: return floorf((phase * 2.0f - 1.0f) * 6.0f) / 6.0f;
    case TS_FM_WAVE_DIGITAL:
        value = sinf((float)(2.0 * M_PI) * phase) + (phase * 2.0f - 1.0f) * 0.7f;
        return floorf(clampf(value, -1.0f, 1.0f) * 15.0f) / 15.0f;
    case TS_FM_WAVE_NOISE: return noise;
    case TS_FM_WAVE_SINE:
    default: return sinf((float)(2.0 * M_PI) * phase);
    }
}

static float triangle_lfo(float phase)
{
    return 1.0f - 4.0f * fabsf(wrap_phase(phase) - 0.5f);
}

static float interaction_sample(const TsFmPatch *patch, int voice,
                                float phase, float increment, float modulation,
                                float noise, float index_scale)
{
    float dry = oscillator(patch->waveforms[voice], phase, increment, noise);
    float wet;
    float amount = patch->depth * index_scale;
    switch (patch->interaction) {
    case TS_FM_INTERACTION_ADD: wet = tanhf(dry + modulation * amount * 0.28f); break;
    case TS_FM_INTERACTION_RING: wet = dry * modulation * (0.35f + amount * 0.18f); break;
    case TS_FM_INTERACTION_MULTIPLY: wet = dry * (0.55f + modulation * amount * 0.22f); break;
    case TS_FM_INTERACTION_SUBTRACT: wet = tanhf(dry - modulation * amount * 0.30f); break;
    case TS_FM_INTERACTION_FOLD: {
        float x = dry + modulation * amount * 0.35f;
        wet = fabsf(fmodf(x + 3.0f, 4.0f) - 2.0f) - 1.0f;
        break;
    }
    case TS_FM_INTERACTION_DIGITAL: {
        int a = (int)lrintf(clampf(dry, -1.0f, 1.0f) * 32767.0f);
        int b = (int)lrintf(clampf(modulation * amount * 0.2f, -1.0f, 1.0f) * 32767.0f);
        wet = (float)((a ^ b) & 0xffff) / 32767.5f - 1.0f;
        break;
    }
    case TS_FM_INTERACTION_CROSS:
        wet = tanhf(dry + modulation * amount * 0.22f) *
              (0.65f + fabsf(modulation) * 0.55f);
        break;
    case TS_FM_INTERACTION_PHASE:
    default:
        wet = oscillator(patch->waveforms[voice],
                         phase + modulation * amount * 0.15f,
                         increment, noise);
        break;
    }
    return dry * (1.0f - patch->interaction_mix) + wet * patch->interaction_mix;
}

static float topology_modulation(const TsFmPatch *patch, int voice,
                                 const float current[TS_FM_OPERATOR_COUNT],
                                 const float previous[TS_FM_OPERATOR_COUNT])
{
    switch (patch->structure) {
    case 0: return voice < 5 ? current[voice + 1] : previous[5] * patch->feedback;
    case 1:
        if (voice == 4 || voice == 3) return current[5];
        if (voice == 2) return current[3];
        if (voice == 1) return current[4];
        if (voice == 0) return current[1] + current[2];
        return previous[5] * patch->feedback;
    case 2:
        if (voice == 4) return current[5];
        if (voice == 2) return current[3];
        if (voice == 1) return current[2];
        if (voice == 0) return current[4];
        return voice == 5 ? previous[5] * patch->feedback : 0.0f;
    case 3: return voice <= 2 ? current[voice + 3] :
                              (voice == 5 ? previous[5] * patch->feedback : 0.0f);
    case 4:
        if (voice == 4) return current[5];
        if (voice == 3) return current[4];
        if (voice <= 2) return current[3] * (1.0f - (float)voice * 0.18f);
        return voice == 5 ? previous[5] * patch->feedback : 0.0f;
    case 5:
        if (voice == 2) return current[5] + current[4] * 0.3f;
        if (voice == 1) return current[4] + current[3] * 0.3f;
        if (voice == 0) return current[3] + current[5] * 0.3f;
        return voice >= 3 ? previous[(voice + 1) % 6] * patch->feedback : 0.0f;
    case 6: return current[(voice + 1) % 6] * 0.7f + previous[(voice + 5) % 6] * 0.5f;
    case 7: return voice < 4 ? current[voice + 1] + current[voice + 2] * 0.35f :
                              previous[voice] * patch->feedback;
    case 8: return current[5 - voice] + previous[voice] * patch->feedback * 0.5f;
    case 9: return (previous[(voice + 1) % 6] + previous[(voice + 3) % 6] -
                            previous[(voice + 5) % 6]) * 0.55f;
    default: return 0.0f;
    }
}

static int topology_carrier(int structure, int voice)
{
    if (structure == 0 || structure == 1 || structure == 6 || structure == 7)
        return voice == 0;
    if (structure == 2) return voice <= 1;
    if (structure == 8) return voice == 0 || voice == 5;
    return voice <= 2;
}

static int crosses_zero(float first, float second)
{
    return first == 0.0f || second == 0.0f ||
           (first < 0.0f && second > 0.0f) ||
           (first > 0.0f && second < 0.0f);
}

static void trim_drone_to_zero_boundaries(float *data, size_t *frame_count,
                                          uint32_t sample_rate)
{
    size_t frames;
    size_t window;
    size_t start = 0u;
    size_t end;
    if (data == NULL || frame_count == NULL || *frame_count < 4u) return;
    frames = *frame_count;
    window = sample_rate / 5u;
    if (window < 2u) window = 2u;
    if (window > frames / 4u) window = frames / 4u;
    for (size_t at = 1u; at < window; ++at) {
        if (crosses_zero(data[at - 1u], data[at])) {
            start = fabsf(data[at - 1u]) <= fabsf(data[at]) ? at - 1u : at;
            break;
        }
    }
    if (start > 0u) {
        memmove(data, data + start, (frames - start) * sizeof(*data));
        frames -= start;
    }
    data[0] = 0.0f;
    end = frames - 1u;
    {
        size_t minimum = frames > window ? frames - window : frames / 2u;
        int found = 0;
        for (size_t at = frames - 1u; at > minimum; --at) {
            if (crosses_zero(data[at - 1u], data[at])) {
                end = fabsf(data[at - 1u]) <= fabsf(data[at]) ? at - 1u : at;
                found = 1;
                break;
            }
        }
        if (!found) {
            float closest = fabsf(data[end]);
            for (size_t at = minimum; at < frames; ++at) {
                float magnitude = fabsf(data[at]);
                if (magnitude < closest) {
                    closest = magnitude;
                    end = at;
                }
            }
        }
    }
    if (end < 2u) end = frames - 1u;
    data[end] = 0.0f;
    *frame_count = end + 1u;
}

int ts_fm_render_sample(TsSample *sample, const TsFmPatch *patch,
                        float seconds, float frequency, uint32_t sample_rate,
                        uint32_t seed, char *error, size_t error_size)
{
    TsFmPatch safe;
    float *data;
    size_t frames;
    float phases[TS_FM_OPERATOR_COUNT] = {0};
    float previous[TS_FM_OPERATOR_COUNT] = {0};
    float lfo_phases[TS_FM_OPERATOR_COUNT] = {0};
    float lfo_random[TS_FM_OPERATOR_COUNT] = {0};
    uint32_t voice_rng[TS_FM_OPERATOR_COUNT];
    float low = 0.0f, band = 0.0f;
    float dc_x = 0.0f, dc_y = 0.0f;
    if (sample == NULL || patch == NULL || sample_rate == 0u ||
        !isfinite(seconds) || !isfinite(frequency)) {
        if (error != NULL && error_size > 0u) snprintf(error, error_size, "Invalid FM render request");
        return 0;
    }
    safe = *patch;
    ts_fm_patch_sanitize(&safe);
    seconds = clampf(seconds, 0.1f, 8.0f);
    frequency = clampf(frequency, 20.0f, 4000.0f);
    frames = (size_t)((double)seconds * (double)sample_rate);
    if (frames < 2u || frames > SIZE_MAX / sizeof(*data)) {
        if (error != NULL && error_size > 0u) snprintf(error, error_size, "FM render duration is too large");
        return 0;
    }
    data = (float *)malloc(frames * sizeof(*data));
    if (data == NULL) {
        if (error != NULL && error_size > 0u) snprintf(error, error_size, "Out of memory rendering FM source");
        return 0;
    }
    for (int voice = 0; voice < TS_FM_OPERATOR_COUNT; ++voice) {
        voice_rng[voice] = seed ^ (0x9e3779b9u * (uint32_t)(voice + 1));
        lfo_random[voice] = rng_bipolar(&voice_rng[voice]);
    }
    for (size_t frame = 0; frame < frames; ++frame) {
        float current[TS_FM_OPERATOR_COUNT] = {0};
        float carriers = 0.0f;
        float filter_lfo = 0.0f;
        float t = (float)frame / (float)sample_rate;
        float remaining = seconds - t;
        float amplitude_attack = safe.drone_mode ? 1.0f :
            fminf(1.0f, t * (18.0f + safe.shape * 760.0f));
        float amplitude_release = safe.drone_mode ? 1.0f :
            fminf(1.0f, remaining * (2.0f + safe.shape * 42.0f));
        float amplitude_envelope = safe.drone_mode ? 1.0f :
            amplitude_attack * amplitude_release *
            expf(-t * (0.08f + safe.shape * 2.8f));
        int carrier_count = 0;
        int first_active = -1;
        for (int voice = TS_FM_OPERATOR_COUNT - 1; voice >= 0; --voice) {
            float lfo_phase;
            float lfo = 0.0f;
            float pitch_scale = 1.0f;
            float amp_scale = 1.0f;
            float index_scale = 1.0f;
            float increment;
            float modulation;
            float noise;
            if ((safe.active_mask & (1u << voice)) == 0u) continue;
            first_active = voice;
            lfo_phases[voice] += safe.lfo_rates[voice] / (float)sample_rate;
            if (lfo_phases[voice] >= 1.0f) {
                lfo_phases[voice] -= floorf(lfo_phases[voice]);
                lfo_random[voice] = rng_bipolar(&voice_rng[voice]);
            }
            lfo_phase = lfo_phases[voice];
            if (safe.lfo_types[voice] == TS_FM_LFO_PITCH_TRIANGLE)
                lfo = triangle_lfo(lfo_phase);
            else if (safe.lfo_types[voice] == TS_FM_LFO_PITCH_SQUARE ||
                     safe.lfo_types[voice] == TS_FM_LFO_AMP_SQUARE)
                lfo = lfo_phase < 0.5f ? 1.0f : -1.0f;
            else if (safe.lfo_types[voice] == TS_FM_LFO_PITCH_RAMP)
                lfo = lfo_phase * 2.0f - 1.0f;
            else if (safe.lfo_types[voice] == TS_FM_LFO_INDEX_RANDOM ||
                     safe.lfo_types[voice] == TS_FM_LFO_FILTER_RANDOM ||
                     safe.lfo_types[voice] == TS_FM_LFO_STEP_AMP)
                lfo = lfo_random[voice];
            else lfo = sinf((float)(2.0 * M_PI) * lfo_phase);
            lfo *= safe.lfo_depths[voice];
            if (safe.lfo_types[voice] >= TS_FM_LFO_PITCH_SINE &&
                safe.lfo_types[voice] <= TS_FM_LFO_PITCH_RAMP)
                pitch_scale = exp2f(lfo * (safe.extreme_mode ? 4.0f : 2.0f));
            else if (safe.lfo_types[voice] == TS_FM_LFO_AMP_SINE ||
                     safe.lfo_types[voice] == TS_FM_LFO_AMP_SQUARE ||
                     safe.lfo_types[voice] == TS_FM_LFO_STEP_AMP)
                amp_scale = clampf(1.0f + lfo, 0.0f,
                                   safe.extreme_mode ? 3.0f : 2.0f);
            else if (safe.lfo_types[voice] == TS_FM_LFO_INDEX_SINE ||
                     safe.lfo_types[voice] == TS_FM_LFO_INDEX_RANDOM)
                index_scale = clampf(1.0f + lfo * 1.5f, 0.0f,
                                     safe.extreme_mode ? 5.0f : 2.5f);
            else if (safe.lfo_types[voice] == TS_FM_LFO_FILTER_SINE ||
                     safe.lfo_types[voice] == TS_FM_LFO_FILTER_RANDOM)
                filter_lfo += lfo;
            increment = clampf(frequency * safe.ratios[voice] * pitch_scale /
                               (float)sample_rate, 0.0f, 0.49f);
            phases[voice] = wrap_phase(phases[voice] + increment);
            modulation = topology_modulation(&safe, voice, current, previous);
            if (voice == 5) modulation += previous[5] * safe.feedback;
            noise = rng_bipolar(&voice_rng[voice]);
            current[voice] = interaction_sample(&safe, voice, phases[voice],
                                                increment, modulation, noise,
                                                index_scale) * amp_scale;
            if (!safe.drone_mode && !topology_carrier(safe.structure, voice))
                current[voice] *= expf(-t * (0.25f + (float)voice * 0.16f +
                                             safe.shape * 3.2f));
            if (topology_carrier(safe.structure, voice)) {
                carriers += current[voice];
                ++carrier_count;
            }
        }
        if (carrier_count == 0 && first_active >= 0) {
            carriers = current[first_active];
            carrier_count = 1;
        }
        for (int voice = 0; voice < TS_FM_OPERATOR_COUNT; ++voice)
            previous[voice] = current[voice];
        {
            float value = carrier_count > 0 ? carriers / sqrtf((float)carrier_count) : 0.0f;
            float noise = rng_bipolar(&voice_rng[0]);
            float filter_attack = safe.drone_mode ? 1.0f :
                fminf(1.0f, t / safe.filter_attack_seconds);
            float filter_release = safe.drone_mode ? 1.0f :
                fminf(1.0f, remaining / safe.filter_release_seconds);
            float filter_envelope = safe.drone_mode ? 1.0f :
                fminf(filter_attack, filter_release);
            float cutoff = safe.filter_cutoff_hz * exp2f(
                safe.filter_envelope_amount * filter_envelope *
                (safe.extreme_mode ? 9.0f : 6.0f) +
                filter_lfo * (safe.extreme_mode ? 3.0f : 1.5f));
            float coefficient;
            float high;
            float damping;
            value *= amplitude_envelope;
            if (!safe.drone_mode)
                value += noise * expf(-t * (22.0f + safe.shape * 90.0f)) *
                         safe.transient_mix;
            cutoff = clampf(cutoff, 20.0f, (float)sample_rate * 0.45f);
            coefficient = 2.0f * sinf((float)M_PI * cutoff / (float)sample_rate);
            coefficient = clampf(coefficient, 0.001f, 0.99f);
            damping = 1.95f - safe.filter_resonance * 1.75f;
            low += coefficient * band;
            low = isfinite(low) ? clampf(low, -16.0f, 16.0f) : 0.0f;
            high = value - low - damping * band;
            high = isfinite(high) ? clampf(high, -16.0f, 16.0f) : 0.0f;
            band += coefficient * high;
            band = isfinite(band) ? clampf(band, -16.0f, 16.0f) : 0.0f;
            if (safe.filter_mode == TS_FILTER_HIGHPASS) value = high;
            else if (safe.filter_mode == TS_FILTER_BANDPASS) value = band;
            else value = low;
            value = tanhf(value * (1.05f + safe.feedback * 1.8f)) * 0.82f;
            {
                float blocked = value - dc_x + dc_y * 0.995f;
                dc_x = value;
                dc_y = blocked;
                data[frame] = isfinite(blocked) ? clampf(blocked, -0.98f, 0.98f) : 0.0f;
            }
        }
    }
    if (safe.drone_mode)
        trim_drone_to_zero_boundaries(data, &frames, sample_rate);
    ts_sample_free(sample);
    sample->data = data;
    sample->frames = frames;
    sample->sample_rate = sample_rate;
    snprintf(sample->name, sizeof(sample->name), "FM %.8s %.8s %08X",
             ts_fm_structure_name(safe.structure),
             ts_fm_interaction_name(safe.interaction), seed);
    if (error != NULL && error_size > 0u) error[0] = '\0';
    return 1;
}
