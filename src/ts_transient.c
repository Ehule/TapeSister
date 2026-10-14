#include "tapesister/transient.h"
#include <math.h>
#include <string.h>

static double bounded(double x, double lo, double hi, double fallback)
{ return isfinite(x) ? fmax(lo, fmin(hi, x)) : fallback; }
static double follow(double x, double target, double coefficient)
{ return fabs(target-x)<1e-12 ? target : x+(target-x)*coefficient; }
static double envelope(double x, double input, double rise, double fall)
{
    double y=follow(x,input,input>x?rise:fall);
    return y<1e-20 ? 0 : y;
}
void ts_transient_set(TsTransient *s, TsTransientControls c)
{
    if(!s)return;
    c.attack_db=(float)bounded(c.attack_db,-15,15,0);
    c.sustain_db=(float)bounded(c.sustain_db,-24,24,0);
    c.response=(float)bounded(c.response,.25,4,1);
    if(c.response!=s->controls.response) {
        /* Fast onset, reference onset, body release, and two adaptive tail
           releases. Only control changes require coefficient exponentials. */
        static const double ms[]={.1,12,35,120,400};
        for(int i=0;i<5;++i)
            s->target_coefficient[i]=1-exp(-1000.0/(s->rate*ms[i]*c.response));
    }
    s->controls=c;
}
void ts_transient_sync(TsTransient *s, TsTransientControls c)
{
    if(!s)return;
    ts_transient_set(s,c);
    s->attack_db=s->controls.attack_db;s->sustain_db=s->controls.sustain_db;
    memcpy(s->coefficient,s->target_coefficient,sizeof(s->coefficient));
}
void ts_transient_init(TsTransient *s, unsigned rate)
{
    if(!s)return;
    memset(s,0,sizeof(*s));s->rate=rate?rate:48000;
    s->smooth=1-exp(-1.0/(.012*s->rate));
    ts_transient_sync(s,(TsTransientControls){0,0,1});
}
TsStereoFrame ts_transient_process(TsTransient *s, TsStereoFrame input)
{
    input=ts_stereo_frame_sanitize(input);
    if(!s || !s->rate)return input;
    s->attack_db=follow(s->attack_db,s->controls.attack_db,s->smooth);
    s->sustain_db=follow(s->sustain_db,s->controls.sustain_db,s->smooth);
    for(int i=0;i<5;++i)
        s->coefficient[i]=follow(s->coefficient[i],s->target_coefficient[i],s->smooth);
    double level=fmax(fabs(input.l),fabs(input.r));
    double rise=s->coefficient[0],body=s->coefficient[2];
    s->attack_fast=envelope(s->attack_fast,level,rise,body);
    s->attack_slow=envelope(s->attack_slow,s->attack_fast,s->coefficient[1],body);
    s->sustain_fast=envelope(s->sustain_fast,level,rise,body);
    double falling=bounded((s->sustain_slow-s->sustain_fast)/fmax(s->sustain_slow,1e-12),0,1,0);
    double release=s->coefficient[3]+falling*(s->coefficient[4]-s->coefficient[3]);
    s->sustain_slow=envelope(s->sustain_slow,level,rise,release);
    /* Relative contrast gives the same response to scaled copies. Small
       relative dead bands reject carrier ripple; they are not level gates. */
    double attack=bounded(((s->attack_fast-s->attack_slow)/fmax(s->attack_fast,1e-12)-.04)/.96,0,1,0);
    double sustain=bounded(((s->sustain_slow-s->sustain_fast)/fmax(s->sustain_slow,1e-12)-.08)/.92,0,1,0);
    /* Smoothly stop amplifying floating-point residue after the sound ends.
       The operating region above -140 dBFS remains independent of level. */
    double alive=bounded((fmax(s->attack_fast,s->sustain_slow)-1e-9)/9.9e-8,0,1,0);
    alive=alive*alive*(3-2*alive);
    s->attack_activity=(float)(attack*alive);s->sustain_activity=(float)(sustain*alive);
    s->gain_db=(float)(s->attack_db*s->attack_activity+s->sustain_db*s->sustain_activity);
    if(s->gain_db==0)return input;
    float gain=exp2f(s->gain_db*.1660964047443681f);
    return ts_stereo_frame_sanitize((TsStereoFrame){input.l*gain,input.r*gain});
}
