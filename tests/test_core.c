#include "tapesister/sample.h"
#include "tapesister/ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int failures;

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); ++failures; } } while (0)

static TsGeneratorRecipe generator(uint32_t seed, TsGeneratorKind kind)
{
    TsGeneratorRecipe result = {seed, kind, 0.25f, 130.8128f};
    return result;
}

static int framebuffer_contains(const TsFramebuffer *fb, uint32_t color)
{
    for (int i = 0; i < TS_UI_WIDTH * TS_UI_HEIGHT; ++i)
        if (fb->pixels[i] == color) return 1;
    return 0;
}

int main(void)
{
    TsSample a, b, loaded, copy;
    TsInstrument generated, imported;
    TsUiState ui;
    TsFramebuffer fb;
    char error[160];
    uint64_t parent_hash, current_hash, edited_hash;
    ts_sample_init(&a); ts_sample_init(&b); ts_sample_init(&loaded); ts_sample_init(&copy);
    ts_instrument_init(&generated); ts_instrument_init(&imported);

    TsGeneratorRecipe first = generator(0x54415045u, TS_GENERATOR_TONAL);
    CHECK(ts_sample_generate(&a, &first, error, sizeof(error)));
    CHECK(ts_sample_generate(&b, &first, error, sizeof(error)));
    CHECK(a.frames == 11025);
    CHECK(a.sample_rate == 44100);
    CHECK(ts_sample_hash(&a) == ts_sample_hash(&b));
    CHECK(ts_sample_peak(&a) > 0.1f && ts_sample_peak(&a) <= 1.0f);
    CHECK(ts_sample_clone(&copy, &a, error, sizeof(error)));
    CHECK(ts_sample_hash(&copy) == ts_sample_hash(&a));

    ts_sample_free(&b);
    TsGeneratorRecipe metallic = generator(0x54415045u, TS_GENERATOR_METALLIC);
    CHECK(ts_sample_generate(&b, &metallic, error, sizeof(error)));
    CHECK(ts_sample_hash(&a) != ts_sample_hash(&b));

    CHECK(ts_sample_save_wav16(&a, "test-roundtrip.wav", error, sizeof(error)));
    CHECK(ts_sample_load_wav(&loaded, "test-roundtrip.wav", error, sizeof(error)));
    CHECK(loaded.frames == a.frames);
    CHECK(loaded.sample_rate == a.sample_rate);
    CHECK(fabsf(ts_sample_peak(&loaded) - ts_sample_peak(&a)) < 0.001f);

    CHECK(ts_instrument_generate(&generated, TS_GENERATOR_TONAL, 0x11223344u,
                                 error, sizeof(error)));
    CHECK(generated.source_kind == TS_SOURCE_GENERATED);
    CHECK(generated.parent.frames == generated.current.frames);
    parent_hash = ts_sample_hash(&generated.parent);
    current_hash = ts_sample_hash(&generated.current);

    TsProcessRecipe process = generated.process;
    process.body = 0.95f;
    process.edge = 0.78f;
    process.drift = 0.55f;
    CHECK(ts_instrument_set_process(&generated, &process, error, sizeof(error)));
    CHECK(ts_sample_hash(&generated.parent) == parent_hash);
    edited_hash = ts_sample_hash(&generated.current);
    CHECK(edited_hash != current_hash);
    CHECK(generated.undo_count == 1);
    CHECK(ts_instrument_undo(&generated, error, sizeof(error)));
    CHECK(ts_sample_hash(&generated.current) == current_hash);
    CHECK(ts_sample_hash(&generated.parent) == parent_hash);
    CHECK(ts_instrument_redo(&generated, error, sizeof(error)));
    CHECK(ts_sample_hash(&generated.current) == edited_hash);

    size_t original_frames = generated.current.frames;
    ts_instrument_set_selection(&generated, 100, 1000);
    CHECK(generated.has_selection);
    CHECK(ts_instrument_zoom_selection(&generated));
    CHECK(generated.view_first == 100 && generated.view_last == 1000);
    CHECK(ts_instrument_frame_from_view_x(&generated, 0, 600) == 100);
    CHECK(ts_instrument_frame_from_view_x(&generated, 599, 600) < 1000);
    CHECK(ts_instrument_crop_selection(&generated, error, sizeof(error)));
    CHECK(generated.current.frames == 900);
    CHECK(ts_sample_hash(&generated.parent) == parent_hash);
    CHECK(!generated.has_selection);
    CHECK(generated.view_first == 0 && generated.view_last == 900);
    CHECK(ts_instrument_undo(&generated, error, sizeof(error)));
    CHECK(generated.current.frames == original_frames);
    CHECK(generated.has_selection);
    CHECK(generated.view_first == 100 && generated.view_last == 1000);

    parent_hash = ts_sample_hash(&generated.parent);
    CHECK(ts_instrument_reseed(&generated, error, sizeof(error)));
    CHECK(ts_sample_hash(&generated.parent) != parent_hash);
    CHECK(generated.generator.kind == TS_GENERATOR_TONAL);

    CHECK(ts_instrument_load_wav(&imported, "test-roundtrip.wav", error, sizeof(error)));
    CHECK(imported.source_kind == TS_SOURCE_IMPORTED);
    parent_hash = ts_sample_hash(&imported.parent);
    current_hash = ts_sample_hash(&imported.current);
    CHECK(ts_instrument_reseed(&imported, error, sizeof(error)));
    CHECK(ts_sample_hash(&imported.parent) == parent_hash);
    CHECK(ts_sample_hash(&imported.current) != current_hash);
    remove("test-roundtrip.wav");

    ts_ui_init(&ui);
    ts_instrument_set_selection(&imported, imported.current.frames / 4, imported.current.frames / 2);
    ts_ui_render(&fb, &ui, &imported);
    CHECK(fb.pixels[0] != 0);
    CHECK(framebuffer_contains(&fb, 0xff1c1c1cu));
    CHECK(framebuffer_contains(&fb, 0xffffe700u));
    CHECK(framebuffer_contains(&fb, 0xff2d0039u));
    CHECK(framebuffer_contains(&fb, 0xff009ee3u));
    CHECK(ts_ui_key_from_point(20, 370) == 0);
    CHECK(ts_ui_key_from_point(50, 320) == 1);
    CHECK(ts_ui_key_from_point(0, 0) == -1);

    ts_sample_free(&a); ts_sample_free(&b); ts_sample_free(&loaded); ts_sample_free(&copy);
    ts_instrument_free(&generated); ts_instrument_free(&imported);
    if (failures) return 1;
    puts("TapeSister Parent/Current and editor tests passed");
    return 0;
}
