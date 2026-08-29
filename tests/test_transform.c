#define _POSIX_C_SOURCE 200809L

#include "tapesister/cdp_adapter.h"
#include "tapesister/cdp_recipe.h"
#include "tapesister/dsp_transform.h"
#include "tapesister/transform.h"
#include "tapesister/ui.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while (0)

static void setup(TsInstrument *instrument, size_t frames)
{
    char error[160];
    ts_instrument_init(instrument);
    instrument->selected_slot = 0;
    CHECK(ts_instrument_activate_silence(instrument, frames, 48000u,
                                        error, sizeof(error)));
    for (size_t i = 0; i < frames; ++i) {
        float value = (float)i / (float)frames * 0.5f - 0.25f;
        instrument->current.data[i] = value;
        instrument->parent.data[i] = value;
        instrument->bank[0].sample.data[i] = value;
        instrument->bank[0].edit_parent.data[i] = value;
    }
    ts_instrument_set_selection(instrument, frames / 4u, frames / 2u);
}

static void recipe_tests(void)
{
    char error[160];
    char formatted[64];
    TsCdpRecipeValues values;
    TsCdpGlistenMapping mapped;
    TsCdpCommand commands[TS_CDP_MAX_STAGES];
    const TsCdpRecipe *recipe = ts_cdp_recipe_find("glisten");
    CHECK(ts_cdp_factory_recipe_count() == TS_CDP_FACTORY_RECIPE_COUNT);
    CHECK(recipe != NULL && recipe == ts_cdp_factory_recipe_at(16) &&
          recipe == ts_cdp_factory_recipe_for_slot(1, 0));
    CHECK(strcmp(ts_cdp_factory_recipe_for_slot(0, 0)->id, "drunk") == 0);
    CHECK(strcmp(ts_cdp_factory_recipe_for_slot(0, 15)->id, "iterate") == 0);
    CHECK(strcmp(ts_cdp_factory_recipe_for_slot(1, 15)->id, "granulate") == 0);
    CHECK(ts_cdp_recipe_find("missing") == NULL);
    CHECK(ts_cdp_recipe_index_for_id("glisten") == 16);
    CHECK(ts_cdp_recipe_index_for_id("missing") == -1);
    {
        int enabled[TS_CDP_CATALOG_CAPACITY] = {0};
        TsCdpCatalogView view;
        for (size_t i = 0; i < ts_cdp_factory_recipe_count(); ++i)
            enabled[i] = 1;
        enabled[0] = 0;
        enabled[16] = 0;
        ts_cdp_catalog_view_build(&view, enabled, TS_CDP_CATALOG_CAPACITY);
        CHECK(view.enabled_count == TS_CDP_FACTORY_RECIPE_COUNT - 2u &&
              view.visible_count == TS_CDP_FACTORY_RECIPE_COUNT - 2u &&
              !view.truncated);
        CHECK(ts_cdp_catalog_index_for_slot(&view, 0u, 0u) == 1 &&
              strcmp(ts_cdp_catalog_recipe_for_slot(&view, 0u, 0u)->id,
                     "shred") == 0);
        CHECK(ts_cdp_catalog_index_for_slot(&view, 1u, 13u) == 31 &&
              ts_cdp_catalog_recipe_for_slot(&view, 1u, 14u) == NULL &&
              ts_cdp_catalog_recipe_for_slot(&view, 1u, 15u) == NULL);
        CHECK(ts_cdp_catalog_index_for_slot(&view, 2u, 0u) == -1);
    }
    CHECK(ts_cdp_recipe_validate(recipe, error, sizeof(error)));
    CHECK(recipe->stage_count == 3u);
    CHECK(strcmp(recipe->stages[0].executable, "pvoc") == 0 &&
          recipe->stages[0].input_type == TS_CDP_IO_WAV &&
          recipe->stages[0].output_type == TS_CDP_IO_ANALYSIS);
    CHECK(strcmp(recipe->stages[1].executable, "glisten") == 0);
    CHECK(strcmp(recipe->stages[2].executable, "pvoc") == 0 &&
          recipe->stages[2].output_type == TS_CDP_IO_WAV);
    for (size_t i = 0; i < TS_CDP_CONTROL_COUNT; ++i)
        CHECK(recipe->controls[i].label != NULL && recipe->controls[i].label[0] != '\0');
    ts_cdp_recipe_values_default(recipe, &values);
    CHECK(values.controls[0] == 8.0f && values.controls[1] == 8.0f &&
          values.controls[2] == 3.0f && values.controls[3] == 0.28f);
    CHECK(ts_cdp_control_quantize(&recipe->controls[0], 11.0f) == 8.0f);
    CHECK(ts_cdp_control_quantize(&recipe->controls[0], 60.0f) == 64.0f);
    CHECK(ts_cdp_control_quantize(&recipe->controls[1], 7.6f) == 8.0f);
    CHECK(ts_cdp_control_quantize(&recipe->controls[2], 99.0f) == 12.0f);
    CHECK(ts_cdp_glisten_map(recipe, &values, &mapped, error, sizeof(error)));
    CHECK(mapped.divide == 8 && mapped.hold_windows == 8 &&
          fabsf(mapped.shift_semitones - 3.0f) < 0.0001f &&
          fabsf(mapped.duration_randomization - 0.28f) < 0.0001f &&
          fabsf(mapped.division_randomization - 0.0784f) < 0.0001f);
    CHECK(ts_cdp_glisten_build_commands(recipe, &values, commands,
                                        error, sizeof(error)));
    CHECK(strcmp(commands[0].executable, "pvoc") == 0 && commands[0].argc == 6 &&
          strcmp(commands[0].arguments[0], "anal") == 0 &&
          strcmp(commands[0].arguments[1], "1") == 0 &&
          strcmp(commands[0].arguments[2], "input.wav") == 0 &&
          strcmp(commands[0].arguments[3], "input.ana") == 0 &&
          strcmp(commands[0].arguments[4], "-c1024") == 0 &&
          strcmp(commands[0].arguments[5], "-o3") == 0);
    CHECK(strcmp(commands[1].arguments[0], "glisten") == 0 &&
          strcmp(commands[1].arguments[1], "input.ana") == 0 &&
          strcmp(commands[1].arguments[2], "effect.ana") == 0 &&
          strcmp(commands[1].arguments[3], "8") == 0 &&
          strcmp(commands[1].arguments[4], "8") == 0 &&
          strcmp(commands[1].arguments[5], "-p3") == 0 &&
          strcmp(commands[1].arguments[6], "-d0.28") == 0 &&
          strcmp(commands[1].arguments[7], "-v0.0784") == 0);
    CHECK(strcmp(commands[2].arguments[0], "synth") == 0 &&
          strcmp(commands[2].arguments[1], "effect.ana") == 0 &&
          strcmp(commands[2].arguments[2], "output.wav") == 0);
    CHECK(!recipe->seed_supported && !recipe->deterministic);
    CHECK(recipe->duration_may_change);
    CHECK(recipe->mix_policy == TS_CDP_MIX_UNSUPPORTED);
    CHECK(values.mix == 1.0f);
    CHECK(recipe->required_input_channels == 1u &&
          recipe->expected_output_channels == 1u && recipe->preserve_sample_rate);
    CHECK(recipe->safety_policy == TS_CDP_SAFETY_ANALYZE_ONLY &&
          recipe->provenance_version == 2u);
    for (size_t i = 0; i < recipe->controls[0].valid_value_count; ++i) {
        int divide = (int)recipe->controls[0].valid_values[i];
        CHECK(divide >= 2 && divide < 513 && 513 % divide == 1);
    }
    {
        TsCdpRecipe invalid = *recipe;
        invalid.recipe_version = 0u;
        CHECK(!ts_cdp_recipe_validate(&invalid, error, sizeof(error)));
        invalid = *recipe;
        invalid.stages[1].input_type = TS_CDP_IO_WAV;
        CHECK(!ts_cdp_recipe_validate(&invalid, error, sizeof(error)));
        invalid = *recipe;
        invalid.stages[1].executable = "../glisten";
        CHECK(!ts_cdp_recipe_validate(&invalid, error, sizeof(error)));
    }
    CHECK(!ts_cdp_recipe_input_valid(recipe, 1000u, 48000u, error, sizeof(error)));
    CHECK(ts_cdp_recipe_input_valid(recipe, 5000u, 48000u, error, sizeof(error)));
    ts_cdp_control_format(&recipe->controls[1], 8.0f, 48000u, 1024u, 3u,
                          formatted, sizeof(formatted));
    CHECK(strstr(formatted, "43MS") != NULL);
    ts_cdp_control_format(&recipe->controls[0], 8.0f, 48000u, 1024u, 3u,
                          formatted, sizeof(formatted));
    CHECK(strcmp(formatted, "8 GROUPS") == 0);
    for (size_t index = 0; index < ts_cdp_factory_recipe_count(); ++index) {
        const TsCdpRecipe *catalog = ts_cdp_factory_recipe_at(index);
        TsCdpRecipeValues defaults;
        TsCdpCommand built[TS_CDP_MAX_STAGES];
        size_t count = 0u;
        CHECK(catalog != NULL && catalog->default_enabled &&
              catalog->bank == index / TS_CDP_BANK_SLOT_COUNT &&
              catalog->slot == index % TS_CDP_BANK_SLOT_COUNT);
        CHECK(ts_cdp_recipe_validate(catalog, error, sizeof(error)));
        CHECK(catalog->control_count >= 1u &&
              catalog->control_count <= TS_CDP_CONTROL_COUNT);
        ts_cdp_recipe_values_default(catalog, &defaults);
        CHECK(ts_cdp_recipe_build_commands(catalog, &defaults, 48000u, 48000u,
                                           built, &count, error, sizeof(error)));
        CHECK(count == catalog->stage_count && count >= 1u);
        for (size_t stage = 0; stage < count; ++stage) {
            CHECK(strcmp(built[stage].executable,
                         catalog->stages[stage].executable) == 0);
            CHECK(built[stage].argc > 0 && built[stage].expected_output[0] != '\0');
        }
        CHECK(strcmp(built[count - 1u].expected_output, "output.wav") == 0);
        for (size_t other = index + 1u; other < ts_cdp_factory_recipe_count(); ++other)
            CHECK(strcmp(catalog->id, ts_cdp_factory_recipe_at(other)->id) != 0 &&
                  strcmp(catalog->display_name,
                         ts_cdp_factory_recipe_at(other)->display_name) != 0);
    }
    {
        const TsCdpRecipe *filter = ts_cdp_recipe_find("filter_bank");
        size_t count = 0u;
        CHECK(filter != NULL);
        ts_cdp_recipe_values_default(filter, &values);
        values.controls[1] = 2.0f;
        values.tuning_hz = 440.0f;
        CHECK(ts_cdp_recipe_build_commands(filter, &values, 48000u, 48000u,
                                           commands, &count, error, sizeof(error)));
        CHECK(count == 1u && strcmp(commands[0].executable, "filter") == 0 &&
              strcmp(commands[0].arguments[0], "bank") == 0 &&
              fabs(atof(commands[0].arguments[6]) - 440.0) < 0.01 &&
              fabs(atof(commands[0].arguments[7]) - 1760.0) < 0.01);
    }
    {
        const TsCdpRecipe *brassage = ts_cdp_recipe_find("brassage");
        const TsCdpRecipe *shred = ts_cdp_recipe_find("shred");
        const TsCdpRecipe *stutter = ts_cdp_recipe_find("stutter");
        size_t count = 0u;
        CHECK(brassage != NULL && brassage->expected_output_channels == 2u);
        ts_cdp_recipe_values_default(brassage, &values);
        CHECK(ts_cdp_recipe_build_commands(brassage, &values, 48000u, 48000u,
                                           commands, &count, error, sizeof(error)));
        CHECK(count == 1u && strcmp(commands[0].arguments[0], "brassage") == 0 &&
              strcmp(commands[0].arguments[1], "6") == 0);
        CHECK(shred != NULL && !shred->duration_may_change &&
              shred->mix_policy == TS_CDP_MIX_EXACT_FRAMES);
        CHECK(stutter != NULL && stutter->seed_supported);
        ts_cdp_recipe_values_default(stutter, &values);
        CHECK(ts_cdp_recipe_build_commands(stutter, &values, 48000u, 48000u,
                                           commands, &count, error, sizeof(error)) &&
              strcmp(commands[0].arguments[9], "1") == 0);
    }
}

