#include "sister_test_helpers.h"
#include "tapesister/audio_mixer.h"

#include <math.h>
#include <stdio.h>

static int failures;
#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; \
} } while (0)
#define CLOSE(a,b) sister_close((a),(b),0.0002f)

int main(void)
{
    TsSisterRuntime runtime;
    TsInstrument instrument;
    TsNoteEvent note;
    TsSisterSourceFrames sources = {0};
    TsSisterRuntimeFrame frame;
    float gain;
    float one_tile_peak;
    float trimmed_tile_peak;
    float two_tile_peak;
    TsAudioMixer mixer;
    TsAudioBuses buses;

    CHECK(sister_test_make_tiles(&instrument, 2, 0, 1000u, 32u));
    for (size_t sample = 0u; sample < instrument.bank[1].sample.frames;
         ++sample)
        instrument.bank[1].sample.data[sample] =
            instrument.bank[0].sample.data[sample];
    CHECK(sister_test_enable(&runtime, 1000u, 2u, 0.1));
    sources.fm = (TsStereoFrame){0.4f, 0.4f};
    sources.external = (TsStereoFrame){0.2f, -0.2f};
    sources.preview = (TsStereoFrame){0.0f, 0.6f};

    ts_sister_runtime_set_sources(&runtime, TS_SISTER_SOURCE_FM);
    for (int sample = 0; sample < 20; ++sample)
        frame = ts_sister_runtime_process_frame(&runtime, &sources);
    CHECK(CLOSE(frame.input.l, 0.4f) && CLOSE(frame.input.r, 0.4f));
    CHECK(CLOSE(frame.duck_sidechain.l, frame.input.l));

    ts_sister_runtime_set_sources(&runtime, TS_SISTER_SOURCE_EXT);
    for (int sample = 0; sample < 20; ++sample)
        frame = ts_sister_runtime_process_frame(&runtime, &sources);
    CHECK(CLOSE(frame.input.l, 0.2f) && CLOSE(frame.input.r, -0.2f));

    ts_sister_runtime_set_sources(&runtime, TS_SISTER_SOURCE_PREVIEW);
    for (int sample = 0; sample < 20; ++sample)
        frame = ts_sister_runtime_process_frame(&runtime, &sources);
    CHECK(CLOSE(frame.input.l, 0.0f) && CLOSE(frame.input.r, 0.6f));

    {
        TsSisterParameters parameters = runtime.parameters;
        parameters.fm_gain = 2.0f;
        parameters.external_gain = 0.5f;
        parameters.preview_gain = 1.5f;
        ts_sister_runtime_set_parameters(&runtime, &parameters);
        ts_sister_runtime_set_sources(
            &runtime, TS_SISTER_SOURCE_FM | TS_SISTER_SOURCE_EXT |
                      TS_SISTER_SOURCE_PREVIEW);
        for (int sample = 0; sample < 25; ++sample)
            frame = ts_sister_runtime_process_frame(&runtime, &sources);
        gain = 1.0f / sqrtf(3.0f);
        CHECK(CLOSE(frame.input.l, 0.9f * gain));
        CHECK(CLOSE(frame.input.r, 1.6f * gain));
        parameters.fm_gain = 1.0f;
        parameters.external_gain = 1.0f;
        parameters.preview_gain = 1.0f;
        ts_sister_runtime_set_parameters(&runtime, &parameters);
        for (int sample = 0; sample < 25; ++sample)
            frame = ts_sister_runtime_process_frame(&runtime, &sources);
    }

    ts_sister_runtime_set_sources(
        &runtime, TS_SISTER_SOURCE_FM | TS_SISTER_SOURCE_EXT |
                  TS_SISTER_SOURCE_PREVIEW);
    frame = ts_sister_runtime_process_frame(&runtime, &sources);
    gain = 1.0f / sqrtf(3.0f);
    CHECK(CLOSE(frame.input.l, 0.6f * gain));
    CHECK(CLOSE(frame.input.r, 0.8f * gain));
    CHECK(CLOSE(frame.duck_sidechain.r, frame.input.r));

    {
        TsSisterParameters parameters = runtime.parameters;
        parameters.input_gain = 0.5f;
        ts_sister_runtime_set_parameters(&runtime, &parameters);
        for (int sample = 0; sample < 25; ++sample)
            frame = ts_sister_runtime_process_frame(&runtime, &sources);
        CHECK(CLOSE(frame.input.l, 0.3f * gain));
        CHECK(CLOSE(frame.input.r, 0.4f * gain));
        parameters.input_gain = 1.0f;
        ts_sister_runtime_set_parameters(&runtime, &parameters);
        for (int sample = 0; sample < 25; ++sample)
            frame = ts_sister_runtime_process_frame(&runtime, &sources);
    }

    CHECK(ts_note_event_qwerty(&note, 0, TS_KEYBOARD_BASE_NOTE));
    {
        TsSisterParameters parameters = runtime.parameters;
        parameters.tiles_gain = 0.5f;
        ts_sister_runtime_set_parameters(&runtime, &parameters);
        ts_sister_runtime_set_sources(&runtime, 0u);
        for (int sample = 0; sample < 25; ++sample)
            (void)ts_sister_runtime_process_frame(&runtime, NULL);
    }
    ts_sister_runtime_set_sources(&runtime, TS_SISTER_SOURCE_TILES);
    CHECK(ts_sister_runtime_set_source_slot(&runtime, &instrument, 0, 1));
    CHECK(ts_sister_runtime_note_on(&runtime, &instrument, &note, 0,
                                    1000) == 1);
    for (int sample = 0; sample < 20; ++sample)
        frame = ts_sister_runtime_process_frame(&runtime, NULL);
    trimmed_tile_peak = sister_peak(frame.input);
    ts_sister_runtime_panic(&runtime);
    {
        TsSisterParameters parameters = runtime.parameters;
        parameters.tiles_gain = 1.0f;
        ts_sister_runtime_set_parameters(&runtime, &parameters);
        ts_sister_runtime_set_sources(&runtime, 0u);
        for (int sample = 0; sample < 25; ++sample)
            (void)ts_sister_runtime_process_frame(&runtime, NULL);
    }
    ts_sister_runtime_set_sources(&runtime, TS_SISTER_SOURCE_TILES);
    CHECK(ts_sister_runtime_note_on(&runtime, &instrument, &note, 0,
                                    1000) == 1);
    for (int sample = 0; sample < 20; ++sample)
        frame = ts_sister_runtime_process_frame(&runtime, NULL);
    one_tile_peak = sister_peak(frame.input);
    CHECK(CLOSE(one_tile_peak / trimmed_tile_peak, 2.0f));
    ts_sister_runtime_note_off(&runtime, &note);
    /* One-shots intentionally survive Note Off.  Clear that first generation
       before measuring the two-tile group's linked normalization. */
    ts_sister_runtime_panic(&runtime);
    CHECK(ts_sister_runtime_set_source_slot(&runtime, &instrument, 1, 1));
    CHECK(ts_sister_runtime_note_on(&runtime, &instrument, &note, 0,
                                    1000) == 2);
    for (int sample = 0; sample < 5; ++sample)
        frame = ts_sister_runtime_process_frame(&runtime, NULL);
    two_tile_peak = sister_peak(frame.input);
    CHECK(one_tile_peak > 0.0f);
    CHECK(CLOSE(two_tile_peak / one_tile_peak, sqrtf(2.0f)));
    CHECK(CLOSE(frame.input.l, frame.input.r));
    ts_sister_runtime_note_off(&runtime, &note);
    frame = ts_sister_runtime_process_frame(&runtime, NULL);
    CHECK(sister_peak(frame.input) > 0.0f);

    /* A routed TILES performance bus is silent on the direct speaker path;
       Sister returns it exactly once. AUDITION/preview remains an independent
       named source and is not accidentally consumed by the TILES switch. */
    ts_audio_mixer_init(&mixer);
    ts_audio_buses_clear(&buses);
    buses.tile_performance = (TsStereoFrame){0.5f, -0.5f};
    buses.sister = (TsStereoFrame){0.25f, -0.25f};
    ts_audio_buses_apply_source_dry(&buses, 0.0f, 0, 1, 0, 0);
    frame.monitor_return = ts_audio_mixer_render(&mixer, &buses);
    CHECK(CLOSE(frame.monitor_return.l, 0.25f));
    CHECK(CLOSE(frame.monitor_return.r, -0.25f));
    buses.legacy_preview = (TsStereoFrame){0.1f, 0.1f};
    frame.monitor_return = ts_audio_mixer_render(&mixer, &buses);
    CHECK(CLOSE(frame.monitor_return.l, 0.33f));
    CHECK(CLOSE(frame.monitor_return.r, -0.17f));

    ts_sister_runtime_set_monitor(&runtime, 0);
    frame = ts_sister_runtime_process_frame(&runtime, &sources);
    CHECK(CLOSE(frame.monitor_return.l, 0.0f));
    ts_sister_runtime_set_monitor(&runtime, 1);
    for (int sample = 0; sample < 20; ++sample)
        frame = ts_sister_runtime_process_frame(&runtime, &sources);
    CHECK(CLOSE(frame.monitor_return.l,
                frame.input.l + frame.tap[TS_SISTER_TAP_MIX].l));

    /* Route ownership hands off over 20 ms, so the inserted and direct paths
       can be mixed as exact complements without an on/off discontinuity. */
    sources.fm = (TsStereoFrame){0.4f, 0.4f};
    ts_sister_runtime_set_sources(&runtime, TS_SISTER_SOURCE_FM);
    for (int sample = 0; sample < 20; ++sample)
        frame = ts_sister_runtime_process_frame(&runtime, &sources);
    CHECK(CLOSE(ts_sister_runtime_source_route(&runtime, 1), 1.0f));
    ts_sister_runtime_set_sources(&runtime, 0u);
    {
        float previous = 1.0f;
        for (int sample = 0; sample < 20; ++sample) {
            float route;
            frame = ts_sister_runtime_process_frame(&runtime, &sources);
            route = ts_sister_runtime_source_route(&runtime, 1);
            CHECK(route <= previous && route >= 0.0f);
            previous = route;
        }
    }
    CHECK(CLOSE(ts_sister_runtime_source_route(&runtime, 1), 0.0f));

    sources.fm = (TsStereoFrame){NAN, INFINITY};
    ts_sister_runtime_set_sources(&runtime, TS_SISTER_SOURCE_FM);
    for (int sample = 0; sample < 20; ++sample)
        frame = ts_sister_runtime_process_frame(&runtime, &sources);
    CHECK(sister_frame_finite(frame.input));
    CHECK(sister_frame_finite(frame.tap[TS_SISTER_TAP_MIX]));

    ts_sister_runtime_set_sources(&runtime, 0u);
    for (int sample = 0; sample < 20; ++sample)
        frame = ts_sister_runtime_process_frame(&runtime, NULL);
    CHECK(CLOSE(frame.input.l, 0.0f) && CLOSE(frame.input.r, 0.0f));

    ts_sister_runtime_disable(&runtime);
    frame = ts_sister_runtime_process_frame(&runtime, &sources);
    CHECK(CLOSE(frame.input.l, 0.0f));
    CHECK(CLOSE(frame.tap[TS_SISTER_TAP_MIX].r, 0.0f));
    CHECK(CLOSE(frame.monitor_return.l, 0.0f));
    ts_sister_runtime_free(&runtime);
    ts_instrument_free(&instrument);

    if (failures) return 1;
    puts("sister route tests passed");
    return 0;
}
