#include "tapesister/config.h"
#include "tapesister/performance.h"
#include "tapesister/sister_limiter.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_error(char *error, size_t error_size, const char *message)
{
    if (error != NULL && error_size > 0)
        snprintf(error, error_size, "%s", message != NULL ? message : "");
}

void ts_config_init(TsConfig *config)
{
    if (config != NULL) {
        memset(config, 0, sizeof(*config));
        config->startup_welcome_sample = 1;
        config->startup_welcome_autoplay = 1;
        config->playhead_zero_snap = 1;
        config->rotate_wheel_fine = TS_ROTATE_WHEEL_FINE_DEFAULT;
        config->rotate_wheel_coarse = TS_ROTATE_WHEEL_COARSE_DEFAULT;
        config->drone_crossfade_ms = TS_DRONE_CROSSFADE_MS_DEFAULT;
        config->chain_stamp_crossfade_ms = TS_CHAIN_STAMP_CROSSFADE_MS_DEFAULT;
        config->voice_attack_ms = TS_AUDITION_ATTACK_MS_DEFAULT;
        config->tile_fade_ms = TS_TILE_FADE_MS_DEFAULT;
        config->ripple_cut_crop_canvas = 0;
        config->reference_tone_volume = TS_REFERENCE_TONE_VOLUME_DEFAULT;
        config->fm_output_percent = TS_FM_OUTPUT_PERCENT_DEFAULT;
        config->audio_buffer_frames = TS_AUDIO_BUFFER_FRAMES_DEFAULT;
        config->record_input_channel = TS_RECORD_INPUT_CHANNEL_DEFAULT;
        config->midi_input_channel = TS_MIDI_INPUT_CHANNEL_DEFAULT;
        config->record_threshold_db = TS_RECORD_THRESHOLD_DB_DEFAULT;
        config->record_preroll_ms = TS_RECORD_PREROLL_MS_DEFAULT;
        config->record_silence_ms = TS_RECORD_SILENCE_MS_DEFAULT;
        config->record_tail_ms = TS_RECORD_TAIL_MS_DEFAULT;
        config->record_max_seconds = TS_RECORD_MAX_SECONDS_DEFAULT;
        config->capture_auto_resize = 1;
        config->capture_max_seconds = TS_CAPTURE_MAX_SECONDS_DEFAULT;
        config->capture_channels = TS_CAPTURE_CHANNELS_DEFAULT;
        config->waveform_display_mode = TS_WAVEFORM_DISPLAY_STEREO;
        config->sister_waveform_display_mode = TS_WAVEFORM_DISPLAY_STEREO;
        config->sister_buffer_seconds = 40;
        config->sister_buffer_channels = 2;
        config->sister_clear_ms = 20;
        config->sister_limiter_enabled = TS_SISTER_LIMITER_DEFAULT_ENABLED;
        config->sister_limiter_ceiling_db =
            TS_SISTER_LIMITER_DEFAULT_CEILING_DB;
        config->sister_limiter_lookahead_ms =
            TS_SISTER_LIMITER_DEFAULT_LOOKAHEAD_MS;
        config->sister_limiter_release_ms =
            TS_SISTER_LIMITER_DEFAULT_RELEASE_MS;
        config->sister_fx_effect_transition_ms = 240000;
        config->sister_fx_transition_ms = 240000;
        config->sister_fallout_transition_ms = 240000;
        config->sister_fallout_component_transition_ms = 240000;
        config->sister_fallout_master_transition_ms = 240000;
        config->sister_fallout_rise_seconds = 3600;
        config->sister_capture_channels = 1;
        config->sister_restart_clear = 1;
        config->sister_dry_percent = TS_SISTER_MONITOR_PERCENT_DEFAULT;
        config->sister_wet_percent = TS_SISTER_MONITOR_PERCENT_DEFAULT;
        config->sister_input_percent = TS_SISTER_INPUT_PERCENT_DEFAULT;
        config->sister_tiles_percent = TS_SISTER_SOURCE_PERCENT_DEFAULT;
        config->sister_fm_percent = TS_SISTER_SOURCE_PERCENT_DEFAULT;
        config->sister_ext_percent = TS_SISTER_SOURCE_PERCENT_DEFAULT;
        config->sister_audition_percent = TS_SISTER_SOURCE_PERCENT_DEFAULT;
        config->sister_fx_return_percent = TS_SISTER_FX_RETURN_PERCENT_DEFAULT;
        config->sister_output_percent = TS_SISTER_OUTPUT_PERCENT_DEFAULT;
        config->sister_erase_percent = TS_SISTER_ERASE_PERCENT_DEFAULT;
        config->sister_ghost_percent = TS_SISTER_GHOST_PERCENT_DEFAULT;
        config->sister_window_maximized = 1;
        config->sister_window_x = -1;
        config->sister_window_y = -1;
        for (size_t recipe = 0; recipe < ts_cdp_factory_recipe_count() &&
                                recipe < TS_CDP_CATALOG_CAPACITY; ++recipe) {
            const TsCdpRecipe *entry = ts_cdp_factory_recipe_at(recipe);
            config->cdp_process_enabled[recipe] =
                entry != NULL ? entry->default_enabled : 0;
        }
    }
}

