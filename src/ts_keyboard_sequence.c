#include "tapesister/keyboard_sequence.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>

const char *ts_keyboard_sequence_mode_name(int mode)
{
    static const char *names[] = {"UP", "DOWN", "UP/DOWN", "ORDER", "RANDOM"};
    return names[mode >= 0 && mode < TS_KEYBOARD_SEQUENCE_MODE_COUNT ? mode : 0];
}

void ts_keyboard_sequence_settings_default(TsKeyboardSequenceSettings *s)
{
    memset(s, 0, sizeof(*s));
    s->seconds = .25; s->gate = .8; s->loop = 1;
    s->volume = 1; s->lfo_seconds = 4; s->lfo_depth = .5;
}

void ts_keyboard_sequence_init(TsKeyboardSequence *s)
{
    memset(s, 0, sizeof(*s));
    ts_keyboard_sequence_bank_default(&s->bank);
    s->settings = s->bank.slot[0];
    s->gain_current = s->effective_gain = 1;
    s->current_note = -1; s->random = 0x5a17b3u; s->slot_random = 0x9317a5u;
}

static void release_voices(TsKeyboardSequence *s)
{
    memset(s->voices, 0, sizeof(s->voices));
    s->fade_from = s->last; s->fade_remaining = s->fade_frames;
}

static void stop_notes(TsKeyboardSequence *s)
{
    release_voices(s);
    s->running = 0; s->current_note = -1; s->elapsed = 0; s->fresh = 0;
}

static void start_slot_cycle(TsKeyboardSequence *s, int anchor);

int ts_keyboard_sequence_active(const TsKeyboardSequence *s)
{
    return s->running || s->slot_running;
}

void ts_keyboard_sequence_stop(TsKeyboardSequence *s)
{
    stop_notes(s);
    s->slot_running = 0; s->slot_elapsed = 0;
}

static int cycle_length(const TsKeyboardSequence *s)
{
    return s->settings.mode == TS_KEYBOARD_SEQUENCE_UP_DOWN && s->order_count > 1 ?
        s->order_count * 2 - 2 : s->order_count;
}

static double full_pattern_seconds(const TsKeyboardSequence *s)
{
    /* Inner notes round to audio frames. Round the minimum up so a fractional
       note interval cannot shave the end off the last note in a full cycle. */
    double step = s->settings.seconds;
    if (s->slot_rate > 0) step = ceil(step * s->slot_rate) / s->slot_rate;
    return cycle_length(s) * step;
}

static double slot_duration(const TsKeyboardSequence *s)
{
    double duration = s->bank.sequence.seconds;
    if (s->slot_min_full_pattern) duration = fmax(duration, full_pattern_seconds(s));
    return duration;
}

void ts_keyboard_sequence_set_slot_policy(TsKeyboardSequence *s, int minimum_full_pattern)
{
    minimum_full_pattern = minimum_full_pattern != 0;
    if (s->slot_min_full_pattern == minimum_full_pattern) return;
    s->slot_min_full_pattern = minimum_full_pattern;
    if (s->slot_running && minimum_full_pattern) s->slot_duration = fmax(s->slot_duration, slot_duration(s));
}

static int note_at(TsKeyboardSequence *s)
{
    int index = s->cursor;
    if (s->settings.mode == TS_KEYBOARD_SEQUENCE_UP_DOWN && index >= s->order_count)
        index = 2 * s->order_count - 2 - index;
    if (s->settings.mode == TS_KEYBOARD_SEQUENCE_RANDOM) {
        s->random ^= s->random << 13; s->random ^= s->random >> 17; s->random ^= s->random << 5;
        index = (int)(s->random % (unsigned)s->order_count);
    }
    return s->order[index];
}

static void trigger(TsKeyboardSequence *s, int note)
{
    release_voices(s);
    s->current_note = note;
    if (!s->source) return;
    double ratio = exp2((note - TS_KEYBOARD_BASE_NOTE) / 12.0);
    for (int i = 0; i < s->source->count; ++i) {
        s->voices[i] = s->source->voices[i];
        s->voices[i].step *= ratio;
        s->voices[i].midi_note = note;
    }
}

static int play_notes(TsKeyboardSequence *s)
{
    if (!s->order_count || !s->source || !s->source->count) return 0;
    s->running = 1; s->cursor = 0; s->elapsed = 0; s->fresh = 1;
    s->lfo_phase = 0;
    s->current_note = -1;
    return 1;
}

