/*
 * TapeSister SDL audio-device Config integration.
 *
 * This translation unit wraps main_sdl.c with the Config-driven SDL device
 * selection layer. Runtime recording behavior remains in main_sdl.c.
 */
#include "tapesister/audio_config.h"
#include "tapesister/ui.h"
#include "tapesister/capture.h"

#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static SDL_AudioDeviceID ts_audio_open_device(const char *device, int iscapture,
                                               const SDL_AudioSpec *desired,
                                               SDL_AudioSpec *obtained,
                                               int allowed_changes);
static void ts_audio_close_device(SDL_AudioDeviceID device);
static void ts_audio_lock_device(SDL_AudioDeviceID device);
static void ts_audio_unlock_device(SDL_AudioDeviceID device);
static void ts_audio_pause_device(SDL_AudioDeviceID device, int pause_on);
static void ts_audio_quit(void);
static int ts_audio_main_config_load(TsConfig *config, const char *path,
                                     char *error, size_t error_size);
static int ts_audio_main_config_save(const TsConfig *config, const char *path,
                                     char *error, size_t error_size);
static void ts_audio_ui_render(TsFramebuffer *fb, const TsUiState *ui,
                               const TsInstrument *instrument);
static int ts_audio_config_field_from_point(int x, int y);
static size_t ts_audio_config_cursor_from_point(const TsUiState *ui,
                                                TsConfigField field, int x);
static int ts_audio_external_recorder_arm(TsExternalRecorder *recorder,
                                          int destination_slot,
                                          uint32_t sample_rate,
                                          int threshold_db,
                                          int pre_roll_ms,
                                          int silence_ms,
                                          int tail_ms,
                                          int max_seconds,
                                          char *error, size_t error_size);

#define SDL_OpenAudioDevice ts_audio_open_device
#define SDL_CloseAudioDevice ts_audio_close_device
#define SDL_LockAudioDevice ts_audio_lock_device
#define SDL_UnlockAudioDevice ts_audio_unlock_device
#define SDL_PauseAudioDevice ts_audio_pause_device
#define SDL_Quit ts_audio_quit
#define ts_config_load ts_audio_main_config_load
#define ts_config_save ts_audio_main_config_save
#define ts_ui_render ts_audio_ui_render
#define ts_ui_config_field_from_point ts_audio_config_field_from_point
#define ts_ui_config_cursor_from_point ts_audio_config_cursor_from_point
#define ts_external_recorder_arm ts_audio_external_recorder_arm

#include "main_sdl.c"

#undef SDL_OpenAudioDevice
#undef SDL_CloseAudioDevice
#undef SDL_LockAudioDevice
#undef SDL_UnlockAudioDevice
#undef SDL_PauseAudioDevice
#undef SDL_Quit
#undef ts_config_load
#undef ts_config_save
#undef ts_ui_render
#undef ts_ui_config_field_from_point
#undef ts_ui_config_cursor_from_point
#undef ts_external_recorder_arm

enum {
    TS_AUDIO_CONFIG_PATH_Y = 58,
    TS_AUDIO_CONFIG_PATH_STEP = 19,
    TS_AUDIO_CONFIG_ROW_H = 16,
    TS_AUDIO_CONFIG_VALUE_X = 188,
    TS_AUDIO_CONFIG_INPUT_Y = 136,
    TS_AUDIO_CONFIG_OUTPUT_Y = 155,
    TS_AUDIO_CONFIG_INPUT_W = 430,
    TS_AUDIO_CONFIG_CHANNEL_X = 455,
    TS_AUDIO_CONFIG_CHANNEL_W = 165
};

#define TS_AUDIO_LOGICAL_OUTPUT ((SDL_AudioDeviceID)0x7fff0001u)
#define TS_AUDIO_LOGICAL_CAPTURE ((SDL_AudioDeviceID)0x7fff0002u)

typedef struct {
    char **input_names;
    int input_count;
    char **output_names;
    int output_count;
} TsAudioDeviceCatalog;

static SDL_AudioDeviceID ts_real_output;
static SDL_AudioDeviceID ts_real_capture;
static SDL_AudioSpec ts_output_desired;
static SDL_AudioSpec ts_capture_desired;
static SDL_AudioSpec *ts_output_obtained_target;
static int ts_output_allowed_changes;
static int ts_capture_allowed_changes;
static void *ts_output_userdata;
static void *ts_capture_userdata;
static TsConfig *ts_live_config;
static TsUiState *ts_live_ui;
static TsAudioDeviceCatalog ts_device_catalog;
static int ts_config_was_open;
static int ts_capture_reconfigure_pending;
static char ts_active_output[TS_CONFIG_PATH_MAX];
static char ts_active_capture[TS_CONFIG_PATH_MAX];
static char ts_pending_notice[160];
static char ts_capture_reconfigure_error[160];

static void ts_copy_name(char *destination, size_t size, const char *source)
{
    size_t length;
    if (destination == NULL || size == 0) return;
    if (source == NULL) source = "";
    length = strlen(source);
    if (length >= size) length = size - 1u;
    memcpy(destination, source, length);
    destination[length] = '\0';
}

static const char *ts_configured_output_name(void)
{
    return ts_live_config != NULL && ts_live_config->audio_output_device[0] != '\0' ?
           ts_live_config->audio_output_device : NULL;
}

static const char *ts_configured_input_name(void)
{
    return ts_live_config != NULL && ts_live_config->record_input_device[0] != '\0' ?
           ts_live_config->record_input_device : NULL;
}

static SDL_AudioDeviceID ts_audio_real_id(SDL_AudioDeviceID logical)
{
    if (logical == TS_AUDIO_LOGICAL_OUTPUT) return ts_real_output;
    if (logical == TS_AUDIO_LOGICAL_CAPTURE) return ts_real_capture;
    return logical;
}

static char *ts_audio_name_copy(const char *name)
{
    size_t length;
    char *copy;
    if (name == NULL) return NULL;
    length = strlen(name);
    copy = (char *)malloc(length + 1u);
    if (copy != NULL) memcpy(copy, name, length + 1u);
    return copy;
}

static void ts_audio_name_list_free(char ***names, int *count)
{
    if (names != NULL && *names != NULL) {
        for (int i = 0; i < *count; ++i) free((*names)[i]);
        free(*names);
        *names = NULL;
    }
    if (count != NULL) *count = 0;
}

static int ts_audio_name_list_refresh(int capture, char ***names, int *count)
{
    int available = SDL_GetNumAudioDevices(capture);
    char **made = NULL;
    int kept = 0;
    if (available < 0) available = 0;
    if (available > 0) {
        made = (char **)calloc((size_t)available, sizeof(*made));
        if (made == NULL) return 0;
        for (int i = 0; i < available; ++i) {
            const char *name = SDL_GetAudioDeviceName(i, capture);
