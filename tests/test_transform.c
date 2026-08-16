#define _POSIX_C_SOURCE 200809L

#include "tapesister/cdp_adapter.h"
#include "tapesister/cdp_recipe.h"
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
    TsCdpCommand commands[3];
    const TsCdpRecipe *recipe = ts_cdp_recipe_find("glisten");
    CHECK(ts_cdp_factory_recipe_count() == 1u);
    CHECK(recipe != NULL && recipe == ts_cdp_factory_recipe_at(0));
    CHECK(ts_cdp_recipe_find("missing") == NULL);
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
          strcmp(commands[1].arguments[2], "glisten.ana") == 0 &&
          strcmp(commands[1].arguments[3], "8") == 0 &&
          strcmp(commands[1].arguments[4], "8") == 0 &&
          strcmp(commands[1].arguments[5], "-p3") == 0 &&
          strcmp(commands[1].arguments[6], "-d0.28") == 0 &&
          strcmp(commands[1].arguments[7], "-v0.0784") == 0);
    CHECK(strcmp(commands[2].arguments[0], "synth") == 0 &&
          strcmp(commands[2].arguments[1], "glisten.ana") == 0 &&
          strcmp(commands[2].arguments[2], "output.wav") == 0);
    CHECK(!recipe->seed_supported && !recipe->deterministic);
    CHECK(recipe->duration_may_change);
    CHECK(recipe->mix_policy == TS_CDP_MIX_UNSUPPORTED);
    CHECK(values.mix == 1.0f);
    CHECK(recipe->required_input_channels == 1u &&
          recipe->expected_output_channels == 1u && recipe->preserve_sample_rate);
    CHECK(recipe->safety_policy == TS_CDP_SAFETY_ANALYZE_ONLY &&
          recipe->provenance_version == 1u);
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
    }
    CHECK(!ts_cdp_recipe_input_valid(recipe, 1000u, 48000u, error, sizeof(error)));
    CHECK(ts_cdp_recipe_input_valid(recipe, 5000u, 48000u, error, sizeof(error)));
    ts_cdp_control_format(&recipe->controls[1], 8.0f, 48000u, 1024u, 3u,
                          formatted, sizeof(formatted));
    CHECK(strstr(formatted, "43MS") != NULL);
}

static void mix_tests(void)
{
    char error[160];
    float dry_data[] = {-0.5f, 0.0f, 0.5f};
    float wet_data[] = {0.5f, 0.5f, -0.5f};
    TsSample dry = {dry_data, 3u, 48000u, "dry"};
    TsSample wet = {wet_data, 3u, 48000u, "wet"};
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
          restored.view_last == instrument.view_last);
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
    ui.transform_open = 1;
    ui.transform_scope = TS_TRANSFORM_SELECTION;
    first = instrument.selection_first;
    last = instrument.selection_last;
    view_first = instrument.view_first;
    view_last = instrument.view_last;
    ts_ui_render(&framebuffer, &ui, &instrument);
    CHECK(instrument.selection_first == first && instrument.selection_last == last &&
          instrument.view_first == view_first && instrument.view_last == view_last);
    CHECK(ts_ui_transform_waveform_contains(20, 62));
    CHECK(ts_ui_transform_control_from_point(20, 166) == 0 &&
          ts_ui_transform_control_from_point(470, 166) == 3);
    CHECK(ts_ui_transform_action_from_point(30, 230) ==
          TS_UI_TRANSFORM_ACTION_RENDER);
    CHECK(ts_ui_transform_action_from_point(130, 230) ==
          TS_UI_TRANSFORM_ACTION_APPLY);
    CHECK(ts_ui_transform_action_from_point(320, 230) ==
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

static int cancel_immediately(void *userdata)
{
    (void)userdata;
    return 1;
}

static void adapter_pipeline_and_fault_tests(void)
{
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
    char root_template[] = "/tmp/tapesister-transform-test-XXXXXX";
    char runtime_dir[1024];
    char jobs_dir[1024];
    char pvoc_path[2048];
    char glisten_path[2048];
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
    CHECK(mkdir(runtime_dir, 0700) == 0);
    CHECK(mkdir(jobs_dir, 0700) == 0);
    CHECK(write_executable(pvoc_path, pvoc_body));
    CHECK(write_executable(glisten_path, glisten_body));
    ts_cdp_runtime_init(&runtime);
    CHECK(ts_cdp_runtime_discover(&runtime, runtime_dir, NULL,
                                  error, sizeof(error)));
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
    CHECK(unlink(pvoc_path) == 0);
    CHECK(unlink(glisten_path) == 0);
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
