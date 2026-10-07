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
            c.enabled=(seed>>9)&1;c.solo_band=(seed>>18)%6;ts_master_eq_set(&e,&c);
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
static void solo_tests(void)
{
    TsMasterEq eq;ts_master_eq_init(&eq);ts_master_eq_prepare(&eq,48000);
    TsMasterEqControls c=eq.controls;c.enabled=1;
    for(int i=0;i<TS_MASTER_EQ_BANDS;++i)c.band[i]=(TsEqBand){1,TS_EQ_BELL,1000,3+i,1};
    c.band[1].enabled=0;
    assert(fabs(ts_master_eq_response_db(&c,48000,1000)-21)<.001);
    TsMasterEqControls remembered=c;
    /* Solo can audition a bypassed band without losing the stored bypass mask. */
    c.solo_band=2;ts_master_eq_set(&eq,&c);
    for(int i=0;i<TS_MASTER_EQ_BANDS;++i)assert(ts_master_eq_band_active(&c,i)==(i==1));
    assert(fabs(ts_master_eq_response_db(&c,48000,1000)-4)<.001);
    assert(fabs(measure(&eq,48000,1000)-4)<.02);
    assert(!memcmp(c.band,remembered.band,sizeof(c.band)));
    c.solo_band=5;ts_master_eq_set(&eq,&c);
    assert(fabs(measure(&eq,48000,1000)-7)<.02);
    c.solo_band=0;ts_master_eq_set(&eq,&c);
    assert(fabs(measure(&eq,48000,1000)-21)<.02);
    assert(!memcmp(&c,&remembered,sizeof(c)));
    /* Reconfiguration and bypass use the same active-band decision as the graph. */
    c.solo_band=2;ts_master_eq_set(&eq,&c);ts_master_eq_prepare(&eq,44100);
    assert(fabs(measure(&eq,44100,1000)-4)<.02);
    c.enabled=0;ts_master_eq_set(&eq,&c);
    assert(fabs(measure(&eq,44100,1000))<.001);
    assert(ts_master_eq_response_db(&c,44100,1000)==0);
    ts_master_eq_toggle_band(&c,1);assert(!c.solo_band && !c.band[1].enabled);
    c.solo_band=3;ts_master_eq_toggle_band(&c,0);
    assert(!c.solo_band && !c.band[0].enabled && c.band[2].enabled);
    c.solo_band=99;ts_master_eq_sanitize(&c);assert(!c.solo_band);
    assert(!ts_master_eq_band_active(&c,-1) && !ts_master_eq_band_active(&c,5));
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
    static TsConfig config,restored;ts_config_init(&config);config.master_eq.enabled=1;config.master_eq.solo_band=3;
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
    assert(!loaded.master_eq.enabled && !loaded.master_eq.solo_band);
    for(int i=0;i<5;++i)assert(loaded.master_eq.band[i].gain_db==0);
    assert(ts_sister_project_state_apply(&loaded,&runtime,NULL));assert(!runtime.master_eq.controls.enabled);
    assert(ts_master_eq_read(&loaded.master_eq,"MasterEq.Band.1","1,0,nan,0,1")<0);
    assert(ts_master_eq_read(&loaded.master_eq,"MasterEq.SoloBand","6")<0);
    assert(ts_master_eq_read(&loaded.master_eq,"MasterEq.SoloBand","2junk")<0);
    assert(ts_master_eq_read(&loaded.master_eq,"MasterEq.Band.9","1,0,100,0,1")<0);
    assert(ts_master_eq_read(&loaded.master_eq,"MasterEq.Band.1","1,0,100,0,1junk")<0);
    remove("test-master-eq.state");ts_sister_runtime_free(&runtime);
}
static void preset_tests(void)
{
    for(int p=0;p<TS_EQ_PRESET_COUNT;++p) {
        TsMasterEqControls c;assert(ts_master_eq_preset(&c,p));
        assert(c.enabled && !c.solo_band && ts_master_eq_preset_match(&c)==p);
        c.enabled=0;c.solo_band=2;assert(ts_master_eq_preset_match(&c)==p);
        c.band[2].gain_db+=.1f;assert(ts_master_eq_preset_match(&c)==-1);
        assert(ts_master_eq_preset(&c,p));
        const unsigned rates[]={8000,44100,48000,96000};
        for(unsigned r=0;r<4;++r)for(int k=0;k<100;++k) {
            double hz=20*pow(ts_master_eq_max_hz(rates[r])/20.,k/99.);
            double db=ts_master_eq_response_db(&c,rates[r],hz);assert(isfinite(db) && db<5);
        }
    }
    TsMasterEqControls c;ts_master_eq_preset(&c,TS_EQ_PRESET_FLAT);
    assert(ts_master_eq_response_db(&c,48000,1000)==0);
    ts_master_eq_preset(&c,TS_EQ_PRESET_RUMBLE_CUT);assert(ts_master_eq_response_db(&c,48000,20)<-6);
    ts_master_eq_preset(&c,TS_EQ_PRESET_LESS_BOOM);assert(ts_master_eq_response_db(&c,48000,150)<-2);
    ts_master_eq_preset(&c,TS_EQ_PRESET_WARM);assert(ts_master_eq_response_db(&c,48000,80)>1);
    ts_master_eq_preset(&c,TS_EQ_PRESET_PRESENCE);assert(ts_master_eq_response_db(&c,48000,2500)>1);
    ts_master_eq_preset(&c,TS_EQ_PRESET_AIR);assert(ts_master_eq_response_db(&c,48000,16000)>1);
    ts_master_eq_preset(&c,TS_EQ_PRESET_SOFTEN_HIGHS);assert(ts_master_eq_response_db(&c,48000,8000)<-2);
    ts_master_eq_preset(&c,TS_EQ_PRESET_TELEPHONE);
    assert(ts_master_eq_response_db(&c,48000,80)<-20 && ts_master_eq_response_db(&c,48000,12000)<-20);
    TsMasterEqControls saved=c;assert(!ts_master_eq_preset(&c,-1) && !memcmp(&saved,&c,sizeof(c)));
}
static void spectrum_tone(TsEqSpectrum *s,unsigned rate,int bin,float right)
{
    for(int i=0;i<TS_EQ_SPECTRUM_FRAMES;++i) {
        float x=.5f*sinf((float)(6.283185307179586*bin*i/TS_EQ_SPECTRUM_FRAMES));
        ts_eq_spectrum_push(s,(TsStereoFrame){x,x*right},rate);
    }
}
static void spectrum_tests(void)
{
    static TsEqSpectrum s;TsEqSpectrumView v;ts_eq_spectrum_init(&s);ts_eq_spectrum_clear(&v,48000);
    spectrum_tone(&s,48000,85,1);assert(!atomic_load(&s.ready) && !s.fill);
    const unsigned rates[]={8000,44100,48000,96000};
    for(unsigned r=0;r<4;++r)for(int phase=0;phase<3;++phase) {
        ts_eq_spectrum_enable(&s,1);ts_eq_spectrum_clear(&v,rates[r]);
        spectrum_tone(&s,rates[r],85,phase==0?1:phase==1?-1:0);
        assert(atomic_load(&s.ready));
        TsStereoFrame first=s.samples[1];spectrum_tone(&s,rates[r],150,1);
        assert(!memcmp(&s.samples[1],&first,sizeof(first))); /* Full mailbox never overwritten. */
        assert(ts_eq_spectrum_poll(&s,&v,rates[r],.1f) && v.valid);
        int peak=0;for(int i=1;i<TS_EQ_SPECTRUM_POINTS;++i)if(v.db[i]>v.db[peak])peak=i;
        double hz=20*pow(ts_master_eq_max_hz(rates[r])/20.,peak/(double)(TS_EQ_SPECTRUM_POINTS-1));
        assert(fabs(hz/(85.*rates[r]/TS_EQ_SPECTRUM_FRAMES)-1)<.025);
        assert(fabs(v.db[peak]-(phase==2?-9.0309:-6.0206))<.05);
        float level=v.db[peak];
        assert(!ts_eq_spectrum_poll(&s,&v,rates[r],.1f) && v.db[peak]==level);
        for(int n=0;n<TS_EQ_SPECTRUM_FRAMES;++n)ts_eq_spectrum_push(&s,(TsStereoFrame){0},rates[r]);
        assert(ts_eq_spectrum_poll(&s,&v,rates[r],3));
        for(int i=0;i<TS_EQ_SPECTRUM_POINTS;++i)assert(v.db[i]==TS_EQ_SPECTRUM_FLOOR_DB);
        ts_eq_spectrum_enable(&s,0);ts_eq_spectrum_poll(&s,&v,rates[r],.1f);assert(!v.valid);
    }
    /* Reopen and rate changes discard queued/partial captures from the old epoch. */
    ts_eq_spectrum_enable(&s,1);spectrum_tone(&s,48000,85,1);
    ts_eq_spectrum_enable(&s,0);ts_eq_spectrum_enable(&s,1);
    assert(!ts_eq_spectrum_poll(&s,&v,48000,.1f) && !v.valid && !atomic_load(&s.ready));
    for(int i=0;i<1000;++i)ts_eq_spectrum_push(&s,(TsStereoFrame){1,1},48000);
    ts_eq_spectrum_enable(&s,0);ts_eq_spectrum_enable(&s,1);
    spectrum_tone(&s,44100,85,-1);assert(s.rate==44100 && !s.fill);
    assert(!ts_eq_spectrum_poll(&s,&v,48000,.1f));
    spectrum_tone(&s,48000,85,1);assert(ts_eq_spectrum_poll(&s,&v,48000,.1f));
    for(int i=0;i<TS_EQ_SPECTRUM_FRAMES;++i)ts_eq_spectrum_push(&s,(TsStereoFrame){NAN,INFINITY},48000);
    assert(ts_eq_spectrum_poll(&s,&v,48000,3));
    for(int i=0;i<TS_EQ_SPECTRUM_POINTS;++i)assert(v.db[i]==TS_EQ_SPECTRUM_FLOOR_DB);
}
static void spectrum_output_isolation(void)
{
    static TsSisterRuntime reference,observed;
    ts_sister_runtime_init(&reference);ts_sister_runtime_init(&observed);
    assert(ts_sister_limiter_reconfigure(&reference.limiter,48000));
    assert(ts_sister_limiter_reconfigure(&observed.limiter,48000));
    ts_master_eq_prepare(&reference.master_eq,48000);ts_master_eq_prepare(&observed.master_eq,48000);
    ts_eq_spectrum_enable(&observed.eq_spectrum,1);
    TsEqSpectrumView view;ts_eq_spectrum_clear(&view,48000);
    for(int i=0;i<48000;++i) {
        if(i%6000==0) {
            TsMasterEqControls c;assert(ts_master_eq_preset(&c,i/6000));
            ts_master_eq_set(&reference.master_eq,&c);ts_master_eq_set(&observed.master_eq,&c);
        }
        TsStereoFrame input={.4f*sinf(i*.07f),-.5f*cosf(i*.31f)};
        TsStereoFrame a=ts_sister_runtime_process_output(&reference,input);
        TsStereoFrame b=ts_sister_runtime_process_output(&observed,input);
        assert(!memcmp(&a,&b,sizeof(a)));
        if(i%4800==4799)ts_eq_spectrum_poll(&observed.eq_spectrum,&view,48000,.1f);
    }
    assert(view.valid);
    ts_sister_runtime_free(&reference);ts_sister_runtime_free(&observed);
}
int main(void)
{ response_tests();stability_tests();transition_tests();solo_tests();output_tests();persistence_tests();preset_tests();spectrum_tests();spectrum_output_isolation();puts("Master EQ response, presets, stereo spectrum, output protection and persistence passed.");return 0; }
