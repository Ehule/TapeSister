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

typedef enum {
    TS_EXTERNAL_CAPTURE_IDLE = 0,
    TS_EXTERNAL_CAPTURE_ARMED,
    TS_EXTERNAL_CAPTURE_RECORDING,
    TS_EXTERNAL_CAPTURE_COMPLETED,
    TS_EXTERNAL_CAPTURE_CANCELED
} TsExternalCaptureState;

typedef struct {
    float *buffer;
    size_t capacity_frames;
    size_t recorded_frames;
    float *pre_roll;
    size_t pre_roll_capacity;
    size_t pre_roll_count;
    size_t pre_roll_write;
    size_t silence_frames;
    size_t tail_frames;
    size_t quiet_frames;
    uint32_t sample_rate;
    float threshold_amplitude;
    int threshold_db;
    int destination_slot;
    int stopped_early;
    _Atomic TsExternalCaptureState state;
} TsExternalRecorder;

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

void ts_external_recorder_init(TsExternalRecorder *recorder);
void ts_external_recorder_free(TsExternalRecorder *recorder);
int ts_external_recorder_arm(TsExternalRecorder *recorder,
                             int destination_slot,
                             uint32_t sample_rate,
                             int threshold_db,
                             int pre_roll_ms,
                             int silence_ms,
                             int tail_ms,
                             int max_seconds,
                             char *error, size_t error_size);
int ts_external_recorder_write_sample(TsExternalRecorder *recorder, float sample);
int ts_external_recorder_stop(TsExternalRecorder *recorder,
                              char *error, size_t error_size);
int ts_external_recorder_cancel(TsExternalRecorder *recorder);
float ts_external_recorder_progress(const TsExternalRecorder *recorder);
const char *ts_external_capture_state_name(TsExternalCaptureState state);
int ts_external_next_chain_slot(int destination_slot);

#endif
