#include "tapesister/sample.h"
#include "tapesister/ui.h"

#include <stdio.h>

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "tapesister-first-slice.ppm";
    TsSample sample;
    TsUiState ui;
    TsFramebuffer fb;
    char error[160];
    ts_sample_init(&sample);
    ts_ui_init(&ui);
    if (!ts_sample_generate(&sample, &ui.recipe, error, sizeof(error))) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }
    snprintf(ui.status, sizeof(ui.status), "GENERATED %08X - READY TO PLAY", ui.recipe.seed);
    ts_ui_render(&fb, &ui, &sample);
    if (!ts_ui_write_ppm(&fb, path)) {
        fprintf(stderr, "Could not write %s\n", path);
        ts_sample_free(&sample);
        return 1;
    }
    ts_sample_free(&sample);
    printf("Wrote %s\n", path);
    return 0;
}
