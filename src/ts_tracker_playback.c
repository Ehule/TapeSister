#include "tapesister/tracker_playback.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void retain(TsTrackerSource *s) { if (s) atomic_fetch_add_explicit(&s->readers, 1, memory_order_relaxed); }
static void release(TsTrackerSource *s) { if (s) atomic_fetch_sub_explicit(&s->readers, 1, memory_order_release); }

static const TsBankSlot *resolve(const TsSamplePages *pages, const TsInstrument *active,
    TsTileId id, const TsInstrument **bank, const TsSample **sample)
{
    TsTileLocation location;
    const TsBankSlot *slot = ts_sample_pages_find_tile(pages, active, id, &location);
    if (!slot) return NULL;
    *bank = ts_sample_pages_page(pages, active, location.page);
    *sample = (*bank)->selected_slot == location.slot && (*bank)->current.data ?
              &(*bank)->current : &slot->sample;
    return slot;
}

static void hash_bytes(uint64_t *hash, const void *data, size_t size)
{
    const unsigned char *p = data;
    for (size_t i = 0; i < size; ++i) { *hash ^= p[i]; *hash *= UINT64_C(1099511628211); }
}

uint64_t ts_tracker_playback_stamp(const TsSamplePages *pages,
                                  const TsInstrument *active,
                                  TsPatternId pattern, int rate)
{
    const TsTrackerPattern *p = pages ? ts_sister_tracker_pattern_const(&pages->tracker, pattern) : NULL;
    if (!p) return 0;
    const TsSisterTracker *t = &pages->tracker;
    uint64_t hash = UINT64_C(14695981039346656037);
#define HASH(value) hash_bytes(&hash, &(value), sizeof(value))
    HASH(rate); HASH(p->id); HASH(p->rows); HASH(p->cells);
    HASH(t->bpm); HASH(t->ticks_per_line); HASH(t->loop); HASH(t->lanes); HASH(t->aliases);
    HASH(t->control_lane); HASH(t->fasttracks_uses_length); HASH(t->length_bypass);
    for (int i = 1; i < TS_TRACKER_ALIASES; ++i) if (t->aliases[i]) {
        const TsInstrument *bank = NULL;
        const TsSample *sample = NULL;
        const TsBankSlot *slot = resolve(pages, active, t->aliases[i], &bank, &sample);
        int available = slot != NULL;
        HASH(available);
        if (!slot) continue;
        HASH(sample->data); HASH(sample->visual_revision); HASH(sample->frames);
        HASH(sample->sample_rate); HASH(sample->channels);
        if (sample == &bank->current) {
            HASH(bank->tuning); HASH(bank->audible_tuning); HASH(bank->has_loop);
            HASH(bank->loop_first); HASH(bank->loop_last); HASH(bank->loop_crossfade_ms); HASH(bank->loop_mode);
        } else {
            HASH(slot->tuning); HASH(slot->audible_tuning); HASH(slot->has_loop);
            HASH(slot->loop_first); HASH(slot->loop_last); HASH(slot->loop_crossfade_ms); HASH(slot->loop_mode);
        }
    }
#undef HASH
    return hash;
}

void ts_tracker_playback_init(TsTrackerPlayback *rt)
{
    memset(rt, 0, sizeof(*rt));
    atomic_init(&rt->display_running, 0); atomic_init(&rt->display_row, 0);
    atomic_init(&rt->display_pattern, 0); atomic_init(&rt->display_missing, 0);
    atomic_init(&rt->display_master_row, 0);
    atomic_init(&rt->display_block,0);atomic_init(&rt->display_block_rows,0);atomic_init(&rt->display_block_lanes,0);
    for (int i = 0; i < TS_TRACKER_LANES; ++i) {
        rt->lanes[i].volume = 0x40;
        atomic_init(&rt->display_lane_row[i], 0);
        atomic_init(&rt->display_lane_phase[i], 0);
    }
}

void ts_tracker_prepared_free(TsTrackerPrepared *p)
{
    if (!p) return;
    for (int i = 1; i < TS_TRACKER_ALIASES; ++i) release(p->bindings[i].source);
    free(p);
}

