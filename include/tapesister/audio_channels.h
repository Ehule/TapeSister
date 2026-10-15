#ifndef TAPESISTER_AUDIO_CHANNELS_H
#define TAPESISTER_AUDIO_CHANNELS_H

/* One-based physical channel numbers. Last == 0 follows the device (ALL).
   The callback stride keeps absolute channel numbers; ranges never renumber
   saved routes. Zero-initialized runtime views also mean all channels. */
typedef struct { int first, last; } TsAudioChannelRange;
static inline int ts_audio_range_valid(TsAudioChannelRange r)
{ return r.first >= 1 && r.first <= 255 && r.last >= 0 && r.last <= 255 && (!r.last || r.last >= r.first); }
static inline unsigned ts_audio_range_end(TsAudioChannelRange r, unsigned channels)
{ return r.last > 0 && (unsigned)r.last < channels ? (unsigned)r.last : channels; }
static inline int ts_audio_range_contains(TsAudioChannelRange r, unsigned ch, unsigned channels)
{ return ch >= (unsigned)(r.first > 0 ? r.first - 1 : 0) && ch < ts_audio_range_end(r, channels); }
static inline int ts_audio_range_fits(TsAudioChannelRange r, unsigned channels)
{ return ts_audio_range_valid(r) && (unsigned)r.first <= channels && (!r.last || (unsigned)r.last <= channels); }
static inline int ts_audio_range_equal(TsAudioChannelRange a, TsAudioChannelRange b)
{ return a.first == b.first && a.last == b.last; }
static inline void ts_audio_range_mask(TsAudioChannelRange r, float *samples, unsigned frames, unsigned channels)
{
    if (r.first <= 1 && (!r.last || (unsigned)r.last >= channels)) return;
    for (unsigned ch = 0; ch < channels; ++ch)
        if (!ts_audio_range_contains(r, ch, channels))
            for (unsigned f = 0; f < frames; ++f) samples[f * channels + ch] = 0;
}
#endif
