#include "tapesister/dsp_transform.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_error(char *error, size_t error_size, const char *message)
{
    if (error != NULL && error_size > 0u)
        snprintf(error, error_size, "%s", message != NULL ? message : "");
}

void ts_dsp_transform_preview_init(TsDspTransformPreview *preview)
{
    if (preview == NULL) return;
    memset(preview, 0, sizeof(*preview));
    ts_sample_init(&preview->sample);
    preview->safety = TS_CDP_SAFETY_INVALID;
}

void ts_dsp_transform_preview_free(TsDspTransformPreview *preview)
{
    if (preview == NULL) return;
    ts_sample_free(&preview->sample);
    ts_dsp_transform_preview_init(preview);
}

int ts_dsp_transform_identity_capture(TsDspTransformIdentity *identity,
                                      const TsInstrument *instrument,
                                      TsTransformScope scope, int preset_slot,
                                      const TsProcessRecipe *process,
                                      uint64_t job_id,
                                      uint64_t render_generation,
                                      char *error, size_t error_size)
{
    if (identity == NULL || instrument == NULL || process == NULL ||
        !ts_recipe_process_valid(process) || instrument->current.data == NULL ||
        instrument->current.frames == 0u || instrument->selected_slot < 0 ||
        instrument->selected_slot >= TS_BANK_SLOT_COUNT ||
        !instrument->bank[instrument->selected_slot].occupied) {
        set_error(error, error_size, "DSP Transform needs an occupied active tile");
        return 0;
    }
    if (scope == TS_TRANSFORM_SELECTION &&
        (!instrument->has_selection ||
         instrument->selection_last <= instrument->selection_first ||
         instrument->selection_last > instrument->current.frames)) {
        set_error(error, error_size, "SELECTION SCOPE NEEDS A NONEMPTY SELECTION");
        return 0;
    }
    memset(identity, 0, sizeof(*identity));
    identity->job_id = job_id;
    identity->render_generation = render_generation;
    identity->tile_slot = instrument->selected_slot;
    identity->audio_revision = ts_sample_hash(&instrument->current);
    identity->tile_frames = instrument->current.frames;
    identity->selection_first = instrument->selection_first;
    identity->selection_last = instrument->selection_last;
    identity->has_selection = instrument->has_selection;
    identity->scope = scope;
    identity->preset_slot = preset_slot;
    identity->process = *process;
    set_error(error, error_size, "");
    return 1;
}

int ts_dsp_transform_identity_matches(const TsDspTransformIdentity *identity,
                                      const TsInstrument *instrument,
                                      TsTransformScope scope, int preset_slot,
                                      const TsProcessRecipe *process,
                                      uint64_t render_generation,
                                      char *error, size_t error_size)
{
    if (identity == NULL || instrument == NULL || process == NULL ||
        identity->render_generation != render_generation ||
        identity->tile_slot != instrument->selected_slot ||
        identity->audio_revision != ts_sample_hash(&instrument->current) ||
        identity->tile_frames != instrument->current.frames ||
        identity->selection_first != instrument->selection_first ||
        identity->selection_last != instrument->selection_last ||
        identity->has_selection != instrument->has_selection ||
        identity->scope != scope || identity->preset_slot != preset_slot ||
        !ts_process_recipe_equal(&identity->process, process)) {
        set_error(error, error_size, "DSP TRANSFORM RESULT IS STALE");
        return 0;
    }
    set_error(error, error_size, "");
    return 1;
}

int ts_dsp_transform_extract_input(const TsInstrument *instrument,
                                   const TsDspTransformIdentity *identity,
                                   TsSample *input,
                                   char *error, size_t error_size)
{
    size_t first;
    size_t last;
    size_t frames;
    float *data;
    if (instrument == NULL || identity == NULL || input == NULL ||
        identity->tile_slot != instrument->selected_slot ||
        identity->audio_revision != ts_sample_hash(&instrument->current)) {
        set_error(error, error_size, "DSP source changed before render");
        return 0;
    }
    first = identity->scope == TS_TRANSFORM_SELECTION ?
            identity->selection_first : 0u;
    last = identity->scope == TS_TRANSFORM_SELECTION ?
           identity->selection_last : instrument->current.frames;
    if (first >= last || last > instrument->current.frames) {
        set_error(error, error_size, "DSP Transform scope is empty");
        return 0;
    }
    frames = last - first;
    if (frames > SIZE_MAX / sizeof(*data)) {
        set_error(error, error_size, "DSP Transform input is too large");
        return 0;
    }
    data = malloc(frames * sizeof(*data));
    if (data == NULL) {
        set_error(error, error_size, "Out of memory snapshotting DSP input");
        return 0;
    }
    memcpy(data, instrument->current.data + first, frames * sizeof(*data));
    ts_sample_free(input);
    input->data = data;
    input->frames = frames;
    input->sample_rate = instrument->current.sample_rate;
    snprintf(input->name, sizeof(input->name), "DSP %.120s", instrument->current.name);
    set_error(error, error_size, "");
    return 1;
}

