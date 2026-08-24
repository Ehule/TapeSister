#include "tapesister/sister_ui.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); ++failures; } } while (0)

int main(void)
{
    TsConfig config;
    TsSisterUiModel model;
    TsSisterRoutingSnapshot routing = {0};
    TsSisterUiHit hit;
    static TsFramebuffer framebuffer;
    TsPalette palette;
    uint64_t first_hash = 1469598103934665603ull;
    uint64_t second_hash = 1469598103934665603ull;
    ts_config_init(&config);
    CHECK(config.sister_buffer_seconds == 40);
    CHECK(config.sister_capture_channels == 1);
    CHECK(config.sister_waveform_display_mode == TS_WAVEFORM_DISPLAY_STEREO);
    ts_sister_ui_model_init(&model, &config);
    CHECK(!model.visible && model.capture_channels == 1);
    CHECK(model.parameters.monitor_dry == 1.0f &&
          model.parameters.monitor_wet == 1.0f &&
          model.parameters.mix_output_gain == 4.0f &&
          model.parameters.write_erase == 1.0f);
    ts_sister_ui_model_show(&model);
    CHECK(model.visible);
    ts_sister_ui_model_hide(&model);
    CHECK(!model.visible);
    routing.enabled = 1;
    routing.rolling = 1;
    routing.monitor_enabled = 1;
    routing.source_mask = 0x8001u;
    ts_sister_ui_model_update(&model, &routing, NULL, NULL, NULL);
    CHECK(model.routing.enabled && model.routing.source_mask == 0x8001u);
    hit = ts_sister_ui_hit_test(12, 10);
    CHECK(hit.action == TS_SISTER_UI_ACTION_POWER);
    CHECK(ts_sister_ui_hit_test(540, 10).action == TS_SISTER_UI_ACTION_WAVE_MODE);
    CHECK(ts_sister_ui_hit_test(90, 195).action == TS_SISTER_UI_ACTION_SOURCE_FM);
    hit = ts_sister_ui_hit_test(100, 224);
    CHECK(hit.action == TS_SISTER_UI_ACTION_PARAMETER);
    CHECK(hit.index == TS_SISTER_UI_PARAM_H1_LEVEL);
    hit = ts_sister_ui_hit_test(440, 224);
    CHECK(hit.action == TS_SISTER_UI_ACTION_PARAMETER &&
          hit.index == TS_SISTER_UI_PARAM_FILTER_CUTOFF);
    hit = ts_sister_ui_hit_test(550, 308);
    CHECK(hit.action == TS_SISTER_UI_ACTION_PARAMETER &&
          hit.index == TS_SISTER_UI_PARAM_FILTER_GAIN);
    hit = ts_sister_ui_hit_test(20, 334);
    CHECK(hit.action == TS_SISTER_UI_ACTION_PARAMETER &&
          hit.index == TS_SISTER_UI_PARAM_MONITOR_DRY);
    hit = ts_sister_ui_hit_test(220, 334);
    CHECK(hit.action == TS_SISTER_UI_ACTION_PARAMETER &&
          hit.index == TS_SISTER_UI_PARAM_MONITOR_WET);
    hit = ts_sister_ui_hit_test(340, 334);
    CHECK(hit.action == TS_SISTER_UI_ACTION_PARAMETER &&
          hit.index == TS_SISTER_UI_PARAM_MIX_OUTPUT);
    hit = ts_sister_ui_hit_test(500, 334);
    CHECK(hit.action == TS_SISTER_UI_ACTION_PARAMETER &&
          hit.index == TS_SISTER_UI_PARAM_WRITE_ERASE);
    CHECK(strcmp(ts_sister_filter_type_name(TS_SISTER_FILTER_BYPASS), "OFF") == 0);
    CHECK(strcmp(ts_sister_filter_type_name(TS_SISTER_FILTER_LOWPASS), "LP") == 0);
    CHECK(ts_sister_ui_hit_test(455, 374).action == TS_SISTER_UI_ACTION_CAPTURE);
    CHECK(ts_sister_ui_hit_test(540, 374).action == TS_SISTER_UI_ACTION_OVERDUB);

    {
        int x = -1;
        int y = -1;
        /* SDL has already converted queued button/motion events to logical
           coordinates, including after a maximize. They must not be scaled a
           second time. Letterbox events arrive outside the logical bounds. */
        CHECK(ts_sister_ui_event_point(320, 200, &x, &y));
        CHECK(x == 320 && y == 200);
        CHECK(ts_sister_ui_event_point(10, 8, &x, &y));
        CHECK(x == 10 && y == 8);
        CHECK(!ts_sister_ui_event_point(-1, 200, &x, &y));
        CHECK(!ts_sister_ui_event_point(640, 200, &x, &y));
        CHECK(!ts_sister_ui_event_point(320, 400, &x, &y));

        /* SDL_GetMouseState remains in raw window coordinates. Native and 2x
           high-DPI output map to the same logical point for wheel targeting. */
        CHECK(ts_sister_ui_window_point(320, 200, 640, 400,
                                        640, 400, &x, &y));
        CHECK(x == 320 && y == 200);
        CHECK(ts_sister_ui_window_point(320, 200, 640, 400,
                                        1280, 800, &x, &y));
        CHECK(x == 320 && y == 200);

        /* A tall window letterboxes vertically: the bars are not controls. */
        CHECK(!ts_sister_ui_window_point(12, 20, 640, 480,
                                         640, 480, &x, &y));
        CHECK(ts_sister_ui_window_point(12, 48, 640, 480,
                                        640, 480, &x, &y));
        CHECK(x == 12 && y == 8);

        /* A wide window letterboxes horizontally and rejects either bar. */
        CHECK(!ts_sister_ui_window_point(20, 10, 800, 400,
                                         800, 400, &x, &y));
        CHECK(ts_sister_ui_window_point(90, 10, 800, 400,
                                        800, 400, &x, &y));
        CHECK(x == 10 && y == 10);
        CHECK(!ts_sister_ui_window_point(790, 10, 800, 400,
                                         800, 400, &x, &y));
    }
    ts_palette_default(&palette);
    ts_sister_ui_render(&framebuffer, &model, &palette);
    for (size_t pixel = 0u; pixel < TS_UI_WIDTH * TS_UI_HEIGHT; ++pixel) {
        first_hash ^= framebuffer.pixels[pixel];
        first_hash *= 1099511628211ull;
    }
    ts_sister_ui_render(&framebuffer, &model, &palette);
    for (size_t pixel = 0u; pixel < TS_UI_WIDTH * TS_UI_HEIGHT; ++pixel) {
        second_hash ^= framebuffer.pixels[pixel];
        second_hash *= 1099511628211ull;
    }
    CHECK(first_hash == second_hash && first_hash != 1469598103934665603ull);
    puts("Sister UI model tests passed");
    return failures != 0;
}
