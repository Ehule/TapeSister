#include "tapesister/sister_ui.h"

#include <stdio.h>
#include <string.h>

static int contains(int x, int y, int left, int top, int width, int height)
{
    return x >= left && x < left + width && y >= top && y < top + height;
}

int ts_sister_ui_event_point(int event_x, int event_y,
                             int *logical_x, int *logical_y)
{
    if (logical_x == NULL || logical_y == NULL || event_x < 0 || event_y < 0 ||
        event_x >= TS_SISTER_UI_WIDTH || event_y >= TS_SISTER_UI_HEIGHT)
        return 0;

    /* SDL_RenderSetLogicalSize filters queued mouse events into the renderer's
       logical coordinate space. Do not scale button or motion event positions
       again when the window is resized or maximized. */
    *logical_x = event_x;
    *logical_y = event_y;
    return 1;
}

int ts_sister_ui_window_point(int raw_x, int raw_y,
                              int window_width, int window_height,
                              int output_width, int output_height,
                              int *logical_x, int *logical_y)
{
    double output_x;
    double output_y;
    double scale_x;
    double scale_y;
    double scale;
    double viewport_width;
    double viewport_height;
    double viewport_x;
    double viewport_y;
    double x;
    double y;

    if (logical_x == NULL || logical_y == NULL || window_width <= 0 ||
        window_height <= 0 || output_width <= 0 || output_height <= 0)
        return 0;

    /* SDL_GetMouseState returns unfiltered window coordinates, unlike queued
       button and motion events. Convert that raw state through the letterboxed
       high-DPI renderer for mouse-wheel targeting. */
    output_x = (double)raw_x * (double)output_width / (double)window_width;
    output_y = (double)raw_y * (double)output_height / (double)window_height;
    scale_x = (double)output_width / (double)TS_SISTER_UI_WIDTH;
    scale_y = (double)output_height / (double)TS_SISTER_UI_HEIGHT;
    scale = scale_x < scale_y ? scale_x : scale_y;
    if (scale <= 0.0) return 0;

    viewport_width = (double)TS_SISTER_UI_WIDTH * scale;
    viewport_height = (double)TS_SISTER_UI_HEIGHT * scale;
    viewport_x = ((double)output_width - viewport_width) * 0.5;
    viewport_y = ((double)output_height - viewport_height) * 0.5;
    x = (output_x - viewport_x) / scale;
    y = (output_y - viewport_y) / scale;
    if (x < 0.0 || x >= (double)TS_SISTER_UI_WIDTH ||
        y < 0.0 || y >= (double)TS_SISTER_UI_HEIGHT)
        return 0;

    *logical_x = (int)x;
    *logical_y = (int)y;
    return 1;
}

void ts_sister_ui_model_init(TsSisterUiModel *model, const TsConfig *config)
{
    if (model == NULL) return;
    memset(model, 0, sizeof(*model));
    model->capture_channels = config != NULL ? config->sister_capture_channels : 1;
    model->waveform_mode = ts_waveform_display_sanitize(
        config != NULL ? config->sister_waveform_display_mode : 0);
    model->selected_tap = TS_SISTER_TAP_MIX;
    model->destination_slot = -1;
    ts_sister_parameters_default(&model->parameters, 48000u);
    if (config != NULL) {
        model->parameters.buffer_seconds =
            (float)config->sister_buffer_seconds;
        model->parameters.input_gain =
            (float)config->sister_input_percent / 100.0f;
        model->parameters.monitor_dry =
            (float)config->sister_dry_percent / 100.0f;
        model->parameters.monitor_wet =
            (float)config->sister_wet_percent / 100.0f;
        model->parameters.mix_output_gain =
            (float)config->sister_output_percent / 100.0f;
        model->parameters.write_erase =
            (float)config->sister_erase_percent / 100.0f;
        model->parameters.ghost_tone =
            (float)config->sister_ghost_percent / 100.0f;
    }
    snprintf(model->status, sizeof(model->status),
             "CLICK POWER TO ENABLE - WINDOW CLOSE HIDES ONLY");
}

void ts_sister_ui_model_show(TsSisterUiModel *model)
{
    if (model != NULL) model->visible = 1;
}

void ts_sister_ui_model_hide(TsSisterUiModel *model)
{
    if (model != NULL) model->visible = 0;
}

void ts_sister_ui_model_update(TsSisterUiModel *model,
                               const TsSisterRoutingSnapshot *routing,
                               const TsSisterSnapshot *engine,
                               const TsSisterWaveSnapshot *waveform,
                               const TsSisterParameters *parameters)
{
    if (model == NULL) return;
    if (routing != NULL) {
        model->routing = *routing;
        model->destination_slot = routing->capture_destination;
    }
    if (engine != NULL) model->engine = *engine;
    if (waveform != NULL) model->waveform = *waveform;
    if (parameters != NULL) model->parameters = *parameters;
}