static void mix_tests(void)
{
    char error[160];
    float dry_data[] = {-0.5f, 0.0f, 0.5f};
    float wet_data[] = {0.5f, 0.5f, -0.5f};
    TsSample dry = {dry_data, 3u, 48000u, "dry", 0u, 1u};
    TsSample wet = {wet_data, 3u, 48000u, "wet", 0u, 1u};
    TsSample output;
    ts_sample_init(&output);
    CHECK(ts_transform_mix_samples(&dry, &wet, 0.0f, TS_CDP_MIX_EXACT_FRAMES,
                                   &output, error, sizeof(error)));
    CHECK(memcmp(output.data, dry_data, sizeof(dry_data)) == 0);
    ts_sample_free(&output);
    CHECK(ts_transform_mix_samples(&dry, &wet, 1.0f, TS_CDP_MIX_EXACT_FRAMES,
                                   &output, error, sizeof(error)));
    CHECK(memcmp(output.data, wet_data, sizeof(wet_data)) == 0);
    ts_sample_free(&output);
    CHECK(ts_transform_mix_samples(&dry, &wet, 0.5f, TS_CDP_MIX_EXACT_FRAMES,
                                   &output, error, sizeof(error)));
    CHECK(fabsf(output.data[0]) < 0.0001f && fabsf(output.data[1] - 0.25f) < 0.0001f);
    ts_sample_free(&output);
    wet.frames = 2u;
    CHECK(!ts_transform_mix_samples(&dry, &wet, 0.5f, TS_CDP_MIX_EXACT_FRAMES,
                                    &output, error, sizeof(error)));
    CHECK(ts_transform_mix_samples(&dry, &wet, 1.0f, TS_CDP_MIX_EXACT_FRAMES,
                                   &output, error, sizeof(error)));
    ts_sample_free(&output);
}

static void identity_and_apply_tests(void)
{
    char error[160];
    TsInstrument instrument;
    TsInstrument restored;
    TsTransformIdentity identity;
    TsTransformPreview preview;
    TsCdpRecipeValues values;
    TsCdpRunResult render;
    TsSample before;
    const TsCdpRecipe *recipe = ts_cdp_recipe_find("glisten");
    setup(&instrument, 8192u);
    ts_sample_init(&before);
    CHECK(ts_sample_clone(&before, &instrument.current, error, sizeof(error)));
    instrument.view_first = 1000u;
    instrument.view_last = 6000u;
    ts_cdp_recipe_values_default(recipe, &values);
    values.mix = 1.0f;
    CHECK(ts_transform_identity_capture(&identity, &instrument,
                                        TS_TRANSFORM_SELECTION, recipe, &values,
                                        7u, 11u, error, sizeof(error)));
    CHECK(identity.selection_first == 2048u && identity.selection_last == 4096u);
    CHECK(!identity.has_seed && identity.seed == 0u &&
          identity.input_channels == 1u && identity.expected_stage_count == 3u &&
          identity.expected_output_type == TS_CDP_IO_WAV);
    CHECK(ts_transform_identity_matches(&identity, &instrument,
                                        TS_TRANSFORM_SELECTION, recipe, &values,
                                        11u, error, sizeof(error)));
    CHECK(!ts_transform_identity_matches(&identity, &instrument,
                                         TS_TRANSFORM_SELECTION, recipe, &values,
                                         12u, error, sizeof(error)));
    {
        TsCdpRecipe changed_recipe = *recipe;
        changed_recipe.recipe_version += 1u;
        CHECK(!ts_transform_identity_matches(
            &identity, &instrument, TS_TRANSFORM_SELECTION, &changed_recipe,
            &values, 11u, error, sizeof(error)));
    }
    ts_instrument_set_selection(&instrument, 2049u, 4096u);
    CHECK(!ts_transform_identity_matches(&identity, &instrument,
                                         TS_TRANSFORM_SELECTION, recipe, &values,
                                         11u, error, sizeof(error)));
    ts_instrument_set_selection(&instrument, 2048u, 4096u);
    ts_cdp_run_result_init(&render);
    render.output.data = malloc(3072u * sizeof(float));
    CHECK(render.output.data != NULL);
    render.output.frames = 3072u;
    render.output.sample_rate = 48000u;
    for (size_t i = 0; i < render.output.frames; ++i) render.output.data[i] = 0.125f;
    render.status = TS_CDP_RUN_OK;
    render.safety = TS_CDP_SAFETY_SAFE;
    render.peak = 0.125f;
    ts_transform_preview_init(&preview);
    CHECK(ts_transform_prepare_preview(&instrument, &identity, recipe, &render,
                                       &preview, error, sizeof(error)));
    CHECK(preview.valid && preview.sample.data != render.output.data);
    CHECK(instrument.current.frames == 8192u && instrument.undo_count == 0);
    CHECK(ts_transform_apply_preview(&instrument, &preview,
                                     TS_TRANSFORM_SELECTION, recipe, &values,
                                     11u, error, sizeof(error)));
    CHECK(instrument.current.frames == 9216u && instrument.undo_count == 1);
    CHECK(instrument.selection_first == 2048u && instrument.selection_last == 5120u);
    CHECK(memcmp(instrument.current.data, before.data, 2048u * sizeof(float)) == 0);
    CHECK(memcmp(instrument.current.data + 5120u, before.data + 4096u,
                 (8192u - 4096u) * sizeof(float)) == 0);
    CHECK(instrument.view_first == 1000u && instrument.view_last == 7024u);
    CHECK(ts_instrument_undo(&instrument, error, sizeof(error)));
    CHECK(instrument.current.frames == before.frames &&
          memcmp(instrument.current.data, before.data,
                 before.frames * sizeof(float)) == 0);
    CHECK(instrument.selection_first == 2048u && instrument.selection_last == 4096u &&
          instrument.view_first == 1000u && instrument.view_last == 6000u);
    CHECK(ts_instrument_redo(&instrument, error, sizeof(error)));
    CHECK(instrument.current.frames == 9216u &&
          instrument.selection_first == 2048u && instrument.selection_last == 5120u);
    CHECK(ts_instrument_save_recipe(&instrument, "test-transform.tsr",
                                    error, sizeof(error)));
    ts_instrument_init(&restored);
    CHECK(ts_instrument_load_recipe(&restored, "test-transform.tsr",
                                    error, sizeof(error)));
    CHECK(restored.current.frames == instrument.current.frames &&
          ts_sample_hash(&restored.current) == ts_sample_hash(&instrument.current) &&
          restored.selection_first == instrument.selection_first &&
          restored.selection_last == instrument.selection_last &&
          restored.view_first == instrument.view_first &&
          restored.view_last == instrument.view_last &&
          restored.post_edit_count == 1 &&
          restored.post_edits[0].kind == TS_POST_MATERIAL_REPLACE);
    {
        uint64_t restored_hash = ts_sample_hash(&restored.current);
        TsProcessRecipe live = restored.process;
        live.body = 1.0f;
        CHECK(ts_instrument_set_process(&restored, &live, error, sizeof(error)) &&
              ts_sample_hash(&restored.current) != restored_hash);
    }
    remove("test-transform.tsr");
    ts_instrument_free(&restored);
    ts_transform_preview_free(&preview);
    ts_cdp_run_result_free(&render);
    ts_sample_free(&before);
    ts_instrument_free(&instrument);
}

static void tile_isolation_and_whole_tests(void)
{
    char error[160];
    TsInstrument instrument;
    TsTransformIdentity identity;
    TsCdpRecipeValues values;
    TsSample input;
    const TsCdpRecipe *recipe = ts_cdp_recipe_find("glisten");
    setup(&instrument, 8192u);
    ts_cdp_recipe_values_default(recipe, &values);
    CHECK(ts_transform_identity_capture(&identity, &instrument, TS_TRANSFORM_WHOLE,
                                        recipe, &values, 1u, 1u,
                                        error, sizeof(error)));
    ts_sample_init(&input);
    CHECK(ts_transform_extract_input(&instrument, &identity, &input,
                                     error, sizeof(error)));
    CHECK(input.frames == instrument.current.frames && input.data != instrument.current.data);
    input.data[0] = 0.9f;
    CHECK(instrument.current.data[0] != input.data[0]);
    ts_sample_free(&input);
    CHECK(ts_instrument_copy_selected(&instrument, 1, error, sizeof(error)));
    CHECK(instrument.selected_slot == 1);
    CHECK(!ts_transform_identity_matches(&identity, &instrument, TS_TRANSFORM_WHOLE,
                                         recipe, &values, 1u,
                                         error, sizeof(error)));
    ts_instrument_free(&instrument);
}

static void replacement_lengths_and_selection_persistence_tests(void)
{
    char error[160];
    TsInstrument instrument;
    TsSample before;
    TsSample rendered;
    setup(&instrument, 8192u);
    ts_sample_init(&before);
    ts_sample_init(&rendered);
    CHECK(ts_sample_clone(&before, &instrument.current, error, sizeof(error)));
    rendered.frames = 1024u;
    rendered.sample_rate = 48000u;
    rendered.data = malloc(rendered.frames * sizeof(*rendered.data));
    CHECK(rendered.data != NULL);
    for (size_t i = 0; i < rendered.frames; ++i) rendered.data[i] = -0.1f;
    instrument.view_first = 1000u;
    instrument.view_last = 6000u;
    CHECK(ts_instrument_apply_rendered_replacement(
        &instrument, &rendered, 2048u, 4096u, error, sizeof(error)));
    CHECK(instrument.current.frames == 7168u &&
          instrument.selection_first == 2048u && instrument.selection_last == 3072u);
    CHECK(memcmp(instrument.current.data, before.data, 2048u * sizeof(float)) == 0);
    CHECK(memcmp(instrument.current.data + 3072u, before.data + 4096u,
                 (8192u - 4096u) * sizeof(float)) == 0);
    CHECK(ts_instrument_undo(&instrument, error, sizeof(error)) &&
          instrument.view_first == 1000u && instrument.view_last == 6000u &&
          instrument.selection_first == 2048u && instrument.selection_last == 4096u);
    ts_sample_free(&rendered);
    rendered.frames = before.frames;
    rendered.sample_rate = before.sample_rate;
    rendered.data = malloc(rendered.frames * sizeof(*rendered.data));
    CHECK(rendered.data != NULL);
    memcpy(rendered.data, before.data, rendered.frames * sizeof(*rendered.data));
    CHECK(ts_instrument_apply_rendered_replacement(
        &instrument, &rendered, 0u, instrument.current.frames,
        error, sizeof(error)));
    CHECK(instrument.current.frames == 8192u && instrument.view_first == 1000u &&
          instrument.view_last == 6000u && instrument.selection_first == 0u &&
          instrument.selection_last == 8192u);
    CHECK(ts_instrument_undo(&instrument, error, sizeof(error)) &&
          instrument.selection_first == 2048u && instrument.selection_last == 4096u &&
          instrument.view_first == 1000u && instrument.view_last == 6000u);
    CHECK(ts_instrument_copy_selected(&instrument, 1, error, sizeof(error)));
    ts_instrument_set_selection(&instrument, 100u, instrument.current.frames);
    CHECK(ts_instrument_select_bank(&instrument, 0, error, sizeof(error)) &&
          instrument.selection_first == 2048u && instrument.selection_last == 4096u);
    CHECK(ts_instrument_select_bank(&instrument, 1, error, sizeof(error)) &&
          instrument.selection_first == 100u &&
          instrument.selection_last == instrument.current.frames);
    ts_sample_free(&rendered);
    ts_sample_free(&before);
    ts_instrument_free(&instrument);
}

