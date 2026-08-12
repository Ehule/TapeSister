#include "tapesister/sample.h"
#include "tapesister/ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); ++failures; } } while (0)

static TsRecipe recipe(uint32_t seed)
{
    TsRecipe result = {seed, 0.62f, 0.34f, 0.23f, 0.25f, 130.8128f};
    return result;
}

int main(void)
{
    TsSample a, b, loaded;
    TsUiState ui;
    TsFramebuffer fb;
    char error[160];
    ts_sample_init(&a); ts_sample_init(&b); ts_sample_init(&loaded);

    TsRecipe first = recipe(0x54415045u);
    CHECK(ts_sample_generate(&a, &first, error, sizeof(error)));
    CHECK(ts_sample_generate(&b, &first, error, sizeof(error)));
    CHECK(a.frames == 11025);
    CHECK(a.sample_rate == 44100);
    CHECK(ts_sample_hash(&a) == ts_sample_hash(&b));
    CHECK(ts_sample_peak(&a) > 0.1f && ts_sample_peak(&a) <= 1.0f);

    ts_sample_free(&b);
    TsRecipe second = recipe(0x54415046u);
    CHECK(ts_sample_generate(&b, &second, error, sizeof(error)));
    CHECK(ts_sample_hash(&a) != ts_sample_hash(&b));

    CHECK(ts_sample_save_wav16(&a, "test-roundtrip.wav", error, sizeof(error)));
    CHECK(ts_sample_load_wav(&loaded, "test-roundtrip.wav", error, sizeof(error)));
    CHECK(loaded.frames == a.frames);
    CHECK(loaded.sample_rate == a.sample_rate);
    CHECK(fabsf(ts_sample_peak(&loaded) - ts_sample_peak(&a)) < 0.001f);
    remove("test-roundtrip.wav");

    ts_ui_init(&ui);
    ts_ui_render(&fb, &ui, &a);
    CHECK(fb.pixels[0] != 0);
    CHECK(ts_ui_key_from_point(20, 370) == 0);
    CHECK(ts_ui_key_from_point(50, 320) == 1);
    CHECK(ts_ui_key_from_point(0, 0) == -1);

    ts_sample_free(&a); ts_sample_free(&b); ts_sample_free(&loaded);
    if (failures) return 1;
    puts("TapeSister core tests passed");
    return 0;
}
