#include "tapesister/matrix_modulation.h"
#include <string.h>
#include <math.h>
static int child_at(const TsMatrixEffectControls *c,uint32_t child)
{
    if(!child || c->type!=TS_MATRIX_FX_PEDALBOARD)return -1;
    for(int i=0;i<c->board.count;++i)if(c->board.slot[i].id==child)return i;
    return -1;
}
static int native_type(int type)
{return type==TS_MATRIX_FX_FILTER?TS_SISTER_FX_FILTER:type==TS_MATRIX_FX_DISPERSION?TS_SISTER_FX_DISPERSION:type==TS_MATRIX_FX_RESONATOR?TS_SISTER_FX_RESONATOR:type>0 && type<6?type:0;}
unsigned ts_mod_target_type(const TsMatrixEffectControls *c,uint32_t child)
{int i=child_at(c,child);return child?(i<0?0:c->board.slot[i].type):(unsigned)c->type;}
static int filter_field(unsigned f)
{
    return f==TS_CF_L_CUT || f==TS_CF_L_RES || f==TS_CF_L_DRIVE || f==TS_CF_L_HP || f==TS_CF_L_HP_RES || f==TS_CF_L_TRIM ||
        f==TS_CF_R_CUT || f==TS_CF_R_RES || f==TS_CF_R_DRIVE || f==TS_CF_R_HP || f==TS_CF_R_HP_RES || f==TS_CF_R_TRIM ||
        f==TS_CF_SPREAD_AMOUNT || f==TS_CF_LFO_RATE || f==TS_CF_LFO_DEPTH || f==TS_CF_LFO_PHASE ||
        f==TS_CF_ENV_AMOUNT || f==TS_CF_ENV_ATTACK || f==TS_CF_ENV_RELEASE;
}
int ts_mod_field_available(const TsMatrixEffectControls *c,uint32_t child,unsigned field)
{
    if(!c || !c->id)return 0;
    if(c->type==TS_MATRIX_FX_MODULATOR)return !child && field==TS_MOD_MIX;
    int i=child_at(c,child),type=child?(i<0?0:c->board.slot[i].type):native_type(c->type);
    if(child && (i<0 || !type))return 0;
    if(field==TS_MOD_MIX)return 1;
    if(field==TS_MOD_GAIN)return child || c->type!=TS_MATRIX_FX_COMPRESSOR;
    if(!child && c->type==TS_MATRIX_FX_META)return field>=TS_MOD_MACRO && field<TS_MOD_MORPH;
    if(!child && c->type==TS_MATRIX_FX_COMPRESSOR)return field>=TS_MOD_COMP && field<TS_MOD_COMP+5;
    if(type==TS_SISTER_FX_FILTER)return field>=TS_MOD_FILTER && field<TS_MOD_FILTER+TS_CF_COUNT && filter_field(field-TS_MOD_FILTER);
    if(type==TS_SISTER_FX_SUNVOX || !type)return 0; /* Host Mix/Output only; no arbitrary SDK buffer edits. */
    return field<3 && *ts_meta_parameter_name(type,field);
}
int ts_mod_target(const TsMatrixControls *c,const TsModMap *m)
{
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i) {
        const TsMatrixEffectControls *v=&c->effect[i];
        if(v->id==m->target && ts_mod_target_type(v,m->child)==m->type && ts_mod_field_available(v,m->child,m->field))return i;
    }
    return -1;
}
/* Control links form a DAG, including bypassed assignments. Audio-key
   feedback is causal through the detector and the next 150 Hz control tick. */