static void reset_notes(TsKeyboardSequence *s)
{
    s->cursor = 0; s->elapsed = 0;
    s->lfo_phase = 0;
    if (s->running) { release_voices(s); s->fresh = 1; }
}

int ts_keyboard_sequence_play(TsKeyboardSequence *s)
{
    if (!play_notes(s)) return 0;
    if (s->bank.sequence.enabled) start_slot_cycle(s, s->active_slot);
    return 1;
}

void ts_keyboard_sequence_reset(TsKeyboardSequence *s)
{
    reset_notes(s);
    if (s->slot_running) start_slot_cycle(s, s->active_slot);
}

void ts_keyboard_sequence_settings_sanitize(TsKeyboardSequenceSettings *settings)
{
    TsKeyboardSequenceSettings next = *settings;
    if (!isfinite(next.seconds)) next.seconds = .25;
    if (next.seconds < TS_KEYBOARD_SEQUENCE_MIN_SECONDS) next.seconds = TS_KEYBOARD_SEQUENCE_MIN_SECONDS;
    if (next.seconds > TS_KEYBOARD_SEQUENCE_MAX_SECONDS) next.seconds = TS_KEYBOARD_SEQUENCE_MAX_SECONDS;
    if (!isfinite(next.gate)) next.gate = .8;
    if (next.gate < .05) next.gate = .05;
    if (next.gate > 1) next.gate = 1;
    if (!isfinite(next.volume)) next.volume = 1;
    next.volume = fmax(0, fmin(2, next.volume));
    if (!isfinite(next.lfo_seconds)) next.lfo_seconds = 4;
    next.lfo_seconds = fmax(TS_KEYBOARD_SEQUENCE_LFO_MIN_SECONDS,
                            fmin(TS_KEYBOARD_SEQUENCE_MAX_SECONDS, next.lfo_seconds));
    if (!isfinite(next.lfo_depth)) next.lfo_depth = .5;
    next.lfo_depth = fmax(0, fmin(1, next.lfo_depth));
    next.lfo_enabled = next.lfo_enabled != 0;
    if (next.mode < 0 || next.mode >= TS_KEYBOARD_SEQUENCE_MODE_COUNT) next.mode = 0;
    next.loop = next.loop != 0;
    int count = 0;
    for (int i = 0; i < next.count && i < TS_KEYBOARD_SEQUENCE_NOTES; ++i) {
        int note = next.notes[i], duplicate = 0;
        if (note < 0 || note > 127) continue;
        for (int j = 0; j < count; ++j) if (next.notes[j] == note) duplicate = 1;
        if (!duplicate) next.notes[count++] = note;
    }
    next.count = count;
    memset(next.notes + count, 0, (TS_KEYBOARD_SEQUENCE_NOTES - count) * sizeof(int));
    *settings = next;
}

static void apply_settings(TsKeyboardSequence *s, const TsKeyboardSequenceSettings *settings)
{
    TsKeyboardSequenceSettings next = *settings;
    ts_keyboard_sequence_settings_sanitize(&next);
    int count = next.count;
    int changed_mode = next.mode != s->settings.mode;
    int old_count = s->order_count;
    int descending = s->settings.mode == TS_KEYBOARD_SEQUENCE_UP_DOWN && s->cursor >= old_count;
    s->settings = next; s->order_count = count;
    memcpy(s->order, next.notes, sizeof(s->order));
    if (next.mode != TS_KEYBOARD_SEQUENCE_ORDER && next.mode != TS_KEYBOARD_SEQUENCE_RANDOM)
        for (int i = 1; i < count; ++i)
        for (int j = i; j > 0 && (next.mode == TS_KEYBOARD_SEQUENCE_DOWN ?
                s->order[j] > s->order[j-1] : s->order[j] < s->order[j-1]); --j) {
            int swap = s->order[j]; s->order[j] = s->order[j-1]; s->order[j-1] = swap;
        }
    if (!count) { stop_notes(s); return; }
    int current = -1;
    for (int i = 0; i < count; ++i) if (s->order[i] == s->current_note) current = i;
    if (changed_mode || (s->running && current < 0)) reset_notes(s);
    else if (current >= 0 && next.mode != TS_KEYBOARD_SEQUENCE_RANDOM) {
        s->cursor = descending && current > 0 && current < count - 1 ?
            2 * count - 2 - current : current;
    } else if (s->cursor >= cycle_length(s)) s->cursor = 0;
}

