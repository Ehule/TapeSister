#include "tapesister/cdp_recipe.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static const float glisten_divisions[] = {2.0f, 4.0f, 8.0f, 16.0f, 32.0f, 64.0f};

static const TsCdpRecipe factory_recipes[] = {
    {
        .id = "glisten",
        .display_name = "GLISTEN",
        .description = "Spectral groups shimmer in a changing sequence",
        .category = "SPECTRAL",
        .schema_version = 1u,
        .recipe_version = 1u,
        .stages = {
            {"pvoc", TS_CDP_IO_WAV, TS_CDP_IO_ANALYSIS},
            {"glisten", TS_CDP_IO_ANALYSIS, TS_CDP_IO_ANALYSIS},
            {"pvoc", TS_CDP_IO_ANALYSIS, TS_CDP_IO_WAV}
        },
        .stage_count = 3u,
        .controls = {
            {"divide", "DIVIDE", TS_CDP_CONTROL_ENUMERATED,
             2.0f, 64.0f, 8.0f, 0.0f,
             glisten_divisions, sizeof(glisten_divisions) / sizeof(glisten_divisions[0]),
             "GROUPS"},
            {"hold", "HOLD", TS_CDP_CONTROL_STEPPED,
             1.0f, 128.0f, 8.0f, 1.0f, NULL, 0u, "WINDOWS"},
            {"shift", "SHIFT", TS_CDP_CONTROL_CONTINUOUS,
             0.0f, 12.0f, 3.0f, 0.25f, NULL, 0u, "ST"},
            {"scatter", "SCATTER", TS_CDP_CONTROL_CONTINUOUS,
             0.0f, 1.0f, 0.28f, 0.01f, NULL, 0u, "%"}
        },
        .mix_policy = TS_CDP_MIX_UNSUPPORTED,
        .duration_may_change = 1,
        .required_input_channels = 1u,
        .expected_output_channels = 1u,
        .preserve_sample_rate = 1,
        .safety_policy = TS_CDP_SAFETY_ANALYZE_ONLY,
        .seed_supported = 0,
        .deterministic = 0,
        .analysis_points = 1024u,
        .analysis_overlap = 3u,
        .minimum_analysis_windows = 8u,
        .provenance_version = 1u
    }
};

static void set_error(char *error, size_t error_size, const char *message)
{
    if (error != NULL && error_size > 0u)
        snprintf(error, error_size, "%s", message != NULL ? message : "");
}

static float clampf(float value, float minimum, float maximum)
{
    return value < minimum ? minimum : value > maximum ? maximum : value;
}

size_t ts_cdp_factory_recipe_count(void)
{
    return sizeof(factory_recipes) / sizeof(factory_recipes[0]);
}

const TsCdpRecipe *ts_cdp_factory_recipe_at(size_t index)
{
    return index < ts_cdp_factory_recipe_count() ? &factory_recipes[index] : NULL;
}

const TsCdpRecipe *ts_cdp_recipe_find(const char *id)
{
    if (id == NULL) return NULL;
    for (size_t i = 0; i < ts_cdp_factory_recipe_count(); ++i)
        if (strcmp(factory_recipes[i].id, id) == 0) return &factory_recipes[i];
    return NULL;
}

int ts_cdp_recipe_validate(const TsCdpRecipe *recipe,
                           char *error, size_t error_size)
{
    if (recipe == NULL || recipe->id == NULL || recipe->id[0] == '\0' ||
        recipe->display_name == NULL || recipe->schema_version == 0u ||
        recipe->recipe_version == 0u || recipe->provenance_version == 0u) {
        set_error(error, error_size, "Recipe identity or version is invalid");
        return 0;
    }
    if (recipe->stage_count == 0u || recipe->stage_count > TS_CDP_MAX_STAGES) {
        set_error(error, error_size, "Recipe stage count is invalid");
        return 0;
    }
    if (recipe->stages[0].input_type != TS_CDP_IO_WAV ||
        recipe->stages[recipe->stage_count - 1u].output_type != TS_CDP_IO_WAV) {
        set_error(error, error_size, "Recipe must begin and end with WAV audio");
        return 0;
    }
    for (size_t i = 0; i < recipe->stage_count; ++i) {
        if (recipe->stages[i].executable == NULL ||
            recipe->stages[i].executable[0] == '\0') {
            set_error(error, error_size, "Recipe stage executable is missing");
            return 0;
        }
        if (i > 0u && recipe->stages[i - 1u].output_type !=
                      recipe->stages[i].input_type) {
            set_error(error, error_size, "Recipe stage types do not connect");
            return 0;
        }
    }
    for (size_t i = 0; i < TS_CDP_CONTROL_COUNT; ++i) {
        const TsCdpControlSpec *control = &recipe->controls[i];
        if (control->id == NULL || control->label == NULL ||
            !isfinite(control->minimum) || !isfinite(control->maximum) ||
            !isfinite(control->default_value) || control->minimum > control->maximum ||
            control->default_value < control->minimum ||
            control->default_value > control->maximum) {
            set_error(error, error_size, "Recipe control definition is invalid");
            return 0;
        }
        if (control->type == TS_CDP_CONTROL_ENUMERATED &&
            (control->valid_values == NULL || control->valid_value_count == 0u)) {
            set_error(error, error_size, "Enumerated control has no valid values");
            return 0;
        }
        if (control->type == TS_CDP_CONTROL_STEPPED && control->step <= 0.0f) {
            set_error(error, error_size, "Stepped control has no valid step");
            return 0;
        }
    }
    if (recipe->duration_may_change && recipe->mix_policy == TS_CDP_MIX_EXACT_FRAMES) {
        set_error(error, error_size, "Duration-changing recipe needs an explicit MIX policy");
        return 0;
    }
    if (recipe->required_input_channels != 1u ||
        recipe->expected_output_channels != 1u || !recipe->preserve_sample_rate ||
        recipe->safety_policy != TS_CDP_SAFETY_ANALYZE_ONLY) {
        set_error(error, error_size,
                  "Factory CDP recipe has unsupported audio or safety properties");
        return 0;
    }
    set_error(error, error_size, "");
    return 1;
}

