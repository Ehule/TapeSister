#ifndef TAPESISTER_AUDITION_H
#define TAPESISTER_AUDITION_H

#include <stddef.h>
#include "tapesister/sample.h"

typedef enum {
    TS_AUDITION_CURRENT = 0,
    TS_AUDITION_PARENT
} TsAuditionSource;

typedef enum {
    TS_AUDITION_ALL = 0,
    TS_AUDITION_SELECTION,
    TS_AUDITION_DISPLAYED,
    TS_AUDITION_NOTE
} TsAuditionRange;

typedef struct {
    const TsSample *sample;
    size_t first;
    size_t last;
} TsAuditionPlan;

int ts_audition_plan(const TsInstrument *instrument, TsAuditionSource source,
                     TsAuditionRange range, TsAuditionPlan *plan);
double ts_audition_map_progress(double position, size_t first, size_t last,
                                size_t target_first, size_t target_last);
const char *ts_audition_source_name(TsAuditionSource source);
const char *ts_audition_range_name(TsAuditionRange range);

#endif
