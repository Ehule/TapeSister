#include "tapesister/keyboard_sequence.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

const char *ts_keyboard_sequence_mode_name(int mode)
{
    static const char *names[] = {"UP", "DOWN", "UP/DOWN", "ORDER", "RANDOM"};
    return names[mode >= 0 && mode < TS_KEYBOARD_SEQUENCE_MODE_COUNT ? mode : 0];
}

void ts_keyboard_sequence_init(TsKeyboardSequence *s)
{
    memset(s, 0, sizeof(*s));
    s->settings.seconds = .25; s->settings.gate = .8; s->settings.loop = 1;
    s->settings.volume = s->gain_current = s->effective_gain = 1;
    s->settings.lfo_seconds = 4; s->settings.lfo_depth = .5;
    s->current_note = -1; s->random = 0x5a17b3u;
}

static void release_voices(TsKeyboardSequence *s)
{
    memset(s->voices, 0, sizeof(s->voices));
    s->fade_from = s->last; s->fade_remaining = s->fade_frames;
}

void ts_keyboard_sequence_stop(TsKeyboardSequence *s)
{
    release_voices(s);
    s->running = 0; s->current_note = -1; s->elapsed = 0; s->fresh = 0;
}

static int cycle_length(const TsKeyboardSequence *s)
{
    return s->settings.mode == TS_KEYBOARD_SEQUENCE_UP_DOWN && s->order_count > 1 ?
        s->order_count * 2 - 2 : s->order_count;
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

int ts_keyboard_sequence_play(TsKeyboardSequence *s)
{
    if (!s->order_count || !s->source || !s->source->count) return 0;
    s->running = 1; s->cursor = 0; s->elapsed = 0; s->fresh = 1;
    s->lfo_phase = 0;
    s->current_note = -1;
    return 1;
}

void ts_keyboard_sequence_reset(TsKeyboardSequence *s)
{
    s->cursor = 0; s->elapsed = 0;
    s->lfo_phase = 0;
    if (s->running) { release_voices(s); s->fresh = 1; }
}

void ts_keyboard_sequence_set(TsKeyboardSequence *s, const TsKeyboardSequenceSettings *settings)
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
    if (!count) { ts_keyboard_sequence_stop(s); return; }
    int current = -1;
    for (int i = 0; i < count; ++i) if (s->order[i] == s->current_note) current = i;
    if (changed_mode || (s->running && current < 0)) ts_keyboard_sequence_reset(s);
    else if (current >= 0 && next.mode != TS_KEYBOARD_SEQUENCE_RANDOM) {
        s->cursor = descending && current > 0 && current < count - 1 ?
            2 * count - 2 - current : current;
    } else if (s->cursor >= cycle_length(s)) s->cursor = 0;
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
    if (s->running) {
        if (s->fresh) { trigger(s, note_at(s)); s->fresh = 0; }
        else if (s->elapsed + .5 / rate >= s->settings.seconds) {
            if (++s->cursor >= cycle_length(s)) {
                if (s->settings.loop) s->cursor = 0;
                else { ts_keyboard_sequence_stop(s); goto fade; }
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
