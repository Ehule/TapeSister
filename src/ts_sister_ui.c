#include "tapesister/sister_ui.h"

#include <stdio.h>
#include <string.h>

static int contains(int x, int y, int left, int top, int width, int height)
{
    return x >= left && x < left + width && y >= top && y < top + height;
}

int ts_sister_ui_parameter_lockable(int parameter)
{
    return parameter >= 0 && parameter < TS_SISTER_UI_PARAM_COUNT;
}

int ts_sister_ui_parameter_locked(const TsSisterUiModel *model,
                                  int parameter)
{
    if (model == NULL || !ts_sister_ui_parameter_lockable(parameter)) return 0;
    if (parameter < 64)
        return (model->parameter_locks &
                TS_SISTER_UI_PARAMETER_BIT(parameter)) != 0u;
    return (model->parameter_locks_high &
            TS_SISTER_UI_PARAMETER_BIT(parameter - 64)) != 0u;
}

int ts_sister_ui_parameter_lock_toggle(TsSisterUiModel *model,
                                       int parameter)
{
    uint64_t bit;
    if (model == NULL || !ts_sister_ui_parameter_lockable(parameter)) return 0;
    if (parameter < 64) {
        bit = TS_SISTER_UI_PARAMETER_BIT(parameter);
        model->parameter_locks ^= bit;
    } else {
        bit = TS_SISTER_UI_PARAMETER_BIT(parameter - 64);
        model->parameter_locks_high ^= bit;
    }
    return 1;
}

static int lock_words_contain(uint64_t locks, uint64_t locks_high,
                              int parameter)
{
    if (parameter < 0 || parameter >= TS_SISTER_UI_PARAM_COUNT) return 0;
    return parameter < 64 ?
        (locks & TS_SISTER_UI_PARAMETER_BIT(parameter)) != 0u :
        (locks_high & TS_SISTER_UI_PARAMETER_BIT(parameter - 64)) != 0u;
}

static void lock_words_add(uint64_t *locks, uint64_t *locks_high,
                           int parameter)
{
    if (locks == NULL || locks_high == NULL || parameter < 0 ||
        parameter >= TS_SISTER_UI_PARAM_COUNT) return;
    if (parameter < 64)
        *locks |= TS_SISTER_UI_PARAMETER_BIT(parameter);
    else
        *locks_high |= TS_SISTER_UI_PARAMETER_BIT(parameter - 64);
}

static void migrate_effect_lock(uint64_t *locks, uint64_t *locks_high,
                                int legacy_parameter, int slot_parameter)
{
    if (locks == NULL || locks_high == NULL) return;
    if (lock_words_contain(*locks, *locks_high, legacy_parameter))
        lock_words_add(locks, locks_high, slot_parameter);
}

