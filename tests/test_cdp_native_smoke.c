#include "tapesister/cdp_adapter.h"
#include "tapesister/cdp_recipe.h"
#include "tapesister/sample.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    TsCdpRuntime runtime;
    TsSample input;
    char error[256];
    int failures = 0;
    if (argc != 3) {
        fprintf(stderr, "usage: %s CDP_BIN_DIRECTORY INPUT_WAV\n", argv[0]);
        return 2;
    }
    ts_cdp_runtime_init(&runtime);
    if (!ts_cdp_runtime_discover(&runtime, argv[1], NULL,
                                 error, sizeof(error))) {
        fprintf(stderr, "runtime discovery failed: %s\n", error);
        return 2;
    }
    ts_sample_init(&input);
    if (!ts_sample_load_wav(&input, argv[2], error, sizeof(error))) {
        fprintf(stderr, "input load failed: %s\n", error);
        return 2;
    }
    for (size_t index = 0; index < ts_cdp_factory_recipe_count(); ++index) {
        const TsCdpRecipe *recipe = ts_cdp_factory_recipe_at(index);
        TsCdpRecipeValues values;
        TsCdpRunOptions options;
        TsCdpRunResult result;
        ts_cdp_recipe_values_default(recipe, &values);
        /* SPLINTER's target-frequency constraint is necessarily content
           dependent.  The welcome fixture's 50% point is above its curated
           6 kHz goal, while the 10% point exercises the same native process. */
        if (recipe != NULL && strcmp(recipe->id, "splinter") == 0)
            values.controls[0] = 0.1f;
        ts_cdp_run_options_init(&options);
        options.job_id = 1000u + index;
        options.timeout_ms = 30000u;
        ts_cdp_run_result_init(&result);
        if (!ts_cdp_run_recipe(&runtime, recipe, &values, &input, &options,
                               &result, error, sizeof(error))) {
            fprintf(stderr, "FAIL page %zu tile %02zu %-16s %s\n",
                    index / TS_CDP_BANK_SLOT_COUNT + 1u,
                    index % TS_CDP_BANK_SLOT_COUNT + 1u,
                    recipe->display_name, error);
            if (result.diagnostic[0] != '\0')
                fprintf(stderr, "     %s\n", result.diagnostic);
            ++failures;
        } else {
            printf("PASS page %zu tile %02zu %-16s %zu frames %u channel%s\n",
                   index / TS_CDP_BANK_SLOT_COUNT + 1u,
                   index % TS_CDP_BANK_SLOT_COUNT + 1u,
                   recipe->display_name, result.output.frames,
                   result.output.channels,
                   result.output.channels == 1u ? "" : "s");
        }
        ts_cdp_run_result_free(&result);
    }
    ts_sample_free(&input);
    if (failures != 0) {
        fprintf(stderr, "%d native CDP recipe(s) failed\n", failures);
        return 1;
    }
    puts("all native CDP recipes passed");
    return 0;
}
