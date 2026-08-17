#include "tapesister/audition.h"

#include <math.h>

static size_t clamp_size(size_t value, size_t maximum)
{
    return value > maximum ? maximum : value;
}

const char *ts_audition_source_name(TsAuditionSource source)
{
    return source == TS_AUDITION_PARENT ? "SOURCE" : "CURRENT";
}

const char *ts_audition_range_name(TsAuditionRange range)
{
    if (range == TS_AUDITION_SELECTION) return "SELECTION";
    if (range == TS_AUDITION_DISPLAYED) return "DISPLAYED";
    if (range == TS_AUDITION_LOOP) return "LOOP";
    if (range == TS_AUDITION_WORKBENCH_LOOP) return "WORKBENCH LOOP";
    if (range == TS_AUDITION_NOTE) return "NOTE";
    return "ALL";
}

int ts_audition_plan(const TsInstrument *instrument, TsAuditionSource source,
                     TsAuditionRange range, TsAuditionPlan *plan)
{
    size_t first;
    size_t last;
    if (instrument == NULL || plan == NULL) return 0;
    plan->sample = source == TS_AUDITION_PARENT ? &instrument->parent : &instrument->current;
    if (plan->sample->data == NULL || plan->sample->frames < 2) return 0;

    if (range == TS_AUDITION_SELECTION ||
        (range == TS_AUDITION_WORKBENCH_LOOP && instrument->has_selection)) {
        if (!instrument->has_selection ||
            instrument->selection_last <= instrument->selection_first) return 0;
        first = instrument->selection_first;
        last = instrument->selection_last;
    } else if (range == TS_AUDITION_DISPLAYED ||
               range == TS_AUDITION_WORKBENCH_LOOP) {
        first = instrument->view_first;
        last = instrument->view_last;
    } else if (range == TS_AUDITION_LOOP) {
        if (!instrument->has_loop || instrument->loop_last <= instrument->loop_first)
            return 0;
        first = instrument->loop_first;
        last = instrument->loop_last;
    } else {
        first = 0;
        last = source == TS_AUDITION_PARENT ? instrument->parent.frames :
                                              instrument->current.frames;
    }

    if (source == TS_AUDITION_PARENT &&
        (range == TS_AUDITION_SELECTION || range == TS_AUDITION_DISPLAYED ||
         range == TS_AUDITION_LOOP)) {
        first += instrument->crop_first;
        last += instrument->crop_first;
    }
    first = clamp_size(first, plan->sample->frames);
    last = clamp_size(last, plan->sample->frames);
    if (first >= last) return 0;
    plan->first = first;
    plan->last = last;
    return 1;
}

int ts_bank_audition_plan(const TsInstrument *instrument, int slot,
                          TsAuditionPlan *plan)
{
    const TsBankSlot *bank_slot;
    if (instrument == NULL || plan == NULL ||
        slot < 0 || slot >= TS_BANK_SLOT_COUNT) return 0;
    bank_slot = &instrument->bank[slot];
    if (!bank_slot->occupied || bank_slot->sample.data == NULL ||
        bank_slot->sample.frames < 2u) return 0;
    plan->sample = &bank_slot->sample;
    plan->first = bank_slot->has_loop ? bank_slot->loop_first : 0;
    plan->last = bank_slot->has_loop ? bank_slot->loop_last : bank_slot->sample.frames;
    if (plan->first >= plan->last || plan->last > bank_slot->sample.frames) return 0;
    return 1;
}

double ts_tuning_pair_audition_pitch(const TsTuning *mapping,
                                     const TsTuning *audible)
{
    double mapped;
    double heard;
    double shift;
    if (mapping == NULL || audible == NULL ||
        mapping->root_note < 0 || mapping->root_note > 127 ||
        audible->root_note < 0 || audible->root_note > 127 ||
        !isfinite(mapping->fine_tune_cents) ||
        !isfinite(audible->fine_tune_cents)) return 1.0;
    mapped = mapping->root_note + mapping->fine_tune_cents / 100.0;
    heard = audible->root_note + audible->fine_tune_cents / 100.0;
    /* set_audible_tuning moves the keyboard mapping equally in the opposite
       direction. Half of the pair's separation is therefore the requested
       non-destructive playback shift from the stored waveform. */
    shift = (heard - mapped) * 0.5;
    return pow(2.0, shift / 12.0);
}

double ts_instrument_audition_pitch(const TsInstrument *instrument)
{
    return instrument != NULL ?
        ts_tuning_pair_audition_pitch(&instrument->tuning,
                                      &instrument->audible_tuning) : 1.0;
}

size_t ts_audition_crossfade_frames(const TsAuditionPlan *plan, float milliseconds)
{
    size_t frames;
    size_t maximum;
    if (plan == NULL || plan->sample == NULL || plan->last <= plan->first + 1u ||
        milliseconds <= 0.0f) return 0;
    frames = (size_t)lrint((double)plan->sample->sample_rate * milliseconds / 1000.0);
    maximum = (plan->last - plan->first) / 2u;
    if (frames > maximum) frames = maximum;
    return frames;
}