void ts_keyboard_sequence_set(TsKeyboardSequence *s, const TsKeyboardSequenceSettings *settings)
{
    TsKeyboardSequenceSettings old = s->settings;
    apply_settings(s, settings);
    if (s->slot_running && s->slot_min_full_pattern &&
        (old.count != s->settings.count || old.mode != s->settings.mode || old.seconds != s->settings.seconds ||
         memcmp(old.notes, s->settings.notes, sizeof(old.notes))))
        s->slot_duration = fmax(s->slot_duration, s->slot_elapsed + full_pattern_seconds(s));
    s->bank.slot[s->active_slot] = s->settings;
    ts_keyboard_sequence_bank_sanitize(&s->bank);
    if (!s->bank.order_count) s->slot_running = 0;
}

static uint32_t slot_random(TsKeyboardSequence *s)
{
    s->slot_random ^= s->slot_random << 13;
    s->slot_random ^= s->slot_random >> 17;
    s->slot_random ^= s->slot_random << 5;
    return s->slot_random;
}

/* Bounded plan built only at transport/cycle boundaries. No allocation or I/O. */
static void start_slot_cycle(TsKeyboardSequence *s, int anchor)
{
    int slots[TS_KEYBOARD_SEQUENCE_SLOTS], path[TS_KEYBOARD_SEQUENCE_SLOTS * 2 - 2];
    int count = 0, mode = s->bank.sequence.mode;
    for (int i = 0; i < TS_KEYBOARD_SEQUENCE_SLOTS; ++i) {
        int slot = mode == TS_KEYBOARD_SEQUENCE_ORDER ?
            (i < s->bank.order_count ? s->bank.order[i] : -1) :
            mode == TS_KEYBOARD_SEQUENCE_DOWN ? TS_KEYBOARD_SEQUENCE_SLOTS - 1 - i : i;
        if (slot >= 0 && s->bank.slot[slot].count) slots[count++] = slot;
    }
    if (!count) { s->slot_running = 0; return; }
    if (mode == TS_KEYBOARD_SEQUENCE_RANDOM) {
        for (int i = count - 1; i > 0; --i) {
            int j = (int)(slot_random(s) % (unsigned)(i + 1));
            int swap = slots[i]; slots[i] = slots[j]; slots[j] = swap;
        }
    }
    int length = 0, first = 0;
    for (int i = 0; i < count; ++i) path[length++] = slots[i];
    if (mode == TS_KEYBOARD_SEQUENCE_UP_DOWN)
        for (int i = count - 2; i > 0; --i) path[length++] = slots[i];
    for (int i = 0; i < length; ++i) if (path[i] == anchor) { first = i; break; }
    for (int i = 0; i < length; ++i) s->slot_path[i] = path[(first + i) % length];
    s->slot_count = length; s->slot_cursor = 0;
    s->slot_elapsed = 0; s->slot_duration = slot_duration(s);
    s->slot_running = 1;
}

void ts_keyboard_sequence_set_slot_sequence(TsKeyboardSequence *s, const TsKeyboardSlotSequence *settings)
{
    TsKeyboardSlotSequence old = s->bank.sequence;
    s->bank.sequence = *settings;
    ts_keyboard_sequence_bank_sanitize(&s->bank);
    if (!s->bank.sequence.enabled) s->slot_running = 0;
    else if (ts_keyboard_sequence_active(s) &&
             (!old.enabled || old.mode != s->bank.sequence.mode))
        start_slot_cycle(s, s->active_slot);
    /* TIME changes take effect at the next slot; LOOP at the next cycle end. */
}

