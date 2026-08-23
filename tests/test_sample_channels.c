#include "tapesister/sample.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void assert_frame(const TsSample *sample, size_t frame,
                         float left, float right)
{
    TsStereoFrame value = ts_sample_read_frame(sample, frame);
    assert(fabsf(value.l - left) < 0.000001f);
    assert(fabsf(value.r - right) < 0.000001f);
}

int main(void)
{
    enum { FRAMES = 4096 };
    float *values = malloc(FRAMES * 2u * sizeof(*values));
    float mono_values[] = {0.1f, 0.2f, 0.3f};
    TsSample source;
    TsSample clone;
    TsSample clipboard;
    TsSample mono_clip = {mono_values, 3u, 48000u, "mono", 1u, 1u};
    TsSample mono_same_frames = {mono_values, 3u, 48000u, "mono", 1u, 1u};
    TsInstrument instrument;
    TsProcessRecipe neutral;
    TsSample processed;
    char error[256];
    size_t origin = 0u;
    size_t before_frames;
    uint64_t original_hash;
    uint64_t edited_hash;

    assert(values != NULL);
    for (size_t frame = 0; frame < FRAMES; ++frame) {
        values[frame * 2u] = (float)frame / (float)FRAMES * 0.8f - 0.4f;
        values[frame * 2u + 1u] = 0.9f - (float)frame / (float)FRAMES * 0.7f;
    }
    source.data = values;
    source.frames = FRAMES;
    source.sample_rate = 48000u;
    snprintf(source.name, sizeof(source.name), "stereo-source");
    source.visual_revision = 1u;
    source.channels = 2u;
    ts_sample_init(&clone);
    ts_sample_init(&clipboard);
    ts_sample_init(&processed);
    ts_instrument_init(&instrument);

    assert(ts_sample_clone(&clone, &source, error, sizeof(error)));
    assert(clone.channels == 2u && clone.frames == FRAMES);
    assert(memcmp(clone.data, source.data, FRAMES * 2u * sizeof(float)) == 0);
    assert(ts_sample_hash(&clone) == ts_sample_hash(&source));
    assert(ts_sample_hash(&mono_same_frames) != ts_sample_hash(&source));

    ts_process_recipe_reset(&neutral);
    assert(ts_sample_process(&processed, &source, 0u, source.frames,
                             &neutral, error, sizeof(error)));
    assert(processed.channels == 2u && processed.frames == source.frames);
    assert(ts_sample_read_channel(&processed, 100u, 0u) !=
           ts_sample_read_channel(&processed, 100u, 1u));

    assert(ts_sample_save_wav32f(&source, "test-sample-channels.wav",
                                 error, sizeof(error)));
    assert(ts_instrument_load_wav(&instrument, "test-sample-channels.wav",
                                  error, sizeof(error)));
    assert(instrument.current.channels == 2u);
    assert(instrument.parent.channels == 2u);
    assert(instrument.bank[0].sample.channels == 2u);
    assert_frame(&instrument.bank[0].sample, 17u,
                 values[34u], values[35u]);

    original_hash = ts_sample_hash(&instrument.current);
    assert(ts_instrument_apply_sample_edit(&instrument, TS_SAMPLE_EDIT_REVERSE,
                                           1.0f, error, sizeof(error)));
    edited_hash = ts_sample_hash(&instrument.current);
    assert(edited_hash != original_hash && instrument.current.channels == 2u);
    assert_frame(&instrument.current, 0u,
                 ts_sample_read_channel(&clone, FRAMES - 1u, 0u),
                 ts_sample_read_channel(&clone, FRAMES - 1u, 1u));
    assert(ts_instrument_undo(&instrument, error, sizeof(error)));
    assert(ts_sample_hash(&instrument.current) == original_hash);
    assert(ts_instrument_redo(&instrument, error, sizeof(error)));
    assert(ts_sample_hash(&instrument.current) == edited_hash);
    assert(ts_instrument_undo(&instrument, error, sizeof(error)));

    ts_instrument_set_selection(&instrument, 128u, 384u);
    assert(ts_instrument_cut_selection_mode(&instrument, &clipboard, &origin, 1,
                                            error, sizeof(error)));
    assert(clipboard.channels == 2u && clipboard.frames == 256u);
    assert(instrument.current.channels == 2u &&
           instrument.current.frames == FRAMES - 256u);
    assert_frame(&clipboard, 3u,
                 ts_sample_read_channel(&clone, 131u, 0u),
                 ts_sample_read_channel(&clone, 131u, 1u));
    assert(ts_instrument_undo(&instrument, error, sizeof(error)));
    assert(instrument.current.channels == 2u && instrument.current.frames == FRAMES);

    before_frames = instrument.current.frames;
    ts_instrument_clear_selection(&instrument);
    assert(ts_instrument_paste(&instrument, &clipboard, before_frames, 0,
                               error, sizeof(error)));
    assert(instrument.current.channels == 2u &&
           instrument.current.frames == before_frames + clipboard.frames);
    assert_frame(&instrument.current, before_frames,
                 ts_sample_read_channel(&clipboard, 0u, 0u),
                 ts_sample_read_channel(&clipboard, 0u, 1u));
    assert(ts_instrument_undo(&instrument, error, sizeof(error)));

    before_frames = instrument.current.frames;
    assert(ts_instrument_resize_canvas(&instrument, 2, 32,
                                       error, sizeof(error)));
    assert(instrument.current.channels == 2u &&
           instrument.current.frames == before_frames + 32u);
    assert_frame(&instrument.current, before_frames, 0.0f, 0.0f);
    assert(ts_instrument_undo(&instrument, error, sizeof(error)));

    original_hash = ts_sample_hash(&instrument.current);
    assert(!ts_instrument_paste(&instrument, &mono_clip, 0u, 0,
                                error, sizeof(error)));
    assert(strstr(error, "matching mono/stereo") != NULL);
    assert(ts_sample_hash(&instrument.current) == original_hash);
    assert(!ts_instrument_apply_rendered_replacement(
        &instrument, &mono_clip, 0u, 3u, error, sizeof(error)));
    assert(ts_sample_hash(&instrument.current) == original_hash);
    assert(!ts_instrument_apply_warp(&instrument, 0.5f, error, sizeof(error)));
    assert(strstr(error, "Stereo WARP") != NULL);

    ts_sample_free(&processed);
    ts_sample_free(&clipboard);
    ts_sample_free(&clone);
    ts_instrument_free(&instrument);
    free(values);
    remove("test-sample-channels.wav");
    puts("sample channel tests passed");
    return 0;
}
