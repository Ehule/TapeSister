#include "tapesister/pedalboard.h"
#include "tapesister/profile.h"
#include "tapesister/matrix_effect_types.h"
#include <errno.h>
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define SLOTS TS_SISTER_FX_SLOT_COUNT
struct TsPedalboardSlot {
    TsSisterFxSlotControls controls;
    TsSisterSingleFx *native;
    TsSunVoxEngine *sunvox;
    float gain, target_gain, mix, smooth;
    uint64_t generation;
    TsStereoFrame *dry;unsigned dry_at,dry_frames;
};
struct TsPedalboard {
    TsPedalboardControls controls;
    TsPedalboardSlot *child[SLOTS], *previous[SLOTS], *orphan[SLOTS];
    atomic_uint remaining[SLOTS], orphan_remaining[SLOTS];
    unsigned total[SLOTS], orphan_total[SLOTS], rate, owner;
    uint32_t board_id;
    float gate[SLOTS], start[SLOTS], previous_gate[SLOTS], orphan_gate[SLOTS];
    unsigned gate_remaining[SLOTS], gate_total[SLOTS];
};
static atomic_uint_least64_t next_generation=1;
static int unit(float x) { return isfinite(x) && x>=0 && x<=1; }
static uint32_t identity(const TsPedalboardControls *c,int i)
{ return c->slot[i].id?c->slot[i].id:(uint32_t)i+1; }
static int slot_valid(const TsSisterFxSlotControls *s)
{
    return s->type>=TS_SISTER_FX_EMPTY && s->type<TS_SISTER_FX_TYPE_COUNT &&
        (s->enabled==0 || s->enabled==1) && s->placement==TS_SISTER_FX_PLACE_POST &&
        isfinite(s->gain_db) && s->gain_db>=-12 && s->gain_db<=12 &&
        unit(s->parameter_a) && unit(s->parameter_b) && unit(s->parameter_c) && unit(s->mix) &&
        s->parameter_locks<=31 && ts_transient_states_valid(s->transient_power,&s->transient_states) &&
        ts_transient_states_valid(0,&s->grain_states) &&
        (s->type!=TS_SISTER_FX_FILTER || ts_character_filter_valid(&s->filter)) &&
        (s->type!=TS_SISTER_FX_SUNVOX || ts_sunvox_valid(&s->sunvox));
}
void ts_pedalboard_identify(TsPedalboardControls *c)
{
    if(!c->next_id)c->next_id=1;
    for(int i=0;i<SLOTS;++i)if(c->slot[i].id>=c->next_id && c->slot[i].id<INT32_MAX)c->next_id=c->slot[i].id+1;
    for(int i=0;i<c->count;++i)if(!c->slot[i].id && c->next_id<INT32_MAX)c->slot[i].id=c->next_id++;
}
void ts_pedalboard_default(TsPedalboardControls *c)
{
    memset(c,0,sizeof(*c));c->initialized=1;c->count=TS_SISTER_FX_SLOT_DEFAULT;
    c->transition=ts_sister_fx_transition_normalized(12);c->next_id=SLOTS+1;
    for(int i=0;i<SLOTS;++i){c->slot[i]=ts_sister_fx_empty_slot();c->slot[i].id=i+1;}
}
int ts_pedalboard_valid(const TsPedalboardControls *c)
{
    if(!c || c->initialized!=1 || c->count<TS_SISTER_FX_SLOT_MIN || c->count>SLOTS || c->next_id>INT32_MAX || !unit(c->transition))return 0;
    for(int i=0;i<SLOTS;++i) {
        if(c->slot[i].id>INT32_MAX || !slot_valid(&c->slot[i]) || (i>=c->count && c->slot[i].type))return 0;
        for(int j=0;j<i && i<c->count;++j)if(identity(c,i)==identity(c,j))return 0;
    }
    return 1;
}
TsPedalboardSlot *ts_pedalboard_slot_new(const TsSisterFxSlotControls *c,unsigned rate,uint32_t seed)
{
    if(!c || !c->type)return NULL;
    TsPedalboardSlot *s=calloc(1,sizeof(*s));if(!s)return NULL;
    s->generation=atomic_fetch_add(&next_generation,1);s->controls=*c;s->smooth=1-expf(-1.f/(.020f*rate));
    if(c->type==TS_SISTER_FX_SUNVOX)s->sunvox=ts_sunvox_new(&c->sunvox,rate);
    else s->native=ts_sister_single_fx_new(c->type,rate,seed);
    if(!s->sunvox && !s->native){free(s);return NULL;}
    if(s->sunvox){s->dry_frames=ts_sunvox_quantum(&c->sunvox)+1;s->dry=calloc(s->dry_frames,sizeof(*s->dry));if(!s->dry){ts_pedalboard_slot_free(s);return NULL;}}
    s->gain=s->target_gain=powf(10,c->gain_db*.05f);s->mix=c->mix;
    ts_pedalboard_slot_set(s,c);return s;
}
void ts_pedalboard_slot_free(TsPedalboardSlot *s)
{ if(s){ts_sister_single_fx_free(s->native);ts_sunvox_free(s->sunvox);free(s->dry);free(s);} }
int ts_pedalboard_slot_matches(const TsPedalboardSlot *s,const TsSisterFxSlotControls *c)
{
    if(!s)return !c->type;
    return c->type==s->controls.type && (c->type!=TS_SISTER_FX_SUNVOX ||
        (c->sunvox.patch==s->controls.sunvox.patch && ts_sunvox_quantum(&c->sunvox)==ts_sunvox_quantum(&s->controls.sunvox)));
}
void ts_pedalboard_slot_set(TsPedalboardSlot *s,const TsSisterFxSlotControls *c)
{
    if(!s || !ts_pedalboard_slot_matches(s,c))return;
    s->controls=*c;s->target_gain=powf(10,c->gain_db*.05f);
    if(s->sunvox)ts_sunvox_set(s->sunvox,&c->sunvox);
    else ts_sister_single_fx_set(s->native,c);
}
unsigned ts_pedalboard_slot_latency(const TsPedalboardSlot *s)
{ return s && s->sunvox?ts_sunvox_quantum(&s->controls.sunvox):0; }
void ts_pedalboard_slot_span(TsPedalboardSlot *s,const TsStereoFrame *in,const float *gate,TsStereoFrame *out,unsigned frames)
{
    if(!s){memmove(out,in,frames*sizeof(*out));return;}
    if(s->native){ts_sister_single_fx_process_span(s->native,in,gate,out,frames);return;}
    while(frames) {
        unsigned n=ts_sunvox_span_limit(s->sunvox);if(n>64)n=64;if(n>frames)n=frames;
        float input[128],wet[128],dry[128];
        for(unsigned j=0;j<n;++j){input[2*j]=in[j].l;input[2*j+1]=in[j].r;}
        int ready=ts_sunvox_stage_span(s->sunvox,input,wet,dry,n);
        for(unsigned j=0;j<n;++j) {
            s->gain+=(s->target_gain-s->gain)*s->smooth;s->mix+=(s->controls.mix-s->mix)*s->smooth;
            float k=s->mix*gate[j];
            out[j]=ts_stereo_frame_sanitize_inline((TsStereoFrame){dry[2*j]+(wet[2*j]*s->gain-dry[2*j])*k,
                dry[2*j+1]+(wet[2*j+1]*s->gain-dry[2*j+1])*k});
        }
        /* Serial dependencies are completed at their own render boundary.
           Nested engines do not consume the outer matrix's fixed job slots. */
        if(ready)ts_sunvox_render_pending(s->sunvox);
        in+=n;out+=n;gate+=n;frames-=n;
    }
}
/* Propagate the board's reference signal through the same latency and
   topology fades as its wet path. This remains aligned even during a quantum
   change; a single aggregate delay tap would jump at the end of that edit. */
