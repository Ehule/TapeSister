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
           cache->sample_channels == sample->channels &&
           cache->sample_visual_revision == sample->visual_revision &&
           cache->replacement_identity == pointer_identity(replacement) &&
           cache->replacement_data_identity ==
               pointer_identity(replacement != NULL ? replacement->data : NULL) &&
           cache->replacement_frames ==
               (replacement != NULL ? replacement->frames : 0u) &&
           cache->replacement_channels ==
               (replacement != NULL ? replacement->channels : 0u) &&
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

static TsStereoFrame displayed_frame(const TsWaveformRequest *request,
                                     size_t frame)
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
        return ts_sample_read_frame(replacement, at);
    }
    return ts_sample_read_frame(request->sample, frame);
}

int ts_waveform_analyze_columns(TsWaveformColumn *columns,
                                const TsWaveformRequest *request)
{
    const TsSample *sample;
    if (columns == NULL || request == NULL || request->sample == NULL ||
        request->sample->data == NULL || request->width <= 0 ||
        request->last <= request->first || request->last > request->sample->frames ||
        !ts_sample_valid_channels(request->sample->channels)) return 0;
    sample = request->sample;
    for (int column = 0; column < request->width; ++column) {
        TsWaveformColumn *result = &columns[column];
        size_t begin = request->first +
                       (size_t)column * (request->last - request->first) /
                       (size_t)request->width;
        size_t end = request->first +
                     (size_t)(column + 1) * (request->last - request->first) /
                     (size_t)request->width;
        float minimum = 1.0f;
        float maximum = -1.0f;
        float left_minimum = 1.0f;
        float left_maximum = -1.0f;
        float right_minimum = 1.0f;
        float right_maximum = -1.0f;
        unsigned zero_crossing = 0;
        size_t span = request->last - request->first;
        /* A boundary belongs to the pixel containing its actual frame. Ceil
           intervals avoid repeating one boundary across sub-sample pixels. */
        size_t marker_first = request->first +
            ((size_t)column * span + request->width - 1u) / request->width;
        size_t marker_last = request->first +
            ((size_t)(column + 1) * span + request->width - 1u) / request->width;
        TsStereoFrame previous = displayed_frame(request, begin > 0u ? begin - 1u : begin);
        if (end <= begin) end = begin + 1u;
        if (end > sample->frames) end = sample->frames;
        size_t analyze_last = end > marker_last ? end : marker_last;
        for (size_t frame = begin; frame < analyze_last; ++frame) {
            TsStereoFrame values = displayed_frame(request, frame);
            if (frame < end) {
                if (values.l < left_minimum) left_minimum = values.l;
                if (values.l > left_maximum) left_maximum = values.l;
                if (values.r < right_minimum) right_minimum = values.r;
                if (values.r > right_maximum) right_maximum = values.r;
                if (values.l < minimum) minimum = values.l;
                if (values.r < minimum) minimum = values.r;
                if (values.l > maximum) maximum = values.l;
                if (values.r > maximum) maximum = values.r;
            }
            if (request->detect_zero_crossings && frame >= marker_first && frame < marker_last)
                zero_crossing |= ts_sample_zero_crossing_channels(previous, values, frame > 0u);
            previous = values;
        }
        result->first = begin;
        result->last = end;
        result->minimum = minimum;
        result->maximum = maximum;
        result->left_minimum = left_minimum;
        result->left_maximum = left_maximum;
        result->right_minimum = right_minimum;
        result->right_maximum = right_maximum;
        result->has_zero_crossing = zero_crossing != 0u;
        result->zero_crossing_channels = zero_crossing;
    }

    return 1;
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
    if (!ts_sample_valid_channels(request->sample->channels) ||
        (request->replacement != NULL && request->replacement->data != NULL &&
         !ts_sample_valid_channels(request->replacement->channels)))
        return 0;
    if (request_matches(cache, request)) return 1;

    sample = request->sample;
    rebuild_count = cache->rebuild_count + 1u;
    if (!ts_waveform_analyze_columns(cache->columns, request)) return 0;

    cache->sample_identity = pointer_identity(sample);
    cache->sample_data_identity = pointer_identity(sample->data);
    cache->sample_frames = sample->frames;
    cache->sample_channels = sample->channels;
    cache->sample_visual_revision = sample->visual_revision;
    cache->replacement_identity = pointer_identity(request->replacement);
    cache->replacement_data_identity = pointer_identity(
        request->replacement != NULL ? request->replacement->data : NULL);
    cache->replacement_frames = request->replacement != NULL ?
                                request->replacement->frames : 0u;
    cache->replacement_channels = request->replacement != NULL ?
                                  request->replacement->channels : 0u;
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