void ts_tracker_playback_collect(TsTrackerPlayback *rt)
{
    TsTrackerSource **link = &rt->sources;
    while (*link) {
        TsTrackerSource *s = *link;
        if (atomic_load_explicit(&s->readers, memory_order_acquire)) { link = &s->next; continue; }
        *link = s->next;
        ts_sample_free(&s->sample);
        free(s);
    }
}

static void stop_lane(TsTrackerLanePlayback *l)
{
    if (l->voice.active || l->source) l->changed = 1;
    l->voice.active = 0;
    l->voice.sample = NULL;
    release(l->source); l->source = NULL;
    l->sounding_tile = 0;
}

void ts_tracker_playback_stop(TsTrackerPlayback *rt)
{
    if (!rt) return;
    rt->running = rt->paused = 0;
    rt->block_active=rt->block_pending=rt->loop_seam=0;
    rt->tail_active = 1;
    for (int i = 0; i < TS_TRACKER_LANES; ++i) stop_lane(&rt->lanes[i]);
    ts_tracker_playback_end_block(rt);
}

void ts_tracker_playback_free(TsTrackerPlayback *rt)
{
    if (!rt) return;
    ts_tracker_playback_stop(rt);
    ts_tracker_prepared_free(rt->prepared); rt->prepared = NULL;
    ts_tracker_playback_collect(rt);
}

TsTrackerPrepared *ts_tracker_playback_prepare(TsTrackerPlayback *rt,
    const TsSamplePages *pages, const TsInstrument *active, TsPatternId pattern,
    int rate, char *error, size_t size)
{
    const TsTrackerPattern *p = pages ? ts_sister_tracker_pattern_const(&pages->tracker, pattern) : NULL;
    if (!rt || !p || rate < 1000) {
        if (error && size) snprintf(error, size, "No playable SisterTracker pattern or output rate");
        return NULL;
    }
    if (!ts_sister_tracker_validate(&pages->tracker, error, size)) return NULL;
    TsTrackerPrepared *prepared = calloc(1, sizeof(*prepared));
    if (!prepared) goto memory;
    prepared->pattern = *p;
    memcpy(prepared->lanes, pages->tracker.lanes, sizeof(prepared->lanes));
    prepared->bpm = pages->tracker.bpm; prepared->ticks_per_line = pages->tracker.ticks_per_line;
    prepared->loop = pages->tracker.loop; prepared->rate = rate;
    prepared->control_lane = pages->tracker.control_lane;
    prepared->fasttracks_uses_length = pages->tracker.fasttracks_uses_length;
    prepared->length_bypass = pages->tracker.length_bypass;
    prepared->stamp = ts_tracker_playback_stamp(pages, active, pattern, rate);
    for (int i = 1; i < TS_TRACKER_ALIASES; ++i) if (pages->tracker.aliases[i]) {
        TsTrackerBinding *b = &prepared->bindings[i];
        const TsInstrument *bank = NULL;
        const TsSample *sample = NULL;
        b->id = pages->tracker.aliases[i];
        const TsBankSlot *slot = resolve(pages, active, b->id, &bank, &sample);
        if (!slot || !sample->data || sample->frames < 2) continue;
        TsTrackerSource *source;
        for (source = rt->sources; source; source = source->next)
            if (source->id == b->id && source->original_data == sample->data &&
                source->original_revision == sample->visual_revision &&
                source->sample.frames == sample->frames && source->sample.channels == sample->channels &&
                source->sample.sample_rate == sample->sample_rate) break;
        if (!source) {
            source = calloc(1, sizeof(*source));
            if (!source) goto memory;
            ts_sample_init(&source->sample);
            if (!ts_sample_clone(&source->sample, sample, error, size)) { free(source); goto failed; }
            source->id = b->id; source->original_data = sample->data;
            source->original_revision = sample->visual_revision;
            atomic_init(&source->readers, 0);
            source->next = rt->sources; rt->sources = source;
        }
        b->source = source; retain(source);
        int live = sample == &bank->current;
        TsTuning mapping = live ? bank->tuning : slot->tuning;
        TsTuning audible = live ? bank->audible_tuning : slot->audible_tuning;
        double tuning = ts_tuning_pair_audition_pitch(&mapping, &audible);
        for (int note = 0; note < 128; ++note)
            b->step[note] = (double)sample->sample_rate / rate * tuning * exp2((note - TS_KEYBOARD_BASE_NOTE) / 12.0);
        b->looping = live ? bank->has_loop : slot->has_loop;
        b->first = b->looping ? (live ? bank->loop_first : slot->loop_first) : 0;
        b->last = b->looping ? (live ? bank->loop_last : slot->loop_last) : sample->frames;
        if (b->first >= b->last || b->last > sample->frames) {
            b->looping = 0; b->first = 0; b->last = sample->frames;
        }
        b->loop_mode = live ? bank->loop_mode : slot->loop_mode;
        TsAuditionPlan plan = {.sample=sample, .first=b->first, .last=b->last};
        b->crossfade = b->looping ? ts_audition_crossfade_frames(&plan,
                            live ? bank->loop_crossfade_ms : slot->loop_crossfade_ms) : 0;
    }
    if (error && size) error[0] = 0;
    return prepared;
memory:
    if (error && size) snprintf(error, size, "Out of memory preparing SisterTracker playback");
failed:
    ts_tracker_prepared_free(prepared);
    ts_tracker_playback_collect(rt);
    return NULL;
}