int ts_mod_order(const TsMatrixControls *c,unsigned char *order)
{
    unsigned incoming[TS_MATRIX_EFFECT_CAPACITY]={0},pending=0;int count=0;
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)if(c->effect[i].id && c->effect[i].type==TS_MATRIX_FX_MODULATOR){
        pending|=1u<<i;const TsModControls *m=&c->effect[i].modulation;
        for(unsigned k=0;k<m->map_count;++k){int j=ts_mod_target(c,&m->map[k]);
            if(j>=0 && c->effect[j].type==TS_MATRIX_FX_MODULATOR)incoming[j]|=1u<<i;}}
    while(pending){int i=0;while(i<TS_MATRIX_EFFECT_CAPACITY && (!(pending&(1u<<i)) || (incoming[i]&pending)))++i;
        if(i==TS_MATRIX_EFFECT_CAPACITY)return -1;if(order)order[count]=(unsigned char)i;++count;pending&=~(1u<<i);}
    return count;
}
int ts_mod_can_connect(const TsMatrixControls *c,uint32_t source,uint32_t target)
{
    if(source==target)return 0;
    unsigned visited=0;uint32_t pending[TS_MATRIX_EFFECT_CAPACITY];int count=0;pending[count++]=target;
    while(count){uint32_t id=pending[--count];if(id==source)return 0;
        for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)if(c->effect[i].id==id && c->effect[i].type==TS_MATRIX_FX_MODULATOR && !(visited&(1u<<i))){
            visited|=1u<<i;const TsModControls *m=&c->effect[i].modulation;
            for(unsigned k=0;k<m->map_count;++k){int j=ts_mod_target(c,&m->map[k]);if(j<0 || c->effect[j].type!=TS_MATRIX_FX_MODULATOR || (visited&(1u<<j)))continue;
                int duplicate=0;for(int n=0;n<count;++n)duplicate|=pending[n]==c->effect[j].id;
                if(!duplicate && count<TS_MATRIX_EFFECT_CAPACITY)pending[count++]=c->effect[j].id;}}}
    return 1;
}
int ts_mod_assignments_valid(const TsMatrixControls *c)
{
    if(ts_mod_order(c,NULL)<0)return 0;
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)if(c->effect[i].type==TS_MATRIX_FX_MODULATOR && c->effect[i].id) {
        const TsModControls *a=&c->effect[i].modulation;
        for(unsigned m=0;m<a->map_count;++m)for(int j=0;j<i;++j)if(c->effect[j].type==TS_MATRIX_FX_MODULATOR && c->effect[j].id) {
            const TsModControls *b=&c->effect[j].modulation;
            for(unsigned n=0;n<b->map_count;++n)if(a->map[m].target==b->map[n].target && a->map[m].child==b->map[n].child &&
                a->map[m].type==b->map[n].type && a->map[m].field==b->map[n].field)return 0;
        }
    }
    return 1;
}
void ts_mod_parameter_name(const TsMatrixEffectControls *c,uint32_t child,unsigned field,char *s,size_t size)
{
    if(field==TS_MOD_MIX){snprintf(s,size,c->type==TS_MATRIX_FX_MODULATOR?"INFLUENCE":"MIX");return;}
    if(field==TS_MOD_GAIN){snprintf(s,size,"OUTPUT");return;}
    int at=child_at(c,child),type=child && at>=0?c->board.slot[at].type:native_type(c->type);
    if(type==TS_SISTER_FX_FILTER && field>=TS_MOD_FILTER && field<TS_MOD_FILTER+TS_CF_COUNT) {
        const char *names[]={"ROUTE","L MODEL","L MODE","L CUTOFF","L RESONANCE","L DRIVE","L HIGH PASS","L HP RES","L TRIM",
            "R MODEL","R MODE","R CUTOFF","R RESONANCE","R DRIVE","R HIGH PASS","R HP RES","R TRIM","SPREAD","LFO RATE","LFO DEPTH","LFO PHASE","ENV AMOUNT","ENV ATTACK","ENV RELEASE","FOCUS"};
        snprintf(s,size,"%s",names[field-TS_MOD_FILTER]);
    } else if(!child && c->type==TS_MATRIX_FX_META && field>=TS_MOD_MACRO && field<TS_MOD_MORPH)snprintf(s,size,"%s",c->meta.label[field-TS_MOD_MACRO]);
    else if(!child && c->type==TS_MATRIX_FX_META && field==TS_MOD_MORPH)snprintf(s,size,"A/B MORPH");
    else if(!child && c->type==TS_MATRIX_FX_COMPRESSOR && field>=TS_MOD_COMP && field<TS_MOD_COMP+5)
        snprintf(s,size,"%s",(const char*[]){"THRESHOLD","RATIO","ATTACK","RELEASE","OUTPUT"}[field-TS_MOD_COMP]);
    else snprintf(s,size,"%s",type && field<3?ts_meta_parameter_name(type,field):"UNAVAILABLE");
}
void ts_mod_parameter_value(const TsMatrixEffectControls *c,uint32_t child,unsigned field,float value,char *s,size_t size)
{
    (void)c;(void)child;
    if(field==TS_MOD_GAIN)snprintf(s,size,"%+.1f dB",value*24-12);
    else if(field==TS_MOD_FILTER+TS_CF_L_CUT || field==TS_MOD_FILTER+TS_CF_R_CUT || field==TS_MOD_FILTER+TS_CF_L_HP || field==TS_MOD_FILTER+TS_CF_R_HP) {
        float hz=ts_character_filter_cutoff(value);if(hz>=1000)snprintf(s,size,"%.2f kHz",hz/1000);else snprintf(s,size,"%.0f Hz",hz);
    } else snprintf(s,size,"%.1f%%",value*100);
}
float ts_mod_parameter_normalized(const TsMatrixEffectControls *c,const TsModMap *m)
{
    unsigned f=m->field;
    if(m->child){int at=child_at(c,m->child);if(at<0)return 0;const TsSisterFxSlotControls *s=&c->board.slot[at];
        if(f==TS_MOD_MIX)return s->mix;if(f==TS_MOD_GAIN)return (s->gain_db+12)/24;
        if(f>=TS_MOD_FILTER)return s->filter.value[f-TS_MOD_FILTER];return f==0?s->parameter_a:f==1?s->parameter_b:s->parameter_c;}
    if(f==TS_MOD_MIX)return c->mix;if(f==TS_MOD_GAIN)return (c->gain_db+12)/24;
    if(f>=TS_MOD_COMP)return ts_compressor_normalized(&c->compressor,f-TS_MOD_COMP);
    if(f==TS_MOD_MORPH)return c->meta.morph;
    if(f>=TS_MOD_MACRO){unsigned i=f-TS_MOD_MACRO;return c->meta.scene_mode && c->meta.captured==3?c->meta.scene[0][i]+(c->meta.scene[1][i]-c->meta.scene[0][i])*c->meta.morph:c->meta.macro[i];}
    if(f>=TS_MOD_FILTER)return c->filter.value[f-TS_MOD_FILTER];return f==0?c->a:f==1?c->b:c->c;
}
int ts_mod_apply(TsMatrixEffectControls *c,const TsModMap *m,float value)
{
    if(c->id!=m->target || ts_mod_target_type(c,m->child)!=m->type || !ts_mod_field_available(c,m->child,m->field))return 0;
    unsigned f=m->field;float v=fmaxf(0,fminf(1,value));
    if(m->child) {
        TsSisterFxSlotControls *s=&c->board.slot[child_at(c,m->child)];
        if(f==TS_MOD_MIX)s->mix=v;else if(f==TS_MOD_GAIN)s->gain_db=v*24-12;
        else if(f>=TS_MOD_FILTER)s->filter.value[f-TS_MOD_FILTER]=v;
        else {if(f==0)s->parameter_a=v;if(f==1)s->parameter_b=v;if(f==2)s->parameter_c=v;
            /* A moving external assignment takes precedence over a captured
               transient/grain state, without changing the authored state. */
            if(s->type==TS_SISTER_FX_TRANSIENT)s->transient_states.selected=0;
            if(s->type==TS_SISTER_FX_GRAIN)s->grain_states.selected=0;}
    } else if(f==TS_MOD_MIX)c->mix=v;
    else if(f==TS_MOD_GAIN)c->gain_db=v*24-12;
    else if(f>=TS_MOD_COMP)ts_compressor_set_normalized(&c->compressor,f-TS_MOD_COMP,v);
    else if(f==TS_MOD_MORPH){c->meta.morph=v;c->meta.scene_mode=1;}
    else if(f>=TS_MOD_MACRO)ts_meta_macro_set(&c->meta,f-TS_MOD_MACRO,v);
    else if(f>=TS_MOD_FILTER)c->filter.value[f-TS_MOD_FILTER]=v;
    else {if(f==0)c->a=v;if(f==1)c->b=v;if(f==2)c->c=v;c->transient_states.selected=0;}
    return 1;
}
