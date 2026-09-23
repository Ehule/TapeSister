#include "tapesister/router.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static TsRouterState participation(const TsRouterControls *c)
{ return (TsRouterState){c->bypass_mask,c->solo}; }
static int state_valid(TsRouterState s)
{ return !(s.bypass_mask>>TS_ROUTER_COUNT) && s.solo>=0 && s.solo<=TS_ROUTER_COUNT; }
static int time_valid(float s)
{ return isfinite(s) && s>=TS_ROUTER_TIME_MIN && s<=TS_ROUTER_TIME_MAX; }
static uint64_t duration(const TsRouter *r,float seconds)
{ uint64_t n=(uint64_t)llround((double)seconds*r->sample_rate);return n?n:1; }
void ts_router_performance_default(TsRouterPerformance *p)
{
    memset(p,0,sizeof(*p));p->length=16;p->loop=1;p->max_seconds=TS_ROUTER_TIME_MAX;
    for(int i=0;i<TS_ROUTER_STEPS;++i)p->step[i]=(TsRouterStep){-1,8};
    for(int i=0;i<TS_ROUTER_COUNT;++i)p->timer_seconds[i]=8;
}
int ts_router_performance_valid(const TsRouterPerformance *p)
{
    if(!p || p->occupied>>TS_ROUTER_STATES || p->length<1 || p->length>TS_ROUTER_STEPS ||
       (p->loop!=0 && p->loop!=1) || !time_valid(p->max_seconds))return 0;
    for(int i=0;i<TS_ROUTER_STATES;++i)if(!state_valid(p->state[i]))return 0;
    for(int i=0;i<TS_ROUTER_STEPS;++i)if(p->step[i].state< -1 || p->step[i].state>=TS_ROUTER_STATES ||
       !time_valid(p->step[i].seconds))return 0;
    for(int i=0;i<TS_ROUTER_COUNT;++i)if(!time_valid(p->timer_seconds[i]) ||
       (p->timer_after[i]!=0 && p->timer_after[i]!=1))return 0;
    return 1;
}
int ts_router_performance_set(TsRouter *r,const TsRouterPerformance *p)
{
    if(!r || !ts_router_performance_valid(p))return 0;
    r->performance=*p;
    for(int i=0;i<TS_ROUTER_STEPS;++i)r->performance.step[i].seconds=fminf(p->step[i].seconds,p->max_seconds);
    for(int i=0;i<TS_ROUTER_COUNT;++i)r->performance.timer_seconds[i]=fminf(p->timer_seconds[i],p->max_seconds);
    return 1; /* In-flight durations and captured step state stay frozen. */
}
static void apply_action(TsRouterState *s,int stage,int kind)
{
    if(kind==TS_ROUTER_TIMER_SOLO)s->solo=stage+1;
    else {s->bypass_mask|=1u<<stage;s->solo=0;}
}
static TsRouterState underlying(const TsRouter *r)
{
    const TsRouterTransport *t=&r->transport;
    return t->running?(t->manual_override?t->manual:t->sequenced):t->base;
}
static void effective(TsRouter *r)
{
    TsRouterState s=underlying(r);
    /* Bounded priority ranks make latest temporary action win; expiry reveals
       older, still-running actions without stale whole-state restoration. */
    for(unsigned rank=1;rank<=TS_ROUTER_COUNT;++rank)
        for(int i=0;i<TS_ROUTER_COUNT;++i) {
            const TsRouterTimer *t=&r->transport.timer[i];
            if(t->kind && !t->after && t->priority==rank)apply_action(&s,i,t->kind);
        }
    r->controls.bypass_mask=s.bypass_mask;r->controls.solo=s.solo;
}
TsRouterControls ts_router_export(const TsRouter *r)
{
    TsRouterControls c=r->controls;c.bypass_mask=r->transport.base.bypass_mask;c.solo=r->transport.base.solo;return c;
}
void ts_router_takeover(TsRouter *r)
{
    r->transport.base=participation(&r->controls);
    memset(r->transport.timer,0,sizeof(r->transport.timer));r->transport.timer_mask=0;
    r->transport.running=0;r->transport.frames=0;r->transport.manual_override=0;
}
void ts_router_reorder(TsRouter *r,int from,int to)
{
    if(r->transport.running)return;
    ts_router_move(&r->controls,from,to);
    if(memcmp(r->order,r->controls.order,sizeof(r->order)))r->handoff=-1;
    else if(r->handoff<0)r->handoff=1;
}
int ts_router_store(TsRouter *r,int slot)
{
    if(slot<0 || slot>=TS_ROUTER_STATES)return 0;
    r->performance.state[slot]=participation(&r->controls);r->performance.occupied|=1u<<slot;return 1;
}
/* An explicit manual gesture cancels timed actions so it takes effect now.
   Sequencing continues and clears its live override at the next step. Neither
   the stored state nor persistent pre-sequence base is overwritten. */
