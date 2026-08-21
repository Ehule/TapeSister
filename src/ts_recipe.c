#include "tapesister/recipe.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static void set_error(char *error, size_t size, const char *message)
{
    if (error != NULL && size > 0) snprintf(error, size, "%s", message);
}

static void put32(FILE *file, uint32_t value)
{
    unsigned char bytes[4] = {
        (unsigned char)value, (unsigned char)(value >> 8),
        (unsigned char)(value >> 16), (unsigned char)(value >> 24)
    };
    fwrite(bytes, 1, sizeof(bytes), file);
}

static int get32(FILE *file, uint32_t *value)
{
    unsigned char bytes[4];
    if (fread(bytes, 1, sizeof(bytes), file) != sizeof(bytes)) return 0;
    *value = (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
             ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
    return 1;
}

static void put_float(FILE *file, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    put32(file, bits);
}

static int get_float(FILE *file, float *value)
{
    uint32_t bits;
    if (!get32(file, &bits)) return 0;
    memcpy(value, &bits, sizeof(bits));
    return isfinite(*value);
}

int ts_recipe_process_valid(const TsProcessRecipe *p)
{
    return p != NULL && isfinite(p->body) && p->body >= 0.0f && p->body <= 1.0f &&
           isfinite(p->edge) && p->edge >= -1.0f && p->edge <= 1.0f &&
           isfinite(p->drift) && p->drift >= -1.0f && p->drift <= 1.0f &&
           (p->noise_enabled == 0 || p->noise_enabled == 1) &&
           p->noise_color >= TS_NOISE_WHITE && p->noise_color < TS_NOISE_COLOR_COUNT &&
           isfinite(p->noise_amount) && p->noise_amount >= 0.0f && p->noise_amount <= 1.0f &&
           (p->delay_enabled == 0 || p->delay_enabled == 1) &&
           isfinite(p->delay_seconds) && p->delay_seconds >= 0.005f && p->delay_seconds <= 1.0f &&
           isfinite(p->delay_feedback) && p->delay_feedback >= 0.0f && p->delay_feedback <= 0.85f &&
           isfinite(p->delay_damping) && p->delay_damping >= 0.0f && p->delay_damping <= 1.0f &&
           isfinite(p->delay_mix) && p->delay_mix >= 0.0f && p->delay_mix <= 1.0f &&
           (p->reverb_enabled == 0 || p->reverb_enabled == 1) &&
           isfinite(p->reverb_decay) && p->reverb_decay >= 0.0f && p->reverb_decay <= 0.9f &&
           isfinite(p->reverb_damping) && p->reverb_damping >= 0.0f && p->reverb_damping <= 1.0f &&
           isfinite(p->reverb_mix) && p->reverb_mix >= 0.0f && p->reverb_mix <= 1.0f &&
           (p->filter_enabled == 0 || p->filter_enabled == 1) &&
           p->filter_mode >= TS_FILTER_LOWPASS && p->filter_mode < TS_FILTER_MODE_COUNT &&
           isfinite(p->filter_cutoff_hz) && p->filter_cutoff_hz >= 20.0f &&
           p->filter_cutoff_hz <= 20000.0f && isfinite(p->filter_resonance) &&
           p->filter_resonance >= 0.0f && p->filter_resonance <= 1.0f &&
           (p->shaper_enabled == 0 || p->shaper_enabled == 1) &&
           p->shaper_mode >= TS_SHAPER_TAPE && p->shaper_mode < TS_SHAPER_MODE_COUNT &&
           isfinite(p->shaper_drive) && p->shaper_drive >= 1.0f && p->shaper_drive <= 16.0f &&
           isfinite(p->shaper_mix) && p->shaper_mix >= 0.0f && p->shaper_mix <= 1.0f;
}

int ts_process_recipe_equal(const TsProcessRecipe *a, const TsProcessRecipe *b)
{
    return a != NULL && b != NULL &&
           a->seed == b->seed && a->body == b->body && a->edge == b->edge &&
           a->drift == b->drift && a->noise_enabled == b->noise_enabled &&
           a->noise_amount == b->noise_amount && a->noise_color == b->noise_color &&
           a->delay_enabled == b->delay_enabled &&
           a->delay_seconds == b->delay_seconds &&
           a->delay_feedback == b->delay_feedback &&
           a->delay_damping == b->delay_damping && a->delay_mix == b->delay_mix &&
           a->reverb_enabled == b->reverb_enabled &&
           a->reverb_decay == b->reverb_decay &&
           a->reverb_damping == b->reverb_damping && a->reverb_mix == b->reverb_mix &&
           a->filter_enabled == b->filter_enabled && a->filter_mode == b->filter_mode &&
           a->filter_cutoff_hz == b->filter_cutoff_hz &&
           a->filter_resonance == b->filter_resonance &&
           a->shaper_enabled == b->shaper_enabled && a->shaper_mode == b->shaper_mode &&
           a->shaper_drive == b->shaper_drive && a->shaper_mix == b->shaper_mix;
}

#define DSP_PERCENT(label_) {label_, 0.0f, 1.0f, 0.01f, 0, TS_DSP_VALUE_PERCENT}
#define DSP_SECONDS(label_, lo_, hi_) {label_, lo_, hi_, 0.01f, 0, TS_DSP_VALUE_SECONDS}
#define DSP_HERTZ(label_, lo_, hi_) {label_, lo_, hi_, 0.01f, 1, TS_DSP_VALUE_HERTZ}
#define DSP_DRIVE(label_, lo_, hi_) {label_, lo_, hi_, 0.01f, 0, TS_DSP_VALUE_DRIVE}

static const TsDspPresetSpec dsp_specs[TS_DSP_PROFILE_COUNT] = {
    {TS_DSP_PROFILE_NEUTRAL, "CLEAN BODY, EDGE AND ORGANIC MOTION", 3,
     {DSP_PERCENT("BODY"), DSP_PERCENT("EDGE"), DSP_PERCENT("DRIFT"), DSP_PERCENT("")}},
    {TS_DSP_PROFILE_WARM, "TAPE WEIGHT AND SATURATED EDGES", 4,
     {DSP_PERCENT("BODY"), DSP_DRIVE("DRIVE", 1.0f, 12.0f),
      DSP_PERCENT("EDGE"), DSP_PERCENT("MIX")}},
    {TS_DSP_PROFILE_DARK, "LOW-PASSED MASS WITH SLOW MOTION", 4,
     {DSP_PERCENT("BODY"), DSP_HERTZ("CUTOFF", 80.0f, 6000.0f),
      DSP_PERCENT("RES"), DSP_PERCENT("DRIFT")}},
    {TS_DSP_PROFILE_BRIGHT, "HIGH-PASSED DUST AND SHARP DETAIL", 4,
     {DSP_HERTZ("CUTOFF", 200.0f, 9000.0f),
      {"DUST", 0.0f, 0.4f, 0.01f, 0, TS_DSP_VALUE_PERCENT},
      DSP_PERCENT("EDGE"), DSP_PERCENT("DRIFT")}},
    {TS_DSP_PROFILE_DUB, "DAMPED REPEATS WITH DIRECT MUSICAL TIMING", 4,
     {DSP_SECONDS("TIME", 0.02f, 1.0f),
      {"FEEDBACK", 0.0f, 0.85f, 0.01f, 0, TS_DSP_VALUE_PERCENT},
      DSP_PERCENT("TONE"), DSP_PERCENT("MIX")}},
    {TS_DSP_PROFILE_HOLLOW, "RESONANT FOCUS INSIDE A SHORT SPACE", 4,
     {DSP_HERTZ("FOCUS", 100.0f, 7000.0f), DSP_PERCENT("RES"),
      {"SPACE", 0.0f, 0.9f, 0.01f, 0, TS_DSP_VALUE_PERCENT},
      DSP_PERCENT("MIX")}},
    {TS_DSP_PROFILE_HARD, "CLIPPED PRESSURE WITH CONTROLLED WEIGHT", 4,
     {DSP_DRIVE("DRIVE", 1.0f, 16.0f), DSP_PERCENT("EDGE"),
      DSP_PERCENT("BODY"), DSP_PERCENT("MIX")}},
    {TS_DSP_PROFILE_BROKEN, "FOLDED METAL, DUST AND UNSTABLE MOTION", 4,
     {DSP_DRIVE("FOLD", 1.0f, 16.0f),
      {"DUST", 0.0f, 0.4f, 0.01f, 0, TS_DSP_VALUE_PERCENT},
      DSP_PERCENT("DRIFT"), DSP_PERCENT("MIX")}},
    {TS_DSP_PROFILE_GENERIC, "BODY, EDGE AND ORGANIC MOTION", 3,
     {DSP_PERCENT("BODY"), DSP_PERCENT("EDGE"), DSP_PERCENT("DRIFT"), DSP_PERCENT("")}}
};

#undef DSP_PERCENT
#undef DSP_SECONDS
#undef DSP_HERTZ
#undef DSP_DRIVE

static float clamp_unit(float value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

const TsDspPresetSpec *ts_dsp_preset_spec(TsDspProfile profile)
{
    if (profile < 0 || profile >= TS_DSP_PROFILE_COUNT)
        profile = TS_DSP_PROFILE_GENERIC;
    return &dsp_specs[profile];
}

float ts_dsp_control_value(const TsDspControlSpec *control, float normalized)
{
    normalized = clamp_unit(normalized);
    if (control == NULL) return normalized;
    if (control->logarithmic && control->minimum > 0.0f &&
        control->maximum > control->minimum)
        return expf(logf(control->minimum) + normalized *
                    logf(control->maximum / control->minimum));
    return control->minimum + normalized * (control->maximum - control->minimum);
}

static float dsp_normalized(const TsDspControlSpec *control, float value)
{
    if (control == NULL || control->maximum <= control->minimum) return 0.0f;
    if (control->logarithmic && control->minimum > 0.0f && value > 0.0f)
        return clamp_unit(logf(value / control->minimum) /
                          logf(control->maximum / control->minimum));
    return clamp_unit((value - control->minimum) /
                      (control->maximum - control->minimum));
}

void ts_dsp_control_format(const TsDspControlSpec *control, float normalized,
                           char *text, size_t text_size)
{
    float value = ts_dsp_control_value(control, normalized);
    if (text == NULL || text_size == 0u) return;
    if (control == NULL) snprintf(text, text_size, "---");
    else if (control->format == TS_DSP_VALUE_SECONDS) {
        if (value < 1.0f) snprintf(text, text_size, "%dMS", (int)lrintf(value * 1000.0f));
        else snprintf(text, text_size, "%.2FS", value);
    } else if (control->format == TS_DSP_VALUE_HERTZ) {
        if (value >= 1000.0f) snprintf(text, text_size, "%.1FK", value / 1000.0f);
        else snprintf(text, text_size, "%dHZ", (int)lrintf(value));
    }
    else if (control->format == TS_DSP_VALUE_DRIVE)
        snprintf(text, text_size, "X%.1F", value);
    else
        snprintf(text, text_size, "%d%%",
                 (int)lrintf(value / (control->maximum > 0.0f ?
                                      control->maximum : 1.0f) * 100.0f));
}

static void apply_dsp_controls(TsPortableRecipe *recipe)
{
    TsProcessRecipe *p;
    const TsDspPresetSpec *spec;
    float a, b, c, d;
    if (recipe == NULL) return;
    p = &recipe->process;
    spec = ts_dsp_preset_spec(recipe->dsp_profile);
    a = ts_dsp_control_value(&spec->controls[0], recipe->dsp_controls[0]);
    b = ts_dsp_control_value(&spec->controls[1], recipe->dsp_controls[1]);
    c = ts_dsp_control_value(&spec->controls[2], recipe->dsp_controls[2]);
    d = ts_dsp_control_value(&spec->controls[3], recipe->dsp_controls[3]);
    switch (recipe->dsp_profile) {
    case TS_DSP_PROFILE_WARM:
        p->body = a; p->shaper_enabled = 1; p->shaper_mode = TS_SHAPER_TAPE;
        p->shaper_drive = b; p->edge = c; p->shaper_mix = d; break;
    case TS_DSP_PROFILE_DARK:
        p->body = a; p->filter_enabled = 1; p->filter_mode = TS_FILTER_LOWPASS;
        p->filter_cutoff_hz = b; p->filter_resonance = c; p->drift = d; break;
    case TS_DSP_PROFILE_BRIGHT:
        p->filter_enabled = 1; p->filter_mode = TS_FILTER_HIGHPASS;
        p->filter_cutoff_hz = a; p->noise_enabled = b > 0.0001f;
        p->noise_amount = b; p->edge = c; p->drift = d; break;
    case TS_DSP_PROFILE_DUB:
        p->delay_enabled = 1; p->delay_seconds = a; p->delay_feedback = b;
        p->delay_damping = 1.0f - c; p->delay_mix = d; break;
    case TS_DSP_PROFILE_HOLLOW:
        p->filter_enabled = 1; p->filter_mode = TS_FILTER_BANDPASS;
        p->filter_cutoff_hz = a; p->filter_resonance = b;
        p->reverb_enabled = 1; p->reverb_decay = c; p->reverb_mix = d; break;
    case TS_DSP_PROFILE_HARD:
        p->shaper_enabled = 1; p->shaper_mode = TS_SHAPER_CLIP;
        p->shaper_drive = a; p->edge = b; p->body = c; p->shaper_mix = d; break;
    case TS_DSP_PROFILE_BROKEN:
        p->shaper_enabled = 1; p->shaper_mode = TS_SHAPER_FOLD;
        p->shaper_drive = a; p->noise_enabled = b > 0.0001f;
        p->noise_color = TS_NOISE_METALLIC; p->noise_amount = b;
        p->drift = c; p->shaper_mix = d; break;
    case TS_DSP_PROFILE_NEUTRAL:
    case TS_DSP_PROFILE_GENERIC:
    default:
        p->body = a; p->edge = b; p->drift = c; break;
    }
}

void ts_dsp_preset_bind(TsPortableRecipe *recipe, TsDspProfile profile)
{
    const TsDspPresetSpec *spec;
    TsProcessRecipe *p;
    if (recipe == NULL) return;
    if (profile < 0 || profile >= TS_DSP_PROFILE_COUNT)
        profile = TS_DSP_PROFILE_GENERIC;
    recipe->dsp_profile = profile;
    recipe->has_dsp_controls = 1;
    p = &recipe->process;
    spec = ts_dsp_preset_spec(profile);
    switch (profile) {
    case TS_DSP_PROFILE_WARM:
        recipe->dsp_controls[0] = p->body;
        recipe->dsp_controls[1] = dsp_normalized(&spec->controls[1], p->shaper_drive);
        recipe->dsp_controls[2] = p->edge; recipe->dsp_controls[3] = p->shaper_mix; break;
    case TS_DSP_PROFILE_DARK:
        recipe->dsp_controls[0] = p->body;
        recipe->dsp_controls[1] = dsp_normalized(&spec->controls[1], p->filter_cutoff_hz);
        recipe->dsp_controls[2] = p->filter_resonance; recipe->dsp_controls[3] = p->drift; break;
    case TS_DSP_PROFILE_BRIGHT:
        recipe->dsp_controls[0] = dsp_normalized(&spec->controls[0], p->filter_cutoff_hz);
        recipe->dsp_controls[1] = dsp_normalized(&spec->controls[1], p->noise_amount);
        recipe->dsp_controls[2] = p->edge; recipe->dsp_controls[3] = p->drift; break;
    case TS_DSP_PROFILE_DUB:
        recipe->dsp_controls[0] = dsp_normalized(&spec->controls[0], p->delay_seconds);
        recipe->dsp_controls[1] = dsp_normalized(&spec->controls[1], p->delay_feedback);
        recipe->dsp_controls[2] = 1.0f - p->delay_damping;
        recipe->dsp_controls[3] = p->delay_mix; break;
    case TS_DSP_PROFILE_HOLLOW:
        recipe->dsp_controls[0] = dsp_normalized(&spec->controls[0], p->filter_cutoff_hz);
        recipe->dsp_controls[1] = p->filter_resonance;
        recipe->dsp_controls[2] = dsp_normalized(&spec->controls[2], p->reverb_decay);
        recipe->dsp_controls[3] = p->reverb_mix; break;
    case TS_DSP_PROFILE_HARD:
        recipe->dsp_controls[0] = dsp_normalized(&spec->controls[0], p->shaper_drive);
        recipe->dsp_controls[1] = p->edge; recipe->dsp_controls[2] = p->body;
        recipe->dsp_controls[3] = p->shaper_mix; break;
    case TS_DSP_PROFILE_BROKEN:
        recipe->dsp_controls[0] = dsp_normalized(&spec->controls[0], p->shaper_drive);
        recipe->dsp_controls[1] = dsp_normalized(&spec->controls[1], p->noise_amount);
        recipe->dsp_controls[2] = p->drift; recipe->dsp_controls[3] = p->shaper_mix; break;
    case TS_DSP_PROFILE_NEUTRAL:
    case TS_DSP_PROFILE_GENERIC:
    default:
        recipe->dsp_controls[0] = p->body; recipe->dsp_controls[1] = p->edge;
        recipe->dsp_controls[2] = p->drift; recipe->dsp_controls[3] = 0.0f; break;
    }
    for (size_t i = 0; i < TS_DSP_CONTROL_COUNT; ++i)
        recipe->dsp_controls[i] = clamp_unit(recipe->dsp_controls[i]);
}

int ts_dsp_preset_set_control(TsPortableRecipe *recipe, size_t index,
                              float normalized)
{
    const TsDspPresetSpec *spec;
    if (recipe == NULL || index >= TS_DSP_CONTROL_COUNT || !isfinite(normalized))
        return 0;
    spec = ts_dsp_preset_spec(recipe->dsp_profile);
    if (index >= spec->control_count) return 0;
    recipe->dsp_controls[index] = clamp_unit(normalized);
    recipe->has_dsp_controls = 1;
    apply_dsp_controls(recipe);
    return ts_recipe_process_valid(&recipe->process);
}

int ts_dsp_preset_set_controls(TsPortableRecipe *recipe,
                               const float controls[TS_DSP_CONTROL_COUNT])
{
    const TsDspPresetSpec *spec;
    if (recipe == NULL || controls == NULL) return 0;
    spec = ts_dsp_preset_spec(recipe->dsp_profile);
    for (size_t i = 0; i < TS_DSP_CONTROL_COUNT; ++i) {
        if (!isfinite(controls[i])) return 0;
        recipe->dsp_controls[i] = i < spec->control_count ?
                                  clamp_unit(controls[i]) : 0.0f;
    }
    recipe->has_dsp_controls = 1;
    apply_dsp_controls(recipe);
    return ts_recipe_process_valid(&recipe->process);
}

int ts_recipe_from_process(TsPortableRecipe *recipe, const TsProcessRecipe *process,
                           const char *name)
{
    if (recipe == NULL || !ts_recipe_process_valid(process)) return 0;
    memset(recipe, 0, sizeof(*recipe));
    recipe->process = *process;
    recipe->occupied = 1;
    snprintf(recipe->name, sizeof(recipe->name), "%s",
             name != NULL && name[0] != '\0' ? name : "UNTITLED");
    ts_dsp_preset_bind(recipe, TS_DSP_PROFILE_GENERIC);
    return 1;
}

int ts_recipe_from_process_and_tuning(TsPortableRecipe *recipe,
                                      const TsProcessRecipe *process,
                                      const TsTuning *tuning, const char *name)
{
    return ts_recipe_from_process_and_tunings(recipe, process, tuning, tuning, name);
}

int ts_recipe_from_process_and_tunings(TsPortableRecipe *recipe,
                                       const TsProcessRecipe *process,
                                       const TsTuning *tuning,
                                       const TsTuning *audible_tuning,
                                       const char *name)
{
    if (!ts_recipe_from_process(recipe, process, name) || tuning == NULL ||
        tuning->root_note < 0 || tuning->root_note > 127 ||
        !isfinite(tuning->fine_tune_cents) ||
        tuning->fine_tune_cents < -100.0f || tuning->fine_tune_cents > 100.0f ||
        audible_tuning == NULL || audible_tuning->root_note < 0 ||
        audible_tuning->root_note > 127 ||
        !isfinite(audible_tuning->fine_tune_cents) ||
        audible_tuning->fine_tune_cents < -100.0f ||
        audible_tuning->fine_tune_cents > 100.0f)
        return 0;
    recipe->tuning = *tuning;
    recipe->audible_tuning = *audible_tuning;
    recipe->has_tuning = 1;
    return 1;
}

static void factory(TsPortableRecipe *slot, const char *name,
                    float body, float edge, float drift)
{
    ts_process_recipe_reset(&slot->process);
    slot->process.body = body;
    slot->process.edge = edge;
    slot->process.drift = drift;
    slot->factory = 1;
    slot->occupied = 1;
    snprintf(slot->name, sizeof(slot->name), "%s", name);
}

void ts_recipe_bank_init(TsRecipeBank *bank)
{
    if (bank == NULL) return;
    memset(bank, 0, sizeof(*bank));
    bank->active_slot = -1;
    factory(&bank->slots[0], "NEUTRAL", 0.5f, 0.0f, 0.0f);
    factory(&bank->slots[1], "WARM TAPE", 0.78f, 0.18f, 0.08f);
    bank->slots[1].process.shaper_enabled = 1;
    bank->slots[1].process.shaper_mode = TS_SHAPER_TAPE;
    bank->slots[1].process.shaper_drive = 3.2f;
    bank->slots[1].process.shaper_mix = 0.72f;
    factory(&bank->slots[2], "DARK DRONE", 0.92f, 0.08f, 0.32f);
    bank->slots[2].process.filter_enabled = 1;
    bank->slots[2].process.filter_cutoff_hz = 950.0f;
    bank->slots[2].process.filter_resonance = 0.42f;
    factory(&bank->slots[3], "BRIGHT DUST", 0.42f, 0.66f, 0.18f);
    bank->slots[3].process.filter_enabled = 1;
    bank->slots[3].process.filter_mode = TS_FILTER_HIGHPASS;
    bank->slots[3].process.filter_cutoff_hz = 1250.0f;
    bank->slots[3].process.noise_enabled = 1;
    bank->slots[3].process.noise_amount = 0.12f;
    factory(&bank->slots[4], "DUB ECHO", 0.64f, 0.22f, 0.10f);
    bank->slots[4].process.delay_enabled = 1;
    bank->slots[4].process.delay_seconds = 0.31f;
    bank->slots[4].process.delay_feedback = 0.64f;
    bank->slots[4].process.delay_damping = 0.66f;
    bank->slots[4].process.delay_mix = 0.42f;
    factory(&bank->slots[5], "HOLLOW SPACE", 0.52f, 0.20f, 0.14f);
    bank->slots[5].process.filter_enabled = 1;
    bank->slots[5].process.filter_mode = TS_FILTER_BANDPASS;
    bank->slots[5].process.filter_cutoff_hz = 1450.0f;
    bank->slots[5].process.filter_resonance = 0.68f;
    bank->slots[5].process.reverb_enabled = 1;
    bank->slots[5].process.reverb_decay = 0.76f;
    bank->slots[5].process.reverb_mix = 0.48f;
    factory(&bank->slots[6], "HARD CLIP", 0.50f, 0.38f, 0.04f);
    bank->slots[6].process.shaper_enabled = 1;
    bank->slots[6].process.shaper_mode = TS_SHAPER_CLIP;
    bank->slots[6].process.shaper_drive = 8.5f;
    bank->slots[6].process.shaper_mix = 0.88f;
    factory(&bank->slots[7], "BROKEN FOLD", 0.58f, 0.48f, 0.44f);
    bank->slots[7].process.shaper_enabled = 1;
    bank->slots[7].process.shaper_mode = TS_SHAPER_FOLD;
    bank->slots[7].process.shaper_drive = 6.0f;
    bank->slots[7].process.shaper_mix = 0.82f;
    bank->slots[7].process.noise_enabled = 1;
    bank->slots[7].process.noise_color = TS_NOISE_METALLIC;
    bank->slots[7].process.noise_amount = 0.10f;
    for (int slot = 0; slot < TS_FACTORY_RECIPE_COUNT; ++slot)
        ts_dsp_preset_bind(&bank->slots[slot], (TsDspProfile)slot);
}

int ts_recipe_bank_capture(TsRecipeBank *bank, int slot, const TsProcessRecipe *process,
                           const TsTuning *tuning, const TsTuning *audible_tuning,
                           const char *name,
                           char *error, size_t error_size)
{
    if (bank == NULL || slot < TS_FACTORY_RECIPE_COUNT || slot >= TS_RECIPE_SLOT_COUNT) {
        set_error(error, error_size, "Choose a user recipe slot 09-16");
        return 0;
    }
    if (bank->slots[slot].occupied) {
        set_error(error, error_size, "Clear the user recipe slot before capturing");
        return 0;
    }
    if (!ts_recipe_from_process_and_tunings(&bank->slots[slot], process, tuning,
                                            audible_tuning, name)) {
        set_error(error, error_size, "Invalid processing recipe");
        return 0;
    }
    bank->active_slot = slot;
    set_error(error, error_size, "");
    return 1;
}

int ts_recipe_bank_clear(TsRecipeBank *bank, int slot, char *error, size_t error_size)
{
    if (bank == NULL || slot < TS_FACTORY_RECIPE_COUNT || slot >= TS_RECIPE_SLOT_COUNT ||
        !bank->slots[slot].occupied) {
        set_error(error, error_size, "Only occupied user recipes can be cleared");
        return 0;
    }
    memset(&bank->slots[slot], 0, sizeof(bank->slots[slot]));
    if (bank->active_slot == slot) bank->active_slot = -1;
    set_error(error, error_size, "");
    return 1;
}

int ts_recipe_bank_rename(TsRecipeBank *bank, int slot, const char *name,
                          char *error, size_t error_size)
{
    const char *first;
    const char *last;
    size_t length;
    if (bank == NULL || slot < TS_FACTORY_RECIPE_COUNT || slot >= TS_RECIPE_SLOT_COUNT ||
        !bank->slots[slot].occupied) {
        set_error(error, error_size, "Only occupied user recipes can be renamed");
        return 0;
    }
    first = name != NULL ? name : "";
    while (*first == ' ' || *first == '\t') ++first;
    last = first + strlen(first);
    while (last > first && (last[-1] == ' ' || last[-1] == '\t')) --last;
    length = (size_t)(last - first);
    if (length == 0 || length > TS_RECIPE_NAME_MAX) {
        set_error(error, error_size, "Recipe name must be 1-31 characters");
        return 0;
    }
    memcpy(bank->slots[slot].name, first, length);
    bank->slots[slot].name[length] = '\0';
    set_error(error, error_size, "");
    return 1;
}

int ts_recipe_bank_add_user(TsRecipeBank *bank, const TsPortableRecipe *recipe,
                            char *error, size_t error_size)
{
    if (bank == NULL || recipe == NULL || !recipe->occupied ||
        !ts_recipe_process_valid(&recipe->process) ||
        (recipe->has_tuning &&
         (recipe->tuning.root_note < 0 || recipe->tuning.root_note > 127 ||
          !isfinite(recipe->tuning.fine_tune_cents) ||
          recipe->tuning.fine_tune_cents < -100.0f ||
          recipe->tuning.fine_tune_cents > 100.0f ||
          recipe->audible_tuning.root_note < 0 ||
          recipe->audible_tuning.root_note > 127 ||
          !isfinite(recipe->audible_tuning.fine_tune_cents) ||
          recipe->audible_tuning.fine_tune_cents < -100.0f ||
          recipe->audible_tuning.fine_tune_cents > 100.0f))) {
        set_error(error, error_size, "Invalid portable recipe");
        return 0;
    }
    for (int slot = TS_FACTORY_RECIPE_COUNT; slot < TS_RECIPE_SLOT_COUNT; ++slot) {
        if (!bank->slots[slot].occupied) {
            bank->slots[slot] = *recipe;
            bank->slots[slot].factory = 0;
            bank->active_slot = slot;
            set_error(error, error_size, "");
            return slot + 1;
        }
    }
    set_error(error, error_size, "Clear a user recipe slot before loading another");
    return 0;
}

static void write_process(FILE *file, const TsProcessRecipe *p)
{
    put32(file, p->seed);
    put_float(file, p->body); put_float(file, p->edge); put_float(file, p->drift);
    put32(file, (uint32_t)p->noise_enabled); put_float(file, p->noise_amount);
    put32(file, (uint32_t)p->noise_color);
    put32(file, (uint32_t)p->delay_enabled); put_float(file, p->delay_seconds);
    put_float(file, p->delay_feedback); put_float(file, p->delay_damping);
    put_float(file, p->delay_mix);
    put32(file, (uint32_t)p->reverb_enabled); put_float(file, p->reverb_decay);
    put_float(file, p->reverb_damping); put_float(file, p->reverb_mix);
    put32(file, (uint32_t)p->filter_enabled); put32(file, (uint32_t)p->filter_mode);
    put_float(file, p->filter_cutoff_hz); put_float(file, p->filter_resonance);
    put32(file, (uint32_t)p->shaper_enabled); put32(file, (uint32_t)p->shaper_mode);
    put_float(file, p->shaper_drive); put_float(file, p->shaper_mix);
}

static int read_process(FILE *file, TsProcessRecipe *p)
{
    uint32_t value;
#define R32(dst) do { if (!get32(file, &value)) return 0; (dst) = value; } while (0)
#define RF(dst) do { if (!get_float(file, &(dst))) return 0; } while (0)
    R32(p->seed); RF(p->body); RF(p->edge); RF(p->drift);
    R32(p->noise_enabled); RF(p->noise_amount); R32(p->noise_color);
    R32(p->delay_enabled); RF(p->delay_seconds); RF(p->delay_feedback);
    RF(p->delay_damping); RF(p->delay_mix);
    R32(p->reverb_enabled); RF(p->reverb_decay); RF(p->reverb_damping); RF(p->reverb_mix);
    R32(p->filter_enabled); R32(p->filter_mode); RF(p->filter_cutoff_hz);
    RF(p->filter_resonance); R32(p->shaper_enabled); R32(p->shaper_mode);
    RF(p->shaper_drive); RF(p->shaper_mix);
#undef R32
#undef RF
    return ts_recipe_process_valid(p);
}

int ts_recipe_save(const TsPortableRecipe *recipe, const char *path,
                   char *error, size_t error_size)
{
    FILE *file;
    size_t length;
    if (recipe == NULL || path == NULL || !recipe->occupied ||
        !ts_recipe_process_valid(&recipe->process)) {
        set_error(error, error_size, "No valid processing recipe to save");
        return 0;
    }
    for (length = 0; length < sizeof(recipe->name) && recipe->name[length] != '\0';
         ++length) {}
    if (length == 0 || length > TS_RECIPE_NAME_MAX) {
        set_error(error, error_size, "Processing recipe needs a valid name");
        return 0;
    }
    file = fopen(path, "wb");
    if (file == NULL) {
        set_error(error, error_size, "Could not create TSP recipe");
        return 0;
    }
    fwrite("TSP2\r\n\032\n", 1, 8, file);
    put32(file, (uint32_t)length);
    fwrite(recipe->name, 1, length, file);
    write_process(file, &recipe->process);
    put32(file, (uint32_t)recipe->has_tuning);
    if (recipe->has_tuning) {
        put32(file, (uint32_t)recipe->tuning.root_note);
        put_float(file, recipe->tuning.fine_tune_cents);
        put32(file, (uint32_t)recipe->audible_tuning.root_note);
        put_float(file, recipe->audible_tuning.fine_tune_cents);
    }
    {
        int failed = ferror(file);
        if (fclose(file) != 0) failed = 1;
        if (failed) {
            set_error(error, error_size, "Could not finish TSP recipe");
            return 0;
        }
    }
    set_error(error, error_size, "");
    return 1;
}

int ts_recipe_load(TsPortableRecipe *recipe, const char *path,
                   char *error, size_t error_size)
{
    FILE *file;
    char magic[8];
    uint32_t length;
    uint32_t value;
    int version;
    TsPortableRecipe loaded;
    if (recipe == NULL || path == NULL) {
        set_error(error, error_size, "No TSP recipe path");
        return 0;
    }
    memset(&loaded, 0, sizeof(loaded));
    file = fopen(path, "rb");
    if (file == NULL) {
        set_error(error, error_size, "Could not open TSP recipe");
        return 0;
    }
    if (fread(magic, 1, sizeof(magic), file) != sizeof(magic)) goto malformed;
    if (memcmp(magic, "TSP2\r\n\032\n", 8) == 0) version = 2;
    else if (memcmp(magic, "TSP1\r\n\032\n", 8) == 0) version = 1;
    else goto malformed;
    if (!get32(file, &length) ||
        length == 0 || length > TS_RECIPE_NAME_MAX ||
        fread(loaded.name, 1, length, file) != length) goto malformed;
    loaded.name[length] = '\0';
    if (!read_process(file, &loaded.process)) goto malformed;
    if (version >= 2) {
        if (!get32(file, &value) || value > 1u) goto malformed;
        loaded.has_tuning = (int)value;
        if (loaded.has_tuning) {
            if (!get32(file, &value) || value > 127u ||
                !get_float(file, &loaded.tuning.fine_tune_cents) ||
                loaded.tuning.fine_tune_cents < -100.0f ||
                loaded.tuning.fine_tune_cents > 100.0f) goto malformed;
            loaded.tuning.root_note = (int)value;
            if (!get32(file, &value) || value > 127u ||
                !get_float(file, &loaded.audible_tuning.fine_tune_cents) ||
                loaded.audible_tuning.fine_tune_cents < -100.0f ||
                loaded.audible_tuning.fine_tune_cents > 100.0f) goto malformed;
            loaded.audible_tuning.root_note = (int)value;
        }
    }
    if (fgetc(file) != EOF) goto malformed;
    loaded.occupied = 1;
    fclose(file);
    *recipe = loaded;
    set_error(error, error_size, "");
    return 1;
malformed:
    fclose(file);
    set_error(error, error_size, "Malformed or unsupported TSP1/TSP2 recipe");
    return 0;
}