double ts_audition_wrap_position(double position, size_t first, size_t last,
                                 size_t crossfade_frames)
{
    double cycle;
    double start;
    if (last <= first + 1u) return (double)first;
    if (crossfade_frames > (last - first) / 2u)
        crossfade_frames = (last - first) / 2u;
    start = (double)(first + crossfade_frames);
    cycle = (double)(last - first - crossfade_frames);
    if (cycle <= 0.0) return (double)first;
    if (position < (double)last) return position < (double)first ? (double)first : position;
    return start + fmod(position - (double)last, cycle);
}

static float interpolated(const TsSample *sample, double position, size_t last)
{
    size_t at;
    float fraction;
    if (sample == NULL || sample->data == NULL || sample->frames == 0) return 0.0f;
    at = position <= 0.0 ? 0u : (size_t)position;
    if (at >= sample->frames) at = sample->frames - 1u;
    if (at + 1u >= last || at + 1u >= sample->frames) return sample->data[at];
    fraction = (float)(position - (double)at);
    return sample->data[at] + (sample->data[at + 1u] - sample->data[at]) * fraction;
}

float ts_audition_read_looped(const TsSample *sample, double position,
                              size_t first, size_t last, size_t crossfade_frames)
{
    float tail;
    if (sample == NULL || sample->data == NULL || last <= first || last > sample->frames)
        return 0.0f;
    position = ts_audition_wrap_position(position, first, last, crossfade_frames);
    tail = interpolated(sample, position, last);
    if (crossfade_frames > 0 && position >= (double)(last - crossfade_frames)) {
        double offset = position - (double)(last - crossfade_frames);
        float blend = (float)(offset / (double)crossfade_frames);
        float head = interpolated(sample, (double)first + offset, last);
        return tail * (1.0f - blend) + head * blend;
    }
    return tail;
}

double ts_audition_loop_position(double position, size_t first, size_t last,
                                 size_t crossfade_frames, TsLoopMode mode,
                                 int *direction)
{
    int travel = direction != NULL && *direction < 0 ? -1 : 1;
    if (last <= first + 1u) return (double)first;
    if (mode == TS_LOOP_FORWARD)
        return ts_audition_wrap_position(position, first, last, crossfade_frames);
    if (mode == TS_LOOP_REVERSE) {
        double cycle;
        double end;
        if (crossfade_frames > (last - first) / 2u)
            crossfade_frames = (last - first) / 2u;
        if (direction != NULL) *direction = -1;
        if (position >= (double)last) return (double)(last - 1u);
        if (position >= (double)first) return position;
        cycle = (double)(last - first - crossfade_frames);
        end = (double)(last - 1u - crossfade_frames);
        if (cycle <= 0.0) return (double)(last - 1u);
        return end - fmod((double)first - position, cycle);
    }
    if (direction == NULL) direction = &travel;
    for (int guard = 0; guard < 8; ++guard) {
        double end = (double)(last - 1u);
        if (*direction > 0 && position > end) {
            position = end - (position - end);
            *direction = -1;
        } else if (*direction < 0 && position < (double)first) {
            position = (double)first + ((double)first - position);
            *direction = 1;
        } else break;
    }
    if (position < (double)first) position = (double)first;
    if (position > (double)(last - 1u)) position = (double)(last - 1u);
    return position;
}

float ts_audition_read_looped_mode(const TsSample *sample, double position,
                                   size_t first, size_t last, size_t crossfade_frames,
                                   TsLoopMode mode)
{
    if (mode == TS_LOOP_FORWARD)
        return ts_audition_read_looped(sample, position, first, last, crossfade_frames);
    if (sample == NULL || sample->data == NULL || last <= first || last > sample->frames)
        return 0.0f;
    if (mode == TS_LOOP_REVERSE && crossfade_frames > 0u &&
        position < (double)(first + crossfade_frames)) {
        double offset = position - (double)first;
        float blend;
        float head;
        float tail;
        if (offset < 0.0) offset = 0.0;
        blend = 1.0f - (float)(offset / (double)crossfade_frames);
        head = interpolated(sample, position, last);
        tail = interpolated(sample, (double)(last - crossfade_frames) + offset, last);
        return head * (1.0f - blend) + tail * blend;
    }
    return interpolated(sample, position, last);
}

double ts_audition_map_progress(double position, size_t first, size_t last,
                                size_t target_first, size_t target_last)
{
    double progress;
    if (last <= first || target_last <= target_first) return (double)target_first;
    progress = (position - (double)first) / (double)(last - first);
    if (progress < 0.0) progress = 0.0;
    if (progress > 1.0) progress = 1.0;
    return (double)target_first + progress * (double)(target_last - target_first);
}
