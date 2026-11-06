#include "tapesister/meta_fx.h"
#include "tapesister/texture_fx.h"
#include <math.h>
#include <stdio.h>
#include <stdint.h>
static uint32_t rng=42;static float noise(void){rng=rng*1664525u+1013904223u;return ((rng>>8)/8388608.f-1)*.12f;}
int main(void){for(int signal=0;signal<2;++signal)for(int p=-3;p<TS_META_PRESETS;++p){TsMetaControls c;TsMetaEngine *m=0;TsDispersion d;TsResonator r;ts_dispersion_init(&d,48000);ts_resonator_init(&r,48000);if(p>=0){ts_meta_preset(&c,p);m=ts_meta_new(&c,48000,42);}double ei=0,eo=0;float peak=0,direct;rng=42;for(int i=0;i<480000;++i){TsStereoFrame in=signal?(TsStereoFrame){.1f*sinf(i*2*3.14159265*164.8138/48000),.1f*sinf(i*2*3.14159265*246.9417/48000)}:(TsStereoFrame){noise(),noise()};TsStereoFrame out=p==-3?ts_dispersion_process(&d,in,.5,.5,.5,1,0,1,&direct):p==-2?ts_dispersion_process(&d,in,.5,.5,.5,.5,0,1,&direct):p==-1?ts_resonator_process(&r,in,.25,.5,.5,1,0,1,&direct):ts_meta_process(m,in);if(i>=240000){ei+=in.l*in.l+in.r*in.r;eo+=out.l*out.l+out.r*out.r;peak=fmaxf(peak,fmaxf(fabsf(out.l),fabsf(out.r)));}}printf("%s %-16s RMS %+6.1f dB peak %.3f\n",signal?"tone ":"noise",p==-3?"DISPERSE WET":p==-2?"DISPERSE 50%":p==-1?"RESONATOR":c.name,10*log10(eo/ei),peak);ts_meta_free(m);ts_resonator_free(&r);}}