TsSisterUiHit ts_sister_ui_hit_test_model(const TsSisterUiModel *model,
                                          int x, int y)
{
    TsSisterUiHit hit = {TS_SISTER_UI_ACTION_NONE, 0, 0.0f};
    static const TsSisterUiAction transport[5] = {
        TS_SISTER_UI_ACTION_POWER, TS_SISTER_UI_ACTION_ROLL,
        TS_SISTER_UI_ACTION_HOLD, TS_SISTER_UI_ACTION_CLEAR,
        TS_SISTER_UI_ACTION_MONITOR
    };
    if (model != NULL && model->preset_manage_open) {
        if (contains(x, y, 180, 200, 128, 22))
            hit.action = TS_SISTER_UI_ACTION_PRESET_SAVE_AS;
        else if (contains(x, y, 332, 200, 128, 22))
            hit.action = TS_SISTER_UI_ACTION_PRESET_OVERWRITE;
        else if (contains(x, y, 180, 230, 128, 22))
            hit.action = TS_SISTER_UI_ACTION_PRESET_RENAME;
        else if (contains(x, y, 332, 230, 128, 22))
            hit.action = TS_SISTER_UI_ACTION_PRESET_DELETE;
        else if (contains(x, y, 180, 260, 128, 22))
            hit.action = TS_SISTER_UI_ACTION_PRESET_CONFIRM;
        else if (contains(x, y, 332, 260, 128, 22))
            hit.action = TS_SISTER_UI_ACTION_PRESET_CANCEL;
        return hit;
    }
    for (int control = 0; control < 5; ++control) {
        if (contains(x, y, 10 + control * 68, 8, 62, 22)) {
            hit.action = transport[control];
            return hit;
        }
    }
    if (contains(x, y, 532, 8, 98, 22)) {
        hit.action = TS_SISTER_UI_ACTION_WAVE_MODE;
        return hit;
    }
    if (contains(x, y, 350, 8, 94, 22)) {
        hit.action = TS_SISTER_UI_ACTION_PARAMETER;
        hit.index = TS_SISTER_UI_PARAM_BUFFER_SECONDS;
        hit.normalized = (float)(x - 350) / 93.0f;
        return hit;
    }
    if (contains(x, y, 450, 8, 76, 22)) {
        hit.action = TS_SISTER_UI_ACTION_PAGE;
        return hit;
    }
    if (contains(x, y, 230, 370, 28, 22)) {
        hit.action = TS_SISTER_UI_ACTION_PRESET_PREVIOUS;
        return hit;
    }
    if (contains(x, y, 264, 370, 130, 22)) {
        hit.action = TS_SISTER_UI_ACTION_PRESET_MANAGE;
        return hit;
    }
    if (contains(x, y, 400, 370, 28, 22)) {
        hit.action = TS_SISTER_UI_ACTION_PRESET_NEXT;
        return hit;
    }
    if (model != NULL && model->fx_page) {
        static const int parameter[3][3] = {
            {TS_SISTER_UI_PARAM_REVERB_TYPE, TS_SISTER_UI_PARAM_REVERB_MIX,
             TS_SISTER_UI_PARAM_REVERB_DECAY},
            {TS_SISTER_UI_PARAM_DELAY_TIME, TS_SISTER_UI_PARAM_DELAY_FEEDBACK,
             TS_SISTER_UI_PARAM_DELAY_MIX},
            {TS_SISTER_UI_PARAM_DISTORTION_DRIVE,
             TS_SISTER_UI_PARAM_DISTORTION_TONE,
             TS_SISTER_UI_PARAM_DISTORTION_MIX}
        };
        for (int row = 0; row < 3; ++row) {
            int top = 72 + row * 78;
            for (int field = 0; field < 3; ++field) {
                int left = 110 + field * 140;
                if (contains(x, y, left, top, 130, 18)) {
                    hit.action = TS_SISTER_UI_ACTION_PARAMETER;
                    hit.index = parameter[row][field];
                    hit.normalized = (float)(x - left) / 129.0f;
                    return hit;
                }
            }
            for (int target = 0; target < TS_SISTER_EFFECT_PROCESSOR_COUNT;
                 ++target) {
                if (contains(x, y, 110 + target * 62, top + 25, 56, 18)) {
                    hit.action = TS_SISTER_UI_ACTION_EFFECT_TARGET;
                    hit.index = ((row + 1) << 8) | (1 << target);
                    return hit;
                }
            }
        }
        if (contains(x, y, 110, 306, 410, 18)) {
            hit.action = TS_SISTER_UI_ACTION_PARAMETER;
            hit.index = TS_SISTER_UI_PARAM_MASTER_FX_FEEDBACK;
            hit.normalized = (float)(x - 110) / 409.0f;
            return hit;
        }
        if (contains(x, y, 10, 370, 58, 22)) hit.action = TS_SISTER_UI_ACTION_TAP;
        else if (contains(x, y, 74, 370, 44, 22)) hit.action = TS_SISTER_UI_ACTION_CAPTURE_FORMAT;
        else if (contains(x, y, 124, 370, 100, 22)) hit.action = TS_SISTER_UI_ACTION_DESTINATION;
        else if (contains(x, y, 450, 370, 82, 22)) hit.action = TS_SISTER_UI_ACTION_CAPTURE;
        else if (contains(x, y, 538, 370, 92, 22)) hit.action = TS_SISTER_UI_ACTION_OVERDUB;
        return hit;
    }
    for (int source = 0; source < 4; ++source) {
        if (contains(x, y, 10 + source * 76, 172, 70, 20)) {
            hit.action = (TsSisterUiAction)(TS_SISTER_UI_ACTION_SOURCE_TILES + source);
            hit.index = source;
            return hit;
        }
    }
    /* Three compact head rows: three continuous fields plus H2 feedback. */
    for (int row = 0; row < 3; ++row) {
        int top = 202 + row * 28;
        for (int field = 0; field < 4; ++field) {
            int left = 72 + field * 120;
            if (contains(x, y, left, top, 110, 18)) {
                static const int parameter[3][4] = {
                    {TS_SISTER_UI_PARAM_H1_LEVEL, TS_SISTER_UI_PARAM_H1_TIME,
                     TS_SISTER_UI_PARAM_H1_FEEDBACK,
                     TS_SISTER_UI_PARAM_FILTER_CUTOFF},
                    {TS_SISTER_UI_PARAM_H2_LEVEL, TS_SISTER_UI_PARAM_H2_SCRUB,
                     TS_SISTER_UI_PARAM_H2_RATE, TS_SISTER_UI_PARAM_H2_FEEDBACK},
                    {TS_SISTER_UI_PARAM_H3_LEVEL, TS_SISTER_UI_PARAM_H3_SPAN,
                     TS_SISTER_UI_PARAM_H3_RATE, TS_SISTER_UI_PARAM_FILTER_Q}
                };
                hit.action = TS_SISTER_UI_ACTION_PARAMETER;
                hit.index = parameter[row][field];
                hit.normalized = (float)(x - left) / 109.0f;
                return hit;
            }
        }
    }
    for (int control = 0; control < 7; ++control) {
        static const int parameters[7] = {
            TS_SISTER_UI_PARAM_WOW, TS_SISTER_UI_PARAM_DROP,
            TS_SISTER_UI_PARAM_DUCK, TS_SISTER_UI_PARAM_DECORRELATE,
            TS_SISTER_UI_PARAM_WIDTH, TS_SISTER_UI_PARAM_FILTER_TYPE,
            TS_SISTER_UI_PARAM_FILTER_GAIN
        };
        if (contains(x, y, 10 + control * 88, 286, 82, 18)) {
            hit.action = TS_SISTER_UI_ACTION_PARAMETER;
            hit.index = parameters[control];
            hit.normalized = (float)(x - (10 + control * 88)) / 81.0f;
            return hit;
        }
    }
    for (int control = 0; control < 6; ++control) {
        static const int parameters[6] = {
            TS_SISTER_UI_PARAM_INPUT_GAIN,
            TS_SISTER_UI_PARAM_MONITOR_DRY,
            TS_SISTER_UI_PARAM_MONITOR_WET,
            TS_SISTER_UI_PARAM_MIX_OUTPUT,
            TS_SISTER_UI_PARAM_WRITE_ERASE,
            TS_SISTER_UI_PARAM_GHOST_TONE
        };
        int left = 10 + control * 103;
        if (contains(x, y, left, 308, 98, 18)) {
            hit.action = TS_SISTER_UI_ACTION_PARAMETER;
            hit.index = parameters[control];
            hit.normalized = (float)(x - left) / 97.0f;
            return hit;
        }
    }
    if (contains(x, y, 10, 330, 124, 18)) {
        hit.action = TS_SISTER_UI_ACTION_PARAMETER;
        hit.index = TS_SISTER_UI_PARAM_SOAK;
        hit.normalized = (float)(x - 10) / 123.0f;
        return hit;
    }
    if (contains(x, y, 140, 330, 124, 18)) {
        hit.action = TS_SISTER_UI_ACTION_PARAMETER;
        hit.index = TS_SISTER_UI_PARAM_BLEED;
        hit.normalized = (float)(x - 140) / 123.0f;
        return hit;
    }
    for (int target = 0; target < TS_SISTER_EFFECT_PROCESSOR_COUNT; ++target) {
        if (contains(x, y, 276 + target * 58, 330, 52, 18)) {
            hit.action = TS_SISTER_UI_ACTION_EFFECT_TARGET;
            hit.index = 1u << target;
            return hit;
        }
    }
    if (contains(x, y, 10, 370, 58, 22)) hit.action = TS_SISTER_UI_ACTION_TAP;
    else if (contains(x, y, 74, 370, 44, 22)) hit.action = TS_SISTER_UI_ACTION_CAPTURE_FORMAT;
    else if (contains(x, y, 124, 370, 100, 22)) hit.action = TS_SISTER_UI_ACTION_DESTINATION;
    else if (contains(x, y, 450, 370, 82, 22)) hit.action = TS_SISTER_UI_ACTION_CAPTURE;
    else if (contains(x, y, 538, 370, 92, 22)) hit.action = TS_SISTER_UI_ACTION_OVERDUB;
    return hit;
}

TsSisterUiHit ts_sister_ui_hit_test(int x, int y)
{
    return ts_sister_ui_hit_test_model(NULL, x, y);
}
