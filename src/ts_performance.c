#include "tapesister/performance.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

enum { TS_PERFORMANCE_REPLACEMENT_CROSSFADE_MS = 5 };

static const TsBankSlot *source_slot_view(const TsInstrument *instrument,
                                          int slot, TsBankSlot *view)
{
    if (instrument == NULL || view == NULL || slot < 0 ||
        slot >= TS_BANK_SLOT_COUNT) return NULL;
    *view = instrument->bank[slot];
    if (slot == instrument->selected_slot && view->occupied &&
        instrument->current.data != NULL && instrument->current.frames >= 2u) {
        view->sample = instrument->current;
        view->tuning = instrument->tuning;
        view->audible_tuning = instrument->audible_tuning;
        view->has_loop = instrument->has_loop;
        view->loop_first = instrument->loop_first;
        view->loop_last = instrument->loop_last;
        view->loop_mode = instrument->loop_mode;
        view->loop_crossfade_ms = instrument->loop_crossfade_ms;
    }
    return view;
}

static void generation_retain(TsPerformanceGeneration *generation)
{
    if (generation != NULL)
        (void)atomic_fetch_add_explicit(&generation->readers, 1u,
                                        memory_order_relaxed);
}

static void generation_release(TsPerformanceGeneration *generation)
{
    if (generation != NULL)
        (void)atomic_fetch_sub_explicit(&generation->readers, 1u,
                                        memory_order_release);
}

static void generation_free(TsPerformanceGeneration *generation)
{
    if (generation == NULL) return;
    ts_sample_free(&generation->sample);
    free(generation);
}

static void voice_deactivate(TsPerformanceVoice *voice)
{
    if (voice == NULL) return;
    generation_release(voice->generation);
    generation_release(voice->pending_generation);
    generation_release(voice->transition_generation);
    memset(voice, 0, sizeof(*voice));
}

static int generation_matches(const TsPerformanceGeneration *generation,
                              const TsSample *sample)
{
    if (generation == NULL || sample == NULL ||
        generation->sample.frames != sample->frames ||
        generation->sample.sample_rate != sample->sample_rate ||
        generation->sample.channels != sample->channels) return 0;
    if (generation->source_data == sample->data &&
        generation->source_visual_revision == sample->visual_revision)
        return 1;
    return generation->source_hash == ts_sample_hash(sample) &&
           generation->sample.frames == sample->frames &&
           generation->sample.sample_rate == sample->sample_rate &&
           generation->sample.channels == sample->channels;
}

static TsPerformanceGeneration *publish_generation(
    TsPerformanceBank *bank, int slot, const TsSample *sample)
{
    TsPerformanceGeneration *made;
    TsPerformanceGeneration *previous;
    if (bank == NULL || sample == NULL || slot < 0 ||
        slot >= TS_BANK_SLOT_COUNT) return NULL;
    previous = bank->slot_generations[slot];
    if (generation_matches(previous, sample)) return previous;
    made = calloc(1u, sizeof(*made));
    if (made == NULL) return NULL;
    ts_sample_init(&made->sample);
    if (!ts_sample_clone(&made->sample, sample, NULL, 0u)) {
        free(made);
        return NULL;
    }
    made->source_data = sample->data;
    made->source_visual_revision = sample->visual_revision;
    made->source_hash = ts_sample_hash(sample);
    made->id = ++bank->next_generation_id;
    atomic_init(&made->readers, 0u);
    made->source_slot = slot;
    bank->slot_generations[slot] = made;
    if (previous != NULL) {
        previous->next_retired = bank->retired_generations;
        bank->retired_generations = previous;
    }
    return made;
}

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
                      const TsNoteEvent *event, int latched)
{
    int free_voice = -1;
    for (int i = 0; i < TS_PERFORMANCE_VOICE_LIMIT; ++i) {
        TsPerformanceVoice *voice = &bank->voices[i];
        if (latched && voice->latched && voice->active &&
            voice->source_slot == source_slot &&
            event != NULL && voice->midi_note == event->midi_note &&
            ts_note_event_same_trigger(event, voice->origin, voice->note,
                                       voice->channel))
            return i;
        if (!voice->active && free_voice < 0) free_voice = i;
    }
    return free_voice;
}

