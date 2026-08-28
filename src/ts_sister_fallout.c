#include "tapesister/sister_fallout.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define FALLOUT_SECONDS 20u
#define FALLOUT_TRANSITION_MIN_MS 10.0f
#define FALLOUT_TRANSITION_MAX_MS 60000.0f
#define FALLOUT_LFO_MIN_HZ (1.0f / 3600.0f)
#define FALLOUT_LFO_MAX_HZ 10.0f
#define FALLOUT_RISE_MIN_SECONDS 1.0f
#define FALLOUT_RISE_MAX_SECONDS 14400.0f

static float clampf(float v, float lo, float hi)
{
    if (!isfinite(v)) return lo;
    return v < lo ? lo : v > hi ? hi : v;
}

const char *ts_sister_fallout_noise_type_name(TsSisterFalloutNoiseType type)
{
    static const char *const names[TS_SISTER_FALLOUT_NOISE_COUNT] = {
        "WHITE", "PINK", "BROWN", "BLUE"
    };
    return type >= 0 && type < TS_SISTER_FALLOUT_NOISE_COUNT ?
        names[type] : names[TS_SISTER_FALLOUT_NOISE_WHITE];
}

const char *ts_sister_fallout_rise_mode_name(TsSisterFalloutRiseMode mode)
{
    static const char *const names[TS_SISTER_FALLOUT_RISE_MODE_COUNT] = {
        "SAW", "1-SHOT"
    };
    return mode >= 0 && mode < TS_SISTER_FALLOUT_RISE_MODE_COUNT ?
        names[mode] : names[TS_SISTER_FALLOUT_RISE_SAW];
}

float ts_sister_fallout_transition_ms(float normalized)
{
    return FALLOUT_TRANSITION_MIN_MS * powf(
        FALLOUT_TRANSITION_MAX_MS / FALLOUT_TRANSITION_MIN_MS,
        clampf(normalized, 0.0f, 1.0f));
}

float ts_sister_fallout_transition_normalized(float milliseconds)
{
    milliseconds = clampf(milliseconds, FALLOUT_TRANSITION_MIN_MS,
                          FALLOUT_TRANSITION_MAX_MS);
    return logf(milliseconds / FALLOUT_TRANSITION_MIN_MS) /
           logf(FALLOUT_TRANSITION_MAX_MS / FALLOUT_TRANSITION_MIN_MS);
}

float ts_sister_fallout_lfo_hz(float normalized)
{
    return FALLOUT_LFO_MIN_HZ * powf(
        FALLOUT_LFO_MAX_HZ / FALLOUT_LFO_MIN_HZ,
        clampf(normalized, 0.0f, 1.0f));
}

float ts_sister_fallout_lfo_normalized(float hz)
{
    hz = clampf(hz, FALLOUT_LFO_MIN_HZ, FALLOUT_LFO_MAX_HZ);
    return logf(hz / FALLOUT_LFO_MIN_HZ) /
           logf(FALLOUT_LFO_MAX_HZ / FALLOUT_LFO_MIN_HZ);
}

float ts_sister_fallout_rise_seconds(float normalized)
{
    return FALLOUT_RISE_MIN_SECONDS * powf(
        FALLOUT_RISE_MAX_SECONDS / FALLOUT_RISE_MIN_SECONDS,
        clampf(normalized, 0.0f, 1.0f));
}

float ts_sister_fallout_rise_normalized(float seconds)
{
    seconds = clampf(seconds, FALLOUT_RISE_MIN_SECONDS,
                     FALLOUT_RISE_MAX_SECONDS);
    return logf(seconds / FALLOUT_RISE_MIN_SECONDS) /
           logf(FALLOUT_RISE_MAX_SECONDS / FALLOUT_RISE_MIN_SECONDS);
}

float ts_sister_fallout_lfo_modulate(float center, float intensity,
                                     float sine_value)
{
    float excursion;
    center = clampf(center, 0.0f, 1.0f);
    intensity = clampf(intensity, 0.0f, 1.0f);
    sine_value = clampf(sine_value, -1.0f, 1.0f);
    excursion = fminf(center, 1.0f - center) * intensity;
    return clampf(center + sine_value * excursion, 0.0f, 1.0f);
}

