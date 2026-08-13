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
    ts_note_bank_init(&notes);
    ts_recipe_bank_init(&recipe_bank);

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
                CHECK(strstr(variants[i].name, " V") != NULL);
            }
            for (int i = 0; i < 4; ++i)
                for (int j = i + 1; j < 4; ++j) CHECK(hashes[i] != hashes[j]);
            for (int i = 0; i < 4; ++i) ts_sample_free(&variants[i]);
        }
    }

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
        ts_instrument_clear_selection(&snap_instrument);
        CHECK(ts_instrument_set_loop_from_selection(&snap_instrument,
                                                     error, sizeof(error)));
        CHECK(snap_instrument.has_selection && snap_instrument.has_loop);
        CHECK(snap_instrument.selection_first == 0 &&
              snap_instrument.selection_last == crossing_sample.frames);
        CHECK(snap_instrument.loop_first == 0 &&
              snap_instrument.loop_last == crossing_sample.frames);
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
        CHECK(!ts_recipe_bank_capture(&recipe_bank, 0, &neutral, &recipe_tuning, "NO",
                                      recipe_error, sizeof(recipe_error)));
        CHECK(ts_recipe_bank_capture(&recipe_bank, 8, &dsp, &recipe_tuning, "MY TEXTURE",
                                     recipe_error, sizeof(recipe_error)));
        CHECK(!ts_recipe_bank_capture(&recipe_bank, 8, &neutral, &recipe_tuning, "OVERWRITE",
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
        CHECK(fabs(ts_tuning_note_pitch(&(TsTuning){60, 0.0f}, 12) - 1.0) < 0.0001);
        CHECK(strcmp(ts_midi_note_name(60, note_name, sizeof(note_name)), "C4") == 0);
        remove("test-tuned.wav");
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
    }

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
    CHECK(ts_instrument_bank_count(&generated) == 1);
    CHECK(ts_sample_hash(&generated.bank[0].sample) == ts_sample_hash(&generated.parent));

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
        CHECK(bank_edit.bank[1].tuning.root_note == 53 &&
              fabsf(bank_edit.bank[1].tuning.fine_tune_cents - 14.0f) < 0.001f);
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
        CHECK(bank_edit.tuning.root_note == 53 &&
              fabsf(bank_edit.tuning.fine_tune_cents - 14.0f) < 0.001f);
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
        TsSample loop_sample = {loop_data, 8, 1000, "loop"};
        float blended = ts_audition_read_looped(&loop_sample, 7.0, 0, 8, 2);
        CHECK(fabsf(blended) < 0.0001f);
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
        ts_instrument_set_selection(&tape, source_first, source_last);
        CHECK(ts_instrument_apply_tape_drag(&tape, TS_POST_COPY_MIX,
                                            source_first, source_last, 3000,
                                            error, sizeof(error)));
        CHECK(tape.post_edit_count == 1 && tape.current.frames == original_frames);
        CHECK(tape.selection_first == (size_t)tape.post_edits[0].destination &&
              tape.selection_last - tape.selection_first == source_last - source_first);
        CHECK(fabsf(tape.current.data[(size_t)mixed_destination + 300u] -
                    (mixed_under_value + mixed_source_value) * 0.5f) < 0.00001f);
        after = ts_sample_hash(&tape.current);
        CHECK(after != before);
        CHECK(ts_instrument_undo(&tape, error, sizeof(error)) &&
              ts_sample_hash(&tape.current) == before && tape.post_edit_count == 0);
        CHECK(ts_instrument_redo(&tape, error, sizeof(error)) &&
              ts_sample_hash(&tape.current) == after && tape.post_edit_count == 1);

        CHECK(ts_instrument_apply_tape_drag(&tape, TS_POST_MOVE_OVERWRITE,
                                            source_first, source_last, 1250,
                                            error, sizeof(error)));
        CHECK(tape.post_edit_count == 2);
        after = ts_sample_hash(&tape.current);
        CHECK(ts_instrument_undo(&tape, error, sizeof(error)));
        CHECK(ts_instrument_redo(&tape, error, sizeof(error)) &&
              ts_sample_hash(&tape.current) == after);

        CHECK(ts_instrument_apply_tape_drag(&tape, TS_POST_COPY_OVERWRITE,
                                            2000, 2400, -100,
                                            error, sizeof(error)));
        CHECK(tape.current.frames == original_frames + 100u);
        CHECK(tape.selection_first == 0 && tape.selection_last == 400u);
        CHECK(ts_instrument_apply_tape_drag(&tape, TS_POST_MOVE_MIX,
                                            500, 800,
                                            (int64_t)tape.current.frames + 20,
                                            error, sizeof(error)));
        CHECK(tape.current.frames == original_frames + 420u);
        CHECK(tape.post_edit_count == 4);
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
    CHECK(committed.bank[1].tuning.root_note == committed.tuning.root_note);
    CHECK(committed.bank[2].sample.frames ==
          committed.selection_last - committed.selection_first);
    CHECK(committed.bank[3].has_loop && committed.bank[3].loop_first == 0 &&
          committed.bank[3].loop_last == committed.bank[3].sample.frames);
    CHECK(ts_instrument_bank_rename(&committed, 2, "  Growing Tail  ",
                                    error, sizeof(error)));
    CHECK(strcmp(committed.bank[2].sample.name, "Growing Tail") == 0);
    CHECK(!ts_instrument_bank_rename(&committed, 0, "New Root",
                                     error, sizeof(error)));
    CHECK(!ts_instrument_bank_rename(&committed, 4, "Empty",
                                     error, sizeof(error)));
    CHECK(!ts_instrument_bank_rename(&committed, 2, "   ",
                                     error, sizeof(error)));
    CHECK(!ts_instrument_bank_capture(&committed, 3, TS_BANK_CAPTURE_CURRENT,
                                      error, sizeof(error)));
    CHECK(!ts_instrument_bank_clear(&committed, 0, error, sizeof(error)));
    CHECK(ts_instrument_save_recipe(&committed, "test-recipe.tsr", error, sizeof(error)));
    {
        FILE *recipe = fopen("test-recipe.tsr", "rb");
        char magic[5] = {0};
        CHECK(recipe != NULL);
        if (recipe != NULL) {
            CHECK(fread(magic, 1, sizeof(magic), recipe) == sizeof(magic));
            fclose(recipe);
        }
        CHECK(memcmp(magic, "TSR10", 5) == 0);
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
    CHECK(fabsf(restored.process.shaper_drive - 4.75f) < 0.001f);
    CHECK(ts_instrument_bank_count(&restored) == 4);
    CHECK(strcmp(restored.bank[2].sample.name, "Growing Tail") == 0);
    for (int slot = 0; slot < 4; ++slot) {
        CHECK(restored.bank[slot].occupied);
        CHECK(ts_sample_hash(&restored.bank[slot].sample) ==
              ts_sample_hash(&committed.bank[slot].sample));
        CHECK(restored.bank[slot].capture_kind == committed.bank[slot].capture_kind);
        CHECK(restored.bank[slot].tuning.root_note == committed.bank[slot].tuning.root_note);
        CHECK(restored.bank[slot].loop_mode == committed.bank[slot].loop_mode);
        CHECK(restored.bank[slot].has_loop == committed.bank[slot].has_loop);
    }
    CHECK(ts_instrument_export_bank(&restored, "test-bank-family", error, sizeof(error)));
    {
        DIR *directory = opendir("test-bank-family");
        struct dirent *entry;
        int wav_count = 0;
        CHECK(directory != NULL);
        if (directory != NULL) {
            while ((entry = readdir(directory)) != NULL) {
                size_t length = strlen(entry->d_name);
                if (length > 4 && strcmp(entry->d_name + length - 4, ".wav") == 0) {
                    char exported[512];
                    snprintf(exported, sizeof(exported), "test-bank-family/%s", entry->d_name);
                    ++wav_count;
                    remove(exported);
                }
            }
            closedir(directory);
        }
        CHECK(wav_count == 4);
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
    ui.has_pitch_suggestion = 1;
    ts_ui_render(&fb, &ui, &committed);
    CHECK(framebuffer_contains(&fb, 0xff147dffu));
    CHECK(framebuffer_contains(&fb, 0xff2d0039u));
    ts_ui_init(&ui);
    ts_instrument_set_selection(&imported, imported.current.frames / 4, imported.current.frames / 2);
    ts_ui_render(&fb, &ui, &imported);
    CHECK(fb.pixels[0] != 0);
    CHECK(framebuffer_contains(&fb, 0xff1c1c1cu));
    CHECK(framebuffer_contains(&fb, 0xffffe700u));
    CHECK(framebuffer_contains(&fb, 0xff2d0039u));
    CHECK(framebuffer_contains(&fb, 0xff009ee3u));
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
    ts_ui_render(&fb, &ui, &imported);
    CHECK(ts_ui_bank_slot_from_point(46, 341) == 0);
    CHECK(fb.pixels[340 * TS_UI_WIDTH + 20] != 0xff1c1c1cu);
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
    CHECK(ts_ui_bank_action(1, 0) == TS_UI_BANK_ACTION_RENAME);
    CHECK(ts_ui_bank_action(1, TS_UI_BANK_MOD_SHIFT) == TS_UI_BANK_ACTION_CLEAR);
    CHECK(ts_ui_bank_action(0, TS_UI_BANK_MOD_SHIFT | TS_UI_BANK_MOD_CTRL) ==
          TS_UI_BANK_ACTION_INVALID);
    ui.show_keyboard = 0;
    ui.show_recipes = 1;
    ts_ui_render(&fb, &ui, &imported);
    CHECK(ts_ui_recipe_slot_from_point(46, 341) == 0);
    CHECK(ui.recipes.slots[0].occupied && ui.recipes.slots[0].factory);
    CHECK(fb.pixels[332 * TS_UI_WIDTH + 12] == 0xff18ff00u);
    ui.show_recipes = 0;
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
        CHECK(fb.pixels[220 * TS_UI_WIDTH + 625] == 0xff2d0039u);
        ui.bank_view_slot = -1;
        ts_ui_render(&fb, &ui, &restored);
        CHECK(fb.pixels[220 * TS_UI_WIDTH + 625] == 0xff5d555du);
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

    ts_sample_free(&a); ts_sample_free(&b); ts_sample_free(&loaded); ts_sample_free(&copy);
    ts_sample_free(&dry); ts_sample_free(&effected); ts_sample_free(&repeated);
    ts_instrument_free(&generated); ts_instrument_free(&imported); ts_instrument_free(&committed);
    ts_instrument_free(&audition);
    ts_instrument_free(&restored);
    ts_instrument_free(&bank_edit);
    ts_instrument_free(&recipe_target);
    if (failures) return 1;
    puts("TapeSister tuning, recipes, shaping, tape gestures, loops, bank, and editor tests passed");
    return 0;
}
