#ifndef TAPESISTER_MATRIX_EFFECTS_H
#define TAPESISTER_MATRIX_EFFECTS_H
#include "tapesister/routing_matrix.h"
#include <stdatomic.h>

typedef struct TsMatrixEffectNode TsMatrixEffectNode;
typedef struct {
    TsMatrixEffectNode *current[TS_MATRIX_EFFECT_CAPACITY],*previous[TS_MATRIX_EFFECT_CAPACITY];
    atomic_uint remaining[TS_MATRIX_EFFECT_CAPACITY];
    unsigned total[TS_MATRIX_EFFECT_CAPACITY],fading,rate;
} TsMatrixEffects;
typedef struct {TsMatrixEffectNode *node[TS_MATRIX_EFFECT_CAPACITY];unsigned changed;} TsMatrixEffectPlan;
void ts_matrix_effects_init(TsMatrixEffects *fx);
void ts_matrix_effects_free(TsMatrixEffects *fx);
/* Prepare on the control thread, before acquiring the device lock. The
   current pointers/identities are control-owned; audio only updates history. */
int ts_matrix_effects_stage(const TsMatrixEffects *fx,const TsMatrixControls *c,unsigned rate,TsMatrixEffectPlan *plan);
void ts_matrix_effects_discard(TsMatrixEffectPlan *plan);
/* Publish under device exclusion. Does not allocate or free. */
void ts_matrix_effects_commit(TsMatrixEffects *fx,const TsMatrixControls *c,unsigned rate,TsMatrixEffectPlan *plan);
/* Detach under exclusion, then free the returned plan after unlocking. */
void ts_matrix_effects_collect(TsMatrixEffects *fx,TsMatrixEffectPlan *garbage);
/* Cold project/device setup, with audio stopped/excluded. */
int ts_matrix_effects_restore(TsMatrixEffects *fx,const TsMatrixControls *c,unsigned rate);
void ts_matrix_effects_tick(TsMatrixEffects *fx);
TsStereoFrame ts_matrix_effects_process(TsMatrixEffects *fx,int slot,TsStereoFrame input,TsStereoFrame key,int connected);
float ts_matrix_effects_reduction(const TsMatrixEffects *fx,int slot);
float ts_matrix_effects_progress(const TsMatrixEffects *fx,int slot);
size_t ts_matrix_effects_memory(const TsMatrixEffects *fx);
#endif
