#include "tapesister/config.h"

#include <ctype.h>
#include <errno.h>
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
            if (!copy_value(loaded.sample_path, value, error, error_size)) {
                fclose(file);
                return 0;
            }
        } else if (strcmp(key, "FastTrackerPath") == 0) {
            if (!copy_value(loaded.fasttracker_path, value, error, error_size)) {
                fclose(file);
                return 0;
            }
        } else if (strcmp(key, "ExchangePath") == 0) {
            if (!copy_value(loaded.exchange_path, value, error, error_size)) {
                fclose(file);
                return 0;
            }
        } else if (strcmp(key, "CdpBinPath") == 0) {
            if (!copy_value(loaded.cdp_bin_path, value, error, error_size)) {
                fclose(file);
                return 0;
            }
        } else if (strcmp(key, "startup_welcome_sample") == 0) {
            if (!parse_boolean(value, &loaded.startup_welcome_sample)) {
                snprintf(error, error_size, "Invalid boolean on config line %d", line_number);
                fclose(file); return 0;
            }
        } else if (strcmp(key, "startup_welcome_autoplay") == 0) {
            if (!parse_boolean(value, &loaded.startup_welcome_autoplay)) {
                snprintf(error, error_size, "Invalid boolean on config line %d", line_number);
                fclose(file); return 0;
            }
        } else if (strcmp(key, "playhead_zero_snap") == 0) {
            if (!parse_boolean(value, &loaded.playhead_zero_snap)) {
                snprintf(error, error_size, "Invalid boolean on config line %d", line_number);
                fclose(file); return 0;
            }
        } else if (strcmp(key, "rotate_wheel_fine") == 0) {
            if (!parse_clamped_integer(value, TS_ROTATE_WHEEL_FINE_MIN,
                                       TS_ROTATE_WHEEL_FINE_MAX,
                                       &loaded.rotate_wheel_fine)) {
                snprintf(error, error_size, "Invalid integer on config line %d", line_number);
                fclose(file); return 0;
            }
        } else if (strcmp(key, "rotate_wheel_coarse") == 0) {
            if (!parse_clamped_integer(value, TS_ROTATE_WHEEL_COARSE_MIN,
                                       TS_ROTATE_WHEEL_COARSE_MAX,
                                       &loaded.rotate_wheel_coarse)) {
                snprintf(error, error_size, "Invalid integer on config line %d", line_number);
                fclose(file); return 0;
            }
        } else if (strcmp(key, "drone_crossfade_ms") == 0) {
            if (!parse_clamped_integer(value, TS_DRONE_CROSSFADE_MS_MIN,
                                       TS_DRONE_CROSSFADE_MS_MAX,
                                       &loaded.drone_crossfade_ms)) {
                snprintf(error, error_size, "Invalid integer on config line %d", line_number);
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
                "; Optional development/runtime override containing pvoc and glisten.\n"
                "CdpBinPath=%s\n"
                "\n[Startup]\n"
                "startup_welcome_sample=%d\n"
                "startup_welcome_autoplay=%d\n"
                "\n[Waveform]\n"
                "playhead_zero_snap=%d\n"
                "rotate_wheel_fine=%d\n"
                "rotate_wheel_coarse=%d\n"
                "drone_crossfade_ms=%d\n",
                config->sample_path, config->fasttracker_path,
                config->exchange_path, config->cdp_bin_path,
                config->startup_welcome_sample,
                config->startup_welcome_autoplay, config->playhead_zero_snap,
                config->rotate_wheel_fine,
                config->rotate_wheel_coarse,
                config->drone_crossfade_ms) < 0;
    if (fclose(file) != 0) write_failed = 1;
    if (write_failed) {
        set_error(error, error_size, "Could not finish writing config");
        return 0;
    }
    set_error(error, error_size, "");
    return 1;
}
