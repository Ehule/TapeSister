#ifndef TAPESISTER_MATRIX_EFFECTS_H
#define TAPESISTER_MATRIX_EFFECTS_H
#include "tapesister/routing_matrix.h"
#include <stdatomic.h>

typedef struct TsMatrixEffectNode TsMatrixEffectNode;
typedef struct {
    TsMatrixEffectNode *current[TS_MATRIX_EFFECT_CAPACITY],*previous[TS_MATRIX_EFFECT_CAPACITY];
    atomic_uint remaining[TS_MATRIX_EFFECT_CAPACITY];
    unsigned total[TS_MATRIX_EFFECT_CAPACITY],fading,rate;
    unsigned suspended;
    float activity[TS_MATRIX_EFFECT_CAPACITY];
    /* At most current + retiring engine per slot. No callback allocation. */
    struct {TsMatrixEffectNode *node;int slot,transitioning;} jobs[2*TS_MATRIX_EFFECT_CAPACITY];
    unsigned jobs_count;
    int batching;
    TsSunVoxBatch *worker;
} TsMatrixEffects;
typedef struct {TsMatrixEffectNode *node[TS_MATRIX_EFFECT_CAPACITY];unsigned changed;TsSunVoxBatch *worker;TsPedalboardPlan board[TS_MATRIX_EFFECT_CAPACITY];} TsMatrixEffectPlan;
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
int ts_matrix_effects_collect_ready(const TsMatrixEffects *fx);
void ts_matrix_effects_finish_edits(TsMatrixEffects *fx);
/* Cold project/device setup, with audio stopped/excluded. */
int ts_matrix_effects_restore(TsMatrixEffects *fx,const TsMatrixControls *c,unsigned rate);
void ts_matrix_effects_tick(TsMatrixEffects *fx);
/* Runtime ownership, independent of bypass/mix. Five-ms output ramps; settled
   suspended slots retain history without advancing their DSP engines. */
void ts_matrix_effects_suspend(TsMatrixEffects *fx,unsigned mask);
/* Bracket one graph sample. Buffered jobs complete before the next sample;
   unbuffered processors and all feedback edges retain sample order. */
void ts_matrix_effects_begin_frame(TsMatrixEffects *fx);
void ts_matrix_effects_end_frame(TsMatrixEffects *fx);
TsStereoFrame ts_matrix_effects_process(TsMatrixEffects *fx,int slot,TsStereoFrame input,TsStereoFrame key,int connected);
unsigned ts_matrix_effects_span_limit(const TsMatrixEffects *fx,unsigned frames);
void ts_matrix_effects_process_span(TsMatrixEffects *fx,int slot,
    const TsStereoFrame *input,const TsStereoFrame *key,TsStereoFrame *output,
    unsigned frames,int connected);
float ts_matrix_effects_reduction(const TsMatrixEffects *fx,int slot);
float ts_matrix_effects_progress(const TsMatrixEffects *fx,int slot);
size_t ts_matrix_effects_memory(const TsMatrixEffects *fx);
#endif