static void rendered_replacement_native_process_regression_tests(void)
{
    char error[160];
    TsInstrument instrument;
    TsInstrument isolated;
    TsSample accepted;
    TsSample rendered;
    TsProcessRecipe neutral;
    TsProcessRecipe process;
    uint64_t accepted_hash;
    uint64_t a_hash;
    uint64_t b_hash;
    uint64_t other_hash;

    /* Reproducer for the PR-31 failure: this same sequence produced an
       unchanged hash when the accepted render was a post-process patch. */
    setup(&instrument, 8192u);
    ts_sample_init(&accepted);
    ts_sample_init(&rendered);
    rendered.frames = instrument.current.frames;
    rendered.sample_rate = instrument.current.sample_rate;
    rendered.data = malloc(rendered.frames * sizeof(*rendered.data));
    CHECK(rendered.data != NULL);
    for (size_t frame = 0; frame < rendered.frames; ++frame) {
        double phase = (double)frame * 2.0 * 3.14159265358979323846 / 73.0;
        rendered.data[frame] = 0.35f * (float)sin(phase) +
                               ((frame & 1u) ? 0.12f : -0.12f);
    }
    CHECK(ts_instrument_apply_rendered_replacement(
        &instrument, &rendered, 0u, instrument.current.frames,
        error, sizeof(error)));
    accepted_hash = ts_sample_hash(&instrument.current);
    CHECK(instrument.post_edit_count == 1 &&
          instrument.post_edits[0].kind == TS_POST_MATERIAL_REPLACE);
    CHECK(ts_sample_clone(&accepted, &instrument.current, error, sizeof(error)));
    ts_process_recipe_reset(&neutral);
    neutral.seed = instrument.process.seed;
    CHECK(ts_process_recipe_equal(&instrument.process, &neutral));

    process = neutral;
    process.body = 1.0f;
    CHECK(ts_instrument_set_process(&instrument, &process, error, sizeof(error)));
    CHECK(ts_sample_hash(&instrument.current) != accepted_hash);

    process = neutral;
    process.edge = 1.0f;
    CHECK(ts_instrument_set_process(&instrument, &process, error, sizeof(error)));
    CHECK(ts_sample_hash(&instrument.current) != accepted_hash);

    process = neutral;
    process.drift = 1.0f;
    CHECK(ts_instrument_set_process(&instrument, &process, error, sizeof(error)));
    CHECK(ts_sample_hash(&instrument.current) != accepted_hash);

    process = neutral;
    process.noise_enabled = 1;
    process.noise_amount = 1.0f;
    process.noise_color = TS_NOISE_METALLIC;
    CHECK(ts_instrument_set_process(&instrument, &process, error, sizeof(error)));
    CHECK(ts_sample_hash(&instrument.current) != accepted_hash);

    process = neutral;
    process.shaper_enabled = 1;
    process.shaper_mode = TS_SHAPER_FOLD;
    process.shaper_drive = 14.0f;
    process.shaper_mix = 1.0f;
    CHECK(ts_instrument_set_process(&instrument, &process, error, sizeof(error)));
    CHECK(ts_sample_hash(&instrument.current) != accepted_hash);

    process = neutral;
    process.delay_enabled = 1;
    process.delay_seconds = 0.005f;
    process.delay_feedback = 0.8f;
    process.delay_damping = 0.1f;
    process.delay_mix = 1.0f;
    CHECK(ts_instrument_set_process(&instrument, &process, error, sizeof(error)));
    CHECK(ts_sample_hash(&instrument.current) != accepted_hash);

    process = neutral;
    process.reverb_enabled = 1;
    process.reverb_decay = 0.95f;
    process.reverb_damping = 0.1f;
    process.reverb_mix = 1.0f;
    CHECK(ts_instrument_set_process(&instrument, &process, error, sizeof(error)));
    CHECK(ts_sample_hash(&instrument.current) != accepted_hash);

    /* Every parameter move rebuilds from the stable accepted material. */
    process = neutral;
    process.body = 0.0f;
    CHECK(ts_instrument_set_process(&instrument, &process, error, sizeof(error)));
    a_hash = ts_sample_hash(&instrument.current);
    process.body = 1.0f;
    CHECK(ts_instrument_set_process(&instrument, &process, error, sizeof(error)));
    b_hash = ts_sample_hash(&instrument.current);
    CHECK(a_hash != b_hash);
    process.body = 0.0f;
    CHECK(ts_instrument_set_process(&instrument, &process, error, sizeof(error)));
    CHECK(ts_sample_hash(&instrument.current) == a_hash);
    ts_sample_free(&accepted);
    ts_sample_free(&rendered);
    ts_instrument_free(&instrument);

    /* A duration-changing selection checkpoint remains live under the native
       stage, including inside the transformed range. */
    setup(&instrument, 8192u);
    ts_sample_init(&accepted);
    ts_sample_init(&rendered);
    rendered.frames = 3072u;
    rendered.sample_rate = instrument.current.sample_rate;
    rendered.data = malloc(rendered.frames * sizeof(*rendered.data));
    CHECK(rendered.data != NULL);
    for (size_t frame = 0; frame < rendered.frames; ++frame)
        rendered.data[frame] = 0.42f * (float)sin((double)frame * 0.071);
    CHECK(ts_instrument_apply_rendered_replacement(
        &instrument, &rendered, instrument.selection_first,
        instrument.selection_last, error, sizeof(error)));
    CHECK(instrument.current.frames == 9216u &&
          instrument.selection_first == 2048u &&
          instrument.selection_last == 5120u);
    CHECK(ts_sample_clone(&accepted, &instrument.current, error, sizeof(error)));
    process = instrument.process;
    process.shaper_enabled = 1;
    process.shaper_mode = TS_SHAPER_CLIP;
    process.shaper_drive = 12.0f;
    process.shaper_mix = 1.0f;
    CHECK(ts_instrument_set_process(&instrument, &process, error, sizeof(error)));
    CHECK(memcmp(instrument.current.data + instrument.selection_first,
                 accepted.data + instrument.selection_first,
                 (instrument.selection_last - instrument.selection_first) *
                 sizeof(*accepted.data)) != 0);
    ts_sample_free(&accepted);
    ts_sample_free(&rendered);
    ts_instrument_free(&instrument);

    /* Whole-tile natural-duration output also becomes live native material. */
    setup(&instrument, 8192u);
    ts_instrument_clear_selection(&instrument);
    ts_sample_init(&rendered);
    rendered.frames = 9216u;
    rendered.sample_rate = instrument.current.sample_rate;
    rendered.data = malloc(rendered.frames * sizeof(*rendered.data));
    CHECK(rendered.data != NULL);
    for (size_t frame = 0; frame < rendered.frames; ++frame)
        rendered.data[frame] = 0.31f * (float)sin((double)frame * 0.043);
    CHECK(ts_instrument_apply_rendered_replacement(
        &instrument, &rendered, 0u, instrument.current.frames,
        error, sizeof(error)));
    accepted_hash = ts_sample_hash(&instrument.current);
    CHECK(instrument.current.frames == rendered.frames &&
          instrument.selection_first == 0u &&
          instrument.selection_last == rendered.frames);
    process = instrument.process;
    process.edge = 1.0f;
    CHECK(ts_instrument_set_process(&instrument, &process, error, sizeof(error)) &&
          ts_sample_hash(&instrument.current) != accepted_hash);
    CHECK(ts_instrument_undo(&instrument, error, sizeof(error)) &&
          ts_sample_hash(&instrument.current) == accepted_hash);
    CHECK(ts_instrument_undo(&instrument, error, sizeof(error)) &&
          instrument.current.frames == 8192u && !instrument.has_selection);
    CHECK(ts_instrument_redo(&instrument, error, sizeof(error)) &&
          instrument.current.frames == 9216u);
    ts_sample_free(&rendered);
    ts_instrument_free(&instrument);

    /* Current already contains the active native process used as Transform
       input. Apply must accept that preview exactly, reset the new live stage,
       and keep Undo/Redo graph state exact rather than processing it twice. */
    setup(&instrument, 8192u);
    ts_sample_init(&rendered);
    process = instrument.process;
    process.body = 1.0f;
    process.edge = 0.45f;
    process.noise_enabled = 1;
    process.noise_amount = 0.35f;
    CHECK(ts_instrument_set_process(&instrument, &process, error, sizeof(error)));
    CHECK(ts_sample_clone(&rendered, &instrument.current, error, sizeof(error)));
    for (size_t frame = 0; frame < rendered.frames; ++frame)
        rendered.data[frame] *= -0.73f;
    {
        int undo_before = instrument.undo_count;
        TsProcessRecipe prior_process = instrument.process;
        uint64_t prior_hash = ts_sample_hash(&instrument.current);
        uint64_t rendered_hash = ts_sample_hash(&rendered);
        instrument.view_first = 777u;
        instrument.view_last = 7000u;
        CHECK(ts_instrument_apply_rendered_replacement(
            &instrument, &rendered, 0u, instrument.current.frames,
            error, sizeof(error)));
        CHECK(instrument.undo_count == undo_before + 1 &&
              ts_sample_hash(&instrument.current) == rendered_hash &&
              instrument.view_first == 777u && instrument.view_last == 7000u);
        ts_process_recipe_reset(&neutral);
        neutral.seed = prior_process.seed;
        CHECK(ts_process_recipe_equal(&instrument.process, &neutral));
        CHECK(ts_instrument_undo(&instrument, error, sizeof(error)) &&
              ts_sample_hash(&instrument.current) == prior_hash &&
              ts_process_recipe_equal(&instrument.process, &prior_process) &&
              instrument.selection_first == 2048u &&
              instrument.selection_last == 4096u);
        CHECK(ts_instrument_redo(&instrument, error, sizeof(error)) &&
              ts_sample_hash(&instrument.current) == rendered_hash &&
              ts_process_recipe_equal(&instrument.process, &neutral));
        process = instrument.process;
        process.body = 0.0f;
        CHECK(ts_instrument_set_process(&instrument, &process, error, sizeof(error)) &&
              ts_sample_hash(&instrument.current) != rendered_hash);
    }
    ts_sample_free(&rendered);
    ts_instrument_free(&instrument);

    /* The checkpoint and all later native processing remain tile-local. */
    setup(&isolated, 8192u);
    CHECK(ts_instrument_copy_selected(&isolated, 1, error, sizeof(error)));
    other_hash = ts_sample_hash(&isolated.current);
    CHECK(ts_instrument_select_bank(&isolated, 0, error, sizeof(error)));
    ts_sample_init(&rendered);
    CHECK(ts_sample_clone(&rendered, &isolated.current, error, sizeof(error)));
    for (size_t frame = 0; frame < rendered.frames; ++frame)
        rendered.data[frame] = -rendered.data[frame];
    CHECK(ts_instrument_apply_rendered_replacement(
        &isolated, &rendered, 0u, isolated.current.frames,
        error, sizeof(error)));
    process = isolated.process;
    process.edge = 1.0f;
    CHECK(ts_instrument_set_process(&isolated, &process, error, sizeof(error)));
    CHECK(ts_sample_hash(&isolated.bank[1].sample) == other_hash);
    CHECK(ts_instrument_select_bank(&isolated, 1, error, sizeof(error)) &&
          ts_sample_hash(&isolated.current) == other_hash);
    ts_sample_free(&rendered);
    ts_instrument_free(&isolated);
}

