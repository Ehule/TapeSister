#include "tapesister/pedalboard.h"
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

struct TsPedalboard {
    TsPedalboardControls controls;
    TsSisterSingleFx *child[TS_SISTER_FX_SLOT_COUNT];
    float gate[TS_SISTER_FX_SLOT_COUNT], start[TS_SISTER_FX_SLOT_COUNT];
    unsigned remaining[TS_SISTER_FX_SLOT_COUNT], total[TS_SISTER_FX_SLOT_COUNT], rate;
};
static int unit(float x) { return isfinite(x) && x >= 0 && x <= 1; }
static int slot_valid(const TsSisterFxSlotControls *s)
{
    return s->type >= TS_SISTER_FX_EMPTY && s->type < TS_SISTER_FX_TYPE_COUNT &&
        (s->enabled == 0 || s->enabled == 1) && s->placement == TS_SISTER_FX_PLACE_POST &&
        isfinite(s->gain_db) && s->gain_db >= -12 && s->gain_db <= 12 &&
        unit(s->parameter_a) && unit(s->parameter_b) && unit(s->parameter_c) && unit(s->mix) &&
        s->parameter_locks <= 31 && ts_transient_states_valid(s->transient_power, &s->transient_states) &&
        ts_transient_states_valid(0, &s->grain_states) &&
        (s->type != TS_SISTER_FX_FILTER || ts_character_filter_valid(&s->filter));
}
void ts_pedalboard_default(TsPedalboardControls *c)
{
    memset(c, 0, sizeof(*c)); c->initialized = 1; c->count = TS_SISTER_FX_SLOT_DEFAULT;
    c->transition = ts_sister_fx_transition_normalized(12);
    for (int i = 0; i < TS_SISTER_FX_SLOT_COUNT; ++i) c->slot[i] = ts_sister_fx_empty_slot();
}
int ts_pedalboard_valid(const TsPedalboardControls *c)
{
    if (!c || c->initialized != 1 || c->count < TS_SISTER_FX_SLOT_MIN ||
        c->count > TS_SISTER_FX_SLOT_COUNT || !unit(c->transition)) return 0;
    for (int i = 0; i < TS_SISTER_FX_SLOT_COUNT; ++i)
        if (!slot_valid(&c->slot[i]) || (i >= c->count && c->slot[i].type)) return 0;
    return 1;
}
int ts_pedalboard_topology_matches(const TsPedalboard *b, const TsPedalboardControls *c)
{
    if (!b || !c) return 0;
    /* Adding/removing empty rows changes only the editor, not DSP history. */
    for (int i = 0; i < TS_SISTER_FX_SLOT_COUNT; ++i)
        if (b->controls.slot[i].type != c->slot[i].type) return 0;
    return 1;
}
TsPedalboard *ts_pedalboard_new(const TsPedalboardControls *c, unsigned rate, uint32_t seed)
{
    if (!ts_pedalboard_valid(c) || rate < 1000 || rate > 384000) return NULL;
    TsPedalboard *b = calloc(1, sizeof(*b)); if (!b) return NULL;
    b->controls = *c; b->rate = rate;
    for (int i = 0; i < c->count; ++i) {
        if (c->slot[i].type) {
            b->child[i] = ts_sister_single_fx_new(c->slot[i].type, rate, seed + (i + 1) * UINT32_C(0x9e3779b9));
            if (!b->child[i]) { ts_pedalboard_free(b); return NULL; }
            ts_sister_single_fx_set(b->child[i], &c->slot[i]);
        }
        b->gate[i] = c->slot[i].enabled;
    }
    return b;
}
void ts_pedalboard_free(TsPedalboard *b)
{
    if (!b) return;
    for (int i = 0; i < TS_SISTER_FX_SLOT_COUNT; ++i) ts_sister_single_fx_free(b->child[i]);
    free(b);
}
void ts_pedalboard_set(TsPedalboard *b, const TsPedalboardControls *c)
{
    if (!ts_pedalboard_valid(c) || !ts_pedalboard_topology_matches(b, c)) return;
    unsigned frames = (unsigned)fmaxf(1, ts_sister_fx_transition_ms(c->transition) * .001f * b->rate);
    for (int i = 0; i < c->count; ++i) {
        if (c->slot[i].enabled != b->controls.slot[i].enabled ||
            (b->remaining[i] && c->transition != b->controls.transition)) {
            b->start[i] = b->gate[i]; b->remaining[i] = b->total[i] = frames;
        }
        ts_sister_single_fx_set(b->child[i], &c->slot[i]);
    }
    b->controls = *c;
}
static float advance(TsPedalboard *b, int i)
{
    if (b->remaining[i]) {
        --b->remaining[i];
        b->gate[i] = b->start[i] + (b->controls.slot[i].enabled - b->start[i]) *
            (1 - (double)b->remaining[i] / b->total[i]);
    }
    return b->gate[i];
}
TsStereoFrame ts_pedalboard_process(TsPedalboard *b, TsStereoFrame in)
{
    if (!b) return in;
    for (int i = 0; i < b->controls.count; ++i)
        if (b->child[i]) in = ts_sister_single_fx_process(b->child[i], in, advance(b, i));
    return ts_stereo_frame_sanitize_inline(in);
}
void ts_pedalboard_process_span(TsPedalboard *b, const TsStereoFrame *in, TsStereoFrame *out, unsigned frames)
{
    /* Bounded scratch; dispatch once per child, preserving every gate sample. */
    while (frames) {
        unsigned n = frames > 64 ? 64 : frames;
        TsStereoFrame scratch[64]; float gate[64];
        memcpy(scratch, in, n * sizeof(*in));
        for (int i = 0; b && i < b->controls.count; ++i) if (b->child[i]) {
            for (unsigned j = 0; j < n; ++j) gate[j] = advance(b, i);
            ts_sister_single_fx_process_span(b->child[i], scratch, gate, scratch, n);
        }
        for (unsigned j = 0; j < n; ++j) out[j] = ts_stereo_frame_sanitize_inline(scratch[j]);
        in += n; out += n; frames -= n;
    }
}
size_t ts_pedalboard_memory(const TsPedalboard *b)
{
    if (!b) return 0;
    size_t size = sizeof(*b);
    for (int i = 0; i < b->controls.count; ++i) size += ts_sister_single_fx_memory(b->child[i]);
    return size;
}

