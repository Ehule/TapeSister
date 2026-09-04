#include "tapesister/audio_lifecycle.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static int text_equal_ci(const char *left, const char *right)
{
    if (left == NULL || right == NULL) return 0;
    while (*left != '\0' && *right != '\0') {
        if (tolower((unsigned char)*left) != tolower((unsigned char)*right))
            return 0;
        ++left;
        ++right;
    }
    return *left == '\0' && *right == '\0';
}

static void copy_text(char *destination, size_t size, const char *source)
{
    if (destination == NULL || size == 0u) return;
    if (source == NULL) source = "";
    snprintf(destination, size, "%s", source);
}

int ts_audio_backend_parse(const char *text, TsAudioBackend *backend)
{
    if (backend == NULL) return 0;
    *backend = TS_AUDIO_BACKEND_AUTO;
    if (text == NULL || text[0] == '\0' || text_equal_ci(text, "auto"))
        return 1;
    if (text_equal_ci(text, "wasapi")) {
        *backend = TS_AUDIO_BACKEND_WASAPI;
        return 1;
    }
    if (text_equal_ci(text, "directsound")) {
        *backend = TS_AUDIO_BACKEND_DIRECTSOUND;
        return 1;
    }
    return 0;
}

const char *ts_audio_backend_name(TsAudioBackend backend)
{
    if (backend == TS_AUDIO_BACKEND_WASAPI) return "WASAPI";
    if (backend == TS_AUDIO_BACKEND_DIRECTSOUND) return "DirectSound";
    return "Auto";
}

const char *ts_audio_backend_sdl_driver(TsAudioBackend backend)
{
    if (backend == TS_AUDIO_BACKEND_WASAPI) return "wasapi";
    if (backend == TS_AUDIO_BACKEND_DIRECTSOUND) return "directsound";
    return NULL;
}

void ts_audio_endpoint_init(TsAudioEndpoint *endpoint,
                            TsAudioEndpointRole role, uint32_t logical_id)
{
    if (endpoint == NULL) return;
    memset(endpoint, 0, sizeof(*endpoint));
    endpoint->role = role;
    endpoint->state = TS_AUDIO_CONNECTION_CLOSED;
    endpoint->logical_id = logical_id;
}

void ts_audio_endpoint_configure(TsAudioEndpoint *endpoint,
                                 const char *configured_name)
{
    if (endpoint == NULL) return;
    copy_text(endpoint->configured_name, sizeof(endpoint->configured_name),
              configured_name);
    endpoint->explicit_device = endpoint->configured_name[0] != '\0';
}

void ts_audio_endpoint_begin_open(TsAudioEndpoint *endpoint,
                                  int fallback_approved)
{
    if (endpoint == NULL) return;
    endpoint->state = TS_AUDIO_CONNECTION_OPENING;
    endpoint->fallback_approved = fallback_approved != 0;
}

void ts_audio_endpoint_opened(TsAudioEndpoint *endpoint, uint32_t real_id,
                              const char *active_name, int fallback_active,
                              uint32_t sample_rate, uint8_t channels,
                              uint32_t buffer_frames, const char *format)
{
    if (endpoint == NULL || real_id == 0u) return;
    if (fallback_active && endpoint->explicit_device &&
        !endpoint->fallback_approved)
        return;
    endpoint->real_id = real_id;
    endpoint->state = fallback_active ? TS_AUDIO_CONNECTION_FALLBACK_ACTIVE :
                                        TS_AUDIO_CONNECTION_ACTIVE;
    endpoint->fallback_approved = fallback_active != 0;
    copy_text(endpoint->active_name, sizeof(endpoint->active_name), active_name);
    endpoint->sample_rate = sample_rate;
    endpoint->channels = channels;
    endpoint->buffer_frames = buffer_frames;
    copy_text(endpoint->format, sizeof(endpoint->format), format);
    endpoint->last_error[0] = '\0';
}

void ts_audio_endpoint_open_failed(TsAudioEndpoint *endpoint,
                                   const char *error)
{
    if (endpoint == NULL) return;
    endpoint->real_id = 0u;
    endpoint->active_name[0] = '\0';
    endpoint->sample_rate = 0u;
    endpoint->channels = 0u;
    endpoint->buffer_frames = 0u;
    endpoint->format[0] = '\0';
    endpoint->fallback_approved = 0;
    endpoint->state = TS_AUDIO_CONNECTION_RETRY_PENDING;
    copy_text(endpoint->last_error, sizeof(endpoint->last_error), error);
}

