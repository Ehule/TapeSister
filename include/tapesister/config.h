#ifndef TAPESISTER_CONFIG_H
#define TAPESISTER_CONFIG_H

#include <stddef.h>

#include "tapesister/recipe.h"
#include "tapesister/cdp_recipe.h"

enum {
    TS_CONFIG_PATH_MAX = 1024,
    TS_CONFIG_FIELD_COUNT = 4,
    TS_ROTATE_WHEEL_FINE_MIN = 1,
    TS_ROTATE_WHEEL_FINE_MAX = 20,
    TS_ROTATE_WHEEL_FINE_DEFAULT = 5,
    TS_ROTATE_WHEEL_COARSE_MIN = 20,
    TS_ROTATE_WHEEL_COARSE_MAX = 100,
    TS_ROTATE_WHEEL_COARSE_DEFAULT = 50,
    TS_DRONE_CROSSFADE_MS_MIN = 1,
    TS_DRONE_CROSSFADE_MS_MAX = 1000,
    TS_DRONE_CROSSFADE_MS_DEFAULT = 50
};

typedef enum {
    TS_CONFIG_SAMPLE_PATH = 0,
    TS_CONFIG_FASTTRACKER_PATH,
    TS_CONFIG_EXCHANGE_PATH,
    TS_CONFIG_CDP_BIN_PATH
} TsConfigField;

typedef struct {
    char sample_path[TS_CONFIG_PATH_MAX];
    char fasttracker_path[TS_CONFIG_PATH_MAX];
    char exchange_path[TS_CONFIG_PATH_MAX];
    char cdp_bin_path[TS_CONFIG_PATH_MAX];
    int startup_welcome_sample;
    int startup_welcome_autoplay;
    int playhead_zero_snap;
    int rotate_wheel_fine;
    int rotate_wheel_coarse;
    int drone_crossfade_ms;
    int dsp_factory_overridden[TS_FACTORY_RECIPE_COUNT];
    float dsp_factory_controls[TS_FACTORY_RECIPE_COUNT][TS_DSP_CONTROL_COUNT];
    int cdp_factory_overridden[TS_CDP_FACTORY_RECIPE_COUNT];
    float cdp_factory_controls[TS_CDP_FACTORY_RECIPE_COUNT][TS_CDP_CONTROL_COUNT];
    float cdp_factory_mix[TS_CDP_FACTORY_RECIPE_COUNT];
    uint64_t cdp_factory_seed[TS_CDP_FACTORY_RECIPE_COUNT];
} TsConfig;

void ts_config_init(TsConfig *config);
int ts_config_load(TsConfig *config, const char *path,
                   char *error, size_t error_size);
int ts_config_save(const TsConfig *config, const char *path,
                   char *error, size_t error_size);
char *ts_config_field(TsConfig *config, TsConfigField field);
const char *ts_config_field_const(const TsConfig *config, TsConfigField field);
const char *ts_config_field_name(TsConfigField field);

#endif
