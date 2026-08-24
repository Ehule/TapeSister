#ifndef TAPESISTER_SISTER_UI_H
#define TAPESISTER_SISTER_UI_H

#include "tapesister/config.h"
#include "tapesister/palette.h"
#include "tapesister/sister_runtime.h"
#include "tapesister/ui.h"

enum { TS_SISTER_UI_WIDTH = 640, TS_SISTER_UI_HEIGHT = 400 };

typedef enum {
    TS_SISTER_UI_ACTION_NONE = 0,
    TS_SISTER_UI_ACTION_POWER,
    TS_SISTER_UI_ACTION_ROLL,
    TS_SISTER_UI_ACTION_HOLD,
    TS_SISTER_UI_ACTION_CLEAR,
    TS_SISTER_UI_ACTION_MONITOR,
    TS_SISTER_UI_ACTION_WAVE_MODE,
    TS_SISTER_UI_ACTION_SOURCE_TILES,
    TS_SISTER_UI_ACTION_SOURCE_FM,
    TS_SISTER_UI_ACTION_SOURCE_EXT,
    TS_SISTER_UI_ACTION_SOURCE_PREVIEW,
    TS_SISTER_UI_ACTION_TAP,
    TS_SISTER_UI_ACTION_CAPTURE_FORMAT,
    TS_SISTER_UI_ACTION_DESTINATION,
    TS_SISTER_UI_ACTION_CAPTURE,
    TS_SISTER_UI_ACTION_OVERDUB,
    TS_SISTER_UI_ACTION_PARAMETER
} TsSisterUiAction;

typedef enum {
    TS_SISTER_UI_DEST_CURRENT = 0,
    TS_SISTER_UI_DEST_NEXT_EMPTY
} TsSisterUiDestinationMode;

typedef enum {
    TS_SISTER_UI_PARAM_H1_LEVEL = 0,
    TS_SISTER_UI_PARAM_H1_TIME,
    TS_SISTER_UI_PARAM_H1_FEEDBACK,
    TS_SISTER_UI_PARAM_H2_LEVEL,
    TS_SISTER_UI_PARAM_H2_SCRUB,
    TS_SISTER_UI_PARAM_H2_RATE,
    TS_SISTER_UI_PARAM_H2_FEEDBACK,
    TS_SISTER_UI_PARAM_H3_LEVEL,
    TS_SISTER_UI_PARAM_H3_SPAN,
    TS_SISTER_UI_PARAM_H3_RATE,
    TS_SISTER_UI_PARAM_WOW,
    TS_SISTER_UI_PARAM_DROP,
    TS_SISTER_UI_PARAM_DUCK,
    TS_SISTER_UI_PARAM_DECORRELATE,
    TS_SISTER_UI_PARAM_WIDTH,
    TS_SISTER_UI_PARAM_FILTER_TYPE,
    TS_SISTER_UI_PARAM_FILTER_CUTOFF,
    TS_SISTER_UI_PARAM_FILTER_Q,
    TS_SISTER_UI_PARAM_FILTER_GAIN,
    TS_SISTER_UI_PARAM_INPUT_GAIN,
    TS_SISTER_UI_PARAM_MONITOR_DRY,
    TS_SISTER_UI_PARAM_MONITOR_WET,
    TS_SISTER_UI_PARAM_MIX_OUTPUT,
    TS_SISTER_UI_PARAM_WRITE_ERASE,
    TS_SISTER_UI_PARAM_COUNT
} TsSisterUiParameter;

typedef struct {
    TsSisterUiAction action;
    int index;
    float normalized;
} TsSisterUiHit;

typedef struct {
    int visible;
    int capture_channels;
    int capture_overdub;
    TsSisterTap selected_tap;
    TsSisterUiDestinationMode destination_mode;
    TsWaveformDisplayMode waveform_mode;
    TsSisterRoutingSnapshot routing;
    TsSisterSnapshot engine;
    TsSisterWaveSnapshot waveform;
    TsSisterParameters parameters;
    int destination_slot;
    char status[128];
} TsSisterUiModel;

void ts_sister_ui_model_init(TsSisterUiModel *model, const TsConfig *config);
void ts_sister_ui_model_show(TsSisterUiModel *model);
void ts_sister_ui_model_hide(TsSisterUiModel *model);
void ts_sister_ui_model_update(TsSisterUiModel *model,
                               const TsSisterRoutingSnapshot *routing,
                               const TsSisterSnapshot *engine,
                               const TsSisterWaveSnapshot *waveform,
                               const TsSisterParameters *parameters);
TsSisterUiHit ts_sister_ui_hit_test(int x, int y);
int ts_sister_ui_event_point(int event_x, int event_y,
                             int *logical_x, int *logical_y);
int ts_sister_ui_window_point(int raw_x, int raw_y,
                              int window_width, int window_height,
                              int output_width, int output_height,
                              int *logical_x, int *logical_y);
void ts_sister_ui_render(TsFramebuffer *framebuffer,
                         const TsSisterUiModel *model,
                         const TsPalette *palette);

#endif
