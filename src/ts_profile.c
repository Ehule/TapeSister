#include "tapesister/profile.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    TsProfileValue value[TS_PROF_COUNT];
    unsigned epoch, blocks;
    atomic_uint revision, published_epoch;
    atomic_uint_least64_t calls[TS_PROF_COUNT], ticks[TS_PROF_COUNT], peak[TS_PROF_COUNT];
} ProfileLane;
static ProfileLane lanes[2];
static atomic_int enabled;
static atomic_uint epoch;
static uint64_t (*read_clock)(void), frequency, started, stopped;
static int available;
int ts_profile_ui_active, ts_profile_audio_active;

int ts_profile_init(uint64_t (*clock)(void), uint64_t hz)
{
    read_clock = clock; frequency = hz;
    available = clock && hz && atomic_is_lock_free(&enabled) &&
        atomic_is_lock_free(&epoch) && atomic_is_lock_free(&lanes[0].revision) &&
        atomic_is_lock_free(&lanes[0].calls[0]);
    ts_profile_reset(); return available;
}
uint64_t ts_profile_clock(void) { return read_clock ? read_clock() : 0; }
int ts_profile_enabled(void) { return atomic_load_explicit(&enabled, memory_order_relaxed); }
void ts_profile_reset(void)
{
    started = ts_profile_clock(); stopped=started;
    atomic_fetch_add_explicit(&epoch, 1u, memory_order_release);
}
void ts_profile_enable(int on)
{
    if (on && !ts_profile_enabled()) ts_profile_reset();
    if (!on && ts_profile_enabled()) stopped=ts_profile_clock();
    atomic_store_explicit(&enabled, on && available, memory_order_relaxed);
}
void ts_profile_lane_begin(int audio)
{
    ProfileLane *p = &lanes[audio != 0];
    unsigned next = atomic_load_explicit(&epoch, memory_order_acquire);
    if (next != p->epoch) {
        memset(p->value, 0, sizeof(p->value)); p->epoch = next; p->blocks = 0;
    }
    int active = ts_profile_enabled();
    if (audio) ts_profile_audio_active = active && (p->blocks++ % 32u == 0u);
    else ts_profile_ui_active = active;
}
void ts_profile_record(TsProfileId id, uint64_t start)
{
    uint64_t end = ts_profile_clock();
    if (end < start) return;
    TsProfileValue *v = &lanes[id >= TS_PROF_AUDIO].value[id];
    uint64_t scale = id > TS_PROF_AUDIO ? 32u : 1u, elapsed = end - start;
    v->calls += scale; v->ticks += elapsed * scale;
    if (elapsed > v->peak) v->peak = elapsed;
}
void ts_profile_lane_end(int audio)
{
    if (!ts_profile_enabled()) return;
    ProfileLane *p = &lanes[audio != 0];
    unsigned rev = atomic_load_explicit(&p->revision, memory_order_relaxed);
    atomic_store_explicit(&p->revision, rev + 1u, memory_order_seq_cst);
    for (int i = audio ? TS_PROF_AUDIO : 0; i < (audio ? TS_PROF_COUNT : TS_PROF_AUDIO); ++i) {
        atomic_store_explicit(&p->calls[i], p->value[i].calls, memory_order_relaxed);
        atomic_store_explicit(&p->ticks[i], p->value[i].ticks, memory_order_relaxed);
        atomic_store_explicit(&p->peak[i], p->value[i].peak, memory_order_relaxed);
    }
    atomic_store_explicit(&p->published_epoch, p->epoch, memory_order_relaxed);
    atomic_store_explicit(&p->revision, rev + 2u, memory_order_release);
}
void ts_profile_snapshot(TsProfileSnapshot *s)
{
    memset(s, 0, sizeof(*s)); s->enabled = ts_profile_enabled();
    s->frequency = (double)frequency;
    uint64_t now = s->enabled ? ts_profile_clock() : stopped;
    s->seconds = frequency && now >= started ? (double)(now-started)/frequency : 0;
    for (int lane = 0; lane < 2; ++lane) {
        ProfileLane *p = &lanes[lane];
        for (int attempt = 0; attempt < 3; ++attempt) {
            unsigned rev = atomic_load_explicit(&p->revision, memory_order_acquire);
            if (rev & 1u) continue;
            for (int i = lane ? TS_PROF_AUDIO : 0; i < (lane ? TS_PROF_COUNT : TS_PROF_AUDIO); ++i) {
                s->value[i].calls = atomic_load_explicit(&p->calls[i], memory_order_relaxed);
                s->value[i].ticks = atomic_load_explicit(&p->ticks[i], memory_order_relaxed);
                s->value[i].peak = atomic_load_explicit(&p->peak[i], memory_order_relaxed);
            }
            atomic_thread_fence(memory_order_acquire);
            if (rev == atomic_load_explicit(&p->revision, memory_order_relaxed) &&
                atomic_load_explicit(&p->published_epoch,memory_order_relaxed) ==
                atomic_load_explicit(&epoch,memory_order_relaxed)) break;
            for (int i = lane ? TS_PROF_AUDIO : 0; i < (lane ? TS_PROF_COUNT : TS_PROF_AUDIO); ++i)
                s->value[i] = (TsProfileValue){0};
        }
    }
}
const char *ts_profile_name(TsProfileId id)
{
    static const char *names[] = {"UI work", "Controller update", "Events / MIDI", "Tracker UI",
        "Main paint", "Waveforms", "Sister paint", "Prism visualizer", "Aux windows",
        "Texture / damage / copy", "Present / swap", "Event wait", "EQ response drawing", "Tracker scopes", "Waveform analysis/cache",
        "Audio callback", "Voices / ARP / Mosaic", "Tracker DSP", "Sister runtime",
        "Router (incl stages)", "Prism DSP", "Fallout DSP", "Pedalboard DSP",
        "Insert DSP", "Master EQ / limiter", "Spatial output", "Audio snapshots", "Sister tape stage"};
    return id < TS_PROF_COUNT ? names[id] : "Unknown";
}
void ts_profile_report(char *text, size_t capacity)
{
    TsProfileSnapshot s; ts_profile_snapshot(&s);
    if (!capacity) return;
    size_t used = (size_t)snprintf(text, capacity,
        "Performance profile: %s; %.1f s\n"
        "Inclusive wall timings; nested rows overlap. Present includes driver/VSync wait.\n"
        "Audio children sample 1/32 blocks: estimated totals/rates, observed peaks.\n"
        "Subsystem | avg ms | peak ms | calls/s | ms/s | %% parent\n",
        s.enabled ? "ON" : "OFF (enable PROFILE in Audio Health)", s.seconds);
    for (int i = 0; i < TS_PROF_COUNT && used < capacity; ++i) {
        const TsProfileValue *v = &s.value[i];
        double ms = s.frequency ? 1000.0 * v->ticks / s.frequency : 0;
        uint64_t parent = s.value[i >= TS_PROF_AUDIO ? TS_PROF_AUDIO : TS_PROF_UI].ticks;
        int n = snprintf(text + used, capacity - used, "%s | %.6f | %.4f | %.1f | %.3f | %.1f\n",
            ts_profile_name((TsProfileId)i), v->calls ? ms/v->calls : 0,
            s.frequency ? 1000.0*v->peak/s.frequency : 0,
            s.seconds ? v->calls/s.seconds : 0, s.seconds ? ms/s.seconds : 0,
            i == TS_PROF_WAIT ? 0 : parent ? 100.0*v->ticks/parent : 0);
        if (n < 0) break;
        used += (size_t)n;
    }
}