int ts_dsp_transform_render(const TsSample *input,
                            const TsProcessRecipe *process,
                            TsSample *output,
                            TsCdpSafetyStatus *safety,
                            float *peak, double *dc_offset,
                            int *clipped_samples,
                            char *error, size_t error_size)
{
    double dc = 0.0;
    float maximum = 0.0f;
    int clipped = 0;
    if (input == NULL || input->data == NULL || input->frames == 0u ||
        output == NULL || !ts_recipe_process_valid(process) ||
        !ts_sample_process(output, input, 0u, input->frames, process,
                           error, error_size)) return 0;
    for (size_t i = 0; i < output->frames; ++i) {
        float value = output->data[i];
        float absolute;
        if (!isfinite(value)) {
            ts_sample_free(output);
            if (safety != NULL) *safety = TS_CDP_SAFETY_INVALID;
            set_error(error, error_size, "DSP preview produced non-finite audio");
            return 0;
        }
        absolute = fabsf(value);
        if (absolute > maximum) maximum = absolute;
        if (absolute >= 0.9999f) ++clipped;
        dc += value;
    }
    dc /= (double)output->frames;
    if (peak != NULL) *peak = maximum;
    if (dc_offset != NULL) *dc_offset = dc;
    if (clipped_samples != NULL) *clipped_samples = clipped;
    if (safety != NULL)
        *safety = maximum < 0.00001f ? TS_CDP_SAFETY_SILENT :
                  clipped > 0 ? TS_CDP_SAFETY_HOT : TS_CDP_SAFETY_SAFE;
    set_error(error, error_size, "");
    return 1;
}

#define TS_DSP_PI 3.14159265358979323846

