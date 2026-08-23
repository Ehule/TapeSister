#ifndef TAPESISTER_AUDITION_H
#define TAPESISTER_AUDITION_H

#include <stddef.h>
#include "tapesister/sample.h"

enum {
    TS_AUDITION_ATTACK_MS_MIN = 0,
    TS_AUDITION_ATTACK_MS_MAX = 20,
    TS_AUDITION_ATTACK_MS_DEFAULT = 2
};

typedef enum {
    TS_AUDITION_CURRENT = 0,
    TS_AUDITION_PARENT
} TsAuditionSource;

typedef enum {
    TS_AUDITION_ALL = 0,
    TS_AUDITION_SELECTION,
    TS_AUDITION_DISPLAYED,
    TS_AUDITION_LOOP,
    TS_AUDITION_WORKBENCH_LOOP,
    TS_AUDITION_NOTE
} TsAuditionRange;

typedef struct {
    const TsSample *sample;
    size_t first;
    size_t last;
} TsAuditionPlan;

int ts_audition_plan(const TsInstrument *instrument, TsAuditionSource source,
                     TsAuditionRange range, TsAuditionPlan *plan);
int ts_bank_audition_plan(const TsInstrument *instrument, int slot,
                          TsAuditionPlan *plan);
double ts_tuning_pair_audition_pitch(const TsTuning *mapping,
                                     const TsTuning *audible);
double ts_instrument_audition_pitch(const TsInstrument *instrument);
double ts_audition_map_progress(double position, size_t first, size_t last,
                                size_t target_first, size_t target_last);
size_t ts_audition_crossfade_frames(const TsAuditionPlan *plan, float milliseconds);
/* A short output-rate envelope removes the discontinuity when a voice begins
   on a nonzero sample. The gain reaches unity without altering stored audio. */
size_t ts_audition_attack_frames(int output_rate, int milliseconds);
float ts_audition_attack_gain(size_t rendered_frame, size_t attack_frames);
double ts_audition_wrap_position(double position, size_t first, size_t last,
                                 size_t crossfade_frames);
float ts_audition_read_looped(const TsSample *sample, double position,
                              size_t first, size_t last, size_t crossfade_frames);
TsStereoFrame ts_audition_read_frame(const TsSample *sample, double position,
                                     size_t last);
TsStereoFrame ts_audition_read_looped_frame(
    const TsSample *sample, double position, size_t first, size_t last,
    size_t crossfade_frames);
double ts_audition_loop_position(double position, size_t first, size_t last,
                                 size_t crossfade_frames, TsLoopMode mode,
                                 int *direction);
float ts_audition_read_looped_mode(const TsSample *sample, double position,
                                   size_t first, size_t last, size_t crossfade_frames,
                                   TsLoopMode mode);
TsStereoFrame ts_audition_read_looped_mode_frame(
    const TsSample *sample, double position, size_t first, size_t last,
    size_t crossfade_frames, TsLoopMode mode);
const char *ts_audition_source_name(TsAuditionSource source);
const char *ts_audition_range_name(TsAuditionRange range);

#endif