void ts_cdp_recipe_values_default(const TsCdpRecipe *recipe,
                                  TsCdpRecipeValues *values)
{
    if (values == NULL) return;
    memset(values, 0, sizeof(*values));
    if (recipe == NULL) return;
    for (size_t i = 0; i < TS_CDP_CONTROL_COUNT; ++i)
        values->controls[i] = recipe->controls[i].default_value;
    values->mix = recipe->mix_policy == TS_CDP_MIX_UNSUPPORTED ? 1.0f : 0.75f;
}

float ts_cdp_control_quantize(const TsCdpControlSpec *control, float value)
{
    if (control == NULL || !isfinite(value))
        return control != NULL ? control->default_value : 0.0f;
    value = clampf(value, control->minimum, control->maximum);
    if (control->type == TS_CDP_CONTROL_ENUMERATED) {
        float closest = control->valid_values[0];
        float distance = fabsf(value - closest);
        for (size_t i = 1; i < control->valid_value_count; ++i) {
            float candidate = control->valid_values[i];
            float candidate_distance = fabsf(value - candidate);
            if (candidate_distance < distance) {
                closest = candidate;
                distance = candidate_distance;
            }
        }
        return closest;
    }
    if ((control->type == TS_CDP_CONTROL_STEPPED || control->step > 0.0f) &&
        control->step > 0.0f) {
        value = control->minimum +
                roundf((value - control->minimum) / control->step) * control->step;
        value = clampf(value, control->minimum, control->maximum);
    }
    return value;
}

int ts_cdp_recipe_set_control(const TsCdpRecipe *recipe,
                              TsCdpRecipeValues *values, size_t index,
                              float value)
{
    if (recipe == NULL || values == NULL || index >= TS_CDP_CONTROL_COUNT) return 0;
    values->controls[index] = ts_cdp_control_quantize(&recipe->controls[index], value);
    return 1;
}

void ts_cdp_control_format(const TsCdpControlSpec *control, float value,
                           uint32_t sample_rate, uint32_t analysis_points,
                           uint32_t analysis_overlap,
                           char *text, size_t text_size)
{
    float actual;
    if (text == NULL || text_size == 0u) return;
    if (control == NULL) { snprintf(text, text_size, "-"); return; }
    actual = ts_cdp_control_quantize(control, value);
    if (strcmp(control->id, "hold") == 0 && sample_rate > 0u &&
        analysis_points > 0u && analysis_overlap > 0u) {
        /* CDP overlap 3 uses an analysis hop of points/4. */
        uint32_t divisor = analysis_overlap == 1u ? 1u :
                           analysis_overlap == 2u ? 2u :
                           analysis_overlap == 3u ? 4u : 8u;
        double milliseconds = actual * (double)analysis_points * 1000.0 /
                              ((double)sample_rate * divisor);
        snprintf(text, text_size, "%d / %.0fMS", (int)lrintf(actual), milliseconds);
    } else if (strcmp(control->id, "scatter") == 0) {
        snprintf(text, text_size, "%d%%", (int)lrintf(actual * 100.0f));
    } else if (control->type == TS_CDP_CONTROL_ENUMERATED ||
               control->type == TS_CDP_CONTROL_STEPPED) {
        snprintf(text, text_size, "%d %s", (int)lrintf(actual),
                 control->unit != NULL ? control->unit : "");
    } else {
        snprintf(text, text_size, "%.2f %s", actual,
                 control->unit != NULL ? control->unit : "");
    }
}