static void native_dsp_recipe_and_preview_tests(void)
{
    char error[160];
    TsRecipeBank bank;
    TsInstrument instrument;
    TsDspTransformIdentity identity;
    TsDspTransformPreview preview;
    TsSample before;
    TsSample input;
    TsSample rendered;
    TsCdpSafetyStatus safety = TS_CDP_SAFETY_INVALID;
    float peak = 0.0f;
    double dc = 0.0;
    int clipped = 0;
    size_t first;
    size_t last;
    ts_recipe_bank_init(&bank);
    for (int slot = 0; slot < TS_FACTORY_RECIPE_COUNT; ++slot) {
        const TsPortableRecipe *preset = &bank.slots[slot];
        const TsDspPresetSpec *spec = ts_dsp_preset_spec(preset->dsp_profile);
        CHECK(preset->has_dsp_controls && spec != NULL);
        CHECK(spec->control_count >= 2u &&
              spec->control_count <= TS_DSP_CONTROL_COUNT);
        for (size_t index = 0; index < spec->control_count; ++index) {
            CHECK(spec->controls[index].label != NULL &&
                  spec->controls[index].label[0] != '\0');
            CHECK(preset->dsp_controls[index] >= 0.0f &&
                  preset->dsp_controls[index] <= 1.0f);
        }
    }
    {
        TsPortableRecipe edited = bank.slots[4];
        CHECK(ts_dsp_preset_set_control(&edited, 0u, -1.0f));
        CHECK(edited.dsp_controls[0] == 0.0f);
        CHECK(ts_dsp_preset_set_control(&edited, 0u, 2.0f));
        CHECK(edited.dsp_controls[0] == 1.0f);
        CHECK(!ts_dsp_preset_set_control(&edited, 0u, NAN));
        CHECK(!ts_process_recipe_equal(&edited.process,
                                       &bank.slots[4].process));
    }

    setup(&instrument, 8192u);
    first = instrument.selection_first;
    last = instrument.selection_last;
    instrument.view_first = 900u;
    instrument.view_last = 6100u;
    ts_sample_init(&before);
    ts_sample_init(&input);
    ts_sample_init(&rendered);
    ts_dsp_transform_preview_init(&preview);
    CHECK(ts_sample_clone(&before, &instrument.current, error, sizeof(error)));
    CHECK(ts_dsp_transform_identity_capture(
        &identity, &instrument, TS_TRANSFORM_SELECTION, 1,
        &bank.slots[1].process, 77u, 9u, error, sizeof(error)));
    CHECK(ts_dsp_transform_identity_matches(
        &identity, &instrument, TS_TRANSFORM_SELECTION, 1,
        &bank.slots[1].process, 9u, error, sizeof(error)));
    CHECK(ts_dsp_transform_extract_input(&instrument, &identity, &input,
                                         error, sizeof(error)));
    CHECK(input.frames == last - first &&
          input.data != instrument.current.data + first);
    CHECK(ts_dsp_transform_render(&input, &bank.slots[1].process, &rendered,
                                  &safety, &peak, &dc, &clipped,
                                  error, sizeof(error)));
    CHECK(rendered.frames == input.frames && isfinite(peak) && isfinite(dc));
    CHECK(ts_sample_hash(&instrument.current) == ts_sample_hash(&before) &&
          instrument.undo_count == 0);
    CHECK(ts_dsp_transform_prepare_preview(
        &instrument, &identity, &rendered, safety, peak, dc, clipped,
        &preview, error, sizeof(error)));
    CHECK(preview.valid && preview.sample.data != rendered.data &&
          preview.replacement_first == first &&
          preview.replacement_last == last);
    CHECK(ts_sample_hash(&instrument.current) == ts_sample_hash(&before) &&
          instrument.undo_count == 0);
    ts_dsp_transform_preview_free(&preview);
    CHECK(!preview.valid && preview.sample.data == NULL &&
          ts_sample_hash(&instrument.current) == ts_sample_hash(&before) &&
          instrument.undo_count == 0);
    CHECK(ts_dsp_transform_prepare_preview(
        &instrument, &identity, &rendered, safety, peak, dc, clipped,
        &preview, error, sizeof(error)));
    ts_instrument_set_selection(&instrument, first + 1u, last);
    CHECK(!ts_dsp_transform_identity_matches(
        &identity, &instrument, TS_TRANSFORM_SELECTION, 1,
        &bank.slots[1].process, 9u, error, sizeof(error)));
    ts_instrument_set_selection(&instrument, first, last);
    {
        TsProcessRecipe changed = bank.slots[1].process;
        changed.body = changed.body > 0.5f ? changed.body - 0.1f : changed.body + 0.1f;
        CHECK(!ts_dsp_transform_identity_matches(
            &identity, &instrument, TS_TRANSFORM_SELECTION, 1,
            &changed, 9u, error, sizeof(error)));
    }
    CHECK(ts_dsp_transform_apply_preview(
        &instrument, &preview, TS_TRANSFORM_SELECTION, 1,
        &bank.slots[1].process, 9u, error, sizeof(error)));
    CHECK(instrument.undo_count == 1 && instrument.current.frames == before.frames);
    CHECK(instrument.selection_first == first && instrument.selection_last == last);
    CHECK(memcmp(instrument.current.data, before.data,
                 first * sizeof(*before.data)) == 0);
    CHECK(memcmp(instrument.current.data + last, before.data + last,
                 (before.frames - last) * sizeof(*before.data)) == 0);
    CHECK(instrument.view_first == 900u && instrument.view_last == 6100u);
    CHECK(ts_instrument_undo(&instrument, error, sizeof(error)));
    CHECK(ts_sample_hash(&instrument.current) == ts_sample_hash(&before) &&
          instrument.selection_first == first && instrument.selection_last == last &&
          instrument.view_first == 900u && instrument.view_last == 6100u);
    CHECK(ts_instrument_redo(&instrument, error, sizeof(error)));
    CHECK(instrument.selection_first == first && instrument.selection_last == last);
    {
        uint64_t accepted_hash = ts_sample_hash(&instrument.current);
        TsProcessRecipe live = instrument.process;
        CHECK(instrument.post_edit_count == 1 &&
              instrument.post_edits[0].kind == TS_POST_MATERIAL_REPLACE);
        live.edge = 1.0f;
        CHECK(ts_instrument_set_process(&instrument, &live, error, sizeof(error)) &&
              ts_sample_hash(&instrument.current) != accepted_hash);
    }
    ts_dsp_transform_preview_free(&preview);
    ts_sample_free(&rendered);
    ts_sample_free(&input);
    ts_sample_free(&before);
    ts_instrument_free(&instrument);
}

static void native_dsp_direct_and_body_range_tests(void)
{
    char error[160];
    TsRecipeBank bank;
    TsInstrument instrument;
    TsSample before;
    TsSample center;
    TsSample light;
    TsSample heavy;
    TsProcessRecipe process;
    double light_difference = 0.0;
    double heavy_difference = 0.0;
    size_t first;
    size_t last;
    ts_recipe_bank_init(&bank);
    setup(&instrument, 8192u);
    first = instrument.selection_first;
    last = instrument.selection_last;
    ts_sample_init(&before);
    CHECK(ts_sample_clone(&before, &instrument.current, error, sizeof(error)));
    CHECK(ts_dsp_transform_apply_direct(
        &instrument, 4, &bank.slots[4].process,
        TS_TRANSFORM_SELECTION, error, sizeof(error)));
    CHECK(instrument.undo_count == 1 && instrument.current.frames == before.frames);
    CHECK(memcmp(instrument.current.data, before.data,
                 first * sizeof(*before.data)) == 0);
    CHECK(memcmp(instrument.current.data + last, before.data + last,
                 (before.frames - last) * sizeof(*before.data)) == 0);
    CHECK(ts_sample_hash(&instrument.current) != ts_sample_hash(&before));
    CHECK(ts_instrument_undo(&instrument, error, sizeof(error)));
    CHECK(ts_sample_hash(&instrument.current) == ts_sample_hash(&before));
    ts_instrument_clear_selection(&instrument);
    CHECK(ts_dsp_transform_apply_direct(
        &instrument, 1, &bank.slots[1].process,
        TS_TRANSFORM_WHOLE, error, sizeof(error)));
    CHECK(instrument.undo_count == 1 &&
          ts_sample_hash(&instrument.current) != ts_sample_hash(&before) &&
          instrument.selection_first == 0u &&
          instrument.selection_last == instrument.current.frames);
    CHECK(ts_instrument_undo(&instrument, error, sizeof(error)) &&
          ts_sample_hash(&instrument.current) == ts_sample_hash(&before) &&
          !instrument.has_selection);

    ts_sample_init(&center);
    ts_sample_init(&light);
    ts_sample_init(&heavy);
    ts_process_recipe_reset(&process);
    process.body = 0.5f;
    CHECK(ts_sample_process(&center, &before, 0u, before.frames,
                            &process, error, sizeof(error)));
    CHECK(ts_sample_hash(&center) == ts_sample_hash(&before));
    process.body = 0.0f;
    CHECK(ts_sample_process(&light, &before, 0u, before.frames,
                            &process, error, sizeof(error)));
    process.body = 1.0f;
    CHECK(ts_sample_process(&heavy, &before, 0u, before.frames,
                            &process, error, sizeof(error)));
    for (size_t frame = 0; frame < before.frames; ++frame) {
        light_difference += fabs((double)light.data[frame] - before.data[frame]);
        heavy_difference += fabs((double)heavy.data[frame] - before.data[frame]);
    }
    CHECK(light_difference / before.frames > 0.02);
    CHECK(heavy_difference / before.frames > 0.02);
    CHECK(ts_sample_hash(&light) != ts_sample_hash(&heavy));
    ts_sample_free(&heavy);
    ts_sample_free(&light);
    ts_sample_free(&center);
    ts_sample_free(&before);
    ts_instrument_free(&instrument);
}

static size_t rising_zero_crossings(const TsSample *sample)
{
    size_t crossings = 0u;
    if (sample == NULL || sample->data == NULL) return 0u;
    for (size_t frame = 1u; frame < sample->frames; ++frame)
        if (sample->data[frame - 1u] <= 0.0f && sample->data[frame] > 0.0f)
            ++crossings;
    return crossings;
}

static void curated_dsp_bank_and_render_tests(void)
{
    char error[160];
    TsSample input;
    TsSample output;
    TsSample low;
    TsSample high;
    TsCdpSafetyStatus safety;
    float peak;
    double dc;
    int clipped;
    ts_sample_init(&input);
    ts_sample_init(&output);
    ts_sample_init(&low);
    ts_sample_init(&high);
    input.frames = 12000u;
    input.sample_rate = 48000u;
    input.data = malloc(input.frames * sizeof(*input.data));
    CHECK(input.data != NULL);
    if (input.data == NULL) return;
    snprintf(input.name, sizeof(input.name), "curated dsp source");
    for (size_t frame = 0; frame < input.frames; ++frame) {
        float time = (float)frame / (float)input.sample_rate;
        input.data[frame] = sinf(time * 6.28318530718f * 113.0f) * 0.32f +
                            sinf(time * 6.28318530718f * 941.0f) * 0.13f +
                            ((frame % 997u) == 0u ? 0.38f : 0.0f);
    }
    CHECK(ts_dsp_factory_recipe_count() == TS_DSP_FACTORY_RECIPE_COUNT);
    for (size_t index = 0; index < ts_dsp_factory_recipe_count(); ++index) {
        const TsDspRecipe *recipe = ts_dsp_factory_recipe_at(index);
        TsDspRecipeValues defaults;
        TsDspRecipeValues minimums;
        TsDspRecipeValues maximums;
        CHECK(recipe != NULL && recipe->kind == (TsDspRecipeKind)index);
        CHECK(recipe != NULL && recipe->bank == index / TS_DSP_BANK_SLOT_COUNT &&
              recipe->slot == index % TS_DSP_BANK_SLOT_COUNT);
        CHECK(recipe == ts_dsp_factory_recipe_for_slot(
                            index / TS_DSP_BANK_SLOT_COUNT,
                            index % TS_DSP_BANK_SLOT_COUNT));
        CHECK(recipe != NULL && recipe == ts_dsp_recipe_find(recipe->id));
        CHECK(ts_dsp_recipe_validate(recipe, error, sizeof(error)));
        CHECK(recipe->control_count == TS_DSP_CONTROL_COUNT);
        CHECK(recipe->primitive == (index >= TS_DSP_BANK_SLOT_COUNT));
        for (size_t other = index + 1u;
             other < ts_dsp_factory_recipe_count(); ++other) {
            CHECK(strcmp(recipe->id, ts_dsp_factory_recipe_at(other)->id) != 0);
            CHECK(strcmp(recipe->display_name,
                         ts_dsp_factory_recipe_at(other)->display_name) != 0);
        }
        ts_dsp_recipe_values_default(recipe, &defaults);
        minimums = defaults;
        maximums = defaults;
        for (size_t control = 0; control < recipe->control_count; ++control) {
            char formatted[32];
            CHECK(recipe->controls[control].label != NULL &&
                  recipe->controls[control].label[0] != '\0');
            CHECK(defaults.controls[control] >= 0.0f &&
                  defaults.controls[control] <= 1.0f);
            minimums.controls[control] = 0.0f;
            maximums.controls[control] = 1.0f;
            ts_dsp_recipe_control_format(&recipe->controls[control],
                                         defaults.controls[control],
                                         formatted, sizeof(formatted));
            CHECK(formatted[0] != '\0');
        }
        safety = TS_CDP_SAFETY_INVALID;
        CHECK(ts_dsp_transform_render_recipe(
            &input, recipe, &defaults, &output, &safety, &peak, &dc, &clipped,
            error, sizeof(error)));
        CHECK(output.data != NULL && output.data != input.data &&
              output.frames == input.frames &&
              output.sample_rate == input.sample_rate &&
              safety != TS_CDP_SAFETY_INVALID && isfinite(peak) && isfinite(dc) &&
              peak > 0.00001f);
        for (size_t frame = 0; frame < output.frames; ++frame)
            CHECK(isfinite(output.data[frame]) && fabsf(output.data[frame]) <= 1.00001f);
        ts_sample_free(&output);
        CHECK(ts_dsp_transform_render_recipe(
            &input, recipe, &minimums, &low, &safety, &peak, &dc, &clipped,
            error, sizeof(error)));
        CHECK(ts_dsp_transform_render_recipe(
            &input, recipe, &maximums, &high, &safety, &peak, &dc, &clipped,
            error, sizeof(error)));
        CHECK(low.frames == input.frames && high.frames == input.frames);
        for (size_t frame = 0; frame < input.frames; ++frame)
            CHECK(isfinite(low.data[frame]) && isfinite(high.data[frame]) &&
                  fabsf(low.data[frame]) <= 1.00001f &&
                  fabsf(high.data[frame]) <= 1.00001f);
        ts_sample_free(&high);
        ts_sample_free(&low);
    }
    CHECK(ts_dsp_factory_recipe_at(TS_DSP_FACTORY_RECIPE_COUNT) == NULL);
    CHECK(ts_dsp_factory_recipe_for_slot(TS_DSP_BANK_COUNT, 0u) == NULL);
    CHECK(ts_dsp_recipe_find("missing") == NULL);
    {
        TsDspRecipe invalid = *ts_dsp_recipe_find("space");
        invalid.control_count = 0u;
        CHECK(!ts_dsp_recipe_validate(&invalid, error, sizeof(error)));
        invalid = *ts_dsp_recipe_find("space");
        invalid.controls[0].maximum = invalid.controls[0].minimum;
        CHECK(!ts_dsp_recipe_validate(&invalid, error, sizeof(error)));
    }
    {
        const TsDspRecipe *sine = ts_dsp_recipe_find("sine");
        TsDspRecipeValues values;
        TsSample generated;
        TsSample dry;
        TsSample mixed;
        ts_sample_init(&generated);
        ts_sample_init(&dry);
        ts_sample_init(&mixed);
        ts_dsp_recipe_values_default(sine, &values);
        values.controls[3] = 0.0f;
        CHECK(ts_dsp_transform_render_recipe(
            &input, sine, &values, &generated, &safety, &peak, &dc, &clipped,
            error, sizeof(error)));
        values.controls[3] = 1.0f;
        CHECK(ts_dsp_transform_render_recipe(
            &input, sine, &values, &dry, &safety, &peak, &dc, &clipped,
            error, sizeof(error)));
        CHECK(memcmp(dry.data, input.data,
                     input.frames * sizeof(*input.data)) == 0);
        values.controls[3] = 0.5f;
        CHECK(ts_dsp_transform_render_recipe(
            &input, sine, &values, &mixed, &safety, &peak, &dc, &clipped,
            error, sizeof(error)));
        for (size_t frame = 0; frame < input.frames; ++frame)
            CHECK(fabsf(mixed.data[frame] -
                        (generated.data[frame] + input.data[frame]) * 0.5f) <
                  0.00001f);
        ts_sample_free(&mixed);
        ts_sample_free(&dry);
        ts_sample_free(&generated);
    }
    {
        const TsDspRecipe *sine = ts_dsp_recipe_find("sine");
        TsDspRecipeValues values;
        TsSample bass;
        TsSample treble;
        ts_sample_init(&bass);
        ts_sample_init(&treble);
        ts_dsp_recipe_values_default(sine, &values);
        values.controls[1] = 0.0f;
        values.controls[2] = 0.0f;
        values.controls[3] = 0.0f;
        values.controls[0] = 0.15f;
        CHECK(ts_dsp_transform_render_recipe(
            &input, sine, &values, &bass, &safety, &peak, &dc, &clipped,
            error, sizeof(error)));
        values.controls[0] = 0.65f;
        CHECK(ts_dsp_transform_render_recipe(
            &input, sine, &values, &treble, &safety, &peak, &dc, &clipped,
            error, sizeof(error)));
        CHECK(rising_zero_crossings(&treble) >
              rising_zero_crossings(&bass) * 3u);
        ts_sample_free(&treble);
        ts_sample_free(&bass);
    }
    ts_sample_free(&input);
}