char *ts_config_field(TsConfig *config, TsConfigField field)
{
    if (config == NULL) return NULL;
    if (field == TS_CONFIG_SAMPLE_PATH) return config->sample_path;
    if (field == TS_CONFIG_FASTTRACKER_PATH) return config->fasttracker_path;
    if (field == TS_CONFIG_EXCHANGE_PATH) return config->exchange_path;
    if (field == TS_CONFIG_CDP_BIN_PATH) return config->cdp_bin_path;
    return NULL;
}

const char *ts_config_field_const(const TsConfig *config, TsConfigField field)
{
    return ts_config_field((TsConfig *)config, field);
}

const char *ts_config_field_name(TsConfigField field)
{
    if (field == TS_CONFIG_SAMPLE_PATH) return "SAMPLE PATH";
    if (field == TS_CONFIG_FASTTRACKER_PATH) return "FASTTRACKER EXECUTABLE";
    if (field == TS_CONFIG_EXCHANGE_PATH) return "FT2 EXCHANGE PATH";
    if (field == TS_CONFIG_CDP_BIN_PATH) return "CDP BIN PATH";
    return "PATH";
}

static char *trim(char *text)
{
    char *end;
    while (*text != '\0' && isspace((unsigned char)*text)) ++text;
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) --end;
    *end = '\0';
    return text;
}

static int copy_value(char *destination, const char *value,
                      char *error, size_t error_size)
{
    size_t length = strlen(value);
    if (length >= TS_CONFIG_PATH_MAX) {
        set_error(error, error_size, "A configured path is too long");
        return 0;
    }
    memcpy(destination, value, length + 1u);
    return 1;
}

static int parse_boolean(const char *value, int *destination)
{
    if (strcmp(value, "0") == 0) { *destination = 0; return 1; }
    if (strcmp(value, "1") == 0) { *destination = 1; return 1; }
    return 0;
}