static const TsTrackerBinding *binding(const TsTrackerPrepared *p, TsTileId id)
{
    if (!p || !id) return NULL;
    for (int i = 1; i < TS_TRACKER_ALIASES; ++i)
        if (p->bindings[i].id == id) return p->bindings[i].source ? &p->bindings[i] : NULL;
    return NULL;
}

static void configure_voice(TsTrackerPlayback *rt, TsTrackerLanePlayback *lane,
                             const TsTrackerBinding *b, int note, int preserve)
{
    TsNoteVoice *v = &lane->voice;
    TsLoopMode old_mode = v->loop_mode;
    int old_looping = v->looping;
    double progress = preserve && v->sample && v->sample->frames ? v->position / v->sample->frames : 0;
    TsTrackerSource *previous = lane->source;
    retain(b->source); lane->source = b->source; release(previous);
    if (!preserve) memset(v, 0, sizeof(*v));
    v->sample = &b->source->sample;
    v->range_first = b->first; v->range_last = b->last; v->crossfade_frames = b->crossfade;
    v->looping = b->looping; v->loop_mode = b->loop_mode;
    v->note = note; v->gain = 1;
    v->step = b->step[note] * rt->prepared->rate / rt->rate;
    if (preserve) {
        v->position = progress * v->sample->frames;
        if (old_mode != b->loop_mode || old_looping != b->looping) {
            int intro = 0;
            if (b->looping) {
                (void)ts_audition_loop_begin(b->first, b->last, b->loop_mode, &v->direction, &intro);
                v->loop_intro = intro && v->position < (double)b->first;
                if (!v->loop_intro && ts_loop_base_mode(b->loop_mode) == TS_LOOP_REVERSE) v->direction = -1;
            } else { v->direction = 1; v->loop_intro = 0; }
        }
        if (v->position >= v->range_last || (v->looping && !v->loop_intro && v->position < v->range_first))
            v->position = v->range_first;
    } else {
        v->position = v->looping ? ts_audition_loop_begin(b->first, b->last, b->loop_mode,
                            &v->direction, &v->loop_intro) : 0;
        if (!v->looping) v->direction = 1;
        v->attack_frames = ts_audition_attack_frames(rt->rate, TS_AUDITION_ATTACK_MS_DEFAULT);
    }
    v->active = 1;
    lane->sounding_tile = b->id;
    lane->changed = 1;
}