static void curated_dsp_preview_apply_tests(void)
{
    char error[160];
    TsInstrument instrument;
    TsSample before;
    TsSample input;
    TsSample rendered;
    TsDspTransformIdentity identity;
    TsDspTransformPreview preview;
    TsDspRecipeValues values;
    const TsDspRecipe *recipe = ts_dsp_recipe_find("metal");
    TsCdpSafetyStatus safety = TS_CDP_SAFETY_INVALID;
    float peak = 0.0f;
    double dc = 0.0;
    int clipped = 0;
    size_t first;
    size_t last;
    setup(&instrument, 8192u);
    first = instrument.selection_first;
    last = instrument.selection_last;
    instrument.view_first = 700u;
    instrument.view_last = 6500u;
    ts_sample_init(&before);
    ts_sample_init(&input);
    ts_sample_init(&rendered);
    ts_dsp_transform_preview_init(&preview);
    CHECK(ts_sample_clone(&before, &instrument.current, error, sizeof(error)));
    ts_dsp_recipe_values_default(recipe, &values);
    CHECK(ts_dsp_transform_identity_capture_recipe(
        &identity, &instrument, TS_TRANSFORM_SELECTION, recipe, &values,
        101u, 19u, error, sizeof(error)));
    CHECK(ts_dsp_transform_identity_matches_recipe(
        &identity, &instrument, TS_TRANSFORM_SELECTION, recipe, &values,
        19u, error, sizeof(error)));
    {
        TsDspRecipeValues changed = values;
        changed.controls[1] += 0.01f;
        CHECK(!ts_dsp_transform_identity_matches_recipe(
            &identity, &instrument, TS_TRANSFORM_SELECTION, recipe, &changed,
            19u, error, sizeof(error)));
    }
    CHECK(ts_dsp_transform_extract_input(&instrument, &identity, &input,
                                         error, sizeof(error)));
    CHECK(ts_dsp_transform_render_recipe(
        &input, recipe, &values, &rendered, &safety, &peak, &dc, &clipped,
        error, sizeof(error)));
    CHECK(ts_dsp_transform_prepare_preview(
        &instrument, &identity, &rendered, safety, peak, dc, clipped,
        &preview, error, sizeof(error)));
    CHECK(preview.valid && instrument.undo_count == 0 &&
          ts_sample_hash(&instrument.current) == ts_sample_hash(&before));
    CHECK(ts_dsp_transform_apply_preview_recipe(
        &instrument, &preview, TS_TRANSFORM_SELECTION, recipe, &values,
        19u, error, sizeof(error)));
    CHECK(instrument.undo_count == 1 && instrument.current.frames == before.frames &&
          instrument.selection_first == first && instrument.selection_last == last &&
          instrument.view_first == 700u && instrument.view_last == 6500u);
    CHECK(memcmp(instrument.current.data, before.data,
                 first * sizeof(*before.data)) == 0);
    CHECK(memcmp(instrument.current.data + last, before.data + last,
                 (before.frames - last) * sizeof(*before.data)) == 0);
    CHECK(memcmp(instrument.current.data + first, preview.sample.data,
                 preview.sample.frames * sizeof(*preview.sample.data)) == 0);
    CHECK(ts_instrument_undo(&instrument, error, sizeof(error)) &&
          ts_sample_hash(&instrument.current) == ts_sample_hash(&before) &&
          instrument.view_first == 700u && instrument.view_last == 6500u);
    CHECK(ts_instrument_redo(&instrument, error, sizeof(error)) &&
          memcmp(instrument.current.data + first, preview.sample.data,
                 preview.sample.frames * sizeof(*preview.sample.data)) == 0);
    ts_dsp_transform_preview_free(&preview);
    ts_sample_free(&rendered);
    ts_sample_free(&input);
    ts_sample_free(&before);
    ts_instrument_free(&instrument);
}

static void curated_dsp_direct_scope_and_tile_tests(void)
{
    char error[160];
    TsInstrument instrument;
    TsSample before;
    TsDspRecipeValues values;
    const TsDspRecipe *echo = ts_dsp_recipe_find("echo");
    const TsDspRecipe *drone = ts_dsp_recipe_find("drone");
    size_t first;
    size_t last;
    setup(&instrument, 8192u);
    first = instrument.selection_first;
    last = instrument.selection_last;
    ts_sample_init(&before);
    CHECK(ts_sample_clone(&before, &instrument.current, error, sizeof(error)));
    ts_dsp_recipe_values_default(echo, &values);
    CHECK(ts_dsp_transform_apply_direct_recipe(
        &instrument, echo, &values, TS_TRANSFORM_SELECTION,
        error, sizeof(error)));
    CHECK(instrument.undo_count == 1 &&
          ts_sample_hash(&instrument.current) != ts_sample_hash(&before) &&
          memcmp(instrument.current.data, before.data,
                 first * sizeof(*before.data)) == 0 &&
          memcmp(instrument.current.data + last, before.data + last,
                 (before.frames - last) * sizeof(*before.data)) == 0);
    CHECK(ts_instrument_undo(&instrument, error, sizeof(error)) &&
          ts_sample_hash(&instrument.current) == ts_sample_hash(&before));
    ts_instrument_clear_selection(&instrument);
    ts_dsp_recipe_values_default(drone, &values);
    values.controls[3] = 0.0f;
    CHECK(ts_dsp_transform_apply_direct_recipe(
        &instrument, drone, &values, TS_TRANSFORM_WHOLE,
        error, sizeof(error)));
    CHECK(instrument.undo_count == 1 && instrument.current.frames == before.frames &&
          ts_sample_hash(&instrument.current) != ts_sample_hash(&before) &&
          instrument.selection_first == 0u &&
          instrument.selection_last == instrument.current.frames);
    CHECK(ts_instrument_undo(&instrument, error, sizeof(error)) &&
          ts_sample_hash(&instrument.current) == ts_sample_hash(&before));
    CHECK(ts_instrument_copy_selected(&instrument, 1, error, sizeof(error)));
    {
        uint64_t other_hash = ts_sample_hash(&instrument.current);
        const TsDspRecipe *sine = ts_dsp_recipe_find("sine");
        CHECK(ts_instrument_select_bank(&instrument, 0, error, sizeof(error)));
        ts_dsp_recipe_values_default(sine, &values);
        values.controls[3] = 0.0f;
        ts_instrument_clear_selection(&instrument);
        CHECK(ts_dsp_transform_apply_direct_recipe(
            &instrument, sine, &values, TS_TRANSFORM_WHOLE,
            error, sizeof(error)));
        CHECK(ts_sample_hash(&instrument.bank[1].sample) == other_hash);
        CHECK(ts_instrument_select_bank(&instrument, 1, error, sizeof(error)) &&
              ts_sample_hash(&instrument.current) == other_hash);
    }
    ts_sample_free(&before);
    ts_instrument_free(&instrument);
}

static void curated_dsp_apply_keeps_native_shelf_live_tests(void)
{
    char error[160];
    TsInstrument instrument;
    TsDspRecipeValues values;
    TsProcessRecipe neutral;
    TsProcessRecipe process;
    const TsDspRecipe *drive = ts_dsp_recipe_find("drive");
    const TsDspRecipe *echo = ts_dsp_recipe_find("echo");
    uint64_t accepted_hash;

    /* Exercise the exact left-click DSP-bank Apply path. The accepted recipe
       must become stable editable material below BODY/EDGE/DRIFT and the
       NOISE/SHAPE/DELAY/SPACE shelf, never a patch that masks that shelf. */
    setup(&instrument, 8192u);
    ts_instrument_clear_selection(&instrument);
    ts_dsp_recipe_values_default(drive, &values);
    values.controls[0] = 0.78f;
    values.controls[3] = 0.86f;
    CHECK(ts_dsp_transform_apply_direct_recipe(
        &instrument, drive, &values, TS_TRANSFORM_WHOLE,
        error, sizeof(error)));
    accepted_hash = ts_sample_hash(&instrument.current);
    ts_process_recipe_reset(&neutral);
    neutral.seed = instrument.process.seed;
    CHECK(ts_process_recipe_equal(&instrument.process, &neutral));

    process = neutral;
    process.body = 0.0f;
    CHECK(ts_instrument_set_process(&instrument, &process, error, sizeof(error)) &&
          ts_sample_hash(&instrument.current) != accepted_hash);
    CHECK(ts_instrument_set_process(&instrument, &neutral, error, sizeof(error)) &&
          ts_sample_hash(&instrument.current) == accepted_hash);

    process = neutral;
    process.edge = 1.0f;
    CHECK(ts_instrument_set_process(&instrument, &process, error, sizeof(error)) &&
          ts_sample_hash(&instrument.current) != accepted_hash);
    CHECK(ts_instrument_set_process(&instrument, &neutral, error, sizeof(error)) &&
          ts_sample_hash(&instrument.current) == accepted_hash);

    process = neutral;
    process.drift = 1.0f;
    CHECK(ts_instrument_set_process(&instrument, &process, error, sizeof(error)) &&
          ts_sample_hash(&instrument.current) != accepted_hash);
    CHECK(ts_instrument_set_process(&instrument, &neutral, error, sizeof(error)) &&
          ts_sample_hash(&instrument.current) == accepted_hash);

    process = neutral;
    process.noise_enabled = 1;
    process.noise_amount = 1.0f;
    process.noise_color = TS_NOISE_METALLIC;
    CHECK(ts_instrument_set_process(&instrument, &process, error, sizeof(error)) &&
          ts_sample_hash(&instrument.current) != accepted_hash);
    CHECK(ts_instrument_set_process(&instrument, &neutral, error, sizeof(error)) &&
          ts_sample_hash(&instrument.current) == accepted_hash);

    process = neutral;
    process.shaper_enabled = 1;
    process.shaper_mode = TS_SHAPER_FOLD;
    process.shaper_drive = 14.0f;
    process.shaper_mix = 1.0f;
    CHECK(ts_instrument_set_process(&instrument, &process, error, sizeof(error)) &&
          ts_sample_hash(&instrument.current) != accepted_hash);
    CHECK(ts_instrument_set_process(&instrument, &neutral, error, sizeof(error)) &&
          ts_sample_hash(&instrument.current) == accepted_hash);

    process = neutral;
    process.delay_enabled = 1;
    process.delay_seconds = 0.005f;
    process.delay_feedback = 0.8f;
    process.delay_damping = 0.1f;
    process.delay_mix = 1.0f;
    CHECK(ts_instrument_set_process(&instrument, &process, error, sizeof(error)) &&
          ts_sample_hash(&instrument.current) != accepted_hash);
    CHECK(ts_instrument_set_process(&instrument, &neutral, error, sizeof(error)) &&
          ts_sample_hash(&instrument.current) == accepted_hash);

    process = neutral;
    process.reverb_enabled = 1;
    process.reverb_decay = 0.9f;
    process.reverb_damping = 0.1f;
    process.reverb_mix = 1.0f;
    CHECK(ts_instrument_set_process(&instrument, &process, error, sizeof(error)) &&
          ts_sample_hash(&instrument.current) != accepted_hash);
    CHECK(ts_instrument_set_process(&instrument, &neutral, error, sizeof(error)) &&
          ts_sample_hash(&instrument.current) == accepted_hash);
    ts_instrument_free(&instrument);

    /* The same guarantee applies when the bank recipe replaces a selection. */
    setup(&instrument, 8192u);
    ts_dsp_recipe_values_default(echo, &values);
    CHECK(ts_dsp_transform_apply_direct_recipe(
        &instrument, echo, &values, TS_TRANSFORM_SELECTION,
        error, sizeof(error)));
    accepted_hash = ts_sample_hash(&instrument.current);
    ts_process_recipe_reset(&neutral);
    neutral.seed = instrument.process.seed;
    process = neutral;
    process.shaper_enabled = 1;
    process.shaper_mode = TS_SHAPER_CLIP;
    process.shaper_drive = 12.0f;
    process.shaper_mix = 1.0f;
    CHECK(ts_instrument_set_process(&instrument, &process, error, sizeof(error)) &&
          ts_sample_hash(&instrument.current) != accepted_hash);
    CHECK(ts_instrument_set_process(&instrument, &neutral, error, sizeof(error)) &&
          ts_sample_hash(&instrument.current) == accepted_hash);
    ts_instrument_free(&instrument);
}

