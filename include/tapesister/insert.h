#ifndef TAPESISTER_INSERT_H
#define TAPESISTER_INSERT_H

#include "tapesister/input_monitor.h"
#include <stdio.h>

enum { TS_INSERT_MAX_CHANNELS = 8 };
typedef struct {
    int send_pair;   /* 0 = unassigned; 1 = outputs 3/4, through 3 = 7/8.
                       Master owns outputs 1/2 exclusively. */
    int return_pair; /* 0 = inputs 1/2, through 3 = 7/8. */
    float send_db, return_db;
} TsInsertControls;

typedef struct {
    TsInsertControls controls;
    TsInputMonitor return_monitor;
    /* Capture acknowledges a new port before playback discards old samples.
       Only the consumer advances its read cursor; no concurrent FIFO reset. */
    _Atomic unsigned port_request, port_ack, monitor_port_ack, input_channels;
    unsigned port_seen, output_channels, sample_rate;
    float send_gain, return_gain, send_target, return_target, slew;
    float send_peak, return_peak, decay;
    TsStereoFrame send;
} TsInsert;

void ts_insert_default(TsInsertControls *controls);
int ts_insert_valid(const TsInsertControls *controls);
void ts_insert_init(TsInsert *insert);
void ts_insert_prepare(TsInsert *insert, unsigned sample_rate);
/* Control changes require the playback callback to be excluded. */
void ts_insert_set(TsInsert *insert, const TsInsertControls *controls);
/* Control thread, with the capture producer stopped/excluded. Playback may run. */
void ts_insert_capture_prepare(TsInsert *insert, unsigned channels,
                               unsigned rate, unsigned buffer_frames);
/* Called by the existing capture callback, once per block. */
void ts_insert_capture(TsInsert *insert, const float *input, size_t frames,
                       unsigned channels);
/* Exclude the reserved return pair from the ordinary EXT monitor/source. */
TsStereoFrame ts_insert_external_frame(const TsInsert *insert,
    const float *frame, unsigned channels, int mode);
int ts_insert_send_available(const TsInsert *insert);
int ts_insert_return_available(const TsInsert *insert);
TsStereoFrame ts_insert_process(TsInsert *insert, TsStereoFrame input,
                                float route_gain);
void ts_insert_write_output(const TsInsert *insert, float *frame,
                             unsigned channels, TsStereoFrame master);
int ts_insert_write(FILE *file, const TsInsertControls *controls);
int ts_insert_read(TsInsertControls *controls, const char *key, const char *value);

#endif