float ts_sister_fallout_rise_modulate(float start, float intensity,
                                      float ramp_value)
{
    start = clampf(start, 0.0f, 1.0f);
    intensity = clampf(intensity, 0.0f, 1.0f);
    ramp_value = clampf(ramp_value, 0.0f, 1.0f);
    return start + (1.0f - start) * intensity * ramp_value;
}

static uint32_t random_u32(TsSisterFalloutEngine *engine)
{
    uint32_t x = engine->prng != 0u ? engine->prng : 0x6d2b79f5u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    engine->prng = x;
    return x;
}

static float random_unit(TsSisterFalloutEngine *engine)
{
    return (float)(random_u32(engine) >> 8) * (1.0f / 16777216.0f);
}

static float random_gaussian(TsSisterFalloutEngine *engine)
{
    float a = fmaxf(random_unit(engine), 0.000001f);
    float b = random_unit(engine);
    return sqrtf(-2.0f * logf(a)) * cosf((float)(2.0 * M_PI) * b);
}

static uint32_t interval_frames(float normalized, uint32_t sample_rate)
{
    float milliseconds = 20.0f * powf(100.0f, clampf(normalized, 0.0f, 1.0f));
    double frames = (double)milliseconds * (double)sample_rate / 1000.0;
    if (frames < 1.0) frames = 1.0;
    if (frames > (double)UINT32_MAX) frames = (double)UINT32_MAX;
    return (uint32_t)frames;
}

static void ramp(float *current, float *step, uint32_t *remaining,
                 float target, uint32_t frames)
{
    if (frames == 0u) {
        *current = target;
        *step = 0.0f;
        *remaining = 0u;
        return;
    }
    *step = (target - *current) / (float)frames;
    *remaining = frames;
}

static float advance(float *current, float target, float step,
                     uint32_t *remaining)
{
    if (*remaining > 0u) {
        *current += step;
        --*remaining;
        if (*remaining == 0u) *current = target;
    }
    return *current;
}

void ts_sister_fallout_controls_default(TsSisterFalloutControls *controls)
{
    if (controls == NULL) return;
    memset(controls, 0, sizeof(*controls));
    controls->mix = 0.65f;
    controls->noise_type = TS_SISTER_FALLOUT_NOISE_WHITE;
    controls->transition = 0.0f;
    controls->drop_rate = 0.45f;
    controls->pan_rate = 0.50f;
    controls->skip_span = 0.35f;
    controls->skip_rate = 0.45f;
    controls->bit_quality = 0.72f;
    controls->bit_resolution = 0.50f;
    controls->bit_rate = 0.50f;
    controls->pitch = 5.0f / 7.0f;
    controls->pitch_ramp = 0.20f;
    controls->pitch_rate = 0.55f;
    controls->lfo_rate = ts_sister_fallout_lfo_normalized(0.01f);
    controls->lfo_intensity = 0.20f;
    controls->lfo_targets = 0u;
    controls->rise_mode = TS_SISTER_FALLOUT_RISE_ONE_SHOT;
    controls->rise_length = ts_sister_fallout_rise_normalized(3600.0f);
    controls->rise_intensity = 1.0f;
    controls->rise_targets = 0u;
}

