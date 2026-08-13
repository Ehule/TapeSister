#ifndef TAPESISTER_MACRO_H
#define TAPESISTER_MACRO_H

#include "tapesister/sample.h"

typedef struct {
    float body;
    float texture;
    float motion;
    float space;
    float pressure;
} TsMacroControls;

typedef struct {
    const char *name;
    TsMacroControls controls;
} TsMacroPreset;

enum { TS_MACRO_PRESET_COUNT = 8 };

void ts_macro_controls_reset(TsMacroControls *controls);
int ts_macro_controls_valid(const TsMacroControls *controls);
int ts_macro_compile(TsProcessRecipe *process, const TsMacroControls *controls,
                     uint32_t seed);
const TsMacroPreset *ts_macro_preset(int index);

#endif
