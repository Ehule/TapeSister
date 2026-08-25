#ifndef TAPESISTER_SISTER_WAVE_SNAPSHOT_H
#define TAPESISTER_SISTER_WAVE_SNAPSHOT_H

#include "tapesister/sample.h"

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

enum { TS_SISTER_WAVE_BIN_COUNT = 256 };

typedef struct {
    float left_minimum, left_maximum;
    float right_minimum, right_maximum;
} TsSisterWaveBin;

typedef struct {
    TsSisterWaveBin bins[TS_SISTER_WAVE_BIN_COUNT];
    size_t write_bin;
    size_t valid_bins;
    uint8_t channels;
    uint64_t revision;
} TsSisterWaveSnapshot;

typedef struct {
    atomic_uint_least64_t revision;
    atomic_uint_least32_t values[TS_SISTER_WAVE_BIN_COUNT][4];
    atomic_uint_least64_t write_bin;
    atomic_uint_least64_t valid_bins;
    atomic_uint_least32_t channels;
    TsSisterWaveBin current;
    size_t current_bin;
    size_t valid_bins_writer;
    uint32_t frames_since_publish;
    int initialized;
} TsSisterWavePublisher;

void ts_sister_wave_publisher_init(TsSisterWavePublisher *publisher);
void ts_sister_wave_publisher_clear(TsSisterWavePublisher *publisher,
                                    uint8_t channels);
void ts_sister_wave_publisher_push(TsSisterWavePublisher *publisher,
                                   TsStereoFrame frame, size_t frame_position,
                                   size_t capacity_frames, uint8_t channels,
                                   int written);
void ts_sister_wave_publisher_resize(TsSisterWavePublisher *publisher,
                                     size_t old_capacity_frames,
                                     size_t new_capacity_frames,
                                     uint64_t master_clock);
int ts_sister_wave_snapshot_get(const TsSisterWavePublisher *publisher,
                                TsSisterWaveSnapshot *snapshot);

#endif
