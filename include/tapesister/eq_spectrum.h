#ifndef TAPESISTER_EQ_SPECTRUM_H
#define TAPESISTER_EQ_SPECTRUM_H
#include <stdatomic.h>
#include "tapesister/sample.h"

#define TS_EQ_SPECTRUM_FRAMES 4096
#define TS_EQ_SPECTRUM_POINTS 184
#define TS_EQ_SPECTRUM_FLOOR_DB (-96.0f)

/* One audio producer / one UI consumer. A full capture remains immutable until
   the UI releases it; a slow/hidden UI can never stall or overwrite audio. */
typedef struct {
    atomic_uint request, ready;
    unsigned producer_request, fill, rate;
    TsStereoFrame samples[TS_EQ_SPECTRUM_FRAMES];
} TsEqSpectrum;
typedef struct {
    float db[TS_EQ_SPECTRUM_POINTS];
    unsigned rate;
    int valid;
    float idle_seconds;
} TsEqSpectrumView;

void ts_eq_spectrum_init(TsEqSpectrum *s);
/* UI thread only. Toggling capture invalidates partial/queued old captures. */
void ts_eq_spectrum_enable(TsEqSpectrum *s, int enabled);
void ts_eq_spectrum_clear(TsEqSpectrumView *view, unsigned rate);
/* Audio thread: bounded sample copy only; no FFT, allocation, locks or waits. */
void ts_eq_spectrum_push(TsEqSpectrum *s, TsStereoFrame frame, unsigned rate);
/* UI thread: at most one stereo Hann-windowed FFT, normally called at 10 Hz. */
int ts_eq_spectrum_poll(TsEqSpectrum *s, TsEqSpectrumView *view,
                        unsigned rate, float elapsed_seconds);
#endif
