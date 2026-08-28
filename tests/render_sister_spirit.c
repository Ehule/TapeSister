#include "tapesister/config.h"
#include "tapesister/palette.h"
#include "tapesister/sister_ui.h"
#include "tapesister/ui.h"

#include <math.h>
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
    if (strcmp(mode, "fallout") == 0) {
        model.routing.enabled = 1;
        model.routing.rolling = 1;
        model.fx_page = 2;
        model.parameters.fx.fallout.enabled = 1;
        model.parameters.fx.fallout.mix = 0.72f;
        model.parameters.fx.fallout.feedback = 0.28f;
        model.parameters.fx.fallout.noise = 0.18f;
        model.parameters.fx.fallout.drop_enabled = 1;
        model.parameters.fx.fallout.pan_enabled = 1;
        model.parameters.fx.fallout.skip_enabled = 1;
        model.parameters.fx.fallout.bit_enabled = 1;
        model.parameters.fx.fallout.pitch_enabled = 1;
        snprintf(model.status, sizeof(model.status),
                 "FALLOUT INSERT ENGAGED");
    } else if (strcmp(mode, "flash") == 0) {
        model.routing.enabled = 1;
        model.power_visual = TS_SISTER_UI_POWER_VISUAL_ON;
        model.power_visual_elapsed_ms = 350u;
        snprintf(model.status, sizeof(model.status),
                 "ENABLED - SELECT SOURCES AND MONITOR WHEN READY");
    } else if (strcmp(mode, "capture") == 0) {
        model.routing.enabled = 1;
        model.routing.rolling = 1;
        model.routing.monitor_enabled = 1;
        model.routing.capture_state = TS_CAPTURE_RECORDING;
        model.routing.capture_recorded_frames = 7u * 48000u;
        model.routing.capture_capacity_frames = 20u * 48000u;
        model.routing.source_switches = TS_SISTER_SOURCE_TILES |
                                        TS_SISTER_SOURCE_FM;
        model.routing.source_mask = 0x0005u;
        model.waveform.channels = 2u;
        model.waveform.valid_bins = TS_SISTER_WAVE_BIN_COUNT;
        for (size_t bin = 0u; bin < TS_SISTER_WAVE_BIN_COUNT; ++bin) {
            float phase = (float)bin * 0.087f;
            float left = 0.12f + 0.34f * fabsf(sinf(phase));
            float right = 0.10f + 0.28f * fabsf(cosf(phase * 0.83f));
            model.waveform.bins[bin].left_minimum = -left;
            model.waveform.bins[bin].left_maximum = left;
            model.waveform.bins[bin].right_minimum = -right;
            model.waveform.bins[bin].right_maximum = right;
        }
        model.engine.write_normalized = 0.72;
        model.engine.head_normalized[0] = 0.15;
        model.engine.head_normalized[1] = 0.42;
        model.engine.head_normalized[2] = 0.81;
        model.text_cursor_visible = 1;
        snprintf(model.status, sizeof(model.status),
                 "CAPTURE RECORDING - PRESS AGAIN TO STOP");
    }
    ts_sister_ui_render(&framebuffer, &model, &palette);
    if (!ts_ui_write_ppm(&framebuffer, path)) {
        fprintf(stderr, "Could not write %s\n", path);
        return 1;
    }
    return 0;
}