static int parse_clamped_integer(const char *value, int minimum, int maximum,
                                 int *destination)
{
    char *end;
    long parsed;
    errno = 0;
    parsed = strtol(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0') return 0;
    if (parsed < minimum) parsed = minimum;
    if (parsed > maximum) parsed = maximum;
    *destination = (int)parsed;
    return 1;
}

static int parse_clamped_float(const char *value, float minimum, float maximum,
                               float *destination)
{
    char *end;
    float parsed;
    errno = 0;
    parsed = strtof(value, &end);
    if (errno != 0 || end == value || *end != '\0' || !isfinite(parsed))
        return 0;
    if (parsed < minimum) parsed = minimum;
    if (parsed > maximum) parsed = maximum;
    *destination = parsed;
    return 1;
}

static int parse_dsp_preset(const char *key, const char *value,
                            TsConfig *config)
{
    int slot;
    float controls[TS_DSP_CONTROL_COUNT];
    char trailing;
    if (sscanf(key, "DspPreset%2d%c", &slot, &trailing) != 1 ||
        slot < 1 || slot > TS_DSP_FACTORY_RECIPE_COUNT)
        return 0;
    if (sscanf(value, "%f,%f,%f,%f%c", &controls[0], &controls[1],
               &controls[2], &controls[3], &trailing) != TS_DSP_CONTROL_COUNT)
        return -1;
    for (int index = 0; index < TS_DSP_CONTROL_COUNT; ++index)
        if (!isfinite(controls[index]) || controls[index] < 0.0f ||
            controls[index] > 1.0f) return -1;
    --slot;
    memcpy(config->dsp_factory_controls[slot], controls, sizeof(controls));
    config->dsp_factory_overridden[slot] = 1;
    return 1;
}

static int parse_cdp_preset(const char *key, const char *value,
                            TsConfig *config)
{
    int slot = -1;
    float controls[TS_CDP_CONTROL_COUNT];
    float mix;
    unsigned long long seed;
    char trailing;
    if (strncmp(key, "CdpPreset.", 10u) == 0) {
        slot = ts_cdp_recipe_index_for_id(key + 10u);
        if (slot < 0) return 0;
    } else {
        if (sscanf(key, "CdpPreset%2d%c", &slot, &trailing) != 1 ||
            slot < 1 || slot > TS_CDP_FACTORY_RECIPE_COUNT)
            return 0;
        --slot;
    }
    if (sscanf(value, "%f,%f,%f,%f,%f,%llu%c", &controls[0], &controls[1],
               &controls[2], &controls[3], &mix, &seed, &trailing) != 6)
        return -1;
    for (int index = 0; index < TS_CDP_CONTROL_COUNT; ++index)
        if (!isfinite(controls[index])) return -1;
    if (!isfinite(mix) || mix < 0.0f || mix > 1.0f) return -1;
    memcpy(config->cdp_factory_controls[slot], controls, sizeof(controls));
    config->cdp_factory_mix[slot] = mix;
    config->cdp_factory_seed[slot] = (uint64_t)seed;
    config->cdp_factory_overridden[slot] = 1;
    return 1;
}

static int parse_cdp_process(const char *key, const char *value,
                             TsConfig *config)
{
    int index;
    int enabled;
    if (strncmp(key, "CdpProcess.", 11u) != 0) return 0;
    index = ts_cdp_recipe_index_for_id(key + 11u);
    if (index < 0 || !parse_boolean(value, &enabled)) return -1;
    config->cdp_process_enabled[index] = enabled;
    return 1;
}

int ts_config_load(TsConfig *config, const char *path,
                   char *error, size_t error_size)
{
    FILE *file;
    char line[TS_CONFIG_PATH_MAX + 80];
    TsConfig loaded;
    int line_number = 0;
    int saw_fx_effect_transition = 0;
    int saw_fallout_master_transition = 0;
    if (config == NULL || path == NULL || path[0] == '\0') {
        set_error(error, error_size, "Invalid config destination");
        return 0;
    }
    file = fopen(path, "rb");
    if (file == NULL) {
        if (errno == ENOENT) {
            ts_config_init(config);
            set_error(error, error_size, "");
            return 1;
        }
        snprintf(error, error_size, "Could not open config: %s", strerror(errno));
        return 0;
    }
    ts_config_init(&loaded);
    while (fgets(line, sizeof(line), file) != NULL) {
        char *key;
        char *value;
        char *equals;
        ++line_number;
        key = trim(line);
        if (*key == '\0' || *key == ';' || *key == '#') continue;
        if (*key == '[') continue;
        equals = strchr(key, '=');
        if (equals == NULL) {
            snprintf(error, error_size, "Malformed config line %d", line_number);
            fclose(file);
            return 0;
        }
        *equals = '\0';
        value = trim(equals + 1);
        key = trim(key);
        if (strcmp(key, "SamplePath") == 0) {
            if (!copy_value(loaded.sample_path, value, error, error_size)) { fclose(file); return 0; }
        } else if (strcmp(key, "FastTrackerPath") == 0) {
            if (!copy_value(loaded.fasttracker_path, value, error, error_size)) { fclose(file); return 0; }
        } else if (strcmp(key, "ExchangePath") == 0) {
            if (!copy_value(loaded.exchange_path, value, error, error_size)) { fclose(file); return 0; }
        } else if (strcmp(key, "CdpBinPath") == 0) {
            if (!copy_value(loaded.cdp_bin_path, value, error, error_size)) { fclose(file); return 0; }
        } else if (strcmp(key, "record_input_device") == 0) {
            if (!copy_value(loaded.record_input_device, value, error, error_size)) { fclose(file); return 0; }
        } else if (strcmp(key, "startup_welcome_sample") == 0) {
            if (!parse_boolean(value, &loaded.startup_welcome_sample)) { snprintf(error, error_size, "Invalid boolean on config line %d", line_number); fclose(file); return 0; }
        } else if (strcmp(key, "startup_welcome_autoplay") == 0) {
            if (!parse_boolean(value, &loaded.startup_welcome_autoplay)) { snprintf(error, error_size, "Invalid boolean on config line %d", line_number); fclose(file); return 0; }
        } else if (strcmp(key, "playhead_zero_snap") == 0) {
            if (!parse_boolean(value, &loaded.playhead_zero_snap)) { snprintf(error, error_size, "Invalid boolean on config line %d", line_number); fclose(file); return 0; }
        } else if (strcmp(key, "rotate_wheel_fine") == 0) {
            if (!parse_clamped_integer(value, TS_ROTATE_WHEEL_FINE_MIN, TS_ROTATE_WHEEL_FINE_MAX, &loaded.rotate_wheel_fine)) { snprintf(error, error_size, "Invalid integer on config line %d", line_number); fclose(file); return 0; }
        } else if (strcmp(key, "rotate_wheel_coarse") == 0) {
            if (!parse_clamped_integer(value, TS_ROTATE_WHEEL_COARSE_MIN, TS_ROTATE_WHEEL_COARSE_MAX, &loaded.rotate_wheel_coarse)) { snprintf(error, error_size, "Invalid integer on config line %d", line_number); fclose(file); return 0; }
        } else if (strcmp(key, "drone_crossfade_ms") == 0) {
            if (!parse_clamped_integer(value, TS_DRONE_CROSSFADE_MS_MIN, TS_DRONE_CROSSFADE_MS_MAX, &loaded.drone_crossfade_ms)) { snprintf(error, error_size, "Invalid integer on config line %d", line_number); fclose(file); return 0; }
        } else if (strcmp(key, "chain_stamp_crossfade_ms") == 0) {
            if (!parse_clamped_integer(value, TS_CHAIN_STAMP_CROSSFADE_MS_MIN, TS_CHAIN_STAMP_CROSSFADE_MS_MAX, &loaded.chain_stamp_crossfade_ms)) { snprintf(error, error_size, "Invalid integer on config line %d", line_number); fclose(file); return 0; }
        } else if (strcmp(key, "voice_attack_ms") == 0) {
            if (!parse_clamped_integer(value, TS_AUDITION_ATTACK_MS_MIN, TS_AUDITION_ATTACK_MS_MAX, &loaded.voice_attack_ms)) { snprintf(error, error_size, "Invalid integer on config line %d", line_number); fclose(file); return 0; }
        } else if (strcmp(key, "tile_fade_ms") == 0) {
            if (!parse_clamped_integer(value, TS_TILE_FADE_MS_MIN, TS_TILE_FADE_MS_MAX, &loaded.tile_fade_ms)) { snprintf(error, error_size, "Invalid integer on config line %d", line_number); fclose(file); return 0; }
        } else if (strcmp(key, "ripple_cut_crop_canvas") == 0) {
            if (!parse_boolean(value, &loaded.ripple_cut_crop_canvas)) { snprintf(error, error_size, "Invalid boolean on config line %d", line_number); fclose(file); return 0; }
        } else if (strcmp(key, "reference_tone_volume") == 0) {
            if (!parse_clamped_integer(value, TS_REFERENCE_TONE_VOLUME_MIN, TS_REFERENCE_TONE_VOLUME_MAX, &loaded.reference_tone_volume)) { snprintf(error, error_size, "Invalid integer on config line %d", line_number); fclose(file); return 0; }
        } else if (strcmp(key, "fm_output_percent") == 0) {
            if (!parse_clamped_integer(value, TS_FM_OUTPUT_PERCENT_MIN, TS_FM_OUTPUT_PERCENT_MAX, &loaded.fm_output_percent)) { snprintf(error, error_size, "Invalid FM output level on config line %d", line_number); fclose(file); return 0; }
        } else if (strcmp(key, "record_input_channel") == 0) {
            if (!parse_clamped_integer(value, TS_RECORD_INPUT_CHANNEL_MIN, TS_RECORD_INPUT_CHANNEL_MAX, &loaded.record_input_channel)) { snprintf(error, error_size, "Invalid integer on config line %d", line_number); fclose(file); return 0; }
        } else if (strcmp(key, "record_threshold_db") == 0) {
            if (!parse_clamped_integer(value, TS_RECORD_THRESHOLD_DB_MIN, TS_RECORD_THRESHOLD_DB_MAX, &loaded.record_threshold_db)) { snprintf(error, error_size, "Invalid integer on config line %d", line_number); fclose(file); return 0; }
        } else if (strcmp(key, "record_preroll_ms") == 0) {
            if (!parse_clamped_integer(value, TS_RECORD_PREROLL_MS_MIN, TS_RECORD_PREROLL_MS_MAX, &loaded.record_preroll_ms)) { snprintf(error, error_size, "Invalid integer on config line %d", line_number); fclose(file); return 0; }
        } else if (strcmp(key, "record_silence_ms") == 0) {
            if (!parse_clamped_integer(value, TS_RECORD_SILENCE_MS_MIN, TS_RECORD_SILENCE_MS_MAX, &loaded.record_silence_ms)) { snprintf(error, error_size, "Invalid integer on config line %d", line_number); fclose(file); return 0; }
        } else if (strcmp(key, "record_tail_ms") == 0) {
            if (!parse_clamped_integer(value, TS_RECORD_TAIL_MS_MIN, TS_RECORD_TAIL_MS_MAX, &loaded.record_tail_ms)) { snprintf(error, error_size, "Invalid integer on config line %d", line_number); fclose(file); return 0; }
        } else if (strcmp(key, "record_max_seconds") == 0) {
            if (!parse_clamped_integer(value, TS_RECORD_MAX_SECONDS_MIN, TS_RECORD_MAX_SECONDS_MAX, &loaded.record_max_seconds)) { snprintf(error, error_size, "Invalid integer on config line %d", line_number); fclose(file); return 0; }
        } else if (strcmp(key, "capture_auto_resize") == 0) {
            if (!parse_boolean(value, &loaded.capture_auto_resize)) { snprintf(error, error_size, "Invalid boolean on config line %d", line_number); fclose(file); return 0; }
        } else if (strcmp(key, "capture_max_seconds") == 0) {
            if (!parse_clamped_integer(value, TS_CAPTURE_MAX_SECONDS_MIN, TS_CAPTURE_MAX_SECONDS_MAX, &loaded.capture_max_seconds)) { snprintf(error, error_size, "Invalid integer on config line %d", line_number); fclose(file); return 0; }
        } else if (strcmp(key, "capture_channels") == 0) {
            if (!parse_clamped_integer(value, TS_CAPTURE_CHANNELS_MIN, TS_CAPTURE_CHANNELS_MAX, &loaded.capture_channels)) { snprintf(error, error_size, "Invalid integer on config line %d", line_number); fclose(file); return 0; }
        } else if (strcmp(key, "waveform_display_mode") == 0) {
            if (!parse_clamped_integer(value, 0, TS_WAVEFORM_DISPLAY_COUNT - 1, &loaded.waveform_display_mode)) { snprintf(error, error_size, "Invalid waveform mode on config line %d", line_number); fclose(file); return 0; }
        } else if (strcmp(key, "sister_waveform_display_mode") == 0) {
            if (!parse_clamped_integer(value, 0, TS_WAVEFORM_DISPLAY_COUNT - 1, &loaded.sister_waveform_display_mode)) { snprintf(error, error_size, "Invalid Sister waveform mode on config line %d", line_number); fclose(file); return 0; }
        } else if (strcmp(key, "sister_buffer_seconds") == 0) {
            if (!parse_clamped_integer(value, 1, 120, &loaded.sister_buffer_seconds)) { snprintf(error, error_size, "Invalid Sister duration on config line %d", line_number); fclose(file); return 0; }
            if (loaded.sister_buffer_seconds < 5) loaded.sister_buffer_seconds = 5;
            if (loaded.sister_buffer_seconds > 60) loaded.sister_buffer_seconds = 60;
        } else if (strcmp(key, "sister_buffer_channels") == 0) {
            if (!parse_clamped_integer(value, 1, 2, &loaded.sister_buffer_channels)) { snprintf(error, error_size, "Invalid Sister channels on config line %d", line_number); fclose(file); return 0; }
        } else if (strcmp(key, "sister_clear_ms") == 0) {
            if (!parse_clamped_integer(value, 1, 1000, &loaded.sister_clear_ms)) { snprintf(error, error_size, "Invalid Sister clear time on config line %d", line_number); fclose(file); return 0; }
        } else if (strcmp(key, "sister_limiter_enabled") == 0) {
            if (!parse_boolean(value, &loaded.sister_limiter_enabled)) { snprintf(error, error_size, "Invalid limiter switch on config line %d", line_number); fclose(file); return 0; }
        } else if (strcmp(key, "sister_limiter_ceiling_db") == 0) {
            if (!parse_clamped_float(value, TS_SISTER_LIMITER_CEILING_DB_MIN, TS_SISTER_LIMITER_CEILING_DB_MAX, &loaded.sister_limiter_ceiling_db)) { snprintf(error, error_size, "Invalid limiter ceiling on config line %d", line_number); fclose(file); return 0; }
        } else if (strcmp(key, "sister_limiter_lookahead_ms") == 0) {
            if (!parse_clamped_float(value, TS_SISTER_LIMITER_LOOKAHEAD_MS_MIN, TS_SISTER_LIMITER_LOOKAHEAD_MS_MAX, &loaded.sister_limiter_lookahead_ms)) { snprintf(error, error_size, "Invalid limiter lookahead on config line %d", line_number); fclose(file); return 0; }
        } else if (strcmp(key, "sister_limiter_release_ms") == 0) {
            if (!parse_clamped_float(value, TS_SISTER_LIMITER_RELEASE_MS_MIN, TS_SISTER_LIMITER_RELEASE_MS_MAX, &loaded.sister_limiter_release_ms)) { snprintf(error, error_size, "Invalid limiter release on config line %d", line_number); fclose(file); return 0; }
        } else if (strcmp(key, "sister_fx_effect_transition_ms") == 0) {
            if (!parse_clamped_integer(value, 10, 3600000, &loaded.sister_fx_effect_transition_ms)) { snprintf(error, error_size, "Invalid individual FX transition time on config line %d", line_number); fclose(file); return 0; }
            saw_fx_effect_transition = 1;
        } else if (strcmp(key, "sister_fx_transition_ms") == 0) {
            if (!parse_clamped_integer(value, 10, 3600000, &loaded.sister_fx_transition_ms)) { snprintf(error, error_size, "Invalid FX transition time on config line %d", line_number); fclose(file); return 0; }
            if (!saw_fx_effect_transition)
                loaded.sister_fx_effect_transition_ms =
                    loaded.sister_fx_transition_ms;
        } else if (strcmp(key, "sister_fallout_transition_ms") == 0) {
            if (!parse_clamped_integer(value, 10, 3600000, &loaded.sister_fallout_transition_ms)) { snprintf(error, error_size, "Invalid Fallout preset transition time on config line %d", line_number); fclose(file); return 0; }
        } else if (strcmp(key, "sister_fallout_component_transition_ms") == 0) {
            if (!parse_clamped_integer(value, 10, 3600000, &loaded.sister_fallout_component_transition_ms)) { snprintf(error, error_size, "Invalid Fallout component transition time on config line %d", line_number); fclose(file); return 0; }
            if (!saw_fallout_master_transition)
                loaded.sister_fallout_master_transition_ms =
                    loaded.sister_fallout_component_transition_ms;
        } else if (strcmp(key, "sister_fallout_master_transition_ms") == 0) {
            if (!parse_clamped_integer(value, 10, 3600000, &loaded.sister_fallout_master_transition_ms)) { snprintf(error, error_size, "Invalid Fallout master transition time on config line %d", line_number); fclose(file); return 0; }
            saw_fallout_master_transition = 1;
        } else if (strcmp(key, "sister_fallout_rise_seconds") == 0) {
            if (!parse_clamped_integer(value, 1, 14400, &loaded.sister_fallout_rise_seconds)) { snprintf(error, error_size, "Invalid Fallout rise time on config line %d", line_number); fclose(file); return 0; }
        } else if (strcmp(key, "sister_capture_channels") == 0) {
            if (!parse_clamped_integer(value, 1, 2, &loaded.sister_capture_channels)) { snprintf(error, error_size, "Invalid Sister capture format on config line %d", line_number); fclose(file); return 0; }
        } else if (strcmp(key, "sister_restart_clear") == 0) {
            if (!parse_boolean(value, &loaded.sister_restart_clear)) { snprintf(error, error_size, "Invalid Sister restart policy on config line %d", line_number); fclose(file); return 0; }
        } else if (strcmp(key, "sister_dry_percent") == 0) {
            if (!parse_clamped_integer(value, TS_SISTER_MONITOR_PERCENT_MIN, TS_SISTER_MONITOR_PERCENT_MAX, &loaded.sister_dry_percent)) { snprintf(error, error_size, "Invalid Sister dry level on config line %d", line_number); fclose(file); return 0; }
        } else if (strcmp(key, "sister_wet_percent") == 0) {
            if (!parse_clamped_integer(value, TS_SISTER_MONITOR_PERCENT_MIN, TS_SISTER_MONITOR_PERCENT_MAX, &loaded.sister_wet_percent)) { snprintf(error, error_size, "Invalid Sister wet level on config line %d", line_number); fclose(file); return 0; }
        } else if (strcmp(key, "sister_input_percent") == 0) {
            if (!parse_clamped_integer(value, TS_SISTER_INPUT_PERCENT_MIN, TS_SISTER_INPUT_PERCENT_MAX, &loaded.sister_input_percent)) { snprintf(error, error_size, "Invalid Sister input level on config line %d", line_number); fclose(file); return 0; }
        } else if (strcmp(key, "sister_tiles_percent") == 0) {
            if (!parse_clamped_integer(value, TS_SISTER_SOURCE_PERCENT_MIN, TS_SISTER_SOURCE_PERCENT_MAX, &loaded.sister_tiles_percent)) { snprintf(error, error_size, "Invalid Sister TILES trim on config line %d", line_number); fclose(file); return 0; }
        } else if (strcmp(key, "sister_fm_percent") == 0) {
            if (!parse_clamped_integer(value, TS_SISTER_SOURCE_PERCENT_MIN, TS_SISTER_SOURCE_PERCENT_MAX, &loaded.sister_fm_percent)) { snprintf(error, error_size, "Invalid Sister FM trim on config line %d", line_number); fclose(file); return 0; }
        } else if (strcmp(key, "sister_ext_percent") == 0) {
            if (!parse_clamped_integer(value, TS_SISTER_SOURCE_PERCENT_MIN, TS_SISTER_SOURCE_PERCENT_MAX, &loaded.sister_ext_percent)) { snprintf(error, error_size, "Invalid Sister EXT trim on config line %d", line_number); fclose(file); return 0; }
        } else if (strcmp(key, "sister_audition_percent") == 0) {
            if (!parse_clamped_integer(value, TS_SISTER_SOURCE_PERCENT_MIN, TS_SISTER_SOURCE_PERCENT_MAX, &loaded.sister_audition_percent)) { snprintf(error, error_size, "Invalid Sister AUDITION trim on config line %d", line_number); fclose(file); return 0; }
        } else if (strcmp(key, "sister_fx_return_percent") == 0) {
            if (!parse_clamped_integer(value, TS_SISTER_FX_RETURN_PERCENT_MIN, TS_SISTER_FX_RETURN_PERCENT_MAX, &loaded.sister_fx_return_percent)) { snprintf(error, error_size, "Invalid Sister FX return level on config line %d", line_number); fclose(file); return 0; }
        } else if (strcmp(key, "sister_output_percent") == 0) {
            if (!parse_clamped_integer(value, TS_SISTER_OUTPUT_PERCENT_MIN, TS_SISTER_OUTPUT_PERCENT_MAX, &loaded.sister_output_percent)) { snprintf(error, error_size, "Invalid Sister output level on config line %d", line_number); fclose(file); return 0; }
        } else if (strcmp(key, "sister_erase_percent") == 0) {
            if (!parse_clamped_integer(value, TS_SISTER_ERASE_PERCENT_MIN, TS_SISTER_ERASE_PERCENT_MAX, &loaded.sister_erase_percent)) { snprintf(error, error_size, "Invalid Sister erase strength on config line %d", line_number); fclose(file); return 0; }
        } else if (strcmp(key, "sister_ghost_percent") == 0) {
            if (!parse_clamped_integer(value, TS_SISTER_GHOST_PERCENT_MIN, TS_SISTER_GHOST_PERCENT_MAX, &loaded.sister_ghost_percent)) { snprintf(error, error_size, "Invalid Sister Ghost Tone on config line %d", line_number); fclose(file); return 0; }
        } else if (strcmp(key, "sister_window_maximized") == 0) {
            if (!parse_boolean(value, &loaded.sister_window_maximized)) { snprintf(error, error_size, "Invalid Sister window maximize setting on config line %d", line_number); fclose(file); return 0; }
        } else if (strcmp(key, "sister_window_x") == 0) {
            if (!parse_clamped_integer(value, -32768, 32767, &loaded.sister_window_x)) { snprintf(error, error_size, "Invalid Sister window X on config line %d", line_number); fclose(file); return 0; }
        } else if (strcmp(key, "sister_window_y") == 0) {
            if (!parse_clamped_integer(value, -32768, 32767, &loaded.sister_window_y)) { snprintf(error, error_size, "Invalid Sister window Y on config line %d", line_number); fclose(file); return 0; }
        } else {
            int process = parse_cdp_process(key, value, &loaded);
            int dsp = process == 0 ? parse_dsp_preset(key, value, &loaded) : 0;
            int cdp = process == 0 && dsp == 0 ?
                      parse_cdp_preset(key, value, &loaded) : 0;
            if (process < 0 || dsp < 0 || cdp < 0 ||
                (process == 0 && strncmp(key, "CdpProcess.", 11u) == 0) ||
                (dsp == 0 && strncmp(key, "DspPreset", 9u) == 0) ||
                (cdp == 0 && strncmp(key, "CdpPreset", 9u) == 0)) {
                snprintf(error, error_size, "Invalid transform preset on config line %d", line_number);
                fclose(file); return 0;
            }
        }
    }
    if (ferror(file)) {
        set_error(error, error_size, "Could not finish reading config");
        fclose(file);
        return 0;
    }
    fclose(file);
    *config = loaded;
    set_error(error, error_size, "");
    return 1;
}

int ts_config_save(const TsConfig *config, const char *path,
                   char *error, size_t error_size)
{
    FILE *file;
    int write_failed;
    if (config == NULL || path == NULL || path[0] == '\0') {
        set_error(error, error_size, "Invalid config source");
        return 0;
    }
    file = fopen(path, "wb");
    if (file == NULL) {
        snprintf(error, error_size, "Could not create config: %s", strerror(errno));
        return 0;
    }
    write_failed = fprintf(file,
                "; TapeSister paths. Blank values disable that shortcut.\n"
                "[Paths]\n"
                "SamplePath=%s\n"
                "FastTrackerPath=%s\n"
                "ExchangePath=%s\n"
                "; Optional development/runtime override containing compatible CDP8 executables.\n"
                "CdpBinPath=%s\n"
                "\n[Startup]\n"
                "startup_welcome_sample=%d\n"
                "startup_welcome_autoplay=%d\n"
                "\n[Waveform]\n"
                "playhead_zero_snap=%d\n"
                "rotate_wheel_fine=%d\n"
                "rotate_wheel_coarse=%d\n"
                "drone_crossfade_ms=%d\n"
                "chain_stamp_crossfade_ms=%d\n"
                "ripple_cut_crop_canvas=%d\n"
                "reference_tone_volume=%d\n"
                "\n[Audition]\n"
                "; FM LOGIC output trim applied to monitoring, Sister FM, and synth capture.\n"
                "fm_output_percent=%d\n"
                "; Per-voice note-on de-click ramp in milliseconds; 0 disables it, 20 is the maximum.\n"
                "voice_attack_ms=%d\n"
                "; Mouse-launched tile fade in/out; 0 disables it, 30000 is the maximum.\n"
                "; Each edge is capped at 20%% of the tile or loop duration.\n"
                "tile_fade_ms=%d\n"
                "\n[External Recording]\n"
                "; Blank uses the operating system default capture device. Otherwise use the exact SDL device name.\n"
                "record_input_device=%s\n"
                "; 0=MIX mono, 1=LEFT mono, 2=RIGHT mono, 3=STEREO L/R.\n"
                "record_input_channel=%d\n"
                "; Threshold in dBFS; lower values trigger on quieter sounds.\n"
                "record_threshold_db=%d\n"
                "; Audio retained before threshold crossing so attacks are not clipped.\n"
                "record_preroll_ms=%d\n"
                "; Quiet time required before automatic stop begins.\n"
                "record_silence_ms=%d\n"
                "; Extra quiet tape kept after silence detection.\n"
                "record_tail_ms=%d\n"
                "; Safety limit for one captured tile.\n"
                "record_max_seconds=%d\n"
                "\n[Internal Capture]\n"
                "; Resize the armed blank tile to the completed Capture duration.\n"
                "capture_auto_resize=%d\n"
                "; Hard safety limit when automatic Capture resizing is enabled.\n"
                "capture_max_seconds=%d\n"
                "; Internal Capture format: 1=M, 2=S. Overdub always follows its target shape.\n"
                "capture_channels=%d\n"
                "\n[Sister Machine]\n"
                "; Display modes: 0=STEREO, 1=LEFT, 2=RIGHT, 3=MONO SUM.\n"
                "waveform_display_mode=%d\n"
                "sister_waveform_display_mode=%d\n"
                "sister_buffer_seconds=%d\n"
                "sister_buffer_channels=%d\n"
                "sister_clear_ms=%d\n"
                "; Final stereo safety limiter; UI switch is a session-only override.\n"
                "sister_limiter_enabled=%d\n"
                "; Ceiling -12..0 dBFS, lookahead 0.1..10 ms, release 10..2000 ms.\n"
                "sister_limiter_ceiling_db=%.3g\n"
                "sister_limiter_lookahead_ms=%.3g\n"
                "sister_limiter_release_ms=%.3g\n"
                "; Default individual Reverb/Delay/Distortion ramp: 10 ms to 60 minutes.\n"
                "sister_fx_effect_transition_ms=%d\n"
                "; Default Master FX gate ramp: 10 ms to 60 minutes.\n"
                "sister_fx_transition_ms=%d\n"
                "; Default Fallout preset crossfade: 10 ms to 60 minutes.\n"
                "sister_fallout_transition_ms=%d\n"
                "; Default Fallout component ramp: 10 ms to 60 minutes.\n"
                "sister_fallout_component_transition_ms=%d\n"
                "; Default Fallout master gate ramp: 10 ms to 60 minutes.\n"
                "sister_fallout_master_transition_ms=%d\n"
                "; Default Fallout RISE length: 1 second to 4 hours.\n"
                "sister_fallout_rise_seconds=%d\n"
                "sister_capture_channels=%d\n"
                "sister_restart_clear=%d\n"
                "; Source trims feed the normalized Sister input mixer; INPUT remains its master.\n"
                "sister_input_percent=%d\n"
                "sister_tiles_percent=%d\n"
                "sister_fm_percent=%d\n"
                "sister_ext_percent=%d\n"
                "sister_audition_percent=%d\n"
                "; Completed post-effects return, before linked safety and Master FX Feedback tap.\n"
                "sister_fx_return_percent=%d\n"
                "; Dry and wet affect monitoring only.\n"
                "sister_dry_percent=%d\n"
                "sister_wet_percent=%d\n"
                "; Post-filter MIX output gain; 400 compensates conservative internal headroom.\n"
                "sister_output_percent=%d\n"
                "; Per-pass write erase: 100=full replacement, 20=retain 80%%.\n"
                "sister_erase_percent=%d\n"
                "; Per-pass spectral aging of retained old tape; 0 is exact bypass.\n"
                "sister_ghost_percent=%d\n"
                "; Open the independent Sister window maximized: 1=yes, 0=no.\n"
                "sister_window_maximized=%d\n"
                "sister_window_x=%d\n"
                "sister_window_y=%d\n"
                "\n[DSP Presets]\n"
                "; SAVE/UPDATE writes normalized macro values here.\n",
                config->sample_path, config->fasttracker_path,
                config->exchange_path, config->cdp_bin_path,
                config->startup_welcome_sample,
                config->startup_welcome_autoplay, config->playhead_zero_snap,
                config->rotate_wheel_fine,
                config->rotate_wheel_coarse,
                config->drone_crossfade_ms,
                config->chain_stamp_crossfade_ms,
                config->ripple_cut_crop_canvas ? 1 : 0,
                config->reference_tone_volume,
                config->fm_output_percent,
                config->voice_attack_ms,
                config->tile_fade_ms,
                config->record_input_device,
                config->record_input_channel,
                config->record_threshold_db,
                config->record_preroll_ms,
                config->record_silence_ms,
                config->record_tail_ms,
                config->record_max_seconds,
                config->capture_auto_resize ? 1 : 0,
                config->capture_max_seconds,
                config->capture_channels,
                config->waveform_display_mode,
                config->sister_waveform_display_mode,
                config->sister_buffer_seconds,
                config->sister_buffer_channels,
                config->sister_clear_ms,
                config->sister_limiter_enabled ? 1 : 0,
                config->sister_limiter_ceiling_db,
                config->sister_limiter_lookahead_ms,
                config->sister_limiter_release_ms,
                config->sister_fx_effect_transition_ms,
                config->sister_fx_transition_ms,
                config->sister_fallout_transition_ms,
                config->sister_fallout_component_transition_ms,
                config->sister_fallout_master_transition_ms,
                config->sister_fallout_rise_seconds,
                config->sister_capture_channels,
                config->sister_restart_clear ? 1 : 0,
                config->sister_input_percent,
                config->sister_tiles_percent,
                config->sister_fm_percent,
                config->sister_ext_percent,
                config->sister_audition_percent,
                config->sister_fx_return_percent,
                config->sister_dry_percent,
                config->sister_wet_percent,
                config->sister_output_percent,
                config->sister_erase_percent,
                config->sister_ghost_percent,
                config->sister_window_maximized ? 1 : 0,
                config->sister_window_x,
                config->sister_window_y) < 0;
    for (int slot = 0; slot < TS_DSP_FACTORY_RECIPE_COUNT && !write_failed; ++slot) {
        if (!config->dsp_factory_overridden[slot]) continue;
        write_failed = fprintf(file, "DspPreset%02d=%.9g,%.9g,%.9g,%.9g\n",
                               slot + 1,
                               config->dsp_factory_controls[slot][0],
                               config->dsp_factory_controls[slot][1],
                               config->dsp_factory_controls[slot][2],
                               config->dsp_factory_controls[slot][3]) < 0;
    }
    if (!write_failed)
        write_failed = fprintf(file,
                               "\n[CDP Processes]\n"
                               "; Stable recipe IDs: 1 enables a process, 0 hides it.\n"
                               "; At most 32 enabled processes are displayed.\n") < 0;
    for (size_t recipe = 0; recipe < ts_cdp_factory_recipe_count() &&
                            recipe < TS_CDP_CATALOG_CAPACITY && !write_failed;
         ++recipe) {
        const TsCdpRecipe *entry = ts_cdp_factory_recipe_at(recipe);
        if (entry == NULL) continue;
        write_failed = fprintf(file, "CdpProcess.%s=%d\n", entry->id,
                               config->cdp_process_enabled[recipe] ? 1 : 0) < 0;
    }
    if (!write_failed)
        write_failed = fprintf(file,
                               "\n[CDP Presets]\n"
                               "; Four musical values, MIX, and the accepted variation seed.\n") < 0;
    for (int slot = 0; slot < TS_CDP_FACTORY_RECIPE_COUNT && !write_failed; ++slot) {
        const TsCdpRecipe *recipe = ts_cdp_factory_recipe_at((size_t)slot);
        if (!config->cdp_factory_overridden[slot]) continue;
        write_failed = fprintf(file,
                               "CdpPreset.%s=%.9g,%.9g,%.9g,%.9g,%.9g,%llu\n",
                               recipe != NULL ? recipe->id : "unknown",
                               config->cdp_factory_controls[slot][0],
                               config->cdp_factory_controls[slot][1],
                               config->cdp_factory_controls[slot][2],
                               config->cdp_factory_controls[slot][3],
                               config->cdp_factory_mix[slot],
                               (unsigned long long)config->cdp_factory_seed[slot]) < 0;
    }
    if (fclose(file) != 0) write_failed = 1;
    if (write_failed) {
        set_error(error, error_size, "Could not finish writing config");
        return 0;
    }
    set_error(error, error_size, "");
    return 1;
}
