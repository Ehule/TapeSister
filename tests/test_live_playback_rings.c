#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/ts_sister_post_fx.c"
#include <assert.h>
#include <stdio.h>
#include <time.h>
#include "reference/fx_rings_207.inc"

static void same(const void *a,const void *b,size_t n)
{assert(!memcmp(a,b,n));}
static TsStereoFrame old_process(TsSisterSingleFx *f,TsStereoFrame in,float gate)
{
    float direct;
    switch(f->controls.type) {
    case TS_SISTER_FX_GRAIN:return reference_grain_process(&f->timing,&f->state.grain,&f->controls,in,gate,&direct,1);
    case TS_SISTER_FX_DELAY:return reference_delay_process(&f->timing,&f->state.delay,&f->controls,in,gate,&direct,1);
    default:return reference_reverb_process(&f->timing,&f->state.reverb,&f->controls,in,gate,&direct,1);
    }
}
/* Equal dispatch for timing: compare kernels through the same three-way
   wrapper. Correctness above still exercises the production entry point. */
static TsStereoFrame new_process(TsSisterSingleFx *f,TsStereoFrame in,float gate)
{
    float direct;
    switch(f->controls.type) {
    case TS_SISTER_FX_GRAIN:return grain_process(&f->timing,&f->state.grain,&f->controls,in,gate,&direct,1);
    case TS_SISTER_FX_DELAY:return delay_process(&f->timing,&f->state.delay,&f->controls,in,gate,&direct,1);
    default:return reverb_process(&f->timing,&f->state.reverb,&f->controls,in,gate,&direct,1);
    }
}
static void state_equal(TsSisterSingleFx *a,TsSisterSingleFx *b,int buffers)
{
    if(a->controls.type==TS_SISTER_FX_GRAIN) {
        TsSisterGrainState x=a->state.grain,y=b->state.grain;x.data=y.data=NULL;same(&x,&y,sizeof(x));
        if(buffers)same(a->state.grain.data,b->state.grain.data,x.capacity_frames*2*sizeof(float));
    } else if(a->controls.type==TS_SISTER_FX_DELAY) {
        TsSisterDelayState x=a->state.delay,y=b->state.delay;x.data=y.data=NULL;same(&x,&y,sizeof(x));
        if(buffers)same(a->state.delay.data,b->state.delay.data,x.capacity_frames*2*sizeof(float));
    } else {
        TsSisterReverbState x=a->state.reverb,y=b->state.reverb;
        for(int l=0;l<TS_SISTER_REVERB_LINES;++l) {
            x.line[l].data=y.line[l].data=NULL;
            if(buffers)same(a->state.reverb.line[l].data,b->state.reverb.line[l].data,x.line[l].capacity_frames*2*sizeof(float));
        }
        same(&x,&y,sizeof(x));
    }
}
static void readers(void)
{
    float data[514];for(int i=0;i<514;++i)data[i]=((i*37%251)-125)*.003f;
    for(size_t capacity=2;capacity<=257;++capacity) {
        for(size_t at=0;at<capacity;++at)assert(ring_next(at,capacity)==(at+1)%capacity);
        for(size_t write=0;write<capacity;write+=(capacity>8?7:1))for(int step=-3;step<19;++step)
        for(size_t ch=0;ch<2;++ch) {
            float delay=(float)step*capacity*.071f;
            float a=delay_read(data,capacity,write,delay,ch),b=reference_delay_read(data,capacity,write,delay,ch);
            same(&a,&b,sizeof(a));
            TsSisterGrainState s={0};s.data=data;s.capacity_frames=capacity;
            double position=(double)write-delay;
            a=grain_read(&s,position,ch);b=reference_grain_read(&s,position,ch);same(&a,&b,sizeof(a));
        }
    }
}
static void check(int type,unsigned rate)
{
    TsSisterSingleFx *a=ts_sister_single_fx_new(type,rate,17),*b=ts_sister_single_fx_new(type,rate,17);assert(a && b);
    /* 12 seconds crosses ring wraps and modulation renormalization, then
       exercises tails, exact silence, bypass, retrigger and interrupted edits. */
    for(unsigned i=0;i<rate*12;++i) {
        if(!(i%257)) {
            TsSisterFxSlotControls c=a->controls;
            c.parameter_a=i<rate*2?.15f:i<rate*4?.7f:.23f;c.parameter_b=type==TS_SISTER_FX_GRAIN?.9f:0;
            c.parameter_c=(i/rate)%2?.73f:.27f;c.mix=i<rate*5?.65f:.4f;c.gain_db=i<rate*3?0:3;
            ts_sister_single_fx_set(a,&c);ts_sister_single_fx_set(b,&c);
        }
        TsStereoFrame in={0};
        if((i>rate && i<rate*3) || i>=rate*11)in=(TsStereoFrame){((int)(i%251)-125)*.001f,((int)(i%113)-56)*.002f};
        if(i==rate/2)in.l=1e-15f;
        float gate=i>=rate*6 && i<rate*7?0:1;
        TsStereoFrame x=ts_sister_single_fx_process(a,in,gate),y=old_process(b,in,gate);
        same(&x,&y,sizeof(x));
        if(!(i%257))state_equal(a,b,!(i%4096));
    }
    state_equal(a,b,1);ts_sister_single_fx_free(a);ts_sister_single_fx_free(b);
}
static volatile double sink;
static double timed(int type,int old)
{
    TsSisterSingleFx *f=ts_sister_single_fx_new(type,48000,17);assert(f);
    TsSisterFxSlotControls c=f->controls;c.parameter_a=.58f;c.parameter_b=.42f;c.mix=.65f;ts_sister_single_fx_set(f,&c);
    const int blocks=256;double sum=0;clock_t start=clock();
    for(unsigned i=0;i<(unsigned)blocks*512;++i) {
        TsStereoFrame in={((int)(i%251)-125)*.001f,((int)(i%113)-56)*.002f};
        TsStereoFrame out=old?old_process(f,in,1):new_process(f,in,1);sum+=out.l;
    }
    double ms=(double)(clock()-start)*1000/CLOCKS_PER_SEC/blocks;sink=sum;ts_sister_single_fx_free(f);return ms;
}
static double median(double *v)
{for(int i=1;i<5;++i)for(int j=i;j>0 && v[j]<v[j-1];--j){double t=v[j];v[j]=v[j-1];v[j-1]=t;}return v[2];}
int main(void)
{
    readers();const int types[]={TS_SISTER_FX_GRAIN,TS_SISTER_FX_DELAY,TS_SISTER_FX_REVERB};
    for(int t=0;t<3;++t){check(types[t],8000);check(types[t],44100);check(types[t],48000);}
    puts("PR #207 oracle: bit-identical native Grain/Delay/Reverb PCM, ring contents, recurrent state, tails, bypass and automation.");
    for(int t=0;t<3;++t) {
        double before[5],after[5];
        for(int i=0;i<5;++i) {
            if(i%2){after[i]=timed(types[t],0);before[i]=timed(types[t],1);}
            else {before[i]=timed(types[t],1);after[i]=timed(types[t],0);}
        }
        double old=median(before),now=median(after);
        printf("%s active kernel, 48k/512, median of 5 alternating trials: old %.6f ms/block, new %.6f ms/block, %.2f%% reduction (not full callback/ASIO)\n",
            ts_sister_fx_type_name(types[t]),old,now,old?100*(old-now)/old:0);
    }
    return 0;
}
