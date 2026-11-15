#include "tapesister/modulation.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <inttypes.h>

static int unit(float v){return isfinite(v) && v>=0 && v<=1;}
static uint32_t random_next(uint32_t *s)
{uint32_t x=*s?*s:0x6d2b79f5u;x^=x<<13;x^=x>>17;x^=x<<5;return *s=x;}
void ts_mod_shape(TsModLane *l,int shape,unsigned points)
{
    if(!l)return;
    unsigned n=points<2?2:points>TS_MOD_POINTS?TS_MOD_POINTS:points;
    if(shape==0)n=17;
    if(shape==1)n=3;
    if(shape==3)n=5;
    uint32_t seed=l->seed;
    memset(l->point,0,sizeof(l->point));l->count=n;
    for(unsigned i=0;i<n;++i) {
        float x=(float)i/(n-1),v=shape==0?.5f-.5f*cosf(x*6.28318530718f):
            shape==1?1-fabsf(x*2-1):(random_next(&seed)>>8)*(1.f/16777215.f);
        l->point[i]=(TsModPoint){x,v};
    }
    /* Near-vertical editable edges preserve the existing curve file format. */
    if(shape==3){l->point[0]=(TsModPoint){0,1};l->point[1]=(TsModPoint){.49999f,1};
        l->point[2]=(TsModPoint){.5f,0};l->point[3]=(TsModPoint){.99999f,0};l->point[4]=(TsModPoint){1,l->mode==TS_MOD_LOOP?1:0};}
    if(shape==2){l->seed=seed;if(l->mode==TS_MOD_LOOP)l->point[n-1].y=l->point[0].y;}
}
/* Add points along the current shape; simplify by removing the least
   consequential interior point. Endpoints and sharp corners survive first. */
