#include "tapesister/audio_config.h"
#include "tapesister/performance.h"

#include <math.h>
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
           expect(config.midi_input_device[0] == '\0',
                  "default MIDI device should use automatic first input") &&
           expect(config.midi_input_channel == TS_MIDI_INPUT_CHANNEL_DEFAULT,
                  "default MIDI channel should be omni") &&
           expect(config.audio_buffer_frames == TS_AUDIO_BUFFER_FRAMES_DEFAULT,
                  "audio buffer should default to the performance-safe size") &&
           expect(config.fm_output_percent == TS_FM_OUTPUT_PERCENT_DEFAULT,
                  "FM output trim should default to a conservative level") &&
           expect(config.master_output_percent ==
                      TS_MASTER_OUTPUT_PERCENT_DEFAULT,
                  "master output should default to exact unity") &&
           expect(config.record_input_channel == TS_RECORD_INPUT_CHANNEL_DEFAULT,
                  "default input channel should remain channel 1") &&
           expect(config.capture_auto_resize == 1,
                  "internal Capture auto resize should default on") &&
           expect(config.capture_max_seconds == TS_CAPTURE_MAX_SECONDS_DEFAULT,
                  "internal Capture should have a bounded default duration") &&
           expect(config.capture_channels == 1,
                  "legacy internal Capture should default to mono") &&
           expect(config.waveform_display_mode == TS_WAVEFORM_DISPLAY_STEREO,
                  "ordinary waveform should default to stereo display") &&
           expect(config.sister_waveform_display_mode == TS_WAVEFORM_DISPLAY_STEREO,
                  "Sister waveform should default to stereo display") &&
           expect(config.sister_buffer_seconds == 40 &&
                  config.sister_buffer_channels == 2,
                  "Sister storage should retain the Kafka foundation defaults") &&
           expect(config.sister_capture_channels == 1,
                  "Sister Capture should remain deliberately mono by default") &&
           expect(config.sister_limiter_enabled == 1 &&
                  fabsf(config.sister_limiter_ceiling_db + 1.0f) < 0.001f &&
                  fabsf(config.sister_limiter_lookahead_ms - 1.0f) < 0.001f &&
                  fabsf(config.sister_limiter_release_ms - 120.0f) < 0.001f,
                  "final limiter should default to transparent safety settings") &&
           expect(config.sister_fx_effect_transition_ms == 240000 &&
                  config.sister_fx_transition_ms == 240000 &&
                  config.sister_fallout_transition_ms == 240000 &&
                  config.sister_fallout_component_transition_ms == 240000 &&
                  config.sister_fallout_master_transition_ms == 240000,
                  "performance transitions should default to four minutes") &&
           expect(config.sister_fallout_rise_seconds == 3600,
                  "Fallout RISE should default to a one-hour ascent") &&
           expect(config.sister_window_maximized == 1,
                  "Sister window should open maximized by default") &&
           expect(config.sister_input_percent == 100 &&
                  config.sister_tiles_percent == 100 &&
                  config.sister_fm_percent == 100 &&
                  config.sister_ext_percent == 100 &&
                  config.sister_audition_percent == 100 &&
                  config.sister_fx_return_percent == 100 &&
                  config.sister_dry_percent == 100 &&
                  config.sister_wet_percent == 100 &&
                  config.sister_output_percent == 400 &&
                  config.sister_erase_percent == 100 &&
                  config.sister_ghost_percent == 0,
                  "Sister input/monitor/output/erase defaults should preserve PR5") &&
           expect(config.voice_attack_ms == TS_AUDITION_ATTACK_MS_DEFAULT,
                  "sample voices should default to a short de-click attack") &&
           expect(config.tile_fade_ms == TS_TILE_FADE_MS_DEFAULT,
                  "mouse-launched tiles should preserve instant legacy behavior by default");
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
    snprintf(saved.midi_input_device, sizeof(saved.midi_input_device),
             "Test MIDI Keyboard");
    saved.record_input_channel = 3;
    saved.audio_buffer_frames = 1024;
    saved.fm_output_percent = 37;
    saved.master_output_percent = 63;
    saved.midi_input_channel = 7;
    saved.capture_auto_resize = 0;
    saved.capture_max_seconds = 47;
    saved.capture_channels = 2;
    saved.waveform_display_mode = TS_WAVEFORM_DISPLAY_RIGHT;
    saved.sister_waveform_display_mode = TS_WAVEFORM_DISPLAY_MONO_SUM;
    saved.sister_buffer_seconds = 55;
    saved.sister_buffer_channels = 1;
    saved.sister_clear_ms = 33;
    saved.sister_limiter_enabled = 0;
    saved.sister_limiter_ceiling_db = -2.5f;
    saved.sister_limiter_lookahead_ms = 2.75f;
    saved.sister_limiter_release_ms = 333.0f;
    saved.sister_fx_effect_transition_ms = 234567;
    saved.sister_fx_transition_ms = 123456;
    saved.sister_fallout_transition_ms = 54321;
    saved.sister_fallout_component_transition_ms = 654321;
    saved.sister_fallout_master_transition_ms = 765432;
    saved.sister_fallout_rise_seconds = 12345;
    saved.sister_capture_channels = 2;
    saved.sister_restart_clear = 0;
    saved.sister_input_percent = 65;
    saved.sister_tiles_percent = 123;
    saved.sister_fm_percent = 234;
    saved.sister_ext_percent = 345;
    saved.sister_audition_percent = 67;
    saved.sister_fx_return_percent = 154;
    saved.sister_dry_percent = 35;
    saved.sister_wet_percent = 80;
    saved.sister_output_percent = 275;
    saved.sister_erase_percent = 20;
    saved.sister_ghost_percent = 67;
    saved.sister_window_maximized = 0;
    saved.sister_window_x = 123;
    saved.sister_window_y = 456;
    saved.voice_attack_ms = 7;
    saved.tile_fade_ms = 12345;
    saved.midi_map.takeover = TS_MIDI_TAKEOVER_JUMP;
    if (!expect(ts_midi_map_assign_trigger(
                    &saved.midi_map, "tile.01.launch",
                    (TsMidiSource){TS_MIDI_SOURCE_CC, 0, 32}, 1),
                "tile MIDI mapping should be accepted") ||
        !expect(ts_midi_map_assign(
                    &saved.midi_map, "sister.param.035",
                    (TsMidiSource){TS_MIDI_SOURCE_PITCH_BEND, 15, 0}),
                "14-bit Sister mapping should be accepted"))
        return 0;

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
         expect(strcmp(loaded.midi_input_device, "Test MIDI Keyboard") == 0,
                "named MIDI input should roundtrip") &&
         expect(loaded.midi_input_channel == 7,
                "MIDI input channel should roundtrip") &&
         expect(loaded.record_input_channel == 3,
                "stereo input mode should roundtrip") &&
         expect(loaded.audio_buffer_frames == 1024,
                "audio buffer size should roundtrip") &&
         expect(loaded.fm_output_percent == 37,
                "FM output trim should roundtrip") &&
         expect(loaded.master_output_percent == 63,
                "master output attenuation should roundtrip") &&
         expect(loaded.capture_auto_resize == 0,
                "Capture auto resize should roundtrip") &&
         expect(loaded.waveform_display_mode == TS_WAVEFORM_DISPLAY_RIGHT &&
                loaded.sister_waveform_display_mode == TS_WAVEFORM_DISPLAY_MONO_SUM,
                "waveform display modes should roundtrip") &&
         expect(loaded.sister_buffer_seconds == 55 &&
                loaded.sister_buffer_channels == 1 &&
                loaded.sister_clear_ms == 33 &&
                loaded.sister_limiter_enabled == 0 &&
                fabsf(loaded.sister_limiter_ceiling_db + 2.5f) < 0.001f &&
                fabsf(loaded.sister_limiter_lookahead_ms - 2.75f) < 0.001f &&
                fabsf(loaded.sister_limiter_release_ms - 333.0f) < 0.001f &&
                loaded.sister_fx_effect_transition_ms == 234567 &&
                loaded.sister_fx_transition_ms == 123456 &&
                loaded.sister_fallout_transition_ms == 54321 &&
                loaded.sister_fallout_component_transition_ms == 654321 &&
                loaded.sister_fallout_master_transition_ms == 765432 &&
                loaded.sister_fallout_rise_seconds == 12345,
                "Sister storage preferences should roundtrip") &&
         expect(loaded.sister_capture_channels == 2 &&
                loaded.sister_restart_clear == 0,
                "Sister capture/restart preferences should roundtrip") &&
         expect(loaded.sister_input_percent == 65 &&
                loaded.sister_tiles_percent == 123 &&
                loaded.sister_fm_percent == 234 &&
                loaded.sister_ext_percent == 345 &&
                loaded.sister_audition_percent == 67 &&
                loaded.sister_fx_return_percent == 154 &&
                loaded.sister_dry_percent == 35 &&
                loaded.sister_wet_percent == 80 &&
                loaded.sister_output_percent == 275 &&
                loaded.sister_erase_percent == 20 &&
                loaded.sister_ghost_percent == 67,
                "Sister input/monitor/output/erase preferences should roundtrip") &&
         expect(loaded.sister_window_maximized == 0 &&
                loaded.sister_window_x == 123 && loaded.sister_window_y == 456,
                "Sister window startup state and position should roundtrip") &&
         expect(loaded.capture_max_seconds == 47,
                "Capture duration limit should roundtrip") &&
         expect(loaded.capture_channels == 2,
                "stereo Capture choice should roundtrip") &&
         expect(loaded.voice_attack_ms == 7,
                "voice de-click attack should roundtrip") &&
         expect(loaded.tile_fade_ms == 12345,
                "mouse-launched tile fade should roundtrip") &&
         expect(loaded.midi_map.takeover == TS_MIDI_TAKEOVER_JUMP,
                "MIDI takeover mode should roundtrip") &&
         expect(loaded.midi_map.count == 2u &&
                ts_midi_map_find_target_const(
                    &loaded.midi_map, "tile.01.launch") != NULL &&
                ts_midi_map_find_target_const(
                    &loaded.midi_map,
                    "tile.01.launch")->trigger_on_zero == 1 &&
                ts_midi_map_find_target_const(
                    &loaded.midi_map, "sister.param.035") != NULL,
                "global MIDI mappings should roundtrip");
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
    saved.midi_input_device[0] = '\0';
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
                "blank output device should survive roundtrip") &&
         expect(loaded.midi_input_device[0] == '\0',
                "blank MIDI input should survive roundtrip") &&
         expect(loaded.midi_input_channel == 0,
                "omni MIDI channel should survive roundtrip");
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
          "record_max_seconds=20\n"
          "sister_fx_transition_ms=34567\n"
          "sister_fallout_component_transition_ms=45678\n", file);
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
                "legacy config should default output to system default") &&
         expect(loaded.midi_input_device[0] == '\0',
                "legacy config should default MIDI to auto") &&
         expect(loaded.midi_input_channel == 0,
                "legacy config should default MIDI to omni") &&
         expect(loaded.audio_buffer_frames == TS_AUDIO_BUFFER_FRAMES_DEFAULT,
                "legacy config should receive the safe audio buffer default") &&
         expect(loaded.voice_attack_ms == TS_AUDITION_ATTACK_MS_DEFAULT,
                "legacy config should receive the de-click default") &&
         expect(loaded.tile_fade_ms == TS_TILE_FADE_MS_DEFAULT,
                "legacy config should receive the tile fade default") &&
         expect(loaded.capture_channels == 1,
                "legacy config should receive mono Capture default") &&
         expect(loaded.sister_fx_transition_ms == 34567 &&
                loaded.sister_fx_effect_transition_ms == 34567,
                "legacy FX clock should seed both split timers") &&
         expect(loaded.sister_fallout_component_transition_ms == 45678 &&
                loaded.sister_fallout_master_transition_ms == 45678,
                "legacy Fallout clock should seed both split timers") &&
         expect(loaded.sister_limiter_enabled == 1 &&
                fabsf(loaded.sister_limiter_ceiling_db + 1.0f) < 0.001f,
                "legacy configs should receive the safe limiter default") &&
         expect(loaded.master_output_percent == 100,
                "legacy configs should receive unity master output");
    remove(path);
    return ok;
}

