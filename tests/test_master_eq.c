#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/master_eq.h"
#include "tapesister/sister_runtime.h"
#include "tapesister/sister_project_state.h"
#include "tapesister/config.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static double measure(TsMasterEq *eq,unsigned rate,double hz)
{
    double input=0,output=0;
    for(unsigned i=0;i<rate*2;++i) {
        float x=.001f*(float)sin(6.283185307179586*hz*i/rate);
        TsStereoFrame y=ts_master_eq_process(eq,(TsStereoFrame){x,-x*.5f});
        assert(isfinite(y.l) && fabs(y.l+2*y.r)<1e-5);
        if(i>=rate){input+=x*x;output+=y.l*y.l;}
    }
    return 10*log10(fmax(1e-24,output/input));
}
static void response_tests(void)
{
    unsigned rates[]={8000,44100,48000,96000};
    for(unsigned ri=0;ri<4;++ri)for(int type=0;type<TS_EQ_TYPE_COUNT;++type) {
        TsMasterEq e;ts_master_eq_init(&e);TsMasterEqControls c=e.controls;
        c.enabled=1;c.band[0]=(TsEqBand){1,type,1000,6,.70710678f};
        ts_master_eq_set(&e,&c);ts_master_eq_prepare(&e,rates[ri]);
        double center=ts_master_eq_response_db(&c,rates[ri],1000);
        if(type==TS_EQ_BELL)assert(fabs(center-6)<1e-5);
        if(type==TS_EQ_LOW_SHELF || type==TS_EQ_HIGH_SHELF)assert(fabs(center-3)<1e-5);
        if(type==TS_EQ_HIGH_PASS || type==TS_EQ_LOW_PASS)assert(fabs(center+3.0103)<.001);
        if(type==TS_EQ_NOTCH)assert(center < -100);
        const double hz[]={100,700,2000};
        for(int i=0;i<3;++i) {
            double expected=ts_master_eq_response_db(&c,rates[ri],hz[i]);
            double actual=measure(&e,rates[ri],hz[i]);
            assert(fabs(expected-actual)<.02);
        }
    }
    TsMasterEq e;ts_master_eq_init(&e);ts_master_eq_prepare(&e,48000);
    TsMasterEqControls c=e.controls;c.enabled=1;
    for(int i=0;i<5;++i)c.band[i]=(TsEqBand){1,TS_EQ_BELL,1000,6,2};
    assert(fabs(ts_master_eq_response_db(&c,48000,1000)-30)<.001);
    c.band[4].enabled=0;assert(fabs(ts_master_eq_response_db(&c,48000,1000)-24)<.001);
    c.enabled=0;assert(ts_master_eq_response_db(&c,48000,1000)==0);
    /* Neutral and settled bypass preserve the exact stereo input. */
    for(int i=0;i<1000;++i) {
        TsStereoFrame x={.1234567f,-.7654321f};
        TsStereoFrame y=ts_master_eq_process(&e,x);assert(!memcmp(&x,&y,sizeof(x)));
    }
    c=e.controls;c.enabled=1;ts_master_eq_set(&e,&c);
    for(int i=0;i<3000;++i) {
        TsStereoFrame x={.1234567f,-.7654321f};
        TsStereoFrame y=ts_master_eq_process(&e,x);assert(!memcmp(&x,&y,sizeof(x)));
    }
}
static void stability_tests(void)
{
    TsMasterEq e;ts_master_eq_init(&e);ts_master_eq_prepare(&e,48000);
    TsMasterEqControls c=e.controls;c.enabled=1;
    unsigned seed=17;double peak=0;clock_t start=clock();
    /* Every filter type, endpoints, tight Q, five boosts, and edits faster than
       the fade. Fixed filter poles cannot be destabilized by coefficient ramps. */
    for(int i=0;i<480000;++i) {
        if(i%113==0) {
            seed=seed*1664525u+1013904223u;
            int n=(int)((seed>>8)%5);TsEqBand *b=&c.band[n];
            b->type=(seed>>12)%6;b->frequency=seed&1?20:20000;
            b->q=seed&2?.3f:8;b->gain_db=seed&4?12:-12;b->enabled=(seed>>6)&1;
            c.enabled=(seed>>9)&1;ts_master_eq_set(&e,&c);
        }
        TsStereoFrame y=ts_master_eq_process(&e,(TsStereoFrame){.1f*sinf(i*.13f),.05f*cosf(i*.17f)});
        assert(isfinite(y.l) && isfinite(y.r));peak=fmax(peak,fmax(fabs(y.l),fabs(y.r)));
        assert(peak<100000); /* Large but stable cascades; final limiter is separate. */
    }
    for(int i=0;i<480000;++i)ts_master_eq_process(&e,(TsStereoFrame){0});
    TsStereoFrame silent=ts_master_eq_process(&e,(TsStereoFrame){0});
    assert(fabs(silent.l)+fabs(silent.r)<1e-8);
    printf("EQ rapid-edit peak %.4f; 20 seconds of DSP in %.3f CPU seconds\n",peak,(double)(clock()-start)/CLOCKS_PER_SEC);
    c=e.controls;c.enabled=1;c.band[0]=(TsEqBand){1,TS_EQ_BELL,1000,NAN,INFINITY};
    ts_master_eq_set(&e,&c);assert(e.controls.band[0].gain_db==0 && isfinite(e.controls.band[0].q));
    TsStereoFrame y=ts_master_eq_process(&e,(TsStereoFrame){NAN,INFINITY});assert(isfinite(y.l+y.r));
    /* Reconfiguration retains controls, clamps coefficients below Nyquist. */
    ts_master_eq_prepare(&e,8000);assert(e.sample_rate==8000 && e.controls.enabled);
    assert(ts_master_eq_max_hz(8000)==3600);
}
static void transition_tests(void)
{
    TsMasterEq e;ts_master_eq_init(&e);ts_master_eq_prepare(&e,48000);
    TsMasterEqControls c=e.controls;c.enabled=1;ts_master_eq_set(&e,&c);
    TsStereoFrame in={.1f,-.05f},previous=in;
    for(int i=0;i<48000;++i)previous=ts_master_eq_process(&e,in);
    /* On steady DC, type/gain/Q/enable changes have no first-sample step;
       the whole transition and its endpoint remain continuous. */
    for(int type=0;type<TS_EQ_TYPE_COUNT;++type) {
        c.band[0]=(TsEqBand){1,type,300,12,2};ts_master_eq_set(&e,&c);
        TsStereoFrame first=ts_master_eq_process(&e,in);
        assert(fabs(first.l-previous.l)<1e-6);previous=first;
        for(int i=0;i<48000;++i) {
            TsStereoFrame y=ts_master_eq_process(&e,in);
            assert(fabs(y.l-previous.l)<.005);previous=y;
        }
    }
    c.band[0].enabled=0;ts_master_eq_set(&e,&c);
    for(int i=0;i<48000;++i) {
        TsStereoFrame y=ts_master_eq_process(&e,in);
        assert(fabs(y.l-previous.l)<.005);previous=y;
    }
    c.band[0]=(TsEqBand){1,TS_EQ_LOW_SHELF,1000,12,.70710678f};ts_master_eq_set(&e,&c);
    for(int i=0;i<48000;++i)previous=ts_master_eq_process(&e,in);
    c.enabled=0;ts_master_eq_set(&e,&c);
    for(int i=0;i<1200;++i) {
        TsStereoFrame y=ts_master_eq_process(&e,in);
        assert(fabs(y.l-previous.l)<.001);previous=y;
    }
    assert(!memcmp(&previous,&in,sizeof(in)));
}
static void output_tests(void)
{
    static TsSisterRuntime r;ts_sister_runtime_init(&r);
    assert(ts_sister_limiter_reconfigure(&r.limiter,48000));ts_master_eq_prepare(&r.master_eq,48000);
    TsMasterEqControls c=r.master_eq.controls;c.enabled=1;
    for(int i=0;i<5;++i)c.band[i]=(TsEqBand){1,TS_EQ_BELL,1000,12,1};
    ts_master_eq_set(&r.master_eq,&c);
    double peak=0;
    ts_sister_runtime_begin_audio_block(&r);
    for(int i=0;i<96000;++i) {
        float x=.1f*sinf(i*6.28318530718f*1000/48000);
        TsStereoFrame y=ts_sister_runtime_process_output(&r,(TsStereoFrame){x,x*.5f});
        assert(isfinite(y.l) && fabs(y.l)<=pow(10,-1./20)+1e-6);
        assert(fabs(y.l-2*y.r)<1e-5);peak=fmax(peak,fabs(y.l));
    }
    ts_sister_runtime_end_audio_block(&r);assert(peak>.85 && r.limiter_gain_reduction_db>20);
    ts_sister_runtime_set_master_output_gain(&r,0);
    for(int i=0;i<5000;++i)ts_sister_runtime_process_output(&r,(TsStereoFrame){.1,.1});
    TsStereoFrame y=ts_sister_runtime_process_output(&r,(TsStereoFrame){.1,.1});assert(y.l==0 && y.r==0);
    ts_sister_runtime_free(&r);
}
static void persistence_tests(void)
{
    static TsConfig config,restored;ts_config_init(&config);config.master_eq.enabled=1;
    for(int i=0;i<5;++i)config.master_eq.band[i]=(TsEqBand){i%2,i,30+1900*i,-12+6*i,.3f+i};
    char error[256];assert(ts_config_save(&config,"test-master-eq.ini",error,sizeof(error)));
    assert(ts_config_load(&restored,"test-master-eq.ini",error,sizeof(error)));
    assert(!memcmp(&config.master_eq,&restored.master_eq,sizeof(config.master_eq)));remove("test-master-eq.ini");
    static TsSisterRuntime runtime;ts_sister_runtime_init(&runtime);
    ts_master_eq_prepare(&runtime.master_eq,48000);ts_master_eq_set(&runtime.master_eq,&config.master_eq);
    static TsSisterProjectState state,loaded;int present;
    ts_sister_project_state_capture(&state,&runtime,1,"CUSTOM");
    assert(ts_sister_project_state_save_file(&state,"test-master-eq.state",error,sizeof(error)));
    assert(ts_sister_project_state_load_file(&loaded,"test-master-eq.state",48000,&present,error,sizeof(error)) && present);
    assert(!memcmp(&loaded.master_eq,&config.master_eq,sizeof(config.master_eq)));
    TsSisterParameters p=runtime.parameters;p.wow=.6f;ts_sister_runtime_set_parameters(&runtime,&p);
    assert(!memcmp(&runtime.master_eq.controls,&config.master_eq,sizeof(config.master_eq))); /* Preset isolation. */
    FILE *f=fopen("test-master-eq.state","w");assert(f);
    fputs("TapeSister Sister Project State\nVersion=21\nPageCount=1\nActivePage=0\n",f);fclose(f);
    assert(ts_sister_project_state_load_file(&loaded,"test-master-eq.state",48000,&present,error,sizeof(error)));
    assert(!loaded.master_eq.enabled);
    for(int i=0;i<5;++i)assert(loaded.master_eq.band[i].gain_db==0);
    assert(ts_sister_project_state_apply(&loaded,&runtime,NULL));assert(!runtime.master_eq.controls.enabled);
    assert(ts_master_eq_read(&loaded.master_eq,"MasterEq.Band.1","1,0,nan,0,1")<0);
    assert(ts_master_eq_read(&loaded.master_eq,"MasterEq.Band.9","1,0,100,0,1")<0);
    assert(ts_master_eq_read(&loaded.master_eq,"MasterEq.Band.1","1,0,100,0,1junk")<0);
    remove("test-master-eq.state");ts_sister_runtime_free(&runtime);
}
int main(void)
{ response_tests();stability_tests();transition_tests();output_tests();persistence_tests();puts("Master EQ response, stability, output protection and persistence passed.");return 0; }
