#include "tapesister/pedalboard.h"
#include <stdlib.h>
#include <string.h>
#define N (TS_SISTER_FX_SLOT_COUNT*TS_SISTER_FX_LOCATION_COUNT)
struct TsSisterSunVoxBank {
    struct {TsPedalboardSlot *fx;uint32_t id;unsigned location;} slot[2*N];
};
static int find(const TsSisterSunVoxBank *b,const TsSisterFxSlotControls *c,unsigned location)
{
    for(int i=0;b && i<2*N;++i)if(b->slot[i].fx && b->slot[i].id==c->id && b->slot[i].location==location && ts_pedalboard_slot_matches(b->slot[i].fx,c))return i;
    return -1;
}
int ts_sister_sunvox_ready(const TsSisterPostFxEngine *e,const TsSisterFxControls *c)
{
    for(int i=0;i<c->slot_count;++i)if(c->slot[i].type==TS_SISTER_FX_SUNVOX)
        for(unsigned l=0;l<TS_SISTER_FX_LOCATION_COUNT;++l)if((c->slot[i].placement&(1u<<l)) && find(e->sunvox,&c->slot[i],l)<0)return 0;
    return 1;
}
int ts_sister_sunvox_stage(const TsSisterPostFxEngine *e,const TsSisterFxControls *c,TsSisterSunVoxPlan *p)
{
    memset(p,0,sizeof(*p));unsigned used=0;
    for(int i=0;i<c->slot_count;++i){if(!c->slot[i].id || c->slot[i].id>INT32_MAX)return 0;
        for(int j=0;j<i;++j)if(c->slot[i].id==c->slot[j].id)return 0;}

    for(int i=0;e->sunvox && i<2*N;++i)used+=e->sunvox->slot[i].fx!=NULL;
    for(int i=0;i<c->slot_count;++i)if(c->slot[i].type==TS_SISTER_FX_SUNVOX) {
        if(!ts_sunvox_valid(&c->slot[i].sunvox))goto fail;
        for(unsigned l=0;l<TS_SISTER_FX_LOCATION_COUNT;++l)if((c->slot[i].placement&(1u<<l)) && find(e->sunvox,&c->slot[i],l)<0) {
            if(used+p->count>=2*N)goto fail;
            if(!e->sunvox && !p->created){p->created=calloc(1,sizeof(*p->created));if(!p->created)goto fail;}
            TsPedalboardSlot *fx=ts_pedalboard_slot_new(&c->slot[i],e->sample_rate?e->sample_rate:48000,c->slot[i].id);
            if(!fx)goto fail;
            p->fresh[p->count]=fx;p->id[p->count]=c->slot[i].id;p->location[p->count++]=l;
        }
    }
    return 1;
fail:ts_sister_sunvox_discard(p);return 0;
}
void ts_sister_sunvox_commit(TsSisterPostFxEngine *e,TsSisterSunVoxPlan *p)
{
    if(p->created){e->sunvox=p->created;p->created=NULL;}
    int at=0;
    for(unsigned i=0;i<p->count;++i){while(at<2*N && e->sunvox->slot[at].fx)++at;
        if(at==2*N)break;
        e->sunvox->slot[at].fx=p->fresh[i];e->sunvox->slot[at].id=p->id[i];e->sunvox->slot[at].location=p->location[i];p->fresh[i]=NULL;
    }
    p->count=0;
}
static int referenced(const TsSisterPostFxEngine *e,int at)
{
    const TsSisterSunVoxBank *b=e->sunvox;
    for(int i=0;i<TS_SISTER_FX_SLOT_COUNT;++i) {
        const TsSisterFxSlotState *s=&e->slot[i];
        const TsSisterFxSlotControls *c[4]={&e->controls.slot[i],&s->active,s->has_pending?&s->pending:NULL,s->has_queued?&s->queued:NULL};
        for(int j=0;j<4;++j)if(c[j] && c[j]->type==TS_SISTER_FX_SUNVOX && c[j]->id==b->slot[at].id &&
            (c[j]->placement&(1u<<b->slot[at].location)) && ts_pedalboard_slot_matches(b->slot[at].fx,c[j]))return 1;
    }
    return 0;
}
void ts_sister_sunvox_collect(TsSisterPostFxEngine *e,TsSisterSunVoxPlan *p)
{
    memset(p,0,sizeof(*p));for(int i=0;e->sunvox && i<2*N;++i)if(e->sunvox->slot[i].fx && !referenced(e,i)){
        p->garbage[i]=e->sunvox->slot[i].fx;e->sunvox->slot[i].fx=NULL;
    }
}
void ts_sister_sunvox_discard(TsSisterSunVoxPlan *p)
{
    for(int i=0;i<N;++i)ts_pedalboard_slot_free(p->fresh[i]);
    for(int i=0;i<2*N;++i)ts_pedalboard_slot_free(p->garbage[i]);
    free(p->created);memset(p,0,sizeof(*p));
}
void ts_sister_sunvox_free(TsSisterPostFxEngine *e)
{
    if(!e || !e->sunvox)return;for(int i=0;i<2*N;++i)ts_pedalboard_slot_free(e->sunvox->slot[i].fx);
    free(e->sunvox);e->sunvox=NULL;
}
void ts_sister_sunvox_set(TsSisterPostFxEngine *e,const TsSisterFxControls *c)
{
    for(int i=0;e->sunvox && i<c->slot_count;++i)if(c->slot[i].type==TS_SISTER_FX_SUNVOX)
        for(unsigned l=0;l<TS_SISTER_FX_LOCATION_COUNT;++l){int at=find(e->sunvox,&c->slot[i],l);if(at>=0)ts_pedalboard_slot_set(e->sunvox->slot[at].fx,&c->slot[i]);}
}
TsStereoFrame ts_sister_sunvox_process(TsSisterPostFxEngine *e,const TsSisterFxSlotControls *c,unsigned location,TsStereoFrame input,float gate,float *direct)
{
    int at=find(e->sunvox,c,location);if(at<0){*direct=1;return input;}
    TsStereoFrame out;ts_pedalboard_slot_span(e->sunvox->slot[at].fx,&input,&gate,&out,1);*direct=ts_pedalboard_slot_direct(e->sunvox->slot[at].fx,gate);return out;
}
TsStereoFrame ts_sister_sunvox_dry(TsSisterPostFxEngine *e,const TsSisterFxSlotControls *c,unsigned location,TsStereoFrame in)
{
    if(c->type!=TS_SISTER_FX_SUNVOX || !(c->placement&(1u<<location)))return in;
    int at=find(e->sunvox,c,location);if(at<0)return in;
    TsStereoFrame out;ts_pedalboard_slot_dry(e->sunvox->slot[at].fx,&in,&out,1);return out;
}
size_t ts_sister_sunvox_memory(const TsSisterPostFxEngine *e)
{
    if(!e || !e->sunvox)return 0;size_t bytes=sizeof(*e->sunvox);
    for(int i=0;i<2*N;++i)bytes+=ts_pedalboard_slot_memory(e->sunvox->slot[i].fx);return bytes;
}