static void tape_gestures_keep_native_shelf_live_tests(void)
{
    char error[160];
    TsInstrument instrument;
    TsProcessRecipe neutral;
    TsProcessRecipe process;
    uint64_t accepted_hash;
    uint64_t prior_hash;

    setup(&instrument, 8192u);
    ts_instrument_clear_selection(&instrument);
    CHECK(ts_instrument_apply_warp(&instrument, 0.72f, error, sizeof(error)));
    CHECK(ts_instrument_apply_smear(&instrument, 0.64f, error, sizeof(error)));
    prior_hash = ts_sample_hash(&instrument.current);
    CHECK(ts_instrument_apply_tear(&instrument, 0.81f, error, sizeof(error)));
    accepted_hash = ts_sample_hash(&instrument.current);
    CHECK(accepted_hash != prior_hash);
    CHECK(instrument.post_edit_count == 1 &&
          instrument.post_edits[0].kind == TS_POST_MATERIAL_REPLACE);
    CHECK(ts_instrument_undo(&instrument, error, sizeof(error)) &&
          ts_sample_hash(&instrument.current) == prior_hash);
    CHECK(ts_instrument_redo(&instrument, error, sizeof(error)) &&
          ts_sample_hash(&instrument.current) == accepted_hash);
    ts_process_recipe_reset(&neutral);
    neutral.seed = instrument.process.seed;
    CHECK(ts_process_recipe_equal(&instrument.process, &neutral));

    process = neutral;
    process.body = 0.0f;
    CHECK(ts_instrument_set_process(&instrument, &process, error, sizeof(error)) &&
          ts_sample_hash(&instrument.current) != accepted_hash);
    CHECK(ts_instrument_set_process(&instrument, &neutral, error, sizeof(error)) &&
          ts_sample_hash(&instrument.current) == accepted_hash);

    process = neutral;
    process.edge = 1.0f;
    CHECK(ts_instrument_set_process(&instrument, &process, error, sizeof(error)) &&
          ts_sample_hash(&instrument.current) != accepted_hash);
    CHECK(ts_instrument_set_process(&instrument, &neutral, error, sizeof(error)) &&
          ts_sample_hash(&instrument.current) == accepted_hash);

    process = neutral;
    process.drift = 1.0f;
    CHECK(ts_instrument_set_process(&instrument, &process, error, sizeof(error)) &&
          ts_sample_hash(&instrument.current) != accepted_hash);
    CHECK(ts_instrument_set_process(&instrument, &neutral, error, sizeof(error)) &&
          ts_sample_hash(&instrument.current) == accepted_hash);

    ts_instrument_free(&instrument);
}

static void native_shelf_selection_scope_tests(void)
{
    char error[160];
    TsInstrument instrument;
    TsInstrument restored;
    TsSample accepted;
    TsProcessRecipe neutral;
    TsProcessRecipe process;
    uint64_t selected_hash;
    size_t first;
    size_t last;

    setup(&instrument, 8192u);
    ts_sample_init(&accepted);
    ts_instrument_init(&restored);
    first = instrument.selection_first;
    last = instrument.selection_last;
    CHECK(ts_instrument_apply_warp(&instrument, 0.72f, error, sizeof(error)));
    CHECK(ts_instrument_apply_smear(&instrument, 0.64f, error, sizeof(error)));
    CHECK(ts_instrument_apply_tear(&instrument, 0.81f, error, sizeof(error)));
    CHECK(instrument.has_selection && instrument.selection_first == first &&
          instrument.selection_last == last);
    CHECK(ts_sample_clone(&accepted, &instrument.current, error, sizeof(error)));
    ts_process_recipe_reset(&neutral);
    neutral.seed = instrument.process.seed;

    process = neutral;
    process.body = 0.0f;
    CHECK(ts_instrument_set_process(&instrument, &process, error, sizeof(error)));
    CHECK(instrument.has_process_range && instrument.process_first == first &&
          instrument.process_last == last);
    CHECK(memcmp(instrument.current.data, accepted.data,
                 first * sizeof(*accepted.data)) == 0);
    CHECK(memcmp(instrument.current.data + last, accepted.data + last,
                 (accepted.frames - last) * sizeof(*accepted.data)) == 0);
    CHECK(memcmp(instrument.current.data + first, accepted.data + first,
                 (last - first) * sizeof(*accepted.data)) != 0);
    selected_hash = ts_sample_hash(&instrument.current);
    CHECK(ts_instrument_undo(&instrument, error, sizeof(error)) &&
          ts_sample_hash(&instrument.current) == ts_sample_hash(&accepted));
    CHECK(ts_instrument_redo(&instrument, error, sizeof(error)) &&
          ts_sample_hash(&instrument.current) == selected_hash &&
          instrument.has_process_range && instrument.process_first == first &&
          instrument.process_last == last);
    CHECK(ts_instrument_set_process(&instrument, &neutral, error, sizeof(error)) &&
          ts_sample_hash(&instrument.current) == ts_sample_hash(&accepted));

    process = neutral;
    process.edge = 1.0f;
    CHECK(ts_instrument_set_process(&instrument, &process, error, sizeof(error)) &&
          memcmp(instrument.current.data, accepted.data,
                 first * sizeof(*accepted.data)) == 0 &&
          memcmp(instrument.current.data + last, accepted.data + last,
                 (accepted.frames - last) * sizeof(*accepted.data)) == 0 &&
          memcmp(instrument.current.data + first, accepted.data + first,
                 (last - first) * sizeof(*accepted.data)) != 0);
    CHECK(ts_instrument_set_process(&instrument, &neutral, error, sizeof(error)) &&
          ts_sample_hash(&instrument.current) == ts_sample_hash(&accepted));

    process = neutral;
    process.drift = 1.0f;
    CHECK(ts_instrument_set_process(&instrument, &process, error, sizeof(error)) &&
          memcmp(instrument.current.data, accepted.data,
                 first * sizeof(*accepted.data)) == 0 &&
          memcmp(instrument.current.data + last, accepted.data + last,
                 (accepted.frames - last) * sizeof(*accepted.data)) == 0 &&
          memcmp(instrument.current.data + first, accepted.data + first,
                 (last - first) * sizeof(*accepted.data)) != 0);
    selected_hash = ts_sample_hash(&instrument.current);

    CHECK(ts_instrument_save_recipe(&instrument, "test-process-scope.tsr",
                                    error, sizeof(error)));
    CHECK(ts_instrument_load_recipe(&restored, "test-process-scope.tsr",
                                    error, sizeof(error)));
    CHECK(restored.has_process_range && restored.process_first == first &&
          restored.process_last == last &&
          ts_sample_hash(&restored.current) == selected_hash);
    remove("test-process-scope.tsr");
    ts_sample_free(&accepted);
    ts_instrument_free(&restored);
    ts_instrument_free(&instrument);
}

static void post_patch_native_shelf_scope_tests(void)
{
    char error[160];
    TsInstrument instrument;
    TsSample clipboard;
    TsSample accepted;
    TsProcessRecipe neutral;
    TsProcessRecipe process;
    size_t origin = 0u;
    size_t first;
    size_t last;
    size_t original_first;
    size_t original_last;
    float pitch = 0.0f;

    /* Copying into a new tile used to place the paste patch after the native
       shelf, making every pasted frame immune to BODY/EDGE/DRIFT. */
    setup(&instrument, 8192u);
    ts_sample_init(&clipboard);
    ts_sample_init(&accepted);
    CHECK(ts_instrument_copy_selection(&instrument, &clipboard, &origin,
                                       error, sizeof(error)));
    instrument.selected_slot = 1;
    CHECK(ts_instrument_activate_silence(&instrument, 8192u, 48000u,
                                         error, sizeof(error)));
    CHECK(ts_instrument_paste(&instrument, &clipboard, origin, 0,
                              error, sizeof(error)));
    CHECK(instrument.has_selection);
    first = instrument.selection_first;
    last = instrument.selection_last;
    CHECK(first == origin && last - first == clipboard.frames);
    CHECK(ts_sample_clone(&accepted, &instrument.current,
                          error, sizeof(error)));
    ts_process_recipe_reset(&neutral);
    neutral.seed = instrument.process.seed;

    process = neutral;
    process.body = 0.0f;
    CHECK(ts_instrument_set_process(&instrument, &process, error, sizeof(error)) &&
          memcmp(instrument.current.data, accepted.data,
                 first * sizeof(*accepted.data)) == 0 &&
          memcmp(instrument.current.data + first, accepted.data + first,
                 (last - first) * sizeof(*accepted.data)) != 0 &&
          memcmp(instrument.current.data + last, accepted.data + last,
                 (accepted.frames - last) * sizeof(*accepted.data)) == 0);
    CHECK(ts_instrument_set_process(&instrument, &neutral,
                                    error, sizeof(error)) &&
          ts_sample_hash(&instrument.current) == ts_sample_hash(&accepted));

    process = neutral;
    process.edge = 1.0f;
    CHECK(ts_instrument_set_process(&instrument, &process, error, sizeof(error)) &&
          memcmp(instrument.current.data + first, accepted.data + first,
                 (last - first) * sizeof(*accepted.data)) != 0);
    CHECK(ts_instrument_set_process(&instrument, &neutral,
                                    error, sizeof(error)) &&
          ts_sample_hash(&instrument.current) == ts_sample_hash(&accepted));

    process = neutral;
    process.drift = 1.0f;
    CHECK(ts_instrument_set_process(&instrument, &process, error, sizeof(error)) &&
          memcmp(instrument.current.data + first, accepted.data + first,
                 (last - first) * sizeof(*accepted.data)) != 0);
    CHECK(ts_instrument_set_process(&instrument, &neutral,
                                    error, sizeof(error)) &&
          ts_sample_hash(&instrument.current) == ts_sample_hash(&accepted));

    /* No selection means the final current waveform, including the paste, is
       the processing domain. */
    ts_instrument_clear_selection(&instrument);
    process = neutral;
    process.body = 0.0f;
    CHECK(ts_instrument_set_process(&instrument, &process, error, sizeof(error)) &&
          !instrument.has_process_range &&
          memcmp(instrument.current.data + first, accepted.data + first,
                 (last - first) * sizeof(*accepted.data)) != 0);
    ts_sample_free(&accepted);

    /* Shift+Alt tape-length patches had the same ordering defect: the newly
       resampled core was overlaid after the shelf. Its complete selected result
       must now remain live to the shelf as one current-domain range. */
    CHECK(ts_instrument_select_bank(&instrument, 0, error, sizeof(error)));
    ts_instrument_set_selection(&instrument, 2048u, 4096u);
    original_first = instrument.selection_first;
    original_last = instrument.selection_last;
    CHECK(ts_instrument_stretch_selection(
        &instrument, (original_first + original_last) / 2u, 1.5f, &pitch,
        error, sizeof(error)));
    CHECK(instrument.has_selection && pitch < -0.01f);
    first = instrument.selection_first;
    last = instrument.selection_last;
    CHECK(first < original_first && last > original_last);
    CHECK(ts_sample_clone(&accepted, &instrument.current,
                          error, sizeof(error)));
    ts_process_recipe_reset(&neutral);
    neutral.seed = instrument.process.seed;
    process = neutral;
    process.body = 0.0f;
    CHECK(ts_instrument_set_process(&instrument, &process, error, sizeof(error)) &&
          instrument.has_process_range &&
          instrument.process_first == first && instrument.process_last == last &&
          memcmp(instrument.current.data + original_first,
                 accepted.data + original_first,
                 (original_last - original_first) *
                 sizeof(*accepted.data)) != 0 &&
          memcmp(instrument.current.data, accepted.data,
                 first * sizeof(*accepted.data)) == 0 &&
          memcmp(instrument.current.data + last, accepted.data + last,
                 (accepted.frames - last) * sizeof(*accepted.data)) == 0);

    ts_sample_free(&accepted);
    ts_sample_free(&clipboard);
    ts_instrument_free(&instrument);
}