static int start_slot_event(TsPerformanceBank *bank, const TsBankSlot *slot,
                            TsPerformanceGeneration *generation,
                            int source_slot, const TsNoteEvent *event,
                            int latched, int output_rate,
                            uint64_t group_id, float group_gain)
{
    TsPerformanceVoice *voice;
    size_t first, last, crossfade;
    int index;
    if (bank == NULL || slot == NULL || event == NULL || output_rate <= 0 ||
        event->key < 0 || event->midi_note < 0 || event->midi_note > 127 ||
        event->velocity <= 0 || event->velocity > 127 ||
        generation == NULL ||
        !voice_range_from_slot(slot, &first, &last, &crossfade))
        return 0;
    index = find_voice(bank, source_slot, event, latched);
    if (index < 0) return 0;
    voice = &bank->voices[index];
    if (voice->active && latched && voice->latched) {
        voice_deactivate(voice);
        return 1;
    }
    voice_deactivate(voice);
    generation_retain(generation);
    voice->generation = generation;
    voice->sample = &generation->sample;
    voice->range_first = first;
    voice->range_last = last;
    voice->looping = slot->has_loop && last > first + 1u;
    voice->loop_mode = slot->loop_mode;
    voice->direction = voice->loop_mode == TS_LOOP_REVERSE ? -1 : 1;
    voice->position = voice->looping && voice->direction < 0 ?
                      (double)(last - 1u) : (double)first;
    voice->step = (double)generation->sample.sample_rate / (double)output_rate *
                  ts_tuning_note_pitch(&slot->audible_tuning,
                                       event->midi_note - TS_KEYBOARD_BASE_NOTE);
    voice->crossfade_frames = crossfade;
    voice->attack_frames = ts_audition_attack_frames(
        output_rate, bank->attack_ms);
    voice->origin = event->origin;
    voice->group_id = group_id;
    voice->note = event->key;
    voice->midi_note = event->midi_note;
    voice->channel = event->channel;
    voice->source_slot = source_slot;
    voice->gain = ts_note_event_gain(event);
    voice->group_gain = group_gain;
    voice->latched = latched != 0;
    voice->active = 1;
    return 1;
}

static size_t tile_fade_frames(const TsPerformanceVoice *voice,
                               int fade_ms, int output_rate)
{
    double traversal;
    size_t requested;
    size_t maximum;
    if (voice == NULL || fade_ms <= 0 || output_rate <= 0 ||
        voice->range_last <= voice->range_first ||
        !isfinite(voice->step) || fabs(voice->step) < 0.0000001)
        return 0u;
    requested = (size_t)llround((double)output_rate *
                                (double)fade_ms / 1000.0);
    traversal = (double)(voice->range_last - voice->range_first) /
                fabs(voice->step);
    maximum = traversal > 0.0 ? (size_t)floor(traversal * 0.20) : 0u;
    if (requested > maximum) requested = maximum;
    return requested;
}

static void tile_ramp(TsPerformanceVoice *voice, float target)
{
    size_t frames;
    float distance;
    if (voice == NULL) return;
    distance = fabsf(target - voice->tile_gain);
    frames = (size_t)ceilf((float)voice->tile_fade_frames * distance);
    if (frames == 0u || distance <= 0.000001f) {
        voice->tile_gain = target;
        voice->tile_gain_step = 0.0f;
        voice->tile_ramp_remaining = 0u;
        return;
    }
    voice->tile_gain_step = (target - voice->tile_gain) / (float)frames;
    voice->tile_ramp_remaining = frames;
}

