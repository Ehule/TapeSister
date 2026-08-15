#include "tapesister/sample.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "SMEAR CHECK FAILED line %d: %s\n", __LINE__, #x); ++failures; } } while (0)

static void setup(TsInstrument *i, const float *data, size_t frames, int rate)
{
    char error[160];
    ts_instrument_init(i);
    i->parent.data = (float *)malloc(frames * sizeof(float));
    memcpy(i->parent.data, data, frames * sizeof(float));
    i->parent.frames = frames; i->parent.sample_rate = rate;
    i->crop_last = frames; i->view_last = frames;
    if (!ts_sample_clone(&i->current, &i->parent, error, sizeof(error))) abort();
}

static double energy(const float *x, size_t first, size_t last)
{
    double e = 0.0; for (size_t n = first; n < last; ++n) e += (double)x[n] * x[n]; return e;
}

int main(void)
{
    enum { RATE = 44100, N = 8192 };
    char error[160]; int failures = 0; float *tone = calloc(N, sizeof(float));
    TsInstrument zero, low, high, selected; TsSmearGesture gesture;
    for (size_t n = 0; n < 700; ++n) tone[n] = 0.55f * sinf(2.0f * 3.14159265358979323846f * 440.0f * n / RATE);
    setup(&zero, tone, N, RATE); CHECK(ts_instrument_apply_smear(&zero, 0.0f, error, sizeof(error)));
    CHECK(zero.current.frames == N); CHECK(memcmp(zero.current.data, tone, N * sizeof(float)) == 0);
    setup(&low, tone, N, RATE); setup(&high, tone, N, RATE);
    CHECK(ts_instrument_apply_smear(&low, 0.2f, error, sizeof(error)));
    CHECK(ts_instrument_apply_smear(&high, 0.9f, error, sizeof(error)));
    CHECK(energy(low.current.data, 1000, 4000) > 1e-5);
    CHECK(energy(high.current.data, 2500, 6000) > energy(low.current.data, 2500, 6000));
    for (size_t n = 0; n < N; ++n) CHECK(isfinite(low.current.data[n]) && isfinite(high.current.data[n]));
    {
        const float amounts[] = {0.05f, 0.5f, 1.0f};
        for (size_t a = 0; a < sizeof(amounts) / sizeof(amounts[0]); ++a) {
            TsInstrument finite; setup(&finite, tone, N, RATE);
            CHECK(ts_instrument_apply_smear(&finite, amounts[a], error, sizeof(error)));
            for (size_t n = 0; n < N; ++n) CHECK(isfinite(finite.current.data[n]));
            ts_instrument_free(&finite);
        }
    }
    {
        TsInstrument repeat; setup(&repeat, tone, N, RATE);
        CHECK(ts_instrument_apply_smear(&repeat, 0.9f, error, sizeof(error)));
        CHECK(memcmp(repeat.current.data, high.current.data, N * sizeof(float)) == 0);
        ts_instrument_free(&repeat);
    }
    setup(&selected, tone, N, RATE); ts_instrument_set_selection(&selected, 300, 5000);
    CHECK(ts_instrument_apply_smear(&selected, 0.7f, error, sizeof(error)));
    CHECK(memcmp(selected.current.data, tone, 300 * sizeof(float)) == 0);
    CHECK(memcmp(selected.current.data + 5000, tone + 5000, (N - 5000) * sizeof(float)) == 0);
    {
        float silence[4096] = {0}; TsInstrument tiny; setup(&tiny, silence, 4096, RATE);
        ts_instrument_set_selection(&tiny, 17, 18); CHECK(ts_instrument_apply_smear(&tiny, 1.0f, error, sizeof(error)));
        CHECK(memcmp(tiny.current.data, silence, sizeof(silence)) == 0);
        ts_instrument_clear_selection(&tiny); CHECK(ts_instrument_apply_smear(&tiny, 1.0f, error, sizeof(error)));
        CHECK(memcmp(tiny.current.data, silence, sizeof(silence)) == 0);
        ts_instrument_free(&tiny);
    }
    {
        float impulse[N]; TsInstrument causal; memset(impulse, 0, sizeof(impulse)); impulse[3000] = 1.0f;
        setup(&causal, impulse, N, RATE); CHECK(ts_instrument_apply_smear(&causal, 1.0f, error, sizeof(error)));
        CHECK(memcmp(causal.current.data, impulse, 3000 * sizeof(float)) == 0);
        ts_instrument_free(&causal);
    }
    {
        float signal[N]; TsInstrument replacement; memset(signal, 0, sizeof(signal));
        for (size_t n = 0; n < 600; ++n) signal[n] = 0.12f * sinf(2.0f * 3.14159265358979323846f * 220.0f * n / RATE);
        for (size_t n = 2600; n < 3400; ++n) signal[n] = 0.9f * sinf(2.0f * 3.14159265358979323846f * 1760.0f * n / RATE);
        setup(&replacement, signal, N, RATE); CHECK(ts_instrument_apply_smear(&replacement, 0.85f, error, sizeof(error)));
        /* Strong new information must dominate the remembered weak event after it arrives. */
        CHECK(energy(replacement.current.data, 3300, 4400) > energy(replacement.current.data, 1800, 2500));
        ts_instrument_free(&replacement);
    }
    ts_smear_gesture_init(&gesture); ts_instrument_free(&selected); setup(&selected, tone, N, RATE);
    ts_instrument_set_selection(&selected, 300, 5000);
    selected.view_first = 32; selected.view_last = 160;
    CHECK(ts_instrument_smear_gesture_begin(&selected, &gesture, error, sizeof(error)));
    CHECK(ts_instrument_smear_gesture_preview(&selected, &gesture, 0.2f, error, sizeof(error)));
    CHECK(selected.view_first == 32 && selected.view_last == 160);
    { float *preview = malloc(N * sizeof(float)); memcpy(preview, selected.current.data, N * sizeof(float));
      CHECK(selected.undo_count == 0); CHECK(ts_instrument_smear_gesture_preview(&selected, &gesture, 0.8f, error, sizeof(error)));
      CHECK(ts_instrument_smear_gesture_preview(&selected, &gesture, 0.2f, error, sizeof(error)));
      CHECK(memcmp(preview, selected.current.data, N * sizeof(float)) == 0); free(preview); }
    CHECK(ts_instrument_smear_gesture_commit(&selected, &gesture, error, sizeof(error)) && selected.undo_count == 1);
    CHECK(selected.view_first == 32 && selected.view_last == 160);
    CHECK(ts_instrument_undo(&selected, error, sizeof(error))); CHECK(memcmp(selected.current.data, tone, N * sizeof(float)) == 0);
    CHECK(ts_instrument_smear_gesture_begin(&selected, &gesture, error, sizeof(error)));
    CHECK(ts_instrument_smear_gesture_preview(&selected, &gesture, 0.7f, error, sizeof(error)));
    CHECK(ts_instrument_smear_gesture_preview(&selected, &gesture, 0.0f, error, sizeof(error)));
    CHECK(ts_instrument_smear_gesture_commit(&selected, &gesture, error, sizeof(error)) && selected.undo_count == 0);
    CHECK(selected.view_first == 32 && selected.view_last == 160);
    CHECK(ts_instrument_smear_gesture_begin(&selected, &gesture, error, sizeof(error)));
    CHECK(ts_instrument_smear_gesture_preview(&selected, &gesture, 0.5f, error, sizeof(error)));
    CHECK(ts_instrument_smear_gesture_cancel(&selected, &gesture, error, sizeof(error)) && selected.undo_count == 0);
    CHECK(selected.view_first == 32 && selected.view_last == 160);
    CHECK(ts_instrument_smear_gesture_begin(&selected, &gesture, error, sizeof(error))); ++selected.generation;
    CHECK(!ts_instrument_smear_gesture_preview(&selected, &gesture, 0.5f, error, sizeof(error))); --selected.generation;
    CHECK(ts_instrument_smear_gesture_cancel(&selected, &gesture, error, sizeof(error)));
    ts_instrument_free(&zero); ts_instrument_free(&low); ts_instrument_free(&high); ts_instrument_free(&selected); free(tone);
    if (failures) return 1;
    puts("TapeSister SMEAR DSP and gesture tests passed");
    return 0;
}
