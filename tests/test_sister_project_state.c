#include "sister_test_helpers.h"
#include "tapesister/sister_project_state.h"
#include "tapesister/sister_ui.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define MAKE_DIR(path) _mkdir(path)
#define REMOVE_DIR(path) _rmdir(path)
#else
#include <sys/stat.h>
#include <unistd.h>
#define MAKE_DIR(path) mkdir(path, 0700)
#define REMOVE_DIR(path) rmdir(path)
#endif

int main(void)
{
    static const char project[] = "test-sister-project.tsr";
    static const char directory[] = "test-sister-project.tsr.samples";
    static const char state_path[] = "test-sister-project.tsr.samples/sister-state.ini";
    TsSisterRuntime runtime, restored;
    TsInstrument instrument;
    TsSisterProjectState state, loaded;
    char error[160];
    int present;
    (void)MAKE_DIR(directory);
    assert(sister_test_make_tiles(&instrument, 2, 0, 1000u, 32u));
    assert(sister_test_enable(&runtime, 1000u, 2u, 0.1));
    ts_sister_runtime_set_sources(&runtime,
        TS_SISTER_SOURCE_TILES | TS_SISTER_SOURCE_FM | TS_SISTER_SOURCE_EXT);
    assert(ts_sister_runtime_set_source_slot(&runtime, &instrument, 0, 1));
    assert(ts_sister_runtime_set_page(&runtime, 1u, &instrument));
    assert(ts_sister_runtime_set_source_slot(&runtime, &instrument, 1, 1));
    runtime.parameters.ghost_tone = 0.61f;
    runtime.parameters.tiles_gain = 1.23f;
    runtime.parameters.fm_gain = 2.34f;
    runtime.parameters.external_gain = 3.45f;
    runtime.parameters.preview_gain = 0.67f;
    runtime.parameters.fx_return_gain = 1.54f;
    runtime.parameters.soak = 0.68f;
    runtime.parameters.bleed = 0.74f;
    runtime.parameters.soak_targets = TS_SISTER_EFFECT_TARGET_H1 |
                                      TS_SISTER_EFFECT_TARGET_H2;
    runtime.parameters.fx.reverb_type = TS_SISTER_REVERB_SPRING;
    runtime.parameters.fx.reverb_mix = 0.57f;
    runtime.parameters.fx.reverb_targets = TS_SISTER_EFFECT_TARGET_H3;
    runtime.parameters.fx.delay_mix = 0.43f;
    runtime.parameters.fx.delay_targets = TS_SISTER_EFFECT_TARGET_H1 |
                                           TS_SISTER_EFFECT_TARGET_H2;
    runtime.parameters.fx.distortion_mix = 0.71f;
    runtime.parameters.fx.distortion_targets = TS_SISTER_EFFECT_TARGET_MIX;
    runtime.parameters.fx.master_feedback = 0.66f;
    runtime.parameters.buffer_seconds = 23.0f;
    runtime.parameter_locks =
        TS_SISTER_UI_PARAMETER_BIT(TS_SISTER_UI_PARAM_FILTER_TYPE) |
        TS_SISTER_UI_PARAMETER_BIT(TS_SISTER_UI_PARAM_EXT_GAIN);
    ts_sister_runtime_set_parameters(&runtime, &runtime.parameters);
    runtime.machine.buffer.data[0] = 0.75f;
    ts_sister_project_state_capture(&state, &runtime, 2u, "GHOST FIELD");
    assert(ts_sister_project_state_save(&state, project, error, sizeof(error)));
    assert(ts_sister_project_state_load(&loaded, project, 1000u, &present,
                                        error, sizeof(error)) && present);
    assert(loaded.page_masks[0] == 1u && loaded.page_masks[1] == 2u);
    assert(loaded.parameters.ghost_tone > 0.60f);
    assert(loaded.parameters.tiles_gain > 1.22f && loaded.parameters.tiles_gain < 1.24f);
    assert(loaded.parameters.fm_gain > 2.33f && loaded.parameters.fm_gain < 2.35f);
    assert(loaded.parameters.external_gain > 3.44f && loaded.parameters.external_gain < 3.46f);
    assert(loaded.parameters.preview_gain > 0.66f && loaded.parameters.preview_gain < 0.68f);
    assert(loaded.parameters.fx_return_gain > 1.53f && loaded.parameters.fx_return_gain < 1.55f);
    assert(loaded.parameters.soak > 0.67f && loaded.parameters.soak < 0.69f);
    assert(loaded.parameters.bleed > 0.73f && loaded.parameters.bleed < 0.75f);
    assert(loaded.parameters.soak_targets ==
           (TS_SISTER_EFFECT_TARGET_H1 | TS_SISTER_EFFECT_TARGET_H2));
    assert(loaded.parameters.fx.reverb_type == TS_SISTER_REVERB_SPRING);
    assert(loaded.parameters.fx.reverb_targets == TS_SISTER_EFFECT_TARGET_H3);
    assert(loaded.parameters.fx.delay_targets ==
           (TS_SISTER_EFFECT_TARGET_H1 | TS_SISTER_EFFECT_TARGET_H2));
    assert(loaded.parameters.fx.distortion_targets ==
           TS_SISTER_EFFECT_TARGET_MIX);
    assert(loaded.parameters.fx.master_feedback > 0.65f);
    assert(loaded.parameters.buffer_seconds == 23.0f);
    assert(loaded.parameter_locks == runtime.parameter_locks);
    assert(strcmp(loaded.selected_preset, "GHOST FIELD") == 0);
    ts_sister_runtime_init(&restored);
    assert(ts_sister_project_state_apply(&loaded, &restored, &instrument));
    assert(restored.source_switches == loaded.source_switches);
    assert(restored.parameter_locks == loaded.parameter_locks);
    assert(restored.active_page == 1u && restored.page_source_masks[0] == 1u);
    assert(restored.machine.buffer.data == NULL); /* live tape was not serialized */
    ts_sister_runtime_free(&restored);
    ts_sister_runtime_free(&runtime);
    ts_instrument_free(&instrument);
    remove(state_path);

    {
        FILE *file = fopen(state_path, "wb");
        assert(file != NULL);
        fputs("TapeSister Sister Project State\nVersion=9\nPageCount=1\n"
              "ActivePage=0\nRoutes=2\nSelectedPreset=FUTURE\n"
              "Mask.0=0000\nGhostTone=0.25\nSoakTargets=255\n"
              "FutureField=17\n", file);
        fclose(file);
        assert(ts_sister_project_state_load(&loaded, project, 48000u,
                                            &present, error, sizeof(error)));
        assert(present && loaded.source_switches == TS_SISTER_SOURCE_FM &&
               loaded.parameters.ghost_tone == 0.25f);
        assert(loaded.parameter_locks == 0u);
        assert(loaded.parameters.soak == 0.0f &&
               loaded.parameters.bleed == 0.25f &&
               loaded.parameters.soak_targets == TS_SISTER_EFFECT_TARGET_MIX);
        assert(loaded.parameters.fx.reverb_mix == 0.0f &&
               loaded.parameters.fx.delay_mix == 0.0f &&
               loaded.parameters.fx.distortion_mix == 0.0f &&
               loaded.parameters.fx.master_feedback == 0.0f &&
               loaded.parameters.tiles_gain == 1.0f &&
               loaded.parameters.fx_return_gain == 1.0f &&
               loaded.parameters.fx.reverb_targets == TS_SISTER_EFFECT_TARGET_MIX);
        assert(loaded.parameters.buffer_seconds == 40.0f);
    }
    remove(state_path);
    {
        FILE *file = fopen(state_path, "wb");
        assert(file != NULL);
        fputs("TapeSister Sister Project State\nVersion=1\nPageCount=0\n", file);
        fclose(file);
        assert(!ts_sister_project_state_load(&loaded, project, 48000u,
                                             &present, error, sizeof(error)));
    }
    remove(state_path);
    REMOVE_DIR(directory);

    ts_sister_project_state_init(&loaded, 48000u);
    assert(ts_sister_project_state_load(&loaded, "missing-project.tsr", 48000u,
                                        &present, error, sizeof(error)));
    assert(!present && loaded.source_switches == 0u &&
           loaded.parameters.soak == 0.0f &&
           loaded.parameters.bleed == 0.25f &&
           loaded.parameters.soak_targets == TS_SISTER_EFFECT_TARGET_MIX &&
           loaded.parameters.fx.reverb_mix == 0.0f &&
           loaded.parameters.fx.delay_mix == 0.0f &&
           loaded.parameters.fx.distortion_mix == 0.0f &&
           loaded.parameters.fx.master_feedback == 0.0f &&
           loaded.parameters.tiles_gain == 1.0f &&
           loaded.parameters.fm_gain == 1.0f &&
           loaded.parameters.external_gain == 1.0f &&
           loaded.parameters.preview_gain == 1.0f &&
           loaded.parameters.fx_return_gain == 1.0f &&
           loaded.parameters.buffer_seconds == 40.0f);
    puts("sister project-state tests passed");
    return 0;
}
