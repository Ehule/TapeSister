#ifndef TAPESISTER_DSP_RECIPE_H
#define TAPESISTER_DSP_RECIPE_H

#include <stddef.h>
#include <stdint.h>

#include "tapesister/recipe.h"

enum {
    TS_DSP_BANK_COUNT = 2,
    TS_DSP_BANK_SLOT_COUNT = 16,
    TS_DSP_FACTORY_RECIPE_COUNT = TS_DSP_BANK_COUNT * TS_DSP_BANK_SLOT_COUNT
};

typedef enum {
    TS_DSP_RECIPE_SPACE = 0,
    TS_DSP_RECIPE_CAVE,
    TS_DSP_RECIPE_ROOM,
    TS_DSP_RECIPE_ECHO,
    TS_DSP_RECIPE_TAPE,
    TS_DSP_RECIPE_DUB,
    TS_DSP_RECIPE_COMB,
    TS_DSP_RECIPE_RESONATE,
    TS_DSP_RECIPE_LOW,
    TS_DSP_RECIPE_HIGH,
    TS_DSP_RECIPE_BAND,
    TS_DSP_RECIPE_NOTCH,
    TS_DSP_RECIPE_CHORUS,
    TS_DSP_RECIPE_FLANGE,
    TS_DSP_RECIPE_DRIVE,
    TS_DSP_RECIPE_CRUSH,
    TS_DSP_RECIPE_SINE,
    TS_DSP_RECIPE_SHAPE,
    TS_DSP_RECIPE_PULSE,
    TS_DSP_RECIPE_SUB,
    TS_DSP_RECIPE_METAL,
    TS_DSP_RECIPE_CHIME,
    TS_DSP_RECIPE_DRONE,
    TS_DSP_RECIPE_BEAT,
    TS_DSP_RECIPE_RUMBLE,
    TS_DSP_RECIPE_HISS,
    TS_DSP_RECIPE_DUST,
    TS_DSP_RECIPE_KNOCK,
    TS_DSP_RECIPE_PING,
    TS_DSP_RECIPE_FM,
    TS_DSP_RECIPE_AM,
    TS_DSP_RECIPE_CHAOS,
    TS_DSP_RECIPE_COUNT
} TsDspRecipeKind;

typedef enum {
    TS_DSP_RECIPE_VALUE_PERCENT = 0,
    TS_DSP_RECIPE_VALUE_SECONDS,
    TS_DSP_RECIPE_VALUE_HERTZ,
    TS_DSP_RECIPE_VALUE_DRIVE,
    TS_DSP_RECIPE_VALUE_RATIO,
    TS_DSP_RECIPE_VALUE_BITS,
    TS_DSP_RECIPE_VALUE_MILLISECONDS
} TsDspRecipeValueFormat;

typedef struct {
    const char *label;
    float minimum;
    float maximum;
    float default_normalized;
    int logarithmic;
    TsDspRecipeValueFormat format;
} TsDspRecipeControl;

typedef struct {
    const char *id;
    const char *display_name;
    const char *description;
    const char *category;
    TsDspRecipeKind kind;
    uint8_t bank;
    uint8_t slot;
    uint8_t control_count;
    uint8_t primitive;
    TsDspRecipeControl controls[TS_DSP_CONTROL_COUNT];
    uint32_t schema_version;
    uint32_t recipe_version;
} TsDspRecipe;

typedef struct {
    float controls[TS_DSP_CONTROL_COUNT];
    uint32_t seed;
    float tuning_hz;
} TsDspRecipeValues;

size_t ts_dsp_factory_recipe_count(void);
const TsDspRecipe *ts_dsp_factory_recipe_at(size_t index);
const TsDspRecipe *ts_dsp_factory_recipe_for_slot(size_t bank, size_t slot);
const TsDspRecipe *ts_dsp_recipe_find(const char *id);
int ts_dsp_recipe_validate(const TsDspRecipe *recipe,
                           char *error, size_t error_size);
void ts_dsp_recipe_values_default(const TsDspRecipe *recipe,
                                  TsDspRecipeValues *values);
int ts_dsp_recipe_values_equal(const TsDspRecipeValues *left,
                               const TsDspRecipeValues *right);
int ts_dsp_recipe_set_control(const TsDspRecipe *recipe,
                              TsDspRecipeValues *values,
                              size_t index, float normalized);
float ts_dsp_recipe_control_value(const TsDspRecipeControl *control,
                                  float normalized);
void ts_dsp_recipe_control_format(const TsDspRecipeControl *control,
                                  float normalized,
                                  char *text, size_t text_size);

#endif
