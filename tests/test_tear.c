#include "tapesister/sample.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "TEAR CHECK FAILED line %d: %s\n", __LINE__, #x); ++failures; } } while (0)

static void setup(TsInstrument *i, const float *data, size_t frames, int rate)
{
    char error[160];
    ts_instrument_init(i);
    i->parent.data = malloc(frames * sizeof(float));
    memcpy(i->parent.data, data, frames * sizeof(float));
    i->parent.frames = frames; i->parent.sample_rate = rate;
    i->crop_last = frames; i->view_first = 320; i->view_last = 1800;
    if (!ts_sample_clone(&i->current, &i->parent, error, sizeof(error))) abort();
}

int main(void)
{
    enum { RATE = 44100, N = 8192 };
    char error[160]; int failures = 0;
    float *material = malloc(N * sizeof(float));
    float *strong = malloc(N * sizeof(float));
    TsInstrument zero, a, b, selected, eof, gesture_a, gesture_b, silence, serial, restored;
    TsTearGesture gesture;
    for (size_t n = 0; n < N; ++n) {
        float phrase = n < 2048 ? 0.63f : n < 4096 ? 0.31f : n < 6144 ? -0.48f : 0.72f;
        material[n] = phrase * sinf(2.0f * 3.14159265358979323846f *
                                    (110.0f + (float)(n / 1024u) * 37.0f) * n / RATE);
    }
    setup(&zero, material, N, RATE);
    CHECK(ts_instrument_apply_tear(&zero, 0.0f, error, sizeof(error)));
    CHECK(memcmp(zero.current.data, material, N * sizeof(float)) == 0);
    CHECK(zero.view_first == 320 && zero.view_last == 1800);

    setup(&a, material, N, RATE); setup(&b, material, N, RATE);
    CHECK(ts_instrument_apply_tear(&a, 0.85f, error, sizeof(error)));
    CHECK(ts_instrument_apply_tear(&b, 0.85f, error, sizeof(error)));
    CHECK(a.current.frames == N && a.current.sample_rate == RATE);
    CHECK(memcmp(a.current.data, b.current.data, N * sizeof(float)) == 0);
    CHECK(memcmp(a.current.data, material, N * sizeof(float)) != 0);
    for (size_t n = 0; n < N; ++n) CHECK(isfinite(a.current.data[n]));

    setup(&selected, material, N, RATE);
    ts_instrument_set_selection(&selected, 1200, 5200);
    CHECK(ts_instrument_apply_tear(&selected, 1.0f, error, sizeof(error)));
    CHECK(memcmp(selected.current.data, material, 1200 * sizeof(float)) == 0);
    CHECK(memcmp(selected.current.data + 5200, material + 5200,
                 (N - 5200) * sizeof(float)) == 0);
    CHECK(memcmp(selected.current.data + 1200, material + 1200,
                 4000 * sizeof(float)) != 0);

    setup(&eof, material, N, RATE);
    ts_instrument_set_selection(&eof, 4096, N);
    CHECK(ts_instrument_apply_tear(&eof, 1.0f, error, sizeof(error)));
    CHECK(eof.selection_last == N);
    CHECK(memcmp(eof.current.data, material, 4096 * sizeof(float)) == 0);
    CHECK(memcmp(eof.current.data + 4096, material + 4096,
                 (N - 4096) * sizeof(float)) != 0);

    setup(&gesture_a, material, N, RATE); setup(&gesture_b, material, N, RATE);
    ts_tear_gesture_init(&gesture);
    CHECK(ts_instrument_tear_gesture_begin(&gesture_a, &gesture, error, sizeof(error)));
    CHECK(ts_instrument_tear_gesture_preview(&gesture_a, &gesture, 0.25f, error, sizeof(error)));
    CHECK(ts_instrument_tear_gesture_preview(&gesture_a, &gesture, 0.75f, error, sizeof(error)));
    memcpy(strong, gesture_a.current.data, N * sizeof(float));
    CHECK(gesture_a.undo_count == 0 && gesture_a.view_first == 320 && gesture_a.view_last == 1800);
    CHECK(ts_instrument_tear_gesture_cancel(&gesture_a, &gesture, error, sizeof(error)));
    CHECK(memcmp(gesture_a.current.data, material, N * sizeof(float)) == 0);
    CHECK(gesture_a.view_first == 320 && gesture_a.view_last == 1800);

    ts_tear_gesture_init(&gesture);
    CHECK(ts_instrument_tear_gesture_begin(&gesture_b, &gesture, error, sizeof(error)));
    CHECK(ts_instrument_tear_gesture_preview(&gesture_b, &gesture, 0.75f, error, sizeof(error)));
    CHECK(memcmp(gesture_b.current.data, strong, N * sizeof(float)) == 0);
    CHECK(ts_instrument_tear_gesture_preview(&gesture_b, &gesture, 0.0f, error, sizeof(error)));
    CHECK(memcmp(gesture_b.current.data, material, N * sizeof(float)) == 0);
    CHECK(gesture_b.view_first == 320 && gesture_b.view_last == 1800);
    CHECK(ts_instrument_tear_gesture_preview(&gesture_b, &gesture, 0.75f, error, sizeof(error)));
    CHECK(ts_instrument_tear_gesture_commit(&gesture_b, &gesture, error, sizeof(error)));
    CHECK(gesture_b.undo_count == 1 && gesture_b.view_first == 320 && gesture_b.view_last == 1800);
    CHECK(ts_instrument_undo(&gesture_b, error, sizeof(error)));
    CHECK(memcmp(gesture_b.current.data, material, N * sizeof(float)) == 0);
    CHECK(gesture_b.view_first == 320 && gesture_b.view_last == 1800);
    CHECK(ts_instrument_redo(&gesture_b, error, sizeof(error)));
    CHECK(memcmp(gesture_b.current.data, strong, N * sizeof(float)) == 0);
    CHECK(gesture_b.view_first == 320 && gesture_b.view_last == 1800);
    ts_instrument_init(&serial); ts_instrument_init(&restored);
    CHECK(ts_instrument_generate(&serial, TS_GENERATOR_PULSE, 0x54454152u,
                                 error, sizeof(error)));
    CHECK(ts_instrument_apply_tear(&serial, 0.75f, error, sizeof(error)));
    {
        uint64_t serial_hash = ts_sample_hash(&serial.current);
    CHECK(ts_instrument_save_recipe(&serial, "test-tear.tsr", error, sizeof(error)));
    if (!ts_instrument_load_recipe(&restored, "test-tear.tsr", error, sizeof(error))) {
        fprintf(stderr, "TEAR roundtrip load: %s\n", error); ++failures;
    } else {
        CHECK(restored.post_edit_count == 1 && restored.post_edits[0].kind == TS_POST_TEAR);
        CHECK(ts_sample_hash(&restored.current) == serial_hash);
    }
    }
    remove("test-tear.tsr");

    {
        float quiet[512] = {0}; setup(&silence, quiet, 512, RATE);
        CHECK(ts_instrument_apply_tear(&silence, 1.0f, error, sizeof(error)));
        for (size_t n = 0; n < 512; ++n) CHECK(isfinite(silence.current.data[n]));
    }
    ts_instrument_free(&zero); ts_instrument_free(&a); ts_instrument_free(&b);
    ts_instrument_free(&selected); ts_instrument_free(&eof);
    ts_instrument_free(&gesture_a); ts_instrument_free(&gesture_b); ts_instrument_free(&silence);
    ts_instrument_free(&restored);
    ts_instrument_free(&serial);
    free(material); free(strong);
    if (failures) return 1;
    puts("TapeSister TEAR transform and gesture tests passed");
    return 0;
}