static float dsp_clamp(float value, float minimum, float maximum)
{
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static float dsp_value(const TsDspRecipe *recipe,
                       const TsDspRecipeValues *values, size_t index)
{
    return ts_dsp_recipe_control_value(&recipe->controls[index],
                                       values->controls[index]);
}

static uint32_t dsp_rng_next(uint32_t *state)
{
    uint32_t value = *state != 0u ? *state : 0x6d2b79f5u;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

static float dsp_rng_bipolar(uint32_t *state)
{
    return (float)((dsp_rng_next(state) >> 8) * (1.0 / 8388607.5) - 1.0);
}

static float dsp_linear(const float *data, size_t frames, double position)
{
    size_t at;
    size_t next;
    float fraction;
    if (frames == 0u) return 0.0f;
    if (position < 0.0) position = 0.0;
    if (position > (double)(frames - 1u)) position = (double)(frames - 1u);
    at = (size_t)position;
    next = at + 1u < frames ? at + 1u : at;
    fraction = (float)(position - (double)at);
    return data[at] + (data[next] - data[at]) * fraction;
}

static int dsp_allocate_like(TsSample *output, const TsSample *input,
                             const char *name, char *error, size_t error_size)
{
    TsSample made;
    if (output == NULL || input == NULL || input->data == NULL ||
        input->frames == 0u || input->sample_rate == 0u ||
        input->frames > SIZE_MAX / sizeof(float)) {
        set_error(error, error_size, "Invalid native DSP input");
        return 0;
    }
    ts_sample_init(&made);
    made.data = malloc(input->frames * sizeof(*made.data));
    if (made.data == NULL) {
        set_error(error, error_size, "Out of memory rendering native DSP");
        return 0;
    }
    made.frames = input->frames;
    made.sample_rate = input->sample_rate;
    snprintf(made.name, sizeof(made.name), "%s %.112s",
             name != NULL ? name : "DSP", input->name);
    ts_sample_free(output);
    *output = made;
    return 1;
}

static void dsp_mix_dry(TsSample *wet, const TsSample *dry, float mix)
{
    mix = dsp_clamp(mix, 0.0f, 1.0f);
    for (size_t frame = 0; frame < wet->frames; ++frame)
        wet->data[frame] = dsp_clamp(dry->data[frame] * (1.0f - mix) +
                                     wet->data[frame] * mix, -1.0f, 1.0f);
}

static int dsp_render_process(const TsSample *input, const TsDspRecipe *recipe,
                              const TsDspRecipeValues *values, TsSample *output,
                              char *error, size_t error_size)
{
    TsProcessRecipe process;
    float a = dsp_value(recipe, values, 0u);
    float b = dsp_value(recipe, values, 1u);
    float c = dsp_value(recipe, values, 2u);
    float d = dsp_value(recipe, values, 3u);
    float external_mix = 1.0f;
    ts_process_recipe_reset(&process);
    process.seed = values->seed;
    switch (recipe->kind) {
    case TS_DSP_RECIPE_SPACE:
        process.reverb_enabled = 1;
        process.reverb_decay = 0.20f + b * 0.70f;
        process.reverb_damping = c;
        process.reverb_mix = d;
        process.body = 0.5f + (a - 0.5f) * 0.24f;
        break;
    case TS_DSP_RECIPE_CAVE:
        process.delay_enabled = 1;
        process.delay_seconds = 0.035f + a * 0.11f;
        process.delay_feedback = 0.18f + b * 0.30f;
        process.delay_damping = d;
        process.delay_mix = 0.22f + c * 0.22f;
        process.reverb_enabled = 1;
        process.reverb_decay = 0.68f + b * 0.22f;
        process.reverb_damping = 0.28f + d * 0.70f;
        process.reverb_mix = 0.58f + a * 0.28f;
        process.body = 0.56f + d * 0.34f;
        break;
    case TS_DSP_RECIPE_ROOM:
        process.delay_enabled = 1;
        process.delay_seconds = 0.008f + a * 0.045f;
        process.delay_feedback = 0.04f + b * 0.34f;
        process.delay_damping = 0.35f;
        process.delay_mix = b * 0.24f;
        process.reverb_enabled = 1;
        process.reverb_decay = 0.08f + c * 0.56f;
        process.reverb_damping = 0.38f;
        process.reverb_mix = d;
        break;
    case TS_DSP_RECIPE_ECHO:
        process.delay_enabled = 1;
        process.delay_seconds = a;
        process.delay_feedback = b * 0.85f;
        process.delay_damping = 1.0f - c;
        process.delay_mix = d;
        break;
    case TS_DSP_RECIPE_TAPE:
        process.delay_enabled = 1;
        process.delay_seconds = a;
        process.delay_feedback = b * 0.82f;
        process.delay_damping = 0.36f + d * 0.58f;
        process.delay_mix = 0.48f;
        process.drift = c;
        process.shaper_enabled = 1;
        process.shaper_mode = TS_SHAPER_TAPE;
        process.shaper_drive = 1.0f + d * 9.0f;
        process.shaper_mix = 0.18f + d * 0.64f;
        break;
    case TS_DSP_RECIPE_DUB:
        process.delay_enabled = 1;
        process.delay_seconds = a;
        process.delay_feedback = b * 0.85f;
        process.delay_damping = 0.58f;
        process.delay_mix = 0.58f;
        process.filter_enabled = 1;
        process.filter_mode = TS_FILTER_LOWPASS;
        process.filter_cutoff_hz = c;
        process.filter_resonance = 0.20f;
        process.shaper_enabled = 1;
        process.shaper_mode = TS_SHAPER_TAPE;
        process.shaper_drive = d;
        process.shaper_mix = 0.44f;
        break;
    case TS_DSP_RECIPE_RESONATE:
        process.filter_enabled = 1;
        process.filter_mode = TS_FILTER_BANDPASS;
        process.filter_cutoff_hz = a;
        process.filter_resonance = b;
        process.reverb_enabled = 1;
        process.reverb_decay = 0.08f + b * 0.30f;
        process.reverb_damping = c;
        process.reverb_mix = 0.10f + b * 0.24f;
        external_mix = d;
        break;
    case TS_DSP_RECIPE_LOW:
    case TS_DSP_RECIPE_HIGH:
    case TS_DSP_RECIPE_BAND:
        process.filter_enabled = 1;
        process.filter_mode = recipe->kind == TS_DSP_RECIPE_LOW ? TS_FILTER_LOWPASS :
                              recipe->kind == TS_DSP_RECIPE_HIGH ? TS_FILTER_HIGHPASS :
                              TS_FILTER_BANDPASS;
        process.filter_cutoff_hz = a;
        process.filter_resonance = recipe->kind == TS_DSP_RECIPE_BAND ?
                                   1.0f - b : b;
        process.shaper_enabled = c > 1.001f;
        process.shaper_mode = TS_SHAPER_TAPE;
        process.shaper_drive = c;
        process.shaper_mix = c > 1.001f ? 0.42f : 0.0f;
        external_mix = d;
        break;
    case TS_DSP_RECIPE_DRIVE:
        process.shaper_enabled = 1;
        process.shaper_mode = b < 0.333333f ? TS_SHAPER_TAPE :
                              b < 0.666667f ? TS_SHAPER_CLIP : TS_SHAPER_FOLD;
        process.shaper_drive = a;
        process.shaper_mix = 1.0f;
        process.filter_enabled = 1;
        process.filter_mode = TS_FILTER_LOWPASS;
        process.filter_cutoff_hz = c;
        process.filter_resonance = 0.08f;
        external_mix = d;
        break;
    default:
        set_error(error, error_size, "Unsupported process mapping");
        return 0;
    }
    if (!ts_sample_process(output, input, 0u, input->frames, &process,
                           error, error_size)) return 0;
    if (external_mix < 0.999999f) dsp_mix_dry(output, input, external_mix);
    return 1;
}

static int dsp_render_comb(const TsSample *input, const TsDspRecipe *recipe,
                           const TsDspRecipeValues *values, TsSample *output,
                           char *error, size_t error_size)
{
    float frequency = dsp_value(recipe, values, 0u);
    float feedback = dsp_value(recipe, values, 1u) * 0.92f;
    float damping = dsp_value(recipe, values, 2u);
    float mix = dsp_value(recipe, values, 3u);
    size_t length = (size_t)llround((double)input->sample_rate / frequency);
    float *delay;
    float low = 0.0f;
    if (length < 2u) length = 2u;
    delay = calloc(length, sizeof(*delay));
    if (delay == NULL || !dsp_allocate_like(output, input, recipe->display_name,
                                             error, error_size)) {
        free(delay);
        return 0;
    }
    for (size_t frame = 0; frame < input->frames; ++frame) {
        size_t at = frame % length;
        float delayed = delay[at];
        low += (delayed - low) * (0.02f + (1.0f - damping) * 0.76f);
        delay[at] = dsp_clamp(input->data[frame] + low * feedback, -1.5f, 1.5f);
        output->data[frame] = dsp_clamp(input->data[frame] * (1.0f - mix) +
                                        delayed * mix, -1.0f, 1.0f);
    }
    free(delay);
    return 1;
}

static int dsp_render_notch(const TsSample *input, const TsDspRecipe *recipe,
                            const TsDspRecipeValues *values, TsSample *output,
                            char *error, size_t error_size)
{
    float center = dsp_value(recipe, values, 0u);
    float q = 0.5f + dsp_value(recipe, values, 1u) * 18.0f;
    float movement = dsp_value(recipe, values, 2u);
    float mix = dsp_value(recipe, values, 3u);
    float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;
    float b0 = 1.0f, b1 = 0.0f, b2 = 1.0f, a1 = 0.0f, a2 = 0.0f;
    if (!dsp_allocate_like(output, input, recipe->display_name, error, error_size))
        return 0;
    for (size_t frame = 0; frame < input->frames; ++frame) {
        if ((frame & 31u) == 0u) {
            double phase = (double)frame * 2.0 * TS_DSP_PI * movement /
                           (double)input->sample_rate;
            double frequency = center * exp2(sin(phase) * 0.75);
            double omega;
            double alpha;
            double a0;
            if (frequency > input->sample_rate * 0.45)
                frequency = input->sample_rate * 0.45;
            omega = 2.0 * TS_DSP_PI * frequency / input->sample_rate;
            alpha = sin(omega) / (2.0 * q);
            a0 = 1.0 + alpha;
            b0 = (float)(1.0 / a0);
            b1 = (float)(-2.0 * cos(omega) / a0);
            b2 = b0;
            a1 = b1;
            a2 = (float)((1.0 - alpha) / a0);
        }
        {
            float dry = input->data[frame];
            float wet = b0 * dry + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = dry; y2 = y1; y1 = wet;
            output->data[frame] = dsp_clamp(dry * (1.0f - mix) + wet * mix,
                                            -1.0f, 1.0f);
        }
    }
    return 1;
}

static int dsp_render_mod_delay(const TsSample *input, const TsDspRecipe *recipe,
                                const TsDspRecipeValues *values, TsSample *output,
                                char *error, size_t error_size)
{
    float rate = dsp_value(recipe, values, 0u);
    float depth_ms = dsp_value(recipe, values, 1u);
    float third = dsp_value(recipe, values, 2u);
    float mix = dsp_value(recipe, values, 3u);
    float feedback = recipe->kind == TS_DSP_RECIPE_FLANGE ? third * 0.82f : 0.0f;
    double base_ms = recipe->kind == TS_DSP_RECIPE_CHORUS ? 8.0 : 0.4;
    size_t maximum = (size_t)ceil((base_ms + depth_ms + 2.0) *
                                  input->sample_rate / 1000.0) + 2u;
    float *history = calloc(maximum, sizeof(*history));
    if (history == NULL || !dsp_allocate_like(output, input, recipe->display_name,
                                               error, error_size)) {
        free(history);
        return 0;
    }
    for (size_t frame = 0; frame < input->frames; ++frame) {
        double phase = (double)frame * 2.0 * TS_DSP_PI * rate /
                       (double)input->sample_rate;
        double lfo = 0.5 + 0.5 * sin(phase);
        double delay_frames = (base_ms + depth_ms * lfo) * input->sample_rate / 1000.0;
        double read = (double)frame - delay_frames;
        float wet = read >= 0.0 ? dsp_linear(history, maximum,
                    fmod(read, (double)maximum)) : 0.0f;
        if (recipe->kind == TS_DSP_RECIPE_CHORUS) {
            double second_delay = (base_ms + depth_ms *
                                   (0.5 + 0.5 * sin(phase + TS_DSP_PI * third))) *
                                  input->sample_rate / 1000.0;
            double second_read = (double)frame - second_delay;
            if (second_read >= 0.0)
                wet = 0.5f * (wet + dsp_linear(history, maximum,
                              fmod(second_read, (double)maximum)));
        }
        history[frame % maximum] = dsp_clamp(input->data[frame] + wet * feedback,
                                              -1.5f, 1.5f);
        output->data[frame] = dsp_clamp(input->data[frame] * (1.0f - mix) + wet * mix,
                                        -1.0f, 1.0f);
    }
    free(history);
    return 1;
}

static int dsp_render_crush(const TsSample *input, const TsDspRecipe *recipe,
                            const TsDspRecipeValues *values, TsSample *output,
                            char *error, size_t error_size)
{
    int bits = (int)lrintf(dsp_value(recipe, values, 0u));
    float rate = dsp_value(recipe, values, 1u);
    float cutoff = dsp_value(recipe, values, 2u);
    float mix = dsp_value(recipe, values, 3u);
    unsigned hold_frames = 1u + (unsigned)lrintf(rate * rate * 63.0f);
    float levels = (float)((1u << (unsigned)(bits - 1)) - 1u);
    float held = 0.0f;
    float filtered = 0.0f;
    float coefficient = 1.0f - expf(-2.0f * (float)TS_DSP_PI * cutoff /
                                    (float)input->sample_rate);
    if (!dsp_allocate_like(output, input, recipe->display_name, error, error_size))
        return 0;
    for (size_t frame = 0; frame < input->frames; ++frame) {
        if (frame % hold_frames == 0u)
            held = roundf(input->data[frame] * levels) / levels;
        filtered += (held - filtered) * coefficient;
        output->data[frame] = dsp_clamp(input->data[frame] * (1.0f - mix) +
                                        filtered * mix, -1.0f, 1.0f);
    }
    return 1;
}

static float primitive_edge(size_t frame, size_t frames, uint32_t rate)
{
    size_t fade = rate / 250u;
    float envelope = 1.0f;
    if (fade < 1u) fade = 1u;
    if (fade * 2u > frames) fade = frames / 2u;
    if (fade == 0u) return 1.0f;
    if (frame < fade) envelope = (float)frame / (float)fade;
    if (frames - 1u - frame < fade) {
        float tail = (float)(frames - 1u - frame) / (float)fade;
        if (tail < envelope) envelope = tail;
    }
    return envelope;
}

static int dsp_render_primitive(const TsSample *input, const TsDspRecipe *recipe,
                                const TsDspRecipeValues *values, TsSample *output,
                                char *error, size_t error_size)
{
    float a = dsp_value(recipe, values, 0u);
    float b = dsp_value(recipe, values, 1u);
    float c = dsp_value(recipe, values, 2u);
    float source = dsp_value(recipe, values, 3u);
    uint32_t rng = values->seed;
    double phase = 0.0;
    double mod_phase = 0.0;
    float low_noise = 0.0f;
    float dust = 0.0f;
    float chaos = 0.137f;
    double dc = 0.0;
    if (!dsp_allocate_like(output, input, recipe->display_name, error, error_size))
        return 0;
    for (size_t frame = 0; frame < input->frames; ++frame) {
        float time = (float)frame / (float)input->sample_rate;
        float random = dsp_rng_bipolar(&rng);
        float generated = 0.0f;
        float edge = primitive_edge(frame, input->frames, input->sample_rate);
        float decay;
        low_noise += (random - low_noise) * 0.012f;
        switch (recipe->kind) {
        case TS_DSP_RECIPE_SINE:
            phase += 2.0 * TS_DSP_PI * a / input->sample_rate;
            generated = sinf((float)phase) + sinf((float)(phase * 2.0)) * b * 0.32f;
            generated *= expf(-time * c * 5.0f);
            break;
        case TS_DSP_RECIPE_SHAPE: {
            float sine;
            float saw;
            float square;
            float morph = dsp_clamp(b + sinf(time * (0.2f + c * 5.0f) *
                                                   2.0f * (float)TS_DSP_PI) * c * 0.22f,
                                    0.0f, 1.0f);
            phase += 2.0 * TS_DSP_PI * a / input->sample_rate;
            if (phase >= 2.0 * TS_DSP_PI) phase -= 2.0 * TS_DSP_PI;
            sine = sinf((float)phase);
            saw = (float)(phase / TS_DSP_PI - 1.0);
            square = phase < TS_DSP_PI ? 1.0f : -1.0f;
            generated = morph < 0.5f ? sine + (saw - sine) * morph * 2.0f :
                        saw + (square - saw) * (morph - 0.5f) * 2.0f;
            break;
        }
        case TS_DSP_RECIPE_PULSE: {
            float width = 0.04f + b * 0.88f + sinf(time * (0.1f + c * 7.0f) *
                                                   2.0f * (float)TS_DSP_PI) * c * 0.12f;
            width = dsp_clamp(width, 0.02f, 0.98f);
            phase += 2.0 * TS_DSP_PI * a / input->sample_rate;
            if (phase >= 2.0 * TS_DSP_PI) phase -= 2.0 * TS_DSP_PI;
            generated = phase < width * 2.0 * TS_DSP_PI ? 0.72f : -0.72f;
            break;
        }
        case TS_DSP_RECIPE_SUB:
            phase += 2.0 * TS_DSP_PI * a / input->sample_rate;
            generated = sinf((float)phase) * (0.72f + b * 0.20f) +
                        sinf((float)(phase * 0.5)) * b * 0.38f;
            generated *= expf(-time * c * 3.0f);
            break;
        case TS_DSP_RECIPE_METAL:
            phase += 2.0 * TS_DSP_PI * a / input->sample_rate;
            mod_phase += 2.0 * TS_DSP_PI * a * b / input->sample_rate;
            generated = sinf((float)(phase + sin(mod_phase) * c * 8.0)) * 0.68f +
                        sinf((float)(phase * (b + 0.37f))) * 0.32f;
            generated *= expf(-time * (0.35f + c * 4.0f));
            break;
        case TS_DSP_RECIPE_CHIME:
            phase += 2.0 * TS_DSP_PI * a / input->sample_rate;
            decay = expf(-time * (0.45f + (1.0f - c) * 8.0f));
            generated = sinf((float)phase) * 0.58f +
                        sinf((float)(phase * 2.756)) * b * 0.34f +
                        sinf((float)(phase * 5.431)) * b * b * 0.22f;
            generated *= decay;
            break;
        case TS_DSP_RECIPE_DRONE:
            phase += 2.0 * TS_DSP_PI * a *
                     (1.0 + sin(time * 0.31) * c * 0.018) / input->sample_rate;
            generated = sinf((float)phase) * (0.72f - b * 0.22f) +
                        sinf((float)(phase * (1.003 + c * 0.014))) * 0.34f +
                        sinf((float)(phase * 2.0)) * b * 0.28f;
            break;
        case TS_DSP_RECIPE_BEAT: {
            float first = sinf(2.0f * (float)TS_DSP_PI * a * time);
            float second = sinf(2.0f * (float)TS_DSP_PI * (a + b) * time);
            float color = sinf(2.0f * (float)TS_DSP_PI * a * 2.0f * time) * c;
            generated = (first + second) * 0.46f + color * 0.18f;
            break;
        }
        case TS_DSP_RECIPE_RUMBLE:
            phase += 2.0 * TS_DSP_PI * a *
                     (1.0 + low_noise * c * 0.08f) / input->sample_rate;
            generated = sinf((float)phase) * (0.65f - b * 0.20f) +
                        low_noise * b * 2.1f + random * b * 0.08f;
            break;
        case TS_DSP_RECIPE_HISS: {
            float rate = 0.002f + (1.0f - b / recipe->controls[1].maximum) * 0.12f;
            low_noise += (random - low_noise) * rate;
            generated = random * (0.25f + a * 0.55f) +
                        low_noise * (1.6f - a * 1.2f);
            generated *= 0.72f + sinf(time * (0.1f + c * 5.0f) *
                                      2.0f * (float)TS_DSP_PI) * c * 0.24f;
            break;
        }
        case TS_DSP_RECIPE_DUST:
            if ((dsp_rng_next(&rng) / (double)UINT32_MAX) <
                (double)a / (double)input->sample_rate)
                dust = random * (0.45f + c * 0.55f);
            dust *= expf(-1.0f / ((b * 0.001f) * input->sample_rate + 1.0f));
            generated = dust;
            break;
        case TS_DSP_RECIPE_KNOCK:
            phase += 2.0 * TS_DSP_PI * a *
                     (1.0 + expf(-time * 24.0f) * (0.2f + b * 1.8f)) /
                     input->sample_rate;
            generated = (sinf((float)phase) + low_noise * b * 0.8f) *
                        expf(-time * (2.0f + (1.0f - c) * 22.0f));
            break;
        case TS_DSP_RECIPE_PING:
            phase += 2.0 * TS_DSP_PI * a / input->sample_rate;
            generated = (sinf((float)phase) + sinf((float)(phase * 2.01)) * b * 0.18f) *
                        expf(-time * (0.8f + (1.0f - c) * 24.0f));
            break;
        case TS_DSP_RECIPE_FM:
            mod_phase += 2.0 * TS_DSP_PI * a * b / input->sample_rate;
            phase += 2.0 * TS_DSP_PI * a / input->sample_rate;
            generated = sinf((float)(phase + sin(mod_phase) * c));
            break;
        case TS_DSP_RECIPE_AM:
            phase += 2.0 * TS_DSP_PI * a / input->sample_rate;
            mod_phase += 2.0 * TS_DSP_PI * b / input->sample_rate;
            generated = sinf((float)phase) *
                        ((1.0f - c) + c * (0.5f + 0.5f * sinf((float)mod_phase)));
            break;
        case TS_DSP_RECIPE_CHAOS:
            chaos = 3.55f + b * 0.44f;
            dust = dsp_clamp(dust + (random - dust) * (0.002f + c * 0.08f),
                             -1.0f, 1.0f);
            phase += 2.0 * TS_DSP_PI * a * (1.0 + dust * b * 0.16f) /
                     input->sample_rate;
            generated = sinf((float)(phase + chaos * dust)) * (0.68f - c * 0.18f) +
                        random * c * 0.28f;
            break;
        default:
            generated = 0.0f;
            break;
        }
        generated = tanhf(generated * 1.12f) * 0.72f * edge;
        output->data[frame] = generated;
        dc += generated;
    }
    dc /= (double)output->frames;
    if (fabs(dc) > 0.000001) {
        for (size_t frame = 0; frame < output->frames; ++frame)
            output->data[frame] -= (float)dc;
    }
    for (size_t frame = 0; frame < output->frames; ++frame)
        output->data[frame] = dsp_clamp(
            output->data[frame] * (1.0f - source) + input->data[frame] * source,
            -1.0f, 1.0f);
    return 1;
}

static int dsp_curated_values_valid(const TsDspRecipe *recipe,
                                    const TsDspRecipeValues *values)
{
    if (recipe == NULL || values == NULL ||
        !ts_dsp_recipe_validate(recipe, NULL, 0u) ||
        !isfinite(values->tuning_hz) || values->tuning_hz <= 0.0f) return 0;
    for (size_t index = 0; index < recipe->control_count; ++index)
        if (!isfinite(values->controls[index]) || values->controls[index] < 0.0f ||
            values->controls[index] > 1.0f) return 0;
    return 1;
}

int ts_dsp_transform_render_recipe(
    const TsSample *input, const TsDspRecipe *recipe,
    const TsDspRecipeValues *values, TsSample *output,
    TsCdpSafetyStatus *safety, float *peak, double *dc_offset,
    int *clipped_samples, char *error, size_t error_size)
{
    double dc = 0.0;
    float maximum = 0.0f;
    int clipped = 0;
    int ok;
    if (input == NULL || input->data == NULL || input->frames == 0u ||
        output == NULL || !dsp_curated_values_valid(recipe, values)) {
        set_error(error, error_size, "Invalid curated DSP render request");
        return 0;
    }
    if (recipe->primitive)
        ok = dsp_render_primitive(input, recipe, values, output, error, error_size);
    else if (recipe->kind == TS_DSP_RECIPE_COMB)
        ok = dsp_render_comb(input, recipe, values, output, error, error_size);
    else if (recipe->kind == TS_DSP_RECIPE_NOTCH)
        ok = dsp_render_notch(input, recipe, values, output, error, error_size);
    else if (recipe->kind == TS_DSP_RECIPE_CHORUS ||
             recipe->kind == TS_DSP_RECIPE_FLANGE)
        ok = dsp_render_mod_delay(input, recipe, values, output, error, error_size);
    else if (recipe->kind == TS_DSP_RECIPE_CRUSH)
        ok = dsp_render_crush(input, recipe, values, output, error, error_size);
    else
        ok = dsp_render_process(input, recipe, values, output, error, error_size);
    if (!ok) return 0;
    for (size_t frame = 0; frame < output->frames; ++frame) {
        float value = output->data[frame];
        float absolute;
        if (!isfinite(value)) {
            ts_sample_free(output);
            if (safety != NULL) *safety = TS_CDP_SAFETY_INVALID;
            set_error(error, error_size, "Native DSP produced non-finite audio");
            return 0;
        }
        absolute = fabsf(value);
        if (absolute > maximum) maximum = absolute;
        if (absolute >= 0.9999f) ++clipped;
        dc += value;
    }
    dc /= (double)output->frames;
    if (peak != NULL) *peak = maximum;
    if (dc_offset != NULL) *dc_offset = dc;
    if (clipped_samples != NULL) *clipped_samples = clipped;
    if (safety != NULL)
        *safety = maximum < 0.00001f ? TS_CDP_SAFETY_SILENT :
                  clipped > 0 ? TS_CDP_SAFETY_HOT : TS_CDP_SAFETY_SAFE;
    set_error(error, error_size, "");
    return 1;
}

int ts_dsp_transform_identity_capture_recipe(
    TsDspTransformIdentity *identity, const TsInstrument *instrument,
    TsTransformScope scope, const TsDspRecipe *recipe,
    const TsDspRecipeValues *values, uint64_t job_id,
    uint64_t render_generation, char *error, size_t error_size)
{
    TsProcessRecipe neutral;
    if (!dsp_curated_values_valid(recipe, values)) {
        set_error(error, error_size, "Invalid curated DSP recipe values");
        return 0;
    }
    ts_process_recipe_reset(&neutral);
    neutral.seed = values->seed;
    if (!ts_dsp_transform_identity_capture(identity, instrument, scope,
                                            (int)recipe->kind, &neutral,
                                            job_id, render_generation,
                                            error, error_size)) return 0;
    identity->curated = 1;
    identity->recipe_index = (int)recipe->kind;
    identity->values = *values;
    return 1;
}

int ts_dsp_transform_identity_matches_recipe(
    const TsDspTransformIdentity *identity, const TsInstrument *instrument,
    TsTransformScope scope, const TsDspRecipe *recipe,
    const TsDspRecipeValues *values, uint64_t render_generation,
    char *error, size_t error_size)
{
    TsProcessRecipe neutral;
    if (identity == NULL || recipe == NULL || values == NULL ||
        !identity->curated || identity->recipe_index != (int)recipe->kind ||
        !ts_dsp_recipe_values_equal(&identity->values, values)) {
        set_error(error, error_size, "DSP TRANSFORM RESULT IS STALE");
        return 0;
    }
    ts_process_recipe_reset(&neutral);
    neutral.seed = values->seed;
    return ts_dsp_transform_identity_matches(identity, instrument, scope,
                                              (int)recipe->kind, &neutral,
                                              render_generation,
                                              error, error_size);
}

int ts_dsp_transform_prepare_preview(const TsInstrument *instrument,
                                     const TsDspTransformIdentity *identity,
                                     const TsSample *rendered,
                                     TsCdpSafetyStatus safety,
                                     float peak, double dc_offset,
                                     int clipped_samples,
                                     TsDspTransformPreview *preview,
                                     char *error, size_t error_size)
{
    size_t first;
    size_t last;
    if (instrument == NULL || identity == NULL || rendered == NULL ||
        rendered->data == NULL || rendered->frames == 0u || preview == NULL ||
        identity->tile_slot != instrument->selected_slot ||
        identity->audio_revision != ts_sample_hash(&instrument->current) ||
        identity->tile_frames != instrument->current.frames ||
        identity->selection_first != instrument->selection_first ||
        identity->selection_last != instrument->selection_last ||
        identity->has_selection != instrument->has_selection) {
        set_error(error, error_size, "No current native DSP render is ready");
        return 0;
    }
    first = identity->scope == TS_TRANSFORM_SELECTION ? identity->selection_first : 0u;
    last = identity->scope == TS_TRANSFORM_SELECTION ?
           identity->selection_last : instrument->current.frames;
    if (rendered->frames != last - first ||
        rendered->sample_rate != instrument->current.sample_rate) {
        set_error(error, error_size, "Native DSP preview has an invalid length or rate");
        return 0;
    }
    ts_dsp_transform_preview_free(preview);
    if (!ts_sample_clone(&preview->sample, rendered, error, error_size)) return 0;
    if (identity->scope == TS_TRANSFORM_SELECTION)
        ts_transform_boundary_splice(&preview->sample, &instrument->current, first, last);
    preview->identity = *identity;
    preview->replacement_first = first;
    preview->replacement_last = last;
    preview->safety = safety;
    preview->peak = peak;
    preview->dc_offset = dc_offset;
    preview->clipped_samples = clipped_samples;
    preview->valid = 1;
    set_error(error, error_size, "");
    return 1;
}

int ts_dsp_transform_apply_preview(TsInstrument *instrument,
                                   const TsDspTransformPreview *preview,
                                   TsTransformScope scope, int preset_slot,
                                   const TsProcessRecipe *process,
                                   uint64_t render_generation,
                                   char *error, size_t error_size)
{
    if (preview == NULL || !preview->valid || preview->sample.data == NULL) {
        set_error(error, error_size, "RENDER A VALID DSP PREVIEW BEFORE APPLY");
        return 0;
    }
    if (!ts_dsp_transform_identity_matches(&preview->identity, instrument, scope,
                                           preset_slot, process,
                                           render_generation,
                                           error, error_size)) return 0;
    return ts_instrument_apply_rendered_replacement(
        instrument, &preview->sample, preview->replacement_first,
        preview->replacement_last, error, error_size);
}

int ts_dsp_transform_apply_direct(TsInstrument *instrument, int preset_slot,
                                  const TsProcessRecipe *process,
                                  TsTransformScope scope,
                                  char *error, size_t error_size)
{
    TsDspTransformIdentity identity;
    TsDspTransformPreview preview;
    TsSample input;
    TsSample rendered;
    TsCdpSafetyStatus safety = TS_CDP_SAFETY_INVALID;
    float peak = 0.0f;
    double dc = 0.0;
    int clipped = 0;
    int ok;
    ts_sample_init(&input);
    ts_sample_init(&rendered);
    ts_dsp_transform_preview_init(&preview);
    ok = ts_dsp_transform_identity_capture(&identity, instrument, scope,
                                            preset_slot, process, 1u, 1u,
                                            error, error_size) &&
         ts_dsp_transform_extract_input(instrument, &identity, &input,
                                        error, error_size) &&
         ts_dsp_transform_render(&input, process, &rendered, &safety,
                                 &peak, &dc, &clipped, error, error_size) &&
         ts_dsp_transform_prepare_preview(instrument, &identity, &rendered,
                                          safety, peak, dc, clipped, &preview,
                                          error, error_size) &&
         ts_dsp_transform_apply_preview(instrument, &preview, scope,
                                        preset_slot, process, 1u,
                                        error, error_size);
    ts_dsp_transform_preview_free(&preview);
    ts_sample_free(&rendered);
    ts_sample_free(&input);
    return ok;
}

int ts_dsp_transform_apply_preview_recipe(
    TsInstrument *instrument, const TsDspTransformPreview *preview,
    TsTransformScope scope, const TsDspRecipe *recipe,
    const TsDspRecipeValues *values, uint64_t render_generation,
    char *error, size_t error_size)
{
    if (preview == NULL || !preview->valid || preview->sample.data == NULL) {
        set_error(error, error_size, "RENDER A VALID DSP PREVIEW BEFORE APPLY");
        return 0;
    }
    if (!ts_dsp_transform_identity_matches_recipe(
            &preview->identity, instrument, scope, recipe, values,
            render_generation, error, error_size)) return 0;
    return ts_instrument_apply_rendered_replacement(
        instrument, &preview->sample, preview->replacement_first,
        preview->replacement_last, error, error_size);
}

int ts_dsp_transform_apply_direct_recipe(
    TsInstrument *instrument, const TsDspRecipe *recipe,
    const TsDspRecipeValues *values, TsTransformScope scope,
    char *error, size_t error_size)
{
    TsDspTransformIdentity identity;
    TsDspTransformPreview preview;
    TsSample input;
    TsSample rendered;
    TsCdpSafetyStatus safety = TS_CDP_SAFETY_INVALID;
    float peak = 0.0f;
    double dc = 0.0;
    int clipped = 0;
    int ok;
    ts_sample_init(&input);
    ts_sample_init(&rendered);
    ts_dsp_transform_preview_init(&preview);
    ok = ts_dsp_transform_identity_capture_recipe(
             &identity, instrument, scope, recipe, values, 1u, 1u,
             error, error_size) &&
         ts_dsp_transform_extract_input(instrument, &identity, &input,
                                        error, error_size) &&
         ts_dsp_transform_render_recipe(&input, recipe, values, &rendered,
                                        &safety, &peak, &dc, &clipped,
                                        error, error_size) &&
         ts_dsp_transform_prepare_preview(instrument, &identity, &rendered,
                                          safety, peak, dc, clipped, &preview,
                                          error, error_size) &&
         ts_dsp_transform_apply_preview_recipe(
             instrument, &preview, scope, recipe, values, 1u,
             error, error_size);
    ts_dsp_transform_preview_free(&preview);
    ts_sample_free(&rendered);
    ts_sample_free(&input);
    return ok;
}