/* Rational clock and LEN/CONTROL rules adapted from Tapehead f053d96:
   ft2_fasttracks_core.c, ft2_fasttracks.c and ft2_replayer.c (BSD-3-Clause).
   See third_party/tapehead/CODE-LICENSE.txt. Private rows are independent of
   the physical pattern container; extension rows must never read hidden data. */
static const uint8_t ratio_numerator[TS_TRACKER_RATIOS] =
    {1,2,3,4,5,7,15,1,17,8,6,5,4,3,2,3,5};
static const uint8_t ratio_denominator[TS_TRACKER_RATIOS] =
    {2,3,4,5,6,8,16,1,16,7,5,4,3,2,1,1,1};

static int private_lane(const TsTrackerPrepared *p, int lane)
{
    /* Saved Song assignments remain Standard until order playback exists. */
    return p->lanes[lane].mode == TS_TRACKER_PATTERN;
}
static unsigned shared_length(const TsTrackerPrepared *p)
{
    if (p->length_bypass) return p->pattern.rows;
    if (p->control_lane >= 0) {
        unsigned length = p->lanes[p->control_lane].length;
        return length ? length : p->pattern.rows;
    }
    unsigned longest = 0;
    for (int i = 0; i < TS_TRACKER_LANES; ++i)
        if (p->lanes[i].length > longest) longest = p->lanes[i].length;
    return longest ? longest : p->pattern.rows;
}
static unsigned private_length(const TsTrackerPrepared *p, int lane)
{
    if (p->length_bypass || !p->fasttracks_uses_length) return p->pattern.rows;
    return p->lanes[lane].length ? p->lanes[lane].length : shared_length(p);
}
static uint32_t master_source_row(const TsTrackerPlayback *rt, int lane)
{
    const TsTrackerPrepared *p = rt->prepared;
    if (p->length_bypass) return (unsigned)rt->row;
    if (p->lanes[lane].length) return rt->master_row % p->lanes[lane].length;
    /* Tapehead's LEN-OFF master stream wraps its physical container under a
       slow private CONTROL, except when the shared domain has a blank tail. */
    return shared_length(p) > p->pattern.rows ? rt->master_row : (unsigned)rt->row;
}
static int private_control(const TsTrackerPrepared *p)
{
    return !p->length_bypass && p->control_lane >= 0 && private_lane(p, p->control_lane);
}

TsTrackerPrepared *ts_tracker_playback_publish(TsTrackerPlayback *rt, TsTrackerPrepared *p)
{
    TsTrackerPrepared *old = rt->prepared;
    rt->prepared = p;
    if (!p) { ts_tracker_playback_stop(rt); return old; }
    if(rt->running && old && old->pattern.id!=p->pattern.id)ts_tracker_playback_stop(rt);
    if (rt->running) {
        double next = (double)rt->rate * 2.5 / p->bpm;
        if (rt->tick_frames > 0) rt->until_tick *= next / rt->tick_frames;
        rt->tick_frames = next;
        if (old && old->ticks_per_line != p->ticks_per_line)
            rt->tick = rt->tick * p->ticks_per_line / old->ticks_per_line;
        /* Row-domain edits take effect at the next tick/row, never replaying
           the event that was already heard. */
        if (old && !rt->block_active) {
            int topology_changed = old->control_lane != p->control_lane ||
                old->length_bypass != p->length_bypass ||
                old->fasttracks_uses_length != p->fasttracks_uses_length;
            for (int i = 0; i < TS_TRACKER_LANES; ++i)
                topology_changed |= old->lanes[i].length != p->lanes[i].length;
            if (topology_changed) rt->control_pending = 0;
            for (int i = 0; i < TS_TRACKER_LANES; ++i) {
                TsTrackerLanePlayback *l = &rt->lanes[i];
                int old_threshold = ratio_denominator[old->lanes[i].ratio] * old->ticks_per_line;
                int threshold = ratio_denominator[p->lanes[i].ratio] * p->ticks_per_line;
                l->accumulator = (int32_t)((int64_t)l->accumulator * threshold / old_threshold);
                if (private_lane(p, i) && !private_lane(old, i)) {
                    l->source_row = master_source_row(rt, i) % private_length(p, i);
                    l->accumulator = ratio_denominator[p->lanes[i].ratio] * rt->tick;
                }
                if (topology_changed || old->lanes[i].mode != p->lanes[i].mode ||
                    old->lanes[i].direction != p->lanes[i].direction) {
                    l->cycle_steps = 0;
                    if (i == p->control_lane) rt->control_pending = 0;
                }
                if (old->lanes[i].direction != p->lanes[i].direction)
                    l->ping_direction = p->lanes[i].direction == TS_TRACKER_REVERSE ? -1 : 1;
            }
        }
    }
    for (int i = 0; i < TS_TRACKER_LANES; ++i) {
        TsTrackerLanePlayback *l = &rt->lanes[i];
        if (!l->voice.active) continue;
        const TsTrackerBinding *b = binding(p, l->sounding_tile);
        if (!b) { stop_lane(l); rt->missing_mask |= (uint8_t)(1u << i); }
        else if (l->source != b->source || l->voice.range_first != b->first ||
                 l->voice.range_last != b->last || l->voice.loop_mode != b->loop_mode ||
                 l->voice.looping != b->looping || l->voice.crossfade_frames != b->crossfade ||
                 l->voice.step != b->step[l->voice.note] * p->rate / rt->rate)
            configure_voice(rt, l, b, l->voice.note, 1);
    }
    return old;
}

