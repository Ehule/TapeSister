#ifndef TAPESISTER_CONFIG_H
#define TAPESISTER_CONFIG_H

#include <stddef.h>

#include "tapesister/audition.h"
#include "tapesister/dsp_recipe.h"
#include "tapesister/recipe.h"
#include "tapesister/cdp_recipe.h"
#include "tapesister/waveform_display.h"

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
    TS_FM_OUTPUT_PERCENT_MIN = 0,
    TS_FM_OUTPUT_PERCENT_MAX = 100,
    TS_FM_OUTPUT_PERCENT_DEFAULT = 50,
    TS_MASTER_OUTPUT_PERCENT_MIN = 0,
    TS_MASTER_OUTPUT_PERCENT_MAX = 100,
    TS_MASTER_OUTPUT_PERCENT_DEFAULT = 100,
    TS_AUDIO_BUFFER_FRAMES_MIN = 256,
    TS_AUDIO_BUFFER_FRAMES_MAX = 1024,
    TS_AUDIO_BUFFER_FRAMES_DEFAULT = 512,
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
    TS_RECORD_INPUT_CHANNEL_MAX = 3,
    TS_RECORD_INPUT_CHANNEL_DEFAULT = 1,
    TS_CAPTURE_CHANNELS_MIN = 1,
    TS_CAPTURE_CHANNELS_MAX = 2,
    TS_CAPTURE_CHANNELS_DEFAULT = 1,
    TS_SISTER_MONITOR_PERCENT_MIN = 0,
    TS_SISTER_MONITOR_PERCENT_MAX = 100,
    TS_SISTER_MONITOR_PERCENT_DEFAULT = 100,
    TS_SISTER_INPUT_PERCENT_MIN = 0,
    TS_SISTER_INPUT_PERCENT_MAX = 200,
    TS_SISTER_INPUT_PERCENT_DEFAULT = 100,
    TS_SISTER_SOURCE_PERCENT_MIN = 0,
    TS_SISTER_SOURCE_PERCENT_MAX = 400,
    TS_SISTER_SOURCE_PERCENT_DEFAULT = 100,
    TS_SISTER_FX_RETURN_PERCENT_MIN = 0,
    TS_SISTER_FX_RETURN_PERCENT_MAX = 200,
    TS_SISTER_FX_RETURN_PERCENT_DEFAULT = 100,
    TS_SISTER_OUTPUT_PERCENT_MIN = 0,
    TS_SISTER_OUTPUT_PERCENT_MAX = 400,
    TS_SISTER_OUTPUT_PERCENT_DEFAULT = 400,
    TS_SISTER_ERASE_PERCENT_MIN = 0,
    TS_SISTER_ERASE_PERCENT_MAX = 100,
    TS_SISTER_ERASE_PERCENT_DEFAULT = 100,
    TS_SISTER_GHOST_PERCENT_MIN = 0,
    TS_SISTER_GHOST_PERCENT_MAX = 100,
    TS_SISTER_GHOST_PERCENT_DEFAULT = 0,
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
    int tile_fade_ms;
    int ripple_cut_crop_canvas;
    int reference_tone_volume;
    int fm_output_percent;
    int master_output_percent;
    int audio_buffer_frames;
    int record_input_channel;
    int midi_input_channel;
    int record_threshold_db;
    int record_preroll_ms;
    int record_silence_ms;
    int record_tail_ms;
    int record_max_seconds;
    int capture_auto_resize;
    int capture_max_seconds;
    int capture_channels;
    int waveform_display_mode;
    int sister_waveform_display_mode;
    int sister_buffer_seconds;
    int sister_buffer_channels;
    int sister_clear_ms;
    int sister_limiter_enabled;
    float sister_limiter_ceiling_db;
    float sister_limiter_lookahead_ms;
    float sister_limiter_release_ms;
    int sister_fx_effect_transition_ms;
    int sister_fx_transition_ms;
    int sister_fallout_transition_ms;
    int sister_fallout_component_transition_ms;
    int sister_fallout_master_transition_ms;
    int sister_fallout_rise_seconds;
    int sister_capture_channels;
    int sister_restart_clear;
    int sister_dry_percent;
    int sister_wet_percent;
    int sister_input_percent;
    int sister_tiles_percent;
    int sister_fm_percent;
    int sister_ext_percent;
    int sister_audition_percent;
    int sister_fx_return_percent;
    int sister_output_percent;
    int sister_erase_percent;
    int sister_ghost_percent;
    int sister_window_maximized;
    int sister_window_x;
    int sister_window_y;
    int dsp_factory_overridden[TS_DSP_FACTORY_RECIPE_COUNT];
    float dsp_factory_controls[TS_DSP_FACTORY_RECIPE_COUNT][TS_DSP_CONTROL_COUNT];
    int cdp_process_enabled[TS_CDP_CATALOG_CAPACITY];
    int cdp_factory_overridden[TS_CDP_CATALOG_CAPACITY];
    float cdp_factory_controls[TS_CDP_CATALOG_CAPACITY][TS_CDP_CONTROL_COUNT];
    float cdp_factory_mix[TS_CDP_CATALOG_CAPACITY];
    uint64_t cdp_factory_seed[TS_CDP_CATALOG_CAPACITY];
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
