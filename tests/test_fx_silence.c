#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/ts_sister_post_fx.c"
#include <assert.h>
#include <stdio.h>
#include <time.h>
/* The reference runs the complete kernels. Compare tails, internal clocks,
   ring occupancy, automation while idle, tiny input, and subsequent re-entry. */
static void check(int type,unsigned rate)
{
    TsSisterSingleFx *a=ts_sister_single_fx_new(type,rate,17),*b=ts_sister_single_fx_new(type,rate,17);
    assert(a && b);int slept_after_tail=0;
    for(unsigned i=0;i<rate*40;++i) {
        TsSisterFxSlotControls c=a->controls;
        c.parameter_a=(i/rate)%2?.15f:.3f;c.parameter_b=0;c.mix=.8f;
        if(i==rate*2)c.gain_db=3;
        ts_sister_single_fx_set(a,&c);ts_sister_single_fx_set(b,&c);
        TsStereoFrame input={0};
        if((i>=rate*4 && i<rate*5) || i>=rate*39)input=(TsStereoFrame){.1f*sinf(i*.041f),.07f*cosf(i*.037f)};
        if(i==rate*2)input.l=1e-15f;
        float da,db;TsStereoFrame x,y;
        if(type==TS_SISTER_FX_GRAIN) {
            c.parameter_b=1;c.parameter_c=(i/rate)%2?.9f:.1f;
            float gate=(i/rate)%3==0?0:1;
            x=grain_process(&a->timing,&a->state.grain,&c,input,gate,&da,1);
            y=grain_process(&b->timing,&b->state.grain,&c,input,gate,&db,0);
            TsSisterGrainState aa=a->state.grain,bb=b->state.grain;aa.data=bb.data=NULL;
            assert(!memcmp(&aa,&bb,sizeof(aa)));
            if(!(i%4096)){size_t used=0;for(size_t n=0;n<aa.capacity_frames*2;++n){assert(a->state.grain.data[n]==b->state.grain.data[n]);used+=a->state.grain.data[n]!=0;}assert(used==aa.nonzero_samples);}
            slept_after_tail|=i>rate*12 && !aa.nonzero_samples;
        } else if(type==TS_SISTER_FX_DELAY) {
            x=delay_process(&a->timing,&a->state.delay,&c,input,1,&da,1);
            y=delay_process(&b->timing,&b->state.delay,&c,input,1,&db,0);
            TsSisterDelayState aa=a->state.delay,bb=b->state.delay;aa.data=bb.data=NULL;
            assert(!memcmp(&aa,&bb,sizeof(aa)));
            if(!(i%4096)){size_t used=0;for(size_t n=0;n<aa.capacity_frames*2;++n){assert(a->state.delay.data[n]==b->state.delay.data[n]);used+=a->state.delay.data[n]!=0;}assert(used==aa.nonzero_samples);}
            slept_after_tail|=i>rate*6 && !aa.nonzero_samples;
        } else {
            x=reverb_process(&a->timing,&a->state.reverb,&c,input,1,&da,1);
            y=reverb_process(&b->timing,&b->state.reverb,&c,input,1,&db,0);
            TsSisterReverbState aa=a->state.reverb,bb=b->state.reverb;
            for(int l=0;l<TS_SISTER_REVERB_LINES;++l) {
                /* Numerically equal zero may carry a different sign. */
                aa.line[l].data=bb.line[l].data=NULL;
                assert(aa.line[l].damping[0]==bb.line[l].damping[0] && aa.line[l].damping[1]==bb.line[l].damping[1]);
                if(!aa.line[l].damping[0])aa.line[l].damping[0]=bb.line[l].damping[0]=0;
                if(!aa.line[l].damping[1])aa.line[l].damping[1]=bb.line[l].damping[1]=0;
            }
            assert(!memcmp(&aa,&bb,sizeof(aa)));
            if(!(i%4096)) {
                size_t used=0;
                for(int l=0;l<TS_SISTER_REVERB_LINES;++l)for(size_t n=0;n<aa.line[l].capacity_frames*2;++n) {
                    assert(a->state.reverb.line[l].data[n]==b->state.reverb.line[l].data[n]);
                    used+=a->state.reverb.line[l].data[n]!=0;
                }
                assert(used==aa.nonzero_samples);
            }
            slept_after_tail|=i>rate*6 && !aa.nonzero_samples;
        }
        assert(x.l==y.l && x.r==y.r && da==db);
    }
    assert(slept_after_tail);
    ts_sister_single_fx_free(a);ts_sister_single_fx_free(b);
}
static void benchmark(void)
{
    for(int type=TS_SISTER_FX_GRAIN;type<=TS_SISTER_FX_GRAIN;++type)
    for(int active=0;active<2;++active)for(int fast=0;fast<2;++fast) {
        TsSisterSingleFx *f=ts_sister_single_fx_new(type,48000,17);assert(f);
        TsSisterFxSlotControls c=f->controls;c.parameter_a=c.parameter_b=c.mix=1;
        uint64_t hash=UINT64_C(14695981039346656037);clock_t start=clock();
        for(unsigned i=0;i<480000;++i) {
            TsStereoFrame in={0},out;float direct;
            if(active)in=(TsStereoFrame){((int)(i%251)-125)*.001f,((int)(i%113)-56)*.002f};
            out=grain_process(&f->timing,&f->state.grain,&c,in,1,&direct,fast);
            const unsigned char *bytes=(const unsigned char *)&out;
            for(size_t n=0;n<sizeof(out);++n)hash=(hash^bytes[n])*UINT64_C(1099511628211);
        }
        printf("%s %s fast=%d: %.3f ms, hash=%016llx\n",ts_sister_fx_type_name(type),active?"active":"silent",fast,
            (double)(clock()-start)*1000/CLOCKS_PER_SEC,(unsigned long long)hash);
        ts_sister_single_fx_free(f);
    }
}
int main(int argc,char **argv)
{
    if(argc==2 && !strcmp(argv[1],"--benchmark")){benchmark();return 0;}
    check(TS_SISTER_FX_GRAIN,8000);check(TS_SISTER_FX_GRAIN,48000);
    check(TS_SISTER_FX_DELAY,8000);check(TS_SISTER_FX_REVERB,8000);
    check(TS_SISTER_FX_DELAY,48000);check(TS_SISTER_FX_REVERB,48000);
    puts("Silent DSP, tails, automation and re-entry match the complete kernels.");
}
