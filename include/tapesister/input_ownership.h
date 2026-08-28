#ifndef TAPESISTER_INPUT_OWNERSHIP_H
#define TAPESISTER_INPUT_OWNERSHIP_H

#include <stdint.h>

typedef enum {
    TS_INPUT_CONSUMER_RECORD_MONITOR = 1u << 0,
    TS_INPUT_CONSUMER_RECORD_ACTIVE = 1u << 1,
    TS_INPUT_CONSUMER_SISTER_EXT = 1u << 2,
    TS_INPUT_CONSUMER_ACTIVITY = 1u << 3
} TsInputConsumer;

typedef struct {
    uint8_t requests;
    int device_open;
    int available;
} TsInputOwnership;

void ts_input_ownership_init(TsInputOwnership *ownership);
int ts_input_ownership_request(TsInputOwnership *ownership,
                               TsInputConsumer consumer);
int ts_input_ownership_release(TsInputOwnership *ownership,
                               TsInputConsumer consumer);
int ts_input_ownership_requested(const TsInputOwnership *ownership,
                                 TsInputConsumer consumer);
int ts_input_ownership_should_open(const TsInputOwnership *ownership);
int ts_input_ownership_should_run(const TsInputOwnership *ownership);
void ts_input_ownership_set_device(TsInputOwnership *ownership,
                                   int open, int available);

#endif
