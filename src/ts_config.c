#include "tapesister/config.h"

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
        config->ripple_cut_crop_canvas = 0;
        config->reference_tone_volume = TS_REFERENCE_TONE_VOLUME_DEFAULT;
        config->record_input_channel = TS_RECORD_INPUT_CHANNEL_DEFAULT;
        config->midi_input_channel = TS_MIDI_INPUT_CHANNEL_DEFAULT;
        config->record_threshold_db = TS_RECORD_THRESHOLD_DB_DEFAULT;
        config->record_preroll_ms = TS_RECORD_PREROLL_MS_DEFAULT;
        config->record_silence_ms = TS_RECORD_SILENCE_MS_DEFAULT;
        config->record_tail_ms = TS_RECORD_TAIL_MS_DEFAULT;
        config->record_max_seconds = TS_RECORD_MAX_SECONDS_DEFAULT;
        config->capture_auto_resize = 1;
        config->capture_max_seconds = TS_CAPTURE_MAX_SECONDS_DEFAULT;
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
    int slot;
    float controls[TS_CDP_CONTROL_COUNT];
    float mix;
    unsigned long long seed;
    char trailing;
    if (sscanf(key, "CdpPreset%2d%c", &slot, &trailing) != 1 ||
        slot < 1 || slot > TS_CDP_FACTORY_RECIPE_COUNT)
        return 0;
    if (sscanf(value, "%f,%f,%f,%f,%f,%llu%c", &controls[0], &controls[1],
               &controls[2], &controls[3], &mix, &seed, &trailing) != 6)
        return -1;
    for (int index = 0; index < TS_CDP_CONTROL_COUNT; ++index)
        if (!isfinite(controls[index])) return -1;
    if (!isfinite(mix) || mix < 0.0f || mix > 1.0f) return -1;
    --slot;
    memcpy(config->cdp_factory_controls[slot], controls, sizeof(controls));
    config->cdp_factory_mix[slot] = mix;
    config->cdp_factory_seed[slot] = (uint64_t)seed;
    config->cdp_factory_overridden[slot] = 1;
    return 1;
}

int ts_config_load(TsConfig *config, const char *path,
                   char *error, size_t error_size)
{
    FILE *file;
    char line[TS_CONFIG_PATH_MAX + 80];
    TsConfig loaded;
    int line_number = 0;
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
        } else if (strcmp(key, "ripple_cut_crop_canvas") == 0) {
            if (!parse_boolean(value, &loaded.ripple_cut_crop_canvas)) { snprintf(error, error_size, "Invalid boolean on config line %d", line_number); fclose(file); return 0; }
        } else if (strcmp(key, "reference_tone_volume") == 0) {
            if (!parse_clamped_integer(value, TS_REFERENCE_TONE_VOLUME_MIN, TS_REFERENCE_TONE_VOLUME_MAX, &loaded.reference_tone_volume)) { snprintf(error, error_size, "Invalid integer on config line %d", line_number); fclose(file); return 0; }
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
        } else {
            int dsp = parse_dsp_preset(key, value, &loaded);
            int cdp = dsp == 0 ? parse_cdp_preset(key, value, &loaded) : 0;
            if (dsp < 0 || cdp < 0 ||
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
                "; Per-voice note-on de-click ramp in milliseconds; 0 disables it, 20 is the maximum.\n"
                "voice_attack_ms=%d\n"
                "\n[External Recording]\n"
                "; Blank uses the operating system default capture device. Otherwise use the exact SDL device name.\n"
                "record_input_device=%s\n"
                "; 0 mixes all input channels to mono, 1 records the first/left channel, 2 the second/right.\n"
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
                config->voice_attack_ms,
                config->record_input_device,
                config->record_input_channel,
                config->record_threshold_db,
                config->record_preroll_ms,
                config->record_silence_ms,
                config->record_tail_ms,
                config->record_max_seconds,
                config->capture_auto_resize ? 1 : 0,
                config->capture_max_seconds) < 0;
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
                               "\n[CDP Presets]\n"
                               "; Four musical values, MIX, and the accepted variation seed.\n") < 0;
    for (int slot = 0; slot < TS_CDP_FACTORY_RECIPE_COUNT && !write_failed; ++slot) {
        if (!config->cdp_factory_overridden[slot]) continue;
        write_failed = fprintf(file,
                               "CdpPreset%02d=%.9g,%.9g,%.9g,%.9g,%.9g,%llu\n",
                               slot + 1,
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
