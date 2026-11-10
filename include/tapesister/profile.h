#ifndef TAPESISTER_PROFILE_H
#define TAPESISTER_PROFILE_H
#include <stddef.h>
#include <stdint.h>
#include "tapesister/source_route_types.h"

/* One writer per lane: main/UI and output callback. Nested timings are
   inclusive. Each audio child samples one complete block in 32, staggered so
   one callback never instruments every per-sample stage/effect together.
   Totals/call rates are estimates; peaks are observed samples, never deadlines. */
typedef enum {
    TS_PROF_UI, TS_PROF_CONTROL, TS_PROF_EVENTS, TS_PROF_TRACKER_UI,
    TS_PROF_RENDER, TS_PROF_WAVEFORM, TS_PROF_SISTER_UI, TS_PROF_PRISM_UI,
    TS_PROF_AUX_UI, TS_PROF_UPLOAD, TS_PROF_PRESENT, TS_PROF_WAIT, TS_PROF_EQ_UI, TS_PROF_SCOPES, TS_PROF_WAVE_CACHE,
    TS_PROF_AUDIO, TS_PROF_VOICES, TS_PROF_TRACKER, TS_PROF_SISTER,
    TS_PROF_ROUTER, TS_PROF_PRISM, TS_PROF_FALLOUT, TS_PROF_PEDALBOARD,
    TS_PROF_INSERT, TS_PROF_MASTER, TS_PROF_SPATIAL, TS_PROF_SNAPSHOT, TS_PROF_SISTER_STAGE,
    TS_PROF_MATRIX_FX,
    TS_PROF_COUNT
} TsProfileId;
typedef struct { uint64_t calls, ticks, peak; } TsProfileValue;
/* Immutable engine identity, prepared off the audio thread. A generation is
   new even when a project/device reload reuses the same saved effect ID. */
typedef struct {
    uint64_t generation, patch;
    uint32_t id, type, quantum;
} TsProfileEffectIdentity;
typedef struct {
    TsProfileEffectIdentity identity;
    TsProfileValue blocks; /* Estimated processed blocks/ticks; observed block peak. */
    uint64_t samples, transitions, last;
} TsProfileEffectValue;
typedef struct {
    TsProfileValue value[TS_PROF_COUNT];
    TsProfileEffectValue effect[TS_MATRIX_EFFECT_CAPACITY];
    uint64_t now;
    double seconds, frequency;
    int enabled;
} TsProfileSnapshot;
/* Initialize before opening audio. Disabled if publication is not lock free. */
int ts_profile_init(uint64_t (*clock)(void), uint64_t frequency);
void ts_profile_enable(int enabled);
int ts_profile_enabled(void);
void ts_profile_reset(void); /* Epoch request: neither lane is locked. */
void ts_profile_lane_begin(int audio);
void ts_profile_lane_end(int audio);
extern int ts_profile_ui_active, ts_profile_audio_active;
extern unsigned ts_profile_audio_phase;
uint64_t ts_profile_clock(void);
void ts_profile_record(TsProfileId id, uint64_t start);
/* Reuses the aggregate Matrix FX clock pair; sums all frames of a sampled
   callback before retaining its per-effect peak. No strings or allocation. */
void ts_profile_effect_end(int slot, const TsProfileEffectIdentity *identity,
                           uint64_t start, int transitioning);
/* Merge worker timing on the audio thread; workers never touch profile lanes. */
void ts_profile_effect_duration(int slot,const TsProfileEffectIdentity *identity,
    uint64_t ticks,uint64_t ended,int transitioning);
/* UI thread only, including SunVox catalog/name lookup. */
void ts_profile_effect_label(const TsProfileEffectIdentity *identity,
                             char *text, size_t capacity);
static inline uint64_t ts_profile_begin(TsProfileId id)
{
    return (id < TS_PROF_AUDIO ? ts_profile_ui_active :
        (id == TS_PROF_AUDIO ? ts_profile_enabled() :
            ts_profile_audio_phase == (unsigned)(id - TS_PROF_VOICES))) ?
        ts_profile_clock() : 0;
}
/* Slots have distinct sampling phases, separate from the stage phases. Keep
   complete callbacks: sampling individual frames misses buffered SunVox work. */
static inline uint64_t ts_profile_effect_begin(int slot)
{
    return slot >= 0 && slot < TS_MATRIX_EFFECT_CAPACITY &&
        ts_profile_audio_phase == 16u + (unsigned)slot ? ts_profile_clock() : 0;
}
static inline void ts_profile_end(TsProfileId id, uint64_t start)
{ if (start) ts_profile_record(id, start); }
void ts_profile_snapshot(TsProfileSnapshot *snapshot);
const char *ts_profile_name(TsProfileId id);
void ts_profile_report(char *text, size_t capacity);
#endif