static int test_attack_clamp(void)
{
    static const char path[] = "test-audio-config-attack.ini";
    TsConfig loaded;
    char error[160];
    FILE *file = fopen(path, "wb");
    int ok;
    if (file == NULL) return 0;
    fputs("[Audition]\nvoice_attack_ms=200\ntile_fade_ms=40000\n", file);
    fclose(file);
    ok = ts_audio_config_load(&loaded, path, error, sizeof(error));
    if (!ok) {
        fprintf(stderr, "FAIL: attack clamp: %s\n", error);
        remove(path);
        return 0;
    }
    ok = expect(loaded.voice_attack_ms == TS_AUDITION_ATTACK_MS_MAX,
                "voice attack should clamp to the documented maximum") &&
         expect(loaded.tile_fade_ms == TS_TILE_FADE_MS_MAX,
                "tile fade should clamp to the documented maximum");
    remove(path);
    return ok;
}

static int test_shared_capture_format_compatibility(void)
{
    static const char legacy_path[] = "test-audio-config-capture-legacy.ini";
    static const char conflict_path[] = "test-audio-config-capture-conflict.ini";
    TsConfig loaded;
    char error[160];
    FILE *file = fopen(legacy_path, "wb");
    int ok;
    if (file == NULL) return 0;
    fputs("[Sister Machine]\nsister_capture_channels=2\n", file);
    fclose(file);
    ok = ts_audio_config_load(&loaded, legacy_path, error, sizeof(error)) &&
         expect(loaded.capture_channels == 2 &&
                loaded.sister_capture_channels == 2,
                "legacy Sister-only format should seed the shared setting");
    remove(legacy_path);
    if (!ok) return 0;

    file = fopen(conflict_path, "wb");
    if (file == NULL) return 0;
    fputs("[Internal Capture]\ncapture_channels=1\n"
          "[Sister Machine]\nsister_capture_channels=2\n", file);
    fclose(file);
    ok = ts_audio_config_load(&loaded, conflict_path, error, sizeof(error)) &&
         expect(loaded.capture_channels == 1 &&
                loaded.sister_capture_channels == 1,
                "main Capture format should resolve conflicting old settings");
    remove(conflict_path);
    return ok;
}

static int test_invalid_audio_buffer(void)
{
    static const char path[] = "test-audio-config-buffer.ini";
    TsConfig loaded;
    char error[160];
    FILE *file = fopen(path, "wb");
    int ok;
    if (file == NULL) return 0;
    fputs("[Audio]\naudio_buffer_frames=384\n", file);
    fclose(file);
    ok = expect(!ts_audio_config_load(&loaded, path, error, sizeof(error)),
                "unsupported audio buffer size should be rejected") &&
         expect(strstr(error, "256, 512, or 1024") != NULL,
                "invalid audio buffer should explain the supported values");
    remove(path);
    return ok;
}

int main(void)
{
    if (!test_defaults()) return 1;
    if (!test_roundtrip()) return 1;
    if (!test_blank_roundtrip()) return 1;
    if (!test_legacy_config()) return 1;
    if (!test_shared_capture_format_compatibility()) return 1;
    if (!test_attack_clamp()) return 1;
    if (!test_invalid_audio_buffer()) return 1;
    puts("audio config tests passed");
    return 0;
}
