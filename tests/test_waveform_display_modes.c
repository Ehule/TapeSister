#include "tapesister/waveform_display.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %d: %s\n", __LINE__, #c); ++failures; } } while (0)
#define CLOSE(a,b) (fabsf((a) - (b)) < 0.00001f)

int main(void)
{
    TsWaveformColumn source;
    TsWaveformDisplayColumn display;
    TsStereoFrame frame = {0.8f, -0.2f};
    memset(&source, 0, sizeof(source));
    source.minimum = -0.8f;
    source.maximum = 0.9f;
    source.left_minimum = -0.5f;
    source.left_maximum = 0.9f;
    source.right_minimum = -0.8f;
    source.right_maximum = 0.3f;
    display = ts_waveform_display_column(&source, 2u, TS_WAVEFORM_DISPLAY_STEREO);
    CHECK(display.stereo && CLOSE(display.left_maximum, 0.9f));
    CHECK(CLOSE(display.right_minimum, -0.8f));
    display = ts_waveform_display_column(&source, 2u, TS_WAVEFORM_DISPLAY_LEFT);
    CHECK(!display.stereo && CLOSE(display.left_minimum, -0.5f));
    display = ts_waveform_display_column(&source, 2u, TS_WAVEFORM_DISPLAY_RIGHT);
    CHECK(CLOSE(display.left_maximum, 0.3f));
    display = ts_waveform_display_column(&source, 2u, TS_WAVEFORM_DISPLAY_MONO_SUM);
    CHECK(CLOSE(display.left_minimum, -0.65f));
    CHECK(CLOSE(display.left_maximum, 0.6f));
    display = ts_waveform_display_column(&source, 1u, TS_WAVEFORM_DISPLAY_RIGHT);
    CHECK(!display.stereo && CLOSE(display.left_minimum, -0.8f));
    frame = ts_waveform_display_frame(frame, 2u, TS_WAVEFORM_DISPLAY_MONO_SUM);
    CHECK(CLOSE(frame.l, 0.3f) && CLOSE(frame.r, 0.3f));
    CHECK(ts_waveform_display_cycle(TS_WAVEFORM_DISPLAY_MONO_SUM, 1) ==
          TS_WAVEFORM_DISPLAY_STEREO);
    CHECK(strcmp(ts_waveform_display_name(TS_WAVEFORM_DISPLAY_RIGHT), "RIGHT") == 0);
    CHECK(strcmp(ts_waveform_display_letter(TS_WAVEFORM_DISPLAY_STEREO), "S") == 0);
    CHECK(strcmp(ts_waveform_display_letter(TS_WAVEFORM_DISPLAY_LEFT), "L") == 0);
    CHECK(strcmp(ts_waveform_display_letter(TS_WAVEFORM_DISPLAY_RIGHT), "R") == 0);
    CHECK(strcmp(ts_waveform_display_letter(TS_WAVEFORM_DISPLAY_MONO_SUM), "M") == 0);
    puts("waveform display mode tests passed");
    return failures != 0;
}
