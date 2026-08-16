#include "tapesister/note_bank.h"

#include <math.h>
#include <string.h>

static int voice_plan(const TsInstrument *instrument, TsAuditionSource source,
                      int looping, TsAuditionPlan *plan)
{
    return ts_audition_plan(instrument, source,
                            looping && instrument->has_loop ?
                            TS_AUDITION_LOOP : TS_AUDITION_NOTE, plan);
}

static void update_voice(TsNoteVoice *voice, const TsInstrument *instrument,
                         const TsTuning *tuning, TsAuditionSource source,
                         int output_rate)
{
    TsAuditionPlan plan;
    int looping = instrument->has_loop;
    size_t old_first = voice->range_first;
    size_t old_last = voice->range_last;
    double old_position = voice->position;
    if (!voice_plan(instrument, source, looping, &plan) || output_rate <= 0) {
        voice->active = 0;
        return;
    }
    voice->position = ts_audition_map_progress(old_position, old_first, old_last,
                                               plan.first, plan.last);
    if (voice->position >= (double)plan.last) voice->position = (double)plan.first;
    voice->sample = plan.sample;
    voice->range_first = plan.first;
    voice->range_last = plan.last;
    voice->source = source;
    voice->looping = looping;
    voice->loop_mode = instrument->loop_mode;
    if (voice->loop_mode == TS_LOOP_REVERSE) voice->direction = -1;
    else if (voice->loop_mode == TS_LOOP_FORWARD) voice->direction = 1;
    else if (voice->direction == 0) voice->direction = 1;
    voice->crossfade_frames = voice->looping ?
                              ts_audition_crossfade_frames(
                                  &plan, instrument->loop_crossfade_ms) : 0;
    voice->pitch = ts_tuning_note_pitch(
        tuning, voice->midi_note - TS_KEYBOARD_BASE_NOTE);
    voice->step = (double)plan.sample->sample_rate / output_rate * voice->pitch;
}

void ts_note_bank_init(TsNoteBank *bank)
{
    if (bank != NULL) memset(bank, 0, sizeof(*bank));
}

void ts_note_bank_clear(TsNoteBank *bank)
{
    ts_note_bank_init(bank);
}

void ts_note_bank_clear_latched(TsNoteBank *bank)
{
    if (bank == NULL) return;
    for (int i = 0; i < TS_NOTE_VOICE_LIMIT; ++i)
        if (bank->voices[i].latched) bank->voices[i].active = 0;
}

TsNoteStartResult ts_note_bank_start(TsNoteBank *bank, const TsInstrument *instrument,
                                     TsAuditionSource source, int note, int latched,
                                     int output_rate)
{
    return ts_note_bank_start_tuned(bank, instrument,
                                    instrument != NULL ? &instrument->tuning : NULL,
                                    source, note, latched, output_rate);
}

TsNoteStartResult ts_note_bank_start_tuned(TsNoteBank *bank,
                                           const TsInstrument *instrument,
                                           const TsTuning *tuning,
                                           TsAuditionSource source, int note,
                                           int latched, int output_rate)
{
    return ts_note_bank_start_tuned_at(bank, instrument, tuning, source, note,
                                       TS_KEYBOARD_BASE_NOTE, latched, output_rate);
}

TsNoteStartResult ts_note_bank_start_tuned_at(TsNoteBank *bank,
                                              const TsInstrument *instrument,
                                              const TsTuning *tuning,
                                              TsAuditionSource source, int note,
                                              int keyboard_base_note,
                                              int latched, int output_rate)
{
    TsAuditionPlan plan;
    int free_voice = -1;
    if (bank == NULL || instrument == NULL || note < 0 || note >= 24 ||
        keyboard_base_note < 0 || keyboard_base_note + note > 127 ||
        tuning == NULL || output_rate <= 0 ||
        !voice_plan(instrument, source, instrument->has_loop, &plan))
        return TS_NOTE_START_FAILED;
    for (int i = 0; i < TS_NOTE_VOICE_LIMIT; ++i) {
        TsNoteVoice *voice = &bank->voices[i];
        if (voice->active && voice->note == note &&
            voice->midi_note == keyboard_base_note + note) {
            if (latched && voice->latched) {
                voice->active = 0;
                return TS_NOTE_TOGGLED_OFF;
            }
            if (voice->latched) return TS_NOTE_STARTED;
            free_voice = i;
            break;
        }
        if (!voice->active && free_voice < 0) free_voice = i;
    }
    if (free_voice < 0) return TS_NOTE_LIMIT_REACHED;
    {
        TsNoteVoice *voice = &bank->voices[free_voice];
        memset(voice, 0, sizeof(*voice));
        voice->sample = plan.sample;
        voice->position = instrument->loop_mode == TS_LOOP_REVERSE &&
                          instrument->has_loop ? (double)(plan.last - 1u) :
                          (double)plan.first;
        voice->pitch = ts_tuning_note_pitch(
            tuning, keyboard_base_note + note - TS_KEYBOARD_BASE_NOTE);
        voice->step = (double)plan.sample->sample_rate / output_rate * voice->pitch;
        voice->range_first = plan.first;
        voice->range_last = plan.last;
        voice->source = source;
        voice->serial = ++bank->next_serial;
        voice->note = note;
        voice->midi_note = keyboard_base_note + note;
        voice->looping = instrument->has_loop;
        voice->loop_mode = instrument->loop_mode;
        voice->direction = voice->loop_mode == TS_LOOP_REVERSE ? -1 : 1;
        voice->latched = latched != 0;
        voice->crossfade_frames = voice->looping ?
                                  ts_audition_crossfade_frames(
                                      &plan, instrument->loop_crossfade_ms) : 0;
        voice->active = 1;
    }
    return TS_NOTE_STARTED;
}