void ts_sister_fallout_controls_sanitize(TsSisterFalloutControls *controls)
{
    if (controls == NULL) return;
    controls->enabled = controls->enabled != 0;
    controls->mix = clampf(controls->mix, 0.0f, 1.0f);
    controls->feedback = clampf(controls->feedback, 0.0f, 1.0f);
    controls->noise = clampf(controls->noise, 0.0f, 1.0f);
    if (controls->noise_type < 0 ||
        controls->noise_type >= TS_SISTER_FALLOUT_NOISE_COUNT)
        controls->noise_type = TS_SISTER_FALLOUT_NOISE_WHITE;
    controls->transition = clampf(controls->transition, 0.0f, 1.0f);
    controls->drop_enabled = controls->drop_enabled != 0;
    controls->drop_rate = clampf(controls->drop_rate, 0.0f, 1.0f);
    controls->pan_enabled = controls->pan_enabled != 0;
    controls->pan_rate = clampf(controls->pan_rate, 0.0f, 1.0f);
    controls->skip_enabled = controls->skip_enabled != 0;
    controls->skip_span = clampf(controls->skip_span, 0.0f, 1.0f);
    controls->skip_rate = clampf(controls->skip_rate, 0.0f, 1.0f);
    controls->bit_enabled = controls->bit_enabled != 0;
    controls->bit_quality = clampf(controls->bit_quality, 0.01f, 1.0f);
    controls->bit_resolution = clampf(controls->bit_resolution, 0.0f, 1.0f);
    controls->bit_rate = clampf(controls->bit_rate, 0.0f, 1.0f);
    controls->pitch_enabled = controls->pitch_enabled != 0;
    controls->pitch = clampf(controls->pitch, 0.0f, 1.0f);
    controls->pitch_ramp = clampf(controls->pitch_ramp, 0.0f, 1.0f);
    controls->pitch_rate = clampf(controls->pitch_rate, 0.0f, 1.0f);
    controls->lfo_rate = clampf(controls->lfo_rate, 0.0f, 1.0f);
    controls->lfo_intensity = clampf(controls->lfo_intensity, 0.0f, 1.0f);
    controls->lfo_targets &= TS_SISTER_FALLOUT_LFO_ALL;
    if (controls->rise_mode < 0 ||
        controls->rise_mode >= TS_SISTER_FALLOUT_RISE_MODE_COUNT)
        controls->rise_mode = TS_SISTER_FALLOUT_RISE_ONE_SHOT;
    controls->rise_length = clampf(controls->rise_length, 0.0f, 1.0f);
    controls->rise_intensity = clampf(controls->rise_intensity, 0.0f, 1.0f);
    controls->rise_targets &= TS_SISTER_FALLOUT_LFO_ALL;
}

void ts_sister_fallout_clear(TsSisterFalloutEngine *engine)
{
    uint32_t seed;
    if (engine == NULL) return;
    seed = engine->prng != 0u ? engine->prng : 0x46414c4cu;
    if (engine->buffer != NULL)
        memset(engine->buffer, 0, engine->capacity_frames * 2u * sizeof(float));
    engine->valid_frames = 0u;
    engine->write_clock = 0u;
    engine->read_clock = 0.0;
    engine->old_read_clock = 0.0;
    engine->loop_start = 0.0;
    engine->loop_length = 1.0;
    engine->next_skip = engine->next_pitch = engine->next_drop = 0u;
    engine->next_pan = engine->next_bit = 0u;
    engine->skip_fade_remaining = engine->skip_fade_total = 0u;
    engine->playback_rate = engine->playback_target = 1.0f;
    engine->playback_step = 0.0f;
    engine->playback_remaining = 0u;
    engine->drop_gain = engine->drop_target = 1.0f;
    engine->drop_step = 0.0f;
    engine->drop_remaining = 0u;
    engine->pan = engine->pan_target = 0.5f;
    engine->pan_step = 0.0f;
    engine->pan_remaining = 0u;
    engine->held[0] = engine->held[1] = 0.0f;
    engine->bit_quality_current = engine->controls.bit_quality > 0.0f ?
        engine->controls.bit_quality : 0.72f;
    engine->hold_remaining = 0u;
    memset(engine->pink_state, 0, sizeof(engine->pink_state));
    engine->brown_state[0] = engine->brown_state[1] = 0.0f;
    engine->previous_white[0] = engine->previous_white[1] = 0.0f;
    engine->lfo_phase = 0.0;
    engine->lfo_value = 0.0f;
    engine->rise_phase = 0.0;
    engine->rise_value = 0.0f;
    engine->rise_one_shot_complete = 0;
    engine->feedback_modulated = engine->controls.feedback;
    engine->prng = seed;
}

