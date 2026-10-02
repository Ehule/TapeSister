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
    for (int i = 0; i < TS_TRACKER_LANES; ++i) rt->lanes[i].volume = 0x40;
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

TsTrackerPrepared *ts_tracker_playback_publish(TsTrackerPlayback *rt, TsTrackerPrepared *p)
{
    TsTrackerPrepared *old = rt->prepared;
    rt->prepared = p;
    if (!p) { ts_tracker_playback_stop(rt); return old; }
    if (rt->running) {
        double next = (double)rt->rate * 2.5 / p->bpm;
        if (rt->tick_frames > 0) rt->until_tick *= next / rt->tick_frames;
        rt->tick_frames = next;
        if (rt->tick >= p->ticks_per_line) rt->tick = p->ticks_per_line - 1;
        if (rt->row >= p->pattern.rows) rt->row = p->pattern.rows - 1;
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
        l->gain = rt->prepared->lanes[i].muted || (rt->solo_mask && !(rt->solo_mask & (1u << i))) ?
                  0 : rt->prepared->lanes[i].trim;
    }
    rt->rate = rt->prepared->rate;
    rt->row = -1; rt->tick = rt->prepared->ticks_per_line - 1;
    rt->until_tick = 0; rt->tick_frames = (double)rt->rate * 2.5 / rt->prepared->bpm;
    rt->elapsed_frames = 0; rt->missing_mask = 0;
    rt->running = 1; rt->paused = 0;
    ts_tracker_playback_end_block(rt);
    return 1;
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

static void execute_row(TsTrackerPlayback *rt)
{
    for (int i = 0; i < TS_TRACKER_LANES; ++i) {
        TsTrackerLanePlayback *l = &rt->lanes[i];
        const TsTrackerCell *c = &rt->prepared->pattern.cells[rt->row][i];
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
}

TsStereoFrame ts_tracker_playback_read(TsTrackerPlayback *rt, int rate)
{
    TsStereoFrame output = {0};
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
            if (++rt->tick >= rt->prepared->ticks_per_line) {
                rt->tick = 0;
                if (++rt->row >= rt->prepared->pattern.rows) {
                    if (rt->prepared->loop) rt->row = 0;
                    else ts_tracker_playback_stop(rt);
                }
                if (rt->running) execute_row(rt);
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
    atomic_store_explicit(&rt->display_row, rt->row < 0 ? 0 : (unsigned)rt->row, memory_order_relaxed);
    atomic_store_explicit(&rt->display_pattern, rt->prepared ? rt->prepared->pattern.id : 0, memory_order_relaxed);
    atomic_store_explicit(&rt->display_missing, rt->missing_mask, memory_order_release);
}
