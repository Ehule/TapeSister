#include "tapesister/router.h"
#include <math.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

void ts_router_default(TsRouterControls *c)
{
    memset(c,0,sizeof(*c));for(int i=0;i<TS_ROUTER_COUNT;++i)c->order[i]=i;
    c->bypass_mask=1u<<TS_ROUTER_INSERT;
}
int ts_router_valid(const TsRouterControls *c)
{
    unsigned seen=0;
    if(!c || c->solo<0 || c->solo>TS_ROUTER_COUNT || c->bypass_mask>>TS_ROUTER_COUNT)return 0;
    for(int i=0;i<TS_ROUTER_COUNT;++i) {
        int s=c->order[i];if(s<0 || s>=TS_ROUTER_COUNT || (seen&(1u<<s)))return 0;
        seen|=1u<<s;
    }
    return 1;
}
void ts_router_sanitize(TsRouterControls *c)
{ if(!ts_router_valid(c))ts_router_default(c); }
int ts_router_active(const TsRouterControls *c,int stage)
{ return c && stage>=0 && stage<TS_ROUTER_COUNT &&
    (c->solo?c->solo==stage+1:!(c->bypass_mask&(1u<<stage))); }
void ts_router_move(TsRouterControls *c,int from,int to)
{
    if(!c || from<0 || to<0 || from>=TS_ROUTER_COUNT || to>=TS_ROUTER_COUNT)return;
    int id=c->order[from];
    while(from<to){c->order[from]=c->order[from+1];++from;}
    while(from>to){c->order[from]=c->order[from-1];--from;}
    c->order[to]=id;
}
void ts_router_toggle_bypass(TsRouterControls *c,int s)
{
    if(!c || s<0 || s>=TS_ROUTER_COUNT)return;
    int was_solo=c->solo==s+1;c->solo=0;
    if(was_solo)c->bypass_mask|=1u<<s;
    else c->bypass_mask^=1u<<s;
}
void ts_router_toggle_solo(TsRouterControls *c,int s)
{ if(c && s>=0 && s<TS_ROUTER_COUNT)c->solo=c->solo==s+1?0:s+1; }
const char *ts_router_name(int s)
{ static const char *names[]={"PRISM","SISTER MACHINE","FALLOUT","PEDALBOARD","INSERT"};return s>=0 && s<TS_ROUTER_COUNT?names[s]:"UNKNOWN"; }
void ts_router_init(TsRouter *r)
{
    memset(r,0,sizeof(*r));ts_router_default(&r->controls);
    memcpy(r->order,r->controls.order,sizeof(r->order));r->gain=1;
    for(int i=0;i<TS_ROUTER_COUNT;++i)r->wet[i]=ts_router_active(&r->controls,i)?1:0;
    ts_router_performance_default(&r->performance);
    r->transport.base=(TsRouterState){r->controls.bypass_mask,r->controls.solo};
    r->transport.state=-1;ts_router_prepare(r,48000);
}
void ts_router_prepare(TsRouter *r,unsigned rate)
{
    if(!rate)return;
    ts_router_performance_rate(r,rate);r->sample_rate=rate;r->step=1.0f/(rate*.005f);
    r->decay=expf(-1.0f/(rate*.15f));
}
void ts_router_set(TsRouter *r,const TsRouterControls *c)
{
    if(!r || !c)return;
    r->controls=*c;ts_router_sanitize(&r->controls);
    ts_router_takeover(r);
    if(memcmp(r->order,r->controls.order,sizeof(r->order)))r->handoff=-1;
    else if(r->handoff<0)r->handoff=1;
}
static float peak(TsStereoFrame f) {return fmaxf(fabsf(f.l),fabsf(f.r));}
TsStereoFrame ts_router_process(TsRouter *r,TsStereoFrame in,TsRouterProcess fn,void *context)
{ return ts_router_process_with_prepare(r,in,fn,NULL,context); }

