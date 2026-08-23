#include "tapesister/sister_ui.h"

#include <stdio.h>
#include <string.h>

static int contains(int x, int y, int left, int top, int width, int height)
{
    return x >= left && x < left + width && y >= top && y < top + height;
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

TsSisterUiHit ts_sister_ui_hit_test(int x, int y)
{
    TsSisterUiHit hit = {TS_SISTER_UI_ACTION_NONE, 0, 0.0f};
    static const TsSisterUiAction transport[5] = {
        TS_SISTER_UI_ACTION_POWER, TS_SISTER_UI_ACTION_ROLL,
        TS_SISTER_UI_ACTION_HOLD, TS_SISTER_UI_ACTION_CLEAR,
        TS_SISTER_UI_ACTION_MONITOR
    };
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
    for (int source = 0; source < 4; ++source) {
        if (contains(x, y, 10 + source * 76, 190, 70, 20)) {
            hit.action = (TsSisterUiAction)(TS_SISTER_UI_ACTION_SOURCE_TILES + source);
            hit.index = source;
            return hit;
        }
    }
    /* Three compact head rows: three continuous fields plus H2 feedback. */
    for (int row = 0; row < 3; ++row) {
        int top = 220 + row * 28;
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
        if (contains(x, y, 10 + control * 88, 304, 82, 18)) {
            hit.action = TS_SISTER_UI_ACTION_PARAMETER;
            hit.index = parameters[control];
            hit.normalized = (float)(x - (10 + control * 88)) / 81.0f;
            return hit;
        }
    }
    if (contains(x, y, 10, 330, 58, 22)) hit.action = TS_SISTER_UI_ACTION_TAP;
    else if (contains(x, y, 74, 330, 44, 22)) hit.action = TS_SISTER_UI_ACTION_CAPTURE_FORMAT;
    else if (contains(x, y, 124, 330, 100, 22)) hit.action = TS_SISTER_UI_ACTION_DESTINATION;
    else if (contains(x, y, 450, 330, 82, 22)) hit.action = TS_SISTER_UI_ACTION_CAPTURE;
    else if (contains(x, y, 538, 330, 92, 22)) hit.action = TS_SISTER_UI_ACTION_OVERDUB;
    return hit;
}