int ts_sister_fallout_reconfigure(TsSisterFalloutEngine *engine,
                                  uint32_t sample_rate)
{
    float *storage;
    size_t frames;
    if (engine == NULL || sample_rate == 0u) return 0;
    frames = (size_t)sample_rate * FALLOUT_SECONDS;
    if (frames / FALLOUT_SECONDS != (size_t)sample_rate ||
        frames > SIZE_MAX / (2u * sizeof(float))) return 0;
    storage = (float *)calloc(frames * 2u, sizeof(float));
    if (storage == NULL) return 0;
    free(engine->buffer);
    engine->buffer = storage;
    engine->capacity_frames = frames;
    engine->sample_rate = sample_rate;
    engine->ready = 1;
    engine->active = 0;
    engine->engage = 0.0f;
    engine->engage_remaining = 0u;
    /* The owner republishes controls after a device reconfigure. Force that
       publication to be treated as a fresh insert edge when it was enabled. */
    engine->controls.enabled = 0;
    ts_sister_fallout_clear(engine);
    return 1;
}

int ts_sister_fallout_init(TsSisterFalloutEngine *engine,
                           uint32_t sample_rate)
{
    if (engine == NULL) return 0;
    memset(engine, 0, sizeof(*engine));
    ts_sister_fallout_controls_default(&engine->controls);
    engine->prng = 0x46414c4cu;
    return ts_sister_fallout_reconfigure(engine, sample_rate);
}

void ts_sister_fallout_free(TsSisterFalloutEngine *engine)
{
    if (engine == NULL) return;
    free(engine->buffer);
    memset(engine, 0, sizeof(*engine));
}

void ts_sister_fallout_seed(TsSisterFalloutEngine *engine, uint32_t seed)
{
    if (engine != NULL) engine->prng = seed != 0u ? seed : 0x46414c4cu;
}

void ts_sister_fallout_set_controls(
    TsSisterFalloutEngine *engine, const TsSisterFalloutControls *controls)
{
    TsSisterFalloutControls next;
    int restart_rise;
    uint32_t removed_targets;
    uint32_t fade;
    if (engine == NULL || controls == NULL) return;
    next = *controls;
    ts_sister_fallout_controls_sanitize(&next);
    restart_rise = next.rise_mode != engine->controls.rise_mode ||
                   next.rise_retrigger != engine->controls.rise_retrigger;
    removed_targets = (engine->controls.lfo_targets & ~next.lfo_targets) |
                      (engine->controls.rise_targets & ~next.rise_targets);
    fade = engine->sample_rate > 0u ? (uint32_t)fmaxf(1.0f,
        ts_sister_fallout_transition_ms(next.transition) *
        (float)engine->sample_rate / 1000.0f) : 1u;
    if (fade == 0u) fade = 1u;
    if (next.enabled && !engine->controls.enabled) {
        ts_sister_fallout_clear(engine);
        engine->active = 1;
        ramp(&engine->engage, &engine->engage_step,
             &engine->engage_remaining, 1.0f, fade);
    } else if (!next.enabled && engine->controls.enabled) {
        ramp(&engine->engage, &engine->engage_step,
             &engine->engage_remaining, 0.0f, fade);
    }
    engine->controls = next;
    /* Event-rate parameters are sampled when their event fires.  Re-arm the
       affected scheduler when modulation is disconnected so the saved panel
       value resumes on the very next audio frame rather than one old interval
       later. */
    if ((removed_targets & (TS_SISTER_FALLOUT_LFO_SKIP_SPAN |
                            TS_SISTER_FALLOUT_LFO_SKIP_RATE)) != 0u)
        engine->next_skip = engine->write_clock;
    if ((removed_targets & TS_SISTER_FALLOUT_LFO_DROP_RATE) != 0u)
        engine->next_drop = engine->write_clock;
    if ((removed_targets & TS_SISTER_FALLOUT_LFO_PAN_RATE) != 0u)
        engine->next_pan = engine->write_clock;
    if ((removed_targets & (TS_SISTER_FALLOUT_LFO_BIT_QUALITY |
                            TS_SISTER_FALLOUT_LFO_BIT_RATE)) != 0u)
        engine->next_bit = engine->write_clock;
    if ((removed_targets & (TS_SISTER_FALLOUT_LFO_PITCH |
                            TS_SISTER_FALLOUT_LFO_PITCH_RAMP |
                            TS_SISTER_FALLOUT_LFO_PITCH_RATE)) != 0u)
        engine->next_pitch = engine->write_clock;
    if (restart_rise) {
        engine->rise_phase = 0.0;
        engine->rise_value = 0.0f;
        engine->rise_one_shot_complete = 0;
    }
}