static void advance_slot(TsKeyboardSequence *s)
{
    double remainder = fmax(0, s->slot_elapsed - s->slot_duration);
    /* Deleted slots are skipped without a burst of old audio transitions. */
    for (int attempt = 0; attempt < TS_KEYBOARD_SEQUENCE_SLOTS * 2; ++attempt) {
        if (++s->slot_cursor >= s->slot_count) {
            if (!s->bank.sequence.loop) { ts_keyboard_sequence_stop(s); return; }
            int anchor = s->slot_path[0];
            start_slot_cycle(s, anchor);
            if (!s->slot_running) { stop_notes(s); return; }
        }
        int slot = s->slot_path[s->slot_cursor];
        if (!s->bank.slot[slot].count) continue;
        stop_notes(s);
        s->active_slot = slot;
        apply_settings(s, &s->bank.slot[slot]);
        if (!play_notes(s)) { ts_keyboard_sequence_stop(s); return; }
        s->slot_duration = slot_duration(s);
        s->slot_elapsed = fmod(remainder, s->slot_duration);
        return;
    }
    ts_keyboard_sequence_stop(s);
}

static int write_settings(FILE *file, const TsKeyboardSequenceSettings *settings, const char *prefix)
{
    TsKeyboardSequenceSettings s = *settings;
    ts_keyboard_sequence_settings_sanitize(&s);
    if (fprintf(file, "%sMode=%d\n%sLoop=%d\n%sStepSeconds=%.17g\n%sGate=%.17g\n"
        "%sVolume=%.17g\n%sLfoEnabled=%d\n%sLfoSeconds=%.17g\n%sLfoDepth=%.17g\n%sNotes=",
        prefix, s.mode, prefix, s.loop, prefix, s.seconds, prefix, s.gate, prefix, s.volume,
        prefix, s.lfo_enabled, prefix, s.lfo_seconds, prefix, s.lfo_depth, prefix) < 0) return 0;
    for (int i = 0; i < s.count; ++i)
        if (fprintf(file, "%s%d", i ? "," : "", s.notes[i]) < 0) return 0;
    return fputc('\n', file) != EOF;
}

static int read_settings(TsKeyboardSequenceSettings *s, const char *key, const char *value)
{
    if (!strcmp(key, "Arp.Notes")) {
        int notes[TS_KEYBOARD_SEQUENCE_NOTES] = {0}, count = 0;
        const char *cursor = value;
        while (isspace((unsigned char)*cursor)) ++cursor;
        while (*cursor) {
            char *end;
            errno = 0;
            long note = strtol(cursor, &end, 10);
            if (errno || end == cursor || note < 0 || note > 127 || count == TS_KEYBOARD_SEQUENCE_NOTES) return -1;
            notes[count++] = (int)note;
            while (isspace((unsigned char)*end)) ++end;
            if (!*end) break;
            if (*end != ',') return -1;
            cursor = end + 1;
            while (isspace((unsigned char)*cursor)) ++cursor;
            if (!*cursor) return -1;
        }
        memcpy(s->notes, notes, sizeof(notes)); s->count = count;
        return 1;
    }
    int *integer = !strcmp(key, "Arp.Mode") ? &s->mode :
        !strcmp(key, "Arp.Loop") ? &s->loop : !strcmp(key, "Arp.LfoEnabled") ? &s->lfo_enabled : NULL;
    double *number = !strcmp(key, "Arp.StepSeconds") ? &s->seconds :
        !strcmp(key, "Arp.Gate") ? &s->gate : !strcmp(key, "Arp.Volume") ? &s->volume :
        !strcmp(key, "Arp.LfoSeconds") ? &s->lfo_seconds : !strcmp(key, "Arp.LfoDepth") ? &s->lfo_depth : NULL;
    if (!integer && !number) return 0;
    char *end;
    errno = 0;
    if (integer) {
        long parsed = strtol(value, &end, 10);
        if (errno || end == value || parsed < INT_MIN || parsed > INT_MAX) return -1;
        while (isspace((unsigned char)*end)) ++end;
        if (*end) return -1;
        *integer = (int)parsed;
    } else {
        double parsed = strtod(value, &end);
        if (errno || end == value || !isfinite(parsed)) return -1;
        while (isspace((unsigned char)*end)) ++end;
        if (*end) return -1;
        *number = parsed;
    }
    return 1;
}

void ts_keyboard_sequence_bank_default(TsKeyboardSequenceBank *bank)
{
    memset(bank, 0, sizeof(*bank));
    bank->sequence.seconds = 10; bank->sequence.loop = 1;
    for (int i = 0; i < TS_KEYBOARD_SEQUENCE_SLOTS; ++i)
        ts_keyboard_sequence_settings_default(&bank->slot[i]);
}

