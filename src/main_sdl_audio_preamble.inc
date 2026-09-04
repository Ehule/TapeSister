/*
 * Deliberately retained SDL/config/UI compatibility shims.
 *
 * main_sdl.c is the canonical translation unit and includes this preamble
 * itself.  These wrappers only coordinate device lifecycle, persistence, and
 * the existing extended UI.  Runtime note, performance, Capture, external
 * input, and mixer behavior must use ordinary functions and AudioState.
 */
#include "tapesister/audio_config.h"
#include "tapesister/audio_lifecycle.h"
#include "tapesister/audio_mixer.h"
#include "tapesister/midi_input.h"
#include "tapesister/performance.h"

static SDL_AudioDeviceID ts_audio_open_device(const char *device, int iscapture,
                                               const SDL_AudioSpec *desired,
                                               SDL_AudioSpec *obtained,
                                               int allowed_changes);
static SDL_AudioDeviceID ts_audio_open_capture_widest(
    const char *device, const SDL_AudioSpec *desired, SDL_AudioSpec *obtained,
    int allowed_changes);
static void ts_audio_close_device(SDL_AudioDeviceID device);
static void ts_audio_lock_device(SDL_AudioDeviceID device);
static void ts_audio_unlock_device(SDL_AudioDeviceID device);
static void ts_audio_pause_device(SDL_AudioDeviceID device, int pause_on);
static int ts_audio_apply_backend_before_sdl(TsAudioBackend backend);
static int ts_audio_log_active_backend(TsAudioBackend backend);
static int ts_audio_device_event_matches(int is_capture,
                                         SDL_AudioDeviceID real_event_id);
static void ts_audio_handle_removed(int is_capture, const char *error);
static int ts_audio_endpoint_should_retry_output(void);
static int ts_audio_endpoint_should_retry_capture(void);
static int ts_audio_retry_capture_for_event(char *error, size_t error_size);
static int ts_audio_output_is_available(void);
static int ts_audio_resolve_output_failure(SDL_Window *window);
static int ts_switch_output_for_config(char *error, size_t error_size);
static void ts_audio_catalog_refresh(void);
static void ts_audio_quit(void);
static int ts_audio_main_config_load(TsConfig *config, const char *path,
                                     char *error, size_t error_size);
static int ts_audio_main_config_write(const TsConfig *config, const char *path,
                                      char *error, size_t error_size);
static int ts_audio_main_config_save(const TsConfig *config, const char *path,
                                     char *error, size_t error_size);
static void ts_audio_ui_render(TsFramebuffer *fb, const TsUiState *ui,
                               const TsInstrument *instrument);
static int ts_audio_config_field_from_point(int x, int y);
static size_t ts_audio_config_cursor_from_point(const TsUiState *ui,
                                                TsConfigField field, int x);
static TsMidiInput *ts_midi_input;

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
