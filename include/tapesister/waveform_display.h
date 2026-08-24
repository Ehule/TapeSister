#ifndef TAPESISTER_WAVEFORM_DISPLAY_H
#define TAPESISTER_WAVEFORM_DISPLAY_H

#include "tapesister/sample.h"
#include "tapesister/waveform_cache.h"

typedef enum {
    TS_WAVEFORM_DISPLAY_STEREO = 0,
    TS_WAVEFORM_DISPLAY_LEFT,
    TS_WAVEFORM_DISPLAY_RIGHT,
    TS_WAVEFORM_DISPLAY_MONO_SUM,
    TS_WAVEFORM_DISPLAY_COUNT
} TsWaveformDisplayMode;

typedef struct {
    float left_minimum, left_maximum;
    float right_minimum, right_maximum;
    int stereo;
} TsWaveformDisplayColumn;

TsWaveformDisplayMode ts_waveform_display_sanitize(int mode);
TsWaveformDisplayMode ts_waveform_display_cycle(TsWaveformDisplayMode mode,
                                                int amount);
const char *ts_waveform_display_name(TsWaveformDisplayMode mode);
TsWaveformDisplayColumn ts_waveform_display_column(
    const TsWaveformColumn *column, uint8_t sample_channels,
    TsWaveformDisplayMode mode);
TsStereoFrame ts_waveform_display_frame(TsStereoFrame frame,
                                        uint8_t sample_channels,
                                        TsWaveformDisplayMode mode);

#endif
