#include "tapesister/sister_tracker.h"
#include "tapesister/source_route.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail(char *error, size_t size, const char *message)
{
    if (error && size) snprintf(error, size, "%s", message);
    return 0;
}

void ts_sister_tracker_init(TsSisterTracker *t)
{
    memset(t, 0, sizeof(*t));
    t->next_pattern_id = 1;
    t->bpm = 125;
    t->ticks_per_line = 6;
    t->channel_count = 8;
    t->loop = 1;
    t->control_lane = -1;
    t->fasttracks_uses_length = 1;
    t->random_seed = 1;
    t->edit_step = 1;
    t->follow = 1;
    for (int lane = 0; lane < TS_TRACKER_LANES; ++lane) {
        snprintf(t->lanes[lane].name, TS_TRACKER_NAME_SIZE, "TRACK %d", lane + 1);
        t->lanes[lane].ratio = 7; /* 1:1 in the frozen 17-entry ratio bank. */
        t->lanes[lane].trim = 1.0f;
        t->lanes[lane].output_route.mode = TS_SOURCE_INHERIT;
        t->lanes[lane].output_stereo = 1;
    }
}

void ts_sister_tracker_free(TsSisterTracker *t)
{
    if (!t) return;
    for (int i = 0; i < TS_TRACKER_PATTERNS; ++i) free(t->patterns[i]);
    free(t->embedded_data);
    ts_sister_tracker_init(t);
}

const TsTrackerPattern *ts_sister_tracker_pattern_const(const TsSisterTracker *t,
                                                       TsPatternId id)
{
    if (!t || !id || t->pattern_count > TS_TRACKER_PATTERNS) return NULL;
    for (int i = 0; i < t->pattern_count; ++i)
        if (t->patterns[i] && t->patterns[i]->id == id) return t->patterns[i];
    return NULL;
}

TsTrackerPattern *ts_sister_tracker_pattern(TsSisterTracker *t, TsPatternId id)
{
    return (TsTrackerPattern *)ts_sister_tracker_pattern_const(t, id);
}

int ts_sister_tracker_clone(TsSisterTracker *dst, const TsSisterTracker *src,
                            char *error, size_t size)
{
    TsSisterTracker copy;
    if (!dst || !ts_sister_tracker_validate(src, error, size)) return 0;
    if (dst == src) return 1;
    copy = *src;
    copy.embedded_data = NULL;
    memset(copy.patterns, 0, sizeof(copy.patterns));
    if(src->embedded_size) {
        copy.embedded_data = malloc(src->embedded_size);
        if(!copy.embedded_data)return fail(error,size,"Out of memory copying embedded tracker");
        memcpy(copy.embedded_data,src->embedded_data,src->embedded_size);
    }
    for (int i = 0; i < src->pattern_count; ++i) {
        copy.patterns[i] = malloc(sizeof(*copy.patterns[i]));
        if (!copy.patterns[i]) {
            ts_sister_tracker_free(&copy);
            return fail(error, size, "Out of memory copying SisterTracker");
        }
        *copy.patterns[i] = *src->patterns[i];
    }
    ts_sister_tracker_free(dst);
    *dst = copy;
    return 1;
}

int ts_sister_tracker_add_pattern(TsSisterTracker *t, uint16_t rows,
                                  TsPatternId *id, char *error, size_t size)
{
    TsTrackerPattern *p;
    if (id) *id = 0;
    if (!t || rows < 1 || rows > TS_TRACKER_ROWS ||
        t->pattern_count >= TS_TRACKER_PATTERNS ||
        !t->next_pattern_id || t->next_pattern_id == UINT32_MAX)
        return fail(error, size, "Invalid pattern length or SisterTracker pattern limit reached");
    p = calloc(1, sizeof(*p));
    if (!p) return fail(error, size, "Out of memory creating SisterTracker pattern");
    p->id = t->next_pattern_id++;
    p->rows = rows;
    snprintf(p->name, sizeof(p->name), "PATTERN %u", p->id);
    t->patterns[t->pattern_count++] = p;
    if (!t->editor_pattern) t->editor_pattern = p->id;
    if (id) *id = p->id;
    return 1;
}

