#include "tapesister/autopan.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
static float ap_clamp(float x,float lo,float hi) {
    return !isfinite(x)?lo:x<lo?lo:x>hi?hi:x;
}
static float ap_rand(TsAutoPan *p) {
    uint32_t x=p->random_state;
    x^=x<<13;x^=x>>17;x^=x<<5;
    p->random_state=x;
    return (float)((double)x/4294967295.0*2.0-1.0);
}
void ts_autopan_default(TsAutoPanControls *c) {
    if(!c)return;
    *c=(TsAutoPanControls){.rate_hz=4.0f,.depth=1.0f,.center=0.0f,
        .phase_offset=0.5f,.wave=TS_AUTOPAN_SINE,.mode=TS_AUTOPAN_BALANCE,.beats=1,.reset_on_transport=1};
}
void ts_autopan_init(TsAutoPan *p,unsigned rate,uint32_t seed) {
    if(!p)return;
    memset(p,0,sizeof(*p));
    p->sample_rate=rate?rate:48000;
    p->seed=p->random_state=seed?seed:0x9e3779b9u;
    p->quarter=.5;p->left=p->right=p->mix=p->mix_target=p->gain=p->gain_target=1;
    p->slew=1-expf(-1.f/(.001f*p->sample_rate));
    ts_autopan_default(&p->controls);
    p->random_a=ap_rand(p);p->random_b=ap_rand(p);
    ts_autopan_set(p,&p->controls);
}
void ts_autopan_set(TsAutoPan *p,const TsAutoPanControls *c) {
    if(!p || !c)return;
    p->controls=*c;
    p->controls.rate_hz=ap_clamp(c->rate_hz,0.01f,100.0f);
    p->controls.depth=ap_clamp(c->depth,0.0f,1.0f);
    p->controls.center=ap_clamp(c->center,-1.0f,1.0f);
    p->controls.phase_offset=ap_clamp(c->phase_offset,0.0f,1.0f);
    if(c->wave<TS_AUTOPAN_SINE || c->wave>TS_AUTOPAN_RANDOM_SMOOTH)
        p->controls.wave=TS_AUTOPAN_SINE;
    if(c->mode<TS_AUTOPAN_BALANCE || c->mode>TS_AUTOPAN_DUAL_TREMOLO)
        p->controls.mode=TS_AUTOPAN_BALANCE;
    p->controls.beats=ap_clamp(c->beats,.03125f,32.f);
    double hz=c->sync?1/(p->quarter*p->controls.beats):p->controls.rate_hz;
    p->step=fmin(100,hz)/(double)(p->sample_rate?p->sample_rate:48000);
}
void ts_autopan_reset_phase(TsAutoPan *p) {
    if(p){p->phase=0.0;p->random_state=p->seed;p->random_a=ap_rand(p);p->random_b=ap_rand(p);}
}
static double ap_wrap(double x) {return x-floor(x);}
static float ap_wave(const TsAutoPan *p,double phase) {
    double t=ap_wrap(phase);
    float ra=p->random_a,rb=p->random_b;
    if(phase>=1) {uint32_t x=p->random_state;x^=x<<13;x^=x>>17;x^=x<<5;ra=rb;rb=(float)((double)x/4294967295.0*2.0-1.0);}

    switch(p->controls.wave) {
    case TS_AUTOPAN_TRIANGLE:return (float)(1.0-4.0*fabs(t-0.5));
    case TS_AUTOPAN_SQUARE:return t<0.5?1.0f:-1.0f;
    case TS_AUTOPAN_SAW:return (float)(2.0*t-1.0);
    case TS_AUTOPAN_RANDOM_STEP:return ra;
    case TS_AUTOPAN_RANDOM_SMOOTH:{
        float f=(float)t;f=f*f*(3.0f-2.0f*f);
        return ra+(rb-ra)*f;
    }
    default:return (float)sin(2.0*M_PI*t);
    }
}
void ts_autopan_process_span(TsAutoPan *p,const TsStereoFrame *in,TsStereoFrame *out,unsigned frames) {
    if(!p || !in || !out)return;
    const TsAutoPanControls *c=&p->controls;
    for(unsigned i=0;i<frames;++i) {
        float left,right;
        if(c->mode==TS_AUTOPAN_DUAL_TREMOLO) {
            float a=ap_wave(p,p->phase),b=ap_wave(p,p->phase+c->phase_offset);
            left=1.0f-c->depth*(a+1.0f)*0.5f;
            right=1.0f-c->depth*(b+1.0f)*0.5f;
            left*=(float)(1.4142135623730951*cos((c->center+1)*M_PI*.25));
            right*=(float)(1.4142135623730951*sin((c->center+1)*M_PI*.25));
        } else {
            float pan=ap_clamp(c->center+c->depth*ap_wave(p,p->phase),-1.0f,1.0f);
            /* Unity at center; equal-power balance at either extreme. */
            left=(float)(1.4142135623730951*cos((pan+1.0f)*M_PI*0.25));
            right=(float)(1.4142135623730951*sin((pan+1.0f)*M_PI*0.25));
        }
        p->left+=(left-p->left)*p->slew;p->right+=(right-p->right)*p->slew;
        TsStereoFrame input=in[i];
        out[i].l=input.l*p->left;out[i].r=input.r*p->right;
        double next=p->phase+p->step;
        if(next>=1.0) {
            next-=floor(next);
            p->random_a=p->random_b;
            p->random_b=ap_rand(p);
        }
        p->phase=next;
    }
}