int ts_tracker_playback_start(TsTrackerPlayback *rt)
{
    if (!rt || !rt->prepared) return 0;
    ts_tracker_playback_stop(rt);
    for (int i = 0; i < TS_TRACKER_LANES; ++i) {
        TsTrackerLanePlayback *l = &rt->lanes[i];
        l->default_tile = 0; l->volume = 0x40; l->notes_started = 0;
        l->source_row = 0; l->accumulator = 0; l->cycle_steps = 0;
        l->ping_direction = rt->prepared->lanes[i].direction == TS_TRACKER_REVERSE ? -1 : 1;
        l->gain = rt->prepared->lanes[i].muted || (rt->solo_mask && !(rt->solo_mask & (1u << i))) ?
                  0 : rt->prepared->lanes[i].trim;
    }
    rt->rate = rt->prepared->rate;
    rt->row = -1; rt->tick = rt->prepared->ticks_per_line - 1;
    rt->until_tick = 0; rt->tick_frames = (double)rt->rate * 2.5 / rt->prepared->bpm;
    rt->elapsed_frames = 0; rt->missing_mask = 0;
    rt->master_row = 0; rt->transport_started = rt->control_pending = 0;
    rt->loop_cycles=0;rt->loop_seam=0;
    rt->running = 1; rt->paused = 0;
    ts_tracker_playback_end_block(rt);
    return 1;
}

static int valid_block(const TsTrackerPlayback *rt,TsTrackerBlock b)
{
    return rt && rt->prepared && b.pattern==rt->prepared->pattern.id &&
        b.row0>=0 && b.row0<=b.row1 && b.row1<rt->prepared->pattern.rows &&
        b.lane0>=0 && b.lane0<=b.lane1 && b.lane1<TS_TRACKER_LANES;
}
static void inherit_before(TsTrackerPlayback *rt,int lane,int row)
{
    TsTrackerLanePlayback *l=&rt->lanes[lane];l->default_tile=0;l->volume=0x40;
    for(int y=0;y<row;++y) {
        const TsTrackerCell *c=&rt->prepared->pattern.cells[y][lane];
        if(c->tile_id)l->default_tile=c->tile_id;
        if(c->has_volume)l->volume=c->volume;
    }
}
int ts_tracker_playback_start_block(TsTrackerPlayback *rt,TsTrackerBlock b)
{
    if(!valid_block(rt,b) || !ts_tracker_playback_start(rt))return 0;
    rt->block=b;rt->block_active=1;rt->row=b.row0-1;
    for(int lane=b.lane0;lane<=b.lane1;++lane)inherit_before(rt,lane,b.row0);
    ts_tracker_playback_end_block(rt);return 1;
}
int ts_tracker_playback_queue_block(TsTrackerPlayback *rt,TsTrackerBlock b)
{
    if(!rt || !rt->block_active || !valid_block(rt,b))return 0;
    rt->pending_block=b;rt->block_pending=1;return 1;
}
/* Tapehead's pending-bounds contract: the current cycle always finishes before
   a new rectangle is adopted. Resized patterns clamp safely at that boundary. */
