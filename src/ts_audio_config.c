#include "tapesister/audio_config.h"

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

static char *trim(char *text)
{
    char *end;
    while (*text != '\0' && isspace((unsigned char)*text)) ++text;
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) --end;
    *end = '\0';
    return text;
}

int ts_audio_buffer_frames_valid(int frames)
{
    return frames == 256 || frames == 512 || frames == 1024;
}

static int load_device_settings(TsConfig *config, const char *path,
                                char *error, size_t error_size)
{
    FILE *file;
    char line[TS_CONFIG_PATH_MAX + 80];

    file = fopen(path, "rb");
    if (file == NULL) {
        if (errno == ENOENT) return 1;
        snprintf(error, error_size, "Could not reopen config audio section: %s",
                 strerror(errno));
        return 0;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        char *key = trim(line);
        char *equals;
        char *value;
        size_t length;
        if (*key == '\0' || *key == ';' || *key == '#' || *key == '[')
            continue;
        equals = strchr(key, '=');
        if (equals == NULL) continue;
        *equals = '\0';
        value = trim(equals + 1);
        key = trim(key);
        if (strcmp(key, "audio_buffer_frames") == 0) {
            char *end;
            long frames = strtol(value, &end, 10);
            if (end == value || *end != '\0' ||
                !ts_audio_buffer_frames_valid((int)frames)) {
                fclose(file);
                set_error(error, error_size,
                          "Configured audio buffer must be 256, 512, or 1024 frames");
                return 0;
            }
            config->audio_buffer_frames = (int)frames;
            continue;
        }
        if (strcmp(key, "midi_input_channel") == 0) {
            char *end;
            long channel = strtol(value, &end, 10);
            if (end == value || *end != '\0') {
                fclose(file);
                set_error(error, error_size, "Configured MIDI channel is invalid");
                return 0;
            }
            if (channel < TS_MIDI_INPUT_CHANNEL_MIN)
                channel = TS_MIDI_INPUT_CHANNEL_MIN;
            if (channel > TS_MIDI_INPUT_CHANNEL_MAX)
                channel = TS_MIDI_INPUT_CHANNEL_MAX;
            config->midi_input_channel = (int)channel;
            continue;
        }
        if (strcmp(key, "audio_output_device") == 0) {
            length = strlen(value);
            if (length >= sizeof(config->audio_output_device)) {
                fclose(file);
                set_error(error, error_size,
                          "Configured audio output device name is too long");
                return 0;
            }
            memcpy(config->audio_output_device, value, length + 1u);
        } else if (strcmp(key, "midi_input_device") == 0) {
            length = strlen(value);
            if (length >= sizeof(config->midi_input_device)) {
                fclose(file);
                set_error(error, error_size,
                          "Configured MIDI input device name is too long");
                return 0;
            }
            memcpy(config->midi_input_device, value, length + 1u);
        }
    }

    if (ferror(file)) {
        fclose(file);
        set_error(error, error_size, "Could not finish reading config audio section");
        return 0;
    }
    fclose(file);
    return 1;
}

int ts_audio_config_load(TsConfig *config, const char *path,
                         char *error, size_t error_size)
{
    if (!ts_config_load(config, path, error, error_size)) return 0;
    if (!load_device_settings(config, path, error, error_size)) return 0;
    set_error(error, error_size, "");
    return 1;
}

int ts_audio_config_save(const TsConfig *config, const char *path,
                         char *error, size_t error_size)
{
    FILE *file;
    if (!ts_config_save(config, path, error, error_size)) return 0;

    file = fopen(path, "ab");
    if (file == NULL) {
        snprintf(error, error_size, "Could not append audio config: %s",
                 strerror(errno));
        return 0;
    }
    if (fprintf(file,
                "\n[Audio]\n"
                "; Blank uses the operating system default stereo playback device.\n"
                "audio_output_device=%s\n"
                "; Shared playback/capture callback size: 256, 512, or 1024 frames.\n"
                "audio_buffer_frames=%d\n"
                "\n[MIDI]\n"
                "; Blank automatically opens the first input; OFF disables MIDI.\n"
                "midi_input_device=%s\n"
                "; 0 listens on all channels; 1-16 selects one channel.\n"
                "midi_input_channel=%d\n",
                config->audio_output_device, config->audio_buffer_frames,
                config->midi_input_device,
                config->midi_input_channel) < 0) {
        fclose(file);
        set_error(error, error_size, "Could not write audio config");
        return 0;
    }
    if (fclose(file) != 0) {
        set_error(error, error_size, "Could not finish writing audio config");
        return 0;
    }

    set_error(error, error_size, "");
    return 1;
}