static const float ap_beats[]={32,16,8,4,2,1,0.75f,0.5f,1.f/3,0.25f,1.f/6,0.125f,0.0625f,0.03125f};
static const char *ap_divisions[]={"8 BARS","4 BARS","2 BARS","1 BAR","1/2","1/4","1/8 D","1/8","1/8 T","1/16","1/16 T","1/32","1/64","1/128"};
float ts_autopan_default_rate(void){return log10f(400.f)/4;}
void ts_autopan_settings_default(TsAutoPanSettings *s)
{*s=(TsAutoPanSettings){.initialized=1,.value={0,0,.5f,0,5.f/13,1}};}
int ts_autopan_settings_valid(const TsAutoPanSettings *s)
{if(!s || s->initialized!=1)return 0;for(int i=0;i<TS_AP_COUNT-3;++i)if(!isfinite(s->value[i]) || s->value[i]<0 || s->value[i]>1)return 0;return 1;}
int ts_autopan_steps(int field)
{return field==TS_AP_WAVE?5:field==TS_AP_DIVISION?13:field==TS_AP_MODE || field==TS_AP_SYNC || field==TS_AP_RESET?1:0;}
void ts_autopan_label(char *s,size_t n,int field,float value)
{
    value=ap_clamp(value,0,1);static const char *waves[]={"SINE","TRIANGLE","SQUARE","SAW","RANDOM STEP","RANDOM SMOOTH"};
    switch(field) {
    case TS_AP_RATE:snprintf(s,n,"RATE %.3F HZ",.01f*powf(10000,value));break;
    case TS_AP_DEPTH:snprintf(s,n,"DEPTH %.1F%%",value*100);break;
    case TS_AP_CENTER:snprintf(s,n,"CENTER %+.1F%%",(value*2-1)*100);break;
    case TS_AP_WAVE:snprintf(s,n,"WAVE %s",waves[(int)lroundf(value*5)]);break;
    case TS_AP_MODE:snprintf(s,n,"MODE %s",value>=.5f?"DUAL TREMOLO":"STEREO BALANCE");break;
    case TS_AP_PHASE:snprintf(s,n,"STEREO PHASE %.0F DEG",value*360);break;
    case TS_AP_SYNC:snprintf(s,n,"TIMING %s",value>=.5f?"TEMPO SYNC":"FREE HZ");break;
    case TS_AP_DIVISION:snprintf(s,n,"CYCLE %s",ap_divisions[(int)lroundf(value*13)]);break;
    default:snprintf(s,n,"TRANSPORT %s",value>=.5f?"RESET ON START / HOME":"FREE RUN");break;
    }
}
void ts_autopan_configure(TsAutoPan *p,const TsAutoPanSettings *s,float a,float b,float c,float mix,float gain)
{
    if(!p)return;
    TsAutoPanSettings fallback;if(!ts_autopan_settings_valid(s)){ts_autopan_settings_default(&fallback);s=&fallback;}
    int cold=!p->configured;
    float parameters[5]={a,b,c,mix,gain};
    if(p->configured && !memcmp(parameters,p->parameter_cache,sizeof(parameters)) && !memcmp(s->value,p->setting_cache,sizeof(s->value)))return;
    memcpy(p->parameter_cache,parameters,sizeof(parameters));memcpy(p->setting_cache,s->value,sizeof(s->value));p->configured=1;
    TsAutoPanControls controls={.rate_hz=.01f*powf(10000,ap_clamp(a,0,1)),.depth=ap_clamp(b,0,1),.center=ap_clamp(c,0,1)*2-1,
        .wave=(int)lroundf(s->value[0]*5),.mode=s->value[1]>=.5f,.phase_offset=s->value[2],.sync=s->value[3]>=.5f,
        .beats=ap_beats[(int)lroundf(ap_clamp(s->value[4],0,1)*13)],.reset_on_transport=s->value[5]>=.5f};
    if(memcmp(&controls,&p->controls,sizeof(controls)))ts_autopan_set(p,&controls);
    p->mix_target=ap_clamp(mix,0,1);p->gain_target=powf(10,ap_clamp(gain,-12,12)*.05f);
    if(cold){p->mix=p->mix_target;p->gain=p->gain_target;}
}
void ts_autopan_transport(TsAutoPan *p,double quarter,uint32_t serial)
{
    if(!p)return;
    if(!isfinite(quarter) || quarter<=0)quarter=.5;
    if(p->quarter!=quarter){p->quarter=quarter;ts_autopan_set(p,&p->controls);}
    if(p->transport_ready && p->transport_serial!=serial && p->controls.reset_on_transport)ts_autopan_reset_phase(p);
    p->transport_ready=1;p->transport_serial=serial;
}
TsStereoFrame ts_autopan_process(TsAutoPan *p,TsStereoFrame in,float gate,float *direct)
{
    TsStereoFrame wet;ts_autopan_process_span(p,&in,&wet,1);
    p->mix+=(p->mix_target-p->mix)*p->slew;p->gain+=(p->gain_target-p->gain)*p->slew;
    float amount=p->mix*gate,makeup=1+(p->gain-1)*gate;
    if(direct)*direct=(1-amount)*makeup;
    return (TsStereoFrame){(in.l+(wet.l-in.l)*amount)*makeup,(in.r+(wet.r-in.r)*amount)*makeup};
}
int ts_autopan_write(FILE *f,const char *key,const TsAutoPanSettings *s)
{
    if(!ts_autopan_settings_valid(s))return 0;
    return fprintf(f,"%s=%.9g,%.9g,%.9g,%.9g,%.9g,%.9g\n",key,s->value[0],s->value[1],s->value[2],s->value[3],s->value[4],s->value[5])>=0;
}
int ts_autopan_read(TsAutoPanSettings *s,const char *text)
{
    TsAutoPanSettings next={.initialized=1};
    for(int i=0;i<6;++i){char *end;errno=0;next.value[i]=strtof(text,&end);if(errno || end==text || (i<5?*end!=',':*end!=0))return 0;text=end+(i<5);}
    if(!ts_autopan_settings_valid(&next))return 0;*s=next;return 1;
}
