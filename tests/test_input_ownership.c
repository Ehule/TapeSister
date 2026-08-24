#include "tapesister/input_ownership.h"

#include <stdio.h>

#define CHECK(condition) do { if (!(condition)) { \
    fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, #condition); \
    return 1; } } while (0)

int main(void)
{
    TsInputOwnership owner;
    ts_input_ownership_init(&owner);
    CHECK(ts_input_ownership_request(&owner, TS_INPUT_CONSUMER_SISTER_EXT));
    CHECK(ts_input_ownership_should_open(&owner));
    ts_input_ownership_set_device(&owner, 1, 1);
    CHECK(ts_input_ownership_should_run(&owner));
    CHECK(!ts_input_ownership_request(&owner, TS_INPUT_CONSUMER_RECORD_MONITOR));
    CHECK(!ts_input_ownership_release(&owner, TS_INPUT_CONSUMER_SISTER_EXT));
    CHECK(ts_input_ownership_requested(&owner, TS_INPUT_CONSUMER_RECORD_MONITOR));
    CHECK(ts_input_ownership_should_run(&owner));
    CHECK(ts_input_ownership_release(&owner, TS_INPUT_CONSUMER_RECORD_MONITOR));
    CHECK(!ts_input_ownership_should_run(&owner));
    ts_input_ownership_set_device(&owner, 0, 0);
    CHECK(!ts_input_ownership_should_open(&owner));
    puts("input ownership tests passed");
    return 0;
}