void ts_pedalboard_slot_dry(TsPedalboardSlot *s,const TsStereoFrame *in,TsStereoFrame *out,unsigned frames)
{
    if(!s || !s->dry){memmove(out,in,frames*sizeof(*out));return;}
    unsigned delay=s->dry_frames-1;
    for(unsigned i=0;i<frames;++i){s->dry[s->dry_at]=in[i];out[i]=s->dry[(s->dry_at+s->dry_frames-delay)%s->dry_frames];s->dry_at=(s->dry_at+1)%s->dry_frames;}
}
float ts_pedalboard_slot_direct(const TsPedalboardSlot *s,float gate)
{return s?1-s->mix*gate:1;}
size_t ts_pedalboard_slot_memory(const TsPedalboardSlot *s)
{ return s?sizeof(*s)+s->dry_frames*sizeof(*s->dry)+ts_sister_single_fx_memory(s->native)+ts_sunvox_memory(s->sunvox):0; }
int ts_pedalboard_topology_matches(const TsPedalboard *b,const TsPedalboardControls *c)
{
    if(!b || !c)return 0;
    for(int i=0;i<SLOTS;++i)if(identity(&b->controls,i)!=identity(c,i) || !ts_pedalboard_slot_matches(b->child[i],&c->slot[i]))return 0;
    return 1;
}
TsPedalboard *ts_pedalboard_new(const TsPedalboardControls *c,unsigned rate,uint32_t seed)
{
    if(!ts_pedalboard_valid(c) || rate<1000 || rate>384000)return NULL;
    TsPedalboard *b=calloc(1,sizeof(*b));if(!b)return NULL;
    b->controls=*c;b->rate=rate;
    for(int i=0;i<SLOTS;++i) {
        atomic_init(&b->remaining[i],0);atomic_init(&b->orphan_remaining[i],0);
        if(c->slot[i].type) {
            b->child[i]=ts_pedalboard_slot_new(&c->slot[i],rate,seed^(identity(c,i)*UINT32_C(0x9e3779b9)));
            if(!b->child[i]){ts_pedalboard_free(b);return NULL;}
        }
        b->gate[i]=c->slot[i].enabled;
    }
    return b;
}
void ts_pedalboard_free(TsPedalboard *b)
{
    if(!b)return;
    for(int i=0;i<SLOTS;++i){ts_pedalboard_slot_free(b->child[i]);ts_pedalboard_slot_free(b->previous[i]);ts_pedalboard_slot_free(b->orphan[i]);}
    free(b);
}
void ts_pedalboard_set(TsPedalboard *b,const TsPedalboardControls *c)
{
    /* Validation belongs to staging, outside the callback/device lock; SunVox
       resource validation takes a catalog mutex. This publication only copies. */
    if(!ts_pedalboard_topology_matches(b,c))return;
    unsigned frames=(unsigned)fmaxf(1,ts_sister_fx_transition_ms(c->transition)*.001f*b->rate);
    for(int i=0;i<SLOTS;++i) {
        if(c->slot[i].enabled!=b->controls.slot[i].enabled || (b->gate_remaining[i] && c->transition!=b->controls.transition)) {
            b->start[i]=b->gate[i];b->gate_remaining[i]=b->gate_total[i]=frames;
        }
        ts_pedalboard_slot_set(b->child[i],&c->slot[i]);
    }
    b->controls=*c;
}
int ts_pedalboard_stage(const TsPedalboard *b,const TsPedalboardControls *c,TsPedalboardPlan *p)
{
    memset(p,0,sizeof(*p));for(int i=0;i<SLOTS;++i)p->reuse[i]=-1;
    if(!b || !ts_pedalboard_valid(c))return 0;
    if(ts_pedalboard_topology_matches(b,c))return 1;
    for(int i=0;i<SLOTS;++i)if(b->previous[i] || b->orphan[i])return 0;
    for(int i=0;i<c->count;++i) {
        for(int j=0;j<b->controls.count;++j)if(identity(c,i)==identity(&b->controls,j) && ts_pedalboard_slot_matches(b->child[j],&c->slot[i])){p->reuse[i]=j;break;}
        if(p->reuse[i]<0 && c->slot[i].type) {
            p->fresh[i]=ts_pedalboard_slot_new(&c->slot[i],b->rate,b->board_id^(identity(c,i)*UINT32_C(0x9e3779b9)));
            if(!p->fresh[i]){ts_pedalboard_discard(p);return 0;}
        }
    }
    p->prepared=1;return 1;
}
void ts_pedalboard_commit(TsPedalboard *b,const TsPedalboardControls *c,TsPedalboardPlan *p)
{
    if(!p->prepared){ts_pedalboard_set(b,c);return;}
    TsPedalboardSlot *old[SLOTS];float gate[SLOTS],start[SLOTS];unsigned remaining[SLOTS],total[SLOTS];int used[SLOTS]={0};
    memcpy(old,b->child,sizeof(old));memcpy(gate,b->gate,sizeof(gate));memcpy(start,b->start,sizeof(start));
    memcpy(remaining,b->gate_remaining,sizeof(remaining));memcpy(total,b->gate_total,sizeof(total));
    unsigned fade=b->rate*12/1000;if(!fade)fade=1;
    for(int i=0;i<SLOTS;++i) {
        int j=p->reuse[i];b->child[i]=j>=0?old[j]:p->fresh[i];p->fresh[i]=NULL;
        if(j>=0){used[j]=1;b->gate[i]=gate[j];b->start[i]=start[j];b->gate_remaining[i]=remaining[j];b->gate_total[i]=total[j];}
        else {b->gate[i]=c->slot[i].enabled;b->gate_remaining[i]=0;}
    }
    for(int i=0;i<c->count;++i)if(p->reuse[i]<0) {
        for(int j=0;j<b->controls.count;++j)if(!used[j] && identity(c,i)==identity(&b->controls,j)) {
            b->previous[i]=old[j];b->previous_gate[i]=gate[j];used[j]=1;break;
        }
        b->total[i]=fade;atomic_store(&b->remaining[i],fade+ts_pedalboard_slot_latency(b->child[i]));
    }
    for(int j=0;j<SLOTS;++j)if(!used[j] && old[j]){b->orphan[j]=old[j];b->orphan_gate[j]=gate[j];b->orphan_total[j]=fade;atomic_store(&b->orphan_remaining[j],fade);}
    /* Compare enable changes against the processor that moved, not its former
       row. Preserve its ongoing ramp and DSP history. */
    TsPedalboardControls before=*c;
    for(int i=0;i<SLOTS;++i)if(p->reuse[i]>=0)before.slot[i].enabled=b->controls.slot[p->reuse[i]].enabled;
    b->controls=before;ts_pedalboard_set(b,c);p->prepared=0;
}
void ts_pedalboard_discard(TsPedalboardPlan *p)
{
    for(int i=0;i<SLOTS;++i)ts_pedalboard_slot_free(p->fresh[i]);
    for(int i=0;i<2*SLOTS;++i)ts_pedalboard_slot_free(p->garbage[i]);memset(p,0,sizeof(*p));
}
int ts_pedalboard_collect_ready(const TsPedalboard *b)
{
    for(int i=0;b && i<SLOTS;++i)if((b->previous[i] && !atomic_load(&b->remaining[i])) || (b->orphan[i] && !atomic_load(&b->orphan_remaining[i])))return 1;
    return 0;
}
void ts_pedalboard_collect(TsPedalboard *b,TsPedalboardPlan *p,int offline)
{
    memset(p,0,sizeof(*p));if(!b)return;
    for(int i=0;i<SLOTS;++i) {
        if(offline){atomic_store(&b->remaining[i],0);atomic_store(&b->orphan_remaining[i],0);}
        if(!atomic_load(&b->remaining[i])){p->garbage[i]=b->previous[i];b->previous[i]=NULL;}
        if(!atomic_load(&b->orphan_remaining[i])){p->garbage[SLOTS+i]=b->orphan[i];b->orphan[i]=NULL;}
    }
}
void ts_pedalboard_finish_edits(TsPedalboard *b)
{for(int i=0;b && i<SLOTS;++i){atomic_store(&b->remaining[i],0);atomic_store(&b->orphan_remaining[i],0);}}
void ts_pedalboard_profile_owner(TsPedalboard *b,unsigned owner,uint32_t board_id)
{ if(b){b->owner=owner;b->board_id=board_id;} }
unsigned ts_pedalboard_latency(const TsPedalboard *b)
{
    unsigned n=0;for(int i=0;b && i<SLOTS;++i) {
        unsigned a=ts_pedalboard_slot_latency(b->child[i]);
        unsigned z=atomic_load(&b->remaining[i])?ts_pedalboard_slot_latency(b->previous[i]):0;
        n+=a>z?a:z;
    }return n;
}
static float advance(TsPedalboard *b,int i)
{
    if(b->gate_remaining[i]){--b->gate_remaining[i];b->gate[i]=b->start[i]+(b->controls.slot[i].enabled-b->start[i])*(1-(double)b->gate_remaining[i]/b->gate_total[i]);}
    return b->gate[i];
}
void ts_pedalboard_process_aligned(TsPedalboard *b,const TsStereoFrame *in,TsStereoFrame *out,TsStereoFrame *dry,unsigned frames)
{
    if(!b){memmove(out,in,frames*sizeof(*out));if(dry)memmove(dry,in,frames*sizeof(*dry));return;}
    while(frames) {
        unsigned n=frames>64?64:frames;
        for(int i=0;i<SLOTS;++i)if(atomic_load(&b->remaining[i]) || atomic_load(&b->orphan_remaining[i])){n=1;break;}
        TsStereoFrame scratch[64],reference[64];float gate[64];memcpy(scratch,in,n*sizeof(*in));memcpy(reference,in,n*sizeof(*in));
        for(int i=0;i<SLOTS;++i) {
            unsigned left=atomic_load(&b->remaining[i]);
            if(!b->child[i] && !left)continue;
            uint64_t measured=ts_profile_child_begin(b->owner,i);
            for(unsigned j=0;j<n;++j)gate[j]=advance(b,i);
            if(left) {
                TsStereoFrame before=scratch[0],old,before_dry=reference[0],old_dry;float oldgate=b->previous_gate[i];
                ts_pedalboard_slot_span(b->child[i],&before,gate,scratch,1);
                ts_pedalboard_slot_span(b->previous[i],&before,&oldgate,&old,1);
                ts_pedalboard_slot_dry(b->child[i],&before_dry,reference,1);ts_pedalboard_slot_dry(b->previous[i],&before_dry,&old_dry,1);
                float t=left>b->total[i]?0:1-(float)left/b->total[i];
                reference[0]=(TsStereoFrame){old_dry.l+(reference[0].l-old_dry.l)*t,old_dry.r+(reference[0].r-old_dry.r)*t};
                scratch[0]=(TsStereoFrame){old.l+(scratch[0].l-old.l)*t,old.r+(scratch[0].r-old.r)*t};
                atomic_store(&b->remaining[i],left-1);
            } else {ts_pedalboard_slot_span(b->child[i],scratch,gate,scratch,n);ts_pedalboard_slot_dry(b->child[i],reference,reference,n);}
            if(measured){TsPedalboardSlot *v=b->child[i]?b->child[i]:b->previous[i];
                if(v){TsProfileEffectIdentity id={.generation=v->generation,.id=identity(&b->controls,i),.type=v->controls.type==TS_SISTER_FX_SUNVOX?TS_MATRIX_FX_SUNVOX:v->controls.type>=TS_SISTER_FX_FILTER?v->controls.type+1:v->controls.type,.patch=v->controls.sunvox.patch,.quantum=ts_pedalboard_slot_latency(v)};
                    ts_profile_child_end(b->owner,i,b->board_id,&id,measured,left!=0);}
            }
        }
        for(int i=0;i<SLOTS;++i) {
            unsigned left=atomic_load(&b->orphan_remaining[i]);if(!left)continue;
            TsStereoFrame zero={0},tail;float k=b->orphan_gate[i];
            ts_pedalboard_slot_span(b->orphan[i],&zero,&k,&tail,1);k=(float)left/b->orphan_total[i];
            TsStereoFrame dry_tail;ts_pedalboard_slot_dry(b->orphan[i],&zero,&dry_tail,1);reference[0].l+=dry_tail.l*k;reference[0].r+=dry_tail.r*k;
            scratch[0].l+=tail.l*k;scratch[0].r+=tail.r*k;atomic_store(&b->orphan_remaining[i],left-1);
        }
        for(unsigned j=0;j<n;++j){out[j]=ts_stereo_frame_sanitize_inline(scratch[j]);if(dry)dry[j]=ts_stereo_frame_sanitize_inline(reference[j]);}
        in+=n;out+=n;if(dry)dry+=n;frames-=n;
    }
}
TsStereoFrame ts_pedalboard_process(TsPedalboard *b,TsStereoFrame in)
{ TsStereoFrame out;ts_pedalboard_process_aligned(b,&in,&out,NULL,1);return out; }
void ts_pedalboard_process_span(TsPedalboard *b,const TsStereoFrame *in,TsStereoFrame *out,unsigned frames)
{ ts_pedalboard_process_aligned(b,in,out,NULL,frames); }
size_t ts_pedalboard_memory(const TsPedalboard *b)
{
    if(!b)return 0;size_t size=sizeof(*b);
    for(int i=0;i<SLOTS;++i)size+=ts_pedalboard_slot_memory(b->child[i])+ts_pedalboard_slot_memory(b->previous[i])+ts_pedalboard_slot_memory(b->orphan[i]);return size;
}
int ts_pedalboard_write(FILE *f, const char *prefix, const TsPedalboardControls *c)
{
    if (!ts_pedalboard_valid(c) || fprintf(f, "%s.Header=1,%d,%.9g\n", prefix, c->count, c->transition) < 0) return 0;
    if(fprintf(f,"%s.NextId=%u\n",prefix,c->next_id)<0)return 0;
    for (int i = 0; i < c->count; ++i) {
        const TsSisterFxSlotControls *s = &c->slot[i]; char key[96];
        if(fprintf(f,"%s.Id%d=%u\n",prefix,i,identity(c,i))<0)return 0;
        if (fprintf(f, "%s.Slot%d=%d,%d,%.9g,%.9g,%.9g,%.9g,%.9g,%u\n", prefix, i,
            s->type, s->enabled, s->gain_db, s->parameter_a, s->parameter_b, s->parameter_c, s->mix, s->parameter_locks) < 0) return 0;
        if(s->type==TS_SISTER_FX_SUNVOX){snprintf(key,sizeof(key),"%s.SunVox%d",prefix,i);if(!ts_sunvox_write(f,key,&s->sunvox))return 0;}
        snprintf(key, sizeof(key), "%s.Transient%d", prefix, i);
        if (!ts_transient_states_write(f, key, s->transient_power, &s->transient_states)) return 0;
        snprintf(key, sizeof(key), "%s.Grain%d", prefix, i);
        if (!ts_transient_states_write(f, key, 0, &s->grain_states)) return 0;
        snprintf(key, sizeof(key), "%s.Filter%d", prefix, i);
        if (!ts_character_filter_write(f, key, &s->filter)) return 0;
    }
    return fprintf(f, "%s.End=1\n", prefix) >= 0;
}
static int numbers(const char *s, double *v, int n)
{
    for (int i = 0; i < n; ++i) {
        char *end; errno = 0; v[i] = strtod(s, &end);
        if (errno || end == s || !isfinite(v[i]) || (i < n - 1 ? *end != ',' : *end != 0)) return 0;
        s = end + (i < n - 1);
    }
    return 1;
}
static int whole(double v, int lo, int hi) { return v >= lo && v <= hi && v == floor(v); }
int ts_pedalboard_read(TsPedalboardControls *c, const char *key, const char *value)
{
    TsPedalboardControls next = *c; int i = -1, used = 0; double v[8];
    if (!strcmp(key, "Header")) {
        if (!numbers(value, v, 3) || v[0] != 1 || !whole(v[1], 5, 20) || v[2] < 0 || v[2] > 1) return -1;
        ts_pedalboard_default(&next); next.initialized = 2; next.count = v[1]; next.transition = v[2];
    } else {
        if (next.initialized != 2) return -1;
        if (!strcmp(key, "End")) {
            next.initialized = 1;
            if (strcmp(value, "1") || !ts_pedalboard_valid(&next)) return -1;
        } else if(!strcmp(key,"NextId")) {
            if(!numbers(value,v,1) || !whole(v[0],1,2147483647))return -1;next.next_id=(uint32_t)v[0];
        } else if(sscanf(key,"Id%d%n",&i,&used)==1 && used && !key[used] && i>=0 && i<next.count) {
            if(!numbers(value,v,1) || !whole(v[0],1,2147483647))return -1;next.slot[i].id=(uint32_t)v[0];
        } else if(sscanf(key,"SunVox%d.%n",&i,&used)==1 && used && i>=0 && i<next.count) {
            if(ts_sunvox_read(&next.slot[i].sunvox,key+used,value)!=1)return -1;
        } else if (sscanf(key, "Slot%d%n", &i, &used) == 1 && used && !key[used] && i >= 0 && i < next.count) {
            if (!numbers(value, v, 8) || !whole(v[0], 0, TS_SISTER_FX_TYPE_COUNT-1) ||
                !whole(v[1], 0, 1) || !whole(v[7], 0, 31)) return -1;
            TsSisterFxSlotControls *s = &next.slot[i];
            s->type = v[0]; s->enabled = v[1]; s->gain_db = v[2]; s->parameter_a = v[3];
            s->parameter_b = v[4]; s->parameter_c = v[5]; s->mix = v[6]; s->parameter_locks = v[7];
            TsSisterFxSlotControls checked=*s;
            if(checked.type==TS_SISTER_FX_SUNVOX)checked.type=TS_SISTER_FX_EMPTY;
            if (!slot_valid(&checked)) return -1;
        } else if (sscanf(key, "Transient%d%n", &i, &used) == 1 && used && !key[used] && i >= 0 && i < next.count) {
            if (!ts_transient_states_read(value, &next.slot[i].transient_power, &next.slot[i].transient_states)) return -1;
        } else if (sscanf(key, "Grain%d%n", &i, &used) == 1 && used && !key[used] && i >= 0 && i < next.count) {
            uint8_t power;
            if (!ts_transient_states_read(value, &power, &next.slot[i].grain_states) || power) return -1;
        } else if (sscanf(key, "Filter%d%n", &i, &used) == 1 && used && !key[used] && i >= 0 && i < next.count) {
            if (!ts_character_filter_read(&next.slot[i].filter, value)) return -1;
        } else return -1;
    }
    *c = next; return 1;
}