TsStereoFrame ts_router_process_with_prepare(TsRouter *r,TsStereoFrame in,
    TsRouterProcess fn,TsRouterProcess prepare,void *context)
{
    in=ts_stereo_frame_sanitize(in);
    if(!r || !fn)return in;
    r->source_peak=fmaxf(peak(in),r->source_peak*r->decay);
    if(r->handoff<0) {
        r->gain=fmaxf(0,r->gain-r->step);
        if(r->gain==0){memcpy(r->order,r->controls.order,sizeof(r->order));r->handoff=1;}
    } else if(r->handoff>0) {
        r->gain=fminf(1,r->gain+r->step);if(r->gain==1)r->handoff=0;
    }
    /* A linear insert crossfade is unity for identical dry/wet signals.
       Bypassed processors tick on silence, retaining tails and modulation. */
    for(int i=0;i<TS_ROUTER_COUNT;++i) {
        int s=r->order[i];float target=ts_router_active(&r->controls,s)?1:0;
        float *wet=&r->wet[s],step=r->step*.5f;
        *wet=target>*wet?fminf(target,*wet+step):fmaxf(target,*wet-step);
        if(prepare)in=ts_stereo_frame_sanitize(prepare(context,s,in));
        r->input_peak[s]=fmaxf(peak(in),r->input_peak[s]*r->decay);
        TsStereoFrame out=ts_stereo_frame_sanitize(fn(context,s,*wet>0?in:(TsStereoFrame){0,0}));
        in=(TsStereoFrame){in.l+(out.l-in.l)*(*wet),in.r+(out.r-in.r)*(*wet)};
        in=ts_stereo_frame_sanitize(in);
        r->output_peak[s]=fmaxf(peak(in),r->output_peak[s]*r->decay);
    }
    in.l*=r->gain;in.r*=r->gain;
    r->master_peak=fmaxf(peak(in),r->master_peak*r->decay);
    ts_router_performance_advance(r,1);
    return in;
}
int ts_router_write(FILE *file,const TsRouterControls *controls)
{
    TsRouterControls c=*controls;ts_router_sanitize(&c);
    if(fprintf(file,"Router.Order=")<0)return 0;
    for(int i=0;i<TS_ROUTER_COUNT;++i)if(fprintf(file,"%s%d",i?",":"",c.order[i])<0)return 0;
    return fprintf(file,"\nRouter.Bypass=%u\nRouter.Solo=%d\n",c.bypass_mask,c.solo)>=0;
}
static int read_integer(const char **text,int *value)
{
    char *end;errno=0;long parsed=strtol(*text,&end,10);
    if(errno || end==*text || parsed<INT_MIN || parsed>INT_MAX)return 0;
    while(isspace((unsigned char)*end))++end;
    *text=end;*value=(int)parsed;return 1;
}
int ts_router_read(TsRouterControls *c,const char *key,const char *value)
{
    TsRouterControls next=*c;int n;
    if(!strcmp(key,"Router.Order")) {
        for(int i=0;i<TS_ROUTER_COUNT;++i) {
            /* v23 saved four stable IDs. Append the new, safely bypassed Insert. */
            if(i==TS_ROUTER_INSERT && !*value) {
                next.order[i]=TS_ROUTER_INSERT;next.bypass_mask|=1u<<TS_ROUTER_INSERT;break;
            }
            if(!read_integer(&value,&next.order[i]))return -1;
            if(i+1<TS_ROUTER_COUNT){
                if(i==TS_ROUTER_INSERT-1 && !*value)continue;
                if(*value!=',')return -1;
                ++value;
            }
        }
    } else if(!strcmp(key,"Router.Bypass")) {
        if(!read_integer(&value,&n) || n<0)return -1;
        next.bypass_mask=(unsigned)n;
    } else if(!strcmp(key,"Router.Solo")) {
        if(!read_integer(&value,&next.solo))return -1;
    } else return 0;
    if(*value || !ts_router_valid(&next))return -1;
    *c=next;return 1;
}