int ts_audio_endpoint_matches_removed(const TsAudioEndpoint *endpoint,
                                      int is_capture, uint32_t real_event_id)
{
    int role_matches;
    if (endpoint == NULL || endpoint->real_id == 0u || real_event_id == 0u)
        return 0;
    role_matches = endpoint->role == TS_AUDIO_ENDPOINT_CAPTURE ?
                   is_capture != 0 : is_capture == 0;
    return role_matches && endpoint->real_id == real_event_id;
}

uint32_t ts_audio_endpoint_removed(TsAudioEndpoint *endpoint,
                                   const char *error)
{
    uint32_t real_id;
    if (endpoint == NULL || endpoint->real_id == 0u) return 0u;
    real_id = endpoint->real_id;
    endpoint->real_id = 0u;
    endpoint->state = TS_AUDIO_CONNECTION_LOST;
    endpoint->active_name[0] = '\0';
    endpoint->sample_rate = 0u;
    endpoint->channels = 0u;
    endpoint->buffer_frames = 0u;
    endpoint->format[0] = '\0';
    endpoint->fallback_approved = 0;
    copy_text(endpoint->last_error, sizeof(endpoint->last_error), error);
    return real_id;
}

uint32_t ts_audio_endpoint_closed(TsAudioEndpoint *endpoint)
{
    uint32_t real_id;
    if (endpoint == NULL) return 0u;
    real_id = endpoint->real_id;
    endpoint->real_id = 0u;
    endpoint->state = TS_AUDIO_CONNECTION_CLOSED;
    endpoint->active_name[0] = '\0';
    endpoint->sample_rate = 0u;
    endpoint->channels = 0u;
    endpoint->buffer_frames = 0u;
    endpoint->format[0] = '\0';
    endpoint->fallback_approved = 0;
    return real_id;
}

int ts_audio_endpoint_should_retry(const TsAudioEndpoint *endpoint)
{
    return endpoint != NULL &&
           (endpoint->state == TS_AUDIO_CONNECTION_LOST ||
            endpoint->state == TS_AUDIO_CONNECTION_RETRY_PENDING);
}

int ts_audio_endpoint_may_open_default(const TsAudioEndpoint *endpoint,
                                       int fallback_approved)
{
    if (endpoint == NULL) return 0;
    return !endpoint->explicit_device || fallback_approved != 0;
}

const char *ts_audio_connection_state_name(TsAudioConnectionState state)
{
    switch (state) {
    case TS_AUDIO_CONNECTION_CLOSED: return "closed";
    case TS_AUDIO_CONNECTION_OPENING: return "opening";
    case TS_AUDIO_CONNECTION_ACTIVE: return "active";
    case TS_AUDIO_CONNECTION_LOST: return "lost";
    case TS_AUDIO_CONNECTION_RETRY_PENDING: return "retry-pending";
    case TS_AUDIO_CONNECTION_FALLBACK_ACTIVE: return "fallback-active";
    default: return "unknown";
    }
}

int ts_audio_endpoint_format_diagnostic(const TsAudioEndpoint *endpoint,
                                        char *text, size_t text_size)
{
    int written;
    const char *configured;
    const char *active;
    if (endpoint == NULL || text == NULL || text_size == 0u) return 0;
    configured = endpoint->explicit_device ? endpoint->configured_name :
                                             "SYSTEM DEFAULT";
    active = endpoint->active_name[0] != '\0' ? endpoint->active_name : "NONE";
    written = snprintf(
        text, text_size,
        "%s state=%s configured=\"%s\" active=\"%s\" logical=%u real=%u "
        "rate=%u format=%s channels=%u buffer=%u fallback=%s error=\"%s\"",
        endpoint->role == TS_AUDIO_ENDPOINT_CAPTURE ? "capture" : "output",
        ts_audio_connection_state_name(endpoint->state), configured, active,
        endpoint->logical_id, endpoint->real_id, endpoint->sample_rate,
        endpoint->format[0] != '\0' ? endpoint->format : "none",
        (unsigned)endpoint->channels, endpoint->buffer_frames,
        endpoint->state == TS_AUDIO_CONNECTION_FALLBACK_ACTIVE ? "approved" :
                                                                  "no",
        endpoint->last_error);
    return written >= 0 && (size_t)written < text_size;
}
