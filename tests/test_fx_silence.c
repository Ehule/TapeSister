#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/ts_sister_post_fx.c"
#include <assert.h>
#include <stdio.h>
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
        if(type==TS_SISTER_FX_DELAY) {
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
int main(void)
{
    check(TS_SISTER_FX_DELAY,8000);check(TS_SISTER_FX_REVERB,8000);
    check(TS_SISTER_FX_DELAY,48000);check(TS_SISTER_FX_REVERB,48000);
    puts("Silent DSP, tails, automation and re-entry match the complete kernels.");
}