static void manual_state(TsRouter *r,TsRouterState state)
{
    memset(r->transport.timer,0,sizeof(r->transport.timer));r->transport.timer_mask=0;
    if(r->transport.running){r->transport.manual=state;r->transport.manual_override=1;}
    else r->transport.base=state;
    effective(r);
}
void ts_router_manual(TsRouter *r,int stage,int solo)
{
    if(stage<0 || stage>=TS_ROUTER_COUNT)return;
    TsRouterState s=underlying(r);
    TsRouterControls gesture=r->controls;
    if(solo)ts_router_toggle_solo(&gesture,stage);else ts_router_toggle_bypass(&gesture,stage);
    s.solo=gesture.solo;
    if(!solo)s.bypass_mask=(s.bypass_mask&~(1u<<stage))|(gesture.bypass_mask&(1u<<stage));
    manual_state(r,s);
}
int ts_router_recall(TsRouter *r,int slot)
{
    if(slot<0 || slot>=TS_ROUTER_STATES || !(r->performance.occupied&(1u<<slot)))return 0;
    manual_state(r,r->performance.state[slot]);return 1;
}
void ts_router_timer_cancel(TsRouter *r,int stage)
{
    if(stage<0 || stage>=TS_ROUTER_COUNT)return;
    unsigned rank=r->transport.timer[stage].priority;
    memset(&r->transport.timer[stage],0,sizeof(r->transport.timer[stage]));
    r->transport.timer_mask&=~(1u<<stage);
    for(int i=0;i<TS_ROUTER_COUNT;++i)if(rank && r->transport.timer[i].priority>rank)--r->transport.timer[i].priority;
    effective(r);
}
int ts_router_timer_start(TsRouter *r,int stage,int kind)
{
    if(stage<0 || stage>=TS_ROUTER_COUNT || kind<TS_ROUTER_TIMER_BYPASS || kind>TS_ROUTER_TIMER_SOLO)return 0;
    ts_router_timer_cancel(r,stage);
    unsigned rank=1;
    for(int i=0;i<TS_ROUTER_COUNT;++i)if(r->transport.timer[i].kind)++rank;
    r->transport.timer[stage]=(TsRouterTimer){kind,r->performance.timer_after[stage],duration(r,r->performance.timer_seconds[stage]),rank};
    r->transport.timer_mask|=1u<<stage;effective(r);return 1;
}
static void enter_step(TsRouter *r,int step)
{
    TsRouterTransport *t=&r->transport;const TsRouterStep *s=&r->performance.step[step];
    t->sequenced=underlying(r);t->manual_override=0;
    t->step=step;t->state=s->state;t->missing=0;
    double exact=(double)s->seconds*r->sample_rate+t->frame_fraction;
    if(exact<1){t->frames=1;t->frame_fraction=0;}
    else {t->frames=(uint64_t)llround(exact);t->frame_fraction=exact-t->frames;}
    if(s->state>=0) {
        if(r->performance.occupied&(1u<<s->state))t->sequenced=r->performance.state[s->state];
        else t->missing=1; /* Empty slots HOLD; never unexpectedly enable Insert. */
    }
    effective(r);
}
void ts_router_sequence_play(TsRouter *r)
{
    if(r->transport.running)return;
    r->transport.before_sequence=r->transport.base;r->transport.restore_valid=1;
    r->transport.sequenced=r->transport.base;r->transport.manual_override=0;r->transport.frame_fraction=0;
    r->transport.running=1;enter_step(r,0);
}
void ts_router_sequence_stop(TsRouter *r)
{
    r->transport.base=underlying(r);r->transport.manual_override=0;
    r->transport.running=0;r->transport.frames=0;effective(r);
}
void ts_router_sequence_reset(TsRouter *r)
{
    if(!r->transport.restore_valid) {
        r->transport.before_sequence=r->transport.base;r->transport.restore_valid=1;
    }
    r->transport.frame_fraction=0;
    int running=r->transport.running;
    if(!running){r->transport.sequenced=r->transport.base;r->transport.running=1;}
    enter_step(r,0);if(!running)ts_router_sequence_stop(r);
}
int ts_router_sequence_restore(TsRouter *r)
{
    if(!r->transport.restore_valid)return 0;
    ts_router_takeover(r);r->transport.base=r->transport.before_sequence;
    r->transport.restore_valid=0;r->transport.step=0;r->transport.state=-1;r->transport.missing=0;
    effective(r);return 1;
}
void ts_router_performance_advance(TsRouter *r,uint64_t frames)
{
    TsRouterTransport *t=&r->transport;
    if(!t->timer_mask && t->running && frames<t->frames){t->frames-=frames;return;}
    while(frames && (t->running || t->timer_mask)) {
        uint64_t delta=frames;
        if(t->running && t->frames<delta)delta=t->frames;
        for(int i=0;i<TS_ROUTER_COUNT;++i)if(t->timer[i].kind && t->timer[i].frames<delta)delta=t->timer[i].frames;
        frames-=delta;
        if(t->running)t->frames-=delta;
        for(int i=0;i<TS_ROUTER_COUNT;++i)if(t->timer[i].kind)t->timer[i].frames-=delta;
        int changed=0;
        /* At a coincident boundary, enter the sequence state first; delayed
           commands then apply in trigger order and temporary overlays win. */
        if(t->running && !t->frames) {
            int next=t->step+1;
            if(next>=r->performance.length) {
                if(r->performance.loop)next=0;else ts_router_sequence_stop(r);
            }
            if(t->running)enter_step(r,next);
        }
        for(unsigned rank=1;rank<=TS_ROUTER_COUNT;++rank)
            for(int i=0;i<TS_ROUTER_COUNT;++i) {
                TsRouterTimer *timer=&t->timer[i];
                if(timer->kind && !timer->frames && timer->priority==rank) {
                    if(timer->after) {
                        TsRouterState state=underlying(r);apply_action(&state,i,timer->kind);
                        if(t->running){t->manual=state;t->manual_override=1;}else t->base=state;
                    }
                    timer->kind=0;t->timer_mask&=~(1u<<i);changed=1;
                }
            }
        if(changed) {
            /* Compact ranks without changing the remaining precedence. */
            unsigned next=1;
            for(unsigned rank=1;rank<=TS_ROUTER_COUNT;++rank)
                for(int i=0;i<TS_ROUTER_COUNT;++i)if(t->timer[i].kind && t->timer[i].priority==rank)t->timer[i].priority=next++;
            for(int i=0;i<TS_ROUTER_COUNT;++i)if(!t->timer[i].kind)memset(&t->timer[i],0,sizeof(t->timer[i]));
            effective(r);
        }
    }
}
void ts_router_performance_rate(TsRouter *r,unsigned rate)
{
    if(!r->sample_rate || !rate || rate==r->sample_rate)return;
    double scale=(double)rate/r->sample_rate;
    r->transport.frames=(uint64_t)ceil(r->transport.frames*scale);
    r->transport.frame_fraction*=scale;
    for(int i=0;i<TS_ROUTER_COUNT;++i)r->transport.timer[i].frames=(uint64_t)ceil(r->transport.timer[i].frames*scale);
}
TsRouterView ts_router_view(const TsRouter *r)
{
    const TsRouterTransport *t=&r->transport;
    TsRouterView v={.running=t->running,.step=t->step,.state=t->state,.missing=t->missing,.restore_valid=t->restore_valid,.manual_override=t->manual_override};
    if(!r->sample_rate)return v;
    v.remaining=(float)((double)t->frames/r->sample_rate);
    for(int i=0;i<TS_ROUTER_COUNT;++i) {
        v.timer_kind[i]=t->timer[i].kind;v.timer_after[i]=t->timer[i].after;
        v.timer_remaining[i]=(float)((double)t->timer[i].frames/r->sample_rate);
    }
    return v;
}
int ts_router_performance_write(FILE *f,const TsRouterPerformance *p)
{
    if(!f || !ts_router_performance_valid(p))return 0;
    if(fprintf(f,"RouterPerf.Length=%d\nRouterPerf.Loop=%d\nRouterPerf.MaxSeconds=%.9g\n",p->length,p->loop,p->max_seconds)<0)return 0;
    for(int i=0;i<TS_ROUTER_STATES;++i)if(p->occupied&(1u<<i))
        if(fprintf(f,"RouterPerf.State.%c=%u,%d\n",'A'+i,p->state[i].bypass_mask,p->state[i].solo)<0)return 0;
    for(int i=0;i<TS_ROUTER_STEPS;++i)
        if(fprintf(f,"RouterPerf.Step.%d=%d,%.9g\n",i+1,p->step[i].state,p->step[i].seconds)<0)return 0;
    for(int i=0;i<TS_ROUTER_COUNT;++i)
        if(fprintf(f,"RouterPerf.Timer.%d=%d,%.9g\n",i,p->timer_after[i],p->timer_seconds[i])<0)return 0;
    return 1;
}
static int integer(const char **p,int *n)
{
    char *end;errno=0;long value=strtol(*p,&end,10);
    if(errno || end==*p || value<INT_MIN || value>INT_MAX)return 0;
    while(isspace((unsigned char)*end))++end;
    *p=end;*n=(int)value;return 1;
}
static int seconds(const char *p,float *n)
{
    char *end;errno=0;float value=strtof(p,&end);
    if(errno || end==p)return 0;
    while(isspace((unsigned char)*end))++end;
    if(*end || !time_valid(value))return 0;
    *n=value;return 1;
}
int ts_router_performance_read(TsRouterPerformance *p,const char *key,const char *value)
{
    if(strncmp(key,"RouterPerf.",11))return 0;
    key+=11;TsRouterPerformance next=*p;int index,n;
    if(!strcmp(key,"Length") || !strcmp(key,"Loop")) {
        if(!integer(&value,&n) || *value)return -1;
        if(*key=='L' && key[1]=='e')next.length=n;else next.loop=n;
    } else if(!strcmp(key,"MaxSeconds")) {
        if(!seconds(value,&next.max_seconds))return -1;
    } else if(!strncmp(key,"State.",6)) {
        index=key[6]-'A';if(index<0 || index>=TS_ROUTER_STATES || key[7])return -1;
        if(!integer(&value,&n) || n<0 || *value++!=',')return -1;
        next.state[index].bypass_mask=(unsigned)n;
        if(!integer(&value,&next.state[index].solo) || *value || next.state[index].solo<0)return -1;
        if(!state_valid(next.state[index])) {
            /* A slot naming an unavailable stable processor is EMPTY, so a
               sequence holds its previous valid state instead of guessing. */
            memset(&next.state[index],0,sizeof(next.state[index]));next.occupied&=~(1u<<index);
        } else next.occupied|=1u<<index;
    } else if(!strncmp(key,"Step.",5)) {
        key+=5;if(!integer(&key,&index) || *key || index<1 || index>TS_ROUTER_STEPS)return -1;
        if(!integer(&value,&next.step[index-1].state) || *value++!=',' || !seconds(value,&next.step[index-1].seconds))return -1;
    } else if(!strncmp(key,"Timer.",6)) {
        key+=6;if(!integer(&key,&index) || *key || index<0 || index>=TS_ROUTER_COUNT)return -1;
        if(!integer(&value,&next.timer_after[index]) || *value++!=',' || !seconds(value,&next.timer_seconds[index]))return -1;
    } else return -1;
    if(!ts_router_performance_valid(&next))return -1;
    *p=next;return 1;
}
