#include "tapesister/sister_post_fx.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static float clampf(float value, float minimum, float maximum)
{
    if (!isfinite(value)) return minimum;
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static float approach(float current, float target, uint32_t rate, float ms)
{
    float coefficient;
    if (!isfinite(current)) current = target;
    if (rate == 0u || ms <= 0.0f) return target;
    coefficient = 1.0f - expf(-1.0f / (ms * 0.001f * (float)rate));
    return current + (target - current) * coefficient;
}

static TsStereoFrame lerp_frame(TsStereoFrame a, TsStereoFrame b, float t)
{
    TsStereoFrame result = {a.l + (b.l - a.l) * t,
                            a.r + (b.r - a.r) * t};
    return ts_stereo_frame_sanitize(result);
}

static TsStereoFrame effect_mix(TsStereoFrame dry, TsStereoFrame wet,
                                float amount)
{
    TsStereoFrame result;
    float dry_gain;
    float wet_gain;
    amount = clampf(amount, 0.0f, 1.0f);
    if (amount <= 0.0f) return ts_stereo_frame_sanitize(dry);
    if (amount >= 1.0f) return ts_stereo_frame_sanitize(wet);
    dry_gain = cosf(amount * (float)(M_PI * 0.5));
    wet_gain = sinf(amount * (float)(M_PI * 0.5));
    result.l = dry.l * dry_gain + wet.l * wet_gain;
    result.r = dry.r * dry_gain + wet.r * wet_gain;
    return ts_stereo_frame_sanitize(result);
}

/* Reverb is most often used as a surrounding field rather than an insert
   replacement. Keep more of the immediate instrument through the middle of
   the control while still reaching exact dry and exact wet at the ends. */
static TsStereoFrame reverb_effect_mix(TsStereoFrame dry, TsStereoFrame wet,
                                       float amount)
{
    TsStereoFrame result;
    float dry_gain;
    float wet_gain;
    amount = clampf(amount, 0.0f, 1.0f);
    if (amount <= 0.0f) return ts_stereo_frame_sanitize(dry);
    if (amount >= 1.0f) return ts_stereo_frame_sanitize(wet);
    dry_gain = cosf(amount * amount * (float)(M_PI * 0.5));
    wet_gain = sinf(amount * (float)(M_PI * 0.5));
    result.l = dry.l * dry_gain + wet.l * wet_gain;
    result.r = dry.r * dry_gain + wet.r * wet_gain;
    return ts_stereo_frame_sanitize(result);
}

static void ramp_start(TsSisterFxRamp *ramp, float target, uint32_t frames)
{
    if (ramp == NULL) return;
    if (frames == 0u) frames = 1u;
    ramp->target = clampf(target, 0.0f, 1.0f);
    ramp->step = (ramp->target - ramp->current) / (float)frames;
    ramp->remaining = frames;
    ramp->total = frames;
}

static void ramp_reset(TsSisterFxRamp *ramp, float value)
{
    if (ramp == NULL) return;
    ramp->current = ramp->target = clampf(value, 0.0f, 1.0f);
    ramp->step = 0.0f;
    ramp->remaining = 0u;
    ramp->total = 0u;
}

static void ramp_retime(TsSisterFxRamp *ramp, uint32_t frames)
{
    uint64_t scaled;
    uint32_t remaining;
    if (ramp == NULL || ramp->remaining == 0u || ramp->total == 0u) return;
    if (frames == 0u) frames = 1u;
    /* Preserve the completed fraction while changing the clock beneath the
       fade. A 25%-complete hour fade changed to one minute therefore has
       45 seconds left, rather than restarting or ignoring the wheel edit. */
    scaled = (uint64_t)frames * (uint64_t)ramp->remaining;
    remaining = (uint32_t)((scaled + ramp->total - 1u) / ramp->total);
    if (remaining == 0u) remaining = 1u;
    ramp->remaining = remaining;
    ramp->total = frames;
    ramp->step = (ramp->target - ramp->current) / (float)remaining;
}

static void ramp_advance(TsSisterFxRamp *ramp)
{
    if (ramp == NULL || ramp->remaining == 0u) return;
    ramp->current += ramp->step;
    if (--ramp->remaining == 0u) ramp->current = ramp->target;
    ramp->current = clampf(ramp->current, 0.0f, 1.0f);
}

static void read_handoff_begin(TsSisterFxReadHandoff *handoff,
                               uint32_t frames)
{
    if (handoff == NULL || !handoff->initialized) return;
    if (frames == 0u) frames = 1u;
    handoff->from = handoff->previous;
    handoff->total = frames;
    handoff->remaining = frames;
}

static TsStereoFrame read_handoff_apply(TsSisterFxReadHandoff *handoff,
                                        TsStereoFrame current)
{
    TsStereoFrame result = current;
    if (handoff == NULL) return result;
    if (handoff->remaining > 0u && handoff->total > 0u) {
        float amount = 1.0f - (float)handoff->remaining /
                                  (float)handoff->total;
        result = lerp_frame(handoff->from, current, amount);
        --handoff->remaining;
    }
    handoff->previous = result;
    handoff->initialized = 1;
    return result;
}

static float soft_clip(float value)
{
    if (!isfinite(value)) return 0.0f;
    value = value / (1.0f + fabsf(value));
    return fabsf(value) < 1.0e-20f ? 0.0f : value;
}

static float reverb_saturate(float value)
{
    if (!isfinite(value)) return 0.0f;
    value = clampf(value, -3.0f, 3.0f);
    value = tanhf(value);
    return fabsf(value) < 1.0e-20f ? 0.0f : value;
}

static float flush_tiny(float value)
{
    if (!isfinite(value) || fabsf(value) < 1.0e-20f) return 0.0f;
    return value;
}

static float feedback_condition(float value)
{
    float magnitude;
    float conditioned;
    if (!isfinite(value)) return 0.0f;
    magnitude = fabsf(value);
    if (magnitude <= 0.9f) return flush_tiny(value);
    conditioned = 0.9f + 0.1f * (magnitude - 0.9f) /
                              (0.1f + magnitude - 0.9f);
    return value < 0.0f ? -conditioned : conditioned;
}

const char *ts_sister_reverb_type_name(TsSisterReverbType type)
{
    switch (type) {
    case TS_SISTER_REVERB_PLATE: return "PLATE";
    case TS_SISTER_REVERB_SPRING: return "SPRING";
    case TS_SISTER_REVERB_CATHEDRAL: return "CATHEDRAL";
    default: return "HALL";
    }
}

float ts_sister_reverb_legacy_size(TsSisterReverbType type)
{
    switch (type) {
    case TS_SISTER_REVERB_PLATE: return 0.34f;
    case TS_SISTER_REVERB_SPRING: return 0.22f;
    case TS_SISTER_REVERB_CATHEDRAL: return 0.82f;
    default: return 0.52f;
    }
}

void ts_sister_fx_controls_default(TsSisterFxControls *controls)
{
    if (controls == NULL) return;
    memset(controls, 0, sizeof(*controls));
    controls->enabled = 1;
    controls->reverb_enabled = 1;
    controls->delay_enabled = 1;
    controls->distortion_enabled = 1;
    controls->transition = ts_sister_fx_transition_normalized(10.0f);
    controls->master_transition = controls->transition;
    controls->reverb_type = TS_SISTER_REVERB_HALL;
    controls->reverb_size = 0.58f;
    controls->reverb_decay = 0.42f;
    controls->reverb_targets = TS_SISTER_EFFECT_TARGET_MIX;
    controls->delay_time = 0.38f;
    controls->delay_feedback = 0.32f;
    controls->delay_targets = TS_SISTER_EFFECT_TARGET_MIX;
    controls->distortion_drive = 0.25f;
    controls->distortion_tone = 0.55f;
    controls->distortion_targets = TS_SISTER_EFFECT_TARGET_MIX;
    ts_sister_fallout_controls_default(&controls->fallout);
}

void ts_sister_fx_controls_sanitize(TsSisterFxControls *controls)
{
    if (controls == NULL) return;
    controls->enabled = controls->enabled != 0;
    controls->reverb_enabled = controls->reverb_enabled != 0;
    controls->delay_enabled = controls->delay_enabled != 0;
    controls->distortion_enabled = controls->distortion_enabled != 0;
    controls->transition = clampf(controls->transition, 0.0f, 1.0f);
    controls->master_transition = clampf(
        controls->master_transition, 0.0f, 1.0f);
    if (controls->reverb_type < TS_SISTER_REVERB_HALL ||
        controls->reverb_type >= TS_SISTER_REVERB_TYPE_COUNT)
        controls->reverb_type = TS_SISTER_REVERB_HALL;
    controls->reverb_size = clampf(controls->reverb_size, 0.0f, 1.0f);
    controls->reverb_mix = clampf(controls->reverb_mix, 0.0f, 1.0f);
    controls->reverb_decay = clampf(controls->reverb_decay, 0.0f, 1.0f);
    controls->reverb_targets = ts_sister_effect_targets_sanitize(
        controls->reverb_targets);
    controls->delay_time = clampf(controls->delay_time, 0.0f, 1.0f);
    controls->delay_feedback = clampf(controls->delay_feedback, 0.0f, 1.0f);
    controls->delay_mix = clampf(controls->delay_mix, 0.0f, 1.0f);
    controls->delay_targets = ts_sister_effect_targets_sanitize(
        controls->delay_targets);
    controls->distortion_drive = clampf(controls->distortion_drive, 0.0f, 1.0f);
    controls->distortion_tone = clampf(controls->distortion_tone, 0.0f, 1.0f);
    controls->distortion_mix = clampf(controls->distortion_mix, 0.0f, 1.0f);
    controls->distortion_targets = ts_sister_effect_targets_sanitize(
        controls->distortion_targets);
    controls->master_feedback = clampf(controls->master_feedback, 0.0f, 1.0f);
    ts_sister_fallout_controls_sanitize(&controls->fallout);
}

float ts_sister_fx_transition_ms(float normalized)
{
    normalized = clampf(normalized, 0.0f, 1.0f);
    return 10.0f * powf(360000.0f, normalized);
}

float ts_sister_fx_transition_normalized(float milliseconds)
{
    milliseconds = clampf(milliseconds, 10.0f, 3600000.0f);
    return logf(milliseconds / 10.0f) / logf(360000.0f);
}

float ts_sister_delay_time_ms(float normalized)
{
    /* 8 ms to 2000 ms, logarithmic. */
    normalized = clampf(normalized, 0.0f, 1.0f);
    return 8.0f * powf(250.0f, normalized);
}

float ts_sister_reverb_decay_seconds(float normalized)
{
    normalized = clampf(normalized, 0.0f, 1.0f);
    /* A single continuous space: close room through an hour-scale musical
       horizon without a discontinuity between named algorithms. */
    return 0.35f * powf(60.0f / 0.35f, normalized);
}

static float delay_read(const float *data, size_t capacity, size_t write,
                        float delay_frames, size_t channel)
{
    double position;
    size_t i0, i1;
    float fraction;
    if (data == NULL || capacity < 2u) return 0.0f;
    delay_frames = clampf(delay_frames, 1.0f, (float)(capacity - 1u));
    position = (double)write - (double)delay_frames;
    while (position < 0.0) position += (double)capacity;
    while (position >= (double)capacity) position -= (double)capacity;
    i0 = (size_t)floor(position);
    i1 = (i0 + 1u) % capacity;
    fraction = (float)(position - (double)i0);
    return data[i0 * 2u + channel] +
        (data[i1 * 2u + channel] - data[i0 * 2u + channel]) * fraction;
}

static float reverb_delay_ms(float size, size_t line)
{
    static const float base[TS_SISTER_REVERB_LINES] = {
        17.3f, 23.7f, 31.1f, 41.9f, 53.3f, 67.7f, 82.1f, 101.3f
    };
    float scale;
    if (line >= TS_SISTER_REVERB_LINES) line = 0u;
    size = clampf(size, 0.0f, 1.0f);
    scale = 0.55f + size * 1.20f;
    return base[line] * scale;
}

static int reverb_init(TsSisterReverbState *state, uint32_t rate)
{
    size_t line;
    memset(state, 0, sizeof(*state));
    state->sample_rate = rate;
    state->size_current = 0.58f;
    state->decay_current = 0.42f;
    for (line = 0u; line < TS_SISTER_REVERB_LINES; ++line) {
        size_t capacity = (size_t)ceil((reverb_delay_ms(
            1.0f, line) + 5.0f) * 0.001f * rate) + 2u;
        state->line[line].data = calloc(capacity * 2u, sizeof(float));
        if (state->line[line].data == NULL) return 0;
        state->line[line].capacity_frames = capacity;
        state->line[line].new_delay_frames =
            reverb_delay_ms(state->size_current, line) * 0.001f * rate;
        state->line[line].old_delay_frames = state->line[line].new_delay_frames;
        {
            float phase = (float)line * 0.731f;
            float step = (float)(2.0 * M_PI) *
                (0.037f + 0.011f * (float)line) / rate;
            state->modulation_sin[line] = sinf(phase);
            state->modulation_cos[line] = cosf(phase);
            state->modulation_step_sin[line] = sinf(step);
            state->modulation_step_cos[line] = cosf(step);
        }
    }
    return 1;
}

static void reverb_free(TsSisterReverbState *state)
{
    if (state == NULL) return;
    for (size_t line = 0u; line < TS_SISTER_REVERB_LINES; ++line)
        free(state->line[line].data);
    memset(state, 0, sizeof(*state));
}

static int delay_init_state(TsSisterDelayState *state, uint32_t rate)
{
    size_t capacity = (size_t)ceil(2.01 * (double)rate) + 2u;
    memset(state, 0, sizeof(*state));
    state->data = calloc(capacity * 2u, sizeof(float));
    if (state->data == NULL) return 0;
    state->capacity_frames = capacity;
    state->delay_current = ts_sister_delay_time_ms(0.38f) * 0.001f * rate;
    state->delay_old = state->delay_current;
    state->delay_target = state->delay_current;
    state->feedback_current = 0.32f;
    return 1;
}

void ts_sister_post_fx_free(TsSisterPostFxEngine *engine)
{
    if (engine == NULL) return;
    for (size_t i = 0u; i < TS_SISTER_EFFECT_PROCESSOR_COUNT; ++i) {
        reverb_free(&engine->reverb[i]);
        free(engine->delay[i].data);
    }
    memset(engine, 0, sizeof(*engine));
}

int ts_sister_post_fx_init(TsSisterPostFxEngine *engine,
                           uint32_t sample_rate)
{
    TsSisterPostFxEngine next;
    if (engine == NULL || sample_rate < 1000u || sample_rate > 384000u)
        return 0;
    memset(&next, 0, sizeof(next));
    next.sample_rate = sample_rate;
    ts_sister_fx_controls_default(&next.controls);
    next.reverb_target.active_mask = next.controls.reverb_targets;
    next.reverb_target.pending_mask = next.controls.reverb_targets;
    next.delay_target.active_mask = next.controls.delay_targets;
    next.delay_target.pending_mask = next.controls.delay_targets;
    next.distortion_target.active_mask = next.controls.distortion_targets;
    next.distortion_target.pending_mask = next.controls.distortion_targets;
    next.master_engage.current = next.master_engage.target = 1.0f;
    next.reverb_engage.current = next.reverb_engage.target = 1.0f;
    next.delay_engage.current = next.delay_engage.target = 1.0f;
    next.distortion_engage.current = next.distortion_engage.target = 1.0f;
    for (size_t i = 0u; i < TS_SISTER_EFFECT_PROCESSOR_COUNT; ++i) {
        if (!reverb_init(&next.reverb[i], sample_rate) ||
            !delay_init_state(&next.delay[i], sample_rate)) {
            ts_sister_post_fx_free(&next);
            return 0;
        }
        next.distortion[i].drive_current = next.controls.distortion_drive;
        next.distortion[i].tone_current = next.controls.distortion_tone;
    }
    next.ready = 1;
    ts_sister_post_fx_free(engine);
    *engine = next;
    return 1;
}

static void target_state_set(TsSisterFxTargetState *state, uint8_t mask,
                             uint32_t sample_rate)
{
    int active_mix;
    int next_mix;
    mask = ts_sister_effect_targets_sanitize(mask);
    if (state == NULL || mask == state->pending_mask) return;
    if (state->handoff_remaining > 0u) {
        state->pending_mask = mask;
        return;
    }
    active_mix = (state->active_mask & TS_SISTER_EFFECT_TARGET_MIX) != 0u;
    next_mix = (mask & TS_SISTER_EFFECT_TARGET_MIX) != 0u;
    state->pending_mask = mask;
    if (state->active_mask != 0u && mask != 0u && active_mix != next_mix) {
        /* Exclusive ownership handoff: old returns fade completely before the
           new group can begin, so a head can never be processed again at MIX. */
        state->active_mask = 0u;
        state->handoff_remaining = (uint32_t)(0.014f * sample_rate);
        if (state->handoff_remaining == 0u) state->handoff_remaining = 1u;
    } else {
        state->active_mask = mask;
        state->handoff_remaining = 0u;
    }
}

static void target_state_advance(TsSisterPostFxEngine *engine,
                                 TsSisterFxTargetState *state, int effect)
{
    if (engine == NULL || state == NULL || state->handoff_remaining == 0u)
        return;
    --state->handoff_remaining;
    if (state->handoff_remaining != 0u) return;
    /* The old branch has spent the entire handoff interval moving toward dry.
       Make its now-inaudible route exact before the new branch ramps in. */
    for (size_t i = 0u; i < TS_SISTER_EFFECT_PROCESSOR_COUNT; ++i) {
        if (effect == 0) engine->distortion[i].route_current = 0.0f;
        else if (effect == 1) engine->delay[i].route_current = 0.0f;
        else engine->reverb[i].route_current = 0.0f;
    }
    state->active_mask = state->pending_mask;
}

int ts_sister_post_fx_reconfigure(TsSisterPostFxEngine *engine,
                                  uint32_t sample_rate)
{
    TsSisterFxControls controls;
    TsSisterPostFxEngine next;
    if (engine == NULL) return 0;
    controls = engine->controls;
    memset(&next, 0, sizeof(next));
    if (!ts_sister_post_fx_init(&next, sample_rate)) return 0;
    ts_sister_post_fx_sync_controls(&next, &controls);
    ts_sister_post_fx_free(engine);
    *engine = next;
    return 1;
}

void ts_sister_post_fx_set_controls(TsSisterPostFxEngine *engine,
                                    const TsSisterFxControls *controls)
{
    TsSisterFxControls next;
    if (engine == NULL || controls == NULL) return;
    next = *controls;
    ts_sister_fx_controls_sanitize(&next);
    {
        uint32_t effect_frames = (uint32_t)fmaxf(1.0f,
            ts_sister_fx_transition_ms(next.transition) *
            (float)engine->sample_rate / 1000.0f);
        uint32_t master_frames = (uint32_t)fmaxf(1.0f,
            ts_sister_fx_transition_ms(next.master_transition) *
            (float)engine->sample_rate / 1000.0f);
        if (next.transition != engine->controls.transition) {
            ramp_retime(&engine->reverb_engage, effect_frames);
            ramp_retime(&engine->delay_engage, effect_frames);
            ramp_retime(&engine->distortion_engage, effect_frames);
        }
        if (next.master_transition != engine->controls.master_transition)
            ramp_retime(&engine->master_engage, master_frames);
        if (next.enabled != engine->controls.enabled)
            ramp_start(&engine->master_engage, next.enabled ? 1.0f : 0.0f,
                       master_frames);
        if (next.reverb_enabled != engine->controls.reverb_enabled)
            ramp_start(&engine->reverb_engage,
                       next.reverb_enabled ? 1.0f : 0.0f, effect_frames);
        if (next.delay_enabled != engine->controls.delay_enabled)
            ramp_start(&engine->delay_engage,
                       next.delay_enabled ? 1.0f : 0.0f, effect_frames);
        if (next.distortion_enabled != engine->controls.distortion_enabled)
            ramp_start(&engine->distortion_engage,
                       next.distortion_enabled ? 1.0f : 0.0f, effect_frames);
    }
    target_state_set(&engine->reverb_target, next.reverb_targets,
                     engine->sample_rate);
    target_state_set(&engine->delay_target, next.delay_targets,
                     engine->sample_rate);
    target_state_set(&engine->distortion_target, next.distortion_targets,
                     engine->sample_rate);
    if (engine->ready && next.reverb_size != engine->controls.reverb_size) {
        for (size_t i = 0u; i < TS_SISTER_EFFECT_PROCESSOR_COUNT; ++i) {
            TsSisterReverbState *state = &engine->reverb[i];
            int interrupted = 0;
            for (size_t line = 0u; line < TS_SISTER_REVERB_LINES; ++line)
                interrupted |= state->line[line].transition_remaining > 0u;
            if (interrupted)
                read_handoff_begin(&state->read_handoff,
                    (uint32_t)(0.060f * engine->sample_rate));
            for (size_t line = 0u; line < TS_SISTER_REVERB_LINES; ++line) {
                state->line[line].old_delay_frames =
                    state->line[line].new_delay_frames;
                state->line[line].new_delay_frames = reverb_delay_ms(
                    next.reverb_size, line) * 0.001f * engine->sample_rate;
                state->line[line].transition_total =
                    (uint32_t)(0.060f * engine->sample_rate);
                state->line[line].transition_remaining =
                    state->line[line].transition_total;
            }
        }
    }
    engine->controls = next;
}

void ts_sister_post_fx_sync_controls(TsSisterPostFxEngine *engine,
                                     const TsSisterFxControls *controls)
{
    TsSisterFxControls next;
    if (engine == NULL || controls == NULL) return;
    next = *controls;
    ts_sister_fx_controls_sanitize(&next);
    ts_sister_post_fx_set_controls(engine, &next);
    ramp_reset(&engine->master_engage, next.enabled ? 1.0f : 0.0f);
    ramp_reset(&engine->reverb_engage, next.reverb_enabled ? 1.0f : 0.0f);
    ramp_reset(&engine->delay_engage, next.delay_enabled ? 1.0f : 0.0f);
    ramp_reset(&engine->distortion_engage,
               next.distortion_enabled ? 1.0f : 0.0f);
    for (size_t i = 0u; i < TS_SISTER_EFFECT_PROCESSOR_COUNT; ++i) {
        engine->reverb[i].size_current = next.reverb_size;
        for (size_t line = 0u; line < TS_SISTER_REVERB_LINES; ++line) {
            float frames = reverb_delay_ms(next.reverb_size, line) * 0.001f *
                           engine->sample_rate;
            engine->reverb[i].line[line].old_delay_frames = frames;
            engine->reverb[i].line[line].new_delay_frames = frames;
            engine->reverb[i].line[line].transition_remaining = 0u;
            engine->reverb[i].line[line].transition_total = 0u;
        }
    }
}

static TsStereoFrame distortion_process(TsSisterPostFxEngine *engine,
                                        size_t index, TsStereoFrame input,
                                        int enabled)
{
    TsSisterDistortionState *state = &engine->distortion[index];
    TsSisterFxControls *c = &engine->controls;
    TsStereoFrame wet = {0.0f, 0.0f};
    float values[2] = {input.l, input.r};
    float *outputs[2] = {&wet.l, &wet.r};
    state->drive_current = approach(state->drive_current,
        c->distortion_drive, engine->sample_rate, 20.0f);
    state->tone_current = approach(state->tone_current,
        c->distortion_tone, engine->sample_rate, 20.0f);
    state->mix_current = approach(state->mix_current,
        c->distortion_mix, engine->sample_rate, 20.0f);
    state->route_current = approach(state->route_current,
        enabled ? 1.0f : 0.0f, engine->sample_rate, 12.0f);
    if (state->mix_current <= FLT_EPSILON && c->distortion_mix <= 0.0f)
        return input;
    for (size_t channel = 0u; channel < 2u; ++channel) {
        float drive = powf(60.0f, state->drive_current);
        float midpoint = 0.5f * (state->previous_input[channel] + values[channel]);
        float a = tanhf(midpoint * drive + 0.08f * midpoint * midpoint * drive);
        float b = tanhf(values[channel] * drive + 0.08f * values[channel] *
                        values[channel] * drive);
        float shaped = 0.5f * (a + b);
        float cutoff = 700.0f * powf(22.0f, state->tone_current);
        float coefficient = 1.0f - expf((float)(-2.0 * M_PI) * cutoff /
                                         engine->sample_rate);
        float dc;
        state->previous_input[channel] = values[channel];
        state->tone_state[channel] +=
            (shaped - state->tone_state[channel]) * coefficient;
        dc = state->tone_state[channel] - state->dc_x1[channel] +
             0.995f * state->dc_y1[channel];
        state->dc_x1[channel] = state->tone_state[channel];
        state->dc_y1[channel] = dc;
        *outputs[channel] = soft_clip(dc * (1.35f - 0.35f * state->drive_current));
    }
    wet = ts_stereo_frame_sanitize(wet);
    return effect_mix(input, wet,
        clampf(state->mix_current * state->route_current *
               engine->distortion_engage.current, 0.0f, 1.0f));
}

static TsStereoFrame delay_process(TsSisterPostFxEngine *engine, size_t index,
                                   TsStereoFrame input, int enabled)
{
    TsSisterDelayState *state = &engine->delay[index];
    TsSisterFxControls *c = &engine->controls;
    TsStereoFrame wet;
    float requested = ts_sister_delay_time_ms(c->delay_time) *
                      0.001f * engine->sample_rate;
    if (fabsf(requested - state->delay_target) > 0.5f) {
        if (state->transition_remaining > 0u)
            read_handoff_begin(&state->read_handoff,
                (uint32_t)(0.025f * engine->sample_rate));
        state->delay_old = state->delay_current;
        state->delay_target = requested;
        state->transition_total = (uint32_t)(0.025f * engine->sample_rate);
        if (state->transition_total == 0u) state->transition_total = 1u;
        state->transition_remaining = state->transition_total;
    }
    state->feedback_current = approach(state->feedback_current,
        c->delay_feedback, engine->sample_rate, 20.0f);
    state->mix_current = approach(state->mix_current,
        c->delay_mix, engine->sample_rate, 20.0f);
    state->route_current = approach(state->route_current,
        enabled ? 1.0f : 0.0f, engine->sample_rate, 12.0f);
    if (!state->has_history &&
        ((state->mix_current <= FLT_EPSILON && c->delay_mix <= 0.0f) ||
         (!enabled && state->route_current <= FLT_EPSILON)))
        return input;
    if (state->route_current * engine->delay_engage.current *
        fmaxf(fabsf(input.l), fabsf(input.r)) > 1.0e-12f)
        state->has_history = 1;
    wet.l = delay_read(state->data, state->capacity_frames,
                       state->write_index, state->delay_current, 0u);
    wet.r = delay_read(state->data, state->capacity_frames,
                       state->write_index, state->delay_current, 1u);
    if (state->transition_remaining > 0u) {
        float amount = 1.0f - (float)state->transition_remaining /
                                  (float)state->transition_total;
        TsStereoFrame next = {
            delay_read(state->data, state->capacity_frames,
                       state->write_index, state->delay_target, 0u),
            delay_read(state->data, state->capacity_frames,
                       state->write_index, state->delay_target, 1u)
        };
        wet = lerp_frame(wet, next, amount);
        --state->transition_remaining;
        if (state->transition_remaining == 0u)
            state->delay_current = state->delay_target;
    }
    wet = read_handoff_apply(&state->read_handoff, wet);
    state->data[state->write_index * 2u] = feedback_condition(
        input.l * state->route_current * engine->delay_engage.current +
        wet.l * (state->feedback_current * 1.08f));
    state->data[state->write_index * 2u + 1u] = feedback_condition(
        input.r * state->route_current * engine->delay_engage.current +
        wet.r * (state->feedback_current * 1.08f));
    state->write_index = (state->write_index + 1u) % state->capacity_frames;
    return effect_mix(input, wet,
        clampf(state->mix_current * state->route_current *
               engine->delay_engage.current, 0.0f, 1.0f));
}

static TsStereoFrame reverb_process(TsSisterPostFxEngine *engine, size_t index,
                                    TsStereoFrame input, int enabled)
{
    TsSisterReverbState *state = &engine->reverb[index];
    TsSisterFxControls *c = &engine->controls;
    float read_l[TS_SISTER_REVERB_LINES];
    float read_r[TS_SISTER_REVERB_LINES];
    float mean_l = 0.0f, mean_r = 0.0f;
    float decay_seconds;
    float damping;
    TsStereoFrame wet = {0.0f, 0.0f};
    static const float output_l[TS_SISTER_REVERB_LINES] = {
         1.0f, -1.0f,  1.0f,  1.0f, -1.0f,  1.0f, -1.0f, -1.0f
    };
    static const float output_r[TS_SISTER_REVERB_LINES] = {
        -1.0f,  1.0f,  1.0f, -1.0f,  1.0f,  1.0f, -1.0f, -1.0f
    };
    state->mix_current = approach(state->mix_current,
        c->reverb_mix, engine->sample_rate, 24.0f);
    state->decay_current = approach(state->decay_current,
        c->reverb_decay, engine->sample_rate, 35.0f);
    state->size_current = approach(state->size_current,
        c->reverb_size, engine->sample_rate, 55.0f);
    state->route_current = approach(state->route_current,
        enabled ? 1.0f : 0.0f, engine->sample_rate, 18.0f);
    if (!state->has_history &&
        ((state->mix_current <= FLT_EPSILON && c->reverb_mix <= 0.0f) ||
         (!enabled && state->route_current <= FLT_EPSILON)))
        return input;
    if (state->route_current * engine->reverb_engage.current *
        fmaxf(fabsf(input.l), fabsf(input.r)) > 1.0e-12f)
        state->has_history = 1;
    decay_seconds = ts_sister_reverb_decay_seconds(state->decay_current);
    damping = 0.90f + (1.0f - state->size_current) * 0.05f +
              (1.0f - state->decay_current) * 0.03f;
    for (size_t line = 0u; line < TS_SISTER_REVERB_LINES; ++line) {
        TsSisterReverbLine *delay = &state->line[line];
        float depth_frames = (0.08f + state->size_current * 0.48f) *
                             0.001f * engine->sample_rate;
        float oscillator_sin = state->modulation_sin[line];
        float oscillator_cos = state->modulation_cos[line];
        float mod_l = oscillator_sin * depth_frames;
        float mod_r = (-0.159519f * oscillator_sin +
                        0.987195f * oscillator_cos) * depth_frames;
        read_l[line] = delay_read(delay->data, delay->capacity_frames,
                                  delay->write_index,
                                  delay->new_delay_frames + mod_l, 0u);
        read_r[line] = delay_read(delay->data, delay->capacity_frames,
                                  delay->write_index,
                                  delay->new_delay_frames + mod_r, 1u);
        if (delay->transition_remaining > 0u) {
            float amount = 1.0f - (float)delay->transition_remaining /
                                      (float)delay->transition_total;
            read_l[line] = read_l[line] * amount +
                delay_read(delay->data, delay->capacity_frames,
                           delay->write_index,
                           delay->old_delay_frames + mod_l, 0u) *
                (1.0f - amount);
            read_r[line] = read_r[line] * amount +
                delay_read(delay->data, delay->capacity_frames,
                           delay->write_index,
                           delay->old_delay_frames + mod_r, 1u) *
                (1.0f - amount);
            --delay->transition_remaining;
        }
        mean_l += read_l[line] * (1.0f / (float)TS_SISTER_REVERB_LINES);
        mean_r += read_r[line] * (1.0f / (float)TS_SISTER_REVERB_LINES);
        wet.l += (read_l[line] * output_l[line] +
                  read_r[line] * output_r[line] * 0.16f) * 0.35355339f;
        wet.r += (read_r[line] * output_r[line] -
                  read_l[line] * output_l[line] * 0.16f) * 0.35355339f;
        state->modulation_sin[line] =
            oscillator_sin * state->modulation_step_cos[line] +
            oscillator_cos * state->modulation_step_sin[line];
        state->modulation_cos[line] =
            oscillator_cos * state->modulation_step_cos[line] -
            oscillator_sin * state->modulation_step_sin[line];
    }
    if (++state->modulation_renormalize >= 4096u) {
        state->modulation_renormalize = 0u;
        for (size_t line = 0u; line < TS_SISTER_REVERB_LINES; ++line) {
            float magnitude = sqrtf(
                state->modulation_sin[line] * state->modulation_sin[line] +
                state->modulation_cos[line] * state->modulation_cos[line]);
            if (magnitude > 0.0f && isfinite(magnitude)) {
                state->modulation_sin[line] /= magnitude;
                state->modulation_cos[line] /= magnitude;
            } else {
                state->modulation_sin[line] = 0.0f;
                state->modulation_cos[line] = 1.0f;
            }
        }
    }
    /* Retain the spacious common mode as well as the decorrelated output
       vectors. Omitting it makes a centered sustained source appear wide but
       lets its reverb body vanish through matrix cancellation. */
    wet.l += mean_l * 1.15f + mean_r * 0.08f;
    wet.r += mean_r * 1.15f + mean_l * 0.08f;
    for (size_t line = 0u; line < TS_SISTER_REVERB_LINES; ++line) {
        TsSisterReverbLine *delay = &state->line[line];
        float seconds = delay->new_delay_frames / engine->sample_rate;
        float gain = powf(0.001f, seconds / decay_seconds);
        float matrix_l = 2.0f * mean_l - read_l[line];
        float matrix_r = 2.0f * mean_r - read_r[line];
        float polarity = output_l[line];
        float inject_l = state->route_current * engine->reverb_engage.current *
            (input.l * (0.155f + 0.006f * (float)line +
                        0.035f * polarity) +
             input.r * polarity * 0.045f);
        float inject_r = state->route_current * engine->reverb_engage.current *
            (input.r * (0.155f + 0.006f * (float)line +
                        0.035f * output_r[line]) -
             input.l * output_r[line] * 0.045f);
        delay->damping[0] += (matrix_l - delay->damping[0]) * damping;
        delay->damping[1] += (matrix_r - delay->damping[1]) * damping;
        delay->damping[0] = flush_tiny(delay->damping[0]);
        delay->damping[1] = flush_tiny(delay->damping[1]);
        delay->data[delay->write_index * 2u] = reverb_saturate(
            inject_l + delay->damping[0] * gain);
        delay->data[delay->write_index * 2u + 1u] = reverb_saturate(
            inject_r + delay->damping[1] * gain);
        delay->write_index = (delay->write_index + 1u) % delay->capacity_frames;
    }
    wet.l = feedback_condition(wet.l);
    wet.r = feedback_condition(wet.r);
    wet = ts_stereo_frame_sanitize(wet);
    wet = read_handoff_apply(&state->read_handoff, wet);
    return reverb_effect_mix(input, wet,
        clampf(state->mix_current * state->route_current *
               engine->reverb_engage.current, 0.0f, 1.0f));
}

TsStereoFrame ts_sister_post_fx_process(TsSisterPostFxEngine *engine,
                                        size_t target_index,
                                        TsStereoFrame input,
                                        int explicit_mono)
{
    uint8_t bit;
    float master_gain;
    TsStereoFrame output;
    if (engine == NULL || !engine->ready ||
        target_index >= TS_SISTER_EFFECT_PROCESSOR_COUNT)
        return ts_stereo_frame_sanitize(input);
    input = ts_stereo_frame_sanitize(input);
    if (explicit_mono) {
        if (target_index == TS_SISTER_EFFECT_PROCESSOR_COUNT - 1u) {
            ramp_advance(&engine->master_engage);
            ramp_advance(&engine->reverb_engage);
            ramp_advance(&engine->delay_engage);
            ramp_advance(&engine->distortion_engage);
        }
        return (TsStereoFrame){input.l, input.l};
    }
    bit = (uint8_t)(1u << target_index);
    output = distortion_process(engine, target_index, input,
        ts_sister_effect_target_enabled(
            engine->distortion_target.active_mask, bit));
    output = delay_process(engine, target_index, output,
        ts_sister_effect_target_enabled(engine->delay_target.active_mask, bit));
    output = reverb_process(engine, target_index, output,
        ts_sister_effect_target_enabled(engine->reverb_target.active_mask, bit));
    master_gain = clampf(engine->master_engage.current, 0.0f, 1.0f);
    /* Master is the final return valve. Keep the exact-zero case explicit so
       no processor state, tail, malformed sample, or individual switch can
       contribute even a floating-point residue once the master reaches dry. */
    if (master_gain <= 0.0f)
        output = input;
    else if (master_gain < 1.0f)
        output = lerp_frame(input, output, master_gain);
    if (target_index == TS_SISTER_EFFECT_PROCESSOR_COUNT - 1u) {
        target_state_advance(engine, &engine->distortion_target, 0);
        target_state_advance(engine, &engine->delay_target, 1);
        target_state_advance(engine, &engine->reverb_target, 2);
        ramp_advance(&engine->master_engage);
        ramp_advance(&engine->reverb_engage);
        ramp_advance(&engine->delay_engage);
        ramp_advance(&engine->distortion_engage);
    }
    return ts_stereo_frame_sanitize(output);
}

float ts_sister_post_fx_master_engage(const TsSisterPostFxEngine *engine)
{
    return engine != NULL && engine->ready ?
        clampf(engine->master_engage.current, 0.0f, 1.0f) : 0.0f;
}

TsSisterFxTransitionStatus ts_sister_post_fx_transition_status(
    const TsSisterPostFxEngine *engine)
{
    const TsSisterFxRamp *ramps[4];
    const TsSisterFxTransitionSource sources[4] = {
        TS_SISTER_FX_TRANSITION_MASTER,
        TS_SISTER_FX_TRANSITION_REVERB,
        TS_SISTER_FX_TRANSITION_DELAY,
        TS_SISTER_FX_TRANSITION_DISTORTION
    };
    const TsSisterFxRamp *latest = NULL;
    TsSisterFxTransitionStatus status = {
        1.0f, TS_SISTER_FX_TRANSITION_NONE, 0, 0
    };
    if (engine == NULL || !engine->ready) return status;
    ramps[0] = &engine->master_engage;
    ramps[1] = &engine->reverb_engage;
    ramps[2] = &engine->delay_engage;
    ramps[3] = &engine->distortion_engage;
    for (size_t i = 0u; i < 4u; ++i) {
        if (ramps[i]->remaining == 0u || ramps[i]->total == 0u) continue;
        if (latest == NULL || ramps[i]->remaining > latest->remaining) {
            latest = ramps[i];
            status.source = sources[i];
        }
    }
    if (latest == NULL) return status;
    status.active = 1;
    status.target_enabled = latest->target >= 0.5f;
    status.progress = clampf(
        1.0f - (float)latest->remaining / (float)latest->total,
        0.0f, 1.0f);
    return status;
}

TsSisterFxTransitionStatus ts_sister_post_fx_effect_transition_status(
    const TsSisterPostFxEngine *engine)
{
    const TsSisterFxRamp *ramps[3];
    const TsSisterFxTransitionSource sources[3] = {
        TS_SISTER_FX_TRANSITION_REVERB,
        TS_SISTER_FX_TRANSITION_DELAY,
        TS_SISTER_FX_TRANSITION_DISTORTION
    };
    const TsSisterFxRamp *latest = NULL;
    TsSisterFxTransitionStatus status = {
        1.0f, TS_SISTER_FX_TRANSITION_NONE, 0, 0
    };
    if (engine == NULL || !engine->ready) return status;
    ramps[0] = &engine->reverb_engage;
    ramps[1] = &engine->delay_engage;
    ramps[2] = &engine->distortion_engage;
    for (size_t i = 0u; i < 3u; ++i) {
        if (ramps[i]->remaining == 0u || ramps[i]->total == 0u) continue;
        if (latest == NULL || ramps[i]->remaining > latest->remaining) {
            latest = ramps[i];
            status.source = sources[i];
        }
    }
    if (latest == NULL) return status;
    status.active = 1;
    status.target_enabled = latest->target >= 0.5f;
    status.progress = clampf(
        1.0f - (float)latest->remaining / (float)latest->total,
        0.0f, 1.0f);
    return status;
}

TsSisterFxTransitionStatus ts_sister_post_fx_master_transition_status(
    const TsSisterPostFxEngine *engine)
{
    TsSisterFxTransitionStatus status = {
        1.0f, TS_SISTER_FX_TRANSITION_NONE, 0, 0
    };
    if (engine == NULL || !engine->ready ||
        engine->master_engage.remaining == 0u ||
        engine->master_engage.total == 0u) return status;
    status.active = 1;
    status.source = TS_SISTER_FX_TRANSITION_MASTER;
    status.target_enabled = engine->master_engage.target >= 0.5f;
    status.progress = clampf(
        1.0f - (float)engine->master_engage.remaining /
               (float)engine->master_engage.total,
        0.0f, 1.0f);
    return status;
}

float ts_sister_post_fx_transition_progress(
    const TsSisterPostFxEngine *engine, int *active)
{
    TsSisterFxTransitionStatus status =
        ts_sister_post_fx_transition_status(engine);
    if (active != NULL) *active = status.active;
    return status.progress;
}

size_t ts_sister_post_fx_memory_bytes(const TsSisterPostFxEngine *engine)
{
    size_t bytes = 0u;
    if (engine == NULL) return 0u;
    for (size_t i = 0u; i < TS_SISTER_EFFECT_PROCESSOR_COUNT; ++i) {
        bytes += engine->delay[i].capacity_frames * 2u * sizeof(float);
        for (size_t line = 0u; line < TS_SISTER_REVERB_LINES; ++line)
            bytes += engine->reverb[i].line[line].capacity_frames *
                     2u * sizeof(float);
    }
    return bytes;
}
