#include "tapesister/clean_output.h"
#include <math.h>

static int reserved(unsigned ch,int pair)
{return pair>0 && (ch==(unsigned)pair*2u || ch==(unsigned)pair*2u+1u);}
unsigned ts_clean_output_available(const TsSpatialControls *c,unsigned channels,int pair)
{
    unsigned mask=0;
    for(int i=0;i<c->speakers && i<16;++i) {
        int ch=c->output[i];
        if(ch<0 || ch>=64 || (unsigned)ch>=channels || reserved((unsigned)ch,pair))continue;
        int duplicate=0;
        for(int j=0;j<c->speakers && j<16;++j)if(j!=i && c->output[j]==ch)duplicate=1;
        if(!duplicate)mask|=1u<<i;
    }
    return mask;
}
void ts_clean_output_mix(TsCleanOutput *s,const TsSourceRouteMix *clean,
                        const TsSpatialControls *c,float gain,float *out,
                        unsigned channels,int pair,unsigned rate)
{
    s->missing=clean->missing;
    if(!clean->mask && (!s->limit || s->limit==1))return;
    if(!s->limit)s->limit=1;
    unsigned available=clean->check_outputs?clean->available:ts_clean_output_available(c,channels,pair);
    for(int i=0;i<16;++i)if((clean->mask&available)&(1u<<i))
        out[c->output[i]]+=clean->speaker[i]*gain;
    /* An unavailable whole route folds to hardware 1/2; never partially
       route a stereo pair or overwrite the reserved external Insert send. */
    if(channels>=2) {out[0]+=clean->fallback.l*gain;out[1]+=clean->fallback.r*gain;}
    float peak=0;
    for(unsigned ch=0;ch<channels && ch<64;++ch)if(!reserved(ch,pair)) {
        if(!isfinite(out[ch]))out[ch]=0;
        peak=fmaxf(peak,fabsf(out[ch]));
    }
    float target=peak>.98f?.98f/peak:1;
    if(target<s->limit)s->limit=target;
    else {s->limit+=(target-s->limit)/(rate?rate*.05f:1.f);if(s->limit>.99999f)s->limit=1;}
    for(unsigned ch=0;ch<channels && ch<64;++ch)if(!reserved(ch,pair))out[ch]*=s->limit;
}