void ts_sister_ui_migrate_legacy_effect_locks(uint64_t *locks,
                                              uint64_t *locks_high)
{
    /* The old fixed chain migrates to Distortion, Grain, Delay, Reverb.
       Retain the old bits as historical data and add their slot equivalents. */
    migrate_effect_lock(locks, locks_high, TS_SISTER_UI_PARAM_DISTORTION_GAIN,
        TS_SISTER_UI_SLOT_PARAMETER(0, 0));
    migrate_effect_lock(locks, locks_high, TS_SISTER_UI_PARAM_DISTORTION_DRIVE,
        TS_SISTER_UI_SLOT_PARAMETER(0, 1));
    migrate_effect_lock(locks, locks_high, TS_SISTER_UI_PARAM_DISTORTION_TONE,
        TS_SISTER_UI_SLOT_PARAMETER(0, 2));
    migrate_effect_lock(locks, locks_high, TS_SISTER_UI_PARAM_DISTORTION_MIX,
        TS_SISTER_UI_SLOT_PARAMETER(0, 4));
    migrate_effect_lock(locks, locks_high, TS_SISTER_UI_PARAM_GRAIN_GAIN,
        TS_SISTER_UI_SLOT_PARAMETER(1, 0));
    migrate_effect_lock(locks, locks_high, TS_SISTER_UI_PARAM_GRAIN_SIZE,
        TS_SISTER_UI_SLOT_PARAMETER(1, 1));
    migrate_effect_lock(locks, locks_high, TS_SISTER_UI_PARAM_GRAIN_DENSITY,
        TS_SISTER_UI_SLOT_PARAMETER(1, 2));
    migrate_effect_lock(locks, locks_high, TS_SISTER_UI_PARAM_GRAIN_PITCH,
        TS_SISTER_UI_SLOT_PARAMETER(1, 3));
    migrate_effect_lock(locks, locks_high, TS_SISTER_UI_PARAM_GRAIN_MIX,
        TS_SISTER_UI_SLOT_PARAMETER(1, 4));
    migrate_effect_lock(locks, locks_high, TS_SISTER_UI_PARAM_DELAY_GAIN,
        TS_SISTER_UI_SLOT_PARAMETER(2, 0));
    migrate_effect_lock(locks, locks_high, TS_SISTER_UI_PARAM_DELAY_TIME,
        TS_SISTER_UI_SLOT_PARAMETER(2, 1));
    migrate_effect_lock(locks, locks_high, TS_SISTER_UI_PARAM_DELAY_FEEDBACK,
        TS_SISTER_UI_SLOT_PARAMETER(2, 2));
    migrate_effect_lock(locks, locks_high, TS_SISTER_UI_PARAM_DELAY_MIX,
        TS_SISTER_UI_SLOT_PARAMETER(2, 4));
    migrate_effect_lock(locks, locks_high, TS_SISTER_UI_PARAM_REVERB_GAIN,
        TS_SISTER_UI_SLOT_PARAMETER(3, 0));
    migrate_effect_lock(locks, locks_high, TS_SISTER_UI_PARAM_REVERB_TYPE,
        TS_SISTER_UI_SLOT_PARAMETER(3, 1));
    migrate_effect_lock(locks, locks_high, TS_SISTER_UI_PARAM_REVERB_DECAY,
        TS_SISTER_UI_SLOT_PARAMETER(3, 2));
    migrate_effect_lock(locks, locks_high, TS_SISTER_UI_PARAM_REVERB_MIX,
        TS_SISTER_UI_SLOT_PARAMETER(3, 4));
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
    model->capture_channels = config != NULL ? config->capture_channels : 1;
    model->waveform_mode = ts_waveform_display_sanitize(
        config != NULL ? config->sister_waveform_display_mode : 0);
    model->routing.limiter_enabled = config != NULL ?
        config->sister_limiter_enabled : TS_SISTER_LIMITER_DEFAULT_ENABLED;
    model->routing.limiter_ceiling_db = config != NULL ?
        config->sister_limiter_ceiling_db :
        TS_SISTER_LIMITER_DEFAULT_CEILING_DB;
    model->routing.master_output_gain = config != NULL ?
        (float)config->master_output_percent / 100.0f : 1.0f;
    model->selected_tap = TS_SISTER_TAP_MIX;
    model->destination_slot = -1;
    ts_sister_parameters_default(&model->parameters, 48000u);
    if (config != NULL) {
        model->parameters.buffer_seconds =
            (float)config->sister_buffer_seconds;
        model->parameters.input_gain =
            (float)config->sister_input_percent / 100.0f;
        model->parameters.tiles_gain =
            (float)config->sister_tiles_percent / 100.0f;
        model->parameters.fm_gain =
            (float)config->sister_fm_percent / 100.0f;
        model->parameters.external_gain =
            (float)config->sister_ext_percent / 100.0f;
        model->parameters.preview_gain =
            (float)config->sister_audition_percent / 100.0f;
        model->parameters.fx_return_gain =
            (float)config->sister_fx_return_percent / 100.0f;
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
        model->parameters.fx.fallout.transition =
            ts_sister_fallout_transition_normalized(
                (float)config->sister_fallout_transition_ms);
        model->parameters.fx.transition =
            ts_sister_fx_transition_normalized(
                (float)config->sister_fx_effect_transition_ms);
        model->parameters.fx.master_transition =
            ts_sister_fx_transition_normalized(
                (float)config->sister_fx_transition_ms);
        model->parameters.fx.fallout.component_transition =
            ts_sister_fallout_transition_normalized(
                (float)config->sister_fallout_component_transition_ms);
        model->parameters.fx.fallout.master_transition =
            ts_sister_fallout_transition_normalized(
                (float)config->sister_fallout_master_transition_ms);
        model->parameters.fx.fallout.rise_length =
            ts_sister_fallout_rise_normalized(
                (float)config->sister_fallout_rise_seconds);
    }
    snprintf(model->status, sizeof(model->status),
             "CLICK POWER TO ENABLE - WINDOW CLOSE HIDES ONLY");
}

