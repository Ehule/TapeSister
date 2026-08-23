#ifndef TAPESISTER_WAVEFORM_CACHE_H
#define TAPESISTER_WAVEFORM_CACHE_H

#include <stddef.h>
#include <stdint.h>

#include "tapesister/sample.h"

enum { TS_WAVEFORM_CACHE_MAX_COLUMNS = 600 };

typedef struct {
    size_t first;
    size_t last;
    float minimum;
    float maximum;
    float left_minimum;
    float left_maximum;
    float right_minimum;
    float right_maximum;
    int has_zero_crossing;
} TsWaveformColumn;

typedef struct {
    const TsSample *sample;
    size_t first;
    size_t last;
    int width;
    int detect_zero_crossings;
    const TsSample *replacement;
    size_t replacement_first;
    size_t replacement_last;
    uint64_t revision;
} TsWaveformRequest;

typedef struct {
    TsWaveformColumn columns[TS_WAVEFORM_CACHE_MAX_COLUMNS];
    uintptr_t sample_identity;
    uintptr_t sample_data_identity;
    size_t sample_frames;
    uint8_t sample_channels;
    uint32_t sample_visual_revision;
    uintptr_t replacement_identity;
    uintptr_t replacement_data_identity;
    size_t replacement_frames;
    uint8_t replacement_channels;
    uint32_t replacement_visual_revision;
    size_t first;
    size_t last;
    size_t replacement_first;
    size_t replacement_last;
    uint64_t revision;
    uint64_t rebuild_count;
    int width;
    int detect_zero_crossings;
    int valid;
} TsWaveformCache;

void ts_waveform_cache_init(TsWaveformCache *cache);
void ts_waveform_cache_invalidate(TsWaveformCache *cache);
int ts_waveform_cache_prepare(TsWaveformCache *cache,
                              const TsWaveformRequest *request);

#endif
