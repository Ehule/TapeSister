#ifndef TAPESISTER_PERFORMANCE_RECORDER_H
#define TAPESISTER_PERFORMANCE_RECORDER_H

#include "tapesister/sample.h"

#include <stddef.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>

typedef enum {
    TS_PERFORMANCE_FILE_IDLE = 0,
    TS_PERFORMANCE_FILE_RECORDING,
    TS_PERFORMANCE_FILE_STOPPING,
    TS_PERFORMANCE_FILE_COMPLETED,
    TS_PERFORMANCE_FILE_FAILED
} TsPerformanceFileState;

/* Single-producer/single-consumer recorder. The audio callback only calls
   push_frame(); allocation, filesystem work, and header maintenance belong
   to the controller and writer thread. */
typedef struct {
    TsStereoFrame *ring;
    size_t ring_capacity_frames;
    FILE *file;
    uint32_t sample_rate;
    uint8_t channels;
    uint64_t checkpoint_frames;
    _Atomic uint_least64_t write_cursor;
    _Atomic uint_least64_t read_cursor;
    _Atomic uint_least64_t accepted_frames;
    _Atomic uint_least64_t written_frames;
    _Atomic uint_least64_t dropped_frames;
    _Atomic TsPerformanceFileState state;
    char path[1200];
    char error[160];
} TsPerformanceRecorder;

void ts_performance_recorder_init(TsPerformanceRecorder *recorder);
void ts_performance_recorder_free(TsPerformanceRecorder *recorder);
int ts_performance_recorder_start(TsPerformanceRecorder *recorder,
                                  const char *path, uint32_t sample_rate,
                                  uint8_t channels, size_t queue_frames,
                                  char *error, size_t error_size);
int ts_performance_recorder_push_frame(TsPerformanceRecorder *recorder,
                                       TsStereoFrame frame);
int ts_performance_recorder_request_stop(TsPerformanceRecorder *recorder);
/* Drains at most max_frames. Returns nonzero while more writer work remains. */
int ts_performance_recorder_pump(TsPerformanceRecorder *recorder,
                                 size_t max_frames);
TsPerformanceFileState ts_performance_recorder_state(
    const TsPerformanceRecorder *recorder);
uint64_t ts_performance_recorder_frames(
    const TsPerformanceRecorder *recorder);
uint64_t ts_performance_recorder_dropped(
    const TsPerformanceRecorder *recorder);
int ts_performance_recorder_uses_rf64(uint64_t frames, uint8_t channels);

#endif
