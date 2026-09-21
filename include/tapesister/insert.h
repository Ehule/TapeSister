#ifndef TAPESISTER_INSERT_H
#define TAPESISTER_INSERT_H

#include "tapesister/input_monitor.h"
#include <stdio.h>

enum { TS_INSERT_MAX_CHANNELS = 8 };
typedef struct {
    int send_pair;   /* 0 = unassigned. Shared Master: 1 = 3/4, 3 = 7/8.
                       Separate SEND device: 1 = 1/2, 4 = 7/8. */
    int return_pair; /* 0 = inputs 1/2, through 3 = 7/8. */
    float send_db, return_db;
} TsInsertControls;

typedef struct {
    TsInsertControls controls;
    TsInputMonitor return_monitor;
    TsInputMonitor send_monitor;
    _Atomic unsigned send_request;
    _Atomic int dedicated_return;
    unsigned send_seen, send_channels;
    int separate_send;
    /* Capture acknowledges a new port before playback discards old samples.
       Only the consumer advances its read cursor; no concurrent FIFO reset. */
    _Atomic unsigned port_request, port_ack, monitor_port_ack, input_channels;
    _Atomic unsigned return_buffer_frames, output_buffer_frames;
    unsigned port_seen, output_channels, sample_rate;
    float send_gain, return_gain, send_target, return_target, slew;
    float send_peak, return_peak, decay;
    TsStereoFrame send;
    /* Synchronous duplex backend: borrowed input valid only during one output
       callback. Its clock is already identical; no FIFO or resampling. */
    const float *duplex_input;
    size_t duplex_frames, duplex_position;
    unsigned duplex_channels;
    int duplex;
} TsInsert;

void ts_insert_default(TsInsertControls *controls);
int ts_insert_valid(const TsInsertControls *controls);
void ts_insert_init(TsInsert *insert);
void ts_insert_prepare(TsInsert *insert, unsigned sample_rate);
/* Master callback, before processing its frames. Sizes the RETURN bridge in
   capture-rate frames and publishes the producer burst size for separate SEND. */
void ts_insert_begin_output_block(TsInsert *insert, unsigned frames);
void ts_insert_duplex_block(TsInsert *insert, int enabled, const float *input,
                            size_t frames, unsigned channels);
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
void ts_insert_write_output(TsInsert *insert, float *frame,
                             unsigned channels, TsStereoFrame master);
/* I/O reconfiguration requires Master, SEND and both capture callbacks excluded. */
void ts_insert_io_mode(TsInsert *insert, int separate_send, int dedicated_return);
/* Master and SEND callbacks excluded; channels=0 disables a separate SEND. */
void ts_insert_send_prepare(TsInsert *insert, unsigned channels,
                            unsigned producer_rate, unsigned buffer_frames);
/* Separate SEND callback: drains/resamples the bounded stereo FIFO. */
void ts_insert_render_send(TsInsert *insert, float *output, size_t frames,
                           unsigned channels, unsigned output_rate);
void ts_insert_capture_prepare_from(TsInsert *insert, unsigned channels,
    unsigned rate, unsigned buffer_frames, int dedicated);
void ts_insert_capture_from(TsInsert *insert, const float *input, size_t frames,
                            unsigned channels, int dedicated);
unsigned ts_insert_reserved_port(const TsInsert *insert);
int ts_insert_write(FILE *file, const TsInsertControls *controls);
int ts_insert_read(TsInsertControls *controls, const char *key, const char *value);

#endif
