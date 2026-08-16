#ifndef TAPESISTER_CONFIG_H
#define TAPESISTER_CONFIG_H

#include <stddef.h>

enum {
    TS_CONFIG_PATH_MAX = 1024,
    TS_CONFIG_FIELD_COUNT = 3,
    TS_ROTATE_WHEEL_FINE_MIN = 1,
    TS_ROTATE_WHEEL_FINE_MAX = 20,
    TS_ROTATE_WHEEL_FINE_DEFAULT = 5,
    TS_ROTATE_WHEEL_COARSE_MIN = 20,
    TS_ROTATE_WHEEL_COARSE_MAX = 100,
    TS_ROTATE_WHEEL_COARSE_DEFAULT = 50
};

typedef enum {
    TS_CONFIG_SAMPLE_PATH = 0,
    TS_CONFIG_FASTTRACKER_PATH,
    TS_CONFIG_EXCHANGE_PATH
} TsConfigField;

typedef struct {
    char sample_path[TS_CONFIG_PATH_MAX];
    char fasttracker_path[TS_CONFIG_PATH_MAX];
    char exchange_path[TS_CONFIG_PATH_MAX];
    int startup_welcome_sample;
    int startup_welcome_autoplay;
    int playhead_zero_snap;
    int rotate_wheel_fine;
    int rotate_wheel_coarse;
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
