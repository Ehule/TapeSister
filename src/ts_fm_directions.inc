/* Explicit musical directions share the existing oscillator/filter renderer.
   Legacy patches (directions == 0) keep their original signal path. */
const char *ts_fm_percussion_name(int percussion)
{
    static const char *names[] = {"KICK", "SNARE", "HAT", "CYMBAL",
                                  "TOM", "WOOD", "BELL", "DIGITAL"};
    return percussion >= 0 && percussion < TS_FM_PERC_COUNT ? names[percussion] : "PERC";
}

static void percussion_profile(TsFmPatch *p, uint32_t *rng)
{
    static const float decay[] = {.12f,.10f,.025f,.65f,.18f,.035f,.55f,.07f};
    static const float root[] = {.20f,.72f,5.0f,3.0f,.48f,1.7f,1.0f,.8f};
    int kind = (int)(rng_next(rng) % TS_FM_PERC_COUNT);
    p->percussion = kind;
    p->attack_seconds = .0004f + rng_unit(rng) * .002f;
    p->decay_seconds = decay[kind] * exp2f(rng_bipolar(rng) * .75f);
    p->pitch_sweep = kind == TS_FM_PERC_KICK ? 2.0f + rng_unit(rng) * 2.0f :
                     kind == TS_FM_PERC_TOM ? .5f + rng_unit(rng) : 0;
    p->pulse_rate = log_value(rng_unit(rng), .5f, 8.0f);
    p->structure = 3; /* Independent body and exciter carriers. */
    p->active_mask = 7u;
    p->depth = .3f + rng_unit(rng) * 2;
    p->feedback = p->interaction_mix = 0;
    p->filter_mode = TS_FM_FILTER_CLEAN;
    p->filter_envelope_amount = 0;
    p->shape = .7f;
    p->transient_mix = kind == TS_FM_PERC_SNARE ? .5f :
                       kind == TS_FM_PERC_HAT || kind == TS_FM_PERC_CYMBAL ? .3f : .05f;
    float center = root[kind] * exp2f(rng_bipolar(rng) * .25f);
    for (int v = 0; v < TS_FM_OPERATOR_COUNT; ++v) {
        p->ratios[v] = center * (v == 0 ? 1 : v == 1 ? 1.59f : 2.73f);
        p->waveforms[v] = TS_FM_WAVE_SINE;
        p->lfo_types[v] = TS_FM_LFO_OFF;
        p->lfo_depths[v] = 0;
        p->lfo_rates[v] = .1f;
    }
    if (kind == TS_FM_PERC_KICK || kind == TS_FM_PERC_TOM) p->active_mask = 1u;
    if (kind == TS_FM_PERC_SNARE) p->waveforms[2] = TS_FM_WAVE_NOISE;
    if (kind == TS_FM_PERC_HAT || kind == TS_FM_PERC_CYMBAL) {
        p->waveforms[0] = p->waveforms[1] = TS_FM_WAVE_SQUARE;
        p->waveforms[2] = TS_FM_WAVE_NOISE;
        p->filter_mode = TS_FILTER_HIGHPASS;
        p->filter_cutoff_hz = 3000 + rng_unit(rng) * 3500;
        p->filter_resonance = .1f;
    }
    if (kind == TS_FM_PERC_BELL || kind == TS_FM_PERC_DIGITAL) {
        p->active_mask = 63u;
        p->structure = 0;
        p->depth = 1.0f + rng_unit(rng) * 3.0f;
        if (kind == TS_FM_PERC_DIGITAL) {
            p->waveforms[1] = TS_FM_WAVE_DIGITAL;
            p->feedback = .3f;
        }
    }
}

static void melodic_profile(TsFmPatch *p, uint32_t *rng)
{
    p->structure = 1;
    p->active_mask = 63u;
    p->ratio_family = 0;
    p->pitch_lock = 1;
    p->pitch_sweep = 0;
    p->feedback = rng_unit(rng) * .1f;
    p->interaction = TS_FM_INTERACTION_PHASE;
    p->interaction_mix = 0;
    p->depth = .35f + rng_unit(rng) * 1.8f;
    p->transient_mix = (p->directions & TS_FM_DIRECTION_PERC) ? .025f : 0;
    p->filter_mode = TS_FILTER_LOWPASS;
    p->filter_cutoff_hz = log_value(rng_unit(rng), 1800, 14000);
    p->filter_resonance = .05f + rng_unit(rng) * .2f;
    p->filter_envelope_amount = 0;
    for (int v = 0; v < TS_FM_OPERATOR_COUNT; ++v) {
        p->ratios[v] = v == 0 ? 1.0f : (float)(1 + rng_next(rng) % 6u);
        p->waveforms[v] = v == 0 && !(p->directions & TS_FM_DIRECTION_PERC) ?
                          (int)(rng_next(rng) % 4u) : TS_FM_WAVE_SINE;
        p->lfo_types[v] = TS_FM_LFO_OFF;
        p->lfo_depths[v] = 0;
    }
    if (!(p->directions & TS_FM_DIRECTION_PERC)) {
        p->attack_seconds = log_value(rng_unit(rng), .003f, .2f);
        p->decay_seconds = log_value(rng_unit(rng), .25f, 2.5f);
    } else {
        /* Tuned strikes need a resonant body even if the roll began as a hat. */
        p->decay_seconds = fmaxf(p->decay_seconds, .15f + rng_unit(rng) * .4f);
    }
}

