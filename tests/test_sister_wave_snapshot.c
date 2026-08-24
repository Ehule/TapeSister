#include "tapesister/sister_wave_snapshot.h"

#include <math.h>
#include <stdio.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main(void)
{
    TsSisterWavePublisher publisher;
    TsSisterWaveSnapshot snapshot;
    uint64_t revision;
    ts_sister_wave_publisher_init(&publisher);
    for (size_t frame = 0u; frame < 512u; ++frame) {
        float phase = (float)frame / 511.0f;
        ts_sister_wave_publisher_push(&publisher,
            (TsStereoFrame){phase, -phase}, frame, 512u, 2u, 1);
    }
    CHECK(ts_sister_wave_snapshot_get(&publisher, &snapshot));
    CHECK(snapshot.channels == 2u);
    CHECK(snapshot.valid_bins == TS_SISTER_WAVE_BIN_COUNT);
    CHECK(snapshot.bins[100].left_maximum > 0.0f);
    CHECK(snapshot.bins[100].right_minimum < 0.0f);
    revision = snapshot.revision;
    ts_sister_wave_publisher_push(&publisher,
        (TsStereoFrame){1.0f, 1.0f}, 0u, 512u, 2u, 0);
    CHECK(ts_sister_wave_snapshot_get(&publisher, &snapshot));
    CHECK(snapshot.revision == revision);
    ts_sister_wave_publisher_clear(&publisher, 1u);
    CHECK(ts_sister_wave_snapshot_get(&publisher, &snapshot));
    CHECK(snapshot.channels == 1u && snapshot.valid_bins == 0u);
    CHECK(isfinite(snapshot.bins[0].left_minimum));
    puts("Sister waveform snapshot tests passed");
    return failures != 0;
}
