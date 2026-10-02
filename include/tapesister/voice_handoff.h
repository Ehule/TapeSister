#ifndef TAPESISTER_VOICE_HANDOFF_H
#define TAPESISTER_VOICE_HANDOFF_H

#include "tapesister/sample.h"

/* A short residual fade for voice removal/replacement and normalization
   changes. No sample pointers survive a release, and no new voice is delayed.
   Only the discontinuity is faded; continuing voices keep their waveform. */
typedef struct {
    TsStereoFrame last, residual;
    uint32_t remaining;
} TsVoiceHandoff;

static inline TsStereoFrame ts_voice_handoff_process(TsVoiceHandoff *h,
    TsStereoFrame input, int changed, uint32_t frames)
{
    input = ts_stereo_frame_sanitize(input);
    /* A cold/silent bank retains the configured note attack, including the
       explicit zero-ms option used for sample-accurate capture. */
    if (changed && frames && (h->remaining || h->last.l != 0.0f || h->last.r != 0.0f)) {
        h->residual = (TsStereoFrame){h->last.l - input.l, h->last.r - input.r};
        h->remaining = frames;
    }
    if (h->remaining && frames) {
        float amount = (float)h->remaining / frames;
        if (amount > 1.0f) amount = 1.0f;
        input.l += h->residual.l * amount;
        input.r += h->residual.r * amount;
        --h->remaining;
    }
    h->last = ts_stereo_frame_sanitize(input);
    return h->last;
}

#endif
