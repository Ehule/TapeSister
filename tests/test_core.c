#include "tapesister/sample.h"
#include "tapesister/note_bank.h"
#include "tapesister/ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>

static int failures;

#define CHECK(expr) do { if (!(expr)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); ++failures; } } while (0)

static TsGeneratorRecipe generator(uint32_t seed, TsGeneratorKind kind)
{
    TsGeneratorRecipe result = {.seed = seed, .kind = kind, .seconds = 0.25f,
                                .frequency = 130.8128f};
    return result;
}

static int framebuffer_contains(const TsFramebuffer *fb, uint32_t color)
{
    for (int i = 0; i < TS_UI_WIDTH * TS_UI_HEIGHT; ++i)
        if (fb->pixels[i] == color) return 1;
    return 0;
}

static int framebuffer_color_count(const TsFramebuffer *fb, uint32_t color,
                                   int x, int y, int w, int h)
{
    int count = 0;
    for (int py = y; py < y + h; ++py)
        for (int px = x; px < x + w; ++px)
            if (fb->pixels[py * TS_UI_WIDTH + px] == color) ++count;
    return count;
}

static int framebuffer_diff_count(const TsFramebuffer *left,
                                  const TsFramebuffer *right,
                                  int x, int y, int w, int h)
{
    int count = 0;
    for (int py = y; py < y + h; ++py)
        for (int px = x; px < x + w; ++px)
            if (left->pixels[py * TS_UI_WIDTH + px] !=
                right->pixels[py * TS_UI_WIDTH + px]) ++count;
    return count;
}

