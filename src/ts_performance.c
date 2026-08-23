#include "tapesister/performance.h"

#include <math.h>
#include <string.h>

static int voice_range_from_slot(const TsBankSlot *slot,
                                 size_t *first, size_t *last,
                                 size_t *crossfade_frames)
{
    size_t made_crossfade = 0u;
    if (slot == NULL || !slot->occupied || slot->sample.data == NULL ||
        slot->sample.frames < 2u || slot->sample.sample_rate == 0u)
        return 0;
    *first = 0u;
    *last = slot->sample.frames;
    if (slot->has_loop && slot->loop_last > slot->loop_first + 1u &&
        slot->loop_last <= slot->sample.frames) {
        *first = slot->loop_first;
        *last = slot->loop_last;
        if (slot->loop_crossfade_ms > 0.0f) {
            made_crossfade = (size_t)llround(
                (double)slot->sample.sample_rate *
                (double)slot->loop_crossfade_ms / 1000.0);
            if (made_crossfade > (*last - *first) / 4u)
                made_crossfade = (*last - *first) / 4u;
        }
    }
    *crossfade_frames = made_crossfade;
    return 1;
}

static int find_voice(TsPerformanceBank *bank, int source_slot,
                      const TsNoteEvent *event)
{
    int free_voice = -1;
    for (int i = 0; i < TS_PERFORMANCE_VOICE_LIMIT; ++i) {
        TsPerformanceVoice *voice = &bank->voices[i];
        if (voice->active && voice->source_slot == source_slot &&
            event != NULL && voice->midi_note == event->midi_note &&
            ts_note_event_same_trigger(event, voice->origin, voice->note,
                                       voice->channel))
            return i;
        if (!voice->active && free_voice < 0) free_voice = i;
    }
    return free_voice;
}

static int start_slot_event(TsPerformanceBank *bank, const TsBankSlot *slot,
                            int source_slot, const TsNoteEvent *event,
                            int latched, int output_rate)
{
    TsPerformanceVoice *voice;
    size_t first, last, crossfade;
    int index;
    if (bank == NULL || slot == NULL || event == NULL || output_rate <= 0 ||
        event->key < 0 || event->midi_note < 0 || event->midi_note > 127 ||
        event->velocity <= 0 || event->velocity > 127 ||
        !voice_range_from_slot(slot, &first, &last, &crossfade))
        return 0;
    index = find_voice(bank, source_slot, event);
    if (index < 0) return 0;
    voice = &bank->voices[index];
    if (voice->active && latched && voice->latched) {
        voice->active = 0;
        return 1;
    }
    memset(voice, 0, sizeof(*voice));
    voice->sample = &slot->sample;
    voice->range_first = first;
    voice->range_last = last;
    voice->looping = slot->has_loop && last > first + 1u;
    voice->loop_mode = slot->loop_mode;
    voice->direction = voice->loop_mode == TS_LOOP_REVERSE ? -1 : 1;
    voice->position = voice->looping && voice->direction < 0 ?
                      (double)(last - 1u) : (double)first;
    voice->step = (double)slot->sample.sample_rate / (double)output_rate *
                  ts_tuning_note_pitch(&slot->audible_tuning,
                                       event->midi_note - TS_KEYBOARD_BASE_NOTE);
    voice->crossfade_frames = crossfade;
    voice->attack_frames = ts_audition_attack_frames(
        output_rate, bank->attack_ms);
    voice->origin = event->origin;
    voice->note = event->key;
    voice->midi_note = event->midi_note;
    voice->channel = event->channel;
    voice->source_slot = source_slot;
    voice->gain = ts_note_event_gain(event);
    voice->latched = latched != 0;
    voice->active = 1;
    return 1;
}

void ts_performance_init(TsPerformanceBank *bank)
{
    if (bank != NULL) {
        memset(bank, 0, sizeof(*bank));
        bank->attack_ms = TS_AUDITION_ATTACK_MS_DEFAULT;
    }
}

void ts_performance_clear(TsPerformanceBank *bank)
{
    int attack_ms;
    if (bank == NULL) return;
    attack_ms = bank->attack_ms;
    memset(bank, 0, sizeof(*bank));
    ts_performance_set_attack_ms(bank, attack_ms);
}

void ts_performance_set_attack_ms(TsPerformanceBank *bank, int milliseconds)
{
    if (bank == NULL) return;
    if (milliseconds < TS_AUDITION_ATTACK_MS_MIN)
        milliseconds = TS_AUDITION_ATTACK_MS_MIN;
    if (milliseconds > TS_AUDITION_ATTACK_MS_MAX)
        milliseconds = TS_AUDITION_ATTACK_MS_MAX;
    bank->attack_ms = milliseconds;
}

