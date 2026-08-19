#include "tapesister/audio_config.h"

#include <stdio.h>
#include <string.h>

static int expect(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        return 0;
    }
    return 1;
}

static int test_defaults(void)
{
    TsConfig config;
    ts_config_init(&config);
    return expect(config.record_input_device[0] == '\0',
                  "default input device should be system default") &&
           expect(config.audio_output_device[0] == '\0',
                  "default output device should be system default") &&
           expect(config.record_input_channel == TS_RECORD_INPUT_CHANNEL_DEFAULT,
                  "default input channel should remain channel 1");
}

static int test_roundtrip(void)
{
    static const char path[] = "test-audio-config.ini";
    TsConfig saved;
    TsConfig loaded;
    char error[160];
    int ok;

    ts_config_init(&saved);
    snprintf(saved.record_input_device, sizeof(saved.record_input_device),
             "Test Capture Device");
    snprintf(saved.audio_output_device, sizeof(saved.audio_output_device),
             "Test Playback Device");
    saved.record_input_channel = 2;

    ok = ts_audio_config_save(&saved, path, error, sizeof(error)) &&
         ts_audio_config_load(&loaded, path, error, sizeof(error));
    if (!ok) {
        fprintf(stderr, "FAIL: roundtrip: %s\n", error);
        remove(path);
        return 0;
    }

    ok = expect(strcmp(loaded.record_input_device, "Test Capture Device") == 0,
                "named input device should roundtrip") &&
         expect(strcmp(loaded.audio_output_device, "Test Playback Device") == 0,
                "named output device should roundtrip") &&
         expect(loaded.record_input_channel == 2,
                "input channel should roundtrip");
    remove(path);
    return ok;
}

static int test_blank_roundtrip(void)
{
    static const char path[] = "test-audio-config-blank.ini";
    TsConfig saved;
    TsConfig loaded;
    char error[160];
    int ok;

    ts_config_init(&saved);
    saved.record_input_device[0] = '\0';
    saved.audio_output_device[0] = '\0';
    saved.record_input_channel = 1;
    ok = ts_audio_config_save(&saved, path, error, sizeof(error)) &&
         ts_audio_config_load(&loaded, path, error, sizeof(error));
    if (!ok) {
        fprintf(stderr, "FAIL: blank roundtrip: %s\n", error);
        remove(path);
        return 0;
    }
    ok = expect(loaded.record_input_device[0] == '\0',
                "blank input device should survive roundtrip") &&
         expect(loaded.audio_output_device[0] == '\0',
                "blank output device should survive roundtrip");
    remove(path);
    return ok;
}

static int test_legacy_config(void)
{
    static const char path[] = "test-audio-config-legacy.ini";
    TsConfig loaded;
    char error[160];
    FILE *file = fopen(path, "wb");
    int ok;
    if (file == NULL) return 0;
    fputs("[External Recording]\n"
          "record_input_device=Legacy Input\n"
          "record_input_channel=2\n"
          "record_threshold_db=-30\n"
          "record_preroll_ms=180\n"
          "record_silence_ms=650\n"
          "record_tail_ms=180\n"
          "record_max_seconds=20\n", file);
    fclose(file);

    ok = ts_audio_config_load(&loaded, path, error, sizeof(error));
    if (!ok) {
        fprintf(stderr, "FAIL: legacy load: %s\n", error);
        remove(path);
        return 0;
    }
    ok = expect(strcmp(loaded.record_input_device, "Legacy Input") == 0,
                "legacy input setting should load") &&
         expect(loaded.record_input_channel == 2,
                "legacy input channel should load") &&
         expect(loaded.audio_output_device[0] == '\0',
                "legacy config should default output to system default");
    remove(path);
    return ok;
}

int main(void)
{
    if (!test_defaults()) return 1;
    if (!test_roundtrip()) return 1;
    if (!test_blank_roundtrip()) return 1;
    if (!test_legacy_config()) return 1;
    puts("audio config tests passed");
    return 0;
}
