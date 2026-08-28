#include "tapesister/audio_mixer.h"
#include "tapesister/note_bank.h"

#include <math.h>
#include <stdio.h>

static int failures;
#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; \
} } while (0)
#define CLOSE(a,b) (fabsf((a) - (b)) < 0.00001f)

int main(void)
{
    TsAudioMixer mixer;
    TsAudioBuses buses;
    TsStereoFrame out;
    ts_audio_mixer_init(&mixer);
    ts_audio_buses_clear(&buses);

    buses.legacy_preview = (TsStereoFrame){0.25f, 0.25f};
    out = ts_audio_mixer_render(&mixer, &buses);
    CHECK(CLOSE(out.l, 0.2f) && CLOSE(out.r, 0.2f));

    ts_audio_buses_clear(&buses);
    buses.sister = (TsStereoFrame){0.1f, -0.2f};
    out = ts_audio_mixer_render(&mixer, &buses);
    CHECK(CLOSE(out.l, 0.1f) && CLOSE(out.r, -0.2f));
    CHECK(CLOSE(mixer.buses.sister.l, 0.1f));
    ts_audio_buses_clear(&buses);
    buses.post_fx = (TsStereoFrame){-0.3f, 0.4f};
    out = ts_audio_mixer_render(&mixer, &buses);
    CHECK(CLOSE(out.l, -0.3f) && CLOSE(out.r, 0.4f));
    CHECK(CLOSE(mixer.buses.post_fx.r, 0.4f));
    ts_audio_buses_clear(&buses);

    buses.legacy_preview = (TsStereoFrame){0.25f, -0.5f};
    buses.tile_performance = (TsStereoFrame){0.5f, 0.0f};
    buses.fm = (TsStereoFrame){0.25f, 0.25f};
    buses.external = (TsStereoFrame){0.1f, -0.2f};
    buses.monitor = buses.external;
    buses.reference = (TsStereoFrame){0.05f, 0.05f};
    buses.capture = (TsStereoFrame){9.0f, -9.0f};
    out = ts_audio_mixer_render(&mixer, &buses);
    CHECK(CLOSE(out.l, 0.95f));
    CHECK(CLOSE(out.r, -0.35f));
    CHECK(CLOSE(mixer.buses.capture.l, 9.0f));
    CHECK(CLOSE(mixer.buses.program.l, 1.0f));
    CHECK(CLOSE(mixer.buses.program.r, -0.25f));

    mixer.monitor_enabled = 0;
    out = ts_audio_mixer_render(&mixer, &buses);
    CHECK(CLOSE(out.l, 0.85f) && CLOSE(out.r, -0.15f));
    CHECK(CLOSE(mixer.buses.capture.r, -9.0f));

    buses.legacy_preview = (TsStereoFrame){NAN, INFINITY};
    buses.tile_performance = (TsStereoFrame){0.0f, 0.0f};
    buses.fm = (TsStereoFrame){0.0f, 0.0f};
    buses.monitor = (TsStereoFrame){0.0f, 0.0f};
    buses.reference = (TsStereoFrame){0.0f, 0.0f};
    out = ts_audio_mixer_render(&mixer, &buses);
    CHECK(isfinite(out.l) && isfinite(out.r));
    CHECK(CLOSE(out.l, 0.0f) && CLOSE(out.r, 0.0f));

    buses.legacy_preview = (TsStereoFrame){4.0f, -4.0f};
    out = ts_audio_mixer_render(&mixer, &buses);
    CHECK(CLOSE(out.l, 0.8f) && CLOSE(out.r, -0.8f));

    for (int count = 1; count <= TS_MIDI_NOTE_VOICE_LIMIT; count *= 2) {
        TsStereoFrame normalized = ts_audio_normalize_linked(
            (TsStereoFrame){(float)count, (float)count * 0.25f}, count);
        CHECK(CLOSE(normalized.r / normalized.l, 0.25f));
    }
    CHECK(CLOSE(ts_audio_normalize_linked(
                    (TsStereoFrame){1.0f, 0.0f}, 1).l, 1.0f));

    ts_audio_buses_clear(&buses);
    mixer.monitor_enabled = 1;
    buses.legacy_preview = (TsStereoFrame){0.25f, 0.25f};
    buses.tile_performance = (TsStereoFrame){0.50f, 0.50f};
    buses.fm = (TsStereoFrame){0.25f, 0.25f};
    buses.monitor = (TsStereoFrame){0.40f, 0.40f};
    buses.capture = (TsStereoFrame){0.75f, -0.75f};
    ts_audio_buses_apply_source_dry(&buses, 0.0f, 1, 0, 1, 1);
    out = ts_audio_mixer_render(&mixer, &buses);
    CHECK(CLOSE(out.l, 0.40f) && CLOSE(out.r, 0.40f));
    CHECK(CLOSE(buses.capture.l, 0.75f) && CLOSE(buses.capture.r, -0.75f));
    ts_audio_buses_apply_source_dry(&buses, 0.0f, 0, 1, 0, 0);
    out = ts_audio_mixer_render(&mixer, &buses);
    CHECK(CLOSE(out.l, 0.0f) && CLOSE(out.r, 0.0f));

    ts_audio_buses_clear(&buses);
    buses.legacy_preview = (TsStereoFrame){0.4f, -0.4f};
    buses.tile_performance = (TsStereoFrame){0.4f, -0.4f};
    buses.fm = (TsStereoFrame){0.4f, -0.4f};
    buses.monitor = (TsStereoFrame){0.4f, -0.4f};
    buses.capture = (TsStereoFrame){0.75f, -0.75f};
    ts_audio_buses_apply_source_insert(&buses, 0.25f, 0.5f, 0.75f, 1.0f);
    CHECK(CLOSE(buses.legacy_preview.l, 0.3f));
    CHECK(CLOSE(buses.tile_performance.l, 0.2f));
    CHECK(CLOSE(buses.fm.l, 0.1f));
    CHECK(CLOSE(buses.monitor.l, 0.0f));
    CHECK(CLOSE(buses.capture.l, 0.75f));

    if (failures) return 1;
    puts("audio mixer tests passed");
    return 0;
}