void ts_fm_patch_set_directions(TsFmPatch *p, uint32_t directions, uint32_t seed)
{
    uint32_t rng = seed ^ 0x44495245u;
    if (!p) return;
    ts_fm_patch_sanitize(p);
    if (!p->directions) p->sound_seed = seed;
    uint32_t added = (directions & TS_FM_DIRECTION_ALL) & ~p->directions;
    p->directions = directions & TS_FM_DIRECTION_ALL;
    p->drone_mode = (p->directions & TS_FM_DIRECTION_DRONE) != 0;
    if (added & TS_FM_DIRECTION_PERC) percussion_profile(p, &rng);
    if ((added & TS_FM_DIRECTION_MELODIC) ||
        ((added & TS_FM_DIRECTION_PERC) && (directions & TS_FM_DIRECTION_MELODIC)))
        melodic_profile(p, &rng);
    /* A newly added profile must retain an already selected experimental layer. */
    if ((directions & TS_FM_DIRECTION_EXPERIMENTAL) &&
        (added & (TS_FM_DIRECTION_PERC | TS_FM_DIRECTION_MELODIC | TS_FM_DIRECTION_EXPERIMENTAL))) {
        int melodic = (directions & TS_FM_DIRECTION_MELODIC) != 0;
        if (!melodic) {
            p->active_mask = 63u;
            p->depth = 3.0f + rng_unit(&rng) * 7;
            if (!(directions & TS_FM_DIRECTION_PERC)) p->structure = (int)(rng_next(&rng) % 10u);
            p->filter_mode = TS_FILTER_LOWPASS;
            p->filter_cutoff_hz = log_value(rng_unit(&rng), 1200, 16000);
        }
        p->interaction = melodic ? TS_FM_INTERACTION_PHASE :
                         TS_FM_INTERACTION_RING + (int)(rng_next(&rng) % 6u);
        p->interaction_mix = melodic ? 0 : .3f + rng_unit(&rng) * .6f;
        p->feedback = melodic ? .12f : .25f + rng_unit(&rng) * .5f;
        for (int v = melodic ? 1 : 0; v < TS_FM_OPERATOR_COUNT; ++v) {
            p->lfo_types[v] = v & 1 ? TS_FM_LFO_INDEX_RANDOM : TS_FM_LFO_FILTER_RANDOM;
            p->lfo_rates[v] = log_value(rng_unit(&rng), .2f, 30.0f);
            p->lfo_depths[v] = melodic ? .08f + rng_unit(&rng) * .2f : .2f + rng_unit(&rng) * .8f;
            if (!melodic) p->waveforms[v] = (int)(rng_next(&rng) % TS_FM_WAVEFORM_COUNT);
        }
    }
    ts_fm_patch_sanitize(p);
}

void ts_fm_patch_directed(TsFmPatch *p, uint32_t seed, uint32_t directions)
{
    ts_fm_patch_fresh(p, seed);
    if (!p || !(directions & TS_FM_DIRECTION_ALL)) return;
    p->attack_seconds = .005f;
    p->decay_seconds = .5f;
    p->pulse_rate = 2;
    ts_fm_patch_set_directions(p, directions, seed);
}

float ts_fm_patch_duration(const TsFmPatch *p, float seconds)
{
    if (p && p->directions && !p->drone_mode &&
        (p->directions & (TS_FM_DIRECTION_PERC | TS_FM_DIRECTION_MELODIC)))
        return fminf(seconds, fmaxf(.1f, p->attack_seconds + p->decay_seconds * 8));
    return seconds;
}

static void vary_directions(const TsFmPatch *base, TsFmPatch *p, float amount, uint32_t *rng)
{
    if (!base->directions) return;
    if (base->mutation_mask & (TS_FM_MUTATE_WAVE | TS_FM_MUTATE_STRUCTURE))
        p->sound_seed = rng_next(rng);
    if (base->mutation_mask & TS_FM_MUTATE_STRUCTURE) {
        p->attack_seconds *= exp2f(rng_bipolar(rng) * amount);
        p->decay_seconds *= exp2f(rng_bipolar(rng) * amount * 1.5f);
        p->pitch_sweep += rng_bipolar(rng) * amount * .4f;
        if (base->directions & TS_FM_DIRECTION_PERC)
            p->attack_seconds = fminf(p->attack_seconds, .006f);
    }
    if (base->mutation_mask & TS_FM_MUTATE_LFO)
        p->pulse_rate *= exp2f(rng_bipolar(rng) * amount);
    if (base->directions & TS_FM_DIRECTION_MELODIC) {
        p->pitch_sweep = 0;
        if (!base->pitch_lock && (base->mutation_mask & TS_FM_MUTATE_PITCH)) {
            p->ratios[0] = base->ratios[0];
            for (int v = 1; v < ts_fm_voice_count(p); ++v)
                p->ratios[v] = fmaxf(1, roundf(p->ratios[v] / p->ratios[0])) * p->ratios[0];
        }
        if (base->mutation_mask & TS_FM_MUTATE_WAVE)
            if (p->waveforms[0] > TS_FM_WAVE_PULSE) p->waveforms[0] = base->waveforms[0];
        if (base->mutation_mask & TS_FM_MUTATE_STRUCTURE) {
            p->active_mask |= base->active_mask & 1u;
            p->interaction = base->interaction;
            p->depth = fminf(p->depth, base->depth + 2);
            p->feedback = fminf(p->feedback, .25f);
        }
        if (base->mutation_mask & TS_FM_MUTATE_LFO)
            for (int v = 0; v < ts_fm_voice_count(p); ++v)
                if (p->lfo_types[v] >= TS_FM_LFO_PITCH_SINE && p->lfo_types[v] <= TS_FM_LFO_PITCH_RAMP)
                    p->lfo_depths[v] = fminf(p->lfo_depths[v], .002f);
    }
}
