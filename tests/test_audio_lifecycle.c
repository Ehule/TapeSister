#include "tapesister/audio_lifecycle.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); \
        return 1; \
    } \
} while (0)

int main(void)
{
    TsAudioBackend backend;
    TsAudioEndpoint output;
    TsAudioEndpoint capture;
    char diagnostic[1024];

    CHECK(ts_audio_backend_parse("Auto", &backend));
    CHECK(backend == TS_AUDIO_BACKEND_AUTO);
    CHECK(ts_audio_backend_parse("WASAPI", &backend));
    CHECK(backend == TS_AUDIO_BACKEND_WASAPI);
    CHECK(strcmp(ts_audio_backend_sdl_driver(backend), "wasapi") == 0);
    CHECK(ts_audio_backend_parse("directsound", &backend));
    CHECK(backend == TS_AUDIO_BACKEND_DIRECTSOUND);
    CHECK(!ts_audio_backend_parse("ASIO", &backend));
    CHECK(backend == TS_AUDIO_BACKEND_AUTO);
    CHECK(ts_audio_backend_sdl_driver(backend) == NULL);

    ts_audio_endpoint_init(&output, TS_AUDIO_ENDPOINT_OUTPUT, 0x7fff0001u);
    ts_audio_endpoint_configure(&output, "MOTU M6");
    CHECK(output.explicit_device);
    CHECK(!ts_audio_endpoint_may_open_default(&output, 0));
    CHECK(ts_audio_endpoint_may_open_default(&output, 1));
    ts_audio_endpoint_begin_open(&output, 0);
    ts_audio_endpoint_open_failed(&output, "device busy");
    CHECK(output.state == TS_AUDIO_CONNECTION_RETRY_PENDING);
    CHECK(output.real_id == 0u);
    CHECK(ts_audio_endpoint_should_retry(&output));
    ts_audio_endpoint_begin_open(&output, 0);
    ts_audio_endpoint_opened(&output, 50u, "SYSTEM DEFAULT", 1,
                             48000u, 2u, 512u, "F32");
    CHECK(output.real_id == 0u);
    CHECK(output.state == TS_AUDIO_CONNECTION_OPENING);
    ts_audio_endpoint_begin_open(&output, 0);
    ts_audio_endpoint_opened(&output, 41u, "MOTU M6", 0,
                             48000u, 2u, 512u, "F32");
    CHECK(output.state == TS_AUDIO_CONNECTION_ACTIVE);
    CHECK(output.real_id == 41u);
    CHECK(!ts_audio_endpoint_matches_removed(&output, 1, 41u));
    CHECK(!ts_audio_endpoint_matches_removed(&output, 0, 42u));
    CHECK(ts_audio_endpoint_matches_removed(&output, 0, 41u));
    CHECK(ts_audio_endpoint_removed(&output, "device removed") == 41u);
    CHECK(output.state == TS_AUDIO_CONNECTION_LOST && output.real_id == 0u);
    CHECK(ts_audio_endpoint_removed(&output, "duplicate") == 0u);
    CHECK(ts_audio_endpoint_should_retry(&output));

    ts_audio_endpoint_begin_open(&output, 1);
    ts_audio_endpoint_opened(&output, 52u, "SYSTEM DEFAULT", 1,
                             44100u, 2u, 1024u, "F32");
    CHECK(output.state == TS_AUDIO_CONNECTION_FALLBACK_ACTIVE);
    CHECK(strcmp(output.configured_name, "MOTU M6") == 0);
    CHECK(output.fallback_approved);
    CHECK(ts_audio_endpoint_format_diagnostic(
              &output, diagnostic, sizeof(diagnostic)));
    CHECK(strstr(diagnostic, "configured=\"MOTU M6\"") != NULL);
    CHECK(strstr(diagnostic, "fallback=approved") != NULL);
    CHECK(ts_audio_endpoint_closed(&output) == 52u);
    CHECK(ts_audio_endpoint_closed(&output) == 0u);

    ts_audio_endpoint_configure(&output, "");
    CHECK(!output.explicit_device);
    CHECK(ts_audio_endpoint_may_open_default(&output, 0));
    ts_audio_endpoint_begin_open(&output, 0);
    ts_audio_endpoint_opened(&output, 88u, "SYSTEM DEFAULT", 0,
                             48000u, 2u, 512u, "F32");

    ts_audio_endpoint_init(&capture, TS_AUDIO_ENDPOINT_CAPTURE, 0x7fff0002u);
    ts_audio_endpoint_configure(&capture, "CABLE Output");
    CHECK(!ts_audio_endpoint_may_open_default(&capture, 0));
    ts_audio_endpoint_begin_open(&capture, 0);
    ts_audio_endpoint_opened(&capture, 73u, "CABLE Output", 0,
                             48000u, 2u, 256u, "F32");
    CHECK(ts_audio_endpoint_matches_removed(&capture, 1, 73u));
    CHECK(!ts_audio_endpoint_matches_removed(&capture, 0, 73u));
    CHECK(ts_audio_endpoint_removed(&capture, "unplugged") == 73u);
    CHECK(capture.state == TS_AUDIO_CONNECTION_LOST);
    /* Capture loss is independent: internally generated playback retains its
       active output and no capture transition mutates it. */
    CHECK(output.state == TS_AUDIO_CONNECTION_ACTIVE);
    CHECK(output.real_id == 88u);

    ts_audio_endpoint_begin_open(&capture, 0);
    ts_audio_endpoint_opened(&capture, 74u, "CABLE Output", 0,
                             48000u, 2u, 256u, "F32");
    CHECK(capture.state == TS_AUDIO_CONNECTION_ACTIVE);
    CHECK(strcmp(capture.configured_name, "CABLE Output") == 0);
    CHECK(ts_audio_endpoint_closed(&output) == 88u);

    ts_audio_endpoint_init(&capture, TS_AUDIO_ENDPOINT_CAPTURE, 0x7fff0002u);
    ts_audio_endpoint_configure(&capture, "");
    CHECK(!capture.explicit_device);
    CHECK(ts_audio_endpoint_may_open_default(&capture, 0));

    puts("audio backend, identity, fallback, and lifecycle tests passed");
    return 0;
}