void ts_note_bank_release(TsNoteBank *bank, int note)
{
    if (bank == NULL) return;
    for (int i = 0; i < TS_NOTE_VOICE_LIMIT; ++i) {
        TsNoteVoice *voice = &bank->voices[i];
        if (voice->active && !voice->latched && voice->note == note && voice->looping)
            voice->active = 0;
    }
}

void ts_note_bank_sync(TsNoteBank *bank, const TsInstrument *instrument, int output_rate)
{
    ts_note_bank_sync_tuned(bank, instrument,
                            instrument != NULL ? &instrument->tuning : NULL,
                            output_rate);
}

void ts_note_bank_sync_tuned(TsNoteBank *bank, const TsInstrument *instrument,
                             const TsTuning *tuning, int output_rate)
{
    if (bank == NULL || instrument == NULL || tuning == NULL) return;
    for (int i = 0; i < TS_NOTE_VOICE_LIMIT; ++i)
        if (bank->voices[i].active)
            update_voice(&bank->voices[i], instrument, tuning,
                         bank->voices[i].source, output_rate);
}

void ts_note_bank_set_source(TsNoteBank *bank, const TsInstrument *instrument,
                             TsAuditionSource source, int output_rate)
{
    ts_note_bank_set_source_tuned(bank, instrument,
                                  instrument != NULL ? &instrument->tuning : NULL,
                                  source, output_rate);
}

void ts_note_bank_set_source_tuned(TsNoteBank *bank,
                                   const TsInstrument *instrument,
                                   const TsTuning *tuning,
                                   TsAuditionSource source, int output_rate)
{
    if (bank == NULL || instrument == NULL || tuning == NULL) return;
    for (int i = 0; i < TS_NOTE_VOICE_LIMIT; ++i)
        if (bank->voices[i].active)
            update_voice(&bank->voices[i], instrument, tuning, source, output_rate);
}

float ts_note_bank_read(TsNoteBank *bank)
{
    float mixed = 0.0f;
    int count = 0;
    if (bank == NULL) return 0.0f;
    for (int i = 0; i < TS_NOTE_VOICE_LIMIT; ++i) {
        TsNoteVoice *voice = &bank->voices[i];
        float value;
        if (!voice->active || voice->sample == NULL || voice->sample->data == NULL) continue;
        if (voice->looping) {
            voice->position = ts_audition_loop_position(
                voice->position, voice->range_first, voice->range_last,
                voice->crossfade_frames, voice->loop_mode, &voice->direction);
            value = ts_audition_read_looped_mode(
                voice->sample, voice->position, voice->range_first,
                voice->range_last, voice->crossfade_frames, voice->loop_mode);
        } else {
            size_t at = voice->position > 0.0 ? (size_t)voice->position : 0;
            if (at + 1u >= voice->range_last || at + 1u >= voice->sample->frames) {
                voice->active = 0;
                continue;
            }
            {
                float fraction = (float)(voice->position - (double)at);
                value = voice->sample->data[at] +
                        (voice->sample->data[at + 1u] - voice->sample->data[at]) * fraction;
            }
        }
        voice->position += voice->step * voice->direction;
        mixed += value;
        ++count;
    }
    return count > 0 ? mixed / sqrtf((float)count) : 0.0f;
}

int ts_note_bank_count(const TsNoteBank *bank)
{
    int count = 0;
    if (bank == NULL) return 0;
    for (int i = 0; i < TS_NOTE_VOICE_LIMIT; ++i)
        if (bank->voices[i].active) ++count;
    return count;
}

uint32_t ts_note_bank_mask(const TsNoteBank *bank)
{
    uint32_t mask = 0;
    if (bank == NULL) return 0;
    for (int i = 0; i < TS_NOTE_VOICE_LIMIT; ++i)
        if (bank->voices[i].active && bank->voices[i].note >= 0 && bank->voices[i].note < 24)
            mask |= 1u << bank->voices[i].note;
    return mask;
}

const TsNoteVoice *ts_note_bank_display_voice(const TsNoteBank *bank)
{
    const TsNoteVoice *result = NULL;
    if (bank == NULL) return NULL;
    for (int i = 0; i < TS_NOTE_VOICE_LIMIT; ++i)
        if (bank->voices[i].active &&
            (result == NULL || bank->voices[i].serial > result->serial))
            result = &bank->voices[i];
    return result;
}
