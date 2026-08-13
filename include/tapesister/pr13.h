#ifndef TAPESISTER_PR13_H
#define TAPESISTER_PR13_H

#include "tapesister/sample.h"

extern int ts_pr13_lock_request;
void ts_pr13_set_lock_request(int enabled);
int ts_pr13_new_parent(TsInstrument *instrument, int reseed,
                       char *error, size_t error_size);

#endif
