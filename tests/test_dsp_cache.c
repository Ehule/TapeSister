#ifdef NDEBUG
#undef NDEBUG
#endif
#include "sister_test_helpers.h"
#include <stdio.h>

/* The second engine deliberately recomputes every coefficient every sample.
   Compare output AND recurrent state while smoothed inputs move and rates change.
   --render also supports an independent comparison with the pre-cache library. */
static void uncached(TsSisterMachine *s,TsSisterFalloutEngine *f)
{
#ifndef TS_BASELINE_ORACLE
    s->ghost_coefficient.valid=s->duck_threshold.valid=0;
    f->lfo_frequency.valid=f->rise_duration.valid=f->pan_coefficient.valid=f->bit_levels.valid=0;
#else
    (void)s;(void)f;
#endif
}
static void same(TsStereoFrame a,TsStereoFrame b)
{assert(isfinite(a.l) && isfinite(a.r));assert(!memcmp(&a,&b,sizeof(a)));}
int main(int argc,char **argv)
{
    int dump=argc>1 && !strcmp(argv[1],"--render");
    const unsigned rates[]={8000,44100,48000,96000,192000};
    for(unsigned r=0;r<sizeof(rates)/sizeof(*rates);++r) {
        TsSisterMachine a,b;TsSisterFalloutEngine fa,fb;
        unsigned rate=rates[r];
        assert(ts_sister_machine_init(&a,rate,2,.05));assert(ts_sister_machine_init(&b,rate,2,.05));
        assert(ts_sister_fallout_init(&fa,rate));assert(ts_sister_fallout_init(&fb,rate));
        TsSisterParameters p;ts_sister_parameters_default(&p,rate);
        p.buffer_seconds=.05;p.write_erase=.35f;p.ghost_tone=.7f;p.duck_enabled=1;p.duck_sensitivity=.4f;
        p.duck_mode=TS_SISTER_DUCK_SAFE;p.wow=p.drop=0;
        sister_configure_immediate(&a,&p);sister_configure_immediate(&b,&p);
        TsSisterFalloutControls c;ts_sister_fallout_controls_default(&c);
        c.enabled=1;c.mix=.8f;c.feedback=.4f;c.bit_enabled=1;c.bit_resolution=.35f;
        c.lfo_rate=.8f;c.rise_length=.1f;c.component_transition=c.master_transition=0;
        ts_sister_fallout_seed(&fa,12345);ts_sister_fallout_seed(&fb,12345);
        for(int phase=0;phase<6;++phase) {
            if(phase==1 || phase==2) {
                p.ghost_tone=phase==1?0:1;p.duck_sensitivity=phase==1?1:0;
                c.lfo_rate=phase==1?1:0;c.rise_length=phase==1?0:1;
                c.bit_resolution=phase==1?1:0;c.pan_enabled=phase==1;
                c.lfo_targets=TS_SISTER_FALLOUT_LFO_ALL;c.lfo_intensity=.7f;
            }
            if(phase==3) { /* Device reconfigure on the same objects. */
                rate=rates[(r+1)%5];
                assert(ts_sister_machine_reconfigure(&a,rate,2,.05));assert(ts_sister_machine_reconfigure(&b,rate,2,.05));
                assert(ts_sister_fallout_reconfigure(&fa,rate));assert(ts_sister_fallout_reconfigure(&fb,rate));
            }
            if(phase==4){p.duck_mode=TS_SISTER_DUCK_KAFKA_BIAS;p.ghost_tone=.23f;c.enabled=0;}
            if(phase==5){p.duck_mode=TS_SISTER_DUCK_SAFE;c.enabled=1;c.bit_resolution=.71f;
                assert(ts_sister_machine_clear_offline(&a));assert(ts_sister_machine_clear_offline(&b));
                ts_sister_fallout_clear(&fa);ts_sister_fallout_clear(&fb);}
            ts_sister_machine_set_parameters(&a,&p);ts_sister_machine_set_parameters(&b,&p);
            ts_sister_fallout_set_controls(&fa,&c);ts_sister_fallout_set_controls(&fb,&c);
            for(int i=0;i<8192;++i) {
                TsStereoFrame in={.17f*sinf(i*.031f),.13f*cosf(i*.017f)};
                TsStereoFrame key={in.r*3,in.l*2};
                uncached(&b,&fb);
                TsSisterOutput x=ts_sister_machine_process_frame(&a,in,key);
                TsSisterFalloutResult fx=ts_sister_fallout_process(&fa,x.mix);
                if(dump) {
                    float values[]={x.write.l,x.write.r,x.mix.l,x.mix.r,a.duck_gain,
                        a.ghost_lowpass_state[0],a.ghost_lowpass_state[1],fx.output.l,fx.output.r,fx.wet.l,fx.wet.r};
                    assert(fwrite(values,sizeof(values),1,stdout)==1);
                } else {
                    TsSisterOutput y=ts_sister_machine_process_frame(&b,in,key);
                    TsSisterFalloutResult fy=ts_sister_fallout_process(&fb,y.mix);
                    same(x.write,y.write);same(x.mix,y.mix);same(fx.output,fy.output);same(fx.wet,fy.wet);
                    assert(a.duck_gain==b.duck_gain && fa.lfo_phase==fb.lfo_phase && fa.rise_phase==fb.rise_phase);
                    assert(fa.prng==fb.prng && fa.write_clock==fb.write_clock);
                }
            }
        }
        ts_sister_machine_free(&a);ts_sister_machine_free(&b);ts_sister_fallout_free(&fa);ts_sister_fallout_free(&fb);
    }
    if(!dump)puts("Exact cached/recomputed DSP passed: steady controls, ramps, bypass, clear and device-rate changes.");
}