static TsStereoFrame read_frame(const TsSisterFalloutEngine *engine,
                                double clock)
{
    TsStereoFrame out = {0.0f, 0.0f};
    uint64_t base;
    double fraction;
    size_t a, b;
    if (engine == NULL || engine->buffer == NULL || engine->valid_frames < 2u ||
        !isfinite(clock)) return out;
    if (clock < 0.0) clock = 0.0;
    base = (uint64_t)floor(clock);
    fraction = clock - floor(clock);
    a = (size_t)(base % engine->capacity_frames);
    b = (a + 1u) % engine->capacity_frames;
    out.l = engine->buffer[a * 2u] +
            (engine->buffer[b * 2u] - engine->buffer[a * 2u]) * (float)fraction;
    out.r = engine->buffer[a * 2u + 1u] +
            (engine->buffer[b * 2u + 1u] - engine->buffer[a * 2u + 1u]) * (float)fraction;
    return ts_stereo_frame_sanitize(out);
}

static void choose_loop(TsSisterFalloutEngine *engine,
                        const TsSisterFalloutControls *controls)
{
    double earliest;
    double latest;
    double maximum_length;
    double length;
    if (engine->valid_frames < 4u) return;
    if (engine->valid_frames > 8u) {
        engine->old_read_clock = engine->read_clock;
        engine->skip_fade_total = engine->sample_rate * 8u / 1000u;
        if (engine->skip_fade_total == 0u) engine->skip_fade_total = 1u;
        engine->skip_fade_remaining = engine->skip_fade_total;
    }
    earliest = (double)(engine->write_clock - engine->valid_frames);
    latest = (double)engine->write_clock - 2.0;
    maximum_length = fmin((double)engine->valid_frames - 2.0,
                          0.004 * engine->sample_rate +
                          controls->skip_span * 0.996 * engine->sample_rate);
    length = fmax(2.0, maximum_length);
    if (latest - earliest > length)
        engine->loop_start = earliest + random_unit(engine) *
                             (float)(latest - earliest - length);
    else
        engine->loop_start = earliest;
    engine->loop_length = fmax(2.0, fmin(length, latest - engine->loop_start));
    engine->read_clock = engine->loop_start;
}

static float choose_pitch(const TsSisterFalloutControls *controls)
{
    static const float ratios[8] = {-3.0f, -2.0f, -1.0f, -0.5f,
                                     0.5f,  1.0f,  2.0f,  3.0f};
    int index;
    index = (int)lrintf(controls->pitch * 7.0f);
    if (index < 0) index = 0;
    if (index > 7) index = 7;
    return ratios[index];
}

static float modulation_target(float start,
                               const TsSisterFalloutControls *controls,
                               uint32_t target, float sine_value,
                               float rise_value)
{
    float center = start;
    if ((controls->rise_targets & target) != 0u)
        center = ts_sister_fallout_rise_modulate(
            start, controls->rise_intensity, rise_value);
    if ((controls->lfo_targets & target) != 0u)
        center = ts_sister_fallout_lfo_modulate(
            center, controls->lfo_intensity, sine_value);
    return center;
}

