#include "tapesister/performance.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; \
} } while (0)
#define CLOSE(a,b) (fabsf((a) - (b)) < 0.0001f)

static void slot(TsBankSlot *slot, uint8_t channels, float l, float r)
{
    memset(slot, 0, sizeof(*slot));
    slot->sample.frames = 8u;
    slot->sample.sample_rate = 48000u;
    slot->sample.channels = channels;
    slot->sample.data = calloc(8u * channels, sizeof(float));
    slot->occupied = slot->sample.data != NULL;
    slot->tuning.root_note = 60;
    slot->audible_tuning.root_note = 60;
    for (size_t frame = 0u; frame < 8u; ++frame) {
        slot->sample.data[frame * channels] = l;
        if (channels == 2u) slot->sample.data[frame * 2u + 1u] = r;
    }
}

int main(void)
{
    TsInstrument instrument;
    TsPerformanceBank bank;
    TsStereoFrame raw;
    TsStereoFrame monitor;
    float loud[] = {2.0f, 0.5f, -1.0f, -0.25f};
    memset(&instrument, 0, sizeof(instrument));
    slot(&instrument.bank[0], 1u, 1.0f, 1.0f);
    slot(&instrument.bank[1], 2u, 1.0f, -1.0f);
    ts_performance_init(&bank);
    ts_performance_set_attack_ms(&bank, 0);
    CHECK(ts_performance_trigger_group(&bank, &instrument, 0x3u,
                                       0, 60, 0, 48000) == 2);
    for (int i = 0; i < TS_PERFORMANCE_VOICE_LIMIT; ++i)
        bank.voices[i].attack_frames = 0u;
    monitor = ts_performance_read_stereo(&bank, &raw);
    CHECK(CLOSE(raw.l, 2.0f) && CLOSE(raw.r, 0.0f));
    CHECK(CLOSE(monitor.l, sqrtf(2.0f)) && CLOSE(monitor.r, 0.0f));

    ts_performance_clear(&bank);
    CHECK(ts_performance_trigger_staged(&bank, &instrument, 0x3u,
                                        (1u << 0) | (1u << 4),
                                        60, 48000) == 4);
    CHECK(ts_performance_count(&bank) == 4);
    ts_performance_release_after_pass(&bank);
    for (int i = 0; i < 16; ++i)
        (void)ts_performance_read_stereo(&bank, &raw);
    CHECK(ts_performance_count(&bank) == 0);

    CHECK(CLOSE(ts_performance_peak_scale_channels(
                    loud, 2u, 2u, 0.98f), 0.49f));
    CHECK(CLOSE(loud[0], 0.98f));
    CHECK(CLOSE(loud[1] / loud[0], 0.25f));
    CHECK(CLOSE(loud[3] / loud[2], 0.25f));

    free(instrument.bank[0].sample.data);
    free(instrument.bank[1].sample.data);
    if (failures) return 1;
    puts("performance stereo tests passed");
    return 0;
}
