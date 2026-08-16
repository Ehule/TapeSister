#ifndef TAPESISTER_CDP_RECIPE_H
#define TAPESISTER_CDP_RECIPE_H

#include <stddef.h>
#include <stdint.h>

enum {
    TS_CDP_CONTROL_COUNT = 4,
    TS_CDP_MAX_STAGES = 4,
    TS_CDP_MAX_COMMAND_ARGS = 12,
    TS_CDP_TEXT_MAX = 64
};

typedef enum {
    TS_CDP_CONTROL_CONTINUOUS = 0,
    TS_CDP_CONTROL_STEPPED,
    TS_CDP_CONTROL_BIPOLAR,
    TS_CDP_CONTROL_ENUMERATED
} TsCdpControlType;

typedef enum {
    TS_CDP_IO_WAV = 0,
    TS_CDP_IO_ANALYSIS
} TsCdpIoType;

typedef enum {
    TS_CDP_MIX_UNSUPPORTED = 0,
    TS_CDP_MIX_EXACT_FRAMES
} TsCdpMixPolicy;

typedef enum {
    TS_CDP_SAFETY_ANALYZE_ONLY = 0
} TsCdpRecipeSafetyPolicy;

typedef struct {
    const char *id;
    const char *label;
    TsCdpControlType type;
    float minimum;
    float maximum;
    float default_value;
    float step;
    const float *valid_values;
    size_t valid_value_count;
    const char *unit;
} TsCdpControlSpec;

typedef struct {
    const char *executable;
    TsCdpIoType input_type;
    TsCdpIoType output_type;
} TsCdpStageSpec;

typedef struct {
    const char *id;
    const char *display_name;
    const char *description;
    const char *category;
    uint32_t schema_version;
    uint32_t recipe_version;
    TsCdpStageSpec stages[TS_CDP_MAX_STAGES];
    size_t stage_count;
    TsCdpControlSpec controls[TS_CDP_CONTROL_COUNT];
    TsCdpMixPolicy mix_policy;
    int duration_may_change;
    uint16_t required_input_channels;
    uint16_t expected_output_channels;
    int preserve_sample_rate;
    TsCdpRecipeSafetyPolicy safety_policy;
    int seed_supported;
    int deterministic;
    uint32_t analysis_points;
    uint32_t analysis_overlap;
    uint32_t minimum_analysis_windows;
    uint32_t provenance_version;
} TsCdpRecipe;

typedef struct {
    float controls[TS_CDP_CONTROL_COUNT];
    float mix;
    uint64_t seed;
} TsCdpRecipeValues;

typedef struct {
    int divide;
    int hold_windows;
    float shift_semitones;
    float duration_randomization;
    float division_randomization;
} TsCdpGlistenMapping;

typedef struct {
    char executable[TS_CDP_TEXT_MAX];
    int argc;
    char arguments[TS_CDP_MAX_COMMAND_ARGS][TS_CDP_TEXT_MAX];
} TsCdpCommand;

size_t ts_cdp_factory_recipe_count(void);
const TsCdpRecipe *ts_cdp_factory_recipe_at(size_t index);
const TsCdpRecipe *ts_cdp_recipe_find(const char *id);
int ts_cdp_recipe_validate(const TsCdpRecipe *recipe,
                           char *error, size_t error_size);
void ts_cdp_recipe_values_default(const TsCdpRecipe *recipe,
                                  TsCdpRecipeValues *values);
float ts_cdp_control_quantize(const TsCdpControlSpec *control, float value);
int ts_cdp_recipe_set_control(const TsCdpRecipe *recipe,
                              TsCdpRecipeValues *values, size_t index,
                              float value);
void ts_cdp_control_format(const TsCdpControlSpec *control, float value,
                           uint32_t sample_rate, uint32_t analysis_points,
                           uint32_t analysis_overlap,
                           char *text, size_t text_size);
int ts_cdp_glisten_map(const TsCdpRecipe *recipe,
                       const TsCdpRecipeValues *values,
                       TsCdpGlistenMapping *mapping,
                       char *error, size_t error_size);
int ts_cdp_glisten_build_commands(const TsCdpRecipe *recipe,
                                  const TsCdpRecipeValues *values,
                                  TsCdpCommand commands[3],
                                  char *error, size_t error_size);
int ts_cdp_recipe_input_valid(const TsCdpRecipe *recipe,
                              size_t frames, uint32_t sample_rate,
                              char *error, size_t error_size);

#endif