static int file_contains_text(const char *path, const char *needle)
{
    FILE *file = fopen(path, "rb");
    char line[256];
    if (file == NULL) return 0;
    while (fgets(line, sizeof(line), file) != NULL) {
        if (strstr(line, needle) != NULL) {
            fclose(file);
            return 1;
        }
    }
    fclose(file);
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

static int samples_equal_outside(const TsSample *left, const TsSample *right,
                                 size_t first, size_t last)
{
    if (left == NULL || right == NULL || left->frames != right->frames ||
        left->sample_rate != right->sample_rate || first > last ||
        last > left->frames) return 0;
    if (first > 0 && memcmp(left->data, right->data,
                            first * sizeof(*left->data)) != 0) return 0;
    return last == left->frames ||
           memcmp(left->data + last, right->data + last,
                  (left->frames - last) * sizeof(*left->data)) == 0;
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
    TsInstrument generated, imported, committed, audition, restored, bank_edit, recipe_target;
    TsInstrument family, family_repeat, family_restored;
    TsUiState ui;
    TsNoteBank notes;
    TsRecipeBank recipe_bank;
    TsBrowser browser;
    TsFramebuffer fb;
    char error[160];
    uint64_t parent_hash, current_hash, edited_hash, family_root_hash;
    ts_sample_init(&a); ts_sample_init(&b); ts_sample_init(&loaded); ts_sample_init(&copy);
    ts_sample_init(&dry); ts_sample_init(&effected); ts_sample_init(&repeated);
    ts_instrument_init(&generated); ts_instrument_init(&imported); ts_instrument_init(&committed);
    ts_instrument_init(&audition);
    ts_instrument_init(&restored);
    ts_instrument_init(&bank_edit);
    ts_instrument_init(&recipe_target);
    ts_instrument_init(&family);
    ts_instrument_init(&family_repeat);
    ts_instrument_init(&family_restored);
    ts_note_bank_init(&notes);
    ts_recipe_bank_init(&recipe_bank);
    ts_ui_init(&ui);
    CHECK(ts_ui_transform_auto_audition_allowed(&ui));
    CHECK(ts_ui_loop_command(&ui, 0) == TS_UI_LOOP_START);
    CHECK(ts_ui_loop_command(&ui, 1) == TS_UI_LOOP_LOCK_START);
    ui.workbench_loop_active = 1;
    CHECK(!ts_ui_transform_auto_audition_allowed(&ui));
    ui.workbench_loop_persistent = 1;
    CHECK(ts_ui_loop_command(&ui, 0) == TS_UI_LOOP_LOCKED);
    CHECK(ts_ui_loop_command(&ui, 1) == TS_UI_LOOP_LOCK_RELEASE);
    CHECK(!ts_ui_loop_transport_can_stop(&ui, 0));
    CHECK(ts_ui_loop_transport_can_stop(&ui, 1));
    ui.workbench_loop_active = 0;
    ui.workbench_loop_persistent = 0;
    CHECK(ui.show_keyboard && !ui.show_recipes && !ui.show_ingredients);
    CHECK(ts_ui_panel(&ui) == TS_UI_PANEL_KEYBOARD);
    ts_ui_select_panel(&ui, TS_UI_PANEL_SAMPLE_TILES);
    CHECK(ts_ui_panel(&ui) == TS_UI_PANEL_SAMPLE_TILES &&
          !ui.show_keyboard && !ui.show_recipes && !ui.show_ingredients);
    ts_ui_select_panel(&ui, TS_UI_PANEL_CDP);
    CHECK(ts_ui_panel(&ui) == TS_UI_PANEL_CDP && ui.show_recipes && ui.cdp_page == 0);
    ts_ui_select_panel(&ui, TS_UI_PANEL_CDP);
    CHECK(ts_ui_panel(&ui) == TS_UI_PANEL_CDP && ui.cdp_page == 1);
    ts_ui_select_panel(&ui, TS_UI_PANEL_DSP);
    CHECK(ts_ui_panel(&ui) == TS_UI_PANEL_DSP && ui.show_ingredients);
    ts_ui_select_panel(&ui, TS_UI_PANEL_CDP);
    CHECK(ts_ui_panel(&ui) == TS_UI_PANEL_CDP && ui.cdp_page == 1);
    ts_ui_select_panel(&ui, TS_UI_PANEL_KEYBOARD);
    ts_ui_cycle_panel(&ui);
    CHECK(!ui.show_keyboard && !ui.show_recipes && !ui.show_ingredients);
    ts_ui_cycle_panel(&ui);
    CHECK(!ui.show_keyboard && ui.show_recipes && !ui.show_ingredients);
    ts_ui_cycle_panel(&ui);
    CHECK(!ui.show_keyboard && !ui.show_recipes && ui.show_ingredients);
    ts_ui_cycle_panel(&ui);
    CHECK(ui.show_keyboard && !ui.show_recipes && !ui.show_ingredients);
    CHECK(ui.cdp_page == 1);
    CHECK(ts_ui_cdp_page_from_point(20, 315) == 0 &&
          ts_ui_cdp_page_from_point(70, 315) == 1 &&
          ts_ui_cdp_page_from_point(120, 315) == -1);

    {
        TsPalette palette;
        TsPalette reopened;
        TsPalette tapehead_reopened;
        FILE *legacy;
        ts_palette_default(&palette);
        CHECK(palette.colors[TS_PALETTE_WAVE_SELECTION] ==
              palette.colors[TS_PALETTE_BLOCK_MARK]);
        CHECK(palette.colors[TS_PALETTE_ACTIVE_TILE] ==
              palette.colors[TS_PALETTE_MOUSE]);
        ts_palette_set_component(&palette, TS_PALETTE_MOUSE, 0, 0x12);
        ts_palette_set_component(&palette, TS_PALETTE_MOUSE, 1, 0x34);
        ts_palette_set_component(&palette, TS_PALETTE_MOUSE, 2, 0x56);
        palette.colors[TS_PALETTE_WAVE_SELECTION] = 0xffabcdefu;
        palette.colors[TS_PALETTE_ACTIVE_TILE] = 0xff654321u;
        palette.desktop_contrast = 17;
        palette.buttons_contrast = 83;
        CHECK(palette.colors[TS_PALETTE_MOUSE] == 0xff123456u);
        CHECK(ts_palette_save(&palette, "test-tapesister.pal", error, sizeof(error)));
        CHECK(file_contains_text("test-tapesister.pal", "WaveSelection=#ABCDEF"));
        CHECK(file_contains_text("test-tapesister.pal", "ActiveTile=#654321"));
        ts_palette_default(&reopened);
        CHECK(ts_palette_load(&reopened, "test-tapesister.pal", error, sizeof(error)));
        CHECK(memcmp(&palette, &reopened, sizeof(palette)) == 0);
        CHECK(ts_palette_save_tapehead(&palette, "test-tapehead.pal",
                                       error, sizeof(error)));
        CHECK(!file_contains_text("test-tapehead.pal", "WaveSelection"));
        CHECK(!file_contains_text("test-tapehead.pal", "ActiveTile"));
        ts_palette_default(&tapehead_reopened);
        CHECK(ts_palette_load(&tapehead_reopened, "test-tapehead.pal",
                              error, sizeof(error)));
        CHECK(tapehead_reopened.colors[TS_PALETTE_WAVE_SELECTION] ==
              palette.colors[TS_PALETTE_BLOCK_MARK]);
        CHECK(tapehead_reopened.colors[TS_PALETTE_ACTIVE_TILE] ==
              palette.colors[TS_PALETTE_MOUSE]);
        for (int color = 0; color < TS_PALETTE_WAVE_SELECTION; ++color)
            CHECK(tapehead_reopened.colors[color] == palette.colors[color]);
        CHECK(tapehead_reopened.desktop_contrast == palette.desktop_contrast);
        CHECK(tapehead_reopened.buttons_contrast == palette.buttons_contrast);
        legacy = fopen("test-tapehead-legacy.pal", "wb");
        CHECK(legacy != NULL);
        if (legacy != NULL) {
            CHECK(fputs("[TapeheadPalette]\n"
                        "PatternText=#102030\nBlockMark=#203040\n"
                        "TextOnBlock=#304050\nMouse=#405060\n"
                        "Desktop=#506070\nButtons=#607080\n",
                        legacy) >= 0);
            CHECK(fclose(legacy) == 0);
        }
        CHECK(ts_palette_load(&reopened, "test-tapehead-legacy.pal",
                              error, sizeof(error)));
        CHECK(reopened.colors[TS_PALETTE_PATTERN_NOTE] == 0xff102030u);
        CHECK(reopened.colors[TS_PALETTE_PATTERN_EMPTY] == 0xff102030u);
        CHECK(reopened.colors[TS_PALETTE_WAVE_SELECTION] == 0xff203040u);
        CHECK(reopened.colors[TS_PALETTE_ACTIVE_TILE] == 0xff405060u);
        CHECK(strcmp(ts_palette_color_name(TS_PALETTE_PATTERN_INSTRUMENT),
                     "PRIMARY") == 0);
        CHECK(strcmp(ts_palette_color_name(TS_PALETTE_ACTIVE_TILE),
                     "ACTIVE TILE") == 0);
        remove("test-tapesister.pal");
        remove("test-tapehead.pal");
        remove("test-tapehead-legacy.pal");
    }

    {
        TsPalette before;
        int value = -1;
        for (int color = 0; color < TS_PALETTE_COLOR_COUNT; ++color) {
            int column = color % TS_PALETTE_SWATCH_COLUMNS;
            int row = color / TS_PALETTE_SWATCH_COLUMNS;
            int x = TS_PALETTE_SWATCH_X + column * TS_PALETTE_SWATCH_STEP_X +
                    TS_PALETTE_SWATCH_W / 2;
            int y = TS_PALETTE_SWATCH_Y + row * TS_PALETTE_SWATCH_STEP_Y +
                    TS_PALETTE_SWATCH_H / 2;
            CHECK(ts_ui_palette_entry_from_point(x, y) == color);
        }
        CHECK(ts_ui_palette_entry_from_point(610, 90) == -1);
        CHECK(ts_ui_palette_channel_from_point(74, 108, &value) == 0 && value == 0);
        CHECK(ts_ui_palette_channel_from_point(215, 108, &value) == 0 && value == 255);
        CHECK(ts_ui_palette_channel_from_point(304, 124, &value) == 4 && value == 1);
        CHECK(ts_ui_palette_channel_from_point(395, 124, &value) == 4 && value == 100);
        CHECK(ts_ui_palette_channel_from_point(50, 210, &value) == -1);
        CHECK(ts_ui_palette_action_from_point(59, 185) ==
              TS_UI_PALETTE_ACTION_IMPORT_TAPEHEAD);
        CHECK(ts_ui_palette_action_from_point(137, 185) ==
              TS_UI_PALETTE_ACTION_SAVE_TAPESISTER);
        CHECK(ts_ui_palette_action_from_point(215, 185) ==
              TS_UI_PALETTE_ACTION_EXPORT_TAPEHEAD);
        CHECK(ts_ui_palette_action_from_point(289, 185) ==
              TS_UI_PALETTE_ACTION_RESET);
        CHECK(ts_ui_palette_action_from_point(352, 185) ==
              TS_UI_PALETTE_ACTION_DONE);
        CHECK(ts_ui_palette_action_from_point(417, 185) ==
              TS_UI_PALETTE_ACTION_CANCEL);
        CHECK(ts_ui_palette_action_from_point(50, 205) == TS_UI_PALETTE_ACTION_NONE);
        CHECK(ts_ui_palette_cycle_entry(0, -1) == TS_PALETTE_COLOR_COUNT - 1);
        CHECK(ts_ui_palette_cycle_entry(TS_PALETTE_COLOR_COUNT - 1, 1) == 0);
        CHECK(ts_ui_palette_cycle_channel(0, -1) == 4);
        CHECK(ts_ui_palette_cycle_channel(4, 1) == 0);
        CHECK(ts_ui_config_cycle_field(TS_CONFIG_SAMPLE_PATH, -1) ==
              TS_CONFIG_CDP_BIN_PATH);
        CHECK(ts_ui_config_cycle_field(TS_CONFIG_CDP_BIN_PATH, 1) ==
              TS_CONFIG_SAMPLE_PATH);
        for (int field = 0; field < TS_CONFIG_FIELD_COUNT; ++field) {
            int y = TS_CONFIG_FIELD_Y + field * TS_CONFIG_FIELD_STEP_Y +
                    TS_CONFIG_FIELD_H / 2;
            CHECK(ts_ui_config_field_from_point(TS_CONFIG_FIELD_X + 20, y) == field);
        }
        CHECK(ts_ui_config_field_from_point(50, 205) == -1);
        CHECK(ts_ui_config_action_from_point(68, 185) == TS_UI_CONFIG_ACTION_SAVE);
        CHECK(ts_ui_config_action_from_point(159, 185) == TS_UI_CONFIG_ACTION_USE_CWD);
        CHECK(ts_ui_config_action_from_point(240, 185) == TS_UI_CONFIG_ACTION_PALETTE);
        CHECK(ts_ui_config_action_from_point(317, 185) == TS_UI_CONFIG_ACTION_CANCEL);
        CHECK(ts_ui_config_action_from_point(50, 205) == TS_UI_CONFIG_ACTION_NONE);

        ts_ui_init(&ui);
        snprintf(ui.config.sample_path, sizeof(ui.config.sample_path), "ABCDE");
        ui.config_field = TS_CONFIG_SAMPLE_PATH;
        ui.config_cursor = 5;
        CHECK(ts_ui_config_cursor_from_point(&ui, TS_CONFIG_SAMPLE_PATH,
                                             TS_CONFIG_FIELD_X + 6 + 12) == 2u);
        before = ui.palette;
        ts_ui_begin_palette_edit(&ui);
        CHECK(ui.palette_open && !ui.config_open);
        ui.palette.colors[TS_PALETTE_WAVE_SELECTION] = 0xff010203u;
        ui.palette.colors[TS_PALETTE_ACTIVE_TILE] = 0xff101112u;
        ts_ui_finish_palette_edit(&ui, 1);
        CHECK(!ui.palette_open && ui.config_open);
        CHECK(memcmp(&ui.palette, &before, sizeof(before)) == 0);
        ts_ui_begin_palette_edit(&ui);
        ui.palette.colors[TS_PALETTE_WAVE_SELECTION] = 0xff040506u;
        ui.palette.colors[TS_PALETTE_ACTIVE_TILE] = 0xff131415u;
        ts_ui_finish_palette_edit(&ui, 0);
        CHECK(ui.palette.colors[TS_PALETTE_WAVE_SELECTION] == 0xff040506u);
        CHECK(ui.palette.colors[TS_PALETTE_ACTIVE_TILE] == 0xff131415u);
    }

    {
        static const struct {
            TsFxPage page;
            int x;
            TsUiSlider slider;
        } sliders[] = {
            {TS_FX_TUNE, 250, TS_UI_SLIDER_TUNE_FINE},
            {TS_FX_TUNE, 420, TS_UI_SLIDER_TUNE_REFERENCE_VOLUME},
            {TS_FX_NOISE, 180, TS_UI_SLIDER_NOISE_AMOUNT},
            {TS_FX_SHAPE, 120, TS_UI_SLIDER_FILTER_CUTOFF},
            {TS_FX_SHAPE, 230, TS_UI_SLIDER_FILTER_RESONANCE},
            {TS_FX_SHAPE, 420, TS_UI_SLIDER_SHAPER_DRIVE},
            {TS_FX_SHAPE, 520, TS_UI_SLIDER_SHAPER_MIX},
            {TS_FX_FAMILY, 300, TS_UI_SLIDER_VARIATION_RANGE},
            {TS_FX_DELAY, 150, TS_UI_SLIDER_DELAY_TIME},
            {TS_FX_DELAY, 250, TS_UI_SLIDER_DELAY_FEEDBACK},
            {TS_FX_DELAY, 350, TS_UI_SLIDER_DELAY_DAMPING},
            {TS_FX_DELAY, 450, TS_UI_SLIDER_DELAY_MIX},
            {TS_FX_SPACE, 150, TS_UI_SLIDER_REVERB_DECAY},
            {TS_FX_SPACE, 300, TS_UI_SLIDER_REVERB_DAMPING},
            {TS_FX_SPACE, 420, TS_UI_SLIDER_REVERB_MIX},
            {TS_FX_LOOP, 450, TS_UI_SLIDER_LOOP_CROSSFADE}
        };
        ts_ui_init(&ui);
        CHECK(ts_ui_slider_from_point(&ui, 40, 244) == TS_UI_SLIDER_BODY);
        CHECK(ts_ui_slider_from_point(&ui, 110, 244) == TS_UI_SLIDER_EDGE);
        CHECK(ts_ui_slider_from_point(&ui, 190, 244) == TS_UI_SLIDER_DRIFT);
        CHECK(ts_ui_slider_from_point(&ui, 280, 244) == TS_UI_SLIDER_NONE);
        CHECK(ts_ui_slider_from_point(&ui, 450, 216) == TS_UI_SLIDER_NONE);
        CHECK(ts_ui_slider_from_point(&ui, 550, 216) == TS_UI_SLIDER_NONE);
        for (size_t i = 0; i < sizeof(sliders) / sizeof(sliders[0]); ++i) {
            ui.fx_page = sliders[i].page;
            CHECK(ts_ui_slider_from_point(&ui, sliders[i].x, 272) ==
                  sliders[i].slider);
        }
        ui.fx_page = TS_FX_EDIT;
        CHECK(ts_ui_slider_from_point(&ui, 300, 272) == TS_UI_SLIDER_NONE);
    }

    TsGeneratorRecipe first = generator(0x54415045u, TS_GENERATOR_TONAL);
    CHECK(ts_sample_generate(&a, &first, error, sizeof(error)));
    CHECK(ts_sample_generate(&b, &first, error, sizeof(error)));
    CHECK(a.frames == 11025);
    CHECK(a.sample_rate == 44100);
    CHECK(ts_sample_hash(&a) == ts_sample_hash(&b));
    CHECK(ts_sample_peak(&a) > 0.1f && ts_sample_peak(&a) <= 1.0f);
    {
        TsSample fm_a, fm_b, fm_other;
        TsGeneratorRecipe fm_recipe = generator(0x464d0001u, TS_GENERATOR_FM);
        TsFmPatch patch;
        ts_sample_init(&fm_a);
        ts_sample_init(&fm_b);
        ts_sample_init(&fm_other);
        ts_fm_patch_from_recipe(&fm_recipe, &patch);
        CHECK(patch.structure >= 0 && patch.structure < TS_FM_STRUCTURE_COUNT);
        CHECK(patch.ratio_family >= 0 &&
              patch.ratio_family < TS_FM_RATIO_FAMILY_COUNT);
        CHECK(patch.depth >= 0.8f && patch.depth <= 8.0f);
        CHECK(strcmp(ts_fm_structure_name(patch.structure), "UNKNOWN") != 0);
        CHECK(strcmp(ts_fm_ratio_family_name(patch.ratio_family), "UNKNOWN") != 0);
        CHECK(ts_sample_generate(&fm_a, &fm_recipe, error, sizeof(error)));
        CHECK(ts_sample_generate(&fm_b, &fm_recipe, error, sizeof(error)));
        CHECK(ts_sample_hash(&fm_a) == ts_sample_hash(&fm_b));
        CHECK(ts_sample_peak(&fm_a) > 0.05f && ts_sample_peak(&fm_a) <= 1.0f);
        CHECK(strncmp(fm_a.name, "FM ", 3) == 0);
        fm_recipe.seed = 0x464d0002u;
        CHECK(ts_sample_generate(&fm_other, &fm_recipe, error, sizeof(error)));
        CHECK(ts_sample_hash(&fm_other) != ts_sample_hash(&fm_a));
        for (size_t i = 0; i < fm_a.frames; ++i) CHECK(isfinite(fm_a.data[i]));
        ts_sample_free(&fm_a);
        ts_sample_free(&fm_b);
        ts_sample_free(&fm_other);
    }
    {
        TsInstrument fm_instrument, fm_restored;
        uint64_t fm_hash;
        ts_instrument_init(&fm_instrument);
        ts_instrument_init(&fm_restored);
        CHECK(ts_instrument_generate(&fm_instrument, TS_GENERATOR_FM,
                                     0x464d5453u, error, sizeof(error)));
        fm_hash = ts_sample_hash(&fm_instrument.parent);
        CHECK(fm_instrument.generator.kind == TS_GENERATOR_FM);
        CHECK(ts_instrument_save_recipe(&fm_instrument, "test-fm.tsr",
                                        error, sizeof(error)));
        CHECK(ts_instrument_load_recipe(&fm_restored, "test-fm.tsr",
                                        error, sizeof(error)));
        CHECK(fm_restored.generator.kind == TS_GENERATOR_FM);
        CHECK(ts_sample_hash(&fm_restored.parent) == fm_hash);
        CHECK(ts_sample_hash(&fm_restored.bank[0].sample) == fm_hash);
        remove("test-fm.tsr");
        ts_instrument_free(&fm_instrument);
        ts_instrument_free(&fm_restored);
    }
    {
        static const TsGeneratorKind expected[] = {
            TS_GENERATOR_METALLIC, TS_GENERATOR_NOISE,
            TS_GENERATOR_PULSE, TS_GENERATOR_FM
        };
        TsInstrument cycle;
        int slot = -1;
        ts_instrument_init(&cycle);
        CHECK(ts_instrument_generate(&cycle, TS_GENERATOR_TONAL,
                                     0x4359434cu, error, sizeof(error)));
        cycle.family_relation = TS_FAMILY_STRANGER;
        cycle.family_trajectory = 1;
        for (int i = 0; i < 4; ++i) {
            CHECK(ts_instrument_generate_family_candidate(&cycle, 0, 0, &slot,
                                                           error, sizeof(error)));
            CHECK(slot == i + 1);
            CHECK(cycle.bank[slot].has_generator);
            CHECK(cycle.bank[slot].generator.kind == expected[i]);
            CHECK(strncmp(cycle.bank[slot].sample.name,
                          ts_generator_name(expected[i]),
                          strlen(ts_generator_name(expected[i]))) == 0);
        }
        CHECK(cycle.bank[slot].generator.kind == TS_GENERATOR_FM);
        CHECK(strncmp(cycle.bank[slot].sample.name, "FM ", 3) == 0);
        ts_instrument_free(&cycle);
    }
    {
        TsInstrument editor;
        TsInstrument reopened;
        TsSample clipboard;
        TsSample target_before;
        TsSample stamp_before;
        size_t origin = 0;
        size_t original_frames;
        uint64_t original_hash;
        uint64_t create_hash;
        uint64_t vary_hash;
        ts_instrument_init(&editor);
        ts_instrument_init(&reopened);
        ts_sample_init(&clipboard);
        ts_sample_init(&target_before);
        ts_sample_init(&stamp_before);

        CHECK(ts_instrument_generate(&editor, TS_GENERATOR_FM,
                                     0x434c4950u, error, sizeof(error)));
        CHECK(ts_instrument_bank_capture(&editor, 1, TS_BANK_CAPTURE_CURRENT,
                                         error, sizeof(error)));
        ts_instrument_set_selection(&editor, 100, 400);
        CHECK(ts_instrument_copy_selection(&editor, &clipboard, &origin,
                                            error, sizeof(error)));
        CHECK(origin == 100 && clipboard.frames == 300);
        CHECK(memcmp(clipboard.data, editor.current.data + 100,
                     clipboard.frames * sizeof(*clipboard.data)) == 0);

        CHECK(ts_instrument_select_bank(&editor, 1, error, sizeof(error)));
        CHECK(ts_sample_clone(&target_before, &editor.current, error, sizeof(error)));
        original_frames = editor.current.frames;
        original_hash = ts_sample_hash(&editor.current);

        /* Exact Paste replaces a shorter target and grows the tile. */
        ts_instrument_set_selection(&editor, 800, 950);
        CHECK(ts_instrument_paste(&editor, &clipboard, origin, 0,
                                  error, sizeof(error)));
        CHECK(editor.current.frames == original_frames + 150);
        CHECK(editor.selection_first == 800 && editor.selection_last == 1100);
        CHECK(memcmp(editor.current.data + 800, clipboard.data,
                     clipboard.frames * sizeof(*clipboard.data)) == 0);
        CHECK(ts_instrument_undo(&editor, error, sizeof(error)));
        CHECK(editor.current.frames == original_frames &&
              ts_sample_hash(&editor.current) == original_hash);
        CHECK(ts_instrument_redo(&editor, error, sizeof(error)));
        CHECK(editor.current.frames == original_frames + 150);
        CHECK(ts_instrument_undo(&editor, error, sizeof(error)));

        /* Exact Paste replaces a longer target and shrinks the tile. */
        ts_instrument_set_selection(&editor, 800, 1400);
        CHECK(ts_instrument_paste(&editor, &clipboard, origin, 0,
                                  error, sizeof(error)));
        CHECK(editor.current.frames == original_frames - 300);
        CHECK(editor.selection_first == 800 && editor.selection_last == 1100);
        CHECK(ts_instrument_save_recipe(&editor, "test-exact-paste.tsr",
                                        error, sizeof(error)));
        CHECK(ts_instrument_load_recipe(&reopened, "test-exact-paste.tsr",
                                        error, sizeof(error)));
        CHECK(reopened.current.frames == original_frames - 300 &&
              ts_sample_hash(&reopened.current) == ts_sample_hash(&editor.current));
        CHECK(ts_instrument_undo(&reopened, error, sizeof(error)));
        CHECK(reopened.current.frames == original_frames &&
              ts_sample_hash(&reopened.current) == original_hash);
        CHECK(ts_instrument_redo(&reopened, error, sizeof(error)));
        CHECK(reopened.current.frames == original_frames - 300);
        ts_instrument_free(&reopened);
        ts_instrument_init(&reopened);
        remove("test-exact-paste.tsr");
        CHECK(ts_instrument_undo(&editor, error, sizeof(error)));
        CHECK(editor.current.frames == original_frames &&
              ts_sample_hash(&editor.current) == original_hash);

        /* Fit Paste stretches into the target without changing tile length. */
        ts_instrument_set_selection(&editor, 800, 1400);
        CHECK(ts_instrument_paste(&editor, &clipboard, origin, 1,
                                  error, sizeof(error)));
        CHECK(editor.current.frames == original_frames);
        CHECK(editor.selection_first == 800 && editor.selection_last == 1400);
        CHECK(samples_equal_outside(&editor.current, &target_before, 800, 1400));
        CHECK(ts_sample_hash(&editor.current) != original_hash);
        CHECK(ts_instrument_save_recipe(&editor, "test-clipboard.tsr",
                                        error, sizeof(error)));
        CHECK(ts_instrument_load_recipe(&reopened, "test-clipboard.tsr",
                                        error, sizeof(error)));
        CHECK(ts_sample_hash(&reopened.current) == ts_sample_hash(&editor.current));
        CHECK(reopened.bank[1].patch_count == editor.bank[1].patch_count);
        CHECK(ts_instrument_undo(&reopened, error, sizeof(error)));
        CHECK(ts_sample_hash(&reopened.current) == original_hash);
        CHECK(ts_instrument_redo(&reopened, error, sizeof(error)));
        CHECK(ts_sample_hash(&reopened.current) == ts_sample_hash(&editor.current));
        remove("test-clipboard.tsr");

        /* Each tile remembers its own selection; Ripple Cut preserves the canvas. */
        CHECK(ts_instrument_select_bank(&editor, 0, error, sizeof(error)));
        CHECK(editor.selection_first == 100 && editor.selection_last == 400);
        original_frames = editor.current.frames;
        original_hash = ts_sample_hash(&editor.current);
        CHECK(ts_instrument_cut_selection(&editor, &clipboard, &origin,
                                           error, sizeof(error)));
        CHECK(origin == 100 && clipboard.frames == 300);
        CHECK(editor.current.frames == original_frames && !editor.has_selection);
        for (size_t frame = original_frames - 300; frame < original_frames; ++frame)
            CHECK(editor.current.data[frame] == 0.0f);
        CHECK(ts_instrument_undo(&editor, error, sizeof(error)));
        CHECK(editor.current.frames == original_frames &&
              ts_sample_hash(&editor.current) == original_hash);
        CHECK(ts_instrument_redo(&editor, error, sizeof(error)));
        CHECK(editor.current.frames == original_frames);
        edited_hash = ts_sample_hash(&editor.current);
        CHECK(editor.post_edit_count >= 2 &&
              editor.post_edits[editor.post_edit_count - 2].kind ==
                  TS_POST_DELETE &&
              editor.post_edits[editor.post_edit_count - 1].kind ==
                  TS_POST_CANVAS_RIGHT_RESIZE);
        CHECK(fabsf(editor.current.data[origin - 1u]) < 0.000001f &&
              fabsf(editor.current.data[origin]) < 0.000001f);
        {
            TsProcessRecipe ripple_process = editor.process;
            ripple_process.body = 0.83f;
            ripple_process.edge = 0.71f;
            ripple_process.drift = 0.61f;
            CHECK(ts_instrument_set_process(&editor, &ripple_process,
                                            error, sizeof(error)));
            CHECK(editor.current.frames == original_frames &&
                  ts_sample_hash(&editor.current) != edited_hash &&
                  editor.post_edit_count >= 2 &&
                  editor.post_edits[editor.post_edit_count - 2].kind ==
                      TS_POST_DELETE);
            CHECK(ts_instrument_undo(&editor, error, sizeof(error)));
            CHECK(ts_sample_hash(&editor.current) == edited_hash);
        }
        CHECK(ts_instrument_save_recipe(&editor, "test-ripple-cut.tsr",
                                        error, sizeof(error)));
        ts_instrument_free(&reopened);
        ts_instrument_init(&reopened);
        CHECK(ts_instrument_load_recipe(&reopened, "test-ripple-cut.tsr",
                                        error, sizeof(error)));
        CHECK(reopened.current.frames == original_frames &&
              ts_sample_hash(&reopened.current) == edited_hash);
        CHECK(ts_instrument_undo(&reopened, error, sizeof(error)));
        CHECK(reopened.current.frames == original_frames &&
              ts_sample_hash(&reopened.current) == original_hash);
        CHECK(ts_instrument_redo(&reopened, error, sizeof(error)));
        CHECK(ts_sample_hash(&reopened.current) == edited_hash);
        remove("test-ripple-cut.tsr");
        CHECK(ts_instrument_undo(&editor, error, sizeof(error)));

        /* The INI-controlled compatibility mode crops the canvas to the ripple. */
        CHECK(ts_instrument_cut_selection_mode(&editor, &clipboard, &origin, 1,
                                                error, sizeof(error)));
        CHECK(editor.current.frames == original_frames - 300 &&
              !editor.has_selection);
        CHECK(ts_instrument_undo(&editor, error, sizeof(error)));
        CHECK(editor.current.frames == original_frames &&
              ts_sample_hash(&editor.current) == original_hash);

        /* With no target range, Paste overwrites at the original position. */
        CHECK(ts_sample_clone(&stamp_before, &editor.current, error, sizeof(error)));
        ts_instrument_clear_selection(&editor);
        CHECK(ts_instrument_paste(&editor, &clipboard, origin, 0,
                                  error, sizeof(error)));
        CHECK(editor.current.frames == original_frames);
        CHECK(editor.selection_first == origin &&
              editor.selection_last == origin + clipboard.frames);
        CHECK(memcmp(editor.current.data + origin, clipboard.data,
                     clipboard.frames * sizeof(*clipboard.data)) == 0);
        CHECK(samples_equal_outside(&editor.current, &stamp_before, origin,
                                    origin + clipboard.frames));
        CHECK(ts_instrument_undo(&editor, error, sizeof(error)));
        CHECK(ts_sample_hash(&editor.current) == original_hash);

        /* An empty tile can become a silent, timeline-preserving paste canvas. */
        CHECK(ts_instrument_select_bank(&editor, 2, error, sizeof(error)));
        CHECK(!editor.bank[2].occupied);
        CHECK(ts_instrument_activate_silence(&editor, original_frames,
                                             clipboard.sample_rate,
                                             error, sizeof(error)));
        CHECK(editor.bank[2].occupied && editor.current.frames == original_frames &&
              editor.current.sample_rate == clipboard.sample_rate &&
              strcmp(editor.current.name, "SILENCE") == 0);
        for (size_t frame = 0; frame < editor.current.frames; ++frame)
            CHECK(editor.current.data[frame] == 0.0f);
        original_hash = ts_sample_hash(&editor.current);
        CHECK(ts_instrument_paste(&editor, &clipboard, origin, 0,
                                  error, sizeof(error)));
        CHECK(editor.current.frames == original_frames);
        CHECK(memcmp(editor.current.data + origin, clipboard.data,
                     clipboard.frames * sizeof(*clipboard.data)) == 0);
        create_hash = ts_sample_hash(&editor.current);
        CHECK(ts_instrument_save_recipe(&editor, "test-silent-paste.tsr",
                                        error, sizeof(error)));
        CHECK(ts_instrument_load_recipe(&reopened, "test-silent-paste.tsr",
                                        error, sizeof(error)));
        CHECK(reopened.selected_slot == 2 &&
              ts_sample_hash(&reopened.current) == create_hash);
        CHECK(ts_instrument_undo(&reopened, error, sizeof(error)));
        CHECK(ts_sample_hash(&reopened.current) == original_hash);
        CHECK(ts_instrument_redo(&reopened, error, sizeof(error)));
        CHECK(ts_sample_hash(&reopened.current) == create_hash);
        remove("test-silent-paste.tsr");
        CHECK(ts_instrument_undo(&editor, error, sizeof(error)));
        ts_instrument_clear_selection(&editor);
        CHECK(ts_instrument_paste(&editor, &clipboard, original_frames + 100u, 0,
                                  error, sizeof(error)));
        CHECK(editor.current.frames == original_frames + 100u + clipboard.frames);
        for (size_t frame = original_frames; frame < original_frames + 100u; ++frame)
            CHECK(editor.current.data[frame] == 0.0f);
        CHECK(memcmp(editor.current.data + original_frames + 100u, clipboard.data,
                     clipboard.frames * sizeof(*clipboard.data)) == 0);
        CHECK(ts_instrument_undo(&editor, error, sizeof(error)));
        CHECK(editor.current.frames == original_frames &&
              ts_sample_hash(&editor.current) == original_hash);
        CHECK(ts_instrument_select_bank(&editor, 0, error, sizeof(error)));

        /* Create and Vary sculpt only the active selection and survive TSR16. */
        ts_instrument_set_selection(&editor, 1000, 1500);
        CHECK(ts_sample_clone(&stamp_before, &editor.current, error, sizeof(error)));
        CHECK(ts_instrument_stamp_create(&editor, 0x5354414du,
                                         error, sizeof(error)));
        CHECK(editor.current.frames == stamp_before.frames);
        CHECK(samples_equal_outside(&editor.current, &stamp_before, 1000, 1500));
        create_hash = ts_sample_hash(&editor.current);
        CHECK(create_hash != ts_sample_hash(&stamp_before));
        CHECK(ts_instrument_undo(&editor, error, sizeof(error)));
        CHECK(ts_sample_hash(&editor.current) == ts_sample_hash(&stamp_before));
        CHECK(ts_instrument_redo(&editor, error, sizeof(error)));
        CHECK(ts_sample_hash(&editor.current) == create_hash);
        CHECK(ts_instrument_stamp_vary(&editor, error, sizeof(error)));
        vary_hash = ts_sample_hash(&editor.current);
        CHECK(vary_hash != create_hash);
        CHECK(samples_equal_outside(&editor.current, &stamp_before, 1000, 1500));
        CHECK(ts_instrument_save_recipe(&editor, "test-stamps.tsr",
                                        error, sizeof(error)));
        ts_instrument_free(&reopened);
        ts_instrument_init(&reopened);
        CHECK(ts_instrument_load_recipe(&reopened, "test-stamps.tsr",
                                        error, sizeof(error)));
        CHECK(ts_sample_hash(&reopened.current) == vary_hash);
        CHECK(ts_instrument_undo(&reopened, error, sizeof(error)));
        CHECK(ts_sample_hash(&reopened.current) == create_hash);
        CHECK(ts_instrument_redo(&reopened, error, sizeof(error)));
        CHECK(ts_sample_hash(&reopened.current) == vary_hash);
        remove("test-stamps.tsr");

        ts_sample_free(&clipboard);
        ts_sample_free(&target_before);
        ts_sample_free(&stamp_before);
        ts_instrument_free(&editor);
        ts_instrument_free(&reopened);
    }
    CHECK(ts_sample_clone(&copy, &a, error, sizeof(error)));
    CHECK(ts_sample_hash(&copy) == ts_sample_hash(&a));
    {
        const uint32_t seeds[4] = {0x10203040u, 0x55667788u,
                                   0x89abcdefu, 0xfedcba98u};
        for (int kind = 0; kind < TS_GENERATOR_COUNT; ++kind) {
            TsSample variants[4];
            uint64_t hashes[4];
            for (int i = 0; i < 4; ++i) {
                TsGeneratorRecipe varied = generator(seeds[i], (TsGeneratorKind)kind);
                ts_sample_init(&variants[i]);
                CHECK(ts_sample_generate(&variants[i], &varied, error, sizeof(error)));
                hashes[i] = ts_sample_hash(&variants[i]);
                CHECK(kind == TS_GENERATOR_FM ?
                      strncmp(variants[i].name, "FM ", 3) == 0 :
                      strstr(variants[i].name, " V") != NULL);
            }
            for (int i = 0; i < 4; ++i)
                for (int j = i + 1; j < 4; ++j) CHECK(hashes[i] != hashes[j]);
            for (int i = 0; i < 4; ++i) ts_sample_free(&variants[i]);
        }
    }

    {
        float crossings[] = {0.8f, 0.5f, -0.2f, -0.4f, 0.1f};
        float no_crossings[] = {0.8f, 0.2f, 0.5f};
        TsSample crossing_sample = {crossings, 5, 44100, "crossings", 0u};
        TsSample fallback_sample = {no_crossings, 3, 44100, "fallback", 0u};
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
        ts_instrument_set_selection_snapped(&snap_instrument, 0,
                                             crossing_sample.frames);
        CHECK(snap_instrument.selection_first == 0 &&
              snap_instrument.selection_last == crossing_sample.frames);
        CHECK(ts_instrument_select_all(&snap_instrument));
        CHECK(snap_instrument.selection_first == 0 &&
              snap_instrument.selection_last == crossing_sample.frames);
        ts_instrument_clear_selection(&snap_instrument);
        CHECK(ts_instrument_set_loop_from_selection(&snap_instrument,
                                                     error, sizeof(error)));
        CHECK(snap_instrument.has_selection && snap_instrument.has_loop);
        CHECK(snap_instrument.selection_first == 0 &&
              snap_instrument.selection_last == crossing_sample.frames);
        CHECK(snap_instrument.loop_first == 0 &&
              snap_instrument.loop_last == crossing_sample.frames);
        {
            float margins[] = {0.0f, 0.0f, 0.25f, -0.40f, 0.10f, 0.0f, 0.0f};
            float silent[7] = {0};
            TsInstrument wave = {0};
            TsInstrument empty_wave = {0};
            wave.current.data = margins;
            wave.current.frames = 7;
            wave.current.sample_rate = 44100;
            CHECK(ts_instrument_select_wave(&wave));
            CHECK(wave.selection_first == 2 && wave.selection_last == 5);
            CHECK(wave.selection_last - wave.selection_first == 3);
            CHECK(ts_instrument_select_all(&wave));
            CHECK(wave.selection_first == 0 && wave.selection_last == 7);
            empty_wave.current.data = silent;
            empty_wave.current.frames = 7;
            empty_wave.current.sample_rate = 44100;
            CHECK(!ts_instrument_select_wave(&empty_wave));
            CHECK(!empty_wave.has_selection);
        }
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

    {
        TsPortableRecipe portable;
        TsPortableRecipe reopened;
        TsSample filtered, saturated, folded;
        TsProcessRecipe shaped;
        TsTuning recipe_tuning = {57, -12.5f};
        uint64_t neutral_hash;
        char recipe_error[160];
        ts_sample_init(&filtered); ts_sample_init(&saturated); ts_sample_init(&folded);
        CHECK(recipe_bank.slots[0].occupied && recipe_bank.slots[0].factory);
        CHECK(strcmp(recipe_bank.slots[0].name, "NEUTRAL") == 0);
        CHECK(recipe_bank.slots[7].occupied && recipe_bank.slots[7].factory);
        CHECK(!ts_recipe_bank_capture(&recipe_bank, 0, &neutral, &recipe_tuning,
                                      &recipe_tuning, "NO",
                                      recipe_error, sizeof(recipe_error)));
        CHECK(ts_recipe_bank_capture(&recipe_bank, 8, &dsp, &recipe_tuning,
                                     &recipe_tuning, "MY TEXTURE",
                                     recipe_error, sizeof(recipe_error)));
        CHECK(!ts_recipe_bank_capture(&recipe_bank, 8, &neutral, &recipe_tuning,
                                      &recipe_tuning, "OVERWRITE",
                                      recipe_error, sizeof(recipe_error)));
        CHECK(!ts_recipe_bank_rename(&recipe_bank, 0, "RENAMED FACTORY",
                                     recipe_error, sizeof(recipe_error)));
        CHECK(ts_recipe_bank_rename(&recipe_bank, 8, "  DRONE BED  ",
                                    recipe_error, sizeof(recipe_error)));
        CHECK(strcmp(recipe_bank.slots[8].name, "DRONE BED") == 0);
        CHECK(ts_recipe_from_process_and_tuning(&portable, &dsp, &recipe_tuning,
                                                "PORTABLE TEXTURE"));
        CHECK(ts_recipe_save(&portable, "test-portable.tsp",
                             recipe_error, sizeof(recipe_error)));
        CHECK(ts_recipe_load(&reopened, "test-portable.tsp",
                             recipe_error, sizeof(recipe_error)));
        CHECK(strcmp(reopened.name, "PORTABLE TEXTURE") == 0);
        CHECK(memcmp(&reopened.process, &portable.process,
                     sizeof(portable.process)) == 0);
        CHECK(reopened.has_tuning && reopened.tuning.root_note == 57 &&
              fabsf(reopened.tuning.fine_tune_cents + 12.5f) < 0.001f);
        {
            TsPortableRecipe guarded = reopened;
            FILE *broken = fopen("test-portable-broken.tsp", "wb");
            CHECK(broken != NULL);
            if (broken != NULL) {
                CHECK(fwrite("TSP", 1, 3, broken) == 3);
                CHECK(fclose(broken) == 0);
            }
            CHECK(!ts_recipe_load(&guarded, "test-portable-broken.tsp",
                                  recipe_error, sizeof(recipe_error)));
            CHECK(memcmp(&guarded, &reopened, sizeof(guarded)) == 0);
            remove("test-portable-broken.tsp");
        }
        CHECK(ts_recipe_bank_add_user(&recipe_bank, &reopened,
                                      recipe_error, sizeof(recipe_error)) == 10);
        CHECK(ts_recipe_bank_clear(&recipe_bank, 8, recipe_error, sizeof(recipe_error)));
        CHECK(!recipe_bank.slots[8].occupied);

        neutral_hash = ts_sample_hash(&dry);
        shaped = neutral;
        shaped.filter_enabled = 1;
        shaped.filter_mode = TS_FILTER_LOWPASS;
        shaped.filter_cutoff_hz = 720.0f;
        shaped.filter_resonance = 0.55f;
        CHECK(ts_sample_process(&filtered, &a, 0, a.frames, &shaped,
                                recipe_error, sizeof(recipe_error)));
        CHECK(ts_sample_hash(&filtered) != neutral_hash);
        shaped = neutral;
        shaped.shaper_enabled = 1;
        shaped.shaper_mode = TS_SHAPER_TAPE;
        shaped.shaper_drive = 5.0f;
        shaped.shaper_mix = 0.8f;
        CHECK(ts_sample_process(&saturated, &a, 0, a.frames, &shaped,
                                recipe_error, sizeof(recipe_error)));
        shaped.shaper_mode = TS_SHAPER_FOLD;
        CHECK(ts_sample_process(&folded, &a, 0, a.frames, &shaped,
                                recipe_error, sizeof(recipe_error)));
        CHECK(ts_sample_hash(&saturated) != neutral_hash);
        CHECK(ts_sample_hash(&folded) != ts_sample_hash(&saturated));
        CHECK(ts_filter_mode_name(TS_FILTER_BANDPASS)[0] == 'B');
        CHECK(strcmp(ts_shaper_mode_name(TS_SHAPER_CLIP), "CLIP") == 0);
        ts_sample_free(&filtered); ts_sample_free(&saturated); ts_sample_free(&folded);
        remove("test-portable.tsp");
    }

    {
        uint64_t recipe_parent;
        uint64_t recipe_before;
        size_t selected_first;
        size_t selected_last;
        CHECK(ts_instrument_generate(&recipe_target, TS_GENERATOR_PULSE, 0x52504339u,
                                     error, sizeof(error)));
        ts_instrument_set_selection_snapped(&recipe_target, 400, 2400);
        CHECK(ts_instrument_set_loop_from_selection(&recipe_target, error, sizeof(error)));
        recipe_parent = ts_sample_hash(&recipe_target.parent);
        recipe_before = ts_sample_hash(&recipe_target.current);
        selected_first = recipe_target.selection_first;
        selected_last = recipe_target.selection_last;
        CHECK(ts_instrument_set_process(&recipe_target,
                                        &recipe_bank.slots[2].process,
                                        error, sizeof(error)));
        CHECK(ts_sample_hash(&recipe_target.parent) == recipe_parent);
        CHECK(ts_sample_hash(&recipe_target.current) != recipe_before);
        CHECK(recipe_target.selection_first == selected_first &&
              recipe_target.selection_last == selected_last);
        CHECK(recipe_target.has_loop && recipe_target.loop_first == selected_first &&
              recipe_target.loop_last == selected_last);
        CHECK(ts_instrument_undo(&recipe_target, error, sizeof(error)));
        CHECK(ts_sample_hash(&recipe_target.current) == recipe_before);
        CHECK(ts_sample_hash(&recipe_target.parent) == recipe_parent);
    }

    ts_sample_free(&b);
    TsGeneratorRecipe metallic = generator(0x54415045u, TS_GENERATOR_METALLIC);
    CHECK(ts_sample_generate(&b, &metallic, error, sizeof(error)));
    CHECK(ts_sample_hash(&a) != ts_sample_hash(&b));

    CHECK(ts_sample_save_wav16(&a, "test-roundtrip.wav", error, sizeof(error)));
    CHECK(ts_sample_load_wav(&loaded, "test-roundtrip.wav", error, sizeof(error)));
    CHECK(loaded.frames == a.frames);
    CHECK(loaded.sample_rate == a.sample_rate);
    CHECK(fabsf(ts_sample_peak(&loaded) - ts_sample_peak(&a)) < 0.001f);
    {
        TsTuning written = {57, -23.25f};
        TsTuning reopened = {0, 0.0f};
        char note_name[12];
        CHECK(ts_sample_save_wav16_tuned(&a, &written, "test-tuned.wav",
                                         error, sizeof(error)));
        CHECK(ts_sample_load_wav_tuned(&loaded, &reopened, "test-tuned.wav",
                                       error, sizeof(error)));
        CHECK(reopened.root_note == written.root_note);
        CHECK(fabsf(reopened.fine_tune_cents - written.fine_tune_cents) < 0.001f);
        CHECK(fabs(ts_tuning_frequency(&(TsTuning){69, 0.0f}) - 440.0) < 0.0001);
        CHECK(fabs(ts_tuning_note_pitch(&(TsTuning){60, 0.0f}, 0) - 1.0) < 0.0001);
        CHECK(strcmp(ts_midi_note_name(60, note_name, sizeof(note_name)), "C4") == 0);
        remove("test-tuned.wav");
    }
    {
        TsTuning written = {62, 7.5f};
        for (int mode = TS_LOOP_FORWARD; mode < TS_LOOP_MODE_COUNT; ++mode) {
            TsTuning reopened = {0, 0.0f};
            TsLoopMode reopened_mode = TS_LOOP_FORWARD;
            size_t loop_first = 0, loop_last = 0;
            int has_loop = 0;
            CHECK(ts_sample_save_wav16_tuned_looped(
                &a, &written, 1, 123, 4321, (TsLoopMode)mode,
                "test-looped.wav", error, sizeof(error)));
            CHECK(ts_sample_load_wav_metadata(
                &loaded, &reopened, &has_loop, &loop_first, &loop_last,
                &reopened_mode, "test-looped.wav", error, sizeof(error)));
            CHECK(has_loop && loop_first == 123 && loop_last == 4321);
            CHECK(reopened_mode == (TsLoopMode)mode);
            CHECK(reopened.root_note == written.root_note);
            CHECK(fabsf(reopened.fine_tune_cents - written.fine_tune_cents) < 0.001f);
        }
        CHECK(ts_instrument_load_wav(&imported, "test-looped.wav",
                                     error, sizeof(error)));
        CHECK(imported.has_loop && imported.loop_first == 123 &&
              imported.loop_last == 4321 &&
              imported.loop_mode == TS_LOOP_PING_PONG);
        CHECK(imported.bank[0].has_loop && imported.bank[0].loop_first == 123);
        CHECK(imported.tuning.root_note == 60 &&
              imported.audible_tuning.root_note == 60);
        remove("test-looped.wav");
    }
    {
        TsInstrument pitch;
        TsTuning suggestion;
        float confidence = 0.0f;
        ts_instrument_init(&pitch);
        pitch.current.frames = 16384;
        pitch.current.sample_rate = 44100;
        pitch.current.data = (float *)malloc(pitch.current.frames * sizeof(float));
        CHECK(pitch.current.data != NULL);
        if (pitch.current.data != NULL) {
            for (size_t i = 0; i < pitch.current.frames; ++i)
                pitch.current.data[i] = sinf((float)(2.0 * 3.14159265358979323846 * 440.0 *
                                                      (double)i / 44100.0));
            CHECK(ts_instrument_suggest_pitch(&pitch, &suggestion, &confidence,
                                               error, sizeof(error)));
            CHECK(suggestion.root_note == 69);
            CHECK(fabsf(suggestion.fine_tune_cents) < 1.0f);
            CHECK(confidence > 0.9f);
            memset(pitch.current.data, 0, pitch.current.frames * sizeof(float));
            CHECK(!ts_instrument_suggest_pitch(&pitch, &suggestion, &confidence,
                                                error, sizeof(error)));
        }
        ts_instrument_free(&pitch);
    }

    CHECK(ts_instrument_generate(&generated, TS_GENERATOR_TONAL, 0x11223344u,
                                 error, sizeof(error)));
    CHECK(generated.source_kind == TS_SOURCE_GENERATED);
    CHECK(generated.parent.frames == generated.current.frames);
    CHECK(ts_sample_hash(&generated.parent) == ts_sample_hash(&generated.current));
    CHECK(generated.process.body == 0.5f && generated.process.edge == 0.0f &&
          generated.process.drift == 0.0f);
    CHECK(generated.tuning.root_note >= 0 && generated.tuning.root_note <= 127);
    CHECK(generated.bank[0].tuning.root_note == generated.tuning.root_note);
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
    {
        TsTuning original = generated.tuning;
        CHECK(ts_instrument_set_tuning(&generated, 64, 17.5f,
                                       error, sizeof(error)));
        CHECK(generated.tuning.root_note == 64 &&
              fabsf(generated.tuning.fine_tune_cents - 17.5f) < 0.001f);
        CHECK(ts_instrument_undo(&generated, error, sizeof(error)));
        CHECK(generated.tuning.root_note == original.root_note &&
              fabsf(generated.tuning.fine_tune_cents -
                    original.fine_tune_cents) < 0.001f);
        CHECK(ts_instrument_redo(&generated, error, sizeof(error)));
        CHECK(generated.tuning.root_note == 64);
        {
            double before_pitch = ts_tuning_note_pitch(&generated.tuning, 0);
            uint64_t before_tuning_hash = ts_sample_hash(&generated.current);
            CHECK(ts_instrument_set_audible_tuning(&generated, 65, 17.5f,
                                                    error, sizeof(error)));
            CHECK(generated.audible_tuning.root_note == 65);
            CHECK(ts_tuning_note_pitch(&generated.tuning, 0) > before_pitch);
            CHECK(ts_sample_hash(&generated.current) == before_tuning_hash);
            CHECK(fabs(ts_instrument_audition_pitch(&generated) -
                       pow(2.0, 1.0 / 12.0)) < 0.000001);
            {
                TsAuditionPlan whole_plan;
                TsAuditionPlan selection_plan;
                ts_instrument_set_selection(&generated, 100u, 1000u);
                CHECK(ts_audition_plan(&generated, TS_AUDITION_CURRENT,
                                       TS_AUDITION_ALL, &whole_plan));
                CHECK(ts_audition_plan(&generated, TS_AUDITION_CURRENT,
                                       TS_AUDITION_SELECTION, &selection_plan));
                CHECK(whole_plan.first == 0u &&
                      whole_plan.last == generated.current.frames);
                CHECK(selection_plan.first == 100u &&
                      selection_plan.last == 1000u);
                CHECK(ts_instrument_audition_pitch(&generated) ==
                      ts_tuning_pair_audition_pitch(
                          &generated.tuning, &generated.audible_tuning));
            }
            CHECK(ts_instrument_undo(&generated, error, sizeof(error)));
            CHECK(generated.audible_tuning.root_note == 64);
            CHECK(ts_sample_hash(&generated.current) == before_tuning_hash);
            CHECK(ts_instrument_redo(&generated, error, sizeof(error)));
            CHECK(generated.audible_tuning.root_note == 65);
            CHECK(ts_sample_hash(&generated.current) == before_tuning_hash);
        }
    }

    {
        TsInstrument tuned;
        size_t before_frames;
        size_t tuned_frames;
        uint64_t before_hash;
        uint64_t tuned_hash;
        ts_instrument_init(&tuned);
        CHECK(ts_instrument_generate(&tuned, TS_GENERATOR_FM, 0x4354554eu,
                                     error, sizeof(error)));
        CHECK(tuned.tuning.root_note == 60 &&
              tuned.audible_tuning.root_note == 60);
        CHECK(fabsf(tuned.generator.frequency - 261.625565f) < 0.001f);
        CHECK(ts_instrument_set_tuning(&tuned, 64, 12.0f,
                                       error, sizeof(error)));
        before_frames = tuned.current.frames;
        before_hash = ts_sample_hash(&tuned.current);
        CHECK(ts_instrument_apply_pitch_shift(&tuned, 12.0f,
                                              error, sizeof(error)));
        tuned_frames = tuned.current.frames;
        tuned_hash = ts_sample_hash(&tuned.current);
        CHECK(tuned_frames == (before_frames + 1u) / 2u);
        CHECK(tuned_hash != before_hash);
        CHECK(tuned.tuning.root_note == 60 &&
              tuned.audible_tuning.root_note == 60 &&
              tuned.tuning.fine_tune_cents == 0.0f);
        CHECK(ts_instrument_undo(&tuned, error, sizeof(error)));
        CHECK(tuned.current.frames == before_frames &&
              ts_sample_hash(&tuned.current) == before_hash);
        CHECK(tuned.tuning.root_note == 64 &&
              fabsf(tuned.tuning.fine_tune_cents - 12.0f) < 0.001f);
        CHECK(ts_instrument_redo(&tuned, error, sizeof(error)));
        CHECK(tuned.current.frames == tuned_frames &&
              ts_sample_hash(&tuned.current) == tuned_hash);
        CHECK(tuned.tuning.root_note == 60 &&
              tuned.audible_tuning.root_note == 60);
        {
            size_t selection_first = 1000u;
            size_t selection_last = 2000u;
            size_t selection_frames = selection_last - selection_first;
            size_t selection_total = tuned.current.frames;
            float prefix = tuned.current.data[500u];
            float suffix = tuned.current.data[2500u];
            ts_instrument_set_selection(&tuned, selection_first, selection_last);
            CHECK(ts_instrument_apply_pitch_shift(&tuned, -12.0f,
                                                  error, sizeof(error)));
            CHECK(tuned.current.frames == selection_total + selection_frames);
            CHECK(tuned.has_selection &&
                  tuned.selection_first == selection_first &&
                  tuned.selection_last == selection_first + 2u * selection_frames);
            CHECK(tuned.current.data[500u] == prefix);
            CHECK(tuned.current.data[3500u] == suffix);
            CHECK(ts_instrument_undo(&tuned, error, sizeof(error)));
            CHECK(tuned.current.frames == selection_total &&
                  ts_sample_hash(&tuned.current) == tuned_hash);
        }
        ts_instrument_free(&tuned);
    }

    size_t original_frames = generated.current.frames;
    ts_instrument_set_selection(&generated, 100, 1000);
    CHECK(generated.has_selection);
    CHECK(ts_instrument_zoom_selection(&generated));
    CHECK(generated.view_first == 100 && generated.view_last == 1000);
    CHECK(ts_instrument_frame_from_view_x(&generated, 0, 600) == 100);
    CHECK(ts_instrument_frame_from_view_x(&generated, 599, 600) == 1000);
    generated.view_first = generated.current.frames - 200u;
    generated.view_last = generated.current.frames;
    CHECK(ts_instrument_frame_from_view_x(&generated, 599, 600) ==
          generated.current.frames);
    ts_instrument_show_all(&generated);
    ts_instrument_set_selection_snapped(
        &generated,
        ts_instrument_frame_from_view_x(&generated, 0, 600),
        ts_instrument_frame_from_view_x(&generated, 599, 600));
    CHECK(generated.selection_first == 0 &&
          generated.selection_last == generated.current.frames);
    ts_instrument_set_selection_snapped(&generated,
                                        generated.current.frames - 100u,
                                        generated.current.frames);
    CHECK(generated.selection_last == generated.current.frames);
    CHECK(generated.selection_last - generated.selection_first > 0u);
    generated.view_first = 100;
    generated.view_last = 1000;
    ts_instrument_set_selection(&generated, 100, 1000);
    CHECK(ts_instrument_crop_selection(&generated, error, sizeof(error)));
    CHECK(generated.current.frames == 900);
    CHECK(ts_sample_hash(&generated.parent) == parent_hash);
    CHECK(!generated.has_selection);
    CHECK(generated.view_first == 0 && generated.view_last == 900);
    CHECK(ts_instrument_undo(&generated, error, sizeof(error)));
    CHECK(generated.current.frames == original_frames);
    CHECK(generated.has_selection);
    CHECK(generated.view_first == 100 && generated.view_last == 1000);

    {
        TsInstrument rotation;
        const float original[] = {-1.0f, -0.5f, 0.5f, 1.0f, -0.25f, -0.75f};
        ts_instrument_init(&rotation);
        rotation.parent.frames = sizeof(original) / sizeof(original[0]);
        rotation.parent.sample_rate = 44100;
        rotation.parent.data = (float *)malloc(sizeof(original));
        CHECK(rotation.parent.data != NULL);
        if (rotation.parent.data != NULL) {
            memcpy(rotation.parent.data, original, sizeof(original));
            CHECK(ts_sample_clone(&rotation.current, &rotation.parent, error, sizeof(error)));
            rotation.crop_last = rotation.parent.frames;
            rotation.view_last = rotation.current.frames;
            CHECK(ts_instrument_rotate_zero_crossing(&rotation, 1, 1, error, sizeof(error)));
            CHECK(rotation.current.frames == rotation.parent.frames);
            CHECK(rotation.current.data[0] == original[2]);
            CHECK(rotation.current.data[4] == original[0]);
            CHECK(ts_instrument_rotate_zero_crossing(&rotation, -1, 1, error, sizeof(error)));
            CHECK(memcmp(rotation.current.data, original, sizeof(original)) == 0);
            CHECK(ts_instrument_undo(&rotation, error, sizeof(error)));
            CHECK(rotation.current.data[0] == original[2]);
            CHECK(ts_instrument_undo(&rotation, error, sizeof(error)));
            CHECK(memcmp(rotation.current.data, original, sizeof(original)) == 0);
            ts_instrument_set_selection(&rotation, 1, 5);
            CHECK(ts_instrument_rotate_zero_crossing(&rotation, -1, 1, error, sizeof(error)));
            CHECK(rotation.current.data[0] == original[0]);
            CHECK(rotation.current.data[5] == original[5]);
            CHECK(ts_instrument_undo(&rotation, error, sizeof(error)));
            CHECK(memcmp(rotation.current.data, original, sizeof(original)) == 0);
        }
        ts_instrument_free(&rotation);
    }

    {
        TsInstrument warp;
        float original[256];
        float first_render[256];
        ts_instrument_init(&warp);
        for (size_t i = 0; i < 256; ++i)
            original[i] = 0.55f * sinf((float)i * 0.071f) +
                          0.18f * sinf((float)i * 0.233f);
        warp.parent.frames = 256;
        warp.parent.sample_rate = 44100;
        warp.parent.data = (float *)malloc(sizeof(original));
        CHECK(warp.parent.data != NULL);
        if (warp.parent.data != NULL) {
            memcpy(warp.parent.data, original, sizeof(original));
            CHECK(ts_sample_clone(&warp.current, &warp.parent, error, sizeof(error)));
            warp.crop_last = warp.parent.frames;
            warp.view_last = warp.current.frames;

            CHECK(ts_instrument_apply_warp(&warp, 0.0f, error, sizeof(error)));
            CHECK(memcmp(warp.current.data, original, sizeof(original)) == 0);
            CHECK(warp.undo_count == 0);

            ts_instrument_set_selection(&warp, 40, 220);
            CHECK(ts_instrument_apply_warp(&warp, 0.72f, error, sizeof(error)));
            CHECK(warp.current.frames == 256);
            CHECK(warp.undo_count == 1);
            CHECK(memcmp(warp.current.data, original, 40 * sizeof(float)) == 0);
            CHECK(memcmp(warp.current.data + 220, original + 220,
                         (256 - 220) * sizeof(float)) == 0);
            CHECK(warp.current.data[40] == original[40]);
            CHECK(warp.current.data[219] == original[219]);
            CHECK(memcmp(warp.current.data + 41, original + 41,
                         (220 - 42) * sizeof(float)) != 0);
            for (size_t i = 0; i < warp.current.frames; ++i)
                CHECK(isfinite(warp.current.data[i]));
            memcpy(first_render, warp.current.data, sizeof(first_render));
            CHECK(ts_instrument_undo(&warp, error, sizeof(error)));
            CHECK(memcmp(warp.current.data, original, sizeof(original)) == 0);
            CHECK(ts_instrument_apply_warp(&warp, 0.72f, error, sizeof(error)));
            CHECK(memcmp(warp.current.data, first_render, sizeof(first_render)) == 0);
        }
        ts_instrument_free(&warp);
    }

    {
        TsInstrument warp;
        float silence[9] = {0};
        ts_instrument_init(&warp);
        warp.parent.frames = 9;
        warp.parent.sample_rate = 8000;
        warp.parent.data = (float *)malloc(sizeof(silence));
        CHECK(warp.parent.data != NULL);
        if (warp.parent.data != NULL) {
            memcpy(warp.parent.data, silence, sizeof(silence));
            CHECK(ts_sample_clone(&warp.current, &warp.parent, error, sizeof(error)));
            warp.crop_last = warp.parent.frames;
            warp.view_last = warp.current.frames;
            CHECK(ts_instrument_apply_warp(&warp, 1.0f, error, sizeof(error)));
            CHECK(warp.current.frames == 9);
            CHECK(memcmp(warp.current.data, silence, sizeof(silence)) == 0);
            CHECK(ts_instrument_undo(&warp, error, sizeof(error)));
            ts_instrument_set_selection(&warp, 3, 4);
            CHECK(ts_instrument_apply_warp(&warp, 1.0f, error, sizeof(error)));
            CHECK(memcmp(warp.current.data, silence, sizeof(silence)) == 0);
            CHECK(!ts_instrument_apply_warp(&warp, NAN, error, sizeof(error)));
            CHECK(!ts_instrument_apply_warp(&warp, 1.1f, error, sizeof(error)));
        }
        ts_instrument_free(&warp);
    }
    {
        TsInstrument warp;
        TsWarpGesture gesture;
        float original[192];
        float preview_20[192];
        ts_instrument_init(&warp);
        ts_warp_gesture_init(&gesture);
        for (size_t i = 0; i < 192; ++i)
            original[i] = 0.61f * sinf((float)i * 0.093f) +
                          0.13f * cosf((float)i * 0.317f);
        warp.parent.frames = 192;
        warp.parent.sample_rate = 44100;
        warp.parent.data = (float *)malloc(sizeof(original));
        CHECK(warp.parent.data != NULL);
        if (warp.parent.data != NULL) {
            memcpy(warp.parent.data, original, sizeof(original));
            CHECK(ts_sample_clone(&warp.current, &warp.parent, error, sizeof(error)));
            warp.crop_last = warp.parent.frames;
            warp.view_first = 48;
            warp.view_last = 144;
            ts_instrument_set_selection(&warp, 24, 168);

            CHECK(ts_instrument_warp_gesture_begin(&warp, &gesture,
                                                    error, sizeof(error)));
            CHECK(ts_instrument_warp_gesture_preview(&warp, &gesture, 0.2f,
                                                      error, sizeof(error)));
            CHECK(warp.view_first == 48 && warp.view_last == 144);
            memcpy(preview_20, warp.current.data, sizeof(preview_20));
            CHECK(memcmp(warp.current.data, original, 24 * sizeof(float)) == 0);
            CHECK(memcmp(warp.current.data + 168, original + 168,
                         24 * sizeof(float)) == 0);
            CHECK(ts_instrument_warp_gesture_preview(&warp, &gesture, 0.8f,
                                                      error, sizeof(error)));
            CHECK(memcmp(warp.current.data, preview_20, sizeof(preview_20)) != 0);
            CHECK(ts_instrument_warp_gesture_preview(&warp, &gesture, 0.2f,
                                                      error, sizeof(error)));
            CHECK(memcmp(warp.current.data, preview_20, sizeof(preview_20)) == 0);
            CHECK(warp.undo_count == 0);
            CHECK(ts_instrument_warp_gesture_commit(&warp, &gesture,
                                                     error, sizeof(error)));
            CHECK(warp.view_first == 48 && warp.view_last == 144);
            CHECK(warp.undo_count == 1);
            CHECK(ts_instrument_undo(&warp, error, sizeof(error)));
            CHECK(memcmp(warp.current.data, original, sizeof(original)) == 0);

            CHECK(ts_instrument_warp_gesture_begin(&warp, &gesture,
                                                    error, sizeof(error)));
            CHECK(ts_instrument_warp_gesture_preview(&warp, &gesture, 0.9f,
                                                      error, sizeof(error)));
            CHECK(ts_instrument_warp_gesture_preview(&warp, &gesture, 0.0f,
                                                      error, sizeof(error)));
            CHECK(memcmp(warp.current.data, original, sizeof(original)) == 0);
            CHECK(ts_instrument_warp_gesture_commit(&warp, &gesture,
                                                     error, sizeof(error)));
            CHECK(warp.view_first == 48 && warp.view_last == 144);
            CHECK(warp.undo_count == 0);
            CHECK(memcmp(warp.current.data, original, sizeof(original)) == 0);

            CHECK(ts_instrument_warp_gesture_begin(&warp, &gesture,
                                                    error, sizeof(error)));
            CHECK(ts_instrument_warp_gesture_preview(&warp, &gesture, 0.7f,
                                                      error, sizeof(error)));
            CHECK(ts_instrument_warp_gesture_cancel(&warp, &gesture,
                                                     error, sizeof(error)));
            CHECK(warp.view_first == 48 && warp.view_last == 144);
            CHECK(warp.undo_count == 0);
            CHECK(memcmp(warp.current.data, original, sizeof(original)) == 0);

            CHECK(ts_instrument_warp_gesture_begin(&warp, &gesture,
                                                    error, sizeof(error)));
            ++warp.generation;
            CHECK(!ts_instrument_warp_gesture_preview(&warp, &gesture, 0.5f,
                                                       error, sizeof(error)));
            --warp.generation;
            CHECK(ts_instrument_warp_gesture_cancel(&warp, &gesture,
                                                     error, sizeof(error)));
        }
        ts_instrument_free(&warp);
    }
    {
        TsInstrument shaped;
        TsAmplitudeGesture gesture;
        float original[256];
        uint64_t original_hash;
        uint64_t shaped_hash;
        ts_instrument_init(&shaped);
        ts_amplitude_gesture_init(&gesture);
        for (size_t frame = 0; frame < 256; ++frame)
            original[frame] = 0.75f * sinf((float)frame * 0.071f);
        shaped.parent.frames = 256;
        shaped.parent.sample_rate = 44100;
        shaped.parent.data = (float *)malloc(sizeof(original));
        CHECK(shaped.parent.data != NULL);
        if (shaped.parent.data != NULL) {
            memcpy(shaped.parent.data, original, sizeof(original));
            CHECK(ts_sample_clone(&shaped.current, &shaped.parent,
                                  error, sizeof(error)));
            shaped.crop_last = shaped.parent.frames;
            shaped.view_last = shaped.current.frames;
            original_hash = ts_sample_hash(&shaped.current);
            CHECK(ts_instrument_amplitude_gesture_begin(
                &shaped, &gesture, error, sizeof(error)));
            CHECK(ts_instrument_amplitude_gesture_preview(
                &shaped, &gesture, 32, 1.0f, 96, 0.0f,
                error, sizeof(error)));
            CHECK(memcmp(shaped.current.data, original,
                         32 * sizeof(float)) == 0);
            CHECK(fabsf(shaped.current.data[64] - original[64] * 0.5f) <
                  0.00001f);
            CHECK(shaped.current.data[96] == 0.0f);
            CHECK(ts_instrument_amplitude_gesture_preview(
                &shaped, &gesture, 96, 0.0f, 96, 0.75f,
                error, sizeof(error)));
            CHECK(fabsf(shaped.current.data[96] - original[96] * 0.75f) <
                  0.00001f);
            CHECK(memcmp(shaped.current.data + 97, original + 97,
                         (256 - 97) * sizeof(float)) == 0);
            CHECK(shaped.undo_count == 0);
            CHECK(ts_instrument_amplitude_gesture_commit(
                &shaped, &gesture, error, sizeof(error)));
            CHECK(shaped.undo_count == 1);
            shaped_hash = ts_sample_hash(&shaped.current);
            CHECK(shaped_hash != original_hash);
            CHECK(ts_instrument_undo(&shaped, error, sizeof(error)));
            CHECK(ts_sample_hash(&shaped.current) == original_hash);
            CHECK(ts_instrument_redo(&shaped, error, sizeof(error)));
            CHECK(ts_sample_hash(&shaped.current) == shaped_hash);
            CHECK(ts_instrument_undo(&shaped, error, sizeof(error)));

            CHECK(ts_instrument_amplitude_gesture_begin(
                &shaped, &gesture, error, sizeof(error)));
            CHECK(ts_instrument_amplitude_gesture_preview(
                &shaped, &gesture, 140, 0.15f, 180, 0.85f,
                error, sizeof(error)));
            CHECK(ts_instrument_amplitude_gesture_cancel(
                &shaped, &gesture, error, sizeof(error)));
            CHECK(ts_sample_hash(&shaped.current) == original_hash &&
                  shaped.undo_count == 0);
        }
        ts_instrument_free(&shaped);
    }
    {
        TsInstrument rotation;
        float original[12];
        ts_instrument_init(&rotation);
        for (size_t i = 0; i < 12; ++i) original[i] = (i & 1u) ? 1.0f : -1.0f;
        rotation.parent.frames = 12;
        rotation.parent.sample_rate = 44100;
        rotation.parent.data = (float *)malloc(sizeof(original));
        CHECK(rotation.parent.data != NULL);
        if (rotation.parent.data != NULL) {
            memcpy(rotation.parent.data, original, sizeof(original));
            CHECK(ts_sample_clone(&rotation.current, &rotation.parent, error, sizeof(error)));
            rotation.crop_last = rotation.parent.frames;
            rotation.view_last = rotation.current.frames;
            CHECK(ts_instrument_rotate_zero_crossing(&rotation, 1, 5,
                                                      error, sizeof(error)));
            CHECK(rotation.current.data[0] == original[5]);
            CHECK(rotation.post_edit_count == 1);
            CHECK(ts_instrument_rotate_zero_crossing(&rotation, 1, 50,
                                                      error, sizeof(error)));
            CHECK(rotation.current.data[0] == original[11]);
            CHECK(rotation.post_edit_count == 1);
            CHECK(ts_instrument_rotate_zero_crossing(&rotation, -1, 50,
                                                      error, sizeof(error)));
            CHECK(rotation.current.data[0] == original[5]);
            CHECK(rotation.post_edit_count == 1);
            CHECK(ts_instrument_rotate_zero_crossing(&rotation, -1, 5,
                                                      error, sizeof(error)));
            CHECK(memcmp(rotation.current.data, original, sizeof(original)) == 0);
            CHECK(rotation.post_edit_count == 0);
            CHECK(ts_instrument_rotate_zero_crossing(&rotation, 1, 1000000,
                                                      error, sizeof(error)));
            CHECK(rotation.current.frames == 12 && rotation.post_edit_count == 1);
            CHECK(ts_instrument_rotate_zero_crossing(&rotation, 1, 100,
                                                      error, sizeof(error)));
            CHECK(rotation.post_edit_count == 1);
        }
        ts_instrument_free(&rotation);
    }

    parent_hash = ts_sample_hash(&generated.parent);
    CHECK(ts_instrument_reseed(&generated, error, sizeof(error)));
    CHECK(ts_sample_hash(&generated.parent) != parent_hash);
    CHECK(generated.generator.kind == TS_GENERATOR_TONAL);
    CHECK(ts_instrument_bank_count(&generated) == 1);
    CHECK(ts_sample_hash(&generated.bank[0].sample) == ts_sample_hash(&generated.parent));

    {
        int child_slot = -1;
        int sibling_slot = -1;
        int path_slot = -1;
        int stranger_slot = -1;
        int stranger_reseed_slot = -1;
        int repeated_slot = -1;
        uint64_t stable_parent;
        CHECK(ts_instrument_generate(&family, TS_GENERATOR_TONAL, 0x46414d31u,
                                     error, sizeof(error)));
        CHECK(ts_instrument_generate(&family_repeat, TS_GENERATOR_TONAL, 0x46414d31u,
                                     error, sizeof(error)));
        ts_instrument_set_selection(&family, family.current.frames / 4u,
                                    family.current.frames * 3u / 4u);
        CHECK(ts_instrument_set_loop_from_selection(&family, error, sizeof(error)));
        family.bank[0].has_loop = family.has_loop;
        family.bank[0].loop_first = family.loop_first;
        family.bank[0].loop_last = family.loop_last;
        family.bank[0].loop_mode = family.loop_mode;
        family.bank[0].loop_crossfade_ms = family.loop_crossfade_ms;
        family_repeat.bank[0].has_loop = family.bank[0].has_loop;
        family_repeat.bank[0].loop_first = family.bank[0].loop_first;
        family_repeat.bank[0].loop_last = family.bank[0].loop_last;
        stable_parent = ts_sample_hash(&family.parent);
        family.family_relation = TS_FAMILY_CHILD;
        family.family_mutation = 0.42f;
        family.family_locks = TS_FAMILY_LOCK_ALL;
        family_repeat.family_relation = family.family_relation;
        family_repeat.family_mutation = family.family_mutation;
        family_repeat.family_locks = family.family_locks;
        CHECK(ts_instrument_generate_family_candidate(&family, 0, 0, &child_slot,
                                                       error, sizeof(error)));
        CHECK(ts_instrument_generate_family_candidate(&family_repeat, 0, 0,
                                                       &repeated_slot,
                                                       error, sizeof(error)));
        CHECK(child_slot == 1 && repeated_slot == 1);
        CHECK(ts_sample_hash(&family.parent) == stable_parent);
        CHECK(family.bank[child_slot].relation == TS_FAMILY_CHILD &&
              family.bank[child_slot].parent_slot == 0);
        CHECK(family.bank[child_slot].sample.frames == family.bank[0].sample.frames);
        CHECK(family.bank[child_slot].tuning.root_note == family.bank[0].tuning.root_note);
        CHECK(family.bank[child_slot].has_loop &&
              family.bank[child_slot].loop_first == family.bank[0].loop_first &&
              family.bank[child_slot].loop_last == family.bank[0].loop_last);
        CHECK(ts_sample_hash(&family.bank[child_slot].sample) ==
              ts_sample_hash(&family_repeat.bank[repeated_slot].sample));
        CHECK(ts_instrument_generate_family_candidate(&family, 0, 1, &sibling_slot,
                                                       error, sizeof(error)));
        CHECK(sibling_slot == 2 && family.bank[sibling_slot].relation == TS_FAMILY_CHILD &&
              family.bank[sibling_slot].parent_slot == 0 &&
              family.bank[sibling_slot].lineage_seed !=
              family.bank[child_slot].lineage_seed);
        CHECK(ts_sample_hash(&family.bank[sibling_slot].sample) !=
              ts_sample_hash(&family.bank[child_slot].sample));
        family.family_trajectory = 1;
        family.family_relation = TS_FAMILY_COUSIN;
        family.family_locks = TS_FAMILY_LOCK_LOOP | TS_FAMILY_LOCK_PITCH;
        CHECK(ts_instrument_generate_family_candidate(&family, 0, 0, &path_slot,
                                                       error, sizeof(error)));
        CHECK(path_slot == 3 && family.bank[path_slot].relation == TS_FAMILY_COUSIN &&
              family.bank[path_slot].parent_slot == sibling_slot &&
              family.bank[path_slot].trajectory_step == 1u);
        family.family_trajectory = 0;
        family.family_relation = TS_FAMILY_STRANGER;
        family.family_locks = TS_FAMILY_LOCK_DURATION | TS_FAMILY_LOCK_PITCH;
        CHECK(ts_instrument_generate_family_candidate(&family, 0, 0, &stranger_slot,
                                                       error, sizeof(error)));
        CHECK(stranger_slot == 4 && family.bank[stranger_slot].relation == TS_FAMILY_STRANGER &&
              family.bank[stranger_slot].has_generator &&
              family.bank[stranger_slot].sample.frames == family.bank[0].sample.frames);
        CHECK(ts_instrument_generate_family_candidate(&family, 0, 1,
                                                       &stranger_reseed_slot,
                                                       error, sizeof(error)));
        CHECK(stranger_reseed_slot == 5 &&
              family.bank[stranger_reseed_slot].relation == TS_FAMILY_STRANGER &&
              family.bank[stranger_reseed_slot].generator.kind ==
              family.bank[stranger_slot].generator.kind &&
              family.bank[stranger_reseed_slot].lineage_seed !=
              family.bank[stranger_slot].lineage_seed);
        CHECK(ts_instrument_save_recipe(&family, "test-family.tsr", error, sizeof(error)));
        CHECK(ts_instrument_load_recipe(&family_restored, "test-family.tsr",
                                        error, sizeof(error)));
        CHECK(family_restored.family_sequence == family.family_sequence &&
              family_restored.family_relation == family.family_relation &&
              family_restored.family_locks == family.family_locks);
        for (int slot = 0; slot <= stranger_reseed_slot; ++slot) {
            CHECK(family_restored.bank[slot].relation == family.bank[slot].relation);
            CHECK(family_restored.bank[slot].parent_slot == family.bank[slot].parent_slot);
            CHECK(family_restored.bank[slot].lineage_seed ==
                  family.bank[slot].lineage_seed);
            CHECK(ts_sample_hash(&family_restored.bank[slot].sample) ==
                  ts_sample_hash(&family.bank[slot].sample));
        }
        for (int slot = stranger_reseed_slot + 1;
             slot < TS_BANK_SLOT_COUNT; ++slot)
            CHECK(ts_instrument_bank_capture(&family, slot,
                                             TS_BANK_CAPTURE_CURRENT,
                                             error, sizeof(error)));
        {
            uint64_t full_parent = ts_sample_hash(&family.parent);
            uint32_t full_sequence = family.family_sequence;
            int refused_slot = -1;
            CHECK(ts_instrument_bank_count(&family) == TS_BANK_SLOT_COUNT);
            CHECK(ts_instrument_bank_next_empty(&family) == -1);
            CHECK(!ts_instrument_generate_family_candidate(&family, 0, 0,
                                                            &refused_slot,
                                                            error, sizeof(error)));
            CHECK(refused_slot == -1 &&
                  ts_sample_hash(&family.parent) == full_parent &&
                  family.family_sequence == full_sequence);
            CHECK(ts_instrument_bank_clear(&family, 8, error, sizeof(error)));
            CHECK(ts_instrument_bank_next_empty(&family) == 8);
            CHECK(ts_instrument_generate_family_candidate(&family, 0, 0,
                                                           &refused_slot,
                                                           error, sizeof(error)));
            CHECK(refused_slot == 8);
            {
                uint32_t sequence_before_clear = family_repeat.family_sequence;
                CHECK(ts_instrument_bank_clear_all(&family_repeat, error, sizeof(error)));
                CHECK(ts_instrument_bank_count(&family_repeat) == 0);
                CHECK(ts_instrument_bank_first_empty(&family_repeat) == 0);
                CHECK(!family_repeat.bank[0].occupied);
                CHECK(family_repeat.family_anchor_slot == 0 &&
                      family_repeat.family_last_slot == -1);
                CHECK(family_repeat.family_sequence == sequence_before_clear);
                CHECK(!ts_instrument_generate_family_candidate(&family_repeat, 0, 0,
                                                                &refused_slot,
                                                                error, sizeof(error)));
            }
        }
        remove("test-family.tsr");
    }

    CHECK(ts_instrument_load_wav(&imported, "test-roundtrip.wav", error, sizeof(error)));
    CHECK(imported.source_kind == TS_SOURCE_IMPORTED);
    CHECK(ts_sample_hash(&imported.parent) == ts_sample_hash(&imported.current));
    parent_hash = ts_sample_hash(&imported.parent);
    current_hash = ts_sample_hash(&imported.current);
    CHECK(ts_instrument_reseed(&imported, error, sizeof(error)));
    CHECK(ts_sample_hash(&imported.parent) == parent_hash);
    CHECK(ts_sample_hash(&imported.current) == current_hash);
    CHECK(ts_sample_hash(&imported.bank[0].sample) == parent_hash);

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
    CHECK(ts_instrument_set_tuning(&committed, 58, -9.0f, error, sizeof(error)));
    CHECK(ts_instrument_bank_count(&committed) == 1);
    CHECK(committed.bank[0].occupied &&
          committed.bank[0].capture_kind == TS_BANK_CAPTURE_ROOT);
    family_root_hash = ts_sample_hash(&committed.bank[0].sample);
    CHECK(family_root_hash == ts_sample_hash(&committed.parent));
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
    CHECK(committed.tuning.root_note == 58 &&
          fabsf(committed.tuning.fine_tune_cents + 9.0f) < 0.001f);
    CHECK(!committed.process.delay_enabled && !committed.process.reverb_enabled);
    CHECK(ts_sample_hash(&committed.bank[0].sample) == family_root_hash);

    {
        uint64_t bank_root;
        uint64_t prior_parent;
        uint64_t selected_hash;
        uint32_t prior_generation;
        CHECK(ts_instrument_generate(&bank_edit, TS_GENERATOR_TONAL, 0x42414e4bu,
                                     error, sizeof(error)));
        CHECK(ts_instrument_set_tuning(&bank_edit, 53, 14.0f,
                                       error, sizeof(error)));
        bank_root = ts_sample_hash(&bank_edit.bank[0].sample);
        prior_parent = ts_sample_hash(&bank_edit.parent);
        prior_generation = bank_edit.generation;
        ts_instrument_set_selection(&bank_edit, bank_edit.current.frames / 4u,
                                    bank_edit.current.frames * 3u / 4u);
        CHECK(ts_instrument_set_loop_from_selection(&bank_edit, error, sizeof(error)));
        CHECK(ts_instrument_set_loop_crossfade(&bank_edit, 13.0f,
                                               error, sizeof(error)));
        CHECK(ts_instrument_bank_capture(&bank_edit, 1, TS_BANK_CAPTURE_LOOP,
                                         error, sizeof(error)));
        CHECK(ts_instrument_bank_rename(&bank_edit, 1, "Drone Core",
                                        error, sizeof(error)));
        CHECK(bank_edit.bank[1].tuning.root_note == TS_KEYBOARD_BASE_NOTE &&
              bank_edit.bank[1].tuning.fine_tune_cents == 0.0f);
        selected_hash = ts_sample_hash(&bank_edit.bank[1].sample);
        process = bank_edit.process;
        process.edge = 0.71f;
        CHECK(ts_instrument_set_process(&bank_edit, &process, error, sizeof(error)));
        CHECK(ts_instrument_set_tuning(&bank_edit, 70, 0.0f, error, sizeof(error)));
        CHECK(bank_edit.undo_count > 0);
        CHECK(ts_instrument_set_bank_as_current(&bank_edit, 1,
                                                error, sizeof(error)));
        CHECK(bank_edit.generation == prior_generation + 1u);
        CHECK(bank_edit.ancestor_hash == prior_parent);
        CHECK(bank_edit.source_kind == TS_SOURCE_COMMITTED);
        CHECK(bank_edit.tuning.root_note == TS_KEYBOARD_BASE_NOTE &&
              bank_edit.tuning.fine_tune_cents == 0.0f);
        CHECK(ts_sample_hash(&bank_edit.parent) == selected_hash);
        CHECK(ts_sample_hash(&bank_edit.current) == selected_hash);
        CHECK(strcmp(bank_edit.parent.name, "Drone Core") == 0);
        CHECK(memcmp(bank_edit.current.data, bank_edit.parent.data,
                     bank_edit.parent.frames * sizeof(float)) == 0);
        CHECK(bank_edit.has_loop && bank_edit.loop_first == 0 &&
              bank_edit.loop_last == bank_edit.current.frames);
        CHECK(fabsf(bank_edit.loop_crossfade_ms - 13.0f) < 0.0001f);
        CHECK(bank_edit.undo_count == 0 && bank_edit.redo_count == 0);
        CHECK(bank_edit.sample_edit_count == 0 && !bank_edit.has_selection);
        CHECK(!bank_edit.process.noise_enabled && !bank_edit.process.delay_enabled &&
              !bank_edit.process.reverb_enabled);
        CHECK(ts_instrument_bank_count(&bank_edit) == 2);
        CHECK(ts_sample_hash(&bank_edit.bank[0].sample) == bank_root);
        CHECK(ts_sample_hash(&bank_edit.bank[1].sample) == selected_hash);
        CHECK(!ts_instrument_set_bank_as_current(&bank_edit, 2,
                                                 error, sizeof(error)));
        process = bank_edit.process;
        process.body = 0.82f;
        CHECK(ts_instrument_set_process(&bank_edit, &process, error, sizeof(error)));
        CHECK(ts_sample_hash(&bank_edit.parent) == selected_hash);
        CHECK(ts_instrument_reset_current(&bank_edit, error, sizeof(error)));
        CHECK(ts_sample_hash(&bank_edit.current) == selected_hash);
    }

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
        CHECK(ts_audition_plan(&audition, TS_AUDITION_CURRENT,
                               TS_AUDITION_WORKBENCH_LOOP, &plan));
        CHECK(plan.first == 10 && plan.last == 500);
        audition.view_first = 20;
        audition.view_last = 400;
        CHECK(ts_audition_plan(&audition, TS_AUDITION_CURRENT,
                               TS_AUDITION_WORKBENCH_LOOP, &plan));
        CHECK(plan.first == 10 && plan.last == 500);
        ts_instrument_clear_selection(&audition);
        CHECK(ts_audition_plan(&audition, TS_AUDITION_CURRENT,
                               TS_AUDITION_WORKBENCH_LOOP, &plan));
        CHECK(plan.first == 20 && plan.last == 400);
        audition.view_first = audition.current.frames + 10u;
        audition.view_last = audition.current.frames + 20u;
        CHECK(!ts_audition_plan(&audition, TS_AUDITION_CURRENT,
                                TS_AUDITION_WORKBENCH_LOOP, &plan));
        audition.view_first = 5;
        audition.view_last = 550;
        ts_instrument_set_selection(&audition, 10, 500);
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
        CHECK(audition.loop_mode == TS_LOOP_FORWARD);
        CHECK(ts_instrument_set_loop_mode(&audition, TS_LOOP_REVERSE,
                                          error, sizeof(error)));
        CHECK(audition.loop_mode == TS_LOOP_REVERSE);
        CHECK(ts_instrument_undo(&audition, error, sizeof(error)) &&
              audition.loop_mode == TS_LOOP_FORWARD);
        CHECK(ts_instrument_redo(&audition, error, sizeof(error)) &&
              audition.loop_mode == TS_LOOP_REVERSE);
        CHECK(ts_instrument_set_loop_mode(&audition, TS_LOOP_PING_PONG,
                                          error, sizeof(error)));
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
            CHECK(!audition.has_loop && audition.loop_mode == TS_LOOP_FORWARD);
            CHECK(ts_instrument_undo(&audition, error, sizeof(error)) && audition.has_loop &&
                  audition.loop_mode == TS_LOOP_PING_PONG);
            CHECK(ts_instrument_commit_current(&audition, error, sizeof(error)));
            CHECK(audition.has_loop && audition.loop_first == loop_first &&
                  audition.loop_last == loop_last);
            CHECK(audition.loop_mode == TS_LOOP_PING_PONG);
            CHECK(audition.undo_count == 0 && audition.redo_count == 0);
        }
    }

    {
        float loop_data[] = {1.0f, 1.0f, 0.5f, 0.0f, -0.5f, -1.0f, -1.0f, -1.0f};
        TsSample loop_sample = {loop_data, 8, 1000, "loop", 0u};
        float blended = ts_audition_read_looped(&loop_sample, 7.0, 0, 8, 2);
        CHECK(fabsf(blended) < 0.0001f);
        /* A zero-crossfade loop still needs cyclic interpolation. Holding the
           final frame until wrap adds a small discontinuity whenever playback
           pitch or the device rate makes the position fractional. */
        CHECK(fabsf(ts_audition_read_looped(&loop_sample, 7.5, 0, 8, 0)) <
              0.0001f);
        CHECK(fabs(ts_audition_wrap_position(8.0, 0, 8, 2) - 2.0) < 0.000001);
        CHECK(fabs(ts_audition_wrap_position(14.0, 0, 8, 2) - 2.0) < 0.000001);
        {
            int direction = -1;
            CHECK(fabs(ts_audition_loop_position(-1.0, 0, 8, 2,
                                                  TS_LOOP_REVERSE, &direction) - 4.0) <
                  0.000001);
            direction = 1;
            CHECK(fabs(ts_audition_loop_position(8.0, 0, 8, 0,
                                                  TS_LOOP_PING_PONG, &direction) - 6.0) <
                  0.000001);
            CHECK(direction == -1);
            CHECK(fabsf(ts_audition_read_looped_mode(&loop_sample, 6.0, 0, 8, 0,
                                                      TS_LOOP_PING_PONG) + 1.0f) <
                  0.0001f);
        }
    }

    {
        TsInstrument tape;
        TsInstrument tape_loaded;
        uint64_t before;
        uint64_t after;
        size_t original_frames;
        size_t source_first = 1000u;
        size_t source_last = 1600u;
        int64_t mixed_destination;
        float mixed_source_value;
        float mixed_under_value;
        float source_peak = 0.0f;
        float destination_peak = 0.0f;
        float summed_peak = 0.0f;
        float target_peak;
        float mix_scale;
        ts_instrument_init(&tape);
        ts_instrument_init(&tape_loaded);
        CHECK(ts_instrument_generate(&tape, TS_GENERATOR_METALLIC, 0x54415038u,
                                     error, sizeof(error)));
        original_frames = tape.current.frames;
        before = ts_sample_hash(&tape.current);
        mixed_destination = ts_sample_snap_tape_destination(
            &tape.current, 3000, source_last - source_first);
        mixed_source_value = tape.current.data[source_first + 300u];
        mixed_under_value = tape.current.data[(size_t)mixed_destination + 300u];
        for (size_t i = 0; i < source_last - source_first; ++i) {
            float source_value = tape.current.data[source_first + i];
            float under_value = tape.current.data[(size_t)mixed_destination + i];
            float edge_gain = 1.0f;
            size_t fade = tape.current.sample_rate / 1000u;
            if (fade < 8u) fade = 8u;
            if (fade > 64u) fade = 64u;
            if (i < fade) edge_gain = (float)(i + 1u) / (float)(fade + 1u);
            if (source_last - source_first - 1u - i < fade) {
                float tail = (float)(source_last - source_first - i) /
                             (float)(fade + 1u);
                if (tail < edge_gain) edge_gain = tail;
            }
            if (fabsf(source_value) > source_peak) source_peak = fabsf(source_value);
            if (fabsf(under_value) > destination_peak)
                destination_peak = fabsf(under_value);
            if (fabsf(under_value + source_value * edge_gain) > summed_peak)
                summed_peak = fabsf(under_value + source_value * edge_gain);
        }
        target_peak = source_peak > destination_peak ? source_peak : destination_peak;
        mix_scale = target_peak / summed_peak;
        ts_instrument_set_selection(&tape, source_first, source_last);
        tape.view_first = 200u; tape.view_last = 900u;
        CHECK(ts_instrument_apply_tape_drag(&tape, TS_POST_COPY_MIX,
                                            source_first, source_last, 3000,
                                            error, sizeof(error)));
        CHECK(tape.post_edit_count == 1 && tape.current.frames == original_frames);
        CHECK(tape.view_first == 200u && tape.view_last == 900u);
        CHECK(tape.selection_first == (size_t)tape.post_edits[0].destination &&
              tape.selection_last - tape.selection_first == source_last - source_first);
        CHECK(fabsf(tape.current.data[(size_t)mixed_destination + 300u] -
                    (mixed_under_value + mixed_source_value) * mix_scale) < 0.00001f);
        {
            float merged_peak = 0.0f;
            for (size_t i = 0; i < source_last - source_first; ++i) {
                float value = fabsf(tape.current.data[(size_t)mixed_destination + i]);
                if (value > merged_peak) merged_peak = value;
            }
            CHECK(fabsf(merged_peak - target_peak) < 0.00001f);
        }
        after = ts_sample_hash(&tape.current);
        CHECK(after != before);
        CHECK(ts_instrument_undo(&tape, error, sizeof(error)) &&
              ts_sample_hash(&tape.current) == before && tape.post_edit_count == 0);
        CHECK(tape.view_first == 200u && tape.view_last == 900u);
        CHECK(ts_instrument_redo(&tape, error, sizeof(error)) &&
              ts_sample_hash(&tape.current) == after && tape.post_edit_count == 1);
        CHECK(tape.view_first == 200u && tape.view_last == 900u);

        CHECK(ts_instrument_apply_tape_drag(&tape, TS_POST_MOVE_OVERWRITE,
                                            source_first, source_last, 1250,
                                            error, sizeof(error)));
        CHECK(tape.post_edit_count == 2);
        CHECK(tape.view_first == 200u && tape.view_last == 900u);
        after = ts_sample_hash(&tape.current);
        CHECK(ts_instrument_undo(&tape, error, sizeof(error)));
        CHECK(ts_instrument_redo(&tape, error, sizeof(error)) &&
              ts_sample_hash(&tape.current) == after);
        CHECK(tape.view_first == 200u && tape.view_last == 900u);

        CHECK(ts_instrument_apply_tape_drag(&tape, TS_POST_COPY_OVERWRITE,
                                            2000, 2400, -100,
                                            error, sizeof(error)));
        CHECK(tape.current.frames == original_frames + 100u);
        CHECK(tape.view_first == 0u && tape.view_last == 700u);
        CHECK(tape.selection_first == 0 && tape.selection_last == 400u);
        CHECK(ts_instrument_apply_tape_drag(&tape, TS_POST_MOVE_MIX,
                                            500, 800,
                                            (int64_t)tape.current.frames + 20,
                                            error, sizeof(error)));
        CHECK(tape.current.frames == original_frames + 420u);
        CHECK(tape.post_edit_count == 4);
        CHECK(tape.view_first == tape.current.frames - 700u &&
              tape.view_last == tape.current.frames);
        CHECK(!ts_instrument_apply_tape_drag(&tape, TS_POST_COPY_MIX,
                                             12u, 12u, 20, error, sizeof(error)));
        CHECK(tape.view_first == tape.current.frames - 700u &&
              tape.view_last == tape.current.frames);
        CHECK(fabsf(tape.current.data[550]) < 0.000001f);
        ts_instrument_set_selection(&tape, tape.selection_first, tape.selection_last);
        CHECK(ts_instrument_apply_sample_edit(&tape, TS_SAMPLE_EDIT_GAIN, 0.5f,
                                              error, sizeof(error)));
        CHECK(tape.post_edit_count == 5);
        {
            size_t crop_length = tape.selection_last - tape.selection_first;
            uint64_t pre_crop_hash = ts_sample_hash(&tape.current);
            CHECK(ts_instrument_crop_selection(&tape, error, sizeof(error)));
            CHECK(tape.current.frames == crop_length && tape.post_edit_count == 6);
            CHECK(ts_instrument_undo(&tape, error, sizeof(error)) &&
                  ts_sample_hash(&tape.current) == pre_crop_hash &&
                  tape.post_edit_count == 5);
        }

        ts_instrument_set_selection(&tape, 100, 900);
        CHECK(ts_instrument_set_loop_from_selection(&tape, error, sizeof(error)));
        CHECK(ts_instrument_set_loop_mode(&tape, TS_LOOP_PING_PONG,
                                          error, sizeof(error)));
        CHECK(ts_instrument_save_recipe(&tape, "test-tape.tsr", error, sizeof(error)));
        CHECK(ts_instrument_load_recipe(&tape_loaded, "test-tape.tsr",
                                        error, sizeof(error)));
        CHECK(ts_sample_hash(&tape_loaded.current) == ts_sample_hash(&tape.current));
        CHECK(tape_loaded.post_edit_count == tape.post_edit_count);
        CHECK(tape_loaded.loop_mode == TS_LOOP_PING_PONG);
        CHECK(tape_loaded.has_loop && tape_loaded.loop_first == tape.loop_first &&
              tape_loaded.loop_last == tape.loop_last);
        remove("test-tape.tsr");
        ts_instrument_free(&tape);
        ts_instrument_free(&tape_loaded);
    }

    {
        TsInstrument move_mix;
        size_t first = 1000u;
        size_t last = 1800u;
        int64_t destination;
        float source_peak = 0.0f;
        float underneath_peak = 0.0f;
        float expected_peak;
        float result_peak = 0.0f;
        ts_instrument_init(&move_mix);
        CHECK(ts_instrument_generate(&move_mix, TS_GENERATOR_PULSE, 0x4d495831u,
                                     error, sizeof(error)));
        destination = ts_sample_snap_tape_destination(&move_mix.current, 1400,
                                                       last - first);
        for (size_t i = 0; i < last - first; ++i) {
            size_t at = (size_t)destination + i;
            float source_value = fabsf(move_mix.current.data[first + i]);
            float under_value = at >= first && at < last ? 0.0f :
                                fabsf(move_mix.current.data[at]);
            if (source_value > source_peak) source_peak = source_value;
            if (under_value > underneath_peak) underneath_peak = under_value;
        }
        expected_peak = source_peak > underneath_peak ? source_peak : underneath_peak;
        CHECK(ts_instrument_apply_tape_drag(&move_mix, TS_POST_MOVE_MIX,
                                            first, last, 1400,
                                            error, sizeof(error)));
        for (size_t i = 0; i < last - first; ++i) {
            float value = fabsf(move_mix.current.data[(size_t)destination + i]);
            if (value > result_peak) result_peak = value;
        }
        CHECK(fabsf(result_peak - expected_peak) < 0.00001f);
        CHECK(fabsf(move_mix.current.data[first + 100u]) < 0.000001f);
        ts_instrument_free(&move_mix);
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
    CHECK(ts_instrument_set_loop_mode(&committed, TS_LOOP_PING_PONG,
                                      error, sizeof(error)));
    process = committed.process;
    process.noise_enabled = 1;
    process.noise_amount = 0.11f;
    process.delay_enabled = 1;
    process.delay_seconds = 0.019f;
    process.delay_mix = 0.22f;
    process.filter_enabled = 1;
    process.filter_mode = TS_FILTER_BANDPASS;
    process.filter_cutoff_hz = 1337.0f;
    process.filter_resonance = 0.61f;
    process.shaper_enabled = 1;
    process.shaper_mode = TS_SHAPER_FOLD;
    process.shaper_drive = 4.75f;
    process.shaper_mix = 0.72f;
    CHECK(ts_instrument_set_process(&committed, &process, error, sizeof(error)));
    CHECK(ts_instrument_bank_capture(&committed, 1, TS_BANK_CAPTURE_CURRENT,
                                     error, sizeof(error)));
    CHECK(ts_instrument_bank_capture(&committed, 2, TS_BANK_CAPTURE_SELECTION,
                                     error, sizeof(error)));
    CHECK(ts_instrument_bank_capture(&committed, 3, TS_BANK_CAPTURE_LOOP,
                                     error, sizeof(error)));
    CHECK(ts_instrument_bank_count(&committed) == 4);
    CHECK(ts_instrument_bank_first_empty(&committed) == 4);
    CHECK(committed.bank[1].sample.frames == committed.current.frames);
    CHECK(committed.bank[1].tuning.root_note == TS_KEYBOARD_BASE_NOTE);
    CHECK(committed.bank[1].audible_tuning.root_note == TS_KEYBOARD_BASE_NOTE);
    CHECK(committed.bank[2].sample.frames ==
          committed.selection_last - committed.selection_first);
    CHECK(committed.bank[3].has_loop && committed.bank[3].loop_first == 0 &&
          committed.bank[3].loop_last == committed.bank[3].sample.frames);
    {
        TsAuditionPlan bank_plan;
        size_t preserved_first = committed.bank[1].loop_first;
        size_t preserved_last = committed.bank[1].loop_last;
        TsLoopMode preserved_mode = committed.bank[1].loop_mode;
        float preserved_crossfade = committed.bank[1].loop_crossfade_ms;
        CHECK(!committed.bank[2].has_loop);
        CHECK(ts_instrument_bank_set_loop_full(&committed, 2,
                                               error, sizeof(error)));
        CHECK(committed.bank[2].has_loop && committed.bank[2].loop_first == 0 &&
              committed.bank[2].loop_last == committed.bank[2].sample.frames);
        CHECK(ts_instrument_bank_clear_loop(&committed, 2,
                                            error, sizeof(error)));
        CHECK(!committed.bank[2].has_loop);
        CHECK(ts_instrument_bank_set_loop_full(&committed, 2,
                                               error, sizeof(error)));
        CHECK(ts_instrument_bank_move_loop_endpoint(
                  &committed, 2, 1, committed.bank[2].sample.frames / 4u) != 0);
        CHECK(ts_instrument_bank_move_loop_endpoint(
                  &committed, 2, 2, committed.bank[2].sample.frames * 3u / 4u) != 0);
        CHECK(committed.bank[2].loop_first < committed.bank[2].loop_last);
        CHECK(ts_instrument_bank_set_loop_mode(&committed, 2, TS_LOOP_REVERSE,
                                               error, sizeof(error)));
        CHECK(ts_instrument_bank_set_loop_crossfade(&committed, 2, 17.0f,
                                                    error, sizeof(error)));
        CHECK(ts_bank_audition_plan(&committed, 2, &bank_plan));
        CHECK(bank_plan.sample == &committed.bank[2].sample &&
              bank_plan.first == committed.bank[2].loop_first &&
              bank_plan.last == committed.bank[2].loop_last);
        CHECK(ts_bank_audition_plan(&committed, 0, &bank_plan));
        CHECK(bank_plan.first == 0 &&
              bank_plan.last == committed.bank[0].sample.frames);
        CHECK(!ts_bank_audition_plan(&committed, 4, &bank_plan));
        CHECK(committed.bank[1].loop_first == preserved_first &&
              committed.bank[1].loop_last == preserved_last &&
              committed.bank[1].loop_mode == preserved_mode &&
              fabsf(committed.bank[1].loop_crossfade_ms - preserved_crossfade) < 0.0001f);
        CHECK(!ts_instrument_bank_set_loop_full(&committed, 4,
                                                error, sizeof(error)));
    }
    CHECK(ts_instrument_bank_rename(&committed, 2, "  Growing Tail  ",
                                    error, sizeof(error)));
    CHECK(strcmp(committed.bank[2].sample.name, "Growing Tail") == 0);
    CHECK(ts_instrument_bank_rename(&committed, 0, "Bank One",
                                    error, sizeof(error)));
    CHECK(strcmp(committed.bank[0].sample.name, "Bank One") == 0);
    CHECK(!ts_instrument_bank_rename(&committed, 4, "Empty",
                                     error, sizeof(error)));
    CHECK(!ts_instrument_bank_rename(&committed, 2, "   ",
                                     error, sizeof(error)));
    CHECK(!ts_instrument_bank_capture(&committed, 3, TS_BANK_CAPTURE_CURRENT,
                                      error, sizeof(error)));
    CHECK(ts_instrument_bank_clear(&committed, 3, error, sizeof(error)));
    CHECK(committed.bank[0].occupied && committed.bank[1].occupied &&
          committed.bank[2].occupied && !committed.bank[3].occupied);
    CHECK(ts_instrument_save_recipe(&committed, "test-recipe.tsr", error, sizeof(error)));
    {
        FILE *recipe = fopen("test-recipe.tsr", "rb");
        char magic[5] = {0};
        CHECK(recipe != NULL);
        if (recipe != NULL) {
            CHECK(fread(magic, 1, sizeof(magic), recipe) == sizeof(magic));
            fclose(recipe);
        }
        CHECK(memcmp(magic, "TSR26", 5) == 0);
    }
    CHECK(ts_instrument_load_recipe(&restored, "test-recipe.tsr", error, sizeof(error)));
    CHECK(ts_sample_hash(&restored.parent) == ts_sample_hash(&committed.parent));
    CHECK(ts_sample_hash(&restored.current) == ts_sample_hash(&committed.current));
    CHECK(restored.generation == committed.generation &&
          restored.ancestor_hash == committed.ancestor_hash);
    CHECK(restored.sample_edit_count == committed.sample_edit_count);
    CHECK(restored.has_loop && restored.loop_first == committed.loop_first &&
          restored.loop_last == committed.loop_last);
    CHECK(fabsf(restored.loop_crossfade_ms - 9.5f) < 0.0001f);
    CHECK(restored.loop_mode == TS_LOOP_PING_PONG);
    CHECK(restored.process.filter_enabled &&
          restored.process.filter_mode == TS_FILTER_BANDPASS);
    CHECK(fabsf(restored.process.filter_cutoff_hz - 1337.0f) < 0.001f);
    CHECK(restored.process.shaper_enabled &&
          restored.process.shaper_mode == TS_SHAPER_FOLD);
    CHECK(restored.tuning.root_note == committed.tuning.root_note &&
          fabsf(restored.tuning.fine_tune_cents -
                committed.tuning.fine_tune_cents) < 0.001f);
    CHECK(restored.audible_tuning.root_note == committed.audible_tuning.root_note &&
          fabsf(restored.audible_tuning.fine_tune_cents -
                committed.audible_tuning.fine_tune_cents) < 0.001f);
    CHECK(fabsf(restored.process.shaper_drive - 4.75f) < 0.001f);
    CHECK(ts_instrument_bank_count(&restored) == 3);
    CHECK(!restored.bank[3].occupied);
    CHECK(strcmp(restored.bank[2].sample.name, "Growing Tail") == 0);
    for (int slot = 0; slot < 3; ++slot) {
        CHECK(restored.bank[slot].occupied);
        CHECK(ts_sample_hash(&restored.bank[slot].sample) ==
              (slot == committed.selected_slot ? ts_sample_hash(&committed.current) :
               ts_sample_hash(&committed.bank[slot].sample)));
        CHECK(restored.bank[slot].capture_kind == committed.bank[slot].capture_kind);
        CHECK(restored.bank[slot].tuning.root_note == committed.bank[slot].tuning.root_note);
        CHECK(restored.bank[slot].loop_mode == committed.bank[slot].loop_mode);
        CHECK(restored.bank[slot].has_loop == committed.bank[slot].has_loop);
        CHECK(restored.bank[slot].loop_first == committed.bank[slot].loop_first);
        CHECK(restored.bank[slot].loop_last == committed.bank[slot].loop_last);
        CHECK(fabsf(restored.bank[slot].loop_crossfade_ms -
                    committed.bank[slot].loop_crossfade_ms) < 0.0001f);
    }
    CHECK(ts_instrument_export_bank(&restored, "test-bank-family", error, sizeof(error)));
    {
        DIR *directory = opendir("test-bank-family");
        struct dirent *entry;
        int wav_count = 0;
        int looped_wav_count = 0;
        int expected_looped = 0;
        for (int slot = 0; slot < TS_BANK_SLOT_COUNT; ++slot)
            if (restored.bank[slot].occupied && restored.bank[slot].has_loop)
                ++expected_looped;
        CHECK(directory != NULL);
        if (directory != NULL) {
            while ((entry = readdir(directory)) != NULL) {
                size_t length = strlen(entry->d_name);
                if (length > 4 && strcmp(entry->d_name + length - 4, ".wav") == 0) {
                    char exported[512];
                    TsSample bank_wav;
                    TsTuning bank_tuning = {0, 0.0f};
                    int has_loop = 0;
                    size_t loop_first = 0, loop_last = 0;
                    TsLoopMode loop_mode = TS_LOOP_FORWARD;
                    snprintf(exported, sizeof(exported), "test-bank-family/%s", entry->d_name);
                    ts_sample_init(&bank_wav);
                    CHECK(ts_sample_load_wav_metadata(
                        &bank_wav, &bank_tuning, &has_loop, &loop_first, &loop_last,
                        &loop_mode, exported, error, sizeof(error)));
                    CHECK(bank_tuning.root_note == TS_KEYBOARD_BASE_NOTE &&
                          fabsf(bank_tuning.fine_tune_cents) < 0.001f);
                    if (has_loop) {
                        CHECK(loop_last > loop_first);
                        ++looped_wav_count;
                    }
                    ts_sample_free(&bank_wav);
                    ++wav_count;
                    remove(exported);
                }
            }
            closedir(directory);
        }
        CHECK(wav_count == 3);
        CHECK(looped_wav_count == expected_looped);
        rmdir("test-bank-family");
    }
    {
        FILE *bad = fopen("test-malformed.tsr", "wb");
        uint64_t parent_before = ts_sample_hash(&restored.parent);
        uint64_t current_before = ts_sample_hash(&restored.current);
        CHECK(bad != NULL);
        if (bad != NULL) {
            CHECK(fwrite("TSR6", 1, 4, bad) == 4);
            fclose(bad);
        }
        CHECK(!ts_instrument_load_recipe(&restored, "test-malformed.tsr",
                                         error, sizeof(error)));
        CHECK(ts_sample_hash(&restored.parent) == parent_before);
        CHECK(ts_sample_hash(&restored.current) == current_before);
        CHECK(restored.has_loop && restored.loop_first == committed.loop_first &&
              restored.loop_last == committed.loop_last);
        remove("test-malformed.tsr");
    }
    {
        size_t old_first = restored.loop_first;
        size_t old_last = restored.loop_last;
        int role;
        ts_instrument_begin_loop_drag(&restored);
        role = ts_instrument_move_loop_endpoint(&restored, 2, 0);
        CHECK(role == 1);
        CHECK(restored.loop_first < restored.loop_last);
        CHECK(ts_instrument_undo(&restored, error, sizeof(error)));
        CHECK(restored.loop_first == old_first && restored.loop_last == old_last);
    }
    CHECK(ts_note_bank_start(&notes, &restored, TS_AUDITION_CURRENT,
                             0, 1, 48000) == TS_NOTE_STARTED);
    {
        const TsNoteVoice *voice = ts_note_bank_display_voice(&notes);
        double prior_pitch = voice != NULL ? voice->pitch : 0.0;
        TsTuning preview = {60, 0.0f};
        int accepted_root = restored.tuning.root_note;
        ts_note_bank_sync_tuned(&notes, &restored, &preview, 48000);
        voice = ts_note_bank_display_voice(&notes);
        CHECK(voice != NULL && fabs(voice->pitch - 1.0) < 0.0001);
        CHECK(restored.tuning.root_note == accepted_root);
        CHECK(ts_instrument_set_tuning(&restored, TS_KEYBOARD_BASE_NOTE, 0.0f,
                                       error, sizeof(error)));
        ts_note_bank_sync(&notes, &restored, 48000);
        voice = ts_note_bank_display_voice(&notes);
        CHECK(voice != NULL && fabs(voice->pitch - 1.0) < 0.0001);
        CHECK(fabs(prior_pitch - voice->pitch) > 0.01);
    }
    CHECK(ts_note_bank_start(&notes, &restored, TS_AUDITION_CURRENT,
                             4, 1, 48000) == TS_NOTE_STARTED);
    CHECK(ts_note_bank_start(&notes, &restored, TS_AUDITION_CURRENT,
                             7, 1, 48000) == TS_NOTE_STARTED);
    CHECK(ts_note_bank_start(&notes, &restored, TS_AUDITION_CURRENT,
                             12, 1, 48000) == TS_NOTE_STARTED);
    CHECK(ts_note_bank_start(&notes, &restored, TS_AUDITION_CURRENT,
                             16, 1, 48000) == TS_NOTE_STARTED);
    CHECK(ts_note_bank_count(&notes) == TS_NOTE_VOICE_LIMIT);
    CHECK(ts_note_bank_display_voice(&notes) != NULL &&
          ts_note_bank_display_voice(&notes)->note == 16);
    CHECK(ts_note_bank_mask(&notes) == ((1u << 0) | (1u << 4) | (1u << 7) |
                                        (1u << 12) | (1u << 16)));
    CHECK(ts_note_bank_start(&notes, &restored, TS_AUDITION_CURRENT,
                             19, 1, 48000) == TS_NOTE_LIMIT_REACHED);
    CHECK(ts_note_bank_start(&notes, &restored, TS_AUDITION_CURRENT,
                             7, 1, 48000) == TS_NOTE_TOGGLED_OFF);
    CHECK(ts_note_bank_count(&notes) == 4);
    CHECK(ts_note_bank_start(&notes, &restored, TS_AUDITION_CURRENT,
                             2, 0, 48000) == TS_NOTE_STARTED);
    CHECK(ts_note_bank_count(&notes) == 5);
    ts_note_bank_release(&notes, 2);
    CHECK(ts_note_bank_count(&notes) == 4);
    {
        const TsNoteVoice *voice = ts_note_bank_display_voice(&notes);
        double before = voice != NULL ? voice->position : 0.0;
        (void)ts_note_bank_read(&notes);
        voice = ts_note_bank_display_voice(&notes);
        CHECK(voice != NULL && voice->position > before);
    }
    process = restored.process;
    process.body = 0.73f;
    CHECK(ts_instrument_set_process(&restored, &process, error, sizeof(error)));
    ts_note_bank_sync(&notes, &restored, 48000);
    CHECK(ts_note_bank_count(&notes) == 4);
    CHECK(ts_note_bank_display_voice(&notes)->sample == &restored.current);
    ts_note_bank_set_source(&notes, &restored, TS_AUDITION_PARENT, 48000);
    CHECK(ts_note_bank_display_voice(&notes)->sample == &restored.parent);
    ts_note_bank_clear_latched(&notes);
    CHECK(ts_note_bank_count(&notes) == 0);
    CHECK(ts_note_bank_start_tuned_at(
              &notes, &restored, &restored.tuning, TS_AUDITION_CURRENT,
              0, 48, 1, 48000) == TS_NOTE_STARTED);
    CHECK(ts_note_bank_start_tuned_at(
              &notes, &restored, &restored.tuning, TS_AUDITION_CURRENT,
              0, 60, 1, 48000) == TS_NOTE_STARTED);
    CHECK(ts_note_bank_count(&notes) == 2);
    CHECK(notes.voices[0].active && notes.voices[0].midi_note == 48 &&
          fabs(notes.voices[0].pitch - 0.5) < 0.0001);
    CHECK(notes.voices[1].active && notes.voices[1].midi_note == 60 &&
          fabs(notes.voices[1].pitch - 1.0) < 0.0001);
    CHECK(ts_note_bank_mask(&notes) == 1u);
    CHECK(ts_note_bank_visible_mask(&notes, 48) == ((1u << 0) | (1u << 12)));
    CHECK(ts_note_bank_visible_mask(&notes, 60) == 1u);
    CHECK(ts_note_bank_visible_mask(&notes, 84) == 0u);
    ts_note_bank_sync(&notes, &restored, 48000);
    CHECK(notes.voices[0].midi_note == 48 && notes.voices[1].midi_note == 60 &&
          fabs(notes.voices[0].pitch - 0.5) < 0.0001 &&
          fabs(notes.voices[1].pitch - 1.0) < 0.0001);
    ts_note_bank_clear(&notes);
    CHECK(ts_note_bank_start_tuned_at(
              &notes, &restored, &restored.tuning, TS_AUDITION_CURRENT,
              5, 48, 1, 48000) == TS_NOTE_STARTED);
    CHECK(ts_note_bank_start_tuned_at(
              &notes, &restored, &restored.tuning, TS_AUDITION_CURRENT,
              9, 48, 1, 48000) == TS_NOTE_STARTED);
    CHECK(ts_note_bank_visible_mask(&notes, 48) == ((1u << 5) | (1u << 9)));
    CHECK(ts_note_bank_visible_mask(&notes, 84) == 0u);
    CHECK(ts_note_bank_count(&notes) == 2);
    CHECK(ts_note_bank_start_tuned_at(
              &notes, &restored, &restored.tuning, TS_AUDITION_CURRENT,
              5, 84, 1, 48000) == TS_NOTE_STARTED);
    CHECK(ts_note_bank_start_tuned_at(
              &notes, &restored, &restored.tuning, TS_AUDITION_CURRENT,
              9, 84, 1, 48000) == TS_NOTE_STARTED);
    CHECK(ts_note_bank_visible_mask(&notes, 84) == ((1u << 5) | (1u << 9)));
    CHECK(ts_note_bank_count(&notes) == 4);
    ts_note_bank_clear(&notes);
    CHECK(ts_note_bank_start_sample(
              &notes, &restored.current, &restored.tuning,
              0, TS_KEYBOARD_BASE_NOTE, 0, 48000) == TS_NOTE_STARTED);
    CHECK(ts_note_bank_start_sample(
              &notes, &restored.current, &restored.tuning,
              4, TS_KEYBOARD_BASE_NOTE, 0, 48000) == TS_NOTE_STARTED);
    CHECK(ts_note_bank_synth_count(&notes) == 2);
    CHECK(ts_note_bank_latched_synth_count(&notes) == 0);
    CHECK(ts_note_bank_latch_active_synth(&notes) == 2);
    CHECK(ts_note_bank_latched_synth_count(&notes) == 2);
    ts_note_bank_release(&notes, 0);
    CHECK(ts_note_bank_synth_count(&notes) == 2);
    CHECK(ts_note_bank_release_latched_synth(&notes) == 2);
    CHECK(ts_note_bank_count(&notes) == 0);
    remove("test-recipe.tsr");
    remove("test-roundtrip.wav");

    {
        TsConfig config;
        TsConfig reopened;
        ts_config_init(&config);
        CHECK(config.rotate_wheel_fine == 5);
        CHECK(config.rotate_wheel_coarse == 50);
        CHECK(config.reference_tone_volume == 50);
        CHECK(config.playhead_zero_snap == 1);
        CHECK(config.ripple_cut_crop_canvas == 0);
        config.playhead_zero_snap = 0;
        config.ripple_cut_crop_canvas = 1;
        config.reference_tone_volume = 73;
        snprintf(config.sample_path, sizeof(config.sample_path), "/samples/drums");
        snprintf(config.fasttracker_path, sizeof(config.fasttracker_path),
                 "/opt/ft2 tapehead/ft2-clone");
        snprintf(config.exchange_path, sizeof(config.exchange_path), "/samples/handoff");
        snprintf(config.cdp_bin_path, sizeof(config.cdp_bin_path), "/opt/cdp/bin");
        config.dsp_factory_overridden[4] = 1;
        config.dsp_factory_controls[4][0] = 0.11f;
        config.dsp_factory_controls[4][1] = 0.22f;
        config.dsp_factory_controls[4][2] = 0.33f;
        config.dsp_factory_controls[4][3] = 0.44f;
        config.dsp_factory_overridden[23] = 1;
        config.dsp_factory_controls[23][0] = 0.66f;
        config.dsp_factory_controls[23][1] = 0.55f;
        config.dsp_factory_controls[23][2] = 0.44f;
        config.dsp_factory_controls[23][3] = 0.33f;
        config.cdp_factory_overridden[17] = 1;
        config.cdp_factory_controls[17][0] = 23.0f;
        config.cdp_factory_controls[17][1] = 2.0f;
        config.cdp_factory_controls[17][2] = -3.0f;
        config.cdp_factory_controls[17][3] = 0.75f;
        config.cdp_factory_mix[17] = 0.9f;
        config.cdp_factory_seed[17] = UINT64_C(123456789);
        CHECK(ts_config_save(&config, "test-tapesister.ini", error, sizeof(error)));
        ts_config_init(&reopened);
        CHECK(ts_config_load(&reopened, "test-tapesister.ini", error, sizeof(error)));
        CHECK(strcmp(reopened.sample_path, config.sample_path) == 0);
        CHECK(strcmp(reopened.fasttracker_path, config.fasttracker_path) == 0);
        CHECK(strcmp(reopened.exchange_path, config.exchange_path) == 0);
        CHECK(strcmp(reopened.cdp_bin_path, config.cdp_bin_path) == 0);
        CHECK(reopened.startup_welcome_sample == 1 &&
              reopened.startup_welcome_autoplay == 1);
        CHECK(reopened.rotate_wheel_fine == 5 && reopened.rotate_wheel_coarse == 50);
        CHECK(reopened.reference_tone_volume == 73);
        CHECK(reopened.playhead_zero_snap == 0);
        CHECK(reopened.ripple_cut_crop_canvas == 1);
        CHECK(reopened.dsp_factory_overridden[4]);
        CHECK(fabsf(reopened.dsp_factory_controls[4][0] - 0.11f) < 0.000001f &&
              fabsf(reopened.dsp_factory_controls[4][1] - 0.22f) < 0.000001f &&
              fabsf(reopened.dsp_factory_controls[4][2] - 0.33f) < 0.000001f &&
              fabsf(reopened.dsp_factory_controls[4][3] - 0.44f) < 0.000001f);
        CHECK(reopened.dsp_factory_overridden[23] &&
              fabsf(reopened.dsp_factory_controls[23][0] - 0.66f) < 0.000001f &&
              fabsf(reopened.dsp_factory_controls[23][1] - 0.55f) < 0.000001f &&
              fabsf(reopened.dsp_factory_controls[23][2] - 0.44f) < 0.000001f &&
              fabsf(reopened.dsp_factory_controls[23][3] - 0.33f) < 0.000001f);
        CHECK(reopened.cdp_factory_overridden[17] &&
              fabsf(reopened.cdp_factory_controls[17][0] - 23.0f) < 0.000001f &&
              fabsf(reopened.cdp_factory_controls[17][2] + 3.0f) < 0.000001f &&
              fabsf(reopened.cdp_factory_mix[17] - 0.9f) < 0.000001f &&
              reopened.cdp_factory_seed[17] == UINT64_C(123456789));
        CHECK(strcmp(ts_config_field_name(TS_CONFIG_FASTTRACKER_PATH),
                     "FASTTRACKER EXECUTABLE") == 0);
        CHECK(strcmp(ts_config_field_name(TS_CONFIG_CDP_BIN_PATH),
                     "CDP BIN PATH") == 0);
        CHECK(ts_config_field(&reopened, TS_CONFIG_CDP_BIN_PATH) ==
              reopened.cdp_bin_path);
        remove("test-tapesister.ini");
        {
            FILE *config_file = fopen("test-tapesister.ini", "wb");
            CHECK(config_file != NULL);
            if (config_file != NULL) {
                fputs("startup_welcome_sample=0\nstartup_welcome_autoplay=0\n",
                      config_file);
                fclose(config_file);
            }
            CHECK(ts_config_load(&reopened, "test-tapesister.ini", error, sizeof(error)));
            CHECK(!reopened.startup_welcome_sample && !reopened.startup_welcome_autoplay);
            CHECK(reopened.rotate_wheel_fine == 5 && reopened.rotate_wheel_coarse == 50);
            CHECK(reopened.reference_tone_volume == 50);
            CHECK(reopened.playhead_zero_snap == 1);
            CHECK(reopened.ripple_cut_crop_canvas == 0);
            config_file = fopen("test-tapesister.ini", "wb");
            CHECK(config_file != NULL);
            if (config_file != NULL) {
                fputs("rotate_wheel_fine=1\nrotate_wheel_coarse=20\n", config_file);
                fclose(config_file);
            }
            CHECK(ts_config_load(&reopened, "test-tapesister.ini", error, sizeof(error)));
            CHECK(reopened.rotate_wheel_fine == 1 && reopened.rotate_wheel_coarse == 20);
            config_file = fopen("test-tapesister.ini", "wb");
            CHECK(config_file != NULL);
            if (config_file != NULL) {
                fputs("rotate_wheel_fine=20\nrotate_wheel_coarse=100\n", config_file);
                fclose(config_file);
            }
            CHECK(ts_config_load(&reopened, "test-tapesister.ini", error, sizeof(error)));
            CHECK(reopened.rotate_wheel_fine == 20 && reopened.rotate_wheel_coarse == 100);
            config_file = fopen("test-tapesister.ini", "wb");
            CHECK(config_file != NULL);
            if (config_file != NULL) {
                fputs("rotate_wheel_fine=-99\nrotate_wheel_coarse=999\n"
                      "reference_tone_volume=999\n", config_file);
                fclose(config_file);
            }
            CHECK(ts_config_load(&reopened, "test-tapesister.ini", error, sizeof(error)));
            CHECK(reopened.rotate_wheel_fine == 1 && reopened.rotate_wheel_coarse == 100);
            CHECK(reopened.reference_tone_volume == 100);
            config_file = fopen("test-tapesister.ini", "wb");
            CHECK(config_file != NULL);
            if (config_file != NULL) {
                fputs("ripple_cut_crop_canvas=2\n", config_file);
                fclose(config_file);
            }
            CHECK(!ts_config_load(&reopened, "test-tapesister.ini", error, sizeof(error)));
            remove("test-tapesister.ini");
            config_file = fopen("test-tapesister.ini", "wb");
            CHECK(config_file != NULL);
            if (config_file != NULL) {
                fputs("startup_welcome_sample=yes\n", config_file);
                fclose(config_file);
            }
            CHECK(!ts_config_load(&reopened, "test-tapesister.ini", error, sizeof(error)));
            remove("test-tapesister.ini");
            config_file = fopen("test-tapesister.ini", "wb");
            CHECK(config_file != NULL);
            if (config_file != NULL) {
                fputs("DspPreset05=0.1,0.2,1.5,0.4\n", config_file);
                fclose(config_file);
            }
            CHECK(!ts_config_load(&reopened, "test-tapesister.ini",
                                  error, sizeof(error)));
            remove("test-tapesister.ini");
        }
        {
            TsUiState startup_ui;
            ts_ui_init(&startup_ui);
            startup_ui.startup_welcome_installed = 1;
            startup_ui.startup_welcome_autoplay = 1;
            CHECK(!ts_ui_request_startup_welcome(&startup_ui, 0, 1));
            CHECK(!startup_ui.startup_welcome_playback_requested);
            CHECK(ts_ui_request_startup_welcome(&startup_ui, 1, 1));
            CHECK(startup_ui.startup_welcome_playback_requested);
            CHECK(!ts_ui_request_startup_welcome(&startup_ui, 1, 1));
            CHECK(!ts_ui_request_startup_welcome(&startup_ui, 1, 0));
            ts_ui_init(&startup_ui);
            startup_ui.startup_welcome_installed = 1;
            startup_ui.startup_welcome_autoplay = 0;
            CHECK(!ts_ui_request_startup_welcome(&startup_ui, 1, 1));
            CHECK(startup_ui.startup_welcome_playback_requested);
        }
    }
    {
        char name[256];
        char first_path[512];
        char next_path[512];
        CHECK(ts_instrument_family_folder_name(&restored, name, sizeof(name)));
        CHECK(strstr(name, "_set") != NULL);
        CHECK(mkdir("test-handoff-root", 0700) == 0);
        CHECK(ts_instrument_next_family_path(
            &restored, "test-handoff-root", first_path, sizeof(first_path),
            error, sizeof(error)));
        CHECK(strstr(first_path, name) != NULL);
        CHECK(mkdir(first_path, 0700) == 0);
        CHECK(ts_instrument_next_family_path(
            &restored, "test-handoff-root", next_path, sizeof(next_path),
            error, sizeof(error)));
        CHECK(strcmp(first_path, next_path) != 0);
        CHECK(strstr(next_path, "_02") != NULL);
        CHECK(rmdir(first_path) == 0);
        CHECK(rmdir("test-handoff-root") == 0);
    }
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
        test_file = fopen("test-browser-process.tsp", "wb");
        CHECK(test_file != NULL);
        if (test_file != NULL) fclose(test_file);
        test_file = fopen("test-browser-ignore.txt", "wb");
        CHECK(test_file != NULL);
        if (test_file != NULL) fclose(test_file);

        ts_browser_init(&browser);
        CHECK(ts_browser_open(&browser, TS_BROWSER_LOAD_WAV, NULL));
        CHECK(browser_find(&browser, "test-browser-dir") >= 0);
        CHECK(browser_find(&browser, "test-browser-load.wav") >= 0);
        CHECK(browser_find(&browser, "test-browser-save.tsr") >= 0);
        CHECK(browser_find(&browser, "test-browser-process.tsp") >= 0);
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
        CHECK(ts_browser_open(&browser, TS_BROWSER_SAVE_PRESET, "my-texture"));
        CHECK(ts_browser_destination_path(&browser, path, sizeof(path)));
        CHECK(strstr(path, "my-texture.tsp") != NULL);
        ts_browser_set_filename(&browser, "named.tsr");
        CHECK(ts_browser_destination_path(&browser, path, sizeof(path)));
        CHECK(strstr(path, "named.tsr") != NULL);
        CHECK(strstr(path, "named.tsr.tsr") == NULL);
        ts_browser_set_filename(&browser, "safe");
        ts_browser_append_filename(&browser, "/name");
        CHECK(strcmp(browser.filename, "safename") == 0);
        ts_browser_move_filename_cursor(&browser, -4);
        ts_browser_append_filename(&browser, "-");
        CHECK(strcmp(browser.filename, "safe-name") == 0);
        ts_browser_backspace_filename(&browser);
        CHECK(strcmp(browser.filename, "safename") == 0);
        ts_browser_move_filename_cursor(&browser, 1);
        ts_browser_delete_filename(&browser);
        CHECK(strcmp(browser.filename, "safenme") == 0);
        ts_browser_set_filename_cursor(&browser, 999);
        CHECK(browser.filename_cursor == strlen(browser.filename));

        CHECK(ts_browser_open(&browser, TS_BROWSER_EXPORT_WAV, "current"));
        CHECK(ts_browser_destination_path(&browser, path, sizeof(path)));
        CHECK(strstr(path, "current.wav") != NULL);
        CHECK(ts_browser_open(&browser, TS_BROWSER_EXPORT_BANK, "metallic_family"));
        CHECK(ts_browser_destination_path(&browser, path, sizeof(path)));
        CHECK(strstr(path, "metallic_family") != NULL);
        CHECK(strstr(path, "metallic_family.wav") == NULL);
        CHECK(ts_browser_mode_allows_create_directory(TS_BROWSER_SAVE_RECIPE));
        CHECK(ts_browser_mode_allows_create_directory(TS_BROWSER_EXPORT_WAV));
        CHECK(!ts_browser_mode_allows_create_directory(TS_BROWSER_LOAD_WAV));
        CHECK(ts_browser_begin_create_directory(&browser));
        CHECK(browser.creating_directory && browser.filename[0] == '\0');
        ts_browser_set_filename(&browser, "test-browser-created");
        CHECK(ts_browser_create_directory(&browser));
        CHECK(!browser.creating_directory);
        CHECK(strstr(browser.directory, "test-browser-created") != NULL);
        CHECK(strcmp(browser.filename, "metallic_family") == 0);
        CHECK(ts_browser_parent(&browser));
        CHECK(rmdir("test-browser-created") == 0);
        CHECK(ts_browser_begin_create_directory(&browser));
        ts_browser_set_filename(&browser, "bad:name");
        CHECK(!ts_browser_create_directory(&browser));
        ts_browser_cancel_create_directory(&browser);
        CHECK(strcmp(browser.filename, "metallic_family") == 0);
        CHECK(!ts_browser_mode_edits_filename(TS_BROWSER_LOAD_WAV));
        CHECK(ts_browser_mode_edits_filename(TS_BROWSER_EXPORT_WAV));
        CHECK(ts_browser_mode_selects_config(TS_BROWSER_SELECT_SAMPLE_DIRECTORY));
        CHECK(ts_browser_mode_selects_directory(TS_BROWSER_SELECT_EXCHANGE_DIRECTORY));
        CHECK(ts_browser_mode_selects_config(TS_BROWSER_SELECT_CDP_BIN_DIRECTORY));
        CHECK(ts_browser_mode_selects_directory(TS_BROWSER_SELECT_CDP_BIN_DIRECTORY));
        CHECK(!ts_browser_mode_selects_directory(
                  TS_BROWSER_SELECT_FASTTRACKER_EXECUTABLE));
        CHECK(strcmp(ts_browser_mode_title(TS_BROWSER_SELECT_SAMPLE_DIRECTORY),
                     "SELECT SAMPLE FOLDER") == 0);
        CHECK(strcmp(ts_browser_mode_title(TS_BROWSER_SELECT_CDP_BIN_DIRECTORY),
                     "SELECT CDP BIN FOLDER") == 0);
        CHECK(ts_browser_open(&browser, TS_BROWSER_SELECT_SAMPLE_DIRECTORY, NULL));
        CHECK(browser_find(&browser, "test-browser-dir") >= 0);
        CHECK(browser_find(&browser, "test-browser-load.wav") < 0);
        CHECK(browser_find(&browser, "test-browser-ignore.txt") < 0);
        CHECK(!browser.filename_focus);
        CHECK(ts_browser_open(&browser,
                              TS_BROWSER_SELECT_FASTTRACKER_EXECUTABLE, NULL));
        CHECK(browser_find(&browser, "test-browser-load.wav") >= 0);
        CHECK(browser_find(&browser, "test-browser-ignore.txt") >= 0);
        CHECK(!browser.filename_focus);
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
        remove("test-browser-process.tsp");
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
    ui.fx_page = TS_FX_TUNE;
    CHECK(ts_ui_audition_tuning(&ui, &committed) == &committed.tuning);
    CHECK(ts_ui_display_tuning(&ui, &committed) == &committed.audible_tuning);
    ui.pitch_suggestion.root_note = 71;
    ui.pitch_suggestion.fine_tune_cents = 8.0f;
    ui.has_pitch_suggestion = 1;
    CHECK(ts_ui_audition_tuning(&ui, &committed) == &committed.tuning);
    CHECK(ts_ui_display_tuning(&ui, &committed) == &committed.audible_tuning);
    CHECK(committed.tuning.root_note != ui.pitch_suggestion.root_note);
    ts_ui_render(&fb, &ui, &committed);
    CHECK(framebuffer_contains(&fb, 0xff147dffu));
    CHECK(framebuffer_contains(&fb, 0xff2d0039u));
    ts_ui_init(&ui);
    CHECK(ui.keyboard_octave == 4 && ts_ui_keyboard_base_note(&ui) == 60);
    CHECK(ui.tune_reference.root_note == 60 &&
          ui.tune_reference.fine_tune_cents == 0.0f);
    CHECK(ts_ui_keyboard_shift_semitone(&ui, 1) == 61 &&
          ts_ui_keyboard_base_note(&ui) == 61);
    CHECK(ts_ui_keyboard_shift_semitone(&ui, -1) == 60);
    CHECK(ts_ui_keyboard_cycle_octave(&ui, 1) == 5 &&
          ts_ui_keyboard_base_note(&ui) == 72);
    CHECK(ts_ui_keyboard_set_octave(&ui, 7) == 7 &&
          ts_ui_keyboard_cycle_octave(&ui, 1) == 0 &&
          ts_ui_keyboard_base_note(&ui) == 12);
    CHECK(ts_ui_keyboard_set_octave(&ui, 3) == 3);
    CHECK(ts_ui_right_drag_playhead_frame(100, 140, 96, 144, 200) == 96);
    CHECK(ts_ui_right_drag_playhead_frame(140, 100, 96, 144, 200) == 144);
    CHECK(ts_ui_right_drag_playhead_frame(200, 100, 96, 200, 200) == 199);
    ts_instrument_set_selection(&imported, imported.current.frames / 4, imported.current.frames / 2);
    ts_ui_render(&fb, &ui, &imported);
    CHECK(fb.pixels[0] != 0);
    CHECK(framebuffer_contains(&fb, 0xff1c1c1cu));
    CHECK(framebuffer_contains(&fb, 0xffffe700u));
    CHECK(framebuffer_contains(&fb, 0xff2d0039u));
    CHECK(framebuffer_contains(&fb, 0xff009ee3u));
    {
        int selection_x = TS_WAVE_X +
            (int)((imported.selection_first - imported.view_first) * TS_WAVE_W /
                  (imported.view_last - imported.view_first));
        int selection_last_x = TS_WAVE_X +
            (int)((imported.selection_last - imported.view_first) * TS_WAVE_W /
                  (imported.view_last - imported.view_first));
        ui.palette.colors[TS_PALETTE_BLOCK_MARK] = 0xff102132u;
        ui.palette.colors[TS_PALETTE_WAVE_SELECTION] = 0xff405162u;
        ts_ui_render(&fb, &ui, &imported);
        CHECK(framebuffer_color_count(&fb, 0xff405162u, selection_x, TS_WAVE_Y,
                                      selection_last_x - selection_x,
                                      TS_WAVE_H) > 100);
        CHECK(framebuffer_color_count(&fb, 0xff102132u, selection_x, TS_WAVE_Y,
                                      selection_last_x - selection_x,
                                      TS_WAVE_H) == 0);
        CHECK(framebuffer_color_count(&fb, 0xff009ee3u, selection_x, TS_WAVE_Y,
                                      selection_last_x - selection_x,
                                      TS_WAVE_H) > 0);
        ui.config_open = 1;
        ts_ui_render(&fb, &ui, &imported);
        CHECK(fb.pixels[10 * TS_UI_WIDTH + 420] == 0xff102132u);
        ui.config_open = 0;
        ts_palette_default(&ui.palette);
    }
    {
        uint32_t saved_effect = ui.palette.colors[TS_PALETTE_PATTERN_EFFECT];
        int label_x = TS_WAVE_X +
                      (int)((imported.selection_first - imported.view_first) * TS_WAVE_W /
                            (imported.view_last - imported.view_first)) + 4;
        int effect_pixels = 0;
        ui.palette.colors[TS_PALETTE_PATTERN_EFFECT] = 0xff010203u;
        ts_ui_render(&fb, &ui, &imported);
        for (int y = TS_WAVE_Y + 5; y < TS_WAVE_Y + 12; ++y)
            for (int x = label_x; x < label_x + 84 && x < TS_WAVE_X + TS_WAVE_W; ++x)
                if (fb.pixels[y * TS_UI_WIDTH + x] == 0xff010203u) ++effect_pixels;
        CHECK(effect_pixels > 0);
        ui.palette.colors[TS_PALETTE_PATTERN_EFFECT] = saved_effect;
    }
    CHECK(ui.warp_amount == 0.0f);
    ui.warp_amount = 0.75f;
    ts_ui_render(&fb, &ui, &imported);
    CHECK(fb.pixels[247 * TS_UI_WIDTH + 295] == 0xffffd265u);
    CHECK(fb.pixels[247 * TS_UI_WIDTH + 325] == 0xff0c0c0cu);
    {
        float *saved = (float *)malloc(imported.current.frames * sizeof(float));
        CHECK(saved != NULL);
        if (saved != NULL) {
            memcpy(saved, imported.current.data, imported.current.frames * sizeof(float));
            for (size_t i = 0; i < imported.current.frames; ++i)
                imported.current.data[i] = (i & 1u) ? 8.0f : -8.0f;
            ts_instrument_clear_selection(&imported);
            ts_ui_render(&fb, &ui, &imported);
            for (int x = TS_WAVE_X; x < TS_WAVE_X + TS_WAVE_W; ++x) {
                CHECK(fb.pixels[(TS_WAVE_Y - 1) * TS_UI_WIDTH + x] != 0xffffe700u);
                CHECK(fb.pixels[(TS_WAVE_Y + TS_WAVE_H) * TS_UI_WIDTH + x] !=
                      0xffffe700u);
            }
            memcpy(imported.current.data, saved, imported.current.frames * sizeof(float));
            free(saved);
            ts_instrument_set_selection(&imported, imported.current.frames / 4,
                                        imported.current.frames / 2);
        }
    }
    {
        int zero_pixels = 0;
        int middle = TS_WAVE_Y + TS_WAVE_H / 2;
        for (int x = TS_WAVE_X; x < TS_WAVE_X + TS_WAVE_W; ++x)
            if (fb.pixels[middle * TS_UI_WIDTH + x] == 0xffff1ce7u) ++zero_pixels;
        CHECK(zero_pixels > 0);
    }
    {
        int ghost_pixels = 0;
        ui.tape_dragging = 1;
        ui.tape_source_first = imported.selection_first;
        ui.tape_source_last = imported.selection_last;
        ui.tape_destination = (int64_t)(imported.current.frames * 3u / 5u);
        ts_ui_render(&fb, &ui, &imported);
        for (int y = TS_WAVE_Y; y < TS_WAVE_Y + TS_WAVE_H; ++y)
            for (int x = TS_WAVE_X; x < TS_WAVE_X + TS_WAVE_W; ++x)
                if (fb.pixels[y * TS_UI_WIDTH + x] == 0xff35ffffu) ++ghost_pixels;
        CHECK(ghost_pixels > 1000);
        ui.tape_dragging = 0;
    }
    ui.show_keyboard = 0;
    CHECK(ts_instrument_bank_capture(&imported, 1, TS_BANK_CAPTURE_CURRENT,
                                     error, sizeof(error)));
    ts_ui_render(&fb, &ui, &imported);
    CHECK(ts_ui_bank_slot_from_point(46, 341) == 0);
    CHECK(fb.pixels[340 * TS_UI_WIDTH + 20] != 0xff1c1c1cu);
    {
        int selected = imported.selected_slot;
        int outline_x = 8 + (selected % 8) * 77;
        int outline_y = 328 + (selected / 8) * 25;
        uint32_t low_contrast;
        uint32_t high_contrast;
        ui.palette.colors[TS_PALETTE_MOUSE] = 0xff123456u;
        ui.palette.colors[TS_PALETTE_ACTIVE_TILE] = 0xff654321u;
        ui.palette.buttons_contrast = 1;
        ts_ui_render(&fb, &ui, &imported);
        CHECK(fb.pixels[outline_y * TS_UI_WIDTH + outline_x] == 0xff654321u);
        low_contrast = fb.pixels[330 * TS_UI_WIDTH + 100];
        CHECK(low_contrast != 0xff123456u);
        ui.palette.buttons_contrast = 100;
        ts_ui_render(&fb, &ui, &imported);
        high_contrast = fb.pixels[330 * TS_UI_WIDTH + 100];
        CHECK(high_contrast != low_contrast && high_contrast != 0xff123456u);
        CHECK(fb.pixels[outline_y * TS_UI_WIDTH + outline_x] == 0xff654321u);
        ts_palette_default(&ui.palette);
    }
    for (int slot = 0; slot < TS_BANK_SLOT_COUNT; ++slot) {
        int x = 10 + (slot % 8) * 77 + 36;
        int y = 330 + (slot / 8) * 25 + 12;
        CHECK(ts_ui_bank_slot_from_point(x, y) == slot);
    }
    CHECK(ts_ui_bank_action(0, 0) == TS_UI_BANK_ACTION_AUDITION);
    CHECK(ts_ui_bank_action(0, TS_UI_BANK_MOD_SHIFT) ==
          TS_UI_BANK_ACTION_CAPTURE_CURRENT);
    CHECK(ts_ui_bank_action(0, TS_UI_BANK_MOD_ALT) ==
          TS_UI_BANK_ACTION_CAPTURE_LOOP);
    CHECK(ts_ui_bank_action(0, TS_UI_BANK_MOD_CTRL) ==
          TS_UI_BANK_ACTION_CAPTURE_SELECTION);
    CHECK(ts_ui_bank_action(0, TS_UI_BANK_MOD_SHIFT | TS_UI_BANK_MOD_CTRL) ==
          TS_UI_BANK_ACTION_CLONE);
    CHECK(ts_ui_bank_action(0, TS_UI_BANK_MOD_CTRL | TS_UI_BANK_MOD_ALT) ==
          TS_UI_BANK_ACTION_TOGGLE_LOCK);
    CHECK(ts_ui_new_page_button_from_point(480, 320));
    CHECK(!ts_ui_new_page_button_from_point(535, 320));
    CHECK(ts_ui_bank_action(1, 0) == TS_UI_BANK_ACTION_RENAME);
    CHECK(ts_ui_bank_action(1, TS_UI_BANK_MOD_SHIFT) == TS_UI_BANK_ACTION_CLEAR);
    CHECK(ts_ui_bank_action(0, TS_UI_BANK_MOD_SHIFT | TS_UI_BANK_MOD_ALT) ==
          TS_UI_BANK_ACTION_INVALID);
    ui.show_keyboard = 0;
    ui.show_recipes = 1;
    ts_ui_render(&fb, &ui, &imported);
    CHECK(ts_ui_cdp_slot_from_point(46, 321) == -1);
    CHECK(ts_ui_cdp_slot_from_point(46, 341) == 0);
    CHECK(ts_ui_cdp_slot_from_point(123, 341) == 1);
    CHECK(ts_ui_cdp_slot_from_point(46, 366) == 8);
    ui.show_recipes = 0;
    ui.show_ingredients = 1;
    ts_ui_render(&fb, &ui, &imported);
    CHECK(ts_ui_recipe_slot_from_point(46, 341) == 0);
    CHECK(ui.recipes.slots[0].occupied && ui.recipes.slots[0].factory);
    CHECK(fb.pixels[332 * TS_UI_WIDTH + 12] == 0xff18ff00u);
    ui.show_ingredients = 0;
    {
        TsPostEditKind action;
        CHECK(ts_ui_tape_action(0, TS_UI_BANK_MOD_SHIFT, &action) &&
              action == TS_POST_COPY_MIX);
        CHECK(ts_ui_tape_action(1, TS_UI_BANK_MOD_SHIFT, &action) &&
              action == TS_POST_COPY_OVERWRITE);
        CHECK(ts_ui_tape_action(0, TS_UI_BANK_MOD_CTRL, &action) &&
              action == TS_POST_MOVE_MIX);
        CHECK(ts_ui_tape_action(1, TS_UI_BANK_MOD_CTRL, &action) &&
              action == TS_POST_MOVE_OVERWRITE);
        CHECK(!ts_ui_tape_action(0, 0, &action));
        CHECK(!ts_ui_tape_action(0, TS_UI_BANK_MOD_SHIFT | TS_UI_BANK_MOD_CTRL,
                                 &action));
    }
    {
        uint64_t current_waveform;
        uint64_t bank_waveform;
        ui.bank_view_slot = -1;
        ts_ui_render(&fb, &ui, &restored);
        current_waveform = waveform_hash(&fb);
        ui.bank_view_slot = 2;
        ts_ui_render(&fb, &ui, &restored);
        bank_waveform = waveform_hash(&fb);
        CHECK(bank_waveform != current_waveform);
        CHECK(fb.pixels[340 * TS_UI_WIDTH + 231] == 0xff147dffu);
        ui.bank_view_slot = -1;
        ts_ui_render(&fb, &ui, &restored);
    }
    ui.renaming_bank_slot = 2;
    ui.text_cursor_visible = 1;
    snprintf(ui.bank_rename, sizeof(ui.bank_rename), "TAIL");
    ui.bank_rename_cursor = strlen(ui.bank_rename);
    ts_ui_render(&fb, &ui, &restored);
    CHECK(fb.pixels[338 * TS_UI_WIDTH + 146] == 0xffffd265u);
    ui.renaming_bank_slot = -1;
    ui.bank_rename[0] = '\0';
    ui.renaming_recipe_slot = 8;
    snprintf(ui.recipe_rename, sizeof(ui.recipe_rename), "DRONE BED");
    ui.recipe_rename_cursor = 5;
    ts_ui_render(&fb, &ui, &restored);
    CHECK(fb.pixels[338 * TS_UI_WIDTH + 178 + 5 * 6] == 0xffffd265u);
    ui.renaming_recipe_slot = -1;
    ui.recipe_rename[0] = '\0';
    ui.export_choice_open = 1;
    ts_ui_render(&fb, &ui, &restored);
    CHECK(fb.pixels[180 * TS_UI_WIDTH + 175] == 0xff5d555du);
    ui.export_choice_open = 0;
    ui.exchange_dialog = TS_UI_EXCHANGE_SEND;
    ui.exchange_item_count = 3;
    ts_ui_render(&fb, &ui, &restored);
    CHECK(ts_ui_exchange_action_from_point(TS_UI_EXCHANGE_SEND, 160, 120) ==
          TS_UI_EXCHANGE_ACTION_SEND_ONE_INSTRUMENT);
    CHECK(ts_ui_exchange_action_from_point(TS_UI_EXCHANGE_SEND, 300, 120) ==
          TS_UI_EXCHANGE_ACTION_SEND_SEPARATE_INSTRUMENTS);
    CHECK(ts_ui_exchange_action_from_point(TS_UI_EXCHANGE_SEND, 430, 120) ==
          TS_UI_EXCHANGE_ACTION_SEND_ALL_PAGES);
    CHECK(ts_ui_exchange_action_from_point(TS_UI_EXCHANGE_SEND, 180, 166) ==
          TS_UI_EXCHANGE_ACTION_CHECK_INBOX);
    CHECK(ts_ui_exchange_action_from_point(TS_UI_EXCHANGE_SEND, 300, 166) ==
          TS_UI_EXCHANGE_ACTION_TOGGLE_NEW_INSTANCE);
    CHECK(ts_ui_exchange_action_from_point(TS_UI_EXCHANGE_SEND, 440, 166) ==
          TS_UI_EXCHANGE_ACTION_LATER);
    CHECK(fb.pixels[116 * TS_UI_WIDTH + 130] == 0xff5d555du);
    ui.exchange_dialog = TS_UI_EXCHANGE_RECEIVE;
    ui.exchange_layout = TS_EXCHANGE_LAYOUT_SEPARATE_INSTRUMENTS;
    snprintf(ui.exchange_name, sizeof(ui.exchange_name), "tapehead_to_tapesister_01");
    ts_ui_render(&fb, &ui, &restored);
    CHECK(ts_ui_exchange_action_from_point(TS_UI_EXCHANGE_RECEIVE, 210, 166) ==
          TS_UI_EXCHANGE_ACTION_IMPORT);
    CHECK(ts_ui_exchange_action_from_point(TS_UI_EXCHANGE_RECEIVE, 360, 166) ==
          TS_UI_EXCHANGE_ACTION_LATER);
    ui.exchange_dialog = TS_UI_EXCHANGE_NONE;
    ui.load_selection_choice_open = 1;
    snprintf(ui.load_selection_name, sizeof(ui.load_selection_name),
             "selection-source.wav");
    ts_ui_render(&fb, &ui, &restored);
    CHECK(ts_ui_load_selection_action_from_point(160, 140) ==
          TS_UI_LOAD_SELECTION_PASTE);
    CHECK(ts_ui_load_selection_action_from_point(300, 140) ==
          TS_UI_LOAD_SELECTION_FIT);
    CHECK(ts_ui_load_selection_action_from_point(420, 140) ==
          TS_UI_LOAD_SELECTION_CANCEL);
    CHECK(ts_ui_load_selection_action_from_point(260, 140) ==
          TS_UI_LOAD_SELECTION_NONE);
    CHECK(fb.pixels[136 * TS_UI_WIDTH + 149] == 0xff5d555du);
    ui.load_selection_choice_open = 0;
    ui.exit_confirm_open = 1;
    ui.exit_has_unsaved = 1;
    ts_ui_render(&fb, &ui, &restored);
    CHECK(fb.pixels[192 * TS_UI_WIDTH + 175] == 0xff5d555du);
    CHECK(fb.pixels[192 * TS_UI_WIDTH + 327] == 0xff2d0039u);
    ui.exit_confirm_open = 0;
    {
        TsFramebuffer normal;
        int differences;
        ui.config_open = 0;
        ui.palette_open = 0;
        ts_ui_render(&normal, &ui, &restored);
        ui.config_open = 1;
        ui.config_field = TS_CONFIG_FASTTRACKER_PATH;
        snprintf(ui.config.fasttracker_path, sizeof(ui.config.fasttracker_path),
                 "/opt/ft2/ft2-clone");
        ui.config_cursor = strlen(ui.config.fasttracker_path);
        ui.text_cursor_visible = 1;
        ts_ui_render(&fb, &ui, &restored);
        CHECK(fb.pixels[(TS_CONFIG_FIELD_Y + TS_CONFIG_FIELD_STEP_Y + 3) *
                        TS_UI_WIDTH + TS_CONFIG_FIELD_X + 6 +
                        (int)ui.config_cursor * 6] == 0xffffd265u);
        differences = framebuffer_diff_count(&normal, &fb, 0, 205,
                                             TS_UI_WIDTH, TS_UI_HEIGHT - 205);
        CHECK(differences == 0);
        CHECK(framebuffer_diff_count(&normal, &fb, TS_MODAL_PANEL_X,
                                     TS_MODAL_PANEL_Y, TS_MODAL_PANEL_W,
                                     TS_MODAL_PANEL_H) > 1000);
        ui.config_open = 0;
        ts_ui_render(&normal, &ui, &restored);
        ui.palette_open = 1;
        ui.palette_entry = TS_PALETTE_WAVE_SELECTION;
        ui.palette_channel = 2;
        ts_ui_render(&fb, &ui, &restored);
        differences = framebuffer_diff_count(&normal, &fb, 0, 205,
                                             TS_UI_WIDTH, TS_UI_HEIGHT - 205);
        CHECK(differences == 0);
        CHECK(framebuffer_diff_count(&normal, &fb, TS_MODAL_PANEL_X,
                                     TS_MODAL_PANEL_Y, TS_MODAL_PANEL_W,
                                     TS_MODAL_PANEL_H) > 1000);
        CHECK(framebuffer_diff_count(&normal, &fb, 0, 204,
                                     TS_UI_WIDTH, 1) == 0);
        ui.palette_open = 0;
        ts_ui_render(&normal, &ui, &restored);
        ui.load_selection_choice_open = 1;
        snprintf(ui.load_selection_name, sizeof(ui.load_selection_name),
                 "selection-source.wav");
        ts_ui_render(&fb, &ui, &restored);
        CHECK(framebuffer_diff_count(&normal, &fb, 0, 205,
                                     TS_UI_WIDTH, TS_UI_HEIGHT - 205) == 0);
        CHECK(framebuffer_diff_count(&normal, &fb, 126, 78, 388, 108) > 1000);
        ui.load_selection_choice_open = 0;
    }
    ui.show_keyboard = 1;
    {
        uint64_t current_waveform = waveform_hash(&fb);
        ui.audition_source = TS_AUDITION_PARENT;
        ts_ui_render(&fb, &ui, &imported);
        CHECK(waveform_hash(&fb) != current_waveform);
        ui.audition_source = TS_AUDITION_CURRENT;
    }
    ts_ui_reset_parent_view(&ui, imported.parent.frames);
    ui.audition_source = TS_AUDITION_PARENT;
    ts_ui_render(&fb, &ui, &imported);
    {
        uint64_t full_parent = waveform_hash(&fb);
        size_t anchor = imported.parent.frames / 3u;
        CHECK(ts_ui_zoom_parent_view(&ui, imported.parent.frames,
                                     anchor, 0.5f, 0.5f));
        CHECK(ui.parent_view_last - ui.parent_view_first == imported.parent.frames / 2u);
        CHECK(ts_ui_parent_frame_from_x(&ui, imported.parent.frames, 599, 600) ==
              ui.parent_view_last);
        CHECK(ts_ui_parent_frame_from_x(&ui, imported.parent.frames, 300, 600) + 1u >=
              anchor);
        ts_ui_render(&fb, &ui, &imported);
        CHECK(waveform_hash(&fb) != full_parent);
        CHECK(ts_ui_pan_parent_view(&ui, imported.parent.frames,
                                    (ptrdiff_t)((ui.parent_view_last -
                                                 ui.parent_view_first) / 8u)));
    }
    ui.audition_source = TS_AUDITION_CURRENT;
    ui.active_notes = (1u << 0) | (1u << 1);
    ts_ui_render(&fb, &ui, &imported);
    CHECK(fb.pixels[340 * TS_UI_WIDTH + 20] == 0xffffd265u);
    CHECK(fb.pixels[340 * TS_UI_WIDTH + 50] == 0xffff1ce7u);
    ui.active_notes = 1u << 4;
    ts_ui_render(&fb, &ui, &imported);
    CHECK(fb.pixels[370 * TS_UI_WIDTH + 116] == 0xffffd265u);
    CHECK(fb.pixels[370 * TS_UI_WIDTH + 73] == 0xffdcd8cfu);
    ui.active_notes = 0;
    ui.playback_active = 0;
    ui.audition_source = TS_AUDITION_CURRENT;
    ts_instrument_show_all(&imported);
    ts_instrument_set_playhead(&imported, imported.current.frames / 3u);
    ts_ui_render(&fb, &ui, &imported);
    CHECK(framebuffer_color_count(&fb, 0xffffd265u,
                                  TS_WAVE_X + TS_WAVE_W / 3 - 2,
                                  TS_WAVE_Y, 5, 4) > 0);
    {
        uint64_t before_stretch_readout = waveform_hash(&fb);
    ui.has_stretch_readout = 1;
    ui.stretch_pitch_semitones = -1.0f;
    ui.stretch_duration_ratio = powf(2.0f, 1.0f / 12.0f);
    ts_ui_render(&fb, &ui, &imported);
        CHECK(waveform_hash(&fb) != before_stretch_readout);
    }
    ui.has_stretch_readout = 0;
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
    ts_ui_reset_parent_view(&ui, imported.parent.frames);
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
    {
        const int white_semitones[14] = {0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19, 21, 23};
        const int black_after[10] = {0, 1, 3, 4, 5, 7, 8, 10, 11, 12};
        const int black_semitones[10] = {1, 3, 6, 8, 10, 13, 15, 18, 20, 22};
        for (int i = 0; i < 14; ++i)
            CHECK(ts_ui_key_from_point(10 + i * 43 + 21, 370) == white_semitones[i]);
        for (int i = 0; i < 10; ++i)
            CHECK(ts_ui_key_from_point(10 + (black_after[i] + 1) * 43, 345) ==
                  black_semitones[i]);
    }
    CHECK(ts_ui_key_from_point(0, 0) == -1);

    ui.fx_page = TS_FX_FAMILY;
    ui.show_keyboard = 0;
    ui.show_recipes = 0;
    ui.bank_view_slot = 1;
    ts_ui_render(&fb, &ui, &family);
    CHECK(fb.pixels[349 * TS_UI_WIDTH + 89] == 0xff18ff00u);
    {
        uint32_t visible_bank_pixel = fb.pixels[340 * TS_UI_WIDTH + 20];

        ts_fm_patch_from_recipe(&family.generator, &ui.fm_patch);
        ui.fm_open = 1;
        ui.fm_page = TS_FM_PAGE_LFO_DEPTH;
        ui.fm_preview_sample = &family.current;
        ui.fm_patch.active_mask = 0u;
        CHECK(ts_fm_set_control_normalized(&ui.fm_patch, ui.fm_page, 0, 0.5f));
        ui.playback_active = 1;
        ui.playhead_sample = &family.current;
        ui.playhead_frame = family.current.frames / 4u;
        ui.playhead_frames = family.current.frames;
        family.family_mutation = 0.64f;
        ts_ui_render(&fb, &ui, &family);
        CHECK(fb.pixels[70 * TS_UI_WIDTH + 22 + 596 / 4] == 0xffff1ce7u);
        CHECK(fb.pixels[176 * TS_UI_WIDTH + 25] == 0xff2d0039u);
        CHECK(fb.pixels[340 * TS_UI_WIDTH + 20] == visible_bank_pixel);
        CHECK(ts_ui_fm_action_from_point(40, 260) == TS_UI_FM_ACTION_RANDOMIZE);
        CHECK(ts_ui_fm_action_from_point(130, 260) == TS_UI_FM_ACTION_BANK_MAKER);
        CHECK(ts_ui_fm_action_from_point(220, 260) == TS_UI_FM_ACTION_APPLY);
        CHECK(ts_ui_fm_action_from_point(300, 260) == TS_UI_FM_ACTION_AUDITION);
        CHECK(ts_ui_fm_action_from_point(400, 260) == TS_UI_FM_ACTION_HOLD);
        CHECK(ts_ui_fm_action_from_point(40, 286) == TS_UI_FM_ACTION_DRONE);
        CHECK(ts_ui_fm_action_from_point(160, 286) == TS_UI_FM_ACTION_EXTREME);
        CHECK(ts_ui_fm_action_from_point(250, 286) == TS_UI_FM_ACTION_CHAIN);
        CHECK(ts_ui_fm_action_from_point(470, 260) == TS_UI_FM_ACTION_BACK);
        CHECK(ts_ui_fm_range_contains(500, 286));
        CHECK(!ts_ui_fm_range_contains(250, 286));
        CHECK(ts_ui_fm_full_action_from_point(120, 284) ==
              TS_UI_FM_ACTION_OVERWRITE);
        CHECK(ts_ui_fm_full_action_from_point(250, 284) ==
              TS_UI_FM_ACTION_NEW_PAGE);
        CHECK(ts_ui_fm_full_action_from_point(420, 284) ==
              TS_UI_FM_ACTION_CANCEL_FULL);
        CHECK(ts_ui_fm_bank_action_from_point(120, 284) ==
              TS_UI_FM_ACTION_BANK_REPLACE);
        CHECK(ts_ui_fm_bank_action_from_point(250, 284) ==
              TS_UI_FM_ACTION_BANK_NEW_PAGE);
        CHECK(ts_ui_fm_bank_action_from_point(420, 284) ==
              TS_UI_FM_ACTION_BANK_CANCEL);
        ui.fm_page = TS_FM_PAGE_PITCH;
        CHECK(ts_ui_fm_action_from_point(120, 230) ==
              TS_UI_FM_ACTION_PITCH_LOCK);
        CHECK(ts_ui_fm_pitch_root_contains(250, 230));
        CHECK(ts_ui_fm_pitch_scale_contains(350, 230));
        CHECK(ts_ui_fm_action_from_point(500, 230) ==
              TS_UI_FM_ACTION_APPLY_PITCHES);
        ui.fm_open = 0;
        ui.fm_preview_sample = NULL;
        ui.playhead_sample = NULL;
        ui.playback_active = 0;
    }

    {
        TsInstrument workflow;
        TsFmPatch created_patch, varied_patch;
        uint64_t original_hash, exact_hash;
        int destination = -1;
        ts_instrument_init(&workflow);
        CHECK(ts_instrument_bank_clear_all(&workflow, error, sizeof(error)));
        CHECK(ts_instrument_select_bank(&workflow, 5, error, sizeof(error)));
        CHECK(ts_instrument_create_selected(&workflow, 0x12345678u, error, sizeof(error)));
        CHECK(workflow.selected_slot == 5 && workflow.bank[5].occupied);
        CHECK(workflow.bank[5].has_generator &&
              workflow.bank[5].generator.kind == TS_GENERATOR_FM);
        CHECK(ts_sample_hash(&workflow.current) == ts_sample_hash(&workflow.bank[5].sample));
        original_hash = ts_sample_hash(&workflow.bank[5].sample);
        ts_fm_patch_from_recipe(&workflow.bank[5].generator, &created_patch);
        workflow.family_mutation = 0.0f;
        CHECK(ts_instrument_vary_selected(&workflow, 0, &destination, error, sizeof(error)));
        CHECK(destination == 5 && ts_sample_hash(&workflow.bank[5].sample) == original_hash);
        CHECK(ts_sample_hash(&workflow.bank[5].edit_parent) ==
              ts_sample_hash(&workflow.current));
        CHECK(ts_instrument_vary_selected(&workflow, 1, &destination, error, sizeof(error)));
        CHECK(destination == 6 && workflow.selected_slot == 6);
        exact_hash = ts_sample_hash(&workflow.bank[6].sample);
        CHECK(exact_hash == original_hash);
        CHECK(ts_instrument_copy_selected(&workflow, 9, error, sizeof(error)));
        CHECK(workflow.selected_slot == 9 && ts_sample_hash(&workflow.bank[9].sample) == exact_hash);
        workflow.family_mutation = 0.75f;
        CHECK(ts_instrument_vary_selected(&workflow, 0, &destination, error, sizeof(error)));
        ts_fm_patch_from_recipe(&workflow.bank[9].generator, &varied_patch);
        CHECK(varied_patch.structure == created_patch.structure);
        CHECK(ts_sample_hash(&workflow.bank[9].sample) != exact_hash);
        CHECK(ts_sample_hash(&workflow.bank[6].sample) == exact_hash);
        workflow.bank[9].generator.fm_patch.drone_mode = 1;
        workflow.bank[9].generator.fm_patch.extreme_mode = 1;
        workflow.bank[9].generator.fm_patch.pitch_lock = 0;
        workflow.bank[9].generator.fm_patch.pitch_root = 8;
        workflow.bank[9].generator.fm_patch.pitch_scale = TS_FM_PITCH_SCALE_MINOR;
        ts_fm_patch_sanitize(&workflow.bank[9].generator.fm_patch);
        CHECK(ts_instrument_save_recipe(&workflow, "test-workflow.tsr", error, sizeof(error)));
        {
            TsInstrument workflow_loaded;
            ts_instrument_init(&workflow_loaded);
            CHECK(ts_instrument_load_recipe(&workflow_loaded, "test-workflow.tsr", error, sizeof(error)));
            CHECK(workflow_loaded.selected_slot == 9);
            CHECK(workflow_loaded.bank[9].occupied &&
                  workflow_loaded.bank[9].generator.kind == TS_GENERATOR_FM);
            CHECK(workflow_loaded.bank[9].generator.has_fm_patch);
            CHECK(memcmp(&workflow_loaded.bank[9].generator.fm_patch,
                         &workflow.bank[9].generator.fm_patch,
                         sizeof(TsFmPatch)) == 0);
            CHECK(ts_sample_hash(&workflow_loaded.bank[6].sample) == exact_hash);
            ts_instrument_free(&workflow_loaded);
        }
        remove("test-workflow.tsr");
        CHECK(ts_instrument_bank_clear_all(&workflow, error, sizeof(error)));
        CHECK(ts_instrument_select_bank(&workflow, 0, error, sizeof(error)));
        CHECK(ts_instrument_create_selected(&workflow, 99u, error, sizeof(error)));
        CHECK(workflow.bank[0].occupied && workflow.bank[0].generator.kind == TS_GENERATOR_FM);
        ts_instrument_free(&workflow);
    }
    {
        TsInstrument apply_route;
        TsFmPatch patch;
        uint64_t preserved_hash;
        int destination;
        ts_instrument_init(&apply_route);
        CHECK(ts_instrument_bank_clear_all(&apply_route, error, sizeof(error)));
        CHECK(ts_instrument_select_bank(&apply_route, 5, error, sizeof(error)));
        CHECK(ts_instrument_create_selected(&apply_route, 0x464d4150u,
                                            error, sizeof(error)));
        preserved_hash = ts_sample_hash(&apply_route.bank[5].sample);
        ts_fm_patch_from_recipe(&apply_route.bank[5].generator, &patch);
        patch.structure = (patch.structure + 1) % TS_FM_STRUCTURE_COUNT;
        destination = ts_instrument_bank_next_empty(&apply_route);
        CHECK(destination == 6);
        CHECK(ts_instrument_select_bank(&apply_route, destination,
                                        error, sizeof(error)));
        CHECK(ts_instrument_apply_fm_patch(&apply_route, &patch,
                                           error, sizeof(error)));
        CHECK(apply_route.selected_slot == 6 && apply_route.bank[6].occupied);
        CHECK(ts_sample_hash(&apply_route.bank[5].sample) == preserved_hash);
        ts_instrument_free(&apply_route);
    }
    {
        TsGeneratorRecipe recipe = generator(0x50495443u, TS_GENERATOR_FM);
        TsFmPatch patch, locked, tonal, applied;
        static const int d_major[12] = {
            0, 1, 1, 0, 1, 0, 1, 1, 0, 1, 0, 1
        };
        ts_fm_patch_from_recipe(&recipe, &patch);
        CHECK(patch.pitch_lock == 1 && patch.pitch_root == 0 &&
              patch.pitch_scale == TS_FM_PITCH_SCALE_MAJOR);
        patch.mutation_mask = TS_FM_MUTATE_PITCH;
        ts_fm_patch_vary(&patch, 0x11112222u, 1.0f, &locked);
        CHECK(memcmp(patch.ratios, locked.ratios,
                     sizeof(patch.ratios)) == 0);
        patch.pitch_lock = 0;
        patch.pitch_root = 2;
        patch.pitch_scale = TS_FM_PITCH_SCALE_MAJOR;
        ts_fm_patch_vary(&patch, 0x11112222u, 1.0f, &tonal);
        for (int voice = 0; voice < TS_FM_OPERATOR_COUNT; ++voice) {
            int semitone = (int)lrintf(12.0f * log2f(tonal.ratios[voice]));
            int pitch_class = (TS_KEYBOARD_BASE_NOTE + semitone) % 12;
            if (pitch_class < 0) pitch_class += 12;
            CHECK(d_major[pitch_class]);
        }
        patch.pitch_root = 11;
        CHECK(ts_fm_step_pitch_root(&patch, 1) && patch.pitch_root == 0);
        patch.pitch_scale = TS_FM_PITCH_SCALE_WHOLE_TONE;
        CHECK(ts_fm_step_pitch_scale(&patch, 1) &&
              patch.pitch_scale == TS_FM_PITCH_SCALE_CHROMATIC);
        CHECK(strcmp(ts_fm_pitch_scale_name(TS_FM_PITCH_SCALE_PENTATONIC),
                     "PENTA") == 0);
        applied = patch;
        applied.pitch_lock = 1;
        applied.pitch_root = 2;
        applied.pitch_scale = TS_FM_PITCH_SCALE_MAJOR;
        applied.active_mask = (1u << 0) | (1u << 2);
        applied.ratios[0] = powf(2.0f, 3.0f / 12.0f);
        applied.ratios[1] = powf(2.0f, 10.0f / 12.0f);
        applied.ratios[2] = powf(2.0f, 5.0f / 12.0f);
        CHECK(ts_fm_apply_pitch_scale(&applied) == 2);
        CHECK(fabsf(applied.ratios[0] - powf(2.0f, 2.0f / 12.0f)) < 0.0001f);
        CHECK(fabsf(applied.ratios[1] - powf(2.0f, 10.0f / 12.0f)) < 0.0001f);
        CHECK(fabsf(applied.ratios[2] - powf(2.0f, 4.0f / 12.0f)) < 0.0001f);
        CHECK(ts_fm_apply_pitch_scale(&applied) == 0);
    }
    {
        TsInstrument bank;
        TsInstrument chained;
        TsInstrument cloned;
        TsFmPatch anchor;
        TsFmPatch tile_patch;
        uint64_t original_hashes[TS_BANK_SLOT_COUNT];
        int any_variation = 0;
        ts_instrument_init(&bank);
        ts_instrument_init(&chained);
        ts_instrument_init(&cloned);
        CHECK(ts_instrument_bank_clear_all(&bank, error, sizeof(error)));
        CHECK(ts_instrument_select_bank(&bank, 0, error, sizeof(error)));
        CHECK(ts_instrument_create_selected(&bank, 0x42414e4bu,
                                            error, sizeof(error)));
        ts_fm_patch_from_recipe(&bank.bank[0].generator, &anchor);
        anchor.pitch_lock = 0;
        anchor.pitch_root = 0;
        anchor.pitch_scale = TS_FM_PITCH_SCALE_MAJOR;
        bank.family_mutation = 0.72f;
        bank.family_trajectory = 0;
        CHECK(ts_instrument_make_fm_bank(&bank, &anchor,
                                         error, sizeof(error)));
        CHECK(ts_instrument_bank_count(&bank) == TS_BANK_SLOT_COUNT);
        CHECK(bank.selected_slot == 0 && bank.family_anchor_slot == 0 &&
              bank.family_last_slot == TS_BANK_SLOT_COUNT - 1);
        ts_fm_patch_sanitize(&anchor);
        for (int slot = 0; slot < TS_BANK_SLOT_COUNT; ++slot) {
            CHECK(bank.bank[slot].occupied && !bank.bank[slot].locked);
            CHECK(bank.bank[slot].has_generator &&
                  bank.bank[slot].generator.kind == TS_GENERATOR_FM);
            CHECK(bank.bank[slot].tuning.root_note == TS_KEYBOARD_BASE_NOTE);
            CHECK(bank.bank[slot].parent_slot == (slot == 0 ? -1 : 0));
            ts_fm_patch_from_recipe(&bank.bank[slot].generator, &tile_patch);
            if (slot == 0)
                CHECK(memcmp(&tile_patch, &anchor, sizeof(anchor)) == 0);
            else if (memcmp(&tile_patch, &anchor, sizeof(anchor)) != 0)
                any_variation = 1;
            original_hashes[slot] = ts_sample_hash(&bank.bank[slot].sample);
        }
        CHECK(any_variation);
        CHECK(ts_instrument_clone(&cloned, &bank, error, sizeof(error)));
        CHECK(ts_sample_hash(&cloned.bank[7].sample) == original_hashes[7]);
        cloned.bank[7].sample.data[0] += 0.25f;
        CHECK(cloned.bank[7].sample.data[0] != bank.bank[7].sample.data[0]);
        bank.bank[5].locked = 1;
        CHECK(!ts_instrument_make_fm_bank(&bank, &anchor,
                                          error, sizeof(error)));
        for (int slot = 0; slot < TS_BANK_SLOT_COUNT; ++slot)
            CHECK(ts_sample_hash(&bank.bank[slot].sample) == original_hashes[slot]);

        CHECK(ts_instrument_clone(&chained, &bank, error, sizeof(error)));
        chained.bank[5].locked = 0;
        chained.family_trajectory = 1;
        CHECK(ts_instrument_make_fm_bank(&chained, &anchor,
                                         error, sizeof(error)));
        for (int slot = 1; slot < TS_BANK_SLOT_COUNT; ++slot)
            CHECK(chained.bank[slot].parent_slot == slot - 1);
        ts_instrument_free(&cloned);
        ts_instrument_free(&chained);
        ts_instrument_free(&bank);
    }

    {
        TsFmPatch base = {0}, zero, low, medium, high, repeat;
        float low_distance, medium_distance, high_distance;
        base.structure = 4; base.ratio_family = 2;
        base.depth = 4.0f; base.shape = 0.5f; base.feedback = 0.4f;
        base.transient_mix = 0.25f;
        for (int op = 0; op < TS_FM_OPERATOR_COUNT; ++op)
            base.ratios[op] = 0.75f + (float)op * 0.65f;
        ts_fm_patch_vary(&base, 0x13579bdu, 0.0f, &zero);
        ts_fm_patch_vary(&base, 0x13579bdu, 0.12f, &low);
        ts_fm_patch_vary(&base, 0x13579bdu, 0.50f, &medium);
        ts_fm_patch_vary(&base, 0x13579bdu, 1.0f, &high);
        ts_fm_patch_vary(&base, 0x13579bdu, 0.50f, &repeat);
        CHECK(memcmp(&base, &zero, sizeof(base)) == 0);
        CHECK(low.structure >= 0 && low.structure < TS_FM_STRUCTURE_COUNT);
        CHECK(medium.structure >= 0 && medium.structure < TS_FM_STRUCTURE_COUNT);
        CHECK(high.structure >= 0 && high.structure < TS_FM_STRUCTURE_COUNT);
        CHECK(low.ratio_family >= 0 && low.ratio_family < TS_FM_RATIO_FAMILY_COUNT);
        CHECK(medium.ratio_family >= 0 &&
              medium.ratio_family < TS_FM_RATIO_FAMILY_COUNT);
        CHECK(high.ratio_family >= 0 && high.ratio_family < TS_FM_RATIO_FAMILY_COUNT);
        CHECK(memcmp(&medium, &repeat, sizeof(medium)) == 0);
        low_distance = ts_fm_patch_distance(&base, &low);
        medium_distance = ts_fm_patch_distance(&base, &medium);
        high_distance = ts_fm_patch_distance(&base, &high);
        CHECK(low_distance > 0.0f && low_distance < medium_distance &&
              medium_distance < high_distance);
        CHECK(high.feedback >= 0.0f && high.feedback <= 0.82f);
        for (int op = 0; op < TS_FM_OPERATOR_COUNT; ++op)
            CHECK(isfinite(high.ratios[op]) && high.ratios[op] >= 0.05f &&
                  high.ratios[op] <= 16.0f);
        {
            TsSample rendered_high;
            TsGeneratorRecipe high_recipe = generator(0x777777u, TS_GENERATOR_FM);
            ts_sample_init(&rendered_high);
            high_recipe.fm_patch = high; high_recipe.has_fm_patch = 1;
            CHECK(ts_sample_generate(&rendered_high, &high_recipe, error, sizeof(error)));
            CHECK(ts_sample_peak(&rendered_high) > 0.01f &&
                  ts_sample_peak(&rendered_high) <= 1.0f);
            for (size_t frame = 0; frame < rendered_high.frames; ++frame)
                CHECK(isfinite(rendered_high.data[frame]));
            ts_sample_free(&rendered_high);
        }
    }

    {
        TsGeneratorRecipe recipe = generator(0xabcdefu, TS_GENERATOR_FM);
        TsFmPatch patch, varied;
        char label[32], value[32];
        ts_fm_patch_from_recipe(&recipe, &patch);
        for (int page = 0; page < TS_FM_PAGE_COUNT; ++page) {
            CHECK(strcmp(ts_fm_page_name((TsFmPage)page), "UNKNOWN") != 0);
            for (int control = 0; control < TS_FM_OPERATOR_COUNT; ++control) {
                CHECK(ts_fm_set_control_normalized(
                    &patch, (TsFmPage)page, control, 0.73f));
                CHECK(ts_fm_control_normalized(
                    &patch, (TsFmPage)page, control) >= 0.0f);
                CHECK(ts_fm_control_normalized(
                    &patch, (TsFmPage)page, control) <= 1.0f);
                ts_fm_control_format(&patch, (TsFmPage)page, control,
                                     label, sizeof(label), value, sizeof(value));
                CHECK(label[0] != '\0' && value[0] != '\0');
            }
        }
        patch.waveforms[0] = TS_FM_WAVE_SINE;
        CHECK(ts_fm_step_control(&patch, TS_FM_PAGE_WAVE, 0, 1, 0));
        CHECK(patch.waveforms[0] == TS_FM_WAVE_TRIANGLE);
        CHECK(ts_fm_step_control(&patch, TS_FM_PAGE_WAVE, 0, -1, 0));
        CHECK(patch.waveforms[0] == TS_FM_WAVE_SINE);
        patch.mutation_mask = TS_FM_MUTATE_PITCH;
        ts_fm_patch_vary(&patch, 0x2468aceu, 1.0f, &varied);
        CHECK(memcmp(patch.waveforms, varied.waveforms,
                     sizeof(patch.waveforms)) == 0);
        CHECK(memcmp(patch.lfo_rates, varied.lfo_rates,
                     sizeof(patch.lfo_rates)) == 0);
        CHECK(patch.filter_mode == varied.filter_mode &&
              patch.filter_cutoff_hz == varied.filter_cutoff_hz &&
              patch.structure == varied.structure &&
              patch.interaction == varied.interaction);
        for (int waveform = 0; waveform < TS_FM_WAVEFORM_COUNT; ++waveform) {
            TsSample first_render, second_render;
            ts_sample_init(&first_render);
            ts_sample_init(&second_render);
            patch.waveforms[0] = waveform;
            patch.active_mask = 1u;
            CHECK(ts_fm_render_sample(&first_render, &patch, 0.1f, 110.0f,
                                      8000u, 1234u, error, sizeof(error)));
            CHECK(ts_fm_render_sample(&second_render, &patch, 0.1f, 110.0f,
                                      8000u, 1234u, error, sizeof(error)));
            CHECK(ts_sample_hash(&first_render) == ts_sample_hash(&second_render));
            CHECK(strcmp(ts_fm_waveform_name(waveform), "UNKNOWN") != 0);
            for (size_t frame = 0; frame < first_render.frames; ++frame)
                CHECK(isfinite(first_render.data[frame]) &&
                      fabsf(first_render.data[frame]) <= 1.0f);
            ts_sample_free(&first_render);
            ts_sample_free(&second_render);
        }
        {
            TsSample drone_render;
            TsSample extreme_render;
            double early_energy = 0.0;
            double late_energy = 0.0;
            size_t quarter;
            ts_sample_init(&drone_render);
            ts_sample_init(&extreme_render);
            patch.drone_mode = 1;
            patch.extreme_mode = 0;
            patch.active_mask = 0x3fu;
            ts_fm_patch_sanitize(&patch);
            CHECK(ts_fm_render_sample(&drone_render, &patch, 1.0f, 110.0f,
                                      12000u, 0x44524f4eu,
                                      error, sizeof(error)));
            CHECK(drone_render.frames > 8000u);
            CHECK(drone_render.data[0] == 0.0f);
            CHECK(drone_render.data[drone_render.frames - 1u] == 0.0f);
            quarter = drone_render.frames / 4u;
            for (size_t frame = quarter / 2u; frame < quarter; ++frame)
                early_energy += (double)drone_render.data[frame] *
                                drone_render.data[frame];
            for (size_t frame = drone_render.frames - quarter;
                 frame < drone_render.frames - quarter / 2u; ++frame)
                late_energy += (double)drone_render.data[frame] *
                               drone_render.data[frame];
            CHECK(early_energy > 0.0001);
            CHECK(late_energy > early_energy * 0.10);

            patch.extreme_mode = 1;
            ts_fm_patch_sanitize(&patch);
            CHECK(ts_fm_set_control_normalized(
                &patch, TS_FM_PAGE_PITCH, 0, 1.0f));
            CHECK(ts_fm_set_control_normalized(
                &patch, TS_FM_PAGE_LFO_RATE, 0, 1.0f));
            CHECK(ts_fm_set_control_normalized(
                &patch, TS_FM_PAGE_LFO_DEPTH, 0, 1.0f));
            CHECK(ts_fm_set_control_normalized(
                &patch, TS_FM_PAGE_STRUCTURE, 2, 1.0f));
            CHECK(patch.ratios[0] > 16.0f && patch.depth > 12.0f &&
                  patch.lfo_rates[0] > 160.0f && patch.lfo_depths[0] > 1.0f);
            CHECK(ts_fm_render_sample(&extreme_render, &patch, 0.25f, 110.0f,
                                      12000u, 0x45585452u,
                                      error, sizeof(error)));
            CHECK(ts_sample_peak(&extreme_render) <= 0.98f);
            for (size_t frame = 0; frame < extreme_render.frames; ++frame)
                CHECK(isfinite(extreme_render.data[frame]));
            ts_sample_free(&drone_render);
            ts_sample_free(&extreme_render);
        }
    }

    {
        TsInstrument first, repeat;
        TsFmPatch first_patch, repeat_patch, next_patch;
        uint64_t first_source_hash, first_final_hash;
        int slot = -1;
        ts_instrument_init(&first); ts_instrument_init(&repeat);
        CHECK(ts_instrument_bank_clear_all(&first, error, sizeof(error)));
        CHECK(ts_instrument_bank_clear_all(&repeat, error, sizeof(error)));
        CHECK(ts_instrument_select_bank(&first, 4, error, sizeof(error)));
        CHECK(ts_instrument_select_bank(&repeat, 4, error, sizeof(error)));
        CHECK(ts_instrument_create_selected(&first, 0x2468aceu, error, sizeof(error)));
        CHECK(ts_instrument_create_selected(&repeat, 0x2468aceu, error, sizeof(error)));
        first.family_mutation = repeat.family_mutation = 0.55f;
        CHECK(ts_instrument_vary_selected(&first, 0, &slot, error, sizeof(error)));
        CHECK(ts_instrument_vary_selected(&repeat, 0, &slot, error, sizeof(error)));
        ts_fm_patch_from_recipe(&first.bank[4].generator, &first_patch);
        ts_fm_patch_from_recipe(&repeat.bank[4].generator, &repeat_patch);
        first_source_hash = ts_sample_hash(&first.bank[4].edit_parent);
        first_final_hash = ts_sample_hash(&first.bank[4].sample);
        CHECK(memcmp(&first_patch, &repeat_patch, sizeof(first_patch)) == 0);
        CHECK(first_source_hash == ts_sample_hash(&repeat.bank[4].edit_parent));
        CHECK(first_final_hash == ts_sample_hash(&repeat.bank[4].sample));
        CHECK(ts_instrument_vary_selected(&repeat, 0, &slot, error, sizeof(error)));
        ts_fm_patch_from_recipe(&repeat.bank[4].generator, &next_patch);
        CHECK(memcmp(&first_patch, &next_patch, sizeof(first_patch)) != 0);
        CHECK(first_source_hash != ts_sample_hash(&repeat.bank[4].edit_parent));
        ts_instrument_free(&first); ts_instrument_free(&repeat);
    }

    {
        TsInstrument gesture;
        uint64_t before, edited, b_hash, varied_from_a, chained;
        int destination = -1;
        ts_instrument_init(&gesture);
        CHECK(ts_instrument_bank_clear_all(&gesture, error, sizeof(error)));
        CHECK(ts_instrument_select_bank(&gesture, 1, error, sizeof(error)));
        CHECK(ts_instrument_create_selected(&gesture, 0xabc123u, error, sizeof(error)));
        before = ts_sample_hash(&gesture.current);
        CHECK(ts_instrument_apply_tape_drag(&gesture, TS_POST_COPY_OVERWRITE,
                                            0, gesture.current.frames / 4u,
                                            (int64_t)(gesture.current.frames / 2u),
                                            error, sizeof(error)));
        edited = ts_sample_hash(&gesture.current);
        CHECK(edited != before && ts_sample_hash(&gesture.bank[1].sample) == edited);
        CHECK(ts_instrument_select_bank(&gesture, 2, error, sizeof(error)));
        CHECK(ts_instrument_create_selected(&gesture, 0xdef456u, error, sizeof(error)));
        b_hash = ts_sample_hash(&gesture.bank[2].sample);
        CHECK(ts_instrument_select_bank(&gesture, 1, error, sizeof(error)));
        CHECK(ts_sample_hash(&gesture.current) == edited);
        CHECK(ts_sample_hash(&gesture.bank[2].sample) == b_hash);
        CHECK(ts_instrument_undo(&gesture, error, sizeof(error)));
        CHECK(ts_sample_hash(&gesture.current) == before);
        CHECK(ts_instrument_redo(&gesture, error, sizeof(error)));
        CHECK(ts_sample_hash(&gesture.current) == edited);
        {
            uint64_t source_before_zero = ts_sample_hash(&gesture.bank[1].edit_parent);
            TsGeneratorRecipe recipe_before_zero = gesture.bank[1].generator;
            gesture.family_mutation = 0.0f;
            CHECK(ts_instrument_vary_selected(&gesture, 0, &destination, error, sizeof(error)));
            CHECK(ts_sample_hash(&gesture.current) == edited);
            CHECK(ts_sample_hash(&gesture.bank[1].edit_parent) == source_before_zero);
            CHECK(memcmp(&gesture.bank[1].generator, &recipe_before_zero,
                         sizeof(recipe_before_zero)) == 0);
        }
        CHECK(ts_instrument_copy_selected(&gesture, 3, error, sizeof(error)));
        gesture.family_mutation = 0.5f;
        CHECK(ts_instrument_vary_selected(&gesture, 0, &destination, error, sizeof(error)));
        varied_from_a = ts_sample_hash(&gesture.current);
        CHECK(gesture.bank[3].generator.has_fm_patch);
        CHECK(gesture.bank[3].edit.post_edit_count == 1);
        CHECK(ts_sample_hash(&gesture.bank[3].edit_parent) != before);
        CHECK(ts_instrument_select_bank(&gesture, 1, error, sizeof(error)));
        CHECK(ts_instrument_copy_selected(&gesture, 5, error, sizeof(error)));
        gesture.family_mutation = 0.5f;
        CHECK(ts_instrument_vary_selected(&gesture, 1, &destination, error, sizeof(error)));
        chained = ts_sample_hash(&gesture.current);
        CHECK(destination == 6 && chained != edited && varied_from_a != edited);
        CHECK(ts_sample_hash(&gesture.bank[2].sample) == b_hash);
        CHECK(ts_instrument_select_bank(&gesture, 1, error, sizeof(error)));
        CHECK(ts_instrument_save_recipe(&gesture, "test-gesture.tsr", error, sizeof(error)));
        {
            TsInstrument loaded_gesture;
            ts_instrument_init(&loaded_gesture);
            CHECK(ts_instrument_load_recipe(&loaded_gesture, "test-gesture.tsr", error, sizeof(error)));
            CHECK(loaded_gesture.selected_slot == 1);
            CHECK(ts_sample_hash(&loaded_gesture.current) == edited);
            CHECK(ts_sample_hash(&loaded_gesture.bank[2].sample) == b_hash);
            ts_instrument_free(&loaded_gesture);
        }
        remove("test-gesture.tsr");
        ts_instrument_free(&gesture);
    }

    {
        TsInstrument load_bank, empty_first, reopened_bank;
        TsSample known, reopened_wav;
        TsGeneratorRecipe known_recipe = generator(0x10adedu, TS_GENERATOR_METALLIC);
        uint64_t slot0_hash, slot0_parent_hash, slot2_hash, slot2_parent_hash, wav_hash;
        uint64_t pasted_hash, fit_hash;
        size_t before_load_paste_frames, selected_load_frames;
        TsGeneratorRecipe slot0_generator, slot2_generator;
        int slot0_undo, slot2_undo;
        ts_sample_init(&known); ts_sample_init(&reopened_wav);
        ts_instrument_init(&load_bank); ts_instrument_init(&empty_first);
        ts_instrument_init(&reopened_bank);
        CHECK(ts_sample_generate(&known, &known_recipe, error, sizeof(error)));
        CHECK(ts_sample_save_wav16(&known, "test-selected-load.wav", error, sizeof(error)));
        CHECK(ts_sample_load_wav(&reopened_wav, "test-selected-load.wav", error, sizeof(error)));
        wav_hash = ts_sample_hash(&reopened_wav);

        CHECK(ts_instrument_bank_clear_all(&load_bank, error, sizeof(error)));
        for (int slot = 0; slot < 3; ++slot) {
            CHECK(ts_instrument_select_bank(&load_bank, slot, error, sizeof(error)));
            CHECK(ts_instrument_create_selected(&load_bank, 0x710000u + (uint32_t)slot,
                                                error, sizeof(error)));
        }
        slot0_hash = ts_sample_hash(&load_bank.bank[0].sample);
        slot0_parent_hash = ts_sample_hash(&load_bank.bank[0].edit_parent);
        slot2_hash = ts_sample_hash(&load_bank.bank[2].sample);
        slot2_parent_hash = ts_sample_hash(&load_bank.bank[2].edit_parent);
        slot0_generator = load_bank.bank[0].generator;
        slot2_generator = load_bank.bank[2].generator;
        slot0_undo = load_bank.bank[0].undo_count;
        slot2_undo = load_bank.bank[2].undo_count;
        CHECK(ts_instrument_select_bank(&load_bank, 1, error, sizeof(error)));
        CHECK(ts_instrument_load_wav(&load_bank, "test-selected-load.wav",
                                     error, sizeof(error)));
        CHECK(load_bank.selected_slot == 1 && load_bank.bank[1].occupied);
        CHECK(ts_sample_hash(&load_bank.bank[1].sample) == wav_hash);
        CHECK(ts_sample_hash(&load_bank.current) == wav_hash);
        CHECK(!load_bank.bank[1].has_generator &&
              !load_bank.bank[1].generator.has_fm_patch);
        CHECK(ts_sample_hash(&load_bank.bank[0].sample) == slot0_hash &&
              ts_sample_hash(&load_bank.bank[0].edit_parent) == slot0_parent_hash &&
              memcmp(&load_bank.bank[0].generator, &slot0_generator,
                     sizeof(slot0_generator)) == 0 &&
              load_bank.bank[0].undo_count == slot0_undo);
        CHECK(ts_sample_hash(&load_bank.bank[2].sample) == slot2_hash &&
              ts_sample_hash(&load_bank.bank[2].edit_parent) == slot2_parent_hash &&
              memcmp(&load_bank.bank[2].generator, &slot2_generator,
                     sizeof(slot2_generator)) == 0 &&
              load_bank.bank[2].undo_count == slot2_undo);
        ts_instrument_set_selection(&load_bank, 0, load_bank.current.frames / 3u);
        CHECK(ts_instrument_apply_sample_edit(&load_bank, TS_SAMPLE_EDIT_REVERSE, 1.0f,
                                              error, sizeof(error)));
        CHECK(ts_sample_hash(&load_bank.current) != wav_hash);
        CHECK(ts_instrument_undo(&load_bank, error, sizeof(error)));
        CHECK(ts_sample_hash(&load_bank.current) == wav_hash);

        /* A WAV chosen for an occupied selection reuses exact Paste/Fit history. */
        before_load_paste_frames = load_bank.current.frames;
        selected_load_frames = load_bank.selection_last - load_bank.selection_first;
        CHECK(selected_load_frames > 0);
        CHECK(ts_instrument_paste(&load_bank, &reopened_wav,
                                  load_bank.selection_first, 0,
                                  error, sizeof(error)));
        CHECK(load_bank.current.frames == before_load_paste_frames -
              selected_load_frames + reopened_wav.frames);
        pasted_hash = ts_sample_hash(&load_bank.current);
        CHECK(ts_instrument_undo(&load_bank, error, sizeof(error)));
        CHECK(load_bank.current.frames == before_load_paste_frames &&
              ts_sample_hash(&load_bank.current) == wav_hash);
        CHECK(ts_instrument_redo(&load_bank, error, sizeof(error)));
        CHECK(ts_sample_hash(&load_bank.current) == pasted_hash);
        CHECK(ts_instrument_undo(&load_bank, error, sizeof(error)));
        CHECK(ts_sample_hash(&load_bank.current) == wav_hash);
        CHECK(ts_instrument_paste(&load_bank, &reopened_wav,
                                  load_bank.selection_first, 1,
                                  error, sizeof(error)));
        CHECK(load_bank.current.frames == before_load_paste_frames);
        fit_hash = ts_sample_hash(&load_bank.current);
        CHECK(fit_hash != wav_hash);
        CHECK(ts_instrument_undo(&load_bank, error, sizeof(error)));
        CHECK(ts_sample_hash(&load_bank.current) == wav_hash);
        CHECK(ts_instrument_redo(&load_bank, error, sizeof(error)));
        CHECK(ts_sample_hash(&load_bank.current) == fit_hash);
        CHECK(ts_instrument_undo(&load_bank, error, sizeof(error)));

        CHECK(ts_instrument_select_bank(&load_bank, 4, error, sizeof(error)));
        CHECK(!load_bank.bank[4].occupied);
        CHECK(ts_instrument_load_wav(&load_bank, "test-selected-load.wav",
                                     error, sizeof(error)));
        CHECK(load_bank.selected_slot == 4 &&
              ts_sample_hash(&load_bank.current) == wav_hash);
        CHECK(ts_sample_hash(&load_bank.bank[0].sample) == slot0_hash &&
              ts_sample_hash(&load_bank.bank[2].sample) == slot2_hash);

        CHECK(ts_instrument_bank_clear_all(&empty_first, error, sizeof(error)));
        CHECK(ts_instrument_select_bank(&empty_first, 1, error, sizeof(error)));
        CHECK(ts_instrument_create_selected(&empty_first, 0x810001u, error, sizeof(error)));
        CHECK(ts_instrument_select_bank(&empty_first, 2, error, sizeof(error)));
        CHECK(ts_instrument_create_selected(&empty_first, 0x810002u, error, sizeof(error)));
        slot2_hash = ts_sample_hash(&empty_first.bank[2].sample);
        CHECK(ts_instrument_select_bank(&empty_first, 0, error, sizeof(error)));
        CHECK(!empty_first.bank[0].occupied);
        CHECK(ts_instrument_load_wav(&empty_first, "test-selected-load.wav",
                                     error, sizeof(error)));
        CHECK(empty_first.selected_slot == 0 &&
              ts_sample_hash(&empty_first.bank[0].sample) == wav_hash &&
              empty_first.bank[1].occupied &&
              ts_sample_hash(&empty_first.bank[2].sample) == slot2_hash);

        CHECK(ts_instrument_save_recipe(&load_bank, "test-selected-load.tsr",
                                        error, sizeof(error)));
        CHECK(ts_instrument_load_recipe(&reopened_bank, "test-selected-load.tsr",
                                        error, sizeof(error)));
        CHECK(reopened_bank.selected_slot == load_bank.selected_slot);
        CHECK(ts_sample_hash(&reopened_bank.bank[4].sample) == wav_hash);
        CHECK(ts_sample_hash(&reopened_bank.bank[0].sample) == slot0_hash);
        CHECK(ts_sample_hash(&reopened_bank.bank[2].sample) ==
              ts_sample_hash(&load_bank.bank[2].sample));
        remove("test-selected-load.wav"); remove("test-selected-load.tsr");
        ts_sample_free(&known); ts_sample_free(&reopened_wav);
        ts_instrument_free(&load_bank); ts_instrument_free(&empty_first);
        ts_instrument_free(&reopened_bank);
    }

    {
        TsInstrument stretch;
        TsInstrument stretch_reopened;
        uint64_t before_hash;
        uint64_t expanded_hash;
        size_t first;
        size_t last;
        size_t before_frames;
        size_t expanded_frames;
        size_t mapped_expected;
        float pitch = 0.0f;
        ts_instrument_init(&stretch);
        ts_instrument_init(&stretch_reopened);
        CHECK(ts_instrument_generate(&stretch, TS_GENERATOR_FM,
                                     0x53545231u, error, sizeof(error)));
        CHECK(ts_instrument_select_bank(&stretch, 1, error, sizeof(error)));
        CHECK(ts_instrument_create_selected(&stretch, 0x53545232u,
                                            error, sizeof(error)));
        CHECK(ts_instrument_select_bank(&stretch, 0, error, sizeof(error)));
        first = stretch.current.frames / 4u;
        last = stretch.current.frames * 3u / 4u;
        ts_instrument_set_selection_snapped(&stretch, first, last);
        first = stretch.selection_first;
        last = stretch.selection_last;
        before_frames = last - first;
        ts_instrument_set_playhead(&stretch, first + before_frames / 3u);
        CHECK(stretch.has_playhead && stretch.playhead_frame >= first &&
              stretch.playhead_frame < last);
        CHECK(ts_instrument_reset_selection_playhead(&stretch));
        CHECK(!stretch.has_selection && stretch.has_playhead &&
              stretch.playhead_frame == 0);
        CHECK(!ts_instrument_reset_selection_playhead(&stretch));
        ts_instrument_clear_playhead(&stretch);
        CHECK(ts_instrument_reset_selection_playhead(&stretch));
        CHECK(!stretch.has_selection && stretch.has_playhead &&
              stretch.playhead_frame == 0);
        ts_instrument_set_selection(&stretch, first, last);
        ts_instrument_set_playhead(&stretch, first + before_frames / 3u);
        {
            size_t requested = first + before_frames / 5u;
            size_t expected = ts_sample_nearest_zero_crossing(
                &stretch.current, requested);
            ts_instrument_set_playhead_snapped(&stretch, requested);
            CHECK(stretch.playhead_frame == expected);
            ts_instrument_set_playhead(&stretch, first + before_frames / 3u);
        }
        {
            size_t original_first = stretch.selection_first;
            CHECK(ts_instrument_resize_selection(&stretch, 1, 1, 1));
            CHECK(stretch.selection_first < original_first &&
                  stretch.selection_last == last);
            ts_instrument_set_selection(&stretch, first, last);
        }
        before_hash = ts_sample_hash(&stretch.current);
        CHECK(ts_instrument_stretch_selection(
            &stretch, stretch.playhead_frame, powf(2.0f, 1.0f / 12.0f),
            &pitch, error, sizeof(error)));
        expanded_frames = stretch.selection_last - stretch.selection_first;
        expanded_hash = ts_sample_hash(&stretch.current);
        CHECK(expanded_frames > before_frames && expanded_hash != before_hash);
        CHECK(pitch < 0.0f && fabsf(pitch + 1.0f) < 0.35f);
        CHECK(stretch.current.frames == stretch.bank[0].sample.frames);
        CHECK(stretch.has_playhead);
        CHECK(ts_instrument_undo(&stretch, error, sizeof(error)));
        CHECK(ts_sample_hash(&stretch.current) == before_hash &&
              stretch.selection_first == first && stretch.selection_last == last &&
              stretch.has_playhead);
        CHECK(ts_instrument_redo(&stretch, error, sizeof(error)));
        CHECK(ts_sample_hash(&stretch.current) == expanded_hash &&
              stretch.selection_last - stretch.selection_first == expanded_frames);
        CHECK(ts_instrument_stretch_selection(
            &stretch, stretch.playhead_frame, powf(2.0f, -1.0f / 12.0f),
            &pitch, error, sizeof(error)));
        CHECK(stretch.selection_last - stretch.selection_first < expanded_frames &&
              pitch > 0.0f);
        CHECK(ts_instrument_undo(&stretch, error, sizeof(error)) &&
              ts_sample_hash(&stretch.current) == expanded_hash);
        CHECK(ts_instrument_save_recipe(&stretch, "test-stretch.tsr",
                                        error, sizeof(error)));
        CHECK(ts_instrument_load_recipe(&stretch_reopened, "test-stretch.tsr",
                                        error, sizeof(error)));
        CHECK(ts_sample_hash(&stretch_reopened.current) == expanded_hash &&
              stretch_reopened.has_playhead &&
              stretch_reopened.playhead_frame == stretch.playhead_frame);
        mapped_expected = stretch.current.frames > 1u ?
            (size_t)llround((double)stretch.playhead_frame *
                            (double)(stretch.bank[1].sample.frames - 1u) /
                            (double)(stretch.current.frames - 1u)) : 0u;
        CHECK(ts_instrument_select_bank(&stretch, 1, error, sizeof(error)));
        CHECK(stretch.has_playhead && stretch.playhead_frame == mapped_expected);
        CHECK(ts_instrument_select_bank(&stretch, 0, error, sizeof(error)));
        CHECK(stretch.has_playhead);
        remove("test-stretch.tsr");
        ts_instrument_free(&stretch);
        ts_instrument_free(&stretch_reopened);
    }

    {
        TsInstrument stretch;
        TsStretchGesture gesture;
        uint64_t before_hash;
        uint64_t preview_hash;
        size_t first, last;
        int patch_count;
        int post_count;
        int undo_count;
        float pitch = 0.0f;
        ts_instrument_init(&stretch);
        ts_stretch_gesture_init(&gesture);
        CHECK(ts_instrument_generate(&stretch, TS_GENERATOR_FM,
                                     0x47455354u, error, sizeof(error)));
        ts_instrument_set_selection_snapped(&stretch,
                                            stretch.current.frames / 4u,
                                            stretch.current.frames * 3u / 4u);
        first = stretch.selection_first;
        last = stretch.selection_last;
        before_hash = ts_sample_hash(&stretch.current);
        patch_count = stretch.bank[stretch.selected_slot].patch_count;
        post_count = stretch.post_edit_count;
        undo_count = stretch.undo_count;
        CHECK(ts_instrument_stretch_gesture_begin(
            &stretch, &gesture, first + (last - first) / 3u,
            error, sizeof(error)));
        for (int step = 0; step < 80; ++step) {
            float ratio = step & 1 ? powf(2.0f, 1.0f / 12.0f) :
                                     powf(2.0f, 2.0f / 12.0f);
            CHECK(ts_instrument_stretch_gesture_preview(
                &stretch, &gesture, ratio, &pitch, error, sizeof(error)));
            CHECK(stretch.bank[stretch.selected_slot].patch_count == patch_count);
            CHECK(stretch.post_edit_count == post_count);
            CHECK(stretch.undo_count == undo_count);
        }
        preview_hash = ts_sample_hash(&stretch.current);
        CHECK(preview_hash != before_hash && pitch < 0.0f);
        CHECK(ts_instrument_stretch_gesture_commit(
            &stretch, &gesture, error, sizeof(error)));
        CHECK(stretch.bank[stretch.selected_slot].patch_count == patch_count + 1);
        CHECK(stretch.post_edit_count == post_count + 1);
        CHECK(stretch.undo_count == undo_count + 1);
        CHECK(ts_sample_hash(&stretch.current) == preview_hash);
        CHECK(ts_instrument_undo(&stretch, error, sizeof(error)));
        CHECK(ts_sample_hash(&stretch.current) == before_hash &&
              stretch.selection_first == first && stretch.selection_last == last);
        CHECK(ts_instrument_redo(&stretch, error, sizeof(error)));
        CHECK(ts_sample_hash(&stretch.current) == preview_hash);

        patch_count = stretch.bank[stretch.selected_slot].patch_count;
        post_count = stretch.post_edit_count;
        undo_count = stretch.undo_count;
        before_hash = ts_sample_hash(&stretch.current);
        first = stretch.selection_first;
        last = stretch.selection_last;
        CHECK(ts_instrument_stretch_gesture_begin(
            &stretch, &gesture, first + (last - first) / 2u,
            error, sizeof(error)));
        CHECK(ts_instrument_stretch_gesture_preview(
            &stretch, &gesture, powf(2.0f, -2.0f / 12.0f),
            &pitch, error, sizeof(error)));
        CHECK(ts_sample_hash(&stretch.current) != before_hash);
        CHECK(ts_instrument_stretch_gesture_cancel(
            &stretch, &gesture, error, sizeof(error)));
        CHECK(ts_sample_hash(&stretch.current) == before_hash &&
              stretch.selection_first == first && stretch.selection_last == last);
        CHECK(stretch.bank[stretch.selected_slot].patch_count == patch_count);
        CHECK(stretch.post_edit_count == post_count);
        CHECK(stretch.undo_count == undo_count);

        CHECK(ts_instrument_stretch_gesture_begin(
            &stretch, &gesture, first + (last - first) / 2u,
            error, sizeof(error)));
        CHECK(ts_instrument_stretch_gesture_preview(
            &stretch, &gesture, 1.0f, &pitch, error, sizeof(error)));
        CHECK(ts_instrument_stretch_gesture_commit(
            &stretch, &gesture, error, sizeof(error)));
        CHECK(ts_sample_hash(&stretch.current) == before_hash);
        CHECK(stretch.bank[stretch.selected_slot].patch_count == patch_count);
        CHECK(stretch.post_edit_count == post_count);
        CHECK(stretch.undo_count == undo_count);
        ts_instrument_free(&stretch);
    }

    ts_sample_free(&a); ts_sample_free(&b); ts_sample_free(&loaded); ts_sample_free(&copy);
    ts_sample_free(&dry); ts_sample_free(&effected); ts_sample_free(&repeated);
    ts_instrument_free(&generated); ts_instrument_free(&imported); ts_instrument_free(&committed);
    ts_instrument_free(&audition);
    ts_instrument_free(&restored);
    ts_instrument_free(&bank_edit);
    ts_instrument_free(&recipe_target);
    ts_instrument_free(&family);
    ts_instrument_free(&family_repeat);
    ts_instrument_free(&family_restored);
    if (failures) return 1;
    puts("TapeSister variation, tuning, recipes, shaping, tape gestures, loops, collection, and editor tests passed");
    return 0;
}