int ts_pedalboard_write(FILE *f, const char *prefix, const TsPedalboardControls *c)
{
    if (!ts_pedalboard_valid(c) || fprintf(f, "%s.Header=1,%d,%.9g\n", prefix, c->count, c->transition) < 0) return 0;
    for (int i = 0; i < c->count; ++i) {
        const TsSisterFxSlotControls *s = &c->slot[i]; char key[96];
        if (fprintf(f, "%s.Slot%d=%d,%d,%.9g,%.9g,%.9g,%.9g,%.9g,%u\n", prefix, i,
            s->type, s->enabled, s->gain_db, s->parameter_a, s->parameter_b, s->parameter_c, s->mix, s->parameter_locks) < 0) return 0;
        snprintf(key, sizeof(key), "%s.Transient%d", prefix, i);
        if (!ts_transient_states_write(f, key, s->transient_power, &s->transient_states)) return 0;
        snprintf(key, sizeof(key), "%s.Grain%d", prefix, i);
        if (!ts_transient_states_write(f, key, 0, &s->grain_states)) return 0;
        snprintf(key, sizeof(key), "%s.Filter%d", prefix, i);
        if (!ts_character_filter_write(f, key, &s->filter)) return 0;
    }
    return fprintf(f, "%s.End=1\n", prefix) >= 0;
}
static int numbers(const char *s, double *v, int n)
{
    for (int i = 0; i < n; ++i) {
        char *end; errno = 0; v[i] = strtod(s, &end);
        if (errno || end == s || !isfinite(v[i]) || (i < n - 1 ? *end != ',' : *end != 0)) return 0;
        s = end + (i < n - 1);
    }
    return 1;
}
static int whole(double v, int lo, int hi) { return v >= lo && v <= hi && v == floor(v); }
int ts_pedalboard_read(TsPedalboardControls *c, const char *key, const char *value)
{
    TsPedalboardControls next = *c; int i = -1, used = 0; double v[8];
    if (!strcmp(key, "Header")) {
        if (!numbers(value, v, 3) || v[0] != 1 || !whole(v[1], 5, 20) || v[2] < 0 || v[2] > 1) return -1;
        ts_pedalboard_default(&next); next.initialized = 2; next.count = v[1]; next.transition = v[2];
    } else {
        if (next.initialized != 2) return -1;
        if (!strcmp(key, "End")) {
            next.initialized = 1;
            if (strcmp(value, "1") || !ts_pedalboard_valid(&next)) return -1;
        } else if (sscanf(key, "Slot%d%n", &i, &used) == 1 && used && !key[used] && i >= 0 && i < next.count) {
            if (!numbers(value, v, 8) || !whole(v[0], 0, TS_SISTER_FX_TYPE_COUNT-1) ||
                !whole(v[1], 0, 1) || !whole(v[7], 0, 31)) return -1;
            TsSisterFxSlotControls *s = &next.slot[i];
            s->type = v[0]; s->enabled = v[1]; s->gain_db = v[2]; s->parameter_a = v[3];
            s->parameter_b = v[4]; s->parameter_c = v[5]; s->mix = v[6]; s->parameter_locks = v[7];
            if (!slot_valid(s)) return -1;
        } else if (sscanf(key, "Transient%d%n", &i, &used) == 1 && used && !key[used] && i >= 0 && i < next.count) {
            if (!ts_transient_states_read(value, &next.slot[i].transient_power, &next.slot[i].transient_states)) return -1;
        } else if (sscanf(key, "Grain%d%n", &i, &used) == 1 && used && !key[used] && i >= 0 && i < next.count) {
            uint8_t power;
            if (!ts_transient_states_read(value, &power, &next.slot[i].grain_states) || power) return -1;
        } else if (sscanf(key, "Filter%d%n", &i, &used) == 1 && used && !key[used] && i >= 0 && i < next.count) {
            if (!ts_character_filter_read(&next.slot[i].filter, value)) return -1;
        } else return -1;
    }
    *c = next; return 1;
}