void ts_keyboard_sequence_bank_sanitize(TsKeyboardSequenceBank *bank)
{
    if (bank->selected < 0 || bank->selected >= TS_KEYBOARD_SEQUENCE_SLOTS) bank->selected = 0;
    for (int i = 0; i < TS_KEYBOARD_SEQUENCE_SLOTS; ++i)
        ts_keyboard_sequence_settings_sanitize(&bank->slot[i]);
    int count = 0, seen = 0;
    for (int i = 0; i < bank->order_count && i < TS_KEYBOARD_SEQUENCE_SLOTS; ++i) {
        int slot = bank->order[i];
        if (slot < 0 || slot >= TS_KEYBOARD_SEQUENCE_SLOTS || !bank->slot[slot].count || (seen & (1 << slot))) continue;
        bank->order[count++] = slot; seen |= 1 << slot;
    }
    for (int i = 0; i < TS_KEYBOARD_SEQUENCE_SLOTS; ++i)
        if (bank->slot[i].count && !(seen & (1 << i))) bank->order[count++] = i;
    bank->order_count = count;
    memset(bank->order + count, 0, (TS_KEYBOARD_SEQUENCE_SLOTS - count) * sizeof(int));
    TsKeyboardSlotSequence *q = &bank->sequence;
    q->enabled = q->enabled != 0; q->loop = q->loop != 0;
    if (q->mode < 0 || q->mode >= TS_KEYBOARD_SEQUENCE_MODE_COUNT) q->mode = 0;
    if (!isfinite(q->seconds)) q->seconds = 10;
    q->seconds = fmax(TS_KEYBOARD_SLOT_MIN_SECONDS, fmin(TS_KEYBOARD_SLOT_MAX_SECONDS, q->seconds));
}

TsKeyboardSequenceBank ts_keyboard_sequence_export(const TsKeyboardSequence *s)
{
    return s->bank; /* Static settings only: automatic selection is runtime state. */
}

void ts_keyboard_sequence_set_bank(TsKeyboardSequence *s, const TsKeyboardSequenceBank *bank)
{
    TsKeyboardSequenceBank next = *bank;
    ts_keyboard_sequence_bank_sanitize(&next);
    ts_keyboard_sequence_stop(s);
    s->bank = next; s->active_slot = next.selected;
    apply_settings(s, &next.slot[next.selected]);
    s->cursor = 0; s->lfo_phase = 0; s->fade_remaining = 0;
    s->last = s->fade_from = (TsStereoFrame){0};
    s->gain_current = s->effective_gain = (float)s->settings.volume;
}

int ts_keyboard_sequence_select_slot(TsKeyboardSequence *s, int slot)
{
    if (slot < 0 || slot >= TS_KEYBOARD_SEQUENCE_SLOTS) return 0;
    s->bank.selected = slot;
    if (slot == s->active_slot) return 1;
    int playing = ts_keyboard_sequence_active(s), chaining = s->slot_running;
    TsKeyboardSequenceSettings next = s->bank.slot[slot];
    if (playing && !next.count && s->settings.count) {
        /* Build a variation without retriggering a note, gate, fade or LFO. */
        s->bank.slot[slot] = s->settings;
        s->active_slot = slot;
        ts_keyboard_sequence_bank_sanitize(&s->bank);
    } else {
        if (!next.count) ts_keyboard_sequence_settings_default(&next);
        stop_notes(s);
        s->active_slot = slot;
        s->cursor = 0; s->lfo_phase = 0;
        ts_keyboard_sequence_set(s, &next);
        if (playing) play_notes(s);
    }
    if (chaining) start_slot_cycle(s, slot);
    return 1;
}

void ts_keyboard_sequence_clear_all(TsKeyboardSequence *s)
{
    TsKeyboardSlotSequence outer = s->bank.sequence;
    ts_keyboard_sequence_stop(s); /* Keep the existing short release fade. */
    ts_keyboard_sequence_bank_default(&s->bank);
    outer.enabled = 0;
    s->bank.sequence = outer;
    s->active_slot = 0;
    apply_settings(s, &s->bank.slot[0]);
    s->cursor = s->slot_cursor = s->slot_count = 0;
    s->slot_duration = s->lfo_phase = 0;
}

