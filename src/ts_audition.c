#include "tapesister/audition.h"

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
    } else {
        first = 0;
        last = source == TS_AUDITION_PARENT ? instrument->parent.frames :
                                              instrument->current.frames;
    }

    if (source == TS_AUDITION_PARENT &&
        (range == TS_AUDITION_SELECTION || range == TS_AUDITION_DISPLAYED)) {
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