void ts_performance_release_sources_after_pass(TsPerformanceBank *bank,
                                               uint16_t source_mask)
{
    if (bank == NULL || source_mask == 0u) return;
    for (int i = 0; i < TS_PERFORMANCE_VOICE_LIMIT; ++i) {
        TsPerformanceVoice *voice = &bank->voices[i];
        if (!voice->active || voice->source_slot < 0 ||
            voice->source_slot >= TS_BANK_SLOT_COUNT ||
            (source_mask & (uint16_t)(1u << voice->source_slot)) == 0u)
            continue;
        /* A plain tile click unlatches the temporary instrument without
           chopping off its sound. One-shots keep running; looped voices finish
           the traversal already in progress, then stop at that boundary. */
        voice->latched = 0;
        voice->releasing = 1;
        voice->looping = 0;
        voice->crossfade_frames = 0u;
    }
}

void ts_performance_release_after_pass(TsPerformanceBank *bank)
{
    ts_performance_release_sources_after_pass(bank, UINT16_MAX);
}

void ts_performance_release(TsPerformanceBank *bank, int note)
{
    TsNoteEvent event;
    if (!ts_note_event_qwerty(&event, note, TS_KEYBOARD_BASE_NOTE)) return;
    ts_performance_release_event(bank, &event);
}

void ts_performance_release_event(TsPerformanceBank *bank,
                                  const TsNoteEvent *event)
{
    if (bank == NULL) return;
    for (int i = 0; i < TS_PERFORMANCE_VOICE_LIMIT; ++i) {
        TsPerformanceVoice *voice = &bank->voices[i];
        if (voice->active && !voice->latched && !voice->releasing &&
            event != NULL &&
            ts_note_event_same_trigger(event, voice->origin, voice->note,
                                       voice->channel))
            voice->active = 0;
    }
}

void ts_performance_release_midi_channel(TsPerformanceBank *bank, int channel)
{
    if (bank == NULL || channel < 0 || channel > 15) return;
    for (int i = 0; i < TS_PERFORMANCE_VOICE_LIMIT; ++i) {
        TsPerformanceVoice *voice = &bank->voices[i];
        if (voice->active && voice->origin == TS_NOTE_ORIGIN_MIDI &&
            voice->channel == channel)
            voice->active = 0;
    }
}

int ts_performance_trigger_group(TsPerformanceBank *bank,
                                 const TsInstrument *instrument,
                                 uint16_t source_mask,
                                 int note,
                                 int keyboard_base_note,
                                 int latched,
                                 int output_rate)
{
    TsNoteEvent event;
    if (!ts_note_event_qwerty(&event, note, keyboard_base_note)) return 0;
    return ts_performance_trigger_group_event(bank, instrument, source_mask,
                                              &event, latched, output_rate);
}

int ts_performance_trigger_group_event(TsPerformanceBank *bank,
                                       const TsInstrument *instrument,
                                       uint16_t source_mask,
                                       const TsNoteEvent *event,
                                       int latched,
                                       int output_rate)
{
    int started = 0;
    if (bank == NULL || instrument == NULL || source_mask == 0u || event == NULL)
        return 0;
    for (int slot = 0; slot < TS_BANK_SLOT_COUNT; ++slot) {
        if ((source_mask & (uint16_t)(1u << slot)) == 0u) continue;
        if (start_slot_event(bank, &instrument->bank[slot], slot, event,
                             latched, output_rate))
            ++started;
    }
    return started;
}

int ts_performance_trigger_staged(TsPerformanceBank *bank,
                                  const TsInstrument *instrument,
                                  uint16_t source_mask,
                                  uint32_t staged_notes,
                                  int keyboard_base_note,
                                  int output_rate)
{
    int started = 0;
    if (bank == NULL || instrument == NULL || source_mask == 0u ||
        staged_notes == 0u) return 0;
    ts_performance_clear(bank);
    for (int note = 0; note < 24; ++note) {
        if ((staged_notes & (1u << note)) == 0u) continue;
        started += ts_performance_trigger_group(bank, instrument, source_mask,
                                                note, keyboard_base_note, 1,
                                                output_rate);
    }
    return started;
}