int ts_sister_tracker_copy_pattern(TsSisterTracker *t, TsPatternId source,
                                   TsPatternId *id, char *error, size_t size)
{
    const TsTrackerPattern *p = ts_sister_tracker_pattern_const(t, source);
    TsPatternId made;
    if (id) *id = 0;
    if (!p) return fail(error, size, "SisterTracker source pattern is missing");
    if (!ts_sister_tracker_add_pattern(t, p->rows, &made, error, size)) return 0;
    *t->patterns[t->pattern_count - 1] = *p;
    t->patterns[t->pattern_count - 1]->id = made;
    if (id) *id = made;
    return 1;
}

int ts_sister_tracker_remove_pattern(TsSisterTracker *t, TsPatternId id,
                                     char *error, size_t size)
{
    if (!ts_sister_tracker_pattern_const(t, id))
        return fail(error, size, "SisterTracker pattern is missing");
    for (int i = 0; i < t->order_count; ++i)
        if (t->orders[i] == id)
            return fail(error, size, "Remove pattern references from the order list first");
    for (int i = 0; i < t->pattern_count; ++i) {
        if (t->patterns[i]->id != id) continue;
        free(t->patterns[i]);
        memmove(&t->patterns[i], &t->patterns[i + 1],
                (t->pattern_count - i - 1u) * sizeof(t->patterns[0]));
        t->patterns[--t->pattern_count] = NULL;
        if (t->editor_pattern == id) {
            t->editor_pattern = t->pattern_count ? t->patterns[0]->id : 0;
            t->editor_row = 0;
        }
        return 1;
    }
    return 0;
}

int ts_sister_tracker_set_rows(TsSisterTracker *t, TsPatternId id, uint16_t rows)
{
    TsTrackerPattern *p = ts_sister_tracker_pattern(t, id);
    if (!p || rows < 1 || rows > TS_TRACKER_ROWS) return 0;
    p->rows = rows;
    if (t->editor_pattern == id && t->editor_row >= rows) t->editor_row = rows - 1;
    return 1;
}

int ts_sister_tracker_insert_order(TsSisterTracker *t, uint16_t index,
                                   TsPatternId id, char *error, size_t size)
{
    if (!t || index > t->order_count || t->order_count >= TS_TRACKER_ORDERS ||
        !ts_sister_tracker_pattern_const(t, id))
        return fail(error, size, "Invalid SisterTracker order or order limit reached");
    memmove(&t->orders[index + 1], &t->orders[index],
            (t->order_count - index) * sizeof(t->orders[0]));
    if (t->order_count && index <= t->restart_order) ++t->restart_order;
    t->orders[index] = id;
    ++t->order_count;
    return 1;
}

int ts_sister_tracker_remove_order(TsSisterTracker *t, uint16_t index)
{
    if (!t || index >= t->order_count || t->order_count > TS_TRACKER_ORDERS) return 0;
    memmove(&t->orders[index], &t->orders[index + 1],
            (t->order_count - index - 1u) * sizeof(t->orders[0]));
    t->orders[--t->order_count] = 0;
    if (index < t->restart_order) --t->restart_order;
    if (t->restart_order >= t->order_count)
        t->restart_order = t->order_count ? t->order_count - 1 : 0;
    return 1;
}

int ts_sister_tracker_bind_tile(TsSisterTracker *t, TsTileId id,
                                uint8_t *alias, char *error, size_t size)
{
    int empty = 0;
    if (alias) *alias = 0;
    if (!t || !ts_tile_id_valid(id)) return fail(error, size, "Invalid SisterTracker tile ID");
    for (int i = 1; i < TS_TRACKER_ALIASES; ++i) {
        if (t->aliases[i] == id) { if (alias) *alias = (uint8_t)i; return 1; }
        if (!empty && !t->aliases[i]) empty = i;
    }
    if (!empty) return fail(error, size, "SisterTracker has 255 referenced tiles already");
    t->aliases[empty] = id;
    ts_tile_id_reserve(id);
    if (alias) *alias = (uint8_t)empty;
    return 1;
}