TsPerformanceTileResult ts_performance_toggle_tile(
    TsPerformanceBank *bank, const TsInstrument *instrument, int source_slot,
    int fade_ms, int output_rate)
{
    TsBankSlot view;
    const TsBankSlot *slot;
    TsPerformanceGeneration *generation;
    TsPerformanceVoice *voice;
    size_t first, last, crossfade;
    int free_voice = -1;
    if (bank == NULL || instrument == NULL || source_slot < 0 ||
        source_slot >= TS_BANK_SLOT_COUNT || output_rate <= 0)
        return TS_PERFORMANCE_TILE_FAILED;
    if (fade_ms < TS_TILE_FADE_MS_MIN) fade_ms = TS_TILE_FADE_MS_MIN;
    if (fade_ms > TS_TILE_FADE_MS_MAX) fade_ms = TS_TILE_FADE_MS_MAX;
    for (int index = 0; index < TS_PERFORMANCE_VOICE_LIMIT; ++index) {
        voice = &bank->voices[index];
        if (!voice->active) {
            if (free_voice < 0) free_voice = index;
            continue;
        }
        if (!voice->tile_launched || voice->source_slot != source_slot)
            continue;
        voice->releasing = !voice->releasing;
        tile_ramp(voice, voice->releasing ? 0.0f : 1.0f);
        if (voice->releasing && voice->tile_fade_frames == 0u) {
            voice_deactivate(voice);
            return TS_PERFORMANCE_TILE_RELEASING;
        }
        return voice->releasing ? TS_PERFORMANCE_TILE_RELEASING :
                                  TS_PERFORMANCE_TILE_RESUMED;
    }
    if (free_voice < 0) return TS_PERFORMANCE_TILE_FAILED;
    slot = source_slot_view(instrument, source_slot, &view);
    if (!voice_range_from_slot(slot, &first, &last, &crossfade))
        return TS_PERFORMANCE_TILE_FAILED;
    generation = publish_generation(bank, source_slot, &slot->sample);
    if (generation == NULL) return TS_PERFORMANCE_TILE_FAILED;
    voice = &bank->voices[free_voice];
    voice_deactivate(voice);
    generation_retain(generation);
    voice->generation = generation;
    voice->sample = &generation->sample;
    voice->range_first = first;
    voice->range_last = last;
    voice->looping = slot->has_loop && last > first + 1u;
    voice->loop_mode = slot->loop_mode;
    voice->direction = voice->loop_mode == TS_LOOP_REVERSE ? -1 : 1;
    voice->position = voice->looping && voice->direction < 0 ?
                      (double)(last - 1u) : (double)first;
    voice->step = (double)generation->sample.sample_rate /
                  (double)output_rate *
                  ts_tuning_pair_audition_pitch(&slot->tuning,
                                                 &slot->audible_tuning);
    voice->crossfade_frames = crossfade;
    voice->source_slot = source_slot;
    voice->gain = 1.0f;
    voice->group_gain = 1.0f;
    voice->latched = 1;
    voice->tile_launched = 1;
    voice->tile_fade_frames = tile_fade_frames(voice, fade_ms, output_rate);
    voice->tile_gain = voice->tile_fade_frames > 0u ? 0.0f : 1.0f;
    tile_ramp(voice, 1.0f);
    voice->active = 1;
    return TS_PERFORMANCE_TILE_STARTED;
}

void ts_performance_fade_all_tiles(TsPerformanceBank *bank)
{
    if (bank == NULL) return;
    for (int index = 0; index < TS_PERFORMANCE_VOICE_LIMIT; ++index) {
        TsPerformanceVoice *voice = &bank->voices[index];
        if (!voice->active || !voice->tile_launched) continue;
        voice->releasing = 1;
        tile_ramp(voice, 0.0f);
        if (voice->tile_fade_frames == 0u) voice_deactivate(voice);
    }
}

uint16_t ts_performance_tile_mask(const TsPerformanceBank *bank)
{
    uint16_t mask = 0u;
    if (bank == NULL) return 0u;
    for (int index = 0; index < TS_PERFORMANCE_VOICE_LIMIT; ++index) {
        const TsPerformanceVoice *voice = &bank->voices[index];
        if (voice->active && voice->tile_launched &&
            voice->source_slot >= 0 &&
            voice->source_slot < TS_BANK_SLOT_COUNT)
            mask |= (uint16_t)(1u << voice->source_slot);
    }
    return mask;
}

const TsPerformanceVoice *ts_performance_tile_display_voice(
    const TsPerformanceBank *bank, int source_slot)
{
    if (bank == NULL || source_slot < 0 ||
        source_slot >= TS_BANK_SLOT_COUNT) return NULL;
    for (int index = 0; index < TS_PERFORMANCE_VOICE_LIMIT; ++index) {
        const TsPerformanceVoice *voice = &bank->voices[index];
        if (voice->active && voice->tile_launched &&
            voice->source_slot == source_slot)
            return voice;
    }
    return NULL;
}

