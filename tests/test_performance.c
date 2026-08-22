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
    float loud[4] = {0.8f, 0.8f, -1.2f, 0.4f};
    float quiet[3] = {0.2f, -0.3f, 0.1f};
    float gain;

    memset(&instrument, 0, sizeof(instrument));
    fill_slot(&instrument.bank[0], 0.25f, 16u, 44100u);
    fill_slot(&instrument.bank[3], 0.50f, 24u, 44100u);
    fill_slot(&instrument.bank[7], 0.75f, 32u, 44100u);

    ts_performance_init(&performance);
    assert(ts_performance_source_count((uint16_t)((1u << 0) | (1u << 3) | (1u << 7))) == 3);
    assert(ts_performance_trigger_group(
               &performance, &instrument,
               (uint16_t)((1u << 0) | (1u << 3) | (1u << 7)),
               0, 60, 0, 44100) == 3);
    assert(ts_performance_count(&performance) == 3);
    monitored = ts_performance_read(&performance, &raw);
    assert(fabsf(raw - 1.5f) < 0.0001f);
    assert(fabsf(monitored - 0.5f) < 0.0001f);

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

    gain = ts_performance_peak_scale(quiet, 3u, 0.98f);
    assert(fabsf(gain - 1.0f) < 0.0001f);
    assert(fabsf(quiet[1] + 0.3f) < 0.0001f);

    gain = ts_performance_peak_scale(loud, 4u, 0.98f);
    assert(fabsf(gain - (0.98f / 1.2f)) < 0.0001f);
    assert(fabsf(loud[2] + 0.98f) < 0.0001f);
    assert(fabsf(loud[0] / loud[1] - 1.0f) < 0.0001f);

    free_slot(&instrument.bank[0]);
    free_slot(&instrument.bank[3]);
    free_slot(&instrument.bank[7]);
    puts("performance tests passed");
    return 0;
}
