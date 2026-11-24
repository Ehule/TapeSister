#ifndef TAPESISTER_MATRIX_EFFECTS_H
#define TAPESISTER_MATRIX_EFFECTS_H
#include "tapesister/routing_matrix.h"
#include <stdatomic.h>

typedef struct TsMatrixEffectNode TsMatrixEffectNode;
typedef struct TsMatrixEffects {
    TsMatrixEffectNode *current[TS_MATRIX_EFFECT_CAPACITY],*previous[TS_MATRIX_EFFECT_CAPACITY];
    atomic_uint remaining[TS_MATRIX_EFFECT_CAPACITY];
    unsigned total[TS_MATRIX_EFFECT_CAPACITY],fading,rate;
    unsigned suspended, rendered;
    float activity[TS_MATRIX_EFFECT_CAPACITY];
    /* At most current + retiring engine per slot. No callback allocation. */
    struct {TsMatrixEffectNode *node;int slot,transitioning;} jobs[2*TS_MATRIX_EFFECT_CAPACITY];
    unsigned jobs_count;
    int batching;
    TsSunVoxBatch *worker;
    unsigned recall_mask;
    unsigned mod_mask,mod_remaining,mod_targets;
    unsigned char mod_order[TS_MATRIX_EFFECT_CAPACITY];unsigned mod_count;
    double mod_quarter;uint32_t transport_serial;
    TsEqSpectrum *eq_spectrum;
    uint32_t eq_spectrum_node,eq_spectrum_child;
} TsMatrixEffects;
/* Audio thread records events in connected sends. The controller takes the
   accumulated peak and count under the normal audio-device exclusion. */
typedef struct { uint32_t id,hits; float level;uint64_t generation; } TsRecallReading;
void ts_matrix_recall_note(TsMatrixEffects *,const TsSourceRoute *);
void ts_matrix_recall_tracker_level(TsMatrixEffects *,const TsSourceRoute *,float);
TsRecallReading ts_matrix_recall_take(TsMatrixEffects *,int slot);
/* Publish under audio exclusion. Child zero observes a standalone EQ card. */
void ts_matrix_effects_observe_eq(TsMatrixEffects *,uint32_t,uint32_t,TsEqSpectrum *);
typedef struct {TsMatrixEffectNode *node[TS_MATRIX_EFFECT_CAPACITY];struct TsMatrixModulation *modulation[TS_MATRIX_EFFECT_CAPACITY];unsigned changed;TsSunVoxBatch *worker;TsPedalboardPlan board[TS_MATRIX_EFFECT_CAPACITY];} TsMatrixEffectPlan;
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
/* Bracket a chronological graph span. All controls publish before any audio
   processor runs, so control links cannot introduce audio graph cycles. */
void ts_matrix_modulation_begin(TsMatrixEffects *fx);
void ts_matrix_modulation_end(TsMatrixEffects *fx,unsigned frames);
double ts_matrix_modulation_phase(const TsMatrixEffects *fx,uint32_t id,unsigned lane);
/* Runtime ownership, independent of bypass/mix. Five-ms output ramps; settled
   suspended slots retain history without advancing their DSP engines. */
/* Call once at the beginning of each output block, even with no graph routes.
   Retires edits on nodes that did not render in the preceding block. */
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