const TsPerformanceVoice *ts_performance_source_display_voice(
    const TsPerformanceBank *bank, int source_slot)
{
    if (bank == NULL || source_slot < 0 ||
        source_slot >= TS_BANK_SLOT_COUNT) return NULL;
    for (int index = 0; index < TS_PERFORMANCE_VOICE_LIMIT; ++index) {
        const TsPerformanceVoice *voice = &bank->voices[index];
        if (voice->active && voice->source_slot == source_slot)
            return voice;
    }
    return NULL;
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
    if (bank == NULL) return;
    for (int voice = 0; voice < TS_PERFORMANCE_VOICE_LIMIT; ++voice)
        voice_deactivate(&bank->voices[voice]);
    ts_performance_collect_retired(bank);
}

void ts_performance_collect_retired(TsPerformanceBank *bank)
{
    TsPerformanceGeneration **link;
    if (bank == NULL) return;
    link = &bank->retired_generations;
    while (*link != NULL) {
        TsPerformanceGeneration *generation = *link;
        if (atomic_load_explicit(&generation->readers,
                                 memory_order_acquire) == 0u) {
            *link = generation->next_retired;
            generation_free(generation);
        } else link = &generation->next_retired;
    }
}

void ts_performance_free(TsPerformanceBank *bank)
{
    if (bank == NULL) return;
    ts_performance_clear(bank);
    for (int slot = 0; slot < TS_BANK_SLOT_COUNT; ++slot) {
        generation_free(bank->slot_generations[slot]);
        bank->slot_generations[slot] = NULL;
    }
    while (bank->retired_generations != NULL) {
        TsPerformanceGeneration *next =
            bank->retired_generations->next_retired;
        generation_free(bank->retired_generations);
        bank->retired_generations = next;
    }
    memset(bank, 0, sizeof(*bank));
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
        if (voice->active && !voice->latched && event != NULL &&
            ts_note_event_same_trigger(event, voice->origin, voice->note,
                                       voice->channel) && voice->looping)
            voice_deactivate(voice);
    }
}

void ts_performance_release_midi_channel(TsPerformanceBank *bank, int channel)
{
    if (bank == NULL || channel < 0 || channel > 15) return;
    for (int i = 0; i < TS_PERFORMANCE_VOICE_LIMIT; ++i) {
        TsPerformanceVoice *voice = &bank->voices[i];
        if (voice->active && voice->origin == TS_NOTE_ORIGIN_MIDI &&
            voice->channel == channel)
            voice_deactivate(voice);
    }
}

