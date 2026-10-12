#ifndef TAPESISTER_CLEAN_OUTPUT_H
#define TAPESISTER_CLEAN_OUTPUT_H
#include "tapesister/source_route.h"
#include "tapesister/spatial.h"
typedef struct {float limit; unsigned missing;} TsCleanOutput;
unsigned ts_clean_output_available(const TsSpatialControls *array,unsigned channels,int insert_pair);
void ts_clean_output_mix(TsCleanOutput *state,const TsSourceRouteMix *clean,
                        const TsSpatialControls *array,float gain,float *out,
                        unsigned channels,int insert_pair,unsigned rate);
#endif