static void block_seam(TsTrackerPlayback *rt)
{
    TsTrackerBlock old=rt->block,b=rt->block_pending?rt->pending_block:old;
    int last=rt->prepared->pattern.rows-1;
    if(b.row0>last)b.row0=last;
    if(b.row1>last)b.row1=last;
    rt->block=b;rt->block_pending=0;rt->row=b.row0;
    for(int lane=0;lane<TS_TRACKER_LANES;++lane) {
        if(lane<b.lane0 || lane>b.lane1) {
            stop_lane(&rt->lanes[lane]);rt->missing_mask&=(uint8_t)~(1u<<lane);
        } else if(lane<old.lane0 || lane>old.lane1)inherit_before(rt,lane,b.row0);
    }
    ++rt->loop_cycles;rt->loop_seam=1;
}

void ts_tracker_playback_pause(TsTrackerPlayback *rt, int paused)
{
    if (rt && rt->running) {
        rt->paused = !!paused;
        for (int i = 0; i < TS_TRACKER_LANES; ++i) rt->lanes[i].changed = 1;
        ts_tracker_playback_end_block(rt);
    }
}
void ts_tracker_playback_solo(TsTrackerPlayback *rt, uint8_t mask) { if (rt) rt->solo_mask = mask; }

static void execute_lane(TsTrackerPlayback *rt, int i, uint32_t row)
{
        rt->lanes[i].source_row = row;
        if (row >= rt->prepared->pattern.rows) return; /* Logical blank tail. */
        TsTrackerLanePlayback *l = &rt->lanes[i];
        const TsTrackerCell *c = &rt->prepared->pattern.cells[row][i];
        if (c->tile_id) l->default_tile = c->tile_id;
        if (c->has_volume) l->volume = c->volume;
        if (c->note_kind == TS_TRACKER_NOTE_OFF) stop_lane(l);
        else if (c->note_kind == TS_TRACKER_NOTE_CUT) {
            stop_lane(l);
            memset(&l->handoff, 0, sizeof(l->handoff));
            l->changed = 0; /* Explicit CUT is sharp; OFF and ordinary stops fade. */
        }
        else if (c->note_kind == TS_TRACKER_NOTE_PITCH) {
            const TsTrackerBinding *b = binding(rt->prepared, l->default_tile);
            if (!b) { stop_lane(l); rt->missing_mask |= (uint8_t)(1u << i); }
            else {
                if (!l->voice.active && !l->handoff.remaining && l->handoff.last.l == 0 && l->handoff.last.r == 0) {
                    const TsTrackerLane *definition = &rt->prepared->lanes[i];
                    l->gain = !definition->muted && (!rt->solo_mask || (rt->solo_mask & (1u << i))) ?
                              definition->trim * l->volume / 64.0f : 0;
                }
                configure_voice(rt, l, b, c->note, 0);
                rt->missing_mask &= (uint8_t)~(1u << i);
                l->last_note_frame = rt->elapsed_frames; ++l->notes_started;
            }
        }
}

static void execute_block_row(TsTrackerPlayback *rt)
{
    for (int i = rt->block.lane0; i <= rt->block.lane1; ++i)
        execute_lane(rt, i, (unsigned)rt->row);
}

