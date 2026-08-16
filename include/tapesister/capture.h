#ifndef TAPESISTER_CAPTURE_H
#define TAPESISTER_CAPTURE_H

#include <stddef.h>
#include <stdatomic.h>
#include <stdint.h>

typedef enum {
    TS_CAPTURE_IDLE = 0,
    TS_CAPTURE_ARMED_WAITING_FOR_TRIGGER,
    TS_CAPTURE_RECORDING,
    TS_CAPTURE_COMPLETED,
    TS_CAPTURE_CANCELED
} TsCaptureState;

typedef struct {
    float *buffer;
    size_t capacity_frames;
    size_t recorded_frames;
    uint32_t sample_rate;
    uint32_t staged_notes;
    int destination_slot;
    int source_slot;
    int stopped_early;
    _Atomic TsCaptureState state;
} TsCaptureRecorder;

void ts_capture_init(TsCaptureRecorder *recorder);
void ts_capture_free(TsCaptureRecorder *recorder);
int ts_capture_arm(TsCaptureRecorder *recorder, int destination_slot,
                   size_t capacity_frames, uint32_t sample_rate,
                   char *error, size_t error_size);
int ts_capture_set_source(TsCaptureRecorder *recorder, int source_slot,
                          char *error, size_t error_size);
int ts_capture_toggle_staged_note(TsCaptureRecorder *recorder, int note,
                                  char *error, size_t error_size);
void ts_capture_clear_staged_notes(TsCaptureRecorder *recorder);
int ts_capture_trigger(TsCaptureRecorder *recorder,
                       char *error, size_t error_size);
int ts_capture_write_sample(TsCaptureRecorder *recorder, float sample);
int ts_capture_stop(TsCaptureRecorder *recorder,
                    char *error, size_t error_size);
int ts_capture_cancel(TsCaptureRecorder *recorder);
float ts_capture_progress(const TsCaptureRecorder *recorder);
const char *ts_capture_state_name(TsCaptureState state);

#endif