int ts_cdp_glisten_map(const TsCdpRecipe *recipe,
                       const TsCdpRecipeValues *values,
                       TsCdpGlistenMapping *mapping,
                       char *error, size_t error_size)
{
    TsCdpRecipeValues safe;
    float scatter;
    if (recipe == NULL || values == NULL || mapping == NULL ||
        strcmp(recipe->id, "glisten") != 0) {
        set_error(error, error_size, "GLISTEN mapping needs the GLISTEN recipe");
        return 0;
    }
    safe = *values;
    for (size_t i = 0; i < TS_CDP_CONTROL_COUNT; ++i)
        safe.controls[i] = ts_cdp_control_quantize(&recipe->controls[i],
                                                   safe.controls[i]);
    scatter = safe.controls[3];
    mapping->divide = (int)lrintf(safe.controls[0]);
    mapping->hold_windows = (int)lrintf(safe.controls[1]);
    mapping->shift_semitones = safe.controls[2];
    /* SCATTER first loosens set duration, then increasingly varies group size.
       Both values remain within the CDP -d/-v [0,1] contract. */
    mapping->duration_randomization = scatter;
    mapping->division_randomization = scatter * scatter;
    set_error(error, error_size, "");
    return 1;
}

static void command_arg(TsCdpCommand *command, const char *value)
{
    if (command->argc >= TS_CDP_MAX_COMMAND_ARGS) return;
    snprintf(command->arguments[command->argc], TS_CDP_TEXT_MAX, "%s", value);
    ++command->argc;
}

int ts_cdp_glisten_build_commands(const TsCdpRecipe *recipe,
                                  const TsCdpRecipeValues *values,
                                  TsCdpCommand commands[3],
                                  char *error, size_t error_size)
{
    TsCdpGlistenMapping mapped;
    char value[TS_CDP_TEXT_MAX];
    if (commands == NULL || !ts_cdp_glisten_map(recipe, values, &mapped,
                                                 error, error_size)) return 0;
    memset(commands, 0, 3u * sizeof(*commands));

    snprintf(commands[0].executable, sizeof(commands[0].executable), "pvoc");
    command_arg(&commands[0], "anal");
    command_arg(&commands[0], "1");
    command_arg(&commands[0], "input.wav");
    command_arg(&commands[0], "input.ana");
    snprintf(value, sizeof(value), "-c%u", recipe->analysis_points);
    command_arg(&commands[0], value);
    snprintf(value, sizeof(value), "-o%u", recipe->analysis_overlap);
    command_arg(&commands[0], value);

    snprintf(commands[1].executable, sizeof(commands[1].executable), "glisten");
    command_arg(&commands[1], "glisten");
    command_arg(&commands[1], "input.ana");
    command_arg(&commands[1], "glisten.ana");
    snprintf(value, sizeof(value), "%d", mapped.divide);
    command_arg(&commands[1], value);
    snprintf(value, sizeof(value), "%d", mapped.hold_windows);
    command_arg(&commands[1], value);
    snprintf(value, sizeof(value), "-p%.4g", mapped.shift_semitones);
    command_arg(&commands[1], value);
    snprintf(value, sizeof(value), "-d%.4g", mapped.duration_randomization);
    command_arg(&commands[1], value);
    snprintf(value, sizeof(value), "-v%.4g", mapped.division_randomization);
    command_arg(&commands[1], value);

    snprintf(commands[2].executable, sizeof(commands[2].executable), "pvoc");
    command_arg(&commands[2], "synth");
    command_arg(&commands[2], "glisten.ana");
    command_arg(&commands[2], "output.wav");
    set_error(error, error_size, "");
    return 1;
}

int ts_cdp_recipe_input_valid(const TsCdpRecipe *recipe,
                              size_t frames, uint32_t sample_rate,
                              char *error, size_t error_size)
{
    size_t hop;
    size_t minimum;
    if (recipe == NULL || frames == 0u || sample_rate == 0u) {
        set_error(error, error_size, "Recipe input is empty");
        return 0;
    }
    hop = recipe->analysis_points /
          (recipe->analysis_overlap == 1u ? 1u :
           recipe->analysis_overlap == 2u ? 2u :
           recipe->analysis_overlap == 3u ? 4u : 8u);
    minimum = recipe->analysis_points +
              (recipe->minimum_analysis_windows > 1u ?
               (recipe->minimum_analysis_windows - 1u) * hop : 0u);
    if (frames < minimum) {
        char message[128];
        snprintf(message, sizeof(message),
                 "GLISTEN NEEDS AT LEAST %.0F MS",
                 (double)minimum * 1000.0 / (double)sample_rate);
        set_error(error, error_size, message);
        return 0;
    }
    set_error(error, error_size, "");
    return 1;
}
