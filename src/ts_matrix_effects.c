#include "tapesister/matrix_effects.h"
#include "tapesister/profile.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

struct TsMatrixEffectNode {
    TsProfileEffectIdentity profile;
    uint32_t id;int type;unsigned rate;
    TsSisterSingleFx *pedal;
    TsSunVoxEngine *sunvox;uint64_t sunvox_patch;unsigned sunvox_quantum;
    TsMetaEngine *meta;float gain,gain_target,smooth;
    TsCompressor compressor;
    float transition,mix_target;
    float gate,target,start,mix;unsigned remaining,total;
};
/* Engine construction happens on control/offline threads, never in process.
   IDs survive project saves; this process-local generation must not. */
static atomic_uint_least64_t profile_generation;
static TsSisterFxType pedal_type(int type)
{return type==TS_MATRIX_FX_FILTER?TS_SISTER_FX_FILTER:type==TS_MATRIX_FX_DISPERSION?TS_SISTER_FX_DISPERSION:type==TS_MATRIX_FX_RESONATOR?TS_SISTER_FX_RESONATOR:(TsSisterFxType)type;}
const char *ts_matrix_effect_name(int type)
{return type==TS_MATRIX_FX_SUNVOX?"SUNVOX FX":type==TS_MATRIX_FX_META?"META FX":type==TS_MATRIX_FX_COMPRESSOR?"COMPRESSOR":ts_sister_fx_type_name(pedal_type(type));}
const char *ts_matrix_effect_short_name(int type)
{
    static const char *names[]={"EMPTY","REV","DLY","DIST","GRAIN","TRANS","COMP","FILTER","DISP","RESON","META","SUNVOX"};
    return type>=0 && type<TS_MATRIX_FX_TYPES?names[type]:"FX";
}
void ts_matrix_effect_default(TsMatrixEffectControls *c,int type,uint32_t id)
{
    memset(c,0,sizeof(*c));c->id=id;c->type=type;c->enabled=1;c->a=c->b=c->c=.5f;c->mix=1;
    c->transition=ts_sister_fx_transition_normalized(12);
    if(type==TS_SISTER_FX_REVERB){c->a=.58f;c->b=.42f;}
    if(type==TS_SISTER_FX_DELAY){c->a=.38f;c->b=.32f;}
    if(type==TS_SISTER_FX_DISTORTION){c->a=.25f;c->b=.55f;}
    if(type==TS_SISTER_FX_GRAIN){c->a=.46f;c->b=.42f;}
    if(type==TS_MATRIX_FX_SUNVOX)ts_sunvox_default(&c->sunvox);
    if(type==TS_MATRIX_FX_META)ts_meta_preset(&c->meta,0);
    if(type==TS_MATRIX_FX_RESONATOR)c->a=.25f;
    ts_character_filter_default(&c->filter);
    ts_compressor_default(&c->compressor);c->compressor.enabled=1;
}
int ts_matrix_effect_valid(const TsMatrixEffectControls *c)
{
    if(!c->id)return c->type==0;
    return c->type>0 && c->type<TS_MATRIX_FX_TYPES && (c->enabled==0 || c->enabled==1) &&
        isfinite(c->a) && c->a>=0 && c->a<=1 && isfinite(c->b) && c->b>=0 && c->b<=1 &&
        isfinite(c->c) && c->c>=0 && c->c<=1 && isfinite(c->mix) && c->mix>=0 && c->mix<=1 &&
        isfinite(c->gain_db) && c->gain_db>=-12 && c->gain_db<=12 &&
        isfinite(c->transition) && c->transition>=0 && c->transition<=1 && ts_compressor_valid(&c->compressor) &&
        ts_transient_states_valid(c->transient_power,&c->transient_states) &&
        (c->type!=TS_MATRIX_FX_FILTER || ts_character_filter_valid(&c->filter)) &&
        (c->type!=TS_MATRIX_FX_META || ts_meta_valid(&c->meta)) &&
        (c->type!=TS_MATRIX_FX_SUNVOX || ts_sunvox_valid(&c->sunvox));
}
static void node_free(TsMatrixEffectNode *n)
{if(n){ts_sister_single_fx_free(n->pedal);ts_meta_free(n->meta);ts_sunvox_free(n->sunvox);free(n);}}
static void node_set(TsMatrixEffectNode *n,const TsMatrixEffectControls *c,int cold)
{
    if(!n)return;
    unsigned frames=(unsigned)fmaxf(1,ts_sister_fx_transition_ms(c->transition)*.001f*n->rate);
    if(cold){n->gain=n->gain_target=powf(10,c->gain_db*.05f);n->gate=n->target=(float)c->enabled;n->remaining=0;n->mix=c->mix;}
    else if(n->target!=(float)c->enabled || (n->remaining && n->transition!=c->transition)) {
        n->target=(float)c->enabled;n->remaining=n->total=frames;n->start=n->gate;
    }
    n->transition=c->transition;n->mix_target=c->mix;
    if(n->sunvox){ts_sunvox_set(n->sunvox,&c->sunvox);n->gain_target=powf(10,c->gain_db*.05f);}
    else if(n->type==TS_MATRIX_FX_META){ts_meta_set(n->meta,&c->meta);n->gain_target=powf(10,c->gain_db*.05f);}
    else if(n->type==TS_MATRIX_FX_COMPRESSOR) {
        TsCompressorControls comp=c->compressor;comp.enabled=1;ts_compressor_set(&n->compressor,&comp);
        if(cold) {
            TsCompressor *k=&n->compressor;k->wet=1;k->external=comp.external;
            k->threshold=comp.threshold_db;k->slope=1-1/comp.ratio;
            k->attack=k->target_attack;k->release=k->target_release;k->output=k->target_output;
        }
    } else {
        TsSisterFxSlotControls slot={pedal_type(c->type),1,TS_SISTER_FX_PLACE_POST,c->gain_db,c->a,c->b,c->c,c->mix,0};
        slot.filter=c->filter;slot.transient_power=c->transient_power;slot.transient_states=c->transient_states;
        ts_sister_single_fx_set(n->pedal,&slot);
    }
}
static TsMatrixEffectNode *node_new(const TsMatrixEffectControls *c,unsigned rate)
{
    TsMatrixEffectNode *n=calloc(1,sizeof(*n));if(!n)return NULL;
    n->id=c->id;n->type=c->type;n->rate=rate;n->smooth=1-expf(-1.f/(rate*.02f));
    if(c->type==TS_MATRIX_FX_SUNVOX){
        n->sunvox_patch=c->sunvox.patch;n->sunvox_quantum=ts_sunvox_quantum(&c->sunvox);
        if(!(n->sunvox=ts_sunvox_new(&c->sunvox,rate))){free(n);return NULL;}
    }
    else if(c->type==TS_MATRIX_FX_META){if(!(n->meta=ts_meta_new(&c->meta,rate,c->id))){free(n);return NULL;}}
    else if(c->type==TS_MATRIX_FX_COMPRESSOR){ts_compressor_init(&n->compressor);ts_compressor_prepare(&n->compressor,rate);}
    else if(!(n->pedal=ts_sister_single_fx_new(pedal_type(c->type),rate,c->id*UINT32_C(0x9e3779b9)))){free(n);return NULL;}
    n->profile = (TsProfileEffectIdentity){
        .generation=atomic_fetch_add_explicit(&profile_generation,1,memory_order_relaxed)+1,
        .patch=n->sunvox_patch,.id=c->id,.type=(uint32_t)c->type,.quantum=n->sunvox_quantum};
    node_set(n,c,1);return n;
}
void ts_matrix_effects_init(TsMatrixEffects *f)
{
    memset(f,0,sizeof(*f));for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)atomic_init(&f->remaining[i],0);
}
void ts_matrix_effects_free(TsMatrixEffects *f)
{
    ts_sunvox_batch_free(f->worker);
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i){node_free(f->current[i]);node_free(f->previous[i]);}
    ts_matrix_effects_init(f);
}
void ts_matrix_effects_discard(TsMatrixEffectPlan *p)
{for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)node_free(p->node[i]);ts_sunvox_batch_free(p->worker);memset(p,0,sizeof(*p));}
static TsSunVoxBatch *prepare_worker(const TsMatrixEffects *f,const TsMatrixEffectPlan *plan)
{
    if(!ts_sunvox_workers_configured())return NULL;
    unsigned count=0,eligible=0;
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i) {
        const TsMatrixEffectNode *n=plan && (plan->changed&(1u<<i))?plan->node[i]:f->current[i];
        if(n && n->sunvox){++count;eligible+=ts_sunvox_parallel_cost(n->sunvox)>=16;}
    }
    return count>1 && eligible?ts_sunvox_batch_new(1):NULL;
}
int ts_matrix_effects_stage(const TsMatrixEffects *f,const TsMatrixControls *c,unsigned rate,TsMatrixEffectPlan *p)
{
    memset(p,0,sizeof(*p));if(rate<1000 || rate>384000 || !ts_matrix_valid(c))return 0;
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i) {
        const TsMatrixEffectControls *v=&c->effect[i];const TsMatrixEffectNode *n=f->current[i];
        if((!v->id && !n) || (v->id && n && n->id==v->id && n->type==v->type && n->rate==rate &&
            (v->type!=TS_MATRIX_FX_META || ts_meta_topology_matches(n->meta,&v->meta)) &&
            (v->type!=TS_MATRIX_FX_SUNVOX || (n->sunvox_patch==v->sunvox.patch &&
                n->sunvox_quantum==ts_sunvox_quantum(&v->sunvox)))))continue;
        /* A short in-flight lifecycle crossfade cannot be overwritten. */
        if(f->previous[i]){ts_matrix_effects_discard(p);return 0;}
        p->changed|=1u<<i;
        if(v->id && !(p->node[i]=node_new(v,rate))){ts_matrix_effects_discard(p);return 0;}
    }
    if(!f->worker)p->worker=prepare_worker(f,p);
    return 1;
}
void ts_matrix_effects_commit(TsMatrixEffects *f,const TsMatrixControls *c,unsigned rate,TsMatrixEffectPlan *p)
{
    if(p->worker){f->worker=p->worker;p->worker=NULL;}
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i) {
        if(p->changed&(1u<<i)) {
            f->previous[i]=f->current[i];f->current[i]=p->node[i];p->node[i]=NULL;
            f->total[i]=rate*12/1000;if(!f->total[i])f->total[i]=1;
            /* Let a newly buffered processor fill before moving the crossfade.
               Its wet and dry outputs are initially silent for one quantum. */
            unsigned warmup=f->current[i] && f->current[i]->sunvox?f->current[i]->sunvox_quantum:0;
            atomic_store_explicit(&f->remaining[i],f->total[i]+warmup,memory_order_relaxed);f->fading|=1u<<i;
        }
        node_set(f->current[i],&c->effect[i],0);
    }
    p->changed=0;f->rate=rate;
}
void ts_matrix_effects_collect(TsMatrixEffects *f,TsMatrixEffectPlan *p)
{
    memset(p,0,sizeof(*p));
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)if(f->previous[i] && !atomic_load_explicit(&f->remaining[i],memory_order_relaxed)) {
        p->node[i]=f->previous[i];f->previous[i]=NULL;
    }
}
int ts_matrix_effects_restore(TsMatrixEffects *f,const TsMatrixControls *c,unsigned rate)
{
    /* Transactional cold replacement: allocate all new nodes before disposing
       the previous project/device bank. Called only while audio is excluded. */
    TsMatrixEffects next;ts_matrix_effects_init(&next);
    if(!ts_matrix_valid(c) || rate<1000 || rate>384000)return 0;
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)if(c->effect[i].id && !(next.current[i]=node_new(&c->effect[i],rate))) {
        ts_matrix_effects_free(&next);return 0;
    }
    next.worker=prepare_worker(&next,NULL);
    ts_matrix_effects_free(f);f->rate=rate;f->worker=next.worker;
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i){f->current[i]=next.current[i];next.current[i]=NULL;}
    return 1;
}
void ts_matrix_effects_tick(TsMatrixEffects *f)
{
    if(!f->fading)return;
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)if(f->fading&(1u<<i)) {
        unsigned n=atomic_load_explicit(&f->remaining[i],memory_order_relaxed);
        if(n)--n;atomic_store_explicit(&f->remaining[i],n,memory_order_relaxed);
        if(!n)f->fading&=~(1u<<i);
    }
}
static TsStereoFrame node_process(TsMatrixEffects *f,int slot,TsMatrixEffectNode *n,TsStereoFrame in,TsStereoFrame key,int connected,int transitioning)
{
    if(!n)return (TsStereoFrame){0,0};
    if(n->remaining){--n->remaining;n->gate=n->start+(n->target-n->start)*(1-(double)n->remaining/n->total);}
    if(!n->sunvox && !connected && !in.l && !in.r && !key.l && !key.r)return (TsStereoFrame){0,0};
    if(n->sunvox) {
        float wet[2],dry[2];
        if(ts_sunvox_stage_frame(n->sunvox,in.l,in.r,wet,dry)) {
            if(f->batching && f->jobs_count<2*TS_MATRIX_EFFECT_CAPACITY) {
                unsigned j=f->jobs_count++;
                f->jobs[j].node=n;f->jobs[j].slot=slot;f->jobs[j].transitioning=transitioning;
            } else ts_sunvox_render_pending(n->sunvox);
        }
        n->gain+=(n->gain_target-n->gain)*n->smooth;
        n->mix+=(n->mix_target-n->mix)*n->smooth;
        float amount=n->gate*n->mix;
        return ts_stereo_frame_sanitize_inline((TsStereoFrame){
            dry[0]+(wet[0]*n->gain-dry[0])*amount,
            dry[1]+(wet[1]*n->gain-dry[1])*amount});
    }
    if(n->pedal)return ts_sister_single_fx_process(n->pedal,in,n->gate);
    if(n->meta){
        TsStereoFrame wet=ts_meta_process(n->meta,in);float k=n->smooth;n->gain+=(n->gain_target-n->gain)*k;
        n->mix+=(n->mix_target-n->mix)*k;float amount=n->gate*n->mix;
        return ts_stereo_frame_sanitize_inline((TsStereoFrame){in.l+(wet.l*n->gain-in.l)*amount,in.r+(wet.r*n->gain-in.r)*amount});
    }
    TsStereoFrame wet=ts_compressor_process(&n->compressor,in,key);
    n->mix+=(n->mix_target-n->mix)*n->compressor.smooth;
    float amount=n->gate*n->mix;
    return (TsStereoFrame){in.l+(wet.l-in.l)*amount,in.r+(wet.r-in.r)*amount};
}
TsStereoFrame ts_matrix_effects_process(TsMatrixEffects *f,int i,TsStereoFrame in,TsStereoFrame key,int connected)
{
    if(i<0 || i>=TS_MATRIX_EFFECT_CAPACITY)return (TsStereoFrame){0,0};
    uint64_t started=ts_profile_effect_begin(i);
    unsigned remaining=atomic_load_explicit(&f->remaining[i],memory_order_relaxed);
    TsStereoFrame next=node_process(f,i,f->current[i],in,key,connected,remaining!=0);
    if(remaining) {
        TsStereoFrame old=node_process(f,i,f->previous[i],in,key,connected,1);
        float t=fmaxf(0,1-(float)remaining/f->total[i]);
        next=(TsStereoFrame){old.l+(next.l-old.l)*t,old.r+(next.r-old.r)*t};
    }
    if(started) {
        const TsMatrixEffectNode *n=f->current[i]?f->current[i]:f->previous[i];
        if(n)ts_profile_effect_end(i,&n->profile,started,remaining!=0);
        else ts_profile_end(TS_PROF_MATRIX_FX,started);
    }
    return next;
}
unsigned ts_matrix_effects_span_limit(const TsMatrixEffects *f,unsigned frames)
{
    if(f->fading || f->jobs_count)return 1;
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i) {
        const TsMatrixEffectNode *n=f->current[i];
        if(n && n->sunvox) {
            unsigned limit=ts_sunvox_span_limit(n->sunvox);
            if(limit<frames)frames=limit;
        }
    }
    return frames;
}
void ts_matrix_effects_process_span(TsMatrixEffects *f,int i,
    const TsStereoFrame *in,const TsStereoFrame *key,TsStereoFrame *out,
    unsigned frames,int connected)
{
    /* Lifecycle fades use the chronological path. A SunVox job may become
       ready only at the last frame; all jobs drain in graph order afterward,
       preserving SDK-global random state and the existing worker policy. */
    TsMatrixEffectNode *n=f->current[i];
    uint64_t started=ts_profile_effect_begin(i);
    if(n && n->sunvox) {
        float input[TS_MATRIX_DELAY*2],wet[TS_MATRIX_DELAY*2],dry[TS_MATRIX_DELAY*2];
        for(unsigned j=0;j<frames;++j){input[j*2]=in[j].l;input[j*2+1]=in[j].r;}
        if(ts_sunvox_stage_span(n->sunvox,input,wet,dry,frames)) {
            unsigned job=f->jobs_count++;
            f->jobs[job].node=n;f->jobs[job].slot=i;f->jobs[job].transitioning=0;
        }
        for(unsigned j=0;j<frames;++j) {
            if(n->remaining){--n->remaining;n->gate=n->start+(n->target-n->start)*(1-(double)n->remaining/n->total);}
            n->gain+=(n->gain_target-n->gain)*n->smooth;
            n->mix+=(n->mix_target-n->mix)*n->smooth;float amount=n->gate*n->mix;
            out[j]=ts_stereo_frame_sanitize_inline((TsStereoFrame){
                dry[2*j]+(wet[2*j]*n->gain-dry[2*j])*amount,
                dry[2*j+1]+(wet[2*j+1]*n->gain-dry[2*j+1])*amount});
        }
    } else if(n && n->pedal && connected) {
        float gate[TS_MATRIX_DELAY];
        for(unsigned j=0;j<frames;++j) {
            if(n->remaining){--n->remaining;n->gate=n->start+(n->target-n->start)*(1-(double)n->remaining/n->total);}
            gate[j]=n->gate;
        }
        ts_sister_single_fx_process_span(n->pedal,in,gate,out,frames);
    } else for(unsigned j=0;j<frames;++j)out[j]=node_process(f,i,n,in[j],key[j],connected,0);
    if(started && n)ts_profile_effect_end(i,&n->profile,started,0);
}
void ts_matrix_effects_end_frame(TsMatrixEffects *f)
{
    if(f->worker && f->jobs_count) {
        TsSunVoxJob jobs[2*TS_MATRIX_EFFECT_CAPACITY];
        for(unsigned j=0;j<f->jobs_count;++j)
            jobs[j]=(TsSunVoxJob){.engine=f->jobs[j].node->sunvox,.timed=ts_profile_effect_begin(f->jobs[j].slot)!=0};
        ts_sunvox_batch_render(f->worker,jobs,f->jobs_count,ts_profile_clock);
        for(unsigned j=0;j<f->jobs_count;++j)if(jobs[j].timed) {
            int slot=f->jobs[j].slot;
            const TsMatrixEffectNode *n=f->current[slot]?f->current[slot]:f->previous[slot];
            if(n)ts_profile_effect_duration(slot,&n->profile,jobs[j].ticks,ts_profile_clock(),f->jobs[j].transitioning);
        }
        f->jobs_count=0;f->batching=0;return;
    }
    for(unsigned j=0;j<f->jobs_count;++j) {
        int slot=f->jobs[j].slot;
        uint64_t started=ts_profile_effect_begin(slot);
        ts_sunvox_render_pending(f->jobs[j].node->sunvox);
        /* Use the current slot identity even when rendering its retiring
           engine, matching the inclusive lifecycle timing in process(). */
        const TsMatrixEffectNode *n=f->current[slot]?f->current[slot]:f->previous[slot];
        if(started && n)ts_profile_effect_end(slot,&n->profile,started,f->jobs[j].transitioning);
    }
    f->jobs_count=0;f->batching=0;
}
void ts_matrix_effects_begin_frame(TsMatrixEffects *f)
{
    if(f->jobs_count)ts_matrix_effects_end_frame(f);
    f->batching=1;
}
float ts_matrix_effects_reduction(const TsMatrixEffects *f,int i)
{return i>=0 && i<TS_MATRIX_EFFECT_CAPACITY && f->current[i]?f->current[i]->compressor.reduction_db:0;}
float ts_matrix_effects_progress(const TsMatrixEffects *f,int i)
{
    const TsMatrixEffectNode *n=i>=0 && i<TS_MATRIX_EFFECT_CAPACITY?f->current[i]:NULL;
    return n && n->remaining && n->total?1-(float)n->remaining/n->total:1;
}
size_t ts_matrix_effects_memory(const TsMatrixEffects *f)
{
    size_t bytes=0;
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i) {
        if(f->current[i])bytes+=sizeof(TsMatrixEffectNode)+ts_sister_single_fx_memory(f->current[i]->pedal)+ts_meta_memory(f->current[i]->meta)+ts_sunvox_memory(f->current[i]->sunvox);
        if(f->previous[i])bytes+=sizeof(TsMatrixEffectNode)+ts_sister_single_fx_memory(f->previous[i]->pedal)+ts_meta_memory(f->previous[i]->meta)+ts_sunvox_memory(f->previous[i]->sunvox);
    }
    return bytes;
}