static int boolean(unsigned value) { return value <= 1; }
static int name_valid(const char *name) { return memchr(name, 0, TS_TRACKER_NAME_SIZE) != NULL; }

int ts_sister_tracker_validate(const TsSisterTracker *t, char *error, size_t size)
{
#define REQUIRE(condition, message) do { if (!(condition)) return fail(error, size, message); } while (0)
    REQUIRE(t, "No SisterTracker definition");
    REQUIRE((!t->embedded_size && !t->embedded_data) ||
            ts_tracker_embedded_validate(t->embedded_data,t->embedded_size),
            "Invalid embedded SisterTracker score");
    REQUIRE(t->pattern_count <= TS_TRACKER_PATTERNS && t->order_count <= TS_TRACKER_ORDERS &&
            t->channel_count >= 2 && t->channel_count <= TS_TRACKER_LANES && !(t->channel_count&1) &&
            t->next_pattern_id != 0 && t->bpm >= 32 && t->bpm <= 999 &&
            (t->ticks_per_line >= 1 || t->embedded_size) && t->ticks_per_line <= 31 && boolean(t->loop) &&
            t->control_lane >= -1 && t->control_lane < TS_TRACKER_LANES &&
            boolean(t->fasttracks_uses_length) && boolean(t->length_bypass) &&
            ((!t->order_count && !t->restart_order) || t->restart_order < t->order_count),
            "Invalid SisterTracker song settings");
    REQUIRE(t->aliases[0] == 0, "SisterTracker alias 00 is reserved");
    for (int i = 1; i < TS_TRACKER_ALIASES; ++i) {
        REQUIRE(!t->aliases[i] || ts_tile_id_valid(t->aliases[i]), "Invalid SisterTracker tile alias");
        for (int j = 1; j < i; ++j)
            REQUIRE(!t->aliases[i] || t->aliases[i] != t->aliases[j], "Duplicate SisterTracker tile alias");
    }
    for (int a=1;a<=128;++a) {
        const TsTrackerInstrument *v=&t->instruments[a];
        REQUIRE(v->present<=1 && v->sample_count<=16 && memchr(v->name,0,sizeof(v->name)), "Invalid XM instrument");
        REQUIRE(v->vol_length<=12 && v->pan_length<=12 && v->vol_sustain<12 && v->vol_start<12 && v->vol_end<12 &&
                v->pan_sustain<12 && v->pan_start<12 && v->pan_end<12 && v->vol_flags<=7 && v->pan_flags<=7 &&
                v->vib_type<=3, "Invalid XM envelope");
        for(int k=0;k<96;++k) REQUIRE(v->note_map[k]<16, "Invalid XM sample map");
        for(int k=0;k<16;++k) REQUIRE((!v->tiles[k] || ts_tile_id_valid(v->tiles[k])) && v->volume[k]<=64 &&
            memchr(v->sample_names[k],0,sizeof(v->sample_names[k])), "Invalid XM sample binding");
        for(int k=0;k<12;++k) REQUIRE(v->vol_points[k][1]<=64 && v->pan_points[k][1]<=64, "Invalid XM envelope point");
    }
    for (int i = 0; i < TS_TRACKER_LANES; ++i) {
        const TsTrackerLane *l = &t->lanes[i];
        REQUIRE(ts_source_route_valid(&l->output_route,1) && name_valid(l->name) && l->length <= TS_TRACKER_ROWS &&
                l->mode <= TS_TRACKER_SONG && l->ratio < TS_TRACKER_RATIOS &&
                l->direction <= TS_TRACKER_PING_PONG && l->voice_mode <= TS_TRACKER_VOICE_OVERLAP &&
                isfinite(l->trim) && l->trim >= 0 && l->trim <= 2 && boolean(l->muted) &&
                l->route <= TS_TRACKER_ROUTE_MAIN_DIRECT && boolean(l->output_stereo) &&
                ((l->route == TS_TRACKER_ROUTE_MAIN && l->output_channel == 0) ||
                 (l->route != TS_TRACKER_ROUTE_MAIN && l->output_channel >= 3 &&
                  l->output_channel <= (l->output_stereo ? 63 : 64))),
                "Invalid SisterTracker lane settings");
    }
    for (int i = 0; i < TS_TRACKER_PATTERNS; ++i) {
        const TsTrackerPattern *p = t->patterns[i];
        if (i >= t->pattern_count) { REQUIRE(!p, "Unexpected SisterTracker pattern storage"); continue; }
        REQUIRE(p && p->id && p->id < t->next_pattern_id && name_valid(p->name) &&
                p->rows >= 1 && p->rows <= TS_TRACKER_ROWS, "Invalid SisterTracker pattern");
        for (int j = 0; j < i; ++j)
            REQUIRE(p->id != t->patterns[j]->id, "Duplicate SisterTracker pattern ID");
        for (int row = 0; row < TS_TRACKER_ROWS; ++row) for (int lane = 0; lane < TS_TRACKER_LANES; ++lane) {
            const TsTrackerCell *c = &p->cells[row][lane];
            REQUIRE(c->note_kind <= TS_TRACKER_NOTE_CUT && c->note <= 127 &&
                    (c->note_kind == TS_TRACKER_NOTE_PITCH || !c->note) &&
                    boolean(c->has_volume) && c->volume <= 0x40 && (c->has_volume || !c->volume) &&
                    (!c->tune_command || c->tune_command == 'M' || c->tune_command == 'N') &&
                    (c->tune_command || !c->tune_value) &&
                    (t->embedded_size || c->tune_command != 'M' || c->tune_value <= 127) &&
                    (!c->fx_command || (c->fx_command >= '0' && c->fx_command <= '9') ||
                     (c->fx_command >= 'A' && c->fx_command <= 'Z')) &&
                    (c->fx_command || !c->fx_value), "Invalid SisterTracker cell command");
            if (c->tile_id) {
                int found = 0;
                for (int alias = 1; alias < TS_TRACKER_ALIASES; ++alias)
                    if (t->aliases[alias] == c->tile_id) { found = 1; break; }
                REQUIRE(found, "SisterTracker cell tile has no display alias");
            }
        }
    }
    for (int i = 0; i < t->order_count; ++i)
        REQUIRE(ts_sister_tracker_pattern_const(t, t->orders[i]), "SisterTracker order references a missing pattern");
    const TsTrackerPattern *editor = ts_sister_tracker_pattern_const(t, t->editor_pattern);
    unsigned editor_rows = editor ? editor->rows : 0;
    if (t->embedded_size) for (int lane = 0; lane < TS_TRACKER_LANES; ++lane)
        if (t->lanes[lane].length > editor_rows) editor_rows = t->lanes[lane].length;
    REQUIRE(((!t->pattern_count && !t->editor_pattern && !t->editor_row) ||
             (editor && t->editor_row < editor_rows)) &&
            t->editor_lane < TS_TRACKER_LANES && t->edit_step <= 16 && boolean(t->follow),
            "Invalid SisterTracker editor position");
    REQUIRE(ts_tracker_embedded_matches(t),"Embedded SisterTracker pattern identity mismatch");
    if (error && size) error[0] = 0;
    return 1;
#undef REQUIRE
}

void ts_sister_tracker_reserve_tile_ids(const TsSisterTracker *t)
{
    if (!t) return;
    for (int i = 1; i < TS_TRACKER_ALIASES; ++i) ts_tile_id_reserve(t->aliases[i]);
    for(int a=1;a<=128;++a)for(int k=0;k<16;++k)ts_tile_id_reserve(t->instruments[a].tiles[k]);
}