void ts_keyboard_sequence_paste(TsKeyboardSequence *s, const TsKeyboardSequenceSettings *settings)
{
    TsKeyboardSequenceSettings next = *settings;
    int playing = ts_keyboard_sequence_active(s), chaining = s->slot_running;
    stop_notes(s);
    s->bank.selected = s->active_slot;
    s->cursor = 0; s->lfo_phase = 0;
    ts_keyboard_sequence_set(s, &next);
    if (playing) play_notes(s);
    if (chaining) start_slot_cycle(s, s->active_slot);
}

int ts_keyboard_sequence_bank_write(FILE *file, const TsKeyboardSequenceBank *bank)
{
    TsKeyboardSequenceBank b = *bank;
    ts_keyboard_sequence_bank_sanitize(&b);
    if (fprintf(file, "Arp.SelectedSlot=%d\nArp.Sequence.Enabled=%d\nArp.Sequence.Mode=%d\n"
        "Arp.Sequence.Loop=%d\nArp.Sequence.Seconds=%.17g\nArp.SlotOrder=",
        b.selected, b.sequence.enabled, b.sequence.mode, b.sequence.loop, b.sequence.seconds) < 0) return 0;
    for (int i = 0; i < b.order_count; ++i)
        if (fprintf(file, "%s%d", i ? "," : "", b.order[i]) < 0) return 0;
    if (fputc('\n', file) == EOF) return 0;
    for (int i = 0; i < TS_KEYBOARD_SEQUENCE_SLOTS; ++i) {
        char prefix[32]; snprintf(prefix, sizeof(prefix), "Arp.Slot.%d.", i);
        if (!write_settings(file, &b.slot[i], prefix)) return 0;
    }
    return 1;
}

int ts_keyboard_sequence_bank_read(TsKeyboardSequenceBank *bank, const char *key, const char *value)
{
    if (!strcmp(key, "Arp.SlotOrder")) {
        TsKeyboardSequenceSettings parsed = {0};
        if (read_settings(&parsed, "Arp.Notes", value) != 1 || parsed.count > TS_KEYBOARD_SEQUENCE_SLOTS) return -1;
        for (int i = 0; i < parsed.count; ++i) if (parsed.notes[i] >= TS_KEYBOARD_SEQUENCE_SLOTS) return -1;
        memcpy(bank->order, parsed.notes, parsed.count * sizeof(int)); bank->order_count = parsed.count;
        return 1;
    }
    if (!strncmp(key, "Arp.Sequence.", 13)) {
        TsKeyboardSequenceSettings parsed = {0};
        const char *name = key + 13;
        int *target = !strcmp(name, "Enabled") ? &bank->sequence.enabled :
            !strcmp(name, "Mode") ? &bank->sequence.mode : !strcmp(name, "Loop") ? &bank->sequence.loop : NULL;
        if (target) {
            int result = read_settings(&parsed, "Arp.Mode", value);
            if (result == 1) *target = parsed.mode;
            return result;
        }
        if (!strcmp(name, "Seconds")) {
            int result = read_settings(&parsed, "Arp.StepSeconds", value);
            if (result == 1) bank->sequence.seconds = parsed.seconds;
            return result;
        }
        return 0;
    }
    if (!strcmp(key, "Arp.SelectedSlot")) {
        TsKeyboardSequenceSettings parsed = {0};
        int result = read_settings(&parsed, "Arp.Mode", value);
        if (result == 1) bank->selected = parsed.mode;
        return result;
    }
    if (!strncmp(key, "Arp.Slot.", 9)) {
        char *end; errno = 0;
        long slot = strtol(key + 9, &end, 10);
        if (errno || end == key + 9 || *end != '.' || slot < 0) return -1;
        if (slot >= TS_KEYBOARD_SEQUENCE_SLOTS) return 0; /* Future extra slots. */
        char setting[96];
        if (snprintf(setting, sizeof(setting), "Arp.%s", end + 1) >= (int)sizeof(setting)) return -1;
        return read_settings(&bank->slot[slot], setting, value);
    }
    return read_settings(&bank->slot[0], key, value);
}

