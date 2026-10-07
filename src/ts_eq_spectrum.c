#include "tapesister/eq_spectrum.h"
#include "tapesister/master_eq.h"
#include <math.h>
#include <string.h>

void ts_eq_spectrum_init(TsEqSpectrum *s)
{
    memset(s,0,sizeof(*s));atomic_init(&s->request,0);atomic_init(&s->ready,0);
}
void ts_eq_spectrum_enable(TsEqSpectrum *s,int enabled)
{
    unsigned old=atomic_load_explicit(&s->request,memory_order_relaxed);
    if((old&1u)!=(unsigned)(enabled!=0))
        atomic_store_explicit(&s->request,old+1u,memory_order_release);
}
void ts_eq_spectrum_clear(TsEqSpectrumView *v,unsigned rate)
{
    for(int i=0;i<TS_EQ_SPECTRUM_POINTS;++i)v->db[i]=TS_EQ_SPECTRUM_FLOOR_DB;
    v->rate=rate;v->valid=0;v->idle_seconds=0;
}
void ts_eq_spectrum_push(TsEqSpectrum *s,TsStereoFrame frame,unsigned rate)
{
    unsigned request=atomic_load_explicit(&s->request,memory_order_acquire);
    if(!(request&1u) || !rate)return;
    if(atomic_load_explicit(&s->ready,memory_order_acquire))return;
    if(request!=s->producer_request || rate!=s->rate) {
        s->producer_request=request;s->rate=rate;s->fill=0;
    }
    s->samples[s->fill++]=ts_stereo_frame_sanitize(frame);
    if(s->fill==TS_EQ_SPECTRUM_FRAMES) {
        s->fill=0;atomic_store_explicit(&s->ready,1,memory_order_release);
    }
}
static void fft(float *re,float *im)
{
    const unsigned n=TS_EQ_SPECTRUM_FRAMES;
    for(unsigned i=1,j=0;i<n;++i) {
        unsigned bit=n>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;
        if(i<j){float t=re[i];re[i]=re[j];re[j]=t;t=im[i];im[i]=im[j];im[j]=t;}
    }
    for(unsigned length=2;length<=n;length<<=1) {
        double angle=-6.283185307179586/length,wr=cos(angle),wi=sin(angle);
        for(unsigned i=0;i<n;i+=length) {
            double cr=1,ci=0;
            for(unsigned j=0;j<length/2;++j) {
                unsigned a=i+j,b=a+length/2;
                float tr=(float)(cr*re[b]-ci*im[b]),ti=(float)(cr*im[b]+ci*re[b]);
                re[b]=re[a]-tr;im[b]=im[a]-ti;re[a]+=tr;im[a]+=ti;
                double next=cr*wr-ci*wi;ci=cr*wi+ci*wr;cr=next;
            }
        }
    }
}
int ts_eq_spectrum_poll(TsEqSpectrum *s,TsEqSpectrumView *v,unsigned rate,float elapsed)
{
    unsigned request=atomic_load_explicit(&s->request,memory_order_acquire);
    unsigned ready=atomic_load_explicit(&s->ready,memory_order_acquire);
    if(!(request&1u) || v->rate!=rate)ts_eq_spectrum_clear(v,rate);
    if(!(request&1u) || !ready || !rate ||
       s->producer_request!=request || s->rate!=rate) {
        if(ready)atomic_store_explicit(&s->ready,0,memory_order_release);
        v->idle_seconds+=fmaxf(0,elapsed);
        /* Low-rate devices need longer than one UI tick to fill the FFT.
           Hold between captures; decay only if the audio stream has stalled. */
        if(v->valid && rate && v->idle_seconds>.15f+TS_EQ_SPECTRUM_FRAMES/(float)rate)
            for(int i=0;i<TS_EQ_SPECTRUM_POINTS;++i)
                v->db[i]=fmaxf(TS_EQ_SPECTRUM_FLOOR_DB,v->db[i]-48*fmaxf(0,elapsed));
        return 0;
    }
    float re[TS_EQ_SPECTRUM_FRAMES],im[TS_EQ_SPECTRUM_FRAMES];
    double power[TS_EQ_SPECTRUM_FRAMES/2+1]={0};
    const double scale=4.0/TS_EQ_SPECTRUM_FRAMES; /* Periodic Hann coherent gain. */
    for(int ch=0;ch<2;++ch) {
        for(int i=0;i<TS_EQ_SPECTRUM_FRAMES;++i) {
            float x=ch?s->samples[i].r:s->samples[i].l;
            /* Display-only bound keeps a corrupt/huge finite source from
               overflowing FFT arithmetic; the audio path is never changed. */
            re[i]=(float)fmax(-1e6,fmin(1e6,x))*(float)(.5-.5*cos(6.283185307179586*i/TS_EQ_SPECTRUM_FRAMES));im[i]=0;
        }
        fft(re,im);
        for(int i=1;i<=TS_EQ_SPECTRUM_FRAMES/2;++i)
            power[i]+=.5*scale*scale*((double)re[i]*re[i]+(double)im[i]*im[i]);
    }
    /* Combine channel power, never mono-sum: opposite-phase stereo must show. */
    double step=log(ts_master_eq_max_hz(rate)/20.)/(TS_EQ_SPECTRUM_POINTS-1);
    for(int i=0;i<TS_EQ_SPECTRUM_POINTS;++i) {
        double center=20*exp(step*i)*TS_EQ_SPECTRUM_FRAMES/rate;
        int lo=(int)ceil(center*exp(-step*.5)),hi=(int)floor(center*exp(step*.5));
        double peak=0;
        if(lo>hi)lo=hi=(int)floor(center+.5);
        if(lo<1)lo=1;
        if(hi<lo)hi=lo;
        if(hi>TS_EQ_SPECTRUM_FRAMES/2)hi=TS_EQ_SPECTRUM_FRAMES/2;
        for(int bin=lo;bin<=hi;++bin)if(power[bin]>peak)peak=power[bin];
        float db=(float)fmax(TS_EQ_SPECTRUM_FLOOR_DB,10*log10(fmax(1e-24,peak)));
        v->db[i]=v->valid?fmaxf(db,v->db[i]-36*fmaxf(0,elapsed)):db;
    }
    v->valid=1;v->idle_seconds=0;
    atomic_store_explicit(&s->ready,0,memory_order_release);
    return 1;
}