void ts_performance_stop_sources(TsPerformanceBank *bank,
                                 uint16_t source_mask)
{
    if (bank == NULL || source_mask == 0u) return;
    for (int i = 0; i < TS_PERFORMANCE_VOICE_LIMIT; ++i) {
        TsPerformanceVoice *voice = &bank->voices[i];
        if (voice->active && voice->source_slot >= 0 &&
            voice->source_slot < TS_BANK_SLOT_COUNT &&
            (source_mask & (uint16_t)(1u << voice->source_slot)) != 0u)
            voice_deactivate(voice);
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
    TsPerformanceGeneration *generations[TS_BANK_SLOT_COUNT] = {0};
    int started = 0;
    int needed = 0;
    int valid_members = 0;
    int free_voices = 0;
    int matching_latched = 0;
    uint64_t group_id;
    float group_gain;
    if (bank == NULL || instrument == NULL || source_mask == 0u || event == NULL)
        return 0;
    for (int voice = 0; voice < TS_PERFORMANCE_VOICE_LIMIT; ++voice)
        if (!bank->voices[voice].active) ++free_voices;
    for (int slot = 0; slot < TS_BANK_SLOT_COUNT; ++slot) {
        TsBankSlot view;
        const TsBankSlot *source;
        size_t first, last, crossfade;
        int already_latched = 0;
        if ((source_mask & (uint16_t)(1u << slot)) == 0u) continue;
        source = source_slot_view(instrument, slot, &view);
        if (!voice_range_from_slot(source, &first, &last, &crossfade))
            continue;
        generations[slot] = publish_generation(bank, slot, &source->sample);
        if (generations[slot] == NULL) return 0;
        ++valid_members;
        if (latched) {
            for (int voice = 0; voice < TS_PERFORMANCE_VOICE_LIMIT; ++voice) {
                const TsPerformanceVoice *candidate = &bank->voices[voice];
                if (candidate->active && candidate->latched &&
                    candidate->source_slot == slot &&
                    candidate->midi_note == event->midi_note &&
                    ts_note_event_same_trigger(event, candidate->origin,
                                               candidate->note,
                                               candidate->channel)) {
                    already_latched = 1;
                    break;
                }
            }
        }
        if (already_latched) ++matching_latched;
    }
    needed = valid_members;
    if (latched && matching_latched == valid_members && valid_members > 0) {
        for (int voice = 0; voice < TS_PERFORMANCE_VOICE_LIMIT; ++voice) {
            TsPerformanceVoice *candidate = &bank->voices[voice];
            if (candidate->active && candidate->latched &&
                candidate->source_slot >= 0 &&
                candidate->source_slot < TS_BANK_SLOT_COUNT &&
                (source_mask & (uint16_t)(1u << candidate->source_slot)) != 0u &&
                candidate->midi_note == event->midi_note &&
                ts_note_event_same_trigger(event, candidate->origin,
                                           candidate->note,
                                           candidate->channel))
                voice_deactivate(candidate);
        }
        return valid_members;
    }
    if (latched && matching_latched > 0) {
        if (needed > free_voices + matching_latched) return 0;
        /* A partially changed mask restarts one complete coherent latch;
           it never leaves only the newly added arbitrary subset sounding. */
        for (int voice = 0; voice < TS_PERFORMANCE_VOICE_LIMIT; ++voice) {
            TsPerformanceVoice *candidate = &bank->voices[voice];
            if (candidate->active && candidate->latched &&
                candidate->midi_note == event->midi_note &&
                ts_note_event_same_trigger(event, candidate->origin,
                                           candidate->note,
                                           candidate->channel))
                voice_deactivate(candidate);
        }
        free_voices += matching_latched;
    }
    /* A performance group is indivisible. If the fixed callback-safe pool
       cannot hold every valid member, reject the whole trigger rather than
       producing an accidental partial chord. */
    if (needed > free_voices) return 0;
    if (valid_members == 0) return 0;
    group_id = ++bank->next_group_id;
    group_gain = 1.0f / sqrtf((float)valid_members);
    for (int slot = 0; slot < TS_BANK_SLOT_COUNT; ++slot) {
        TsBankSlot view;
        const TsBankSlot *source;
        if ((source_mask & (uint16_t)(1u << slot)) == 0u) continue;
        source = source_slot_view(instrument, slot, &view);
        if (generations[slot] != NULL &&
            start_slot_event(bank, source, generations[slot], slot, event,
                             latched, output_rate, group_id, group_gain))
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

static int loop_boundary(const TsPerformanceVoice *voice)
{
    if (voice == NULL || !voice->looping) return 0;
    return voice->direction >= 0 ?
           voice->position >= (double)voice->range_last :
           voice->position < (double)voice->range_first;
}

static void begin_ping_pong_turnaround(TsPerformanceVoice *voice)
{
    double output_frames;
    size_t frames;
    if (voice == NULL || !voice->previous_frame_valid ||
        voice->crossfade_frames == 0u || !isfinite(voice->step) ||
        fabs(voice->step) < 0.0000001) return;
    output_frames = (double)voice->crossfade_frames / fabs(voice->step);
    frames = (size_t)ceil(output_frames);
    if (frames < 2u) frames = 2u;
    voice->turnaround_from = voice->previous_frame;
    voice->turnaround_crossfade_frames = frames;
    voice->turnaround_crossfade_remaining = frames;
}

static TsStereoFrame apply_ping_pong_turnaround(TsPerformanceVoice *voice,
                                                 TsStereoFrame current)
{
    if (voice != NULL && voice->turnaround_crossfade_remaining > 0u &&
        voice->turnaround_crossfade_frames > 0u) {
        size_t completed = voice->turnaround_crossfade_frames -
                           voice->turnaround_crossfade_remaining;
        float phase = voice->turnaround_crossfade_frames > 1u ?
            (float)completed /
            (float)(voice->turnaround_crossfade_frames - 1u) : 1.0f;
        float smooth = phase * phase * (3.0f - 2.0f * phase);
        current.l = voice->turnaround_from.l +
            (current.l - voice->turnaround_from.l) * smooth;
        current.r = voice->turnaround_from.r +
            (current.r - voice->turnaround_from.r) * smooth;
        --voice->turnaround_crossfade_remaining;
    }
    return ts_stereo_frame_sanitize(current);
}

static void adopt_pending_generation(TsPerformanceVoice *voice)
{
    TsPerformanceGeneration *pending;
    int boundary_direction;
    if (voice == NULL || voice->pending_generation == NULL) return;
    pending = voice->pending_generation;
    boundary_direction = voice->direction;
    generation_release(voice->transition_generation);
    voice->transition_generation = voice->generation;
    voice->transition_position = voice->position;
    voice->transition_step = voice->step;
    voice->transition_range_first = voice->range_first;
    voice->transition_range_last = voice->range_last;
    voice->transition_crossfade_frames = voice->crossfade_frames;
    voice->transition_loop_mode = voice->loop_mode;
    voice->transition_direction = voice->direction;
    voice->transition_frame = 0u;
    voice->transition_frames = voice->pending_transition_frames;
    voice->generation = pending;
    voice->pending_generation = NULL;
    voice->sample = &pending->sample;
    voice->range_first = voice->pending_range_first;
    voice->range_last = voice->pending_range_last;
    voice->crossfade_frames = voice->pending_crossfade_frames;
    voice->loop_mode = voice->pending_loop_mode;
    voice->direction = voice->pending_direction;
    voice->step = voice->pending_step;
    if (voice->loop_mode == TS_LOOP_PING_PONG) {
        voice->direction = boundary_direction >= 0 ? -1 : 1;
        voice->position = voice->direction < 0 ?
            (double)(voice->range_last - 1u) :
            (double)voice->range_first;
    } else {
        voice->position = voice->direction >= 0 ?
            (double)voice->range_first :
            (double)(voice->range_last - 1u);
    }
}

static TsStereoFrame transition_old_frame(TsPerformanceVoice *voice)
{
    TsStereoFrame frame = {0.0f, 0.0f};
    if (voice == NULL || voice->transition_generation == NULL) return frame;
    voice->transition_position = ts_audition_loop_position(
        voice->transition_position, voice->transition_range_first,
        voice->transition_range_last, voice->transition_crossfade_frames,
        voice->transition_loop_mode, &voice->transition_direction);
    frame = ts_audition_read_looped_mode_frame(
        &voice->transition_generation->sample, voice->transition_position,
        voice->transition_range_first, voice->transition_range_last,
        voice->transition_crossfade_frames, voice->transition_loop_mode);
    voice->transition_position +=
        voice->transition_step * voice->transition_direction;
    return frame;
}

TsStereoFrame ts_performance_read_stereo(TsPerformanceBank *bank,
                                         TsStereoFrame *raw_mix)
{
    TsStereoFrame mixed = {0.0f, 0.0f};
    if (raw_mix != NULL) *raw_mix = (TsStereoFrame){0.0f, 0.0f};
    if (bank == NULL) return mixed;
    for (int i = 0; i < TS_PERFORMANCE_VOICE_LIMIT; ++i) {
        TsPerformanceVoice *voice = &bank->voices[i];
        TsStereoFrame value;
        float gain;
        if (!voice->active) continue;
        if (voice->generation == NULL || voice->sample == NULL ||
            voice->sample->data == NULL ||
            voice->sample->frames < 2u) {
            voice_deactivate(voice);
            continue;
        }
        if (voice->looping) {
            int direction_before = voice->direction;
            if (voice->pending_generation != NULL && loop_boundary(voice))
                adopt_pending_generation(voice);
            voice->position = ts_audition_loop_position(
                voice->position, voice->range_first, voice->range_last,
                voice->crossfade_frames, voice->loop_mode, &voice->direction);
            if (voice->loop_mode == TS_LOOP_PING_PONG &&
                voice->direction != direction_before &&
                voice->transition_generation == NULL)
                begin_ping_pong_turnaround(voice);
            value = ts_audition_read_looped_mode_frame(
                voice->sample, voice->position, voice->range_first,
                voice->range_last, voice->crossfade_frames, voice->loop_mode);
        } else {
            size_t at;
            if ((voice->direction >= 0 &&
                 voice->position + 1.0 >= (double)voice->range_last) ||
                (voice->direction < 0 &&
                 voice->position <= (double)voice->range_first)) {
                voice_deactivate(voice);
                continue;
            }
            at = voice->position > 0.0 ? (size_t)voice->position : 0u;
            if (at + 1u >= voice->sample->frames) {
                voice_deactivate(voice);
                continue;
            }
            value = ts_audition_read_frame(
                voice->sample, voice->position, voice->range_last);
        }
        value = apply_ping_pong_turnaround(voice, value);
        voice->previous_frame = value;
        voice->previous_frame_valid = 1;
        if (voice->tile_launched) {
            if (voice->tile_ramp_remaining > 0u) {
                voice->tile_gain += voice->tile_gain_step;
                if (--voice->tile_ramp_remaining == 0u)
                    voice->tile_gain = voice->releasing ? 0.0f : 1.0f;
            }
            if (voice->releasing && voice->tile_gain <= 0.000001f) {
                voice_deactivate(voice);
                continue;
            }
            gain = voice->gain * voice->tile_gain;
            if (!voice->looping && voice->tile_fade_frames > 0u) {
                double remaining = voice->direction >= 0 ?
                    (double)voice->range_last - voice->position :
                    voice->position - (double)voice->range_first;
                float tail = (float)(remaining / fabs(voice->step) /
                                     (double)voice->tile_fade_frames);
                if (tail < 0.0f) tail = 0.0f;
                if (tail < 1.0f) gain *= tail;
            }
        } else {
            gain = voice->gain * ts_audition_attack_gain(
                voice->attack_frame, voice->attack_frames);
        }
        value.l *= gain;
        value.r *= gain;
        if (voice->transition_generation != NULL) {
            TsStereoFrame old = transition_old_frame(voice);
            float phase = voice->transition_frames > 1u ?
                (float)voice->transition_frame /
                (float)(voice->transition_frames - 1u) : 1.0f;
            float from = cosf(phase * 1.5707963267948966f);
            float to = sinf(phase * 1.5707963267948966f);
            old.l *= gain;
            old.r *= gain;
            value.l = old.l * from + value.l * to;
            value.r = old.r * from + value.r * to;
            if (++voice->transition_frame >= voice->transition_frames) {
                generation_release(voice->transition_generation);
                voice->transition_generation = NULL;
                voice->transition_frame = voice->transition_frames = 0u;
            }
        }
        if (!voice->tile_launched &&
            voice->attack_frame < voice->attack_frames)
            ++voice->attack_frame;
        voice->position += voice->step * voice->direction;
        if (raw_mix != NULL) {
            raw_mix->l += value.l;
            raw_mix->r += value.r;
        }
        mixed.l += value.l * voice->group_gain;
        mixed.r += value.r * voice->group_gain;
    }
    /* Each logical trigger receives a fixed 1/sqrt(group members) gain. A
       short layer ending therefore cannot make its longer siblings jump in
       level, and overlapping retriggers remain independently normalized. */
    return ts_stereo_frame_sanitize(mixed);
}

float ts_performance_read(TsPerformanceBank *bank, float *raw_mix)
{
    TsStereoFrame raw;
    TsStereoFrame mixed = ts_performance_read_stereo(
        bank, raw_mix != NULL ? &raw : NULL);
    if (raw_mix != NULL) *raw_mix = ts_stereo_frame_fold_mono(raw);
    return ts_stereo_frame_fold_mono(mixed);
}

int ts_performance_prepare_sync(TsPerformanceBank *bank,
                                const TsInstrument *instrument)
{
    uint16_t prepared = 0u;
    if (bank == NULL || instrument == NULL) return 0;
    for (int voice = 0; voice < TS_PERFORMANCE_VOICE_LIMIT; ++voice) {
        TsPerformanceVoice *active = &bank->voices[voice];
        TsBankSlot view;
        const TsBankSlot *slot;
        uint16_t bit;
        if (!active->active || active->source_slot < 0 ||
            active->source_slot >= TS_BANK_SLOT_COUNT) continue;
        bit = (uint16_t)(1u << active->source_slot);
        if ((prepared & bit) != 0u) continue;
        slot = source_slot_view(instrument, active->source_slot, &view);
        if (slot == NULL || publish_generation(
                bank, active->source_slot, &slot->sample) == NULL)
            return 0;
        prepared |= bit;
    }
    return 1;
}

void ts_performance_sync(TsPerformanceBank *bank,
                         const TsInstrument *instrument,
                         int output_rate)
{
    if (bank == NULL || instrument == NULL || output_rate <= 0) return;
    for (int i = 0; i < TS_PERFORMANCE_VOICE_LIMIT; ++i) {
        TsPerformanceVoice *voice = &bank->voices[i];
        TsBankSlot view;
        const TsBankSlot *slot;
        TsPerformanceGeneration *generation;
        size_t first, last, crossfade;
        if (!voice->active || voice->source_slot < 0 ||
            voice->source_slot >= TS_BANK_SLOT_COUNT) continue;
        slot = source_slot_view(instrument, voice->source_slot, &view);
        if (!voice_range_from_slot(slot, &first, &last, &crossfade)) {
            voice_deactivate(voice);
            continue;
        }
        generation = publish_generation(bank, voice->source_slot,
                                        &slot->sample);
        if (generation == NULL) continue;
        if (generation != voice->generation) {
            /* One-shots retain the immutable generation they began with.
               Repeating voices adopt both channels at a loop boundary. */
            if (voice->looping && !voice->releasing) {
                generation_release(voice->pending_generation);
                generation_retain(generation);
                voice->pending_generation = generation;
                voice->pending_range_first = first;
                voice->pending_range_last = last;
                voice->pending_crossfade_frames = crossfade;
                voice->pending_loop_mode = slot->loop_mode;
                voice->pending_direction =
                    slot->loop_mode == TS_LOOP_REVERSE ? -1 : 1;
                voice->pending_step =
                    (double)generation->sample.sample_rate /
                    (double)output_rate *
                    (voice->tile_launched ?
                     ts_tuning_pair_audition_pitch(&slot->tuning,
                                                   &slot->audible_tuning) :
                     ts_tuning_note_pitch(&slot->audible_tuning,
                         voice->midi_note - TS_KEYBOARD_BASE_NOTE));
                voice->pending_transition_frames = (size_t)llround(
                    (double)output_rate *
                    TS_PERFORMANCE_REPLACEMENT_CROSSFADE_MS / 1000.0);
                if (voice->pending_transition_frames < 1u)
                    voice->pending_transition_frames = 1u;
            }
            continue;
        }
        generation_release(voice->pending_generation);
        voice->pending_generation = NULL;
        voice->range_first = first;
        voice->range_last = last;
        voice->looping = voice->releasing ? 0 : slot->has_loop;
        voice->loop_mode = slot->loop_mode;
        if (!voice->releasing)
            voice->direction = voice->loop_mode == TS_LOOP_REVERSE ? -1 : 1;
        voice->crossfade_frames = voice->releasing ? 0u : crossfade;
        voice->step = (double)slot->sample.sample_rate / (double)output_rate *
                      (voice->tile_launched ?
                       ts_tuning_pair_audition_pitch(&slot->tuning,
                                                     &slot->audible_tuning) :
                       ts_tuning_note_pitch(&slot->audible_tuning,
                                           voice->midi_note -
                                           TS_KEYBOARD_BASE_NOTE));
    }
    ts_performance_collect_retired(bank);
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

float ts_performance_peak_scale_channels(float *samples, size_t frames,
                                         uint8_t channels, float safe_peak)
{
    float peak = 0.0f;
    float gain = 1.0f;
    size_t scalar_count;
    if (samples == NULL || frames == 0u ||
        !ts_sample_dimensions(frames, channels, &scalar_count, NULL)) return 1.0f;
    if (!(safe_peak > 0.0f && safe_peak <= 1.0f)) safe_peak = 0.98f;
    for (size_t i = 0; i < scalar_count; ++i) {
        float value = isfinite(samples[i]) ? samples[i] : 0.0f;
        float level;
        samples[i] = value;
        level = fabsf(value);
        if (level > peak) peak = level;
    }
    if (peak <= safe_peak || peak <= 0.0f) return 1.0f;
    gain = safe_peak / peak;
    for (size_t i = 0; i < scalar_count; ++i) samples[i] *= gain;
    return gain;
}

float ts_performance_peak_scale(float *samples, size_t frames, float safe_peak)
{
    return ts_performance_peak_scale_channels(samples, frames, 1u, safe_peak);
}
