#ifndef TAPESISTER_PROFILE_H
#define TAPESISTER_PROFILE_H
#include <stddef.h>
#include <stdint.h>

/* One writer per lane: main/UI and output callback. Nested timings are
   inclusive. Audio children sample one complete block in 32; totals and
   call rates are estimates, peaks are observed samples, never deadlines. */
typedef enum {
    TS_PROF_UI, TS_PROF_CONTROL, TS_PROF_EVENTS, TS_PROF_TRACKER_UI,
    TS_PROF_RENDER, TS_PROF_WAVEFORM, TS_PROF_SISTER_UI, TS_PROF_PRISM_UI,
    TS_PROF_AUX_UI, TS_PROF_UPLOAD, TS_PROF_PRESENT, TS_PROF_WAIT, TS_PROF_EQ_UI, TS_PROF_SCOPES, TS_PROF_WAVE_CACHE,
    TS_PROF_AUDIO, TS_PROF_VOICES, TS_PROF_TRACKER, TS_PROF_SISTER,
    TS_PROF_ROUTER, TS_PROF_PRISM, TS_PROF_FALLOUT, TS_PROF_PEDALBOARD,
    TS_PROF_INSERT, TS_PROF_MASTER, TS_PROF_SPATIAL, TS_PROF_SNAPSHOT, TS_PROF_SISTER_STAGE,
    TS_PROF_COUNT
} TsProfileId;
typedef struct { uint64_t calls, ticks, peak; } TsProfileValue;
typedef struct {
    TsProfileValue value[TS_PROF_COUNT];
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
uint64_t ts_profile_clock(void);
void ts_profile_record(TsProfileId id, uint64_t start);
static inline uint64_t ts_profile_begin(TsProfileId id)
{
    return (id < TS_PROF_AUDIO ? ts_profile_ui_active :
        (id == TS_PROF_AUDIO ? ts_profile_enabled() : ts_profile_audio_active)) ?
        ts_profile_clock() : 0;
}
static inline void ts_profile_end(TsProfileId id, uint64_t start)
{ if (start) ts_profile_record(id, start); }
void ts_profile_snapshot(TsProfileSnapshot *snapshot);
const char *ts_profile_name(TsProfileId id);
void ts_profile_report(char *text, size_t capacity);
#endif