static TsSisterFalloutControls modulated_controls(
    TsSisterFalloutEngine *engine)
{
    TsSisterFalloutControls out = engine->controls;
    float value = sinf((float)(engine->lfo_phase * 2.0 * M_PI));
    float rise = 0.0f;
    double increment = (double)ts_sister_fallout_lfo_hz(out.lfo_rate) /
                       (double)engine->sample_rate;
    double rise_increment = 1.0 /
        ((double)ts_sister_fallout_rise_seconds(out.rise_length) *
         (double)engine->sample_rate);
    engine->lfo_phase += increment;
    if (engine->lfo_phase >= 1.0) engine->lfo_phase -= floor(engine->lfo_phase);
    engine->lfo_value = value;
    if (out.rise_mode == TS_SISTER_FALLOUT_RISE_SAW ||
        !engine->rise_one_shot_complete) {
        rise = (float)engine->rise_phase;
        engine->rise_phase += rise_increment;
        if (engine->rise_phase >= 1.0) {
            rise = 1.0f;
            engine->rise_phase = 0.0;
            if (out.rise_mode == TS_SISTER_FALLOUT_RISE_ONE_SHOT)
                engine->rise_one_shot_complete = 1;
        }
    }
    engine->rise_value = rise;
#define MODULATE(member, target) \
    out.member = modulation_target(engine->controls.member, &out, target, \
                                   value, rise)
    MODULATE(mix, TS_SISTER_FALLOUT_LFO_MIX);
    MODULATE(feedback, TS_SISTER_FALLOUT_LFO_FEEDBACK);
    MODULATE(noise, TS_SISTER_FALLOUT_LFO_NOISE);
    MODULATE(drop_rate, TS_SISTER_FALLOUT_LFO_DROP_RATE);
    MODULATE(pan_rate, TS_SISTER_FALLOUT_LFO_PAN_RATE);
    MODULATE(skip_span, TS_SISTER_FALLOUT_LFO_SKIP_SPAN);
    MODULATE(skip_rate, TS_SISTER_FALLOUT_LFO_SKIP_RATE);
    MODULATE(bit_quality, TS_SISTER_FALLOUT_LFO_BIT_QUALITY);
    MODULATE(bit_resolution, TS_SISTER_FALLOUT_LFO_BIT_RESOLUTION);
    MODULATE(bit_rate, TS_SISTER_FALLOUT_LFO_BIT_RATE);
    MODULATE(pitch, TS_SISTER_FALLOUT_LFO_PITCH);
    MODULATE(pitch_ramp, TS_SISTER_FALLOUT_LFO_PITCH_RAMP);
    MODULATE(pitch_rate, TS_SISTER_FALLOUT_LFO_PITCH_RATE);
#undef MODULATE
    engine->feedback_modulated = out.feedback;
    return out;
}

static float colored_noise(TsSisterFalloutEngine *engine, int channel,
                           TsSisterFalloutNoiseType type)
{
    float white = random_unit(engine) * 2.0f - 1.0f;
    float output;
    switch (type) {
    case TS_SISTER_FALLOUT_NOISE_PINK:
        engine->pink_state[channel][0] =
            0.99765f * engine->pink_state[channel][0] + white * 0.0990460f;
        engine->pink_state[channel][1] =
            0.96300f * engine->pink_state[channel][1] + white * 0.2965164f;
        engine->pink_state[channel][2] =
            0.57000f * engine->pink_state[channel][2] + white * 1.0526913f;
        output = (engine->pink_state[channel][0] +
                  engine->pink_state[channel][1] +
                  engine->pink_state[channel][2] + white * 0.1848f) * 0.10f;
        break;
    case TS_SISTER_FALLOUT_NOISE_BROWN:
        engine->brown_state[channel] =
            engine->brown_state[channel] * 0.995f + white * 0.020f;
        output = engine->brown_state[channel] * 1.25f;
        break;
    case TS_SISTER_FALLOUT_NOISE_BLUE:
        output = (white - engine->previous_white[channel]) * 0.306f;
        break;
    case TS_SISTER_FALLOUT_NOISE_WHITE:
    default:
        output = white * 0.433f;
        break;
    }
    engine->previous_white[channel] = white;
    return output;
}

