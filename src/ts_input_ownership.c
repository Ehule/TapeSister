#include "tapesister/input_ownership.h"

#include <string.h>

static uint8_t consumer_bit(TsInputConsumer consumer)
{
    uint8_t bit = (uint8_t)consumer;
    return bit == TS_INPUT_CONSUMER_RECORD_MONITOR ||
           bit == TS_INPUT_CONSUMER_RECORD_ACTIVE ||
           bit == TS_INPUT_CONSUMER_SISTER_EXT ||
           bit == TS_INPUT_CONSUMER_ACTIVITY ? bit : 0u;
}

void ts_input_ownership_init(TsInputOwnership *ownership)
{
    if (ownership != NULL) memset(ownership, 0, sizeof(*ownership));
}

int ts_input_ownership_request(TsInputOwnership *ownership,
                               TsInputConsumer consumer)
{
    uint8_t bit;
    uint8_t previous;
    if (ownership == NULL || (bit = consumer_bit(consumer)) == 0u) return 0;
    previous = ownership->requests;
    ownership->requests |= bit;
    return previous == 0u;
}

int ts_input_ownership_release(TsInputOwnership *ownership,
                               TsInputConsumer consumer)
{
    uint8_t bit;
    uint8_t previous;
    if (ownership == NULL || (bit = consumer_bit(consumer)) == 0u) return 0;
    previous = ownership->requests;
    ownership->requests &= (uint8_t)~bit;
    return previous != 0u && ownership->requests == 0u;
}

int ts_input_ownership_requested(const TsInputOwnership *ownership,
                                 TsInputConsumer consumer)
{
    uint8_t bit = consumer_bit(consumer);
    return ownership != NULL && bit != 0u &&
           (ownership->requests & bit) != 0u;
}

int ts_input_ownership_should_open(const TsInputOwnership *ownership)
{
    return ownership != NULL && ownership->requests != 0u &&
           !ownership->device_open;
}

int ts_input_ownership_should_run(const TsInputOwnership *ownership)
{
    return ownership != NULL && ownership->requests != 0u &&
           ownership->device_open && ownership->available;
}

void ts_input_ownership_set_device(TsInputOwnership *ownership,
                                   int open, int available)
{
    if (ownership == NULL) return;
    ownership->device_open = open != 0;
    ownership->available = ownership->device_open && available != 0;
}