void ts_mod_resize(TsModLane *l,unsigned count)
{
    if(!l || l->count<2 || l->count>TS_MOD_POINTS)return;
    count=count<2?2:count>TS_MOD_POINTS?TS_MOD_POINTS:count;
    while(l->count<count) {
        unsigned at=1;float span=0;
        for(unsigned i=1;i<l->count;++i)if(l->point[i].x-l->point[i-1].x>span){span=l->point[i].x-l->point[i-1].x;at=i;}
        float x=(l->point[at-1].x+l->point[at].x)*.5f,y=ts_mod_value(l,x);
        memmove(l->point+at+1,l->point+at,(l->count-at)*sizeof(l->point[0]));l->point[at]=(TsModPoint){x,y};++l->count;
    }
    while(l->count>count) {
        unsigned at=1;float error=2;
        for(unsigned i=1;i+1<l->count;++i){TsModPoint a=l->point[i-1],b=l->point[i],c=l->point[i+1];
            float e=fabsf(b.y-(a.y+(c.y-a.y)*(b.x-a.x)/(c.x-a.x)))*(c.x-a.x);
            if(e<error){error=e;at=i;}}
        memmove(l->point+at,l->point+at+1,(l->count-at-1)*sizeof(l->point[0]));memset(l->point+--l->count,0,sizeof(l->point[0]));
    }
}
void ts_mod_lane_default(TsModLane *l,unsigned i)
{
    memset(l,0,sizeof(*l));snprintf(l->name,sizeof(l->name),"LANE %u",i+1);
    l->enabled=1;l->seconds=90;l->beats=4;l->smooth=.65f;l->seed=0x6d2b79f5u+i*0x9e3779b9u;
    ts_mod_shape(l,0,17);
}
void ts_mod_default(TsModControls *c)
{
    memset(c,0,sizeof(*c));c->initialized=1;c->count=4;
    for(unsigned i=0;i<c->count;++i)ts_mod_lane_default(&c->lane[i],i);
}
int ts_mod_valid(const TsModControls *c)
{
    if(!c || c->initialized!=1 || !c->count || c->count>TS_MOD_LANES || c->map_count>TS_MOD_MAPS)return 0;
    for(unsigned i=0;i<c->count;++i) {
        const TsModLane *l=&c->lane[i];
        if(l->enabled>1 || l->mode>TS_MOD_ONESHOT || l->timing>TS_MOD_TEMPO ||
           l->count<2 || l->count>TS_MOD_POINTS || !unit(l->smooth) ||
           !isfinite(l->seconds) || l->seconds<.05f || l->seconds>14400 ||
           !isfinite(l->beats) || l->beats<.0625f || l->beats>1024 ||
           !memchr(l->name,0,sizeof(l->name)))return 0;
        for(const unsigned char *p=(const unsigned char*)l->name;*p;++p)if(*p<32 || *p>126 || *p=='=')return 0;
        if(l->point[0].x!=0 || l->point[l->count-1].x!=1)return 0;
        for(unsigned j=0;j<l->count;++j)if(!unit(l->point[j].x) || !unit(l->point[j].y) ||
            (j && l->point[j].x<=l->point[j-1].x))return 0;
    }
    for(unsigned i=0;i<c->map_count;++i) {
        const TsModMap *m=&c->map[i];
        if(!m->target || !m->type || m->type>255 || m->field>255 || m->lane>=c->count || m->enabled>1 || !unit(m->low) || !unit(m->high))return 0;
        for(unsigned j=0;j<i;++j) {
            const TsModMap *p=&c->map[j];
            if(p->target==m->target && p->child==m->child && p->type==m->type && p->field==m->field)return 0;
        }
    }
    return 1;
}
float ts_mod_value(const TsModLane *l,double phase)
{
    if(!l || l->count<2 || l->count>TS_MOD_POINTS || !isfinite(phase))return 0;
    if(phase<=0)return l->point[0].y;
    if(phase>=1)return l->point[l->count-1].y;
    unsigned lo=0,hi=l->count-1;
    while(hi-lo>1){unsigned mid=(lo+hi)/2;if(phase<l->point[mid].x)hi=mid;else lo=mid;}
    float t=(float)((phase-l->point[lo].x)/(l->point[hi].x-l->point[lo].x));
    /* Smoothstep is bounded and does not overshoot feedback/level ranges. */
    t+=(t*t*(3-2*t)-t)*l->smooth;
    return l->point[lo].y+(l->point[hi].y-l->point[lo].y)*t;
}
void ts_mod_advance(TsModState *s,const TsModControls *c,unsigned frames,unsigned rate,double quarter)
{
    if(!s || !c || !rate)return;
    if(s->retrigger!=c->retrigger){memset(s->phase,0,sizeof(s->phase));s->retrigger=c->retrigger;}
    if(!isfinite(quarter) || quarter<=0)quarter=.5;
    for(unsigned i=0;i<c->count;++i)if(c->lane[i].enabled) {
        const TsModLane *l=&c->lane[i];
        double seconds=l->timing==TS_MOD_TEMPO?l->beats*quarter:l->seconds;
        double phase=s->phase[i]+frames/(seconds*rate);
        s->phase[i]=l->mode==TS_MOD_LOOP?phase-floor(phase):fmin(1,phase);
    }
}
int ts_mod_write(FILE *f,const char *prefix,const TsModControls *c)
{
    if(!ts_mod_valid(c) || fprintf(f,"%s.Header=1,%u,%u\n",prefix,c->count,c->map_count)<0)return 0;
    for(unsigned i=0;i<c->count;++i) {
        const TsModLane *l=&c->lane[i];
        if(fprintf(f,"%s.Lane%u=%u,%u,%u,%u,%.9g,%.9g,%.9g,%" PRIu32 "\n%s.Name%u=%s\n",
            prefix,i,l->enabled,l->mode,l->timing,l->count,l->seconds,l->beats,l->smooth,l->seed,prefix,i,l->name)<0)return 0;
        for(unsigned j=0;j<l->count;++j)if(fprintf(f,"%s.Point%u.%u=%.9g,%.9g\n",prefix,i,j,l->point[j].x,l->point[j].y)<0)return 0;
    }
    for(unsigned i=0;i<c->map_count;++i) {
        const TsModMap *m=&c->map[i];
        if(fprintf(f,"%s.Map%u=%" PRIu32 ",%" PRIu32 ",%u,%u,%u,%u,%.9g,%.9g\n",prefix,i,m->target,m->child,m->type,m->field,m->lane,m->enabled,m->low,m->high)<0)return 0;
    }
    return fprintf(f,"%s.End=1\n",prefix)>=0;
}
static int numbers(const char *p,double *x,unsigned n)
{
    for(unsigned i=0;i<n;++i){char *end;errno=0;x[i]=strtod(p,&end);if(errno || end==p || !isfinite(x[i]) || (i+1<n?*end!=',':*end!=0))return 0;p=end+(i+1<n);}
    return 1;
}
static int whole(double x,double lo,double hi){return x>=lo && x<=hi && x==floor(x);}
int ts_mod_read(TsModControls *c,const char *key,const char *v)
{
    TsModControls n=*c;unsigned i,j;int used=0;double x[8];
    if(!strcmp(key,"Header")) {
        if(!numbers(v,x,3) || x[0]!=1 || !whole(x[1],1,TS_MOD_LANES) || !whole(x[2],0,TS_MOD_MAPS))return -1;
        memset(&n,0,sizeof(n));n.initialized=2;n.count=(unsigned)x[1];n.map_count=(unsigned)x[2];
    } else {
        if(n.initialized!=2)return -1;
        if(!strcmp(key,"End")) {n.initialized=1;if(strcmp(v,"1") || !ts_mod_valid(&n))return -1;}
        else if(sscanf(key,"Lane%u%n",&i,&used)==1 && used && !key[used] && i<n.count) {
            if(!numbers(v,x,8) || !whole(x[0],0,1) || !whole(x[1],0,1) || !whole(x[2],0,1) || !whole(x[3],2,TS_MOD_POINTS) ||
               x[4]<.05 || x[4]>14400 || x[5]<.0625 || x[5]>1024 || x[6]<0 || x[6]>1 || !whole(x[7],0,UINT32_MAX))return -1;
            TsModLane *l=&n.lane[i];l->enabled=x[0];l->mode=x[1];l->timing=x[2];l->count=x[3];l->seconds=x[4];l->beats=x[5];l->smooth=x[6];l->seed=x[7];
        } else if(sscanf(key,"Name%u%n",&i,&used)==1 && used && !key[used] && i<n.count) {
            if(strlen(v)>=sizeof(n.lane[i].name))return -1;
            for(const unsigned char *p=(const unsigned char*)v;*p;++p)if(*p<32 || *p>126 || *p=='=')return -1;
            strcpy(n.lane[i].name,v);
        } else if(sscanf(key,"Point%u.%u%n",&i,&j,&used)==2 && used && !key[used] && i<n.count && j<n.lane[i].count) {
            if(!numbers(v,x,2) || !unit(x[0]) || !unit(x[1]))return -1;
            n.lane[i].point[j]=(TsModPoint){x[0],x[1]};
        } else if(sscanf(key,"Map%u%n",&i,&used)==1 && used && !key[used] && i<n.map_count) {
            if(!numbers(v,x,8) || !whole(x[0],1,UINT32_MAX) || !whole(x[1],0,UINT32_MAX) || !whole(x[2],1,255) ||
               !whole(x[3],0,255) || !whole(x[4],0,n.count-1) || !whole(x[5],0,1) || !unit(x[6]) || !unit(x[7]))return -1;
            n.map[i]=(TsModMap){x[0],x[1],x[2],x[3],x[4],x[5],x[6],x[7]};
        } else return -1;
    }
    *c=n;return 1;
}
