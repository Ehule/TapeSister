#ifndef TAPESISTER_RECIPE_H
#define TAPESISTER_RECIPE_H

#include "tapesister/sample.h"

enum {
    TS_RECIPE_SLOT_COUNT = 16,
    TS_FACTORY_RECIPE_COUNT = 8,
    TS_RECIPE_NAME_MAX = 31,
    TS_DSP_CONTROL_COUNT = 4
};

typedef enum {
    TS_DSP_PROFILE_NEUTRAL = 0,
    TS_DSP_PROFILE_WARM,
    TS_DSP_PROFILE_DARK,
    TS_DSP_PROFILE_BRIGHT,
    TS_DSP_PROFILE_DUB,
    TS_DSP_PROFILE_HOLLOW,
    TS_DSP_PROFILE_HARD,
    TS_DSP_PROFILE_BROKEN,
    TS_DSP_PROFILE_GENERIC,
    TS_DSP_PROFILE_COUNT
} TsDspProfile;

typedef enum {
    TS_DSP_VALUE_PERCENT = 0,
    TS_DSP_VALUE_SECONDS,
    TS_DSP_VALUE_HERTZ,
    TS_DSP_VALUE_DRIVE
} TsDspValueFormat;

typedef struct {
    const char *label;
    float minimum;
    float maximum;
    float step;
    int logarithmic;
    TsDspValueFormat format;
} TsDspControlSpec;

typedef struct {
    TsDspProfile profile;
    const char *description;
    size_t control_count;
    TsDspControlSpec controls[TS_DSP_CONTROL_COUNT];
} TsDspPresetSpec;

typedef struct {
    char name[TS_RECIPE_NAME_MAX + 1];
    TsProcessRecipe process;
    TsDspProfile dsp_profile;
    float dsp_controls[TS_DSP_CONTROL_COUNT];
    int has_dsp_controls;
    TsTuning tuning;
    TsTuning audible_tuning;
    int has_tuning;
    int factory;
    int occupied;
} TsPortableRecipe;

typedef struct {
    TsPortableRecipe slots[TS_RECIPE_SLOT_COUNT];
    int active_slot;
} TsRecipeBank;

void ts_recipe_bank_init(TsRecipeBank *bank);
int ts_recipe_bank_capture(TsRecipeBank *bank, int slot, const TsProcessRecipe *process,
                           const TsTuning *tuning, const TsTuning *audible_tuning,
                           const char *name,
                           char *error, size_t error_size);
int ts_recipe_bank_clear(TsRecipeBank *bank, int slot, char *error, size_t error_size);
int ts_recipe_bank_rename(TsRecipeBank *bank, int slot, const char *name,
                          char *error, size_t error_size);
int ts_recipe_bank_add_user(TsRecipeBank *bank, const TsPortableRecipe *recipe,
                            char *error, size_t error_size);
int ts_recipe_save(const TsPortableRecipe *recipe, const char *path,
                   char *error, size_t error_size);
int ts_recipe_load(TsPortableRecipe *recipe, const char *path,
                   char *error, size_t error_size);
int ts_recipe_from_process(TsPortableRecipe *recipe, const TsProcessRecipe *process,
                           const char *name);
int ts_recipe_from_process_and_tuning(TsPortableRecipe *recipe,
                                      const TsProcessRecipe *process,
                                      const TsTuning *tuning, const char *name);
int ts_recipe_from_process_and_tunings(TsPortableRecipe *recipe,
                                       const TsProcessRecipe *process,
                                       const TsTuning *tuning,
                                       const TsTuning *audible_tuning,
                                       const char *name);
int ts_recipe_process_valid(const TsProcessRecipe *process);
int ts_process_recipe_equal(const TsProcessRecipe *left,
                            const TsProcessRecipe *right);
const TsDspPresetSpec *ts_dsp_preset_spec(TsDspProfile profile);
void ts_dsp_preset_bind(TsPortableRecipe *recipe, TsDspProfile profile);
int ts_dsp_preset_set_control(TsPortableRecipe *recipe, size_t index,
                              float normalized);
int ts_dsp_preset_set_controls(TsPortableRecipe *recipe,
                               const float controls[TS_DSP_CONTROL_COUNT]);
float ts_dsp_control_value(const TsDspControlSpec *control, float normalized);
void ts_dsp_control_format(const TsDspControlSpec *control, float normalized,
                           char *text, size_t text_size);

#endif
