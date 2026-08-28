#include "tapesister/performance.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fill_slot(TsBankSlot *slot, float value, size_t frames, uint32_t rate)
{
    memset(slot, 0, sizeof(*slot));
    slot->sample.data = (float *)malloc(frames * sizeof(float));
    assert(slot->sample.data != NULL);
    slot->sample.frames = frames;
    slot->sample.sample_rate = rate;
    slot->sample.channels = 1u;
    slot->occupied = 1;
    slot->tuning.root_note = 60;
    slot->audible_tuning.root_note = 60;
    for (size_t i = 0; i < frames; ++i) slot->sample.data[i] = value;
}

static void free_slot(TsBankSlot *slot)
{
    free(slot->sample.data);
    slot->sample.data = NULL;
}

int main(void)
{
    TsInstrument instrument;
    TsPerformanceBank performance;
    float raw = 0.0f;
    float monitored;
    float attack_gain;
    float loud[4] = {0.8f, 0.8f, -1.2f, 0.4f};
    float quiet[3] = {0.2f, -0.3f, 0.1f};
    float gain;

    memset(&instrument, 0, sizeof(instrument));
    fill_slot(&instrument.bank[0], 0.25f, 16u, 44100u);
    fill_slot(&instrument.bank[3], 0.50f, 24u, 44100u);
    fill_slot(&instrument.bank[7], 0.75f, 32u, 44100u);

    ts_performance_init(&performance);
    ts_performance_set_attack_ms(&performance, 7);
    ts_performance_clear(&performance);
    assert(performance.attack_ms == 7);
    ts_performance_set_attack_ms(&performance, TS_AUDITION_ATTACK_MS_DEFAULT);
    assert(ts_performance_source_count((uint16_t)((1u << 0) | (1u << 3) | (1u << 7))) == 3);
    assert(ts_performance_trigger_group(
               &performance, &instrument,
               (uint16_t)((1u << 0) | (1u << 3) | (1u << 7)),
               0, 60, 0, 44100) == 3);
    assert(ts_performance_count(&performance) == 3);
    monitored = ts_performance_read(&performance, &raw);
    assert(raw == 0.0f && monitored == 0.0f);
    for (int frame = 1; frame < 8; ++frame)
        monitored = ts_performance_read(&performance, &raw);
    attack_gain = ts_audition_attack_gain(
        7u, ts_audition_attack_frames(44100, TS_AUDITION_ATTACK_MS_DEFAULT));
    assert(fabsf(raw - 1.5f * attack_gain) < 0.0001f);
    assert(fabsf(monitored - (raw / sqrtf(3.0f))) < 0.0001f);

    assert(ts_performance_trigger_group(
               &performance, &instrument,
               (uint16_t)((1u << 0) | (1u << 3) | (1u << 7)),
               2, 60, 0, 44100) == 3);
    assert(ts_performance_count(&performance) == 6);
    ts_performance_release(&performance, 0);
    assert(ts_performance_count(&performance) == 6);
    ts_performance_release(&performance, 2);
    assert(ts_performance_count(&performance) == 6);
    ts_performance_clear(&performance);

    assert(ts_performance_trigger_staged(
               &performance, &instrument,
               (uint16_t)((1u << 0) | (1u << 3) | (1u << 7)),
               (1u << 0) | (1u << 4) | (1u << 7), 60, 44100) == 9);
    assert(ts_performance_count(&performance) == 9);
    ts_performance_clear(&performance);

    /* Different source lengths end independently; the longest source survives. */
    assert(ts_performance_trigger_group(
               &performance, &instrument,
               (uint16_t)((1u << 0) | (1u << 3) | (1u << 7)),
               0, 60, 0, 44100) == 3);
    for (int i = 0; i < 18; ++i) (void)ts_performance_read(&performance, &raw);
    assert(ts_performance_count(&performance) == 2);
    for (int voice = 0; voice < TS_PERFORMANCE_VOICE_LIMIT; ++voice)
        if (performance.voices[voice].active)
            assert(fabsf(performance.voices[voice].group_gain -
                         1.0f / sqrtf(3.0f)) < 0.0001f);
    for (int i = 0; i < 8; ++i) (void)ts_performance_read(&performance, &raw);
    assert(ts_performance_count(&performance) == 1);
    for (int i = 0; i < 8; ++i) (void)ts_performance_read(&performance, &raw);
    assert(ts_performance_count(&performance) == 0);

    gain = ts_performance_peak_scale(quiet, 3u, 0.98f);
    assert(fabsf(gain - 1.0f) < 0.0001f);
    assert(fabsf(quiet[1] + 0.3f) < 0.0001f);

    gain = ts_performance_peak_scale(loud, 4u, 0.98f);
    assert(fabsf(gain - (0.98f / 1.2f)) < 0.0001f);
    assert(fabsf(loud[2] + 0.98f) < 0.0001f);
    assert(fabsf(loud[0] / loud[1] - 1.0f) < 0.0001f);

    /* All 16 tile sources can fan out from one note without a low arbitrary cap. */
    ts_performance_clear(&performance);
    for (int slot = 0; slot < TS_BANK_SLOT_COUNT; ++slot) {
        if (!instrument.bank[slot].occupied)
            fill_slot(&instrument.bank[slot], 0.01f * (float)(slot + 1), 8u, 44100u);
    }
    assert(ts_performance_trigger_group(&performance, &instrument, 0xffffu,
                                        0, 60, 0, 44100) == TS_BANK_SLOT_COUNT);
    assert(ts_performance_count(&performance) == TS_BANK_SLOT_COUNT);
    ts_performance_stop_sources(&performance,
                                (uint16_t)((1u << 0) | (1u << 3)));
    assert(ts_performance_count(&performance) == TS_BANK_SLOT_COUNT - 2);
    ts_performance_release(&performance, 0);
    assert(ts_performance_count(&performance) == TS_BANK_SLOT_COUNT - 2);
    ts_performance_clear(&performance);

    /* The full 16-tile, 24-note fan-out fits exactly in the dedicated bank. */
    for (int note = 0; note < 24; ++note)
        assert(ts_performance_trigger_group(&performance, &instrument, 0xffffu,
                                            note, 60, 0, 44100) ==
               TS_BANK_SLOT_COUNT);
    assert(ts_performance_count(&performance) == TS_PERFORMANCE_VOICE_LIMIT);
    assert(ts_performance_trigger_group(&performance, &instrument, 0xffffu,
                                        0, 60, 0, 44100) == 0);
    assert(ts_performance_count(&performance) == TS_PERFORMANCE_VOICE_LIMIT);
    ts_performance_clear(&performance);

    /* Plain-click group release is a graceful unlatch, not a hard Note Off.
       Forward and reverse loops finish their current traversal once. */
    instrument.bank[0].has_loop = 1;
    instrument.bank[0].loop_first = 2u;
    instrument.bank[0].loop_last = 7u;
    instrument.bank[0].loop_mode = TS_LOOP_FORWARD;
    instrument.bank[3].has_loop = 1;
    instrument.bank[3].loop_first = 2u;
    instrument.bank[3].loop_last = 9u;
    instrument.bank[3].loop_mode = TS_LOOP_REVERSE;
    ts_performance_clear(&performance);
    assert(ts_performance_trigger_group(
               &performance, &instrument,
               (uint16_t)((1u << 0) | (1u << 3)),
               0, 60, 1, 44100) == 2);
    for (int i = 0; i < 3; ++i) (void)ts_performance_read(&performance, &raw);
    ts_performance_release_sources_after_pass(&performance,
                                               (uint16_t)(1u << 0));
    ts_performance_sync(&performance, &instrument, 44100);
    for (int i = 0; i < 8; ++i) (void)ts_performance_read(&performance, &raw);
    assert(ts_performance_count(&performance) == 1);
    ts_performance_release_after_pass(&performance);
    assert(ts_performance_count(&performance) == 1);
    for (int i = 0; i < 8; ++i) (void)ts_performance_read(&performance, &raw);
    assert(ts_performance_count(&performance) == 0);

    /* Active voices own immutable sample generations. One-shots finish the
       old generation after source replacement; loops stage the replacement
       and adopt it atomically at a boundary with a linked crossfade. */
    instrument.bank[0].has_loop = 0;
    ts_performance_clear(&performance);
    assert(ts_performance_trigger_group(&performance, &instrument,
                                        (uint16_t)(1u << 0),
                                        0, 60, 0, 44100) == 1);
    {
        TsPerformanceGeneration *began = performance.voices[0].generation;
        float *equivalent = malloc(16u * sizeof(*equivalent));
        float *replacement = malloc(16u * sizeof(*replacement));
        assert(equivalent != NULL && replacement != NULL);
        for (int i = 0; i < 16; ++i) equivalent[i] = 0.25f;
        free(instrument.bank[0].sample.data);
        instrument.bank[0].sample.data = equivalent;
        ++instrument.bank[0].sample.visual_revision;
        ts_performance_sync(&performance, &instrument, 44100);
        assert(performance.voices[0].generation == began);
        for (int i = 0; i < 16; ++i) replacement[i] = -0.9f;
        free(instrument.bank[0].sample.data);
        instrument.bank[0].sample.data = replacement;
        ++instrument.bank[0].sample.visual_revision;
        ts_performance_sync(&performance, &instrument, 44100);
        assert(performance.voices[0].generation == began);
        assert(performance.voices[0].sample->data[0] == 0.25f);
    }
    ts_performance_clear(&performance);
    instrument.bank[3].has_loop = 1;
    instrument.bank[3].loop_first = 2u;
    instrument.bank[3].loop_last = 9u;
    instrument.bank[3].loop_mode = TS_LOOP_FORWARD;
    assert(ts_performance_trigger_group(&performance, &instrument,
                                        (uint16_t)(1u << 3),
                                        0, 60, 1, 44100) == 1);
    {
        TsPerformanceGeneration *began = performance.voices[0].generation;
        float *replacement = malloc(12u * sizeof(*replacement));
        assert(replacement != NULL);
        for (int i = 0; i < 12; ++i) replacement[i] = -0.4f;
        free(instrument.bank[3].sample.data);
        instrument.bank[3].sample.data = replacement;
        instrument.bank[3].sample.frames = 12u;
        instrument.bank[3].loop_last = 11u;
        ++instrument.bank[3].sample.visual_revision;
        assert(ts_performance_prepare_sync(&performance, &instrument));
        assert(performance.voices[0].generation == began);
        assert(performance.slot_generations[3] != began);
        ts_performance_sync(&performance, &instrument, 44100);
        assert(performance.voices[0].generation == began);
        assert(performance.voices[0].pending_generation != NULL);
        for (int i = 0; i < 12; ++i) {
            monitored = ts_performance_read(&performance, &raw);
            assert(isfinite(monitored));
        }
        assert(performance.voices[0].generation != began);
    }
    ts_performance_clear(&performance);

    /* Mouse-launched tiles are independent, capped to 20% fade edges, and
       reverse an in-flight loop release without restarting its playhead. */
    free_slot(&instrument.bank[5]);
    fill_slot(&instrument.bank[5], 1.0f, 100u, 1000u);
    instrument.selected_slot = -1;
    assert(ts_performance_toggle_tile(&performance, &instrument, 5, 50, 1000) ==
           TS_PERFORMANCE_TILE_STARTED);
    assert(performance.voices[0].tile_fade_frames == 20u);
    monitored = ts_performance_read(&performance, NULL);
    assert(monitored > 0.04f && monitored < 0.06f);
    for (int i = 0; i < 19; ++i) (void)ts_performance_read(&performance, NULL);
    assert(fabsf(performance.voices[0].tile_gain - 1.0f) < 0.0001f);
    for (int i = 0; i < 90; ++i) (void)ts_performance_read(&performance, NULL);
    assert(ts_performance_count(&performance) == 0);

    instrument.bank[5].has_loop = 1;
    instrument.bank[5].loop_first = 10u;
    instrument.bank[5].loop_last = 90u;
    instrument.bank[5].loop_mode = TS_LOOP_FORWARD;
    assert(ts_performance_toggle_tile(&performance, &instrument, 5, 50, 1000) ==
           TS_PERFORMANCE_TILE_STARTED);
    assert(performance.voices[0].tile_fade_frames == 16u);
    for (int i = 0; i < 8; ++i) (void)ts_performance_read(&performance, NULL);
    {
        double continuing_position = performance.voices[0].position;
        float before_release = performance.voices[0].tile_gain;
        assert(ts_performance_toggle_tile(
                   &performance, &instrument, 5, 50, 1000) ==
               TS_PERFORMANCE_TILE_RELEASING);
        for (int i = 0; i < 4; ++i) (void)ts_performance_read(&performance, NULL);
        assert(performance.voices[0].tile_gain < before_release);
        assert(ts_performance_toggle_tile(
                   &performance, &instrument, 5, 50, 1000) ==
               TS_PERFORMANCE_TILE_RESUMED);
        assert(performance.voices[0].position > continuing_position);
        assert(performance.voices[0].position !=
               (double)instrument.bank[5].loop_first);
    }
    for (int i = 0; i < 16; ++i) (void)ts_performance_read(&performance, NULL);
    assert(fabsf(performance.voices[0].tile_gain - 1.0f) < 0.0001f);
    assert(ts_performance_toggle_tile(&performance, &instrument, 6, 50, 1000) ==
           TS_PERFORMANCE_TILE_STARTED);
    assert((ts_performance_tile_mask(&performance) &
            (uint16_t)((1u << 5) | (1u << 6))) ==
           (uint16_t)((1u << 5) | (1u << 6)));
    ts_performance_fade_all_tiles(&performance);
    for (int i = 0; i < 20; ++i) (void)ts_performance_read(&performance, NULL);
    assert(ts_performance_tile_mask(&performance) == 0u);
    assert(ts_performance_count(&performance) == 0);

    ts_performance_free(&performance);
    for (int slot = 0; slot < TS_BANK_SLOT_COUNT; ++slot) free_slot(&instrument.bank[slot]);
    puts("performance tests passed");
    return 0;
}