void ts_sister_ui_set_capture_channels(TsSisterUiModel *model,
                                       TsConfig *config, int channels)
{
    int shared_channels = channels == 2 ? 2 : 1;
    if (model != NULL) model->capture_channels = shared_channels;
    if (config != NULL) {
        config->capture_channels = shared_channels;
        /* Retain the old key as a synchronized compatibility alias. */
        config->sister_capture_channels = shared_channels;
    }
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
    if (model != NULL && model->fallout_lfo_open) {
        static const uint32_t targets[13] = {
            TS_SISTER_FALLOUT_LFO_MIX, TS_SISTER_FALLOUT_LFO_FEEDBACK,
            TS_SISTER_FALLOUT_LFO_NOISE, TS_SISTER_FALLOUT_LFO_DROP_RATE,
            TS_SISTER_FALLOUT_LFO_PAN_RATE, TS_SISTER_FALLOUT_LFO_SKIP_SPAN,
            TS_SISTER_FALLOUT_LFO_SKIP_RATE,
            TS_SISTER_FALLOUT_LFO_BIT_QUALITY,
            TS_SISTER_FALLOUT_LFO_BIT_RESOLUTION,
            TS_SISTER_FALLOUT_LFO_BIT_RATE, TS_SISTER_FALLOUT_LFO_PITCH,
            TS_SISTER_FALLOUT_LFO_PITCH_RAMP,
            TS_SISTER_FALLOUT_LFO_PITCH_RATE
        };
        if (contains(x, y, 474, 72, 76, 22)) {
            hit.action = TS_SISTER_UI_ACTION_FALLOUT_LFO_DIALOG;
            return hit;
        }
        if (contains(x, y, 300, 72, 72, 22) ||
            contains(x, y, 378, 72, 88, 22)) {
            hit.action = TS_SISTER_UI_ACTION_FALLOUT_RISE_MODE;
            hit.index = contains(x, y, 300, 72, 72, 22) ?
                TS_SISTER_FALLOUT_RISE_SAW :
                TS_SISTER_FALLOUT_RISE_ONE_SHOT;
            return hit;
        }
        for (int target = 0; target < 13; ++target) {
            int column = target / 7;
            int row = target % 7;
            int base_x = 90 + column * 240;
            if (contains(x, y, base_x + 144, 112 + row * 25, 34, 22)) {
                hit.action = TS_SISTER_UI_ACTION_FALLOUT_LFO_TARGET;
                hit.index = (int)targets[target];
                return hit;
            }
            if (contains(x, y, base_x + 184, 112 + row * 25, 34, 22)) {
                hit.action = TS_SISTER_UI_ACTION_FALLOUT_RISE_TARGET;
                hit.index = (int)targets[target];
                return hit;
            }
        }
        return hit;
    }
    if (model != NULL && model->preset_manage_open) {
        if (contains(x, y, 180, 160, 24, 18))
            hit.action = TS_SISTER_UI_ACTION_PRESET_PREVIOUS;
        else if (contains(x, y, 436, 160, 24, 18))
            hit.action = TS_SISTER_UI_ACTION_PRESET_NEXT;
        else if (contains(x, y, 180, 200, 128, 22))
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
    if (contains(x, y, 494, 8, 30, 22)) {
        hit.action = TS_SISTER_UI_ACTION_LIMITER_TOGGLE;
        return hit;
    }
    if (contains(x, y, 528, 8, 48, 22)) {
        hit.action = TS_SISTER_UI_ACTION_MASTER_OUTPUT;
        hit.index = TS_SISTER_UI_PARAM_MASTER_OUTPUT;
        hit.normalized = (float)(x - 528) / 47.0f;
        return hit;
    }
    if (model != NULL && model->fx_page == 0 &&
        contains(x, y, 600, 144, 24, 18)) {
        hit.action = TS_SISTER_UI_ACTION_WAVE_MODE;
        return hit;
    }
    if (contains(x, y, 350, 8, 86, 22)) {
        hit.action = TS_SISTER_UI_ACTION_PARAMETER;
        hit.index = TS_SISTER_UI_PARAM_BUFFER_SECONDS;
        hit.normalized = (float)(x - 350) / 85.0f;
        return hit;
    }
    if (contains(x, y, 440, 8, 50, 22)) {
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
    if (model != NULL && model->fx_page == 2) {
        static const int toggle[7][5] = {
            {TS_SISTER_UI_FALLOUT_POWER, 16, 50, 86, 22},
            {TS_SISTER_UI_FALLOUT_NOISE_TYPE, 16, 84, 76, 18},
            {TS_SISTER_UI_FALLOUT_DROP, 16, 116, 76, 18},
            {TS_SISTER_UI_FALLOUT_PAN, 16, 150, 76, 18},
            {TS_SISTER_UI_FALLOUT_SKIP, 16, 184, 76, 18},
            {TS_SISTER_UI_FALLOUT_BIT, 16, 218, 76, 18},
            {TS_SISTER_UI_FALLOUT_PITCH, 16, 252, 76, 18}
        };
        static const int slider[][5] = {
            {TS_SISTER_UI_PARAM_FALLOUT_MIX, 120, 52, 165, 18},
            {TS_SISTER_UI_PARAM_FALLOUT_FEEDBACK, 300, 52, 220, 18},
            {TS_SISTER_UI_PARAM_FALLOUT_NOISE, 120, 84, 400, 18},
            {TS_SISTER_UI_PARAM_FALLOUT_DROP_RATE, 120, 116, 400, 18},
            {TS_SISTER_UI_PARAM_FALLOUT_PAN_RATE, 120, 150, 400, 18},
            {TS_SISTER_UI_PARAM_FALLOUT_SKIP_SPAN, 120, 184, 190, 18},
            {TS_SISTER_UI_PARAM_FALLOUT_SKIP_RATE, 330, 184, 190, 18},
            {TS_SISTER_UI_PARAM_FALLOUT_BIT_QUALITY, 120, 218, 125, 18},
            {TS_SISTER_UI_PARAM_FALLOUT_BIT_RESOLUTION, 260, 218, 125, 18},
            {TS_SISTER_UI_PARAM_FALLOUT_BIT_RATE, 400, 218, 120, 18},
            {TS_SISTER_UI_PARAM_FALLOUT_PITCH, 120, 252, 125, 18},
            {TS_SISTER_UI_PARAM_FALLOUT_PITCH_RAMP, 260, 252, 125, 18},
            {TS_SISTER_UI_PARAM_FALLOUT_PITCH_RATE, 400, 252, 120, 18},
            {TS_SISTER_UI_PARAM_FALLOUT_TRANSITION, 120, 284, 125, 18},
            {TS_SISTER_UI_PARAM_FALLOUT_COMPONENT_TRANSITION, 260, 284, 125, 18},
            {TS_SISTER_UI_PARAM_FALLOUT_MASTER_TRANSITION, 400, 284, 120, 18}
        };
        if (contains(x, y, 10, 316, 86, 22)) {
            hit.action = TS_SISTER_UI_ACTION_FALLOUT_RISE_RETRIGGER;
            return hit;
        }
        for (int i = 0; i < 7; ++i) {
            if (contains(x, y, toggle[i][1], toggle[i][2],
                         toggle[i][3], toggle[i][4])) {
                hit.action = TS_SISTER_UI_ACTION_FALLOUT_TOGGLE;
                hit.index = toggle[i][0];
                return hit;
            }
        }
        for (size_t i = 0u; i < sizeof(slider) / sizeof(slider[0]); ++i) {
            if (contains(x, y, slider[i][1], slider[i][2],
                         slider[i][3], slider[i][4])) {
                hit.action = TS_SISTER_UI_ACTION_PARAMETER;
                hit.index = slider[i][0];
                hit.normalized = (float)(x - slider[i][1]) /
                                 (float)(slider[i][3] - 1);
                return hit;
            }
        }
        if (contains(x, y, 548, 55, 74, 22)) {
            hit.action = TS_SISTER_UI_ACTION_FALLOUT_LFO_DIALOG;
            return hit;
        }
        if (contains(x, y, 546, 101, 20, 127)) {
            hit.action = TS_SISTER_UI_ACTION_PARAMETER;
            hit.index = TS_SISTER_UI_PARAM_FALLOUT_LFO_RATE;
            hit.normalized = 1.0f - (float)(y - 101) / 126.0f;
            return hit;
        }
        if (contains(x, y, 567, 101, 20, 127)) {
            hit.action = TS_SISTER_UI_ACTION_PARAMETER;
            hit.index = TS_SISTER_UI_PARAM_FALLOUT_LFO_INTENSITY;
            hit.normalized = 1.0f - (float)(y - 101) / 126.0f;
            return hit;
        }
        if (contains(x, y, 588, 101, 20, 127)) {
            hit.action = TS_SISTER_UI_ACTION_PARAMETER;
            hit.index = TS_SISTER_UI_PARAM_FALLOUT_RISE_LENGTH;
            hit.normalized = 1.0f - (float)(y - 101) / 126.0f;
            return hit;
        }
        if (contains(x, y, 609, 101, 20, 127)) {
            hit.action = TS_SISTER_UI_ACTION_PARAMETER;
            hit.index = TS_SISTER_UI_PARAM_FALLOUT_RISE_INTENSITY;
            hit.normalized = 1.0f - (float)(y - 101) / 126.0f;
            return hit;
        }
        if (contains(x, y, 10, 370, 58, 22)) hit.action = TS_SISTER_UI_ACTION_TAP;
        else if (contains(x, y, 74, 370, 44, 22)) hit.action = TS_SISTER_UI_ACTION_CAPTURE_FORMAT;
        else if (contains(x, y, 124, 370, 100, 22)) hit.action = TS_SISTER_UI_ACTION_DESTINATION;
        else if (contains(x, y, 450, 370, 82, 22)) hit.action = TS_SISTER_UI_ACTION_CAPTURE;
        else if (contains(x, y, 538, 370, 92, 22)) hit.action = TS_SISTER_UI_ACTION_OVERDUB;
        return hit;
    }
    if (model != NULL && model->fx_page == 1) {
        for (int row = 0; row < 4; ++row) {
            int top = 52 + row * 56;
            int field_count = model->parameters.fx.slot[row].type ==
                              TS_SISTER_FX_GRAIN ? 5 : 4;
            int field_width = field_count == 5 ? 70 : 88;
            int field_step = field_count == 5 ? 74 : 92;
            if (contains(x, y, 16, top - 2, 40, 22)) {
                hit.action = TS_SISTER_UI_ACTION_FX_SLOT_TOGGLE;
                hit.index = row;
                return hit;
            }
            if (contains(x, y, 60, top - 2, 90, 22)) {
                hit.action = TS_SISTER_UI_ACTION_FX_SLOT_TYPE;
                hit.index = row;
                return hit;
            }
            if (model->parameters.fx.slot[row].type != TS_SISTER_FX_EMPTY) {
                for (int field = 0; field < field_count; ++field) {
                    int left = 156 + field * field_step;
                    if (contains(x, y, left, top, field_width, 18)) {
                        hit.action = TS_SISTER_UI_ACTION_PARAMETER;
                        hit.index = TS_SISTER_UI_SLOT_PARAMETER(
                            row, field_count == 4 && field == 3 ? 4 : field);
                        hit.normalized = (float)(x - left) /
                                         (float)(field_width - 1);
                        return hit;
                    }
                }
            }
            for (int placement = 0; placement < 5; ++placement) {
                if (contains(x, y, 60 + placement * 52,
                             top + 22, 46, 18)) {
                    hit.action = TS_SISTER_UI_ACTION_FX_SLOT_PLACEMENT;
                    hit.index = (row << 8) | (1 << placement);
                    return hit;
                }
            }
            if (row > 0 && contains(x, y, 542, top, 36, 18)) {
                hit.action = TS_SISTER_UI_ACTION_FX_SLOT_MOVE;
                hit.index = row << 8;
                return hit;
            }
            if (row < 3 && contains(x, y, 584, top, 36, 18)) {
                hit.action = TS_SISTER_UI_ACTION_FX_SLOT_MOVE;
                hit.index = (row << 8) | 1;
                return hit;
            }
        }
        if (contains(x, y, 10, 304, 92, 22)) {
            hit.action = TS_SISTER_UI_ACTION_FX_TOGGLE;
            hit.index = TS_SISTER_UI_FX_MASTER;
            return hit;
        }
        if (contains(x, y, 110, 276, 300, 18)) {
            hit.action = TS_SISTER_UI_ACTION_PARAMETER;
            hit.index = TS_SISTER_UI_PARAM_FX_TRANSITION;
            hit.normalized = (float)(x - 110) / 299.0f;
            return hit;
        }
        if (contains(x, y, 110, 306, 300, 18)) {
            hit.action = TS_SISTER_UI_ACTION_PARAMETER;
            hit.index = TS_SISTER_UI_PARAM_MASTER_FX_TRANSITION;
            hit.normalized = (float)(x - 110) / 299.0f;
            return hit;
        }
        if (contains(x, y, 110, 332, 300, 18)) {
            hit.action = TS_SISTER_UI_ACTION_PARAMETER;
            hit.index = TS_SISTER_UI_PARAM_MASTER_FX_FEEDBACK;
            hit.normalized = (float)(x - 110) / 299.0f;
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
    for (int control = 0; control < 5; ++control) {
        static const int parameters[5] = {
            TS_SISTER_UI_PARAM_TILES_GAIN,
            TS_SISTER_UI_PARAM_FM_GAIN,
            TS_SISTER_UI_PARAM_EXT_GAIN,
            TS_SISTER_UI_PARAM_PREVIEW_GAIN,
            TS_SISTER_UI_PARAM_FX_RETURN_GAIN
        };
        int left = 550 + control * 15;
        if (contains(x, y, left, 194, 14, 84)) {
            hit.action = TS_SISTER_UI_ACTION_PARAMETER;
            hit.index = parameters[control];
            hit.normalized = 1.0f - (float)(y - 194) / 83.0f;
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