static void destructive_material_macro_tests(void)
{
    char error[160];
    TsInstrument instrument;
    TsMaterialMacroGesture gesture;
    TsSample original;
    TsSample committed;
    TsSample clipboard;
    TsProcessRecipe live;
    size_t origin = 0u;
    size_t first;
    size_t last;
    uint64_t negative_edge_hash;
    uint64_t positive_edge_hash;
    uint64_t negative_drift_hash;
    uint64_t positive_drift_hash;
    int undo_before;

    setup(&instrument, 8192u);
    ts_material_macro_gesture_init(&gesture);
    ts_sample_init(&original);
    ts_sample_init(&committed);
    ts_sample_init(&clipboard);
    first = instrument.selection_first;
    last = instrument.selection_last;
    CHECK(ts_sample_clone(&original, &instrument.current,
                          error, sizeof(error)));
    undo_before = instrument.undo_count;
    CHECK(ts_instrument_material_macro_gesture_begin(
        &instrument, &gesture, TS_MATERIAL_MACRO_BODY,
        error, sizeof(error)));
    CHECK(ts_instrument_material_macro_gesture_preview(
        &instrument, &gesture, -1.0f, error, sizeof(error)));
    CHECK(memcmp(instrument.current.data, original.data,
                 first * sizeof(*original.data)) == 0);
    CHECK(memcmp(instrument.current.data + first, original.data + first,
                 (last - first) * sizeof(*original.data)) != 0);
    CHECK(memcmp(instrument.current.data + last, original.data + last,
                 (original.frames - last) * sizeof(*original.data)) == 0);
    CHECK(ts_instrument_material_macro_gesture_commit(
        &instrument, &gesture, error, sizeof(error)));
    CHECK(instrument.undo_count == undo_before + 1 &&
          fabsf(instrument.process.body - 0.5f) < 0.000001f &&
          instrument.process.edge == 0.0f && instrument.process.drift == 0.0f &&
          instrument.post_edit_count == 1 &&
          instrument.post_edits[0].kind == TS_POST_MATERIAL_REPLACE);
    CHECK(ts_sample_clone(&committed, &instrument.current,
                          error, sizeof(error)));
    CHECK(ts_instrument_undo(&instrument, error, sizeof(error)) &&
          ts_sample_hash(&instrument.current) == ts_sample_hash(&original));
    CHECK(ts_instrument_redo(&instrument, error, sizeof(error)) &&
          ts_sample_hash(&instrument.current) == ts_sample_hash(&committed));

    CHECK(ts_instrument_material_macro_gesture_begin(
        &instrument, &gesture, TS_MATERIAL_MACRO_EDGE,
        error, sizeof(error)));
    CHECK(ts_instrument_material_macro_gesture_preview(
        &instrument, &gesture, -1.0f, error, sizeof(error)));
    negative_edge_hash = ts_sample_hash(&instrument.current);
    CHECK(ts_sample_hash(&instrument.current) != ts_sample_hash(&committed));
    CHECK(ts_instrument_material_macro_gesture_preview(
        &instrument, &gesture, 1.0f, error, sizeof(error)));
    positive_edge_hash = ts_sample_hash(&instrument.current);
    CHECK(positive_edge_hash != negative_edge_hash &&
          positive_edge_hash != ts_sample_hash(&committed));
    CHECK(ts_instrument_material_macro_gesture_cancel(
        &instrument, &gesture, error, sizeof(error)) &&
          ts_sample_hash(&instrument.current) == ts_sample_hash(&committed));

    CHECK(ts_instrument_material_macro_gesture_begin(
        &instrument, &gesture, TS_MATERIAL_MACRO_DRIFT,
        error, sizeof(error)));
    CHECK(ts_instrument_material_macro_gesture_preview(
        &instrument, &gesture, -0.8f, error, sizeof(error)));
    negative_drift_hash = ts_sample_hash(&instrument.current);
    CHECK(negative_drift_hash != ts_sample_hash(&committed));
    CHECK(ts_instrument_material_macro_gesture_preview(
        &instrument, &gesture, 0.8f, error, sizeof(error)));
    positive_drift_hash = ts_sample_hash(&instrument.current);
    CHECK(positive_drift_hash != negative_drift_hash &&
          positive_drift_hash != ts_sample_hash(&committed));
    CHECK(ts_instrument_material_macro_gesture_cancel(
        &instrument, &gesture, error, sizeof(error)) &&
          ts_sample_hash(&instrument.current) == ts_sample_hash(&committed));

    /* The remaining shelf stays live while the material macro prints and
       resets. It is not silently baked or applied a second time. */
    live = instrument.process;
    live.noise_enabled = 1;
    live.noise_amount = 0.4f;
    CHECK(ts_instrument_set_process(&instrument, &live,
                                    error, sizeof(error)));
    CHECK(ts_instrument_apply_material_macro(
        &instrument, TS_MATERIAL_MACRO_DRIFT, 0.8f,
        error, sizeof(error)));
    CHECK(instrument.process.noise_enabled &&
          fabsf(instrument.process.noise_amount - 0.4f) < 0.000001f &&
          fabsf(instrument.process.body - 0.5f) < 0.000001f &&
          instrument.process.edge == 0.0f && instrument.process.drift == 0.0f);

    /* A pasted tile is ordinary material: selection-scoped EDGE reaches every
       pasted frame and Undo restores the exact pasted waveform. */
    CHECK(ts_instrument_select_bank(&instrument, 0, error, sizeof(error)));
    CHECK(ts_instrument_copy_selection(&instrument, &clipboard, &origin,
                                       error, sizeof(error)));
    CHECK(ts_instrument_select_bank(&instrument, 1, error, sizeof(error)));
    CHECK(ts_instrument_activate_silence(&instrument, 8192u, 48000u,
                                         error, sizeof(error)));
    CHECK(ts_instrument_paste(&instrument, &clipboard, origin, 0,
                              error, sizeof(error)));
    first = instrument.selection_first;
    last = instrument.selection_last;
    ts_sample_free(&original);
    CHECK(ts_sample_clone(&original, &instrument.current,
                          error, sizeof(error)));
    CHECK(ts_instrument_apply_material_macro(
        &instrument, TS_MATERIAL_MACRO_EDGE, 1.0f,
        error, sizeof(error)));
    CHECK(memcmp(instrument.current.data, original.data,
                 first * sizeof(*original.data)) == 0 &&
          memcmp(instrument.current.data + first, original.data + first,
                 (last - first) * sizeof(*original.data)) != 0 &&
          memcmp(instrument.current.data + last, original.data + last,
                 (original.frames - last) * sizeof(*original.data)) == 0);
    CHECK(ts_instrument_undo(&instrument, error, sizeof(error)) &&
          ts_sample_hash(&instrument.current) == ts_sample_hash(&original));

    ts_sample_free(&clipboard);
    ts_sample_free(&committed);
    ts_sample_free(&original);
    ts_instrument_free(&instrument);
}

static void transform_ui_contract_tests(void)
{
    TsInstrument instrument;
    TsUiState ui;
    TsFramebuffer framebuffer;
    size_t first;
    size_t last;
    size_t view_first;
    size_t view_last;
    setup(&instrument, 8192u);
    ts_ui_init(&ui);
    CHECK(ts_ui_panel(&ui) == TS_UI_PANEL_KEYBOARD && ui.dsp_page == 0);
    ts_ui_select_panel(&ui, TS_UI_PANEL_DSP);
    CHECK(ts_ui_panel(&ui) == TS_UI_PANEL_DSP && ui.dsp_page == 0);
    ts_ui_select_panel(&ui, TS_UI_PANEL_DSP);
    CHECK(ts_ui_panel(&ui) == TS_UI_PANEL_DSP && ui.dsp_page == 1);
    ts_ui_select_panel(&ui, TS_UI_PANEL_SAMPLE_TILES);
    ts_ui_select_panel(&ui, TS_UI_PANEL_DSP);
    CHECK(ts_ui_panel(&ui) == TS_UI_PANEL_DSP && ui.dsp_page == 1);
    ts_ui_select_panel(&ui, TS_UI_PANEL_DSP);
    CHECK(ui.dsp_page == 0 && ts_ui_cdp_page_from_point(15, 313) == 0 &&
          ts_ui_dsp_page_from_point(70, 313) == 1 &&
          ts_ui_dsp_page_from_point(120, 313) == -1);
    CHECK(ts_ui_space_plays_selection(&instrument));
    CHECK(!ts_ui_space_plays_selection(NULL));
    CHECK(ui.transform_recipe_index == -1);
    ui.transform_open = 1;
    ui.transform_scope = TS_TRANSFORM_SELECTION;
    first = instrument.selection_first;
    last = instrument.selection_last;
    view_first = instrument.view_first;
    view_last = instrument.view_last;
    ui.playback_active = 1;
    ui.playhead_sample = &instrument.current;
    ui.playhead_frame = view_first + (view_last - view_first) / 2u;
    ui.playhead_frames = instrument.current.frames;
    ts_ui_render(&framebuffer, &ui, &instrument);
    CHECK(instrument.selection_first == first && instrument.selection_last == last &&
          instrument.view_first == view_first && instrument.view_last == view_last);
    CHECK(framebuffer.pixels[(TS_TRANSFORM_WAVE_Y + 5) * TS_UI_WIDTH +
          TS_TRANSFORM_WAVE_X + TS_TRANSFORM_WAVE_W / 2] == 0xffff47e7u);
    CHECK(ts_ui_transform_waveform_contains(20, 62));
    CHECK(ts_ui_transform_control_from_point(20, 166) == 0 &&
          ts_ui_transform_control_from_point(470, 166) == 3);
    CHECK(ts_ui_transform_action_from_point(30, 230) ==
          TS_UI_TRANSFORM_ACTION_RENDER);
    CHECK(ts_ui_transform_action_from_point(130, 230) ==
          TS_UI_TRANSFORM_ACTION_APPLY);
    CHECK(ts_ui_transform_action_from_point(320, 230) ==
          TS_UI_TRANSFORM_ACTION_SAVE);
    CHECK(ts_ui_transform_action_from_point(430, 230) ==
          TS_UI_TRANSFORM_ACTION_BACK);
    ts_instrument_set_selection(&instrument, 4000u, instrument.current.frames);
    ts_ui_render(&framebuffer, &ui, &instrument);
    CHECK(instrument.selection_last == instrument.current.frames);
    ts_instrument_free(&instrument);
}

static void runtime_missing_test(void)
{
    char error[160];
    TsCdpRuntime runtime;
    ts_cdp_runtime_init(&runtime);
    CHECK(!ts_cdp_runtime_discover(&runtime, "/definitely/not/a/cdp/runtime", NULL,
                                   error, sizeof(error)));
    CHECK(strstr(error, "PVOC") != NULL || strstr(error, "runtime") != NULL ||
          strstr(error, "RUNTIME") != NULL);
}

#ifndef _WIN32
static int write_executable(const char *path, const char *body)
{
    FILE *file = fopen(path, "wb");
    if (file == NULL) return 0;
    if (fwrite(body, 1, strlen(body), file) != strlen(body) || fclose(file) != 0)
        return 0;
    return chmod(path, 0700) == 0;
}

