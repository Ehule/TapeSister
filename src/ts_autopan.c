#include "tapesister/autopan.h"
#include <math.h>
#include <string.h>
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
        .phase_offset=0.5f,.wave=TS_AUTOPAN_SINE,.mode=TS_AUTOPAN_BALANCE};
}
void ts_autopan_init(TsAutoPan *p,unsigned rate,uint32_t seed) {
    if(!p)return;
    memset(p,0,sizeof(*p));
    p->sample_rate=rate?rate:48000;
    p->random_state=seed?seed:0x9e3779b9u;
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
    p->step=(double)p->controls.rate_hz/(double)(p->sample_rate?p->sample_rate:48000);
}
void ts_autopan_reset_phase(TsAutoPan *p) {
    if(p)p->phase=0.0;
}
static double ap_wrap(double x) {return x-floor(x);}
static float ap_wave(const TsAutoPan *p,double phase) {
    double t=ap_wrap(phase);
    switch(p->controls.wave) {
    case TS_AUTOPAN_TRIANGLE:return (float)(1.0-4.0*fabs(t-0.5));
    case TS_AUTOPAN_SQUARE:return t<0.5?1.0f:-1.0f;
    case TS_AUTOPAN_SAW:return (float)(2.0*t-1.0);
    case TS_AUTOPAN_RANDOM_STEP:return p->random_a;
    case TS_AUTOPAN_RANDOM_SMOOTH:{
        float f=(float)t;f=f*f*(3.0f-2.0f*f);
        return p->random_a+(p->random_b-p->random_a)*f;
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
        } else {
            float pan=ap_clamp(c->center+c->depth*ap_wave(p,p->phase),-1.0f,1.0f);
            /* Unity at center; equal-power balance at either extreme. */
            left=(float)(1.4142135623730951*cos((pan+1.0f)*M_PI*0.25));
            right=(float)(1.4142135623730951*sin((pan+1.0f)*M_PI*0.25));
        }
        out[i].l=in[i].l*left;
        out[i].r=in[i].r*right;
        double next=p->phase+p->step;
        if(next>=1.0) {
            next-=floor(next);
            p->random_a=p->random_b;
            p->random_b=ap_rand(p);
        }
        p->phase=next;
    }
}
