#ifndef TAPESISTER_DSP_COEFFICIENT_H
#define TAPESISTER_DSP_COEFFICIENT_H
#include <stdint.h>

/* Runtime-only memoization of an exact scalar calculation. Callers retain the
   original expression on every miss; smoothed controls are checked each sample.
   A zero-initialized cache is invalid. Use rate=0 for rate-independent mappings.
   This storage is never serialized as a user parameter or audio history. */
typedef struct {
    float input,value;
    uint32_t rate;
    int valid;
} TsDspCoefficient;
static inline int ts_dsp_coefficient_matches(const TsDspCoefficient *cache,float input,uint32_t rate)
{return cache->valid && cache->input==input && cache->rate==rate;}
static inline float ts_dsp_coefficient_store(TsDspCoefficient *cache,float input,uint32_t rate,float value)
{
    cache->input=input;cache->rate=rate;cache->value=value;cache->valid=1;
    return value;
}
#endif
