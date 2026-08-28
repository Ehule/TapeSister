#include "tapesister/config.h"
#include "tapesister/palette.h"
#include "tapesister/sister_ui.h"
#include "tapesister/ui.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    TsConfig config;
    TsPalette palette;
    TsSisterUiModel model;
    static TsFramebuffer framebuffer;
    const char *mode = argc > 1 ? argv[1] : "off";
    const char *path = argc > 2 ? argv[2] : "sister-spirit.ppm";
    ts_config_init(&config);
    ts_palette_default(&palette);
    ts_sister_ui_model_init(&model, &config);
    ts_sister_ui_model_show(&model);
    model.magnetic_phase = 3u;
    if (strcmp(mode, "flash") == 0) {
        model.routing.enabled = 1;
        model.power_visual = TS_SISTER_UI_POWER_VISUAL_ON;
        model.power_visual_elapsed_ms = 350u;
        snprintf(model.status, sizeof(model.status),
                 "ENABLED - SELECT SOURCES AND MONITOR WHEN READY");
    }
    ts_sister_ui_render(&framebuffer, &model, &palette);
    if (!ts_ui_write_ppm(&framebuffer, path)) {
        fprintf(stderr, "Could not write %s\n", path);
        return 1;
    }
    return 0;
}