static int write_mock_stereo_wav(const char *path)
{
    static const unsigned char wav[] = {
        'R','I','F','F', 40,0,0,0, 'W','A','V','E',
        'f','m','t',' ', 16,0,0,0, 1,0, 2,0,
        0x80,0xbb,0,0, 0,0xee,2,0, 4,0, 16,0,
        'd','a','t','a', 4,0,0,0, 0,0x10, 0,0xf0
    };
    FILE *file = fopen(path, "wb");
    int ok;
    if (file == NULL) return 0;
    ok = fwrite(wav, 1, sizeof(wav), file) == sizeof(wav);
    if (fclose(file) != 0) ok = 0;
    return ok;
}

static int cancel_immediately(void *userdata)
{
    (void)userdata;
    return 1;
}

static void adapter_pipeline_and_fault_tests(void)
{
    static const char *const required_executables[] = {
        "blur", "distmore", "distort", "distshift", "extend", "filter",
        "freeze", "glisten", "grain", "hover", "modify", "motor", "pvoc",
        "scramble", "sorter", "splinter", "stutter"
    };
    static const TsCdpFault faults[] = {
        TS_CDP_FAULT_LAUNCH,
        TS_CDP_FAULT_NONZERO_EXIT,
        TS_CDP_FAULT_TEXT_ERROR,
        TS_CDP_FAULT_TIMEOUT,
        TS_CDP_FAULT_CANCEL,
        TS_CDP_FAULT_MISSING_OUTPUT,
        TS_CDP_FAULT_EMPTY_OUTPUT,
        TS_CDP_FAULT_MALFORMED_WAV,
        TS_CDP_FAULT_NONFINITE_OUTPUT,
        TS_CDP_FAULT_UNSUPPORTED_CHANNELS,
        TS_CDP_FAULT_EXCESSIVE_LENGTH
    };
    const char *pvoc_body =
        "#!/bin/sh\n"
        "echo pvoc:$*\n"
        "echo pvoc-diagnostic >&2\n"
        "if [ \"$1\" = \"anal\" ]; then cp \"$3\" \"$4\"; exit $?; fi\n"
        "if [ \"$1\" = \"synth\" ]; then cp \"$2\" \"$3\"; exit $?; fi\n"
        "exit 7\n";
    const char *glisten_body =
        "#!/bin/sh\n"
        "echo glisten:$*\n"
        "echo glisten-diagnostic >&2\n"
        "cp \"$2\" \"$3\"\n";
    const char *pvoc_missing_intermediate_body =
        "#!/bin/sh\n"
        "if [ \"$1\" = \"anal\" ]; then exit 0; fi\n"
        "if [ \"$1\" = \"synth\" ]; then cp \"$2\" \"$3\"; fi\n";
    const char *pvoc_slow_body =
        "#!/bin/sh\n"
        "sleep 1\n"
        "if [ \"$1\" = \"anal\" ]; then cp \"$3\" \"$4\"; exit $?; fi\n"
        "if [ \"$1\" = \"synth\" ]; then cp \"$2\" \"$3\"; exit $?; fi\n";
    const char *generic_body =
        "#!/bin/sh\n"
        "input=\n"
        "output=\n"
        "for item in \"$@\"; do\n"
        "  case \"$item\" in\n"
        "    input.wav|input.ana) input=\"$item\" ;;\n"
        "    output.wav|effect.ana) output=\"$item\" ;;\n"
        "  esac\n"
        "done\n"
        "if [ -z \"$input\" ] || [ -z \"$output\" ]; then exit 8; fi\n"
        "if [ \"$1\" = \"brassage\" ] && [ \"$2\" = \"6\" ]; then\n"
        "  cp \"$(dirname \"$0\")/stereo.wav\" \"$output\"\n"
        "else\n"
        "  cp \"$input\" \"$output\"\n"
        "fi\n";
    char root_template[] = "/tmp/tapesister-transform-test-XXXXXX";
    char runtime_dir[1024];
    char jobs_dir[1024];
    char pvoc_path[2048];
    char glisten_path[2048];
    char stereo_path[2048];
    char error[160];
    char *root = mkdtemp(root_template);
    TsCdpRuntime runtime;
    TsCdpRunOptions options;
    TsCdpRunResult result;
    TsSample input;
    TsCdpRecipeValues values;
    const TsCdpRecipe *recipe = ts_cdp_recipe_find("glisten");
    CHECK(root != NULL);
    if (root == NULL) return;
    snprintf(runtime_dir, sizeof(runtime_dir), "%s/runtime", root);
    snprintf(jobs_dir, sizeof(jobs_dir), "%s/jobs", root);
    snprintf(pvoc_path, sizeof(pvoc_path), "%s/pvoc", runtime_dir);
    snprintf(glisten_path, sizeof(glisten_path), "%s/glisten", runtime_dir);
    snprintf(stereo_path, sizeof(stereo_path), "%s/stereo.wav", runtime_dir);
    CHECK(mkdir(runtime_dir, 0700) == 0);
    CHECK(mkdir(jobs_dir, 0700) == 0);
    CHECK(write_executable(pvoc_path, pvoc_body));
    CHECK(write_executable(glisten_path, glisten_body));
    ts_cdp_runtime_init(&runtime);
    CHECK(ts_cdp_runtime_discover(&runtime, runtime_dir, NULL,
                                  error, sizeof(error)));
    CHECK(ts_cdp_runtime_recipe_available(&runtime, recipe,
                                          error, sizeof(error)));
    CHECK(!ts_cdp_runtime_recipe_available(&runtime,
                                           ts_cdp_recipe_find("fractal"),
                                           error, sizeof(error)) &&
          strstr(error, "distort") != NULL);
    ts_sample_init(&input);
    input.frames = 8192u;
    input.sample_rate = 48000u;
    input.data = malloc(input.frames * sizeof(*input.data));
    CHECK(input.data != NULL);
    for (size_t i = 0; i < input.frames; ++i)
        input.data[i] = sinf((float)i * 0.01f) * 0.3f;
    snprintf(input.name, sizeof(input.name), "adapter source");
    ts_cdp_recipe_values_default(recipe, &values);
    ts_cdp_run_options_init(&options);
    options.job_id = 42u;
    snprintf(options.temporary_root, sizeof(options.temporary_root), "%s", jobs_dir);
    ts_cdp_run_result_init(&result);
    CHECK(ts_cdp_run_recipe(&runtime, recipe, &values, &input, &options,
                            &result, error, sizeof(error)));
    CHECK(result.status == TS_CDP_RUN_OK && result.output.frames == input.frames &&
          result.output.sample_rate == input.sample_rate &&
          result.output.data != input.data && result.job_directory[0] == '\0');
    CHECK(strstr(result.diagnostic, "pvoc") != NULL &&
          strstr(result.diagnostic, "diagnostic") != NULL);
    ts_cdp_run_result_free(&result);

    CHECK(write_mock_stereo_wav(stereo_path));
    for (size_t executable = 0;
         executable < sizeof(required_executables) / sizeof(required_executables[0]);
         ++executable) {
        char path[2048];
        const char *name = required_executables[executable];
        if (strcmp(name, "pvoc") == 0 || strcmp(name, "glisten") == 0) continue;
        snprintf(path, sizeof(path), "%s/%s", runtime_dir, name);
        CHECK(write_executable(path, generic_body));
    }
    for (size_t index = 0; index < ts_cdp_factory_recipe_count(); ++index) {
        const TsCdpRecipe *catalog = ts_cdp_factory_recipe_at(index);
        ts_cdp_recipe_values_default(catalog, &values);
        ts_cdp_run_options_init(&options);
        options.job_id = 500u + index;
        snprintf(options.temporary_root, sizeof(options.temporary_root), "%s", jobs_dir);
        ts_cdp_run_result_init(&result);
        CHECK(ts_cdp_run_recipe(&runtime, catalog, &values, &input, &options,
                                &result, error, sizeof(error)));
        CHECK(result.status == TS_CDP_RUN_OK && result.output.data != NULL &&
              result.output.frames > 0u && result.output.sample_rate == 48000u &&
              result.job_directory[0] == '\0');
        ts_cdp_run_result_free(&result);
    }

    CHECK(write_executable(pvoc_path, pvoc_missing_intermediate_body));
    ts_cdp_run_options_init(&options);
    options.job_id = 43u;
    snprintf(options.temporary_root, sizeof(options.temporary_root), "%s", jobs_dir);
    ts_cdp_run_result_init(&result);
    CHECK(!ts_cdp_run_recipe(&runtime, recipe, &values, &input, &options,
                             &result, error, sizeof(error)));
    CHECK(result.status == TS_CDP_RUN_FAILED && result.output.data == NULL);
    ts_cdp_run_result_free(&result);

    CHECK(write_executable(pvoc_path, pvoc_slow_body));
    ts_cdp_run_options_init(&options);
    options.job_id = 44u;
    options.timeout_ms = 20u;
    snprintf(options.temporary_root, sizeof(options.temporary_root), "%s", jobs_dir);
    ts_cdp_run_result_init(&result);
    CHECK(!ts_cdp_run_recipe(&runtime, recipe, &values, &input, &options,
                             &result, error, sizeof(error)));
    CHECK(result.status == TS_CDP_RUN_TIMEOUT && result.output.data == NULL);
    ts_cdp_run_result_free(&result);

    ts_cdp_run_options_init(&options);
    options.job_id = 45u;
    options.cancel_check = cancel_immediately;
    snprintf(options.temporary_root, sizeof(options.temporary_root), "%s", jobs_dir);
    ts_cdp_run_result_init(&result);
    CHECK(!ts_cdp_run_recipe(&runtime, recipe, &values, &input, &options,
                             &result, error, sizeof(error)));
    CHECK(result.status == TS_CDP_RUN_CANCELLED && result.output.data == NULL);
    ts_cdp_run_result_free(&result);
    CHECK(write_executable(pvoc_path, pvoc_body));

    for (size_t i = 0; i < sizeof(faults) / sizeof(faults[0]); ++i) {
        ts_cdp_run_options_init(&options);
        options.job_id = 100u + i;
        options.fault = faults[i];
        snprintf(options.temporary_root, sizeof(options.temporary_root), "%s", jobs_dir);
        ts_cdp_run_result_init(&result);
        CHECK(!ts_cdp_run_recipe(&runtime, recipe, &values, &input, &options,
                                 &result, error, sizeof(error)));
        CHECK(result.status == TS_CDP_RUN_FAILED ||
              result.status == TS_CDP_RUN_TIMEOUT ||
              result.status == TS_CDP_RUN_CANCELLED);
        CHECK(result.output.data == NULL);
        ts_cdp_run_result_free(&result);
    }
    ts_cdp_run_options_init(&options);
    options.job_id = 999u;
    options.fault = TS_CDP_FAULT_CLEANUP;
    snprintf(options.temporary_root, sizeof(options.temporary_root), "%s", jobs_dir);
    ts_cdp_run_result_init(&result);
    CHECK(ts_cdp_run_recipe(&runtime, recipe, &values, &input, &options,
                            &result, error, sizeof(error)));
    CHECK(result.cleanup_failed && result.job_directory[0] != '\0');
    CHECK(ts_cdp_cleanup_job_directory(result.job_directory, error, sizeof(error)));
    ts_cdp_run_result_free(&result);
    ts_sample_free(&input);
    for (size_t executable = 0;
         executable < sizeof(required_executables) / sizeof(required_executables[0]);
         ++executable) {
        char path[2048];
        snprintf(path, sizeof(path), "%s/%s", runtime_dir,
                 required_executables[executable]);
        CHECK(unlink(path) == 0);
    }
    CHECK(unlink(stereo_path) == 0);
    CHECK(rmdir(runtime_dir) == 0);
    CHECK(rmdir(jobs_dir) == 0);
    CHECK(rmdir(root) == 0);
}
#endif

int main(void)
{
    recipe_tests();
    mix_tests();
    identity_and_apply_tests();
    tile_isolation_and_whole_tests();
    replacement_lengths_and_selection_persistence_tests();
    rendered_replacement_native_process_regression_tests();
    native_dsp_recipe_and_preview_tests();
    native_dsp_direct_and_body_range_tests();
    curated_dsp_bank_and_render_tests();
    curated_dsp_preview_apply_tests();
    curated_dsp_direct_scope_and_tile_tests();
    curated_dsp_apply_keeps_native_shelf_live_tests();
    tape_gestures_keep_native_shelf_live_tests();
    native_shelf_selection_scope_tests();
    post_patch_native_shelf_scope_tests();
    destructive_material_macro_tests();
    transform_ui_contract_tests();
    runtime_missing_test();
#ifndef _WIN32
    adapter_pipeline_and_fault_tests();
#endif
    if (failures != 0) {
        fprintf(stderr, "%d transform test(s) failed\n", failures);
        return 1;
    }
    puts("transform tests passed");
    return 0;
}
