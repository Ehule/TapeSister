#include "tapesister/sample.h"
#include "tapesister/ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

static uint64_t waveform_hash(const TsFramebuffer *fb)
{
    uint64_t hash = 1469598103934665603ull;
    for (int y = TS_WAVE_Y; y < TS_WAVE_Y + TS_WAVE_H; ++y) {
        for (int x = TS_WAVE_X; x < TS_WAVE_X + TS_WAVE_W; ++x) {
            hash ^= fb->pixels[y * TS_UI_WIDTH + x];
            hash *= 1099511628211ull;
        }
    }
    return hash;
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

static int browser_find(const TsBrowser *browser, const char *name)
{
    for (int i = 0; i < browser->entry_count; ++i)
        if (strcmp(browser->entries[i].name, name) == 0) return i;
    return -1;
}

int main(void)
{
    TsSample a, b, loaded, copy, dry, effected, repeated;
    TsInstrument generated, imported, committed, audition;
    TsUiState ui;
    TsBrowser browser;
    TsFramebuffer fb;
    char error[160];
    uint64_t parent_hash, current_hash, edited_hash;
    ts_sample_init(&a); ts_sample_init(&b); ts_sample_init(&loaded); ts_sample_init(&copy);
    ts_sample_init(&dry); ts_sample_init(&effected); ts_sample_init(&repeated);
    ts_instrument_init(&generated); ts_instrument_init(&imported); ts_instrument_init(&committed);
    ts_instrument_init(&audition);

    TsGeneratorRecipe first = generator(0x54415045u, TS_GENERATOR_TONAL);
    CHECK(ts_sample_generate(&a, &first, error, sizeof(error)));
    CHECK(ts_sample_generate(&b, &first, error, sizeof(error)));
    CHECK(a.frames == 11025);
    CHECK(a.sample_rate == 44100);
    CHECK(ts_sample_hash(&a) == ts_sample_hash(&b));
    CHECK(ts_sample_peak(&a) > 0.1f && ts_sample_peak(&a) <= 1.0f);
    CHECK(ts_sample_clone(&copy, &a, error, sizeof(error)));
    CHECK(ts_sample_hash(&copy) == ts_sample_hash(&a));

    {
        float crossings[] = {0.8f, 0.5f, -0.2f, -0.4f, 0.1f};
        float no_crossings[] = {0.8f, 0.2f, 0.5f};
        TsSample crossing_sample = {crossings, 5, 44100, "crossings"};
        TsSample fallback_sample = {no_crossings, 3, 44100, "fallback"};
        TsInstrument snap_instrument = {0};
        CHECK(ts_sample_nearest_zero_crossing(&crossing_sample, 3) == 2);
        CHECK(ts_sample_nearest_zero_crossing(&crossing_sample, 4) == 4);
        CHECK(ts_sample_nearest_zero_crossing(&fallback_sample, 2) == 1);
        snap_instrument.current = crossing_sample;
        ts_instrument_set_selection_snapped(&snap_instrument, 3, 4);
        CHECK(snap_instrument.has_selection);
        CHECK(snap_instrument.selection_first == 2 && snap_instrument.selection_last == 4);
        ts_instrument_set_selection_snapped(&snap_instrument, 4, 3);
        CHECK(snap_instrument.selection_first == 2 && snap_instrument.selection_last == 4);
        ts_instrument_set_selection(&snap_instrument, 0, crossing_sample.frames);
        CHECK(snap_instrument.selection_first == 0 &&
              snap_instrument.selection_last == crossing_sample.frames);
    }

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

    {
        TsAuditionPlan plan;
        CHECK(ts_instrument_generate(&audition, TS_GENERATOR_PULSE, 0x41420001u,
                                     error, sizeof(error)));
        ts_instrument_set_selection(&audition, 100, 1000);
        CHECK(ts_instrument_crop_selection(&audition, error, sizeof(error)));
        CHECK(audition.crop_first == 100);
        ts_instrument_set_selection(&audition, 10, 500);
        audition.view_first = 5;
        audition.view_last = 550;
        CHECK(ts_audition_plan(&audition, TS_AUDITION_CURRENT,
                               TS_AUDITION_SELECTION, &plan));
        CHECK(plan.sample == &audition.current && plan.first == 10 && plan.last == 500);
        CHECK(ts_audition_plan(&audition, TS_AUDITION_PARENT,
                               TS_AUDITION_SELECTION, &plan));
        CHECK(plan.sample == &audition.parent && plan.first == 110 && plan.last == 600);
        CHECK(ts_audition_plan(&audition, TS_AUDITION_PARENT,
                               TS_AUDITION_DISPLAYED, &plan));
        CHECK(plan.first == 105 && plan.last == 650);
        CHECK(ts_audition_plan(&audition, TS_AUDITION_PARENT,
                               TS_AUDITION_ALL, &plan));
        CHECK(plan.first == 0 && plan.last == audition.parent.frames);
        CHECK(fabs(ts_audition_map_progress(15.0, 10, 20, 110, 120) - 115.0) < 0.000001);
        CHECK(ts_instrument_set_loop_from_selection(&audition, error, sizeof(error)));
        CHECK(audition.has_loop);
        CHECK(audition.loop_first ==
              ts_sample_nearest_zero_crossing(&audition.current, 10));
        CHECK(audition.loop_last ==
              ts_sample_nearest_zero_crossing(&audition.current, 500));
        CHECK(ts_audition_plan(&audition, TS_AUDITION_CURRENT, TS_AUDITION_LOOP, &plan));
        CHECK(plan.first == audition.loop_first && plan.last == audition.loop_last);
        CHECK(ts_audition_plan(&audition, TS_AUDITION_PARENT, TS_AUDITION_LOOP, &plan));
        CHECK(plan.first == audition.loop_first + audition.crop_first);
        CHECK(plan.last == audition.loop_last + audition.crop_first);
        CHECK(ts_audition_crossfade_frames(&plan, audition.loop_crossfade_ms) <=
              (plan.last - plan.first) / 2u);
        CHECK(ts_instrument_clear_loop(&audition, error, sizeof(error)));
        CHECK(!audition.has_loop);
        CHECK(ts_instrument_undo(&audition, error, sizeof(error)) && audition.has_loop);
        CHECK(ts_instrument_redo(&audition, error, sizeof(error)) && !audition.has_loop);
        CHECK(ts_instrument_undo(&audition, error, sizeof(error)) && audition.has_loop);
        CHECK(ts_instrument_set_loop_crossfade(&audition, 12.5f, error, sizeof(error)));
        CHECK(fabsf(audition.loop_crossfade_ms - 12.5f) < 0.0001f);
        CHECK(ts_instrument_undo(&audition, error, sizeof(error)));
        CHECK(fabsf(audition.loop_crossfade_ms - 8.0f) < 0.0001f);
        CHECK(ts_instrument_redo(&audition, error, sizeof(error)));
        CHECK(fabsf(audition.loop_crossfade_ms - 12.5f) < 0.0001f);
        {
            size_t loop_first = audition.loop_first;
            size_t loop_last = audition.loop_last;
            ts_instrument_set_selection(&audition, loop_first, loop_last);
            CHECK(ts_instrument_crop_selection(&audition, error, sizeof(error)));
            CHECK(audition.has_loop && audition.loop_first == 0 &&
                  audition.loop_last == loop_last - loop_first);
            CHECK(ts_instrument_undo(&audition, error, sizeof(error)));
            CHECK(audition.has_loop && audition.loop_first == loop_first &&
                  audition.loop_last == loop_last);
            CHECK(ts_instrument_reset_current(&audition, error, sizeof(error)));
            CHECK(!audition.has_loop);
            CHECK(ts_instrument_undo(&audition, error, sizeof(error)) && audition.has_loop);
            CHECK(ts_instrument_commit_current(&audition, error, sizeof(error)));
            CHECK(audition.has_loop && audition.loop_first == loop_first &&
                  audition.loop_last == loop_last);
            CHECK(audition.undo_count == 0 && audition.redo_count == 0);
        }
    }

    {
        float loop_data[] = {1.0f, 1.0f, 0.5f, 0.0f, -0.5f, -1.0f, -1.0f, -1.0f};
        TsSample loop_sample = {loop_data, 8, 1000, "loop"};
        float blended = ts_audition_read_looped(&loop_sample, 7.0, 0, 8, 2);
        CHECK(fabsf(blended) < 0.0001f);
        CHECK(fabs(ts_audition_wrap_position(8.0, 0, 8, 2) - 2.0) < 0.000001);
        CHECK(fabs(ts_audition_wrap_position(14.0, 0, 8, 2) - 2.0) < 0.000001);
    }

    CHECK(ts_sample_clone(&copy, &committed.current, error, sizeof(error)));
    parent_hash = ts_sample_hash(&committed.parent);
    ts_instrument_set_selection(&committed, 100, 1000);
    CHECK(ts_instrument_apply_sample_edit(&committed, TS_SAMPLE_EDIT_REVERSE, 1.0f,
                                          error, sizeof(error)));
    CHECK(committed.sample_edit_count == 1);
    CHECK(ts_sample_hash(&committed.parent) == parent_hash);
    CHECK(fabsf(committed.current.data[100] - copy.data[999]) < 0.000001f);
    CHECK(fabsf(committed.current.data[999] - copy.data[100]) < 0.000001f);
    edited_hash = ts_sample_hash(&committed.current);
    CHECK(ts_instrument_undo(&committed, error, sizeof(error)));
    CHECK(ts_sample_hash(&committed.current) == ts_sample_hash(&copy));
    CHECK(committed.sample_edit_count == 0);
    CHECK(ts_instrument_redo(&committed, error, sizeof(error)));
    CHECK(ts_sample_hash(&committed.current) == edited_hash);
    CHECK(committed.sample_edit_count == 1);

    CHECK(ts_instrument_apply_sample_edit(&committed, TS_SAMPLE_EDIT_FADE_IN, 1.0f,
                                          error, sizeof(error)));
    CHECK(fabsf(committed.current.data[100]) < 0.000001f);
    CHECK(ts_instrument_apply_sample_edit(&committed, TS_SAMPLE_EDIT_FADE_OUT, 1.0f,
                                          error, sizeof(error)));
    CHECK(fabsf(committed.current.data[999]) < 0.000001f);
    CHECK(ts_instrument_apply_sample_edit(&committed, TS_SAMPLE_EDIT_GAIN, 0.5f,
                                          error, sizeof(error)));
    CHECK(ts_instrument_apply_sample_edit(&committed, TS_SAMPLE_EDIT_NORMALIZE, 0.98f,
                                          error, sizeof(error)));
    CHECK(committed.sample_edit_count == 5);

    ts_instrument_show_all(&committed);
    {
        size_t anchor = committed.current.frames / 4u;
        CHECK(ts_instrument_zoom_view(&committed, anchor, 0.25f, 0.5f));
        CHECK(committed.view_last - committed.view_first == committed.current.frames / 2u);
        {
            size_t mapped = ts_instrument_frame_from_view_x(&committed, 150, 600);
            CHECK(mapped + 1u >= anchor && mapped <= anchor + 1u);
        }
        CHECK(ts_instrument_pan_view(&committed,
                                     (ptrdiff_t)((committed.view_last - committed.view_first) / 8u)));
        CHECK(ts_instrument_pan_view(&committed,
                                     -(ptrdiff_t)((committed.view_last - committed.view_first) / 8u)));
        ts_instrument_show_all(&committed);
    }

    CHECK(ts_instrument_set_loop_from_selection(&committed, error, sizeof(error)));
    CHECK(ts_instrument_set_loop_crossfade(&committed, 9.5f, error, sizeof(error)));
    CHECK(ts_instrument_save_recipe(&committed, "test-recipe.tsr", error, sizeof(error)));
    CHECK(file_contains("test-recipe.tsr", "\"schema\": 5"));
    CHECK(file_contains("test-recipe.tsr", "\"renderer\": 3"));
    CHECK(file_contains("test-recipe.tsr", "\"bypass\": true"));
    CHECK(file_contains("test-recipe.tsr", "\"generation\": 1"));
    CHECK(file_contains("test-recipe.tsr", "\"ancestor_hash\""));
    CHECK(file_contains("test-recipe.tsr", "\"sample_edits\""));
    CHECK(file_contains("test-recipe.tsr", "\"REVERSE\""));
    CHECK(file_contains("test-recipe.tsr", "\"loop\": {\"enabled\": true"));
    CHECK(file_contains("test-recipe.tsr", "\"crossfade_ms\": 9.5"));
    remove("test-recipe.tsr");
    remove("test-roundtrip.wav");

    {
        FILE *test_file;
        char path[TS_BROWSER_PATH_MAX];
        mkdir("test-browser-dir", 0700);
        test_file = fopen("test-browser-load.wav", "wb");
        CHECK(test_file != NULL);
        if (test_file != NULL) fclose(test_file);
        test_file = fopen("test-browser-save.tsr", "wb");
        CHECK(test_file != NULL);
        if (test_file != NULL) fclose(test_file);
        test_file = fopen("test-browser-ignore.txt", "wb");
        CHECK(test_file != NULL);
        if (test_file != NULL) fclose(test_file);

        ts_browser_init(&browser);
        CHECK(ts_browser_open(&browser, TS_BROWSER_LOAD_WAV, NULL));
        CHECK(browser_find(&browser, "test-browser-dir") >= 0);
        CHECK(browser_find(&browser, "test-browser-load.wav") >= 0);
        CHECK(browser_find(&browser, "test-browser-save.tsr") < 0);
        CHECK(browser_find(&browser, "test-browser-ignore.txt") < 0);
        ts_browser_select(&browser, browser_find(&browser, "test-browser-load.wav"));
        CHECK(ts_browser_selected_path(&browser, path, sizeof(path)));
        CHECK(strstr(path, "test-browser-load.wav") != NULL);
        ts_browser_select(&browser, browser_find(&browser, "test-browser-dir"));
        CHECK(ts_browser_enter_selected_directory(&browser));
        CHECK(strstr(browser.directory, "test-browser-dir") != NULL);
        CHECK(ts_browser_parent(&browser));

        CHECK(ts_browser_open(&browser, TS_BROWSER_SAVE_RECIPE, "new-family"));
        CHECK(browser_find(&browser, "test-browser-save.tsr") >= 0);
        CHECK(browser_find(&browser, "test-browser-load.wav") < 0);
        CHECK(ts_browser_destination_path(&browser, path, sizeof(path)));
        CHECK(strstr(path, "new-family.tsr") != NULL);
        ts_browser_set_filename(&browser, "named.tsr");
        CHECK(ts_browser_destination_path(&browser, path, sizeof(path)));
        CHECK(strstr(path, "named.tsr") != NULL);
        CHECK(strstr(path, "named.tsr.tsr") == NULL);
        ts_browser_set_filename(&browser, "safe");
        ts_browser_append_filename(&browser, "/name");
        CHECK(strcmp(browser.filename, "safename") == 0);
        ts_browser_backspace_filename(&browser);
        CHECK(strcmp(browser.filename, "safenam") == 0);

        CHECK(ts_browser_open(&browser, TS_BROWSER_EXPORT_WAV, "current"));
        CHECK(ts_browser_destination_path(&browser, path, sizeof(path)));
        CHECK(strstr(path, "current.wav") != NULL);
        browser.entry_count = 30;
        browser.selected = 0;
        ts_browser_set_scroll(&browser, 999);
        CHECK(browser.scroll == 30 - TS_BROWSER_VISIBLE_ROWS);
        ts_browser_move_selection(&browser, 999);
        CHECK(browser.selected == 29);
        ts_browser_move_selection(&browser, -999);
        CHECK(browser.selected == 0);
        ts_browser_close(&browser);
        CHECK(browser.mode == TS_BROWSER_CLOSED);

        remove("test-browser-load.wav");
        remove("test-browser-save.tsr");
        remove("test-browser-ignore.txt");
        rmdir("test-browser-dir");
    }

    ts_ui_init(&ui);
    ui.fx_page = TS_FX_LOOP;
    ts_instrument_show_all(&committed);
    ts_ui_render(&fb, &ui, &committed);
    {
        int loop_x = TS_WAVE_X + (int)(committed.loop_first * TS_WAVE_W /
                                       committed.current.frames);
        CHECK(fb.pixels[(TS_WAVE_Y + 10) * TS_UI_WIDTH + loop_x] == 0xff147dffu);
    }
    ts_ui_init(&ui);
    ts_instrument_set_selection(&imported, imported.current.frames / 4, imported.current.frames / 2);
    ts_ui_render(&fb, &ui, &imported);
    CHECK(fb.pixels[0] != 0);
    CHECK(framebuffer_contains(&fb, 0xff1c1c1cu));
    CHECK(framebuffer_contains(&fb, 0xffffe700u));
    CHECK(framebuffer_contains(&fb, 0xff2d0039u));
    CHECK(framebuffer_contains(&fb, 0xff009ee3u));
    {
        uint64_t current_waveform = waveform_hash(&fb);
        ui.audition_source = TS_AUDITION_PARENT;
        ts_ui_render(&fb, &ui, &imported);
        CHECK(waveform_hash(&fb) != current_waveform);
        ui.audition_source = TS_AUDITION_CURRENT;
    }
    ui.playback_active = 1;
    ui.playhead_source = TS_AUDITION_CURRENT;
    ui.playhead_frame = imported.current.frames / 2;
    ui.playhead_frames = imported.current.frames;
    ts_instrument_show_all(&imported);
    ts_ui_render(&fb, &ui, &imported);
    CHECK(fb.pixels[(TS_WAVE_Y + 1) * TS_UI_WIDTH + TS_WAVE_X + TS_WAVE_W / 2] ==
          0xffffd265u);
    ui.playhead_source = TS_AUDITION_PARENT;
    ui.audition_source = TS_AUDITION_PARENT;
    ui.playhead_frame = imported.parent.frames / 2;
    ui.playhead_frames = imported.parent.frames;
    ts_ui_render(&fb, &ui, &imported);
    CHECK(fb.pixels[(TS_WAVE_Y + 1) * TS_UI_WIDTH + TS_WAVE_X + TS_WAVE_W / 2] ==
          0xff18ff00u);
    CHECK(ts_browser_open(&ui.browser, TS_BROWSER_EXPORT_WAV, "cursor.wav"));
    ui.text_cursor_visible = 1;
    ts_ui_render(&fb, &ui, &imported);
    CHECK(fb.pixels[301 * TS_UI_WIDTH + 64 + 10 * 6] == 0xffffd265u);
    ui.browser.filename_focus = 0;
    ts_ui_render(&fb, &ui, &imported);
    CHECK(fb.pixels[301 * TS_UI_WIDTH + 64 + 10 * 6] != 0xffffd265u);
    ts_browser_close(&ui.browser);
    CHECK(ts_ui_key_from_point(20, 370) == 0);
    CHECK(ts_ui_key_from_point(50, 340) == 1);
    CHECK(ts_ui_key_from_point(0, 0) == -1);

    ts_sample_free(&a); ts_sample_free(&b); ts_sample_free(&loaded); ts_sample_free(&copy);
    ts_sample_free(&dry); ts_sample_free(&effected); ts_sample_free(&repeated);
    ts_instrument_free(&generated); ts_instrument_free(&imported); ts_instrument_free(&committed);
    ts_instrument_free(&audition);
    if (failures) return 1;
    puts("TapeSister zero-snap, loop, A/B audition, browser, and editor tests passed");
    return 0;
}
