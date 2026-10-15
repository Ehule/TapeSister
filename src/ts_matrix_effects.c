#include "tapesister/matrix_effects.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

struct TsMatrixEffectNode {
    uint32_t id;int type;unsigned rate;
    TsSisterSingleFx *pedal;
    TsCompressor compressor;
    TsMatrixEffectControls controls;
    float gate,target,start,mix;unsigned remaining,total;
};
const char *ts_matrix_effect_name(int type)
{return type==TS_MATRIX_FX_COMPRESSOR?"COMPRESSOR":ts_sister_fx_type_name((TsSisterFxType)type);}
const char *ts_matrix_effect_short_name(int type)
{
    static const char *names[]={"EMPTY","REV","DLY","DIST","GRAIN","TRANS","COMP"};
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
    ts_compressor_default(&c->compressor);c->compressor.enabled=1;
}
int ts_matrix_effect_valid(const TsMatrixEffectControls *c)
{
    if(!c->id)return c->type==0;
    return c->type>0 && c->type<TS_MATRIX_FX_TYPES && (c->enabled==0 || c->enabled==1) &&
        isfinite(c->a) && c->a>=0 && c->a<=1 && isfinite(c->b) && c->b>=0 && c->b<=1 &&
        isfinite(c->c) && c->c>=0 && c->c<=1 && isfinite(c->mix) && c->mix>=0 && c->mix<=1 &&
        isfinite(c->gain_db) && c->gain_db>=-12 && c->gain_db<=12 &&
        isfinite(c->transition) && c->transition>=0 && c->transition<=1 && ts_compressor_valid(&c->compressor);
}
static void node_free(TsMatrixEffectNode *n)
{if(n){ts_sister_single_fx_free(n->pedal);free(n);}}
static void node_set(TsMatrixEffectNode *n,const TsMatrixEffectControls *c,int cold)
{
    if(!n)return;
    unsigned frames=(unsigned)fmaxf(1,ts_sister_fx_transition_ms(c->transition)*.001f*n->rate);
    if(cold){n->gate=n->target=(float)c->enabled;n->remaining=0;n->mix=c->mix;}
    else if(n->target!=(float)c->enabled || (n->remaining && n->controls.transition!=c->transition)) {
        n->target=(float)c->enabled;n->remaining=n->total=frames;n->start=n->gate;
    }
    n->controls=*c;
    if(n->type==TS_MATRIX_FX_COMPRESSOR) {
        TsCompressorControls comp=c->compressor;comp.enabled=1;ts_compressor_set(&n->compressor,&comp);
        if(cold) {
            TsCompressor *k=&n->compressor;k->wet=1;k->external=comp.external;
            k->threshold=comp.threshold_db;k->slope=1-1/comp.ratio;
            k->attack=k->target_attack;k->release=k->target_release;k->output=k->target_output;
        }
    } else {
        TsSisterFxSlotControls slot={(TsSisterFxType)c->type,1,TS_SISTER_FX_PLACE_POST,c->gain_db,c->a,c->b,c->c,c->mix};
        ts_sister_single_fx_set(n->pedal,&slot);
    }
}
static TsMatrixEffectNode *node_new(const TsMatrixEffectControls *c,unsigned rate)
{
    TsMatrixEffectNode *n=calloc(1,sizeof(*n));if(!n)return NULL;
    n->id=c->id;n->type=c->type;n->rate=rate;
    if(c->type==TS_MATRIX_FX_COMPRESSOR){ts_compressor_init(&n->compressor);ts_compressor_prepare(&n->compressor,rate);}
    else if(!(n->pedal=ts_sister_single_fx_new((TsSisterFxType)c->type,rate,c->id*UINT32_C(0x9e3779b9)))){free(n);return NULL;}
    node_set(n,c,1);return n;
}
void ts_matrix_effects_init(TsMatrixEffects *f)
{
    memset(f,0,sizeof(*f));for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)atomic_init(&f->remaining[i],0);
}
void ts_matrix_effects_free(TsMatrixEffects *f)
{
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i){node_free(f->current[i]);node_free(f->previous[i]);}
    ts_matrix_effects_init(f);
}
void ts_matrix_effects_discard(TsMatrixEffectPlan *p)
{for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)node_free(p->node[i]);memset(p,0,sizeof(*p));}
int ts_matrix_effects_stage(const TsMatrixEffects *f,const TsMatrixControls *c,unsigned rate,TsMatrixEffectPlan *p)
{
    memset(p,0,sizeof(*p));if(rate<1000 || rate>384000 || !ts_matrix_valid(c))return 0;
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i) {
        const TsMatrixEffectControls *v=&c->effect[i];const TsMatrixEffectNode *n=f->current[i];
        if((!v->id && !n) || (v->id && n && n->id==v->id && n->type==v->type && n->rate==rate))continue;
        /* A short in-flight lifecycle crossfade cannot be overwritten. */
        if(f->previous[i]){ts_matrix_effects_discard(p);return 0;}
        p->changed|=1u<<i;
        if(v->id && !(p->node[i]=node_new(v,rate))){ts_matrix_effects_discard(p);return 0;}
    }
    return 1;
}
void ts_matrix_effects_commit(TsMatrixEffects *f,const TsMatrixControls *c,unsigned rate,TsMatrixEffectPlan *p)
{
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i) {
        if(p->changed&(1u<<i)) {
            f->previous[i]=f->current[i];f->current[i]=p->node[i];p->node[i]=NULL;
            f->total[i]=rate*12/1000;if(!f->total[i])f->total[i]=1;
            atomic_store_explicit(&f->remaining[i],f->total[i],memory_order_relaxed);f->fading|=1u<<i;
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
    ts_matrix_effects_free(f);f->rate=rate;
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
static TsStereoFrame node_process(TsMatrixEffectNode *n,TsStereoFrame in,TsStereoFrame key,int connected)
{
    if(!n)return (TsStereoFrame){0,0};
    if(n->remaining){--n->remaining;n->gate=n->start+(n->target-n->start)*(1-(double)n->remaining/n->total);}
    if(!connected && !in.l && !in.r && !key.l && !key.r)return (TsStereoFrame){0,0};
    if(n->pedal)return ts_sister_single_fx_process(n->pedal,in,n->gate);
    TsStereoFrame wet=ts_compressor_process(&n->compressor,in,key);
    n->mix+=(n->controls.mix-n->mix)*n->compressor.smooth;
    float amount=n->gate*n->mix;
    return (TsStereoFrame){in.l+(wet.l-in.l)*amount,in.r+(wet.r-in.r)*amount};
}
TsStereoFrame ts_matrix_effects_process(TsMatrixEffects *f,int i,TsStereoFrame in,TsStereoFrame key,int connected)
{
    if(i<0 || i>=TS_MATRIX_EFFECT_CAPACITY)return (TsStereoFrame){0,0};
    TsStereoFrame next=node_process(f->current[i],in,key,connected);
    unsigned remaining=atomic_load_explicit(&f->remaining[i],memory_order_relaxed);
    if(!remaining)return next;
    TsStereoFrame old=node_process(f->previous[i],in,key,connected);
    float t=1-(float)remaining/f->total[i];
    return (TsStereoFrame){old.l+(next.l-old.l)*t,old.r+(next.r-old.r)*t};
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
        if(f->current[i])bytes+=sizeof(TsMatrixEffectNode)+ts_sister_single_fx_memory(f->current[i]->pedal);
        if(f->previous[i])bytes+=sizeof(TsMatrixEffectNode)+ts_sister_single_fx_memory(f->previous[i]->pedal);
    }
    return bytes;
}
