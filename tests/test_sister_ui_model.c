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
    CHECK(model.parameter_locks == 0u);
    for (int parameter = 0; parameter < TS_SISTER_UI_PARAM_COUNT;
         ++parameter)
        CHECK(ts_sister_ui_parameter_lockable(parameter));
    CHECK(!ts_sister_ui_parameter_lockable(-1));
    CHECK(!ts_sister_ui_parameter_lockable(TS_SISTER_UI_PARAM_COUNT));
    CHECK(ts_sister_ui_parameter_lock_toggle(
              &model, TS_SISTER_UI_PARAM_FILTER_TYPE));
    CHECK(ts_sister_ui_parameter_locked(
              &model, TS_SISTER_UI_PARAM_FILTER_TYPE));
    CHECK(!ts_sister_ui_parameter_locked(
              &model, TS_SISTER_UI_PARAM_FILTER_CUTOFF));
    CHECK(ts_sister_ui_parameter_lock_toggle(
              &model, TS_SISTER_UI_PARAM_FILTER_CUTOFF));
    CHECK(ts_sister_ui_parameter_locked(
              &model, TS_SISTER_UI_PARAM_FILTER_TYPE));
    CHECK(ts_sister_ui_parameter_locked(
              &model, TS_SISTER_UI_PARAM_FILTER_CUTOFF));
    CHECK(ts_sister_ui_parameter_lock_toggle(
              &model, TS_SISTER_UI_PARAM_FILTER_TYPE));
    CHECK(!ts_sister_ui_parameter_locked(
              &model, TS_SISTER_UI_PARAM_FILTER_TYPE));
    CHECK(ts_sister_ui_parameter_lock_toggle(
              &model, TS_SISTER_UI_PARAM_FILTER_Q));
    CHECK(ts_sister_ui_parameter_locked(
              &model, TS_SISTER_UI_PARAM_FILTER_Q));
    CHECK(ts_sister_ui_parameter_lock_toggle(
              &model, TS_SISTER_UI_PARAM_BUFFER_SECONDS));
    CHECK(ts_sister_ui_parameter_locked(
              &model, TS_SISTER_UI_PARAM_BUFFER_SECONDS));
    CHECK(ts_sister_ui_parameter_lock_toggle(
              &model, TS_SISTER_UI_PARAM_REVERB_MIX));
    CHECK(ts_sister_ui_parameter_locked(
              &model, TS_SISTER_UI_PARAM_REVERB_MIX));
    model.parameter_locks = 0u;
    CHECK(model.parameters.input_gain == 1.0f &&
          model.parameters.tiles_gain == 1.0f &&
          model.parameters.fm_gain == 1.0f &&
          model.parameters.external_gain == 1.0f &&
          model.parameters.preview_gain == 1.0f &&
          model.parameters.fx_return_gain == 1.0f &&
          model.parameters.monitor_dry == 1.0f &&
          model.parameters.monitor_wet == 1.0f &&
          model.parameters.mix_output_gain == 4.0f &&
          model.parameters.write_erase == 1.0f &&
          model.parameters.ghost_tone == 0.0f &&
          model.parameters.soak == 0.0f &&
          model.parameters.bleed == 0.25f &&
          model.parameters.soak_targets == TS_SISTER_EFFECT_TARGET_MIX &&
          model.parameters.fx.reverb_targets == TS_SISTER_EFFECT_TARGET_MIX &&
          model.parameters.fx.delay_targets == TS_SISTER_EFFECT_TARGET_MIX &&
          model.parameters.fx.distortion_targets == TS_SISTER_EFFECT_TARGET_MIX &&
          model.parameters.buffer_seconds == 40.0f);
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
    CHECK(ts_sister_ui_hit_test(455, 10).action == TS_SISTER_UI_ACTION_PAGE);
    hit = ts_sister_ui_hit_test(360, 10);
    CHECK(hit.action == TS_SISTER_UI_ACTION_PARAMETER &&
          hit.index == TS_SISTER_UI_PARAM_BUFFER_SECONDS);
    CHECK(ts_sister_ui_hit_test(90, 177).action == TS_SISTER_UI_ACTION_SOURCE_FM);
    hit = ts_sister_ui_hit_test(555, 235);
    CHECK(hit.action == TS_SISTER_UI_ACTION_PARAMETER &&
          hit.index == TS_SISTER_UI_PARAM_TILES_GAIN);
    hit = ts_sister_ui_hit_test(570, 235);
    CHECK(hit.action == TS_SISTER_UI_ACTION_PARAMETER &&
          hit.index == TS_SISTER_UI_PARAM_FM_GAIN);
    hit = ts_sister_ui_hit_test(585, 235);
    CHECK(hit.action == TS_SISTER_UI_ACTION_PARAMETER &&
          hit.index == TS_SISTER_UI_PARAM_EXT_GAIN);
    hit = ts_sister_ui_hit_test(600, 235);
    CHECK(hit.action == TS_SISTER_UI_ACTION_PARAMETER &&
          hit.index == TS_SISTER_UI_PARAM_PREVIEW_GAIN);
    hit = ts_sister_ui_hit_test(615, 235);
    CHECK(hit.action == TS_SISTER_UI_ACTION_PARAMETER &&
          hit.index == TS_SISTER_UI_PARAM_FX_RETURN_GAIN);
    hit = ts_sister_ui_hit_test(100, 206);
    CHECK(hit.action == TS_SISTER_UI_ACTION_PARAMETER);
    CHECK(hit.index == TS_SISTER_UI_PARAM_H1_LEVEL);
    hit = ts_sister_ui_hit_test(440, 206);
    CHECK(hit.action == TS_SISTER_UI_ACTION_PARAMETER &&
          hit.index == TS_SISTER_UI_PARAM_FILTER_CUTOFF);
    hit = ts_sister_ui_hit_test(550, 290);
    CHECK(hit.action == TS_SISTER_UI_ACTION_PARAMETER &&
          hit.index == TS_SISTER_UI_PARAM_FILTER_GAIN);
    hit = ts_sister_ui_hit_test(440, 262);
    CHECK(hit.action == TS_SISTER_UI_ACTION_PARAMETER &&
          hit.index == TS_SISTER_UI_PARAM_FILTER_Q);
    hit = ts_sister_ui_hit_test(20, 312);
    CHECK(hit.action == TS_SISTER_UI_ACTION_PARAMETER &&
          hit.index == TS_SISTER_UI_PARAM_INPUT_GAIN);
    hit = ts_sister_ui_hit_test(120, 312);
    CHECK(hit.action == TS_SISTER_UI_ACTION_PARAMETER &&
          hit.index == TS_SISTER_UI_PARAM_MONITOR_DRY);
    hit = ts_sister_ui_hit_test(220, 312);
    CHECK(hit.action == TS_SISTER_UI_ACTION_PARAMETER &&
          hit.index == TS_SISTER_UI_PARAM_MONITOR_WET);
    hit = ts_sister_ui_hit_test(325, 312);
    CHECK(hit.action == TS_SISTER_UI_ACTION_PARAMETER &&
          hit.index == TS_SISTER_UI_PARAM_MIX_OUTPUT);
    hit = ts_sister_ui_hit_test(430, 312);
    CHECK(hit.action == TS_SISTER_UI_ACTION_PARAMETER &&
          hit.index == TS_SISTER_UI_PARAM_WRITE_ERASE);
    hit = ts_sister_ui_hit_test(530, 312);
    CHECK(hit.action == TS_SISTER_UI_ACTION_PARAMETER &&
          hit.index == TS_SISTER_UI_PARAM_GHOST_TONE);
    hit = ts_sister_ui_hit_test(20, 334);
    CHECK(hit.action == TS_SISTER_UI_ACTION_PARAMETER &&
          hit.index == TS_SISTER_UI_PARAM_SOAK);
    hit = ts_sister_ui_hit_test(150, 334);
    CHECK(hit.action == TS_SISTER_UI_ACTION_PARAMETER &&
          hit.index == TS_SISTER_UI_PARAM_BLEED);
    hit = ts_sister_ui_hit_test(280, 334);
    CHECK(hit.action == TS_SISTER_UI_ACTION_EFFECT_TARGET &&
          hit.index == TS_SISTER_EFFECT_TARGET_H1);
    hit = ts_sister_ui_hit_test(455, 334);
    CHECK(hit.action == TS_SISTER_UI_ACTION_EFFECT_TARGET &&
          hit.index == TS_SISTER_EFFECT_TARGET_MIX);
    CHECK(ts_sister_ui_hit_test(235, 374).action ==
          TS_SISTER_UI_ACTION_PRESET_PREVIOUS);
    CHECK(ts_sister_ui_hit_test(280, 374).action ==
          TS_SISTER_UI_ACTION_PRESET_MANAGE);
    model.preset_manage_open = 1;
    CHECK(ts_sister_ui_hit_test_model(&model, 190, 205).action ==
          TS_SISTER_UI_ACTION_PRESET_SAVE_AS);
    model.preset_manage_open = 0;
    CHECK(strcmp(ts_sister_filter_type_name(TS_SISTER_FILTER_BYPASS), "OFF") == 0);
    CHECK(strcmp(ts_sister_filter_type_name(TS_SISTER_FILTER_LOWPASS), "LP") == 0);
    CHECK(ts_sister_ui_hit_test(455, 374).action == TS_SISTER_UI_ACTION_CAPTURE);
    CHECK(ts_sister_ui_hit_test(540, 374).action == TS_SISTER_UI_ACTION_OVERDUB);
    model.fx_page = 1;
    hit = ts_sister_ui_hit_test_model(&model, 120, 76);
    CHECK(hit.action == TS_SISTER_UI_ACTION_PARAMETER &&
          hit.index == TS_SISTER_UI_PARAM_REVERB_TYPE);
    hit = ts_sister_ui_hit_test_model(&model, 260, 76);
    CHECK(hit.action == TS_SISTER_UI_ACTION_PARAMETER &&
          hit.index == TS_SISTER_UI_PARAM_REVERB_DECAY);
    hit = ts_sister_ui_hit_test_model(&model, 400, 76);
    CHECK(hit.action == TS_SISTER_UI_ACTION_PARAMETER &&
          hit.index == TS_SISTER_UI_PARAM_REVERB_MIX);
    hit = ts_sister_ui_hit_test_model(&model, 260, 154);
    CHECK(hit.action == TS_SISTER_UI_ACTION_PARAMETER &&
          hit.index == TS_SISTER_UI_PARAM_DELAY_FEEDBACK);
    hit = ts_sister_ui_hit_test_model(&model, 400, 232);
    CHECK(hit.action == TS_SISTER_UI_ACTION_PARAMETER &&
          hit.index == TS_SISTER_UI_PARAM_DISTORTION_MIX);
    hit = ts_sister_ui_hit_test_model(&model, 115, 101);
    CHECK(hit.action == TS_SISTER_UI_ACTION_EFFECT_TARGET &&
          hit.index == ((1 << 8) | TS_SISTER_EFFECT_TARGET_H1));
    hit = ts_sister_ui_hit_test_model(&model, 300, 257);
    CHECK(hit.action == TS_SISTER_UI_ACTION_EFFECT_TARGET &&
          hit.index == ((3 << 8) | TS_SISTER_EFFECT_TARGET_MIX));
    hit = ts_sister_ui_hit_test_model(&model, 200, 310);
    CHECK(hit.action == TS_SISTER_UI_ACTION_PARAMETER &&
          hit.index == TS_SISTER_UI_PARAM_MASTER_FX_FEEDBACK);
    model.fx_page = 2;
    hit = ts_sister_ui_hit_test_model(&model, 30, 55);
    CHECK(hit.action == TS_SISTER_UI_ACTION_FALLOUT_TOGGLE &&
          hit.index == TS_SISTER_UI_FALLOUT_POWER);
    hit = ts_sister_ui_hit_test_model(&model, 200, 56);
    CHECK(hit.action == TS_SISTER_UI_ACTION_PARAMETER &&
          hit.index == TS_SISTER_UI_PARAM_FALLOUT_MIX);
    hit = ts_sister_ui_hit_test_model(&model, 30, 190);
    CHECK(hit.action == TS_SISTER_UI_ACTION_FALLOUT_TOGGLE &&
          hit.index == TS_SISTER_UI_FALLOUT_SKIP);
    hit = ts_sister_ui_hit_test_model(&model, 350, 190);
    CHECK(hit.action == TS_SISTER_UI_ACTION_PARAMETER &&
          hit.index == TS_SISTER_UI_PARAM_FALLOUT_SKIP_RATE);
    model.fx_page = 0;

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
    model.waveform.channels = 2u;
    for (size_t bin = 0u; bin < TS_SISTER_WAVE_BIN_COUNT; ++bin) {
        model.waveform.bins[bin].left_minimum = -1.0f;
        model.waveform.bins[bin].left_maximum = 1.0f;
        model.waveform.bins[bin].right_minimum = -1.0f;
        model.waveform.bins[bin].right_maximum = 1.0f;
    }
    ts_sister_ui_render(&framebuffer, &model, &palette);
    /* Full-scale Sister bins must reach the top half of the left lane and
       the last plot column.  The ordinary-canvas line clip begins at y=64
       and ends at x=619, so these pixels guard against accidentally reusing
       that canvas-only helper here. */
    CHECK(framebuffer.pixels[43u * TS_UI_WIDTH + 100u] ==
          palette.colors[TS_PALETTE_STEREO_WAVE_LEFT]);
    CHECK(framebuffer.pixels[43u * TS_UI_WIDTH + 625u] ==
          palette.colors[TS_PALETTE_STEREO_WAVE_LEFT]);
    CHECK(framebuffer.pixels[106u * TS_UI_WIDTH + 625u] ==
          palette.colors[TS_PALETTE_STEREO_WAVE_RIGHT]);
    /* PR8's default MIX target has its stereo-role top/under lines. H1 is
       inactive, while the default 25-percent BLEED bar is visibly filled. */
    CHECK(framebuffer.pixels[331u * TS_UI_WIDTH + 451u] ==
          palette.colors[TS_PALETTE_STEREO_WAVE_RIGHT]);
    CHECK(framebuffer.pixels[331u * TS_UI_WIDTH + 277u] !=
          palette.colors[TS_PALETTE_PATTERN_NOTE]);
    CHECK(framebuffer.pixels[344u * TS_UI_WIDTH + 145u] ==
          palette.colors[TS_PALETTE_PATTERN_EFFECT]);
    model.routing.capture_state = TS_CAPTURE_RECORDING;
    model.routing.capture_recorded_frames = 25u;
    model.routing.capture_capacity_frames = 100u;
    model.capture_overdub = 0;
    model.text_cursor_visible = 1;
    ts_sister_ui_render(&framebuffer, &model, &palette);
    CHECK(framebuffer.pixels[40u * TS_UI_WIDTH + 164u] ==
          palette.colors[TS_PALETTE_PATTERN_VOLUME]);
    CHECK(framebuffer.pixels[40u * TS_UI_WIDTH + 165u] == 0xff1e0808u);
    CHECK(framebuffer.pixels[368u * TS_UI_WIDTH + 448u] ==
          palette.colors[TS_PALETTE_ACTIVE_TILE]);
    model.text_cursor_visible = 0;
    ts_sister_ui_render(&framebuffer, &model, &palette);
    CHECK(framebuffer.pixels[368u * TS_UI_WIDTH + 448u] !=
          palette.colors[TS_PALETTE_ACTIVE_TILE]);
    model.capture_overdub = 1;
    model.text_cursor_visible = 1;
    ts_sister_ui_render(&framebuffer, &model, &palette);
    CHECK(framebuffer.pixels[368u * TS_UI_WIDTH + 536u] ==
          palette.colors[TS_PALETTE_ACTIVE_TILE]);
    model.routing.capture_state = TS_CAPTURE_IDLE;
    model.capture_overdub = 0;
    model.text_cursor_visible = 0;
    model.parameter_locks =
        TS_SISTER_UI_PARAMETER_BIT(TS_SISTER_UI_PARAM_H1_LEVEL);
    ts_sister_ui_render(&framebuffer, &model, &palette);
    {
        uint32_t color = palette.colors[TS_PALETTE_PATTERN_NOTE];
        uint32_t dimmed = 0xff000000u |
            (uint32_t)(12u + (((color >> 16) & 0xffu) * 2u / 5u)) << 16 |
            (uint32_t)(12u + (((color >> 8) & 0xffu) * 2u / 5u)) << 8 |
            (uint32_t)(12u + ((color & 0xffu) * 2u / 5u));
        CHECK(framebuffer.pixels[216u * TS_UI_WIDTH + 73u] == dimmed);
        /* Dimming alone communicates the lock. The former trailing L began
           at this otherwise-empty pixel. */
        CHECK(framebuffer.pixels[205u * TS_UI_WIDTH + 173u] == 0xff181719u);
    }
    model.parameter_locks = 0u;
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
    model.parameters.soak_targets = TS_SISTER_EFFECT_TARGET_H1 |
                                    TS_SISTER_EFFECT_TARGET_H3;
    ts_sister_ui_render(&framebuffer, &model, &palette);
    CHECK(framebuffer.pixels[331u * TS_UI_WIDTH + 277u] ==
          palette.colors[TS_PALETTE_PATTERN_NOTE]);
    CHECK(framebuffer.pixels[331u * TS_UI_WIDTH + 393u] ==
          palette.colors[TS_PALETTE_PATTERN_TUNING]);
    CHECK(framebuffer.pixels[331u * TS_UI_WIDTH + 451u] !=
          palette.colors[TS_PALETTE_STEREO_WAVE_RIGHT]);
    model.fx_page = 1;
    model.parameters.fx.reverb_mix = 0.5f;
    model.parameters.fx.delay_mix = 0.5f;
    model.parameters.fx.distortion_mix = 0.5f;
    ts_sister_ui_render(&framebuffer, &model, &palette);
    CHECK(framebuffer.pixels[98u * TS_UI_WIDTH + 297u] ==
          palette.colors[TS_PALETTE_STEREO_WAVE_RIGHT]);
    CHECK(framebuffer.pixels[176u * TS_UI_WIDTH + 297u] ==
          palette.colors[TS_PALETTE_PATTERN_EFFECT]);
    CHECK(framebuffer.pixels[254u * TS_UI_WIDTH + 297u] ==
          palette.colors[TS_PALETTE_PATTERN_VOLUME]);
    CHECK(framebuffer.pixels[320u * TS_UI_WIDTH + 111u] !=
          palette.colors[TS_PALETTE_PATTERN_TUNING]);
    puts("Sister UI model tests passed");
    return failures != 0;
}