TsSisterFalloutResult ts_sister_fallout_process(
    TsSisterFalloutEngine *engine, TsStereoFrame input)
{
    TsSisterFalloutResult result = {input, {0.0f, 0.0f}};
    TsSisterFalloutControls controls;
    TsStereoFrame wet;
    float rate, gain, pan, left, right, bits, levels, mix;
    uint32_t frames;
    size_t write_index;
    double loop_end;
    input = ts_stereo_frame_sanitize(input);
    result.output = input;
    if (engine == NULL || !engine->ready || !engine->active) return result;
    controls = modulated_controls(engine);

    write_index = (size_t)(engine->write_clock % engine->capacity_frames);
    engine->buffer[write_index * 2u] = input.l;
    engine->buffer[write_index * 2u + 1u] = input.r;
    ++engine->write_clock;
    if (engine->valid_frames < engine->capacity_frames) ++engine->valid_frames;
    if (engine->valid_frames == 2u) choose_loop(engine, &controls);

    if (!controls.skip_enabled && engine->valid_frames >= 4u) {
        double earliest = (double)(engine->write_clock - engine->valid_frames);
        engine->loop_start = earliest;
        engine->loop_length = fmax(2.0, (double)engine->valid_frames - 2.0);
        if (engine->read_clock < engine->loop_start ||
            engine->read_clock >= engine->loop_start + engine->loop_length)
            engine->read_clock = engine->loop_start;
        engine->skip_fade_remaining = 0u;
    }

    if (controls.skip_enabled && engine->write_clock >= engine->next_skip) {
        choose_loop(engine, &controls);
        engine->next_skip = engine->write_clock +
            interval_frames(controls.skip_rate, engine->sample_rate);
    }
    if (!controls.pitch_enabled) {
        if (engine->playback_target != 1.0f) {
            frames = engine->sample_rate / 100u; /* 10 ms de-click bypass. */
            engine->playback_target = 1.0f;
            ramp(&engine->playback_rate, &engine->playback_step,
                 &engine->playback_remaining, 1.0f, frames);
        }
    } else if (engine->write_clock >= engine->next_pitch) {
        float target = choose_pitch(&controls);
        frames = (uint32_t)(controls.pitch_ramp * 0.5f * engine->sample_rate);
        engine->playback_target = target;
        ramp(&engine->playback_rate, &engine->playback_step,
             &engine->playback_remaining, target, frames);
        engine->next_pitch = engine->write_clock +
            interval_frames(controls.pitch_rate, engine->sample_rate);
    }
    rate = advance(&engine->playback_rate, engine->playback_target,
                   engine->playback_step, &engine->playback_remaining);
    wet = read_frame(engine, engine->read_clock);
    if (engine->skip_fade_remaining > 0u && engine->skip_fade_total > 0u) {
        TsStereoFrame old = read_frame(engine, engine->old_read_clock);
        float amount = 1.0f - (float)engine->skip_fade_remaining /
                              (float)engine->skip_fade_total;
        wet.l = old.l + (wet.l - old.l) * amount;
        wet.r = old.r + (wet.r - old.r) * amount;
        engine->old_read_clock += rate;
        --engine->skip_fade_remaining;
    }
    engine->read_clock += rate;
    loop_end = engine->loop_start + engine->loop_length;
    while (engine->read_clock >= loop_end) engine->read_clock -= engine->loop_length;
    while (engine->read_clock < engine->loop_start) engine->read_clock += engine->loop_length;

    if (controls.drop_enabled && engine->write_clock >= engine->next_drop) {
        engine->drop_target = clampf(0.70f + 0.25f * random_gaussian(engine), 0.0f, 1.2f);
        ramp(&engine->drop_gain, &engine->drop_step, &engine->drop_remaining,
             engine->drop_target, engine->sample_rate * 5u / 1000u);
        engine->next_drop = engine->write_clock +
            interval_frames(controls.drop_rate, engine->sample_rate);
    } else if (!controls.drop_enabled) {
        engine->drop_gain = engine->drop_target = 1.0f;
        engine->drop_remaining = 0u;
    }
    gain = advance(&engine->drop_gain, engine->drop_target,
                   engine->drop_step, &engine->drop_remaining);
    wet.l *= gain;
    wet.r *= gain;

    if (controls.pan_enabled && engine->write_clock >= engine->next_pan) {
        engine->pan_target = clampf(0.5f + 0.25f * random_gaussian(engine), 0.0f, 1.0f);
        ramp(&engine->pan, &engine->pan_step, &engine->pan_remaining,
             engine->pan_target, engine->sample_rate * 50u / 1000u);
        engine->next_pan = engine->write_clock +
            interval_frames(controls.pan_rate, engine->sample_rate);
    } else if (!controls.pan_enabled) {
        engine->pan = engine->pan_target = 0.5f;
        engine->pan_remaining = 0u;
    }
    pan = advance(&engine->pan, engine->pan_target,
                  engine->pan_step, &engine->pan_remaining);
    left = cosf(pan * (float)(M_PI * 0.5));
    right = sinf(pan * (float)(M_PI * 0.5));
    wet.l *= left * 1.41421356f;
    wet.r *= right * 1.41421356f;

    if (controls.bit_enabled) {
        uint32_t hold;
        if (engine->write_clock >= engine->next_bit) {
            engine->bit_quality_current = clampf(
                controls.bit_quality + 0.15f * random_gaussian(engine),
                0.01f, 1.0f);
            engine->next_bit = engine->write_clock +
                interval_frames(controls.bit_rate, engine->sample_rate);
        }
        hold = 1u + (uint32_t)lrintf(
            (1.0f - engine->bit_quality_current) * 127.0f);
        if (engine->hold_remaining == 0u) {
            engine->held[0] = wet.l;
            engine->held[1] = wet.r;
            engine->hold_remaining = hold;
        }
        --engine->hold_remaining;
        bits = 8.0f + 16.0f * controls.bit_resolution;
        levels = powf(2.0f, bits - 1.0f);
        wet.l = roundf(engine->held[0] * levels) / levels;
        wet.r = roundf(engine->held[1] * levels) / levels;
    } else {
        engine->hold_remaining = 0u;
    }

    if (controls.noise > 0.0f) {
        gain = controls.noise * 0.42f * fminf(fabsf(rate), 3.0f);
        wet.l += colored_noise(engine, 0, controls.noise_type) * gain;
        wet.r += colored_noise(engine, 1, controls.noise_type) * gain;
    }
    wet = ts_stereo_frame_sanitize(wet);
    wet.l = tanhf(wet.l);
    wet.r = tanhf(wet.r);
    result.wet = wet;
    mix = controls.mix;
    result.output.l = input.l * (1.0f - mix) + wet.l * mix;
    result.output.r = input.r * (1.0f - mix) + wet.r * mix;
    engine->engage = advance(&engine->engage,
                             engine->controls.enabled ? 1.0f : 0.0f,
                             engine->engage_step, &engine->engage_remaining);
    result.output.l = input.l + (result.output.l - input.l) * engine->engage;
    result.output.r = input.r + (result.output.r - input.r) * engine->engage;
    result.output = ts_stereo_frame_sanitize(result.output);
    if (!engine->controls.enabled && engine->engage_remaining == 0u &&
        engine->engage <= 0.0f) {
        engine->active = 0;
        ts_sister_fallout_clear(engine);
        result.output = input;
        result.wet = (TsStereoFrame){0.0f, 0.0f};
    }
    return result;
}

size_t ts_sister_fallout_memory_bytes(const TsSisterFalloutEngine *engine)
{
    return engine != NULL ? engine->capacity_frames * 2u * sizeof(float) : 0u;
}

float ts_sister_fallout_engage(const TsSisterFalloutEngine *engine)
{
    return engine != NULL ? clampf(engine->engage, 0.0f, 1.0f) : 0.0f;
}

float ts_sister_fallout_feedback_amount(const TsSisterFalloutEngine *engine)
{
    return engine != NULL ? clampf(engine->feedback_modulated, 0.0f, 1.0f) :
                            0.0f;
}
