#include "tapesister/audition.h"

#include <math.h>

static size_t clamp_size(size_t value, size_t maximum)
{
    return value > maximum ? maximum : value;
}

const char *ts_audition_source_name(TsAuditionSource source)
{
    return source == TS_AUDITION_PARENT ? "PARENT" : "CURRENT";
}

const char *ts_audition_range_name(TsAuditionRange range)
{
    if (range == TS_AUDITION_SELECTION) return "SELECTION";
    if (range == TS_AUDITION_DISPLAYED) return "DISPLAYED";
    if (range == TS_AUDITION_LOOP) return "LOOP";
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

    if (range == TS_AUDITION_SELECTION) {
        if (!instrument->has_selection ||
            instrument->selection_last <= instrument->selection_first) return 0;
        first = instrument->selection_first;
        last = instrument->selection_last;
    } else if (range == TS_AUDITION_DISPLAYED) {
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