static void advance_private(TsTrackerPlayback *rt, int lane)
{
    const TsTrackerLane *definition = &rt->prepared->lanes[lane];
    TsTrackerLanePlayback *l = &rt->lanes[lane];
    unsigned length = private_length(rt->prepared, lane);
    unsigned row = l->source_row % length;
    unsigned cycle = length;
    if (definition->direction == TS_TRACKER_PING_PONG) {
        /* Native extension: no repeated endpoints, and LEN 1 still clocks. */
        cycle = length > 1 ? 2 * (length - 1) : 1;
        if (length == 1) row = 0;
        else {
            if (!row) l->ping_direction = 1;
            else if (row == length - 1) l->ping_direction = -1;
            row = (unsigned)((int)row + l->ping_direction);
        }
    } else if (definition->direction == TS_TRACKER_REVERSE)
        row = row ? row - 1 : length - 1;
    else row = (row + 1) % length;
    if (++l->cycle_steps >= cycle) {
        l->cycle_steps = 0;
        if (private_control(rt->prepared) && lane == rt->prepared->control_lane)
            rt->control_pending = 1;
    }
    execute_lane(rt, lane, row);
}

static void transport_tick(TsTrackerPlayback *rt)
{
    const TsTrackerPrepared *p = rt->prepared;
    if (!rt->transport_started) {
        rt->transport_started = 1;
        rt->tick = rt->row = 0;
        for (int i = 0; i < TS_TRACKER_LANES; ++i) execute_lane(rt, i, 0);
        return;
    }
    int row_advanced = ++rt->tick >= p->ticks_per_line;
    if (row_advanced) {
        rt->tick = 0;
        ++rt->master_row;
    }
    /* Like Tapehead's replayer, a private CONTROL crossing requests the next
       tick-zero boundary. All lanes finish the current tick before it applies. */
    int boundary = private_control(p) ? rt->control_pending :
                   row_advanced && rt->master_row >= shared_length(p);
    if (boundary) {
        if (!p->loop) { ts_tracker_playback_stop(rt); return; }
        rt->master_row = 0; rt->tick = 0; rt->control_pending = 0;
        row_advanced = 1;
        ++rt->loop_cycles;
        for (int i = 0; i < TS_TRACKER_LANES; ++i) rt->lanes[i].cycle_steps = 0;
    }
    rt->row = (int)(rt->master_row % p->pattern.rows);
    for (int i = 0; i < TS_TRACKER_LANES; ++i) {
        if (private_lane(p, i)) {
            TsTrackerLanePlayback *l = &rt->lanes[i];
            unsigned ratio = p->lanes[i].ratio;
            int threshold = ratio_denominator[ratio] * p->ticks_per_line;
            l->accumulator += ratio_numerator[ratio];
            /* All crossings matter: a 5:1 track at TPL 1 can encounter five
               tile/volume/OFF/CUT/note events in one mixer tick. */
            while (l->accumulator >= threshold) {
                l->accumulator -= threshold;
                advance_private(rt, i);
            }
        } else if (row_advanced) execute_lane(rt, i, master_source_row(rt, i));
    }
}

