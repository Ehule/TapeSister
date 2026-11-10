#include "tapesister/profile.h"
#include "tapesister/matrix_effect_types.h"
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
typedef struct {
    atomic_uint_least64_t generation, patch, id, type, quantum;
    atomic_uint_least64_t calls, ticks, peak, samples, transitions, last;
} PublishedEffect;
static struct {
    TsProfileEffectValue value[TS_MATRIX_EFFECT_CAPACITY];
    uint64_t ticks[TS_MATRIX_EFFECT_CAPACITY];
    unsigned seen, transitioning, dirty;
    PublishedEffect published[TS_MATRIX_EFFECT_CAPACITY];
} effects; /* Audio writer; published under lanes[1].revision. */
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
        atomic_is_lock_free(&lanes[0].calls[0]) &&
        atomic_is_lock_free(&effects.published[0].generation);
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
        if (audio) { memset(effects.value, 0, sizeof(effects.value)); effects.dirty = 1; }
    }
    int active = ts_profile_enabled();
    if (audio) {
        /* One sample per group, rotating through every phase in 1024 blocks.
           A fixed phase can miss every expensive alternating render quantum. */
        unsigned block = p->blocks++;
        ts_profile_audio_active = active && ((block & 31u) == ((block >> 5) & 31u));
        effects.seen = effects.transitioning = 0;
        if (ts_profile_audio_active) memset(effects.ticks, 0, sizeof(effects.ticks));
    }
    else ts_profile_ui_active = active;
}
static void record_elapsed(TsProfileId id, uint64_t elapsed)
{
    TsProfileValue *v = &lanes[id >= TS_PROF_AUDIO].value[id];
    uint64_t scale = id > TS_PROF_AUDIO ? 32u : 1u;
    v->calls += scale; v->ticks += elapsed * scale;
    if (elapsed > v->peak) v->peak = elapsed;
}
void ts_profile_record(TsProfileId id, uint64_t start)
{
    uint64_t end = ts_profile_clock();
    if (end >= start) record_elapsed(id, end - start);
}
void ts_profile_effect_end(int slot, const TsProfileEffectIdentity *identity,
                           uint64_t start, int transitioning)
{
    if (!start || !ts_profile_audio_active || slot < 0 || slot >= TS_MATRIX_EFFECT_CAPACITY) return;
    uint64_t end = ts_profile_clock();
    if (end < start) return;
    record_elapsed(TS_PROF_MATRIX_FX, end - start);
    TsProfileEffectValue *v = &effects.value[slot];
    if (v->identity.generation != identity->generation) {
        memset(v, 0, sizeof(*v)); v->identity = *identity;
        effects.ticks[slot] = 0;
        effects.transitioning &= ~(1u << slot);
    }
    effects.seen |= 1u << slot;
    effects.dirty = 1;
    if (transitioning) effects.transitioning |= 1u << slot;
    effects.ticks[slot] += end - start; v->last = end;
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
    if (audio && effects.dirty) for (int i = 0; i < TS_MATRIX_EFFECT_CAPACITY; ++i) {
        TsProfileEffectValue *v = &effects.value[i];
        if (effects.seen & (1u << i)) {
            v->blocks.calls += 32; v->blocks.ticks += effects.ticks[i] * 32;
            if (effects.ticks[i] > v->blocks.peak) v->blocks.peak = effects.ticks[i];
            ++v->samples; v->transitions += (effects.transitioning >> i) & 1u;
        }
        PublishedEffect *p = &effects.published[i];
#define PUBLISH(field, value) atomic_store_explicit(&p->field, value, memory_order_relaxed)
        PUBLISH(generation, v->identity.generation); PUBLISH(patch, v->identity.patch);
        PUBLISH(id, v->identity.id); PUBLISH(type, v->identity.type); PUBLISH(quantum, v->identity.quantum);
        PUBLISH(calls, v->blocks.calls); PUBLISH(ticks, v->blocks.ticks); PUBLISH(peak, v->blocks.peak);
        PUBLISH(samples, v->samples); PUBLISH(transitions, v->transitions); PUBLISH(last, v->last);
#undef PUBLISH
    }
    if (audio) effects.seen = effects.transitioning = effects.dirty = 0;
    atomic_store_explicit(&p->published_epoch, p->epoch, memory_order_relaxed);
    atomic_store_explicit(&p->revision, rev + 2u, memory_order_release);
}
void ts_profile_snapshot(TsProfileSnapshot *s)
{
    memset(s, 0, sizeof(*s)); s->enabled = ts_profile_enabled();
    s->frequency = (double)frequency;
    uint64_t now = s->enabled ? ts_profile_clock() : stopped;
    s->now = now;
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
            if (lane) for (int i = 0; i < TS_MATRIX_EFFECT_CAPACITY; ++i) {
                const PublishedEffect *p = &effects.published[i]; TsProfileEffectValue *v = &s->effect[i];
#define READ(field) atomic_load_explicit(&p->field, memory_order_relaxed)
                v->identity.generation = READ(generation); v->identity.patch = READ(patch);
                v->identity.id = (uint32_t)READ(id); v->identity.type = (uint32_t)READ(type);
                v->identity.quantum = (uint32_t)READ(quantum);
                v->blocks.calls = READ(calls); v->blocks.ticks = READ(ticks); v->blocks.peak = READ(peak);
                v->samples = READ(samples); v->transitions = READ(transitions); v->last = READ(last);
#undef READ
            }
            atomic_thread_fence(memory_order_acquire);
            if (rev == atomic_load_explicit(&p->revision, memory_order_relaxed) &&
                atomic_load_explicit(&p->published_epoch,memory_order_relaxed) ==
                atomic_load_explicit(&epoch,memory_order_relaxed)) break;
            for (int i = lane ? TS_PROF_AUDIO : 0; i < (lane ? TS_PROF_COUNT : TS_PROF_AUDIO); ++i)
                s->value[i] = (TsProfileValue){0};
            if (lane) memset(s->effect, 0, sizeof(s->effect));
        }
    }
}
void ts_profile_effect_label(const TsProfileEffectIdentity *v, char *text, size_t capacity)
{
    if (v->type == TS_MATRIX_FX_SUNVOX) {
        TsSunVoxControls c = {.patch = v->patch};
        snprintf(text, capacity, "#%u SUNVOX %s (%u frames)",
            (unsigned)v->id, ts_sunvox_name(&c), (unsigned)v->quantum);
    } else snprintf(text, capacity, "#%u %s", (unsigned)v->id, ts_matrix_effect_name((int)v->type));
}
const char *ts_profile_name(TsProfileId id)
{
    static const char *names[] = {"UI work", "Controller update", "Events / MIDI", "Tracker UI",
        "Main paint", "Waveforms", "Sister paint", "Prism visualizer", "Aux windows",
        "Texture / damage / copy", "Present / swap", "Event wait", "EQ response drawing", "Tracker scopes", "Waveform analysis/cache",
        "Audio callback", "Voices / ARP / Mosaic", "Tracker DSP", "Sister runtime",
        "Router (incl stages)", "Prism DSP", "Fallout DSP", "Pedalboard DSP",
        "Insert DSP", "Master EQ / limiter", "Spatial output", "Audio snapshots", "Sister tape stage", "Matrix effects DSP"};
    return id < TS_PROF_COUNT ? names[id] : "Unknown";
}
void ts_profile_report(char *text, size_t capacity)
{
    TsProfileSnapshot s; ts_profile_snapshot(&s);
    if (!capacity) return;
    size_t used = (size_t)snprintf(text, capacity,
        "Performance profile: %s; %.1f s\n"
        "Inclusive wall timings; nested rows overlap. Present includes driver/VSync wait.\n"
        "Audio children sample 1/32 blocks (rotating phase): estimated totals/rates, observed peaks.\n"
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
    if (used >= capacity) return;
    int n = snprintf(text + used, capacity - used,
        "\nMatrix effect instances: last measured identity in each slot; replacement restarts that row.\n"
        "Sampled 1/32 callbacks; block times sum the effect's work across the callback.\n"
        "Totals/rates are estimates over the whole profile; peaks are observed samples, not worst-case bounds.\n"
        "Crossfades include both engines. Rows are children of Matrix effects DSP; do not add them again.\n"
        "Effect | slot / generation | samples | avg ms/block | peak ms/block | ms/s | %% callback | crossfade samples | last seen s ago\n");
    if (n < 0) return;
    used += (size_t)n;
    for (int i = 0; i < TS_MATRIX_EFFECT_CAPACITY && used < capacity; ++i) {
        const TsProfileEffectValue *v = &s.effect[i]; if (!v->samples) continue;
        char label[256]; ts_profile_effect_label(&v->identity, label, sizeof(label));
        double ms = s.frequency ? 1000.0 * v->blocks.ticks / s.frequency : 0;
        n = snprintf(text + used, capacity - used,
            "%s | %d / %llu | %llu | %.6f | %.4f | %.3f | %.1f | %llu | %.3f\n", label, i+1,
            (unsigned long long)v->identity.generation, (unsigned long long)v->samples,
            v->blocks.calls ? ms / v->blocks.calls : 0,
            s.frequency ? 1000.0 * v->blocks.peak / s.frequency : 0,
            s.seconds ? ms / s.seconds : 0,
            s.value[TS_PROF_AUDIO].ticks ? 100.0 * v->blocks.ticks / s.value[TS_PROF_AUDIO].ticks : 0,
            (unsigned long long)v->transitions,
            s.frequency && s.now >= v->last ? (s.now-v->last)/s.frequency : 0);
        if (n < 0) break;
        used += (size_t)n;
    }
}
