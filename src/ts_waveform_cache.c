#include "tapesister/waveform_cache.h"

#include <string.h>

static uintptr_t pointer_identity(const void *pointer)
{
    return (uintptr_t)pointer;
}

void ts_waveform_cache_init(TsWaveformCache *cache)
{
    if (cache != NULL) memset(cache, 0, sizeof(*cache));
}

void ts_waveform_cache_invalidate(TsWaveformCache *cache)
{
    if (cache != NULL) cache->valid = 0;
}

static int request_matches(const TsWaveformCache *cache,
                           const TsWaveformRequest *request)
{
    const TsSample *sample = request->sample;
    const TsSample *replacement = request->replacement;
    return cache->valid &&
           cache->sample_identity == pointer_identity(sample) &&
           cache->sample_data_identity == pointer_identity(sample->data) &&
           cache->sample_frames == sample->frames &&
           cache->sample_visual_revision == sample->visual_revision &&
           cache->replacement_identity == pointer_identity(replacement) &&
           cache->replacement_data_identity ==
               pointer_identity(replacement != NULL ? replacement->data : NULL) &&
           cache->replacement_frames ==
               (replacement != NULL ? replacement->frames : 0u) &&
           cache->replacement_visual_revision ==
               (replacement != NULL ? replacement->visual_revision : 0u) &&
           cache->first == request->first &&
           cache->last == request->last &&
           cache->replacement_first == request->replacement_first &&
           cache->replacement_last == request->replacement_last &&
           cache->revision == request->revision &&
           cache->width == request->width &&
           cache->detect_zero_crossings == request->detect_zero_crossings;
}

static float displayed_value(const TsWaveformRequest *request, size_t frame)
{
    const TsSample *replacement = request->replacement;
    if (replacement != NULL && replacement->data != NULL &&
        replacement->frames > 0u &&
        request->replacement_last > request->replacement_first &&
        frame >= request->replacement_first &&
        frame < request->replacement_last) {
        size_t span = request->replacement_last - request->replacement_first;
        size_t at = (frame - request->replacement_first) * replacement->frames /
                    span;
        if (at >= replacement->frames) at = replacement->frames - 1u;
        return replacement->data[at];
    }
    return request->sample->data[frame];
}

int ts_waveform_cache_prepare(TsWaveformCache *cache,
                              const TsWaveformRequest *request)
{
    const TsSample *sample;
    uint64_t rebuild_count;
    if (cache == NULL || request == NULL || request->sample == NULL ||
        request->sample->data == NULL || request->sample->frames == 0u ||
        request->width <= 0 ||
        request->width > TS_WAVEFORM_CACHE_MAX_COLUMNS ||
        request->last <= request->first ||
        request->last > request->sample->frames)
        return 0;
    if (request_matches(cache, request)) return 1;

    sample = request->sample;
    rebuild_count = cache->rebuild_count + 1u;
    for (int column = 0; column < request->width; ++column) {
        TsWaveformColumn *result = &cache->columns[column];
        size_t begin = request->first +
                       (size_t)column * (request->last - request->first) /
                       (size_t)request->width;
        size_t end = request->first +
                     (size_t)(column + 1) * (request->last - request->first) /
                     (size_t)request->width;
        float minimum = 1.0f;
        float maximum = -1.0f;
        int zero_crossing = 0;
        if (end <= begin) end = begin + 1u;
        if (end > sample->frames) end = sample->frames;
        for (size_t frame = begin; frame < end; ++frame) {
            float value = displayed_value(request, frame);
            if (value < minimum) minimum = value;
            if (value > maximum) maximum = value;
            if (request->detect_zero_crossings && !zero_crossing &&
                (sample->data[frame] == 0.0f ||
                 (frame > 0u &&
                  ((sample->data[frame - 1u] < 0.0f &&
                    sample->data[frame] > 0.0f) ||
                   (sample->data[frame - 1u] > 0.0f &&
                    sample->data[frame] < 0.0f)))))
                zero_crossing = 1;
        }
        result->first = begin;
        result->last = end;
        result->minimum = minimum;
        result->maximum = maximum;
        result->has_zero_crossing = zero_crossing;
    }

    cache->sample_identity = pointer_identity(sample);
    cache->sample_data_identity = pointer_identity(sample->data);
    cache->sample_frames = sample->frames;
    cache->sample_visual_revision = sample->visual_revision;
    cache->replacement_identity = pointer_identity(request->replacement);
    cache->replacement_data_identity = pointer_identity(
        request->replacement != NULL ? request->replacement->data : NULL);
    cache->replacement_frames = request->replacement != NULL ?
                                request->replacement->frames : 0u;
    cache->replacement_visual_revision = request->replacement != NULL ?
                                         request->replacement->visual_revision : 0u;
    cache->first = request->first;
    cache->last = request->last;
    cache->replacement_first = request->replacement_first;
    cache->replacement_last = request->replacement_last;
    cache->revision = request->revision;
    cache->rebuild_count = rebuild_count;
    cache->width = request->width;
    cache->detect_zero_crossings = request->detect_zero_crossings;
    cache->valid = 1;
    return 1;
}
