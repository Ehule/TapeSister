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
    assert(ts_performance_count(&performance) == 3);
    ts_performance_release(&performance, 2);
    assert(ts_performance_count(&performance) == 0);

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
    ts_performance_release(&performance, 0);
    assert(ts_performance_count(&performance) == 0);

    /* The full 16-tile, 24-note fan-out fits exactly in the dedicated bank. */
    for (int note = 0; note < 24; ++note)
        assert(ts_performance_trigger_group(&performance, &instrument, 0xffffu,
                                            note, 60, 0, 44100) ==
               TS_BANK_SLOT_COUNT);
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

    for (int slot = 0; slot < TS_BANK_SLOT_COUNT; ++slot) free_slot(&instrument.bank[slot]);
    puts("performance tests passed");
    return 0;
}
