#include "tapesister/master_eq.h"
#include <math.h>
#include <string.h>

static double clamp(double x,double lo,double hi,double fallback)
{ return !isfinite(x)?fallback:fmax(lo,fmin(hi,x)); }
float ts_master_eq_max_hz(unsigned rate)
{ return rate ? fmaxf(20,fminf(20000,.45f*rate)) : 20000; }
const char *ts_master_eq_type_name(int type)
{
    static const char *names[]={"BELL","LOW SHELF","HIGH SHELF","HIGH PASS","LOW PASS","NOTCH"};
    return names[type>=0 && type<TS_EQ_TYPE_COUNT?type:0];
}
int ts_master_eq_has_gain(int type) { return type<=TS_EQ_HIGH_SHELF; }
void ts_master_eq_default(TsMasterEqControls *c)
{
    static const float hz[]={80,300,1000,4000,12000};
    memset(c,0,sizeof(*c));
    for(int i=0;i<TS_MASTER_EQ_BANDS;++i)c->band[i]=(TsEqBand){1,TS_EQ_BELL,hz[i],0,.70710678f};
}
void ts_master_eq_sanitize(TsMasterEqControls *c)
{
    c->enabled=c->enabled!=0;
    if(c->solo_band<0 || c->solo_band>TS_MASTER_EQ_BANDS)c->solo_band=0;
    for(int i=0;i<TS_MASTER_EQ_BANDS;++i) {
        TsEqBand *b=&c->band[i]; b->enabled=b->enabled!=0;
        if(b->type<0 || b->type>=TS_EQ_TYPE_COUNT)b->type=TS_EQ_BELL;
        b->frequency=(float)clamp(b->frequency,20,20000,1000);
        b->gain_db=(float)clamp(b->gain_db,-12,12,0);
        b->q=(float)clamp(b->q,.3,8,.70710678);
    }
}
int ts_master_eq_band_active(const TsMasterEqControls *c,int band)
{
    if(band<0 || band>=TS_MASTER_EQ_BANDS)return 0;
    return c->solo_band?c->solo_band==band+1:c->band[band].enabled;
}
void ts_master_eq_toggle_band(TsMasterEqControls *c,int band)
{
    if(band<0 || band>=TS_MASTER_EQ_BANDS)return;
    /* A plain click on the soloed band bypasses it. Other clicks toggle the
       remembered state. Both leave solo without disturbing the other bands. */
    c->band[band].enabled=c->solo_band==band+1?0:!c->band[band].enabled;
    c->solo_band=0;
}
static TsEqBand active_band(const TsMasterEqControls *c,int band)
{
    TsEqBand b=c->band[band];b.enabled=ts_master_eq_band_active(c,band);return b;
}
/* RBJ biquads, normalized a0; formula reference: https://www.w3.org/TR/audio-eq-cookbook/
   Coefficients are calculated by the control/device thread only. Each fading
   filter has fixed stable poles; rapid edits coalesce into the next transition. */