int ts_keyboard_sequence_toggle(TsKeyboardSequence *s, int note)
{
    TsKeyboardSequenceSettings next = s->settings;
    if (note < 0 || note > 127) return 0;
    for (int i = 0; i < next.count; ++i) if (next.notes[i] == note) {
        memmove(next.notes + i, next.notes + i + 1, (size_t)(--next.count - i) * sizeof(int));
        ts_keyboard_sequence_set(s, &next); return 1;
    }
    if (next.count == TS_KEYBOARD_SEQUENCE_NOTES) return 0;
    next.notes[next.count++] = note; ts_keyboard_sequence_set(s, &next); return 1;
}

TsKeyboardSequenceSource *ts_keyboard_sequence_source(TsKeyboardSequence *s, TsKeyboardSequenceSource *source)
{
    TsKeyboardSequenceSource *old = s->source;
    s->source = source;
    if (!source || !source->count) ts_keyboard_sequence_stop(s);
    else if (s->running && s->current_note >= 0) trigger(s, s->current_note);
    return old;
}

void ts_keyboard_sequence_source_free(TsKeyboardSequenceSource *source)
{
    if (!source) return;
    if (source->group) { ts_performance_free(source->group); free(source->group); }
    ts_sample_free(&source->sample); free(source);
}

TsStereoFrame ts_keyboard_sequence_read(TsKeyboardSequence *s, int rate)
{
    TsStereoFrame out = {0};
    if (rate <= 0) { ts_keyboard_sequence_stop(s); s->last = out; return out; }
    s->fade_frames = (unsigned)(rate / 200); /* 5 ms boundary/Stop de-click. */
    if (s->slot_rate != rate) {
        s->slot_rate = rate;
        if (s->slot_running && s->slot_min_full_pattern)
            s->slot_duration = fmax(s->slot_duration, slot_duration(s));
    }
    if (s->slot_running && s->slot_elapsed + .5 / rate >= s->slot_duration) advance_slot(s);
    if (s->slot_running) s->slot_elapsed += 1.0 / rate;
    if (s->running) {
        if (s->fresh) { trigger(s, note_at(s)); s->fresh = 0; }
        else if (s->elapsed + .5 / rate >= s->settings.seconds) {
            if (++s->cursor >= cycle_length(s)) {
                if (s->settings.loop) s->cursor = 0;
                else { stop_notes(s); goto fade; }
            }
            s->elapsed = 0; trigger(s, note_at(s));
        }
        double remaining = s->settings.seconds * s->settings.gate - s->elapsed;
        if (remaining > 0 && s->source) {
            float gate = (float)fmin(1.0, remaining / .005);
            for (int i = 0; i < s->source->count; ++i) {
                TsStereoFrame frame = ts_note_voice_read(&s->voices[i]);
                out.l += frame.l * gate; out.r += frame.r * gate;
            }
        }
        s->elapsed += 1.0 / rate;
    }
fade:
    if (s->fade_remaining && s->fade_frames) {
        float from = (float)s->fade_remaining / s->fade_frames;
        out.l = out.l * (1 - from) + s->fade_from.l * from;
        out.r = out.r * (1 - from) + s->fade_from.r * from;
        --s->fade_remaining;
    }
    s->last = ts_stereo_frame_sanitize(out);
    /* Keep boundary history unscaled: applying gain before that history would
       attenuate it repeatedly at note changes. This stage owns only ARP voices. */
    double slew = 2.0 / (.005 * rate);
    double depth = s->settings.lfo_enabled ? s->settings.lfo_depth : 0;
    double gain = s->settings.volume;
    if (depth > 0) gain *= 1 - depth * (.5 - .5 * cos(6.283185307179586 * s->lfo_phase));
    /* Also smooth LFO enable/depth/reset, without slowing ordinary sine motion. */
    s->gain_current += fmax(-slew, fmin(slew, gain - s->gain_current));
    s->effective_gain = (float)s->gain_current;
    if (s->running) {
        s->lfo_phase += 1.0 / (rate * s->settings.lfo_seconds);
        s->lfo_phase -= floor(s->lfo_phase);
    }
    out.l = s->last.l * s->effective_gain;
    out.r = s->last.r * s->effective_gain;
    return ts_stereo_frame_sanitize(out);
}

uint32_t ts_keyboard_sequence_mask(const TsKeyboardSequenceSettings *settings, int base)
{
    uint32_t mask = 0;
    for (int i = 0; i < settings->count; ++i) {
        int n = settings->notes[i] - base;
        if (n >= 0 && n < 24) mask |= 1u << n;
    }
    return mask;
}