float ts_performance_read(TsPerformanceBank *bank, float *raw_mix)
{
    float mixed = 0.0f;
    int count = 0;
    if (raw_mix != NULL) *raw_mix = 0.0f;
    if (bank == NULL) return 0.0f;
    for (int i = 0; i < TS_PERFORMANCE_VOICE_LIMIT; ++i) {
        TsPerformanceVoice *voice = &bank->voices[i];
        float value;
        if (!voice->active) continue;
        if (voice->sample == NULL || voice->sample->data == NULL ||
            voice->sample->frames < 2u) {
            voice->active = 0;
            continue;
        }
        if (voice->looping) {
            voice->position = ts_audition_loop_position(
                voice->position, voice->range_first, voice->range_last,
                voice->crossfade_frames, voice->loop_mode, &voice->direction);
            value = ts_audition_read_looped_mode(
                voice->sample, voice->position, voice->range_first,
                voice->range_last, voice->crossfade_frames, voice->loop_mode);
        } else {
            size_t at;
            if ((voice->direction >= 0 &&
                 voice->position + 1.0 >= (double)voice->range_last) ||
                (voice->direction < 0 &&
                 voice->position <= (double)voice->range_first)) {
                voice->active = 0;
                continue;
            }
            at = voice->position > 0.0 ? (size_t)voice->position : 0u;
            if (at + 1u >= voice->sample->frames) {
                voice->active = 0;
                continue;
            }
            {
                float fraction = (float)(voice->position - (double)at);
                value = voice->sample->data[at] +
                        (voice->sample->data[at + 1u] - voice->sample->data[at]) *
                        fraction;
            }
        }
        value *= voice->gain * ts_audition_attack_gain(
            voice->attack_frame, voice->attack_frames);
        if (voice->attack_frame < voice->attack_frames)
            ++voice->attack_frame;
        voice->position += voice->step * voice->direction;
        mixed += value;
        ++count;
    }
    if (raw_mix != NULL) *raw_mix = mixed;
    /* Monitoring follows TapeSister's established polyphonic headroom curve.
       Capture receives raw_mix unchanged and only gets one peak-safe gain pass
       if the completed performance actually exceeds the safe output range. */
    return count > 0 ? mixed / sqrtf((float)count) : 0.0f;
}

void ts_performance_sync(TsPerformanceBank *bank,
                         const TsInstrument *instrument,
                         int output_rate)
{
    if (bank == NULL || instrument == NULL || output_rate <= 0) return;
    for (int i = 0; i < TS_PERFORMANCE_VOICE_LIMIT; ++i) {
        TsPerformanceVoice *voice = &bank->voices[i];
        const TsBankSlot *slot;
        size_t first, last, crossfade;
        double progress;
        if (!voice->active || voice->source_slot < 0 ||
            voice->source_slot >= TS_BANK_SLOT_COUNT) continue;
        slot = &instrument->bank[voice->source_slot];
        if (!voice_range_from_slot(slot, &first, &last, &crossfade)) {
            voice->active = 0;
            continue;
        }
        progress = voice->range_last > voice->range_first ?
            (voice->position - (double)voice->range_first) /
            (double)(voice->range_last - voice->range_first) : 0.0;
        if (progress < 0.0) progress = 0.0;
        if (progress > 1.0) progress = 1.0;
        voice->sample = &slot->sample;
        voice->range_first = first;
        voice->range_last = last;
        voice->position = (double)first + progress * (double)(last - first);
        if (voice->position >= (double)last)
            voice->position = slot->loop_mode == TS_LOOP_REVERSE ?
                              (double)(last - 1u) : (double)first;
        voice->looping = voice->releasing ? 0 : slot->has_loop;
        voice->loop_mode = slot->loop_mode;
        if (!voice->releasing)
            voice->direction = voice->loop_mode == TS_LOOP_REVERSE ? -1 : 1;
        voice->crossfade_frames = voice->releasing ? 0u : crossfade;
        voice->step = (double)slot->sample.sample_rate / (double)output_rate *
                      ts_tuning_note_pitch(&slot->audible_tuning,
                                           voice->midi_note - TS_KEYBOARD_BASE_NOTE);
    }
}

int ts_performance_count(const TsPerformanceBank *bank)
{
    int count = 0;
    if (bank == NULL) return 0;
    for (int i = 0; i < TS_PERFORMANCE_VOICE_LIMIT; ++i)
        if (bank->voices[i].active) ++count;
    return count;
}

uint32_t ts_performance_visible_mask(const TsPerformanceBank *bank,
                                     int keyboard_base_note)
{
    uint32_t mask = 0u;
    if (bank == NULL) return 0u;
    for (int i = 0; i < TS_PERFORMANCE_VOICE_LIMIT; ++i) {
        const TsPerformanceVoice *voice = &bank->voices[i];
        int visible_note;
        if (!voice->active) continue;
        visible_note = voice->midi_note - keyboard_base_note;
        if (visible_note >= 0 && visible_note < 24)
            mask |= 1u << visible_note;
    }
    return mask;
}

int ts_performance_source_count(uint16_t source_mask)
{
    int count = 0;
    while (source_mask != 0u) {
        count += (int)(source_mask & 1u);
        source_mask >>= 1u;
    }
    return count;
}

float ts_performance_peak_scale(float *samples, size_t frames, float safe_peak)
{
    float peak = 0.0f;
    float gain = 1.0f;
    if (samples == NULL || frames == 0u) return 1.0f;
    if (!(safe_peak > 0.0f && safe_peak <= 1.0f)) safe_peak = 0.98f;
    for (size_t i = 0; i < frames; ++i) {
        float value = isfinite(samples[i]) ? samples[i] : 0.0f;
        float level;
        samples[i] = value;
        level = fabsf(value);
        if (level > peak) peak = level;
    }
    if (peak <= safe_peak || peak <= 0.0f) return 1.0f;
    gain = safe_peak / peak;
    for (size_t i = 0; i < frames; ++i) samples[i] *= gain;
    return gain;
}
