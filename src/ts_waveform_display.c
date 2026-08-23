#include "tapesister/waveform_display.h"

#include <string.h>

TsWaveformDisplayMode ts_waveform_display_sanitize(int mode)
{
    return mode >= 0 && mode < TS_WAVEFORM_DISPLAY_COUNT ?
           (TsWaveformDisplayMode)mode : TS_WAVEFORM_DISPLAY_STEREO;
}

TsWaveformDisplayMode ts_waveform_display_cycle(TsWaveformDisplayMode mode,
                                                int amount)
{
    int value = ((int)ts_waveform_display_sanitize(mode) + amount) %
                TS_WAVEFORM_DISPLAY_COUNT;
    if (value < 0) value += TS_WAVEFORM_DISPLAY_COUNT;
    return (TsWaveformDisplayMode)value;
}

const char *ts_waveform_display_name(TsWaveformDisplayMode mode)
{
    static const char *const names[TS_WAVEFORM_DISPLAY_COUNT] = {
        "STEREO", "LEFT", "RIGHT", "MONO SUM"
    };
    return names[ts_waveform_display_sanitize(mode)];
}

static void ordered(float *minimum, float *maximum)
{
    if (*minimum > *maximum) {
        float swap = *minimum;
        *minimum = *maximum;
        *maximum = swap;
    }
}

TsWaveformDisplayColumn ts_waveform_display_column(
    const TsWaveformColumn *column, uint8_t sample_channels,
    TsWaveformDisplayMode mode)
{
    TsWaveformDisplayColumn result;
    float minimum;
    float maximum;
    memset(&result, 0, sizeof(result));
    if (column == NULL) return result;
    mode = ts_waveform_display_sanitize(mode);
    if (sample_channels < 2u) {
        result.left_minimum = result.right_minimum = column->minimum;
        result.left_maximum = result.right_maximum = column->maximum;
        return result;
    }
    if (mode == TS_WAVEFORM_DISPLAY_STEREO) {
        result.left_minimum = column->left_minimum;
        result.left_maximum = column->left_maximum;
        result.right_minimum = column->right_minimum;
        result.right_maximum = column->right_maximum;
        result.stereo = 1;
        return result;
    }
    if (mode == TS_WAVEFORM_DISPLAY_LEFT) {
        minimum = column->left_minimum;
        maximum = column->left_maximum;
    } else if (mode == TS_WAVEFORM_DISPLAY_RIGHT) {
        minimum = column->right_minimum;
        maximum = column->right_maximum;
    } else {
        minimum = 0.5f * (column->left_minimum + column->right_minimum);
        maximum = 0.5f * (column->left_maximum + column->right_maximum);
        ordered(&minimum, &maximum);
    }
    result.left_minimum = result.right_minimum = minimum;
    result.left_maximum = result.right_maximum = maximum;
    return result;
}

TsStereoFrame ts_waveform_display_frame(TsStereoFrame frame,
                                        uint8_t sample_channels,
                                        TsWaveformDisplayMode mode)
{
    float mono;
    mode = ts_waveform_display_sanitize(mode);
    if (sample_channels < 2u || mode == TS_WAVEFORM_DISPLAY_STEREO)
        return frame;
    mono = mode == TS_WAVEFORM_DISPLAY_LEFT ? frame.l :
           mode == TS_WAVEFORM_DISPLAY_RIGHT ? frame.r :
           0.5f * (frame.l + frame.r);
    return (TsStereoFrame){mono, mono};
}
