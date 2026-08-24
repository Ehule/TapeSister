#include "sister_test_helpers.h"
#include "tapesister/sister_project_state.h"

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
    runtime.parameters.soak = 0.68f;
    runtime.parameters.bleed = 0.74f;
    runtime.parameters.soak_targets = TS_SISTER_EFFECT_TARGET_H1 |
                                      TS_SISTER_EFFECT_TARGET_H2;
    ts_sister_runtime_set_parameters(&runtime, &runtime.parameters);
    runtime.machine.buffer.data[0] = 0.75f;
    ts_sister_project_state_capture(&state, &runtime, 2u, "GHOST FIELD");
    assert(ts_sister_project_state_save(&state, project, error, sizeof(error)));
    assert(ts_sister_project_state_load(&loaded, project, 1000u, &present,
                                        error, sizeof(error)) && present);
    assert(loaded.page_masks[0] == 1u && loaded.page_masks[1] == 2u);
    assert(loaded.parameters.ghost_tone > 0.60f);
    assert(loaded.parameters.soak > 0.67f && loaded.parameters.soak < 0.69f);
    assert(loaded.parameters.bleed > 0.73f && loaded.parameters.bleed < 0.75f);
    assert(loaded.parameters.soak_targets ==
           (TS_SISTER_EFFECT_TARGET_H1 | TS_SISTER_EFFECT_TARGET_H2));
    assert(strcmp(loaded.selected_preset, "GHOST FIELD") == 0);
    ts_sister_runtime_init(&restored);
    assert(ts_sister_project_state_apply(&loaded, &restored, &instrument));
    assert(restored.source_switches == loaded.source_switches);
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
        assert(loaded.parameters.soak == 0.0f &&
               loaded.parameters.bleed == 0.25f &&
               loaded.parameters.soak_targets == TS_SISTER_EFFECT_TARGET_MIX);
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
           loaded.parameters.soak_targets == TS_SISTER_EFFECT_TARGET_MIX);
    puts("sister project-state tests passed");
    return 0;
}
