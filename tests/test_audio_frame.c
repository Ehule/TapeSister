#include "tapesister/sample.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

int main(void)
{
    float mono_data[] = {0.25f, -0.5f};
    float stereo_data[] = {0.1f, 0.8f, -0.3f, 0.7f};
    float nonfinite_data[] = {NAN, INFINITY};
    TsSample mono = {mono_data, 2u, 48000u, "mono", 1u, 1u};
    TsSample stereo = {stereo_data, 2u, 48000u, "stereo", 1u, 2u};
    TsSample nonfinite = {nonfinite_data, 1u, 48000u, "bad", 1u, 2u};
    TsStereoFrame frame;
    size_t scalars = 0u;
    size_t bytes = 0u;

    assert(ts_sample_valid_channels(1u));
    assert(ts_sample_valid_channels(2u));
    assert(!ts_sample_valid_channels(0u));
    assert(!ts_sample_valid_channels(3u));
    assert(ts_sample_dimensions(7u, 2u, &scalars, &bytes));
    assert(scalars == 14u && bytes == 14u * sizeof(float));
    assert(!ts_sample_dimensions(SIZE_MAX, 2u, &scalars, &bytes));
    assert(!ts_sample_dimensions(1u, 0u, &scalars, &bytes));

    frame = ts_sample_read_frame(&mono, 0u);
    assert(frame.l == 0.25f && frame.r == 0.25f);
    assert(ts_sample_read_mono(&mono, 1u) == -0.5f);
    frame = ts_sample_read_frame(&stereo, 0u);
    assert(frame.l == 0.1f && frame.r == 0.8f);
    assert(fabsf(ts_sample_read_mono(&stereo, 0u) - 0.45f) < 0.000001f);

    frame = ts_sample_read_frame(&nonfinite, 0u);
    assert(frame.l == 0.0f && frame.r == 0.0f);
    frame.l = NAN;
    frame.r = -INFINITY;
    frame = ts_stereo_frame_sanitize(frame);
    assert(frame.l == 0.0f && frame.r == 0.0f);

    frame.l = -0.25f;
    frame.r = 0.75f;
    assert(ts_sample_write_frame(&stereo, 1u, frame));
    assert(stereo_data[2] == -0.25f && stereo_data[3] == 0.75f);
    assert(ts_sample_write_frame(&mono, 0u, frame));
    assert(mono_data[0] == 0.25f);
    assert(!ts_sample_write_frame(&stereo, 2u, frame));
    assert(ts_sample_scalar_count(&stereo, &scalars) && scalars == 4u);

    puts("audio frame tests passed");
    return 0;
}
