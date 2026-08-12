#include "tapesister/sample.h"
#include "tapesister/ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static int file_contains(const char *path, const char *needle)
{
    FILE *f = fopen(path, "rb");
    char buffer[8192];
    size_t used;
    if (f == NULL) return 0;
    used = fread(buffer, 1, sizeof(buffer) - 1, f);
    buffer[used] = '\0';
    fclose(f);
    return strstr(buffer, needle) != NULL;
}

int main(void)
{
    TsSample a, b, loaded, copy, dry, effected, repeated;
    TsInstrument generated, imported, committed;
    TsUiState ui;
    TsFramebuffer fb;
    char error[160];
    uint64_t parent_hash, current_hash, edited_hash;
    ts_sample_init(&a); ts_sample_init(&b); ts_sample_init(&loaded); ts_sample_init(&copy);
    ts_sample_init(&dry); ts_sample_init(&effected); ts_sample_init(&repeated);
    ts_instrument_init(&generated); ts_instrument_init(&imported); ts_instrument_init(&committed);

    TsGeneratorRecipe first = generator(0x54415045u, TS_GENERATOR_TONAL);
    CHECK(ts_sample_generate(&a, &first, error, sizeof(error)));
    CHECK(ts_sample_generate(&b, &first, error, sizeof(error)));
    CHECK(a.frames == 11025);
    CHECK(a.sample_rate == 44100);
    CHECK(ts_sample_hash(&a) == ts_sample_hash(&b));
    CHECK(ts_sample_peak(&a) > 0.1f && ts_sample_peak(&a) <= 1.0f);
    CHECK(ts_sample_clone(&copy, &a, error, sizeof(error)));
    CHECK(ts_sample_hash(&copy) == ts_sample_hash(&a));

    TsProcessRecipe neutral;
    ts_process_recipe_reset(&neutral);
    CHECK(ts_sample_process(&dry, &a, 0, a.frames, &neutral, error, sizeof(error)));
    CHECK(ts_sample_hash(&dry) == ts_sample_hash(&a));
    TsProcessRecipe dsp = neutral;
    dsp.seed = 0x12345678u;
    dsp.noise_enabled = 1;
    dsp.noise_amount = 0.28f;
    dsp.noise_color = TS_NOISE_METALLIC;
    dsp.delay_enabled = 1;
    dsp.delay_seconds = 0.037f;
    dsp.delay_feedback = 0.51f;
    dsp.delay_damping = 0.43f;
    dsp.delay_mix = 0.31f;
    dsp.reverb_enabled = 1;
    dsp.reverb_decay = 0.67f;
    dsp.reverb_damping = 0.58f;
    dsp.reverb_mix = 0.29f;
    CHECK(ts_sample_process(&effected, &a, 0, a.frames, &dsp, error, sizeof(error)));
    CHECK(ts_sample_process(&repeated, &a, 0, a.frames, &dsp, error, sizeof(error)));
    CHECK(ts_sample_hash(&effected) != ts_sample_hash(&dry));
    CHECK(ts_sample_hash(&effected) == ts_sample_hash(&repeated));

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

    process = imported.process;
    process.noise_enabled = 1;
    process.noise_amount = 0.32f;
    process.delay_enabled = 1;
    process.delay_mix = 0.36f;
    process.reverb_enabled = 1;
    process.reverb_mix = 0.41f;
    CHECK(ts_instrument_set_process(&imported, &process, error, sizeof(error)));
    edited_hash = ts_sample_hash(&imported.current);
    CHECK(ts_sample_hash(&imported.parent) == parent_hash);
    CHECK(ts_instrument_reset_current(&imported, error, sizeof(error)));
    CHECK(ts_sample_hash(&imported.current) == ts_sample_hash(&imported.parent));
    CHECK(imported.current.frames == imported.parent.frames);
    CHECK(memcmp(imported.current.data, imported.parent.data,
                 imported.parent.frames * sizeof(float)) == 0);
    CHECK(!imported.process.noise_enabled && !imported.process.delay_enabled &&
          !imported.process.reverb_enabled);
    CHECK(ts_instrument_undo(&imported, error, sizeof(error)));
    CHECK(ts_sample_hash(&imported.current) == edited_hash);

    CHECK(ts_instrument_generate(&committed, TS_GENERATOR_METALLIC, 0x90909090u,
                                 error, sizeof(error)));
    parent_hash = ts_sample_hash(&committed.parent);
    process = committed.process;
    process.delay_enabled = 1;
    process.delay_seconds = 0.024f;
    process.delay_feedback = 0.62f;
    process.delay_mix = 0.45f;
    process.reverb_enabled = 1;
    process.reverb_mix = 0.34f;
    CHECK(ts_instrument_set_process(&committed, &process, error, sizeof(error)));
    edited_hash = ts_sample_hash(&committed.current);
    CHECK(ts_instrument_commit_current(&committed, error, sizeof(error)));
    CHECK(committed.source_kind == TS_SOURCE_COMMITTED);
    CHECK(committed.generation == 1);
    CHECK(committed.ancestor_hash == parent_hash);
    CHECK(ts_sample_hash(&committed.parent) == edited_hash);
    CHECK(ts_sample_hash(&committed.current) == edited_hash);
    CHECK(memcmp(committed.current.data, committed.parent.data,
                 committed.parent.frames * sizeof(float)) == 0);
    CHECK(committed.undo_count == 0 && committed.redo_count == 0);
    CHECK(!committed.process.delay_enabled && !committed.process.reverb_enabled);

    CHECK(ts_instrument_save_recipe(&committed, "test-recipe.tsr", error, sizeof(error)));
    CHECK(file_contains("test-recipe.tsr", "\"schema\": 3"));
    CHECK(file_contains("test-recipe.tsr", "\"renderer\": 2"));
    CHECK(file_contains("test-recipe.tsr", "\"bypass\": true"));
    CHECK(file_contains("test-recipe.tsr", "\"generation\": 1"));
    CHECK(file_contains("test-recipe.tsr", "\"ancestor_hash\""));
    remove("test-recipe.tsr");
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
    CHECK(ts_ui_key_from_point(50, 340) == 1);
    CHECK(ts_ui_key_from_point(0, 0) == -1);

    ts_sample_free(&a); ts_sample_free(&b); ts_sample_free(&loaded); ts_sample_free(&copy);
    ts_sample_free(&dry); ts_sample_free(&effected); ts_sample_free(&repeated);
    ts_instrument_free(&generated); ts_instrument_free(&imported); ts_instrument_free(&committed);
    if (failures) return 1;
    puts("TapeSister Parent/Current, DSP, commit, and editor tests passed");
    return 0;
}
