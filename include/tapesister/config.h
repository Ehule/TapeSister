#ifndef TAPESISTER_CONFIG_H
#define TAPESISTER_CONFIG_H

#include <stddef.h>

#include "tapesister/audition.h"
#include "tapesister/dsp_recipe.h"
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
    TS_DRONE_CROSSFADE_MS_DEFAULT = 50,
    TS_CHAIN_STAMP_CROSSFADE_MS_MIN = 0,
    TS_CHAIN_STAMP_CROSSFADE_MS_MAX = 50,
    TS_CHAIN_STAMP_CROSSFADE_MS_DEFAULT = 3,
    TS_REFERENCE_TONE_VOLUME_MIN = 0,
    TS_REFERENCE_TONE_VOLUME_MAX = 100,
    TS_REFERENCE_TONE_VOLUME_DEFAULT = 50,
    TS_RECORD_THRESHOLD_DB_MIN = -90,
    TS_RECORD_THRESHOLD_DB_MAX = 0,
    TS_RECORD_THRESHOLD_DB_DEFAULT = -30,
    TS_RECORD_PREROLL_MS_MIN = 0,
    TS_RECORD_PREROLL_MS_MAX = 1000,
    TS_RECORD_PREROLL_MS_DEFAULT = 180,
    TS_RECORD_SILENCE_MS_MIN = 50,
    TS_RECORD_SILENCE_MS_MAX = 5000,
    TS_RECORD_SILENCE_MS_DEFAULT = 650,
    TS_RECORD_TAIL_MS_MIN = 0,
    TS_RECORD_TAIL_MS_MAX = 3000,
    TS_RECORD_TAIL_MS_DEFAULT = 180,
    TS_RECORD_MAX_SECONDS_MIN = 1,
    TS_RECORD_MAX_SECONDS_MAX = 600,
    TS_RECORD_MAX_SECONDS_DEFAULT = 20,
    TS_CAPTURE_MAX_SECONDS_MIN = 1,
    TS_CAPTURE_MAX_SECONDS_MAX = 600,
    TS_CAPTURE_MAX_SECONDS_DEFAULT = 20,
    TS_RECORD_INPUT_CHANNEL_MIN = 0,
    TS_RECORD_INPUT_CHANNEL_MAX = 2,
    TS_RECORD_INPUT_CHANNEL_DEFAULT = 1,
    TS_MIDI_INPUT_CHANNEL_MIN = 0,
    TS_MIDI_INPUT_CHANNEL_MAX = 16,
    TS_MIDI_INPUT_CHANNEL_DEFAULT = 0
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
    char record_input_device[TS_CONFIG_PATH_MAX];
    char audio_output_device[TS_CONFIG_PATH_MAX];
    char midi_input_device[TS_CONFIG_PATH_MAX];
    int startup_welcome_sample;
    int startup_welcome_autoplay;
    int playhead_zero_snap;
    int rotate_wheel_fine;
    int rotate_wheel_coarse;
    int drone_crossfade_ms;
    int chain_stamp_crossfade_ms;
    int voice_attack_ms;
    int ripple_cut_crop_canvas;
    int reference_tone_volume;
    int record_input_channel;
    int midi_input_channel;
    int record_threshold_db;
    int record_preroll_ms;
    int record_silence_ms;
    int record_tail_ms;
    int record_max_seconds;
    int capture_auto_resize;
    int capture_max_seconds;
    int dsp_factory_overridden[TS_DSP_FACTORY_RECIPE_COUNT];
    float dsp_factory_controls[TS_DSP_FACTORY_RECIPE_COUNT][TS_DSP_CONTROL_COUNT];
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