TsStereoFrame ts_tracker_playback_read(TsTrackerPlayback *rt, int rate)
{
    TsStereoFrame output = {0};
    if(rt)rt->loop_seam=0;
    if (!rt || rate < 1000) return output;
    if (!rt->running && !rt->tail_active) return output;
    if (rt->rate && rt->rate != rate) {
        double ratio = (double)rate / rt->rate;
        rt->until_tick *= ratio; rt->tick_frames *= ratio;
        for (int i = 0; i < TS_TRACKER_LANES; ++i) {
            rt->lanes[i].voice.step /= ratio;
            rt->lanes[i].voice.attack_frame = (uint32_t)(rt->lanes[i].voice.attack_frame * ratio);
            rt->lanes[i].voice.attack_frames = (uint32_t)(rt->lanes[i].voice.attack_frames * ratio);
            rt->lanes[i].handoff.remaining = (uint32_t)(rt->lanes[i].handoff.remaining * ratio);
        }
    }
    rt->rate = rate;
    if (rt->running && !rt->paused && rt->prepared) {
        if (rt->until_tick <= 1e-9) {
            if (!rt->block_active) transport_tick(rt);
            else if (++rt->tick >= rt->prepared->ticks_per_line) {
                rt->tick = 0;
                ++rt->row;
                if(rt->row>rt->block.row1 || rt->row>=rt->prepared->pattern.rows)block_seam(rt);
                else if(!rt->elapsed_frames)rt->loop_seam=1;
                execute_block_row(rt);
            }
            rt->until_tick += rt->tick_frames;
        }
        rt->until_tick -= 1;
        ++rt->elapsed_frames;
    }
    int tails = 0;
    for (int i = 0; i < TS_TRACKER_LANES; ++i) {
        TsTrackerLanePlayback *l = &rt->lanes[i];
        const TsTrackerLane *definition = rt->prepared ? &rt->prepared->lanes[i] : NULL;
        float target = definition && !definition->muted && (!rt->solo_mask || (rt->solo_mask & (1u << i))) &&
                       !rt->paused ? definition->trim * l->volume / 64.0f : 0;
        float slew = 2.0f / (rate * 0.005f);
        l->gain += fmaxf(-slew, fminf(slew, target - l->gain));
        int active = l->voice.active;
        TsStereoFrame value = active && !rt->paused ? ts_note_voice_read(&l->voice) : (TsStereoFrame){0};
        if (active && !l->voice.active) stop_lane(l);
        value = ts_voice_handoff_process(&l->handoff, value, l->changed,
                                          (uint32_t)(rate * 0.005));
        l->changed = 0;
        tails |= l->handoff.remaining != 0 || l->voice.active;
        value.l *= l->gain; value.r *= l->gain;
        output.l += value.l; output.r += value.r;
    }
    rt->tail_active = tails;
    return ts_stereo_frame_sanitize(output);
}

void ts_tracker_playback_end_block(TsTrackerPlayback *rt)
{
    atomic_store_explicit(&rt->display_running, rt->running ? (rt->paused ? 2 : 1) : 0, memory_order_relaxed);
    unsigned display_row = rt->row < 0 ? 0 : (unsigned)rt->row;
    unsigned master_row=display_row;
    if(rt->prepared && !rt->block_active && shared_length(rt->prepared)>rt->prepared->pattern.rows)
        master_row=rt->master_row;
    atomic_store_explicit(&rt->display_master_row,master_row,memory_order_relaxed);
    if (rt->prepared && !rt->block_active && !rt->prepared->length_bypass && rt->prepared->control_lane >= 0)
        display_row = rt->lanes[rt->prepared->control_lane].source_row;
    atomic_store_explicit(&rt->display_row, display_row, memory_order_relaxed);
    atomic_store_explicit(&rt->display_pattern, rt->prepared ? rt->prepared->pattern.id : 0, memory_order_relaxed);
    atomic_store_explicit(&rt->display_missing, rt->missing_mask, memory_order_release);
    atomic_store_explicit(&rt->display_block_rows,(unsigned)rt->block.row0|((unsigned)rt->block.row1<<8),memory_order_relaxed);
    atomic_store_explicit(&rt->display_block_lanes,(unsigned)rt->block.lane0|((unsigned)rt->block.lane1<<8),memory_order_relaxed);
    atomic_store_explicit(&rt->display_block,rt->block_active,memory_order_release);
    for (int i = 0; i < TS_TRACKER_LANES; ++i) {
        atomic_store_explicit(&rt->display_lane_row[i], rt->lanes[i].source_row, memory_order_relaxed);
        unsigned phase=0;
        if(rt->prepared && rt->running && !rt->block_active && private_lane(rt->prepared,i)) {
            const TsTrackerLanePlayback *l=&rt->lanes[i];
            int length=(int)private_length(rt->prepared,i);
            int expected=ratio_denominator[rt->prepared->lanes[i].ratio]*rt->tick;
            if(l->source_row==master_row%(unsigned)length && l->accumulator==expected)phase=2;
            else {
                int offset=((int)l->source_row-(int)master_row)%length;
                if(offset<0)offset+=length;
                if(offset>length/2)offset-=length;
                phase=offset<0?1:offset>0?4:5;
            }
        }
        atomic_store_explicit(&rt->display_lane_phase[i],phase,memory_order_relaxed);
    }
}
