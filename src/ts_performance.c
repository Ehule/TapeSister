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
                      int note, int midi_note)
{
    int free_voice = -1;
    for (int i = 0; i < TS_PERFORMANCE_VOICE_LIMIT; ++i) {
        TsPerformanceVoice *voice = &bank->voices[i];
        if (voice->active && voice->source_slot == source_slot &&
            voice->note == note && voice->midi_note == midi_note)
            return i;
        if (!voice->active && free_voice < 0) free_voice = i;
    }
    return free_voice;
}

static int start_slot(TsPerformanceBank *bank, const TsBankSlot *slot,
                      int source_slot, int note, int keyboard_base_note,
                      int latched, int output_rate)
{
    TsPerformanceVoice *voice;
    size_t first, last, crossfade;
    int index;
    int midi_note = keyboard_base_note + note;
    if (bank == NULL || slot == NULL || output_rate <= 0 || note < 0 || note >= 24 ||
        keyboard_base_note < 0 || midi_note > 127 ||
        !voice_range_from_slot(slot, &first, &last, &crossfade))
        return 0;
    index = find_voice(bank, source_slot, note, midi_note);
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
                                       midi_note - TS_KEYBOARD_BASE_NOTE);
    voice->crossfade_frames = crossfade;
    voice->note = note;
    voice->midi_note = midi_note;
    voice->source_slot = source_slot;
    voice->latched = latched != 0;
    voice->active = 1;
    return 1;
}

void ts_performance_init(TsPerformanceBank *bank)
{
    if (bank != NULL) memset(bank, 0, sizeof(*bank));
}

void ts_performance_clear(TsPerformanceBank *bank)
{
    ts_performance_init(bank);
}

void ts_performance_release(TsPerformanceBank *bank, int note)
{
    if (bank == NULL) return;
    for (int i = 0; i < TS_PERFORMANCE_VOICE_LIMIT; ++i) {
        TsPerformanceVoice *voice = &bank->voices[i];
        if (voice->active && !voice->latched && voice->note == note)
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
    int started = 0;
    if (bank == NULL || instrument == NULL || source_mask == 0u) return 0;
    for (int slot = 0; slot < TS_BANK_SLOT_COUNT; ++slot) {
        if ((source_mask & (uint16_t)(1u << slot)) == 0u) continue;
        if (start_slot(bank, &instrument->bank[slot], slot, note,
                       keyboard_base_note, latched, output_rate))
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
        if (!voice->active || voice->sample == NULL || voice->sample->data == NULL)
            continue;
        if (voice->looping) {
            voice->position = ts_audition_loop_position(
                voice->position, voice->range_first, voice->range_last,
                voice->crossfade_frames, voice->loop_mode, &voice->direction);
            value = ts_audition_read_looped_mode(
                voice->sample, voice->position, voice->range_first,
                voice->range_last, voice->crossfade_frames, voice->loop_mode);
        } else {
            size_t at = voice->position > 0.0 ? (size_t)voice->position : 0u;
            if (at + 1u >= voice->range_last || at + 1u >= voice->sample->frames) {
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
        voice->position += voice->step * voice->direction;
        mixed += value;
        ++count;
    }
    if (raw_mix != NULL) *raw_mix = mixed;
    /* Monitoring gets deterministic headroom. Capture receives raw_mix and
       applies one peak-safe gain pass only when the committed mix needs it. */
    return count > 0 ? mixed / (float)count : 0.0f;
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
            voice->position = voice->loop_mode == TS_LOOP_REVERSE ?
                              (double)(last - 1u) : (double)first;
        voice->looping = slot->has_loop;
        voice->loop_mode = slot->loop_mode;
        voice->direction = voice->loop_mode == TS_LOOP_REVERSE ? -1 : 1;
        voice->crossfade_frames = crossfade;
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