TsEqCoefficients ts_master_eq_coefficients(TsEqBand b,unsigned rate)
{
    TsEqCoefficients c={1,0,0,0,0};
    if(!rate || !b.enabled || (ts_master_eq_has_gain(b.type) && b.gain_db==0))return c;
    double w=6.283185307179586*clamp(b.frequency,20,ts_master_eq_max_hz(rate),1000)/rate;
    double cs=cos(w),sn=sin(w),a=sn/(2*clamp(b.q,.3,8,.70710678));
    double A=pow(10,clamp(b.gain_db,-12,12,0)/40),v=2*sqrt(A)*a;
    double a0=1+a,a1=-2*cs,a2=1-a,b0=1,b1=-2*cs,b2=1;
    switch(b.type) {
    case TS_EQ_BELL: b0=1+a*A;b2=1-a*A;a0=1+a/A;a2=1-a/A;break;
    case TS_EQ_LOW_SHELF:
        b0=A*((A+1)-(A-1)*cs+v);b1=2*A*((A-1)-(A+1)*cs);b2=A*((A+1)-(A-1)*cs-v);
        a0=(A+1)+(A-1)*cs+v;a1=-2*((A-1)+(A+1)*cs);a2=(A+1)+(A-1)*cs-v;break;
    case TS_EQ_HIGH_SHELF:
        b0=A*((A+1)+(A-1)*cs+v);b1=-2*A*((A-1)+(A+1)*cs);b2=A*((A+1)+(A-1)*cs-v);
        a0=(A+1)-(A-1)*cs+v;a1=2*((A-1)-(A+1)*cs);a2=(A+1)-(A-1)*cs-v;break;
    case TS_EQ_HIGH_PASS: b0=b2=(1+cs)/2;b1=-(1+cs);break;
    case TS_EQ_LOW_PASS: b0=b2=(1-cs)/2;b1=1-cs;break;
    default: break; /* Notch numerator already set. */
    }
    return (TsEqCoefficients){b0/a0,b1/a0,b2/a0,a1/a0,a2/a0};
}
void ts_master_eq_init(TsMasterEq *eq)
{ memset(eq,0,sizeof(*eq));ts_master_eq_default(&eq->controls); }
void ts_master_eq_prepare(TsMasterEq *eq,unsigned rate)
{
    if(!rate || eq->sample_rate==rate)return;
    eq->sample_rate=rate;eq->fade_frames=(unsigned)fmax(1,rate*.04);
    eq->mix=eq->controls.enabled;
    memset(eq->stage,0,sizeof(eq->stage));
    for(int i=0;i<TS_MASTER_EQ_BANDS;++i)
        eq->stage[i].current.c=eq->stage[i].pending=ts_master_eq_coefficients(active_band(&eq->controls,i),rate);
}
void ts_master_eq_set(TsMasterEq *eq,const TsMasterEqControls *controls)
{
    eq->controls=*controls;ts_master_eq_sanitize(&eq->controls);
    for(int i=0;i<TS_MASTER_EQ_BANDS;++i) {
        TsEqStage *s=&eq->stage[i];
        TsEqCoefficients c=ts_master_eq_coefficients(active_band(&eq->controls,i),eq->sample_rate);
        if(memcmp(&s->pending,&c,sizeof(c))) { s->pending=c;s->dirty=1; }
    }
}
static double filter(TsEqFilter *f,double in,int ch)
{
    double out=f->c.b0*in+f->z1[ch];
    f->z1[ch]=f->c.b1*in-f->c.a1*out+f->z2[ch];
    f->z2[ch]=f->c.b2*in-f->c.a2*out;
    if(!isfinite(out) || fabs(out)>1e12) { f->z1[ch]=f->z2[ch]=0;return 0; }
    if(fabs(f->z1[ch])<1e-30)f->z1[ch]=0;
    if(fabs(f->z2[ch])<1e-30)f->z2[ch]=0;
    return out;
}
TsStereoFrame ts_master_eq_process(TsMasterEq *eq,TsStereoFrame input)
{
    input=ts_stereo_frame_sanitize(input);
    if(!eq || !eq->sample_rate)return input;
    double v[2]={input.l,input.r};
    for(int i=0;i<TS_MASTER_EQ_BANDS;++i) {
        TsEqStage *s=&eq->stage[i];
        if(!s->fade && s->dirty) {
            memset(&s->next,0,sizeof(s->next));s->next.c=s->pending;
            s->fade=eq->fade_frames;s->dirty=0;
        }
        double blend=s->fade?1-(double)s->fade/eq->fade_frames:0;
        blend=blend*blend*(3-2*blend); /* Smooth endpoints; no gain bump. */
        for(int ch=0;ch<2;++ch) {
            double old=filter(&s->current,v[ch],ch);
            v[ch]=s->fade?old+(filter(&s->next,v[ch],ch)-old)*blend:old;
        }
        if(s->fade && !--s->fade)s->current=s->next;
    }
    double step=1.0/fmax(1,eq->sample_rate*.02);
    eq->mix=eq->controls.enabled?fmin(1,eq->mix+step):fmax(0,eq->mix-step);
    /* Exact dry return after the ramp, with no added delay. Wet filters stay warm. */
    if(eq->mix==0)return input;
    return ts_stereo_frame_sanitize((TsStereoFrame){
        (float)(input.l+(v[0]-input.l)*eq->mix),(float)(input.r+(v[1]-input.r)*eq->mix)});
}
double ts_master_eq_response_db(const TsMasterEqControls *c,unsigned rate,double hz)
{
    if(!c->enabled || !rate)return 0;
    double w=6.283185307179586*fmin(hz,.499*rate)/rate,cs=cos(w),sn=sin(w),c2=cos(2*w),s2=sin(2*w),db=0;
    for(int i=0;i<TS_MASTER_EQ_BANDS;++i) {
        TsEqCoefficients k=ts_master_eq_coefficients(active_band(c,i),rate);
        double nr=k.b0+k.b1*cs+k.b2*c2,ni=-k.b1*sn-k.b2*s2;
        double dr=1+k.a1*cs+k.a2*c2,di=-k.a1*sn-k.a2*s2;
        db+=10*log10(fmax(1e-24,(nr*nr+ni*ni)/fmax(1e-24,dr*dr+di*di)));
    }
    return db;
}
float ts_master_eq_normalized(const TsEqBand *b,int control,unsigned rate)
{
    if(control==0)return (float)(log(fmin(b->frequency,ts_master_eq_max_hz(rate))/20)/log(ts_master_eq_max_hz(rate)/20));
    if(control==1)return (b->gain_db+12)/24;
    return (float)(log(b->q/.3)/log(8/.3));
}
void ts_master_eq_set_normalized(TsEqBand *b,int control,float v,unsigned rate)
{
    v=(float)clamp(v,0,1,.5);
    if(control==0)b->frequency=20*powf(ts_master_eq_max_hz(rate)/20,v);
    else if(control==1)b->gain_db=24*v-12;
    else b->q=.3f*powf(8/.3f,v);
}
int ts_master_eq_write(FILE *file,const TsMasterEqControls *controls)
{
    TsMasterEqControls c=*controls;ts_master_eq_sanitize(&c);
    if(fprintf(file,"MasterEq.Enabled=%d\nMasterEq.SoloBand=%d\n",c.enabled,c.solo_band)<0)return 0;
    for(int i=0;i<TS_MASTER_EQ_BANDS;++i) {
        TsEqBand b=c.band[i];
        if(fprintf(file,"MasterEq.Band.%d=%d,%d,%.9g,%.9g,%.9g\n",i+1,b.enabled,b.type,b.frequency,b.gain_db,b.q)<0)return 0;
    }
    return 1;
}
int ts_master_eq_read(TsMasterEqControls *c,const char *key,const char *value)
{
    if(strncmp(key,"MasterEq.",9))return 0;
    int n=-1,end=0;char extra;
    if(!strcmp(key,"MasterEq.Enabled")) {
        if(sscanf(value,"%d %c",&n,&extra)!=1 || (n!=0 && n!=1))return -1;
        c->enabled=n;return 1;
    }
    if(!strcmp(key,"MasterEq.SoloBand")) {
        if(sscanf(value,"%d %c",&n,&extra)!=1 || n<0 || n>TS_MASTER_EQ_BANDS)return -1;
        c->solo_band=n;return 1;
    }
    if(sscanf(key,"MasterEq.Band.%d%c",&n,&extra)!=1 || n<1 || n>5)return -1;
    TsEqBand b;
    if(sscanf(value,"%d,%d,%f,%f,%f %n",&b.enabled,&b.type,&b.frequency,&b.gain_db,&b.q,&end)!=5 ||
       !end || value[end] || !isfinite(b.frequency) || !isfinite(b.gain_db) || !isfinite(b.q) ||
       b.type<0 || b.type>=TS_EQ_TYPE_COUNT || (b.enabled!=0 && b.enabled!=1))return -1;
    c->band[n-1]=b;ts_master_eq_sanitize(c);return 1;
}
