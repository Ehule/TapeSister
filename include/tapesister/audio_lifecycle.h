#ifndef TAPESISTER_AUDIO_LIFECYCLE_H
#define TAPESISTER_AUDIO_LIFECYCLE_H

#include <stddef.h>
#include <stdint.h>

enum {
    TS_AUDIO_ENDPOINT_NAME_MAX = 1024,
    TS_AUDIO_ENDPOINT_ERROR_MAX = 192,
    TS_AUDIO_ENDPOINT_FORMAT_MAX = 24
};

typedef enum {
    TS_AUDIO_BACKEND_AUTO = 0,
    TS_AUDIO_BACKEND_WASAPI,
    TS_AUDIO_BACKEND_DIRECTSOUND
} TsAudioBackend;

typedef enum {
    TS_AUDIO_ENDPOINT_OUTPUT = 0,
    TS_AUDIO_ENDPOINT_CAPTURE
} TsAudioEndpointRole;

typedef enum {
    TS_AUDIO_CONNECTION_CLOSED = 0,
    TS_AUDIO_CONNECTION_OPENING,
    TS_AUDIO_CONNECTION_ACTIVE,
    TS_AUDIO_CONNECTION_LOST,
    TS_AUDIO_CONNECTION_RETRY_PENDING,
    TS_AUDIO_CONNECTION_FALLBACK_ACTIVE
} TsAudioConnectionState;

typedef struct {
    TsAudioEndpointRole role;
    TsAudioConnectionState state;
    uint32_t logical_id;
    uint32_t real_id;
    char configured_name[TS_AUDIO_ENDPOINT_NAME_MAX];
    char active_name[TS_AUDIO_ENDPOINT_NAME_MAX];
    char last_error[TS_AUDIO_ENDPOINT_ERROR_MAX];
    char format[TS_AUDIO_ENDPOINT_FORMAT_MAX];
    uint32_t sample_rate;
    uint32_t buffer_frames;
    uint8_t channels;
    int explicit_device;
    int fallback_approved;
} TsAudioEndpoint;

int ts_audio_backend_parse(const char *text, TsAudioBackend *backend);
const char *ts_audio_backend_name(TsAudioBackend backend);
const char *ts_audio_backend_sdl_driver(TsAudioBackend backend);

void ts_audio_endpoint_init(TsAudioEndpoint *endpoint,
                            TsAudioEndpointRole role, uint32_t logical_id);
void ts_audio_endpoint_configure(TsAudioEndpoint *endpoint,
                                 const char *configured_name);
void ts_audio_endpoint_begin_open(TsAudioEndpoint *endpoint,
                                  int fallback_approved);
void ts_audio_endpoint_opened(TsAudioEndpoint *endpoint, uint32_t real_id,
                              const char *active_name, int fallback_active,
                              uint32_t sample_rate, uint8_t channels,
                              uint32_t buffer_frames, const char *format);
void ts_audio_endpoint_open_failed(TsAudioEndpoint *endpoint,
                                   const char *error);
int ts_audio_endpoint_matches_removed(const TsAudioEndpoint *endpoint,
                                      int is_capture, uint32_t real_event_id);
uint32_t ts_audio_endpoint_removed(TsAudioEndpoint *endpoint,
                                   const char *error);
uint32_t ts_audio_endpoint_closed(TsAudioEndpoint *endpoint);
int ts_audio_endpoint_should_retry(const TsAudioEndpoint *endpoint);
int ts_audio_endpoint_may_open_default(const TsAudioEndpoint *endpoint,
                                       int fallback_approved);
const char *ts_audio_connection_state_name(TsAudioConnectionState state);
int ts_audio_endpoint_format_diagnostic(const TsAudioEndpoint *endpoint,
                                        char *text, size_t text_size);

#endif
