#include "tapesister/waveform_cache.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int brute_zero(const TsSample *sample, size_t first, size_t last)
{
    for (size_t i = first; i < last; ++i) {
        if (sample->data[i] == 0.0f ||
            (i > 0u &&
             ((sample->data[i - 1u] < 0.0f && sample->data[i] > 0.0f) ||
              (sample->data[i - 1u] > 0.0f && sample->data[i] < 0.0f))))
            return 1;
    }
    return 0;
}

static void check_brute_force(const TsSample *sample, size_t first, size_t last,
                              int width)
{
    TsWaveformCache cache;
    TsWaveformRequest request;
    ts_waveform_cache_init(&cache);
    memset(&request, 0, sizeof(request));
    request.sample = sample;
    request.first = first;
    request.last = last;
    request.width = width;
    request.detect_zero_crossings = 1;
    assert(ts_waveform_cache_prepare(&cache, &request));
    for (int column = 0; column < width; ++column) {
        const TsWaveformColumn *result = &cache.columns[column];
        float minimum = 1.0f;
        float maximum = -1.0f;
        size_t begin = first + (size_t)column * (last - first) / (size_t)width;
        size_t end = first + (size_t)(column + 1) * (last - first) /
                     (size_t)width;
        if (end <= begin) end = begin + 1u;
        if (end > sample->frames) end = sample->frames;
        for (size_t i = begin; i < end; ++i) {
            if (sample->data[i] < minimum) minimum = sample->data[i];
            if (sample->data[i] > maximum) maximum = sample->data[i];
        }
        assert(result->first == begin && result->last == end);
        assert(result->minimum == minimum && result->maximum == maximum);
        assert(result->has_zero_crossing == brute_zero(sample, begin, end));
    }
}

static void test_cache_keys_and_revisions(void)
{
    float data[4096];
    float other_data[4096];
    TsSample sample = {data, 4096u, 48000u, "sample", 1u};
    TsSample other = {other_data, 4096u, 48000u, "other", 1u};
    TsWaveformCache cache;
    TsWaveformRequest request;
    for (size_t i = 0; i < sample.frames; ++i) {
        data[i] = sinf((float)i * 0.03125f);
        other_data[i] = cosf((float)i * 0.021f);
    }
    check_brute_force(&sample, 0u, sample.frames, 600);
    check_brute_force(&sample, 17u, 2033u, 127);

    ts_waveform_cache_init(&cache);
    memset(&request, 0, sizeof(request));
    request.sample = &sample;
    request.last = sample.frames;
    request.width = 600;
    request.detect_zero_crossings = 1;
    assert(ts_waveform_cache_prepare(&cache, &request));
    assert(cache.rebuild_count == 1u);
    /* Playhead, selection, and loop state are deliberately absent from the
       audio-analysis key, so overlay-only frames reuse the columns. */
    assert(ts_waveform_cache_prepare(&cache, &request));
    assert(cache.rebuild_count == 1u);

    data[0] = -data[0];
    ++sample.visual_revision;
    assert(ts_waveform_cache_prepare(&cache, &request));
    assert(cache.rebuild_count == 2u);
    request.first = 100u;
    request.last = 2000u;
    assert(ts_waveform_cache_prepare(&cache, &request));
    assert(cache.rebuild_count == 3u);
    request.sample = &other;
    request.first = 0u;
    request.last = other.frames;
    assert(ts_waveform_cache_prepare(&cache, &request));
    assert(cache.rebuild_count == 4u);
}

static void test_preview_publication_and_removal(void)
{
    float data[16];
    float preview_data[4] = {0.8f, 0.6f, -0.6f, -0.8f};
    TsSample sample = {data, 16u, 48000u, "source", 1u};
    TsSample preview = {preview_data, 4u, 48000u, "preview", 1u};
    TsWaveformCache cache;
    TsWaveformRequest request;
    for (size_t i = 0; i < 16u; ++i) data[i] = (float)i / 32.0f;
    ts_waveform_cache_init(&cache);
    memset(&request, 0, sizeof(request));
    request.sample = &sample;
    request.last = sample.frames;
    request.width = 8;
    assert(ts_waveform_cache_prepare(&cache, &request));
    assert(cache.rebuild_count == 1u);
    request.replacement = &preview;
    request.replacement_first = 4u;
    request.replacement_last = 12u;
    assert(ts_waveform_cache_prepare(&cache, &request));
    assert(cache.rebuild_count == 2u);
    assert(cache.columns[2].maximum == preview_data[0]);
    request.replacement = NULL;
    request.replacement_first = 0u;
    request.replacement_last = 0u;
    assert(ts_waveform_cache_prepare(&cache, &request));
    assert(cache.rebuild_count == 3u);
}

static void test_empty_short_and_long_samples(void)
{
    float one = 0.25f;
    TsSample empty = {NULL, 0u, 48000u, "empty", 1u};
    TsSample single = {&one, 1u, 48000u, "single", 1u};
    TsWaveformCache cache;
    TsWaveformRequest request;
    float *long_data = malloc(200000u * sizeof(*long_data));
    TsSample long_sample = {long_data, 200000u, 48000u, "long", 1u};
    assert(long_data != NULL);
    ts_waveform_cache_init(&cache);
    memset(&request, 0, sizeof(request));
    request.sample = &empty;
    request.width = 600;
    assert(!ts_waveform_cache_prepare(&cache, &request));
    request.sample = &single;
    request.last = 1u;
    assert(ts_waveform_cache_prepare(&cache, &request));
    for (int i = 0; i < 600; ++i) {
        assert(cache.columns[i].first == 0u);
        assert(cache.columns[i].last == 1u);
        assert(cache.columns[i].minimum == one);
        assert(cache.columns[i].maximum == one);
    }
    for (size_t i = 0; i < long_sample.frames; ++i)
        long_data[i] = (float)((int)(i % 257u) - 128) / 128.0f;
    check_brute_force(&long_sample, 13u, long_sample.frames - 7u, 600);
    free(long_data);
}

int main(void)
{
    test_cache_keys_and_revisions();
    test_preview_publication_and_removal();
    test_empty_short_and_long_samples();
    puts("Waveform cache tests passed.");
    return 0;
}
