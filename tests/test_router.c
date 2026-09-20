#include "tapesister/router.h"
#include "tapesister/config.h"
#include "tapesister/sister_project_state.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static int next_order(int *p,int length)
{
    int i=length-2;while(i>=0 && p[i]>p[i+1])--i;if(i<0)return 0;
    int j=length-1;while(p[j]<p[i])--j;
    int v=p[i];p[i]=p[j];p[j]=v;
    for(int a=i+1,b=length-1;a<b;++a,--b){v=p[a];p[a]=p[b];p[b]=v;}
    return 1;
}
typedef struct {int seen[TS_ROUTER_COUNT],count;} Trace;
static TsStereoFrame effect(void *ctx,int s,TsStereoFrame in)
{
    if(ctx){Trace *t=ctx;t->seen[t->count++%TS_ROUTER_COUNT]=s;}
    float *p[]={&in.l,&in.r};
    for(int i=0;i<2;++i)switch(s) {
    case 0:*p[i]=*p[i]*.7f+.01f;break;
    case 1:*p[i]=tanhf(*p[i]*2);break;
    case 2:*p[i]=*p[i]*.4f+.1f;break;
    case 3:*p[i]=*p[i]*fabsf(*p[i])+*p[i]*.2f;break;
    }
    return in;
}
static void engine_tests(void)
{
    TsRouter r;ts_router_init(&r);ts_router_prepare(&r,48000);
    TsRouterControls c=r.controls;c.bypass_mask=0;Trace trace={0};int combinations=0;
    TsStereoFrame in={.1f,-.07f},last={0},out;
    do {
        ts_router_set(&r,&c);
        for(int n=0;n<600;++n){trace.count=0;out=ts_router_process(&r,in,effect,&trace);assert(isfinite(out.l+out.r));}
        assert(!r.handoff && r.gain==1 && trace.count==TS_ROUTER_COUNT);
        TsStereoFrame expected=in;
        for(int i=0;i<TS_ROUTER_COUNT;++i){assert(trace.seen[i]==c.order[i]);expected=effect(NULL,c.order[i],expected);}
        assert(fabsf(expected.l-out.l)<1e-6f && fabsf(expected.r-out.r)<1e-6f);++combinations;
    }while(next_order(c.order,TS_ROUTER_COUNT));
    assert(combinations==120);
    c.bypass_mask=31;ts_router_set(&r,&c);
    for(int n=0;n<600;++n)out=ts_router_process(&r,in,effect,NULL);
    assert(!memcmp(&out,&in,sizeof(in)));
    for(int s=0;s<TS_ROUTER_COUNT;++s) {
        ts_router_toggle_solo(&c,s);assert(c.bypass_mask==31);ts_router_set(&r,&c);
        for(int n=0;n<600;++n)out=ts_router_process(&r,in,effect,NULL);
        TsStereoFrame expected=effect(NULL,s,in);assert(fabsf(expected.l-out.l)<1e-6f);
        ts_router_toggle_solo(&c,s);assert(!c.solo && c.bypass_mask==31);
    }
    /* Constant input isolates discontinuities caused by rapid graph changes. */
    ts_router_default(&c);ts_router_set(&r,&c);
    for(int n=0;n<2000;++n)last=ts_router_process(&r,in,effect,NULL);
    for(int n=0;n<16000;++n) {
        if(n%97==0){ts_router_move(&c,n%4,(n/97)%4);ts_router_toggle_bypass(&c,(n/97)%4);ts_router_set(&r,&c);}
        out=ts_router_process(&r,in,effect,NULL);
        assert(fabsf(out.l-last.l)<.01f && fabsf(out.r-last.r)<.01f);last=out;
    }
    out=ts_router_process(&r,(TsStereoFrame){NAN,INFINITY},effect,NULL);assert(isfinite(out.l+out.r));
    c.order[1]=c.order[0];ts_router_set(&r,&c);assert(ts_router_valid(&r.controls));
}

static void persistence_tests(void)
{
    TsConfig config,loaded;ts_config_init(&config);char error[160];
    ts_router_move(&config.router,0,3);config.router.bypass_mask=5;config.router.solo=2;
    assert(ts_config_save(&config,"test-router-config.ini",error,sizeof(error)));
    assert(ts_config_load(&loaded,"test-router-config.ini",error,sizeof(error)));
    assert(!memcmp(&loaded.router,&config.router,sizeof(config.router)));
    TsSisterProjectState state,read;ts_sister_project_state_init(&state,48000);state.router=config.router;
    assert(ts_sister_project_state_save_file(&state,"test-router-project.ini",error,sizeof(error)));int present=0;
    assert(ts_sister_project_state_load_file(&read,"test-router-project.ini",48000,&present,error,sizeof(error)) && present);
    assert(!memcmp(&state.router,&read.router,sizeof(state.router)));
    TsSisterRuntime runtime;ts_sister_runtime_init(&runtime);
    assert(ts_sister_project_state_apply(&read,&runtime,NULL));
    assert(!memcmp(&runtime.router.controls,&state.router,sizeof(state.router)));
    TsSisterParameters sound=runtime.parameters;sound.prism.spread=.6f;ts_sister_runtime_set_parameters(&runtime,&sound);
    assert(!memcmp(&runtime.router.controls,&state.router,sizeof(state.router))); /* Presets do not own routing. */
    assert(ts_sister_runtime_reconfigure(&runtime,44100,2,error,sizeof(error)));
    assert(!memcmp(&runtime.router.controls,&state.router,sizeof(state.router)) && runtime.router.sample_rate==44100);
    ts_sister_runtime_free(&runtime);
    FILE *f=fopen("test-router-project.ini","wb");assert(f);
    fputs("TapeSister Sister Project State\nVersion=22\nPageCount=1\nActivePage=0\n",f);fclose(f);
    assert(ts_sister_project_state_load_file(&read,"test-router-project.ini",48000,&present,error,sizeof(error)));
    TsRouterControls defaults;ts_router_default(&defaults);assert(!memcmp(&read.router,&defaults,sizeof(defaults)));
    f=fopen("test-router-config.ini","wb");assert(f);fputs("[Audio]\n",f);fclose(f);
    assert(ts_config_load(&loaded,"test-router-config.ini",error,sizeof(error)));
    assert(!memcmp(&loaded.router,&defaults,sizeof(defaults)));
    assert(ts_router_read(&defaults,"Router.Order","0,0,2,3")==-1);
    assert(ts_router_read(&defaults,"Router.Bypass","32")==-1);
    assert(ts_router_read(&defaults,"Router.Solo","-1")==-1);
    assert(ts_router_read(&defaults,"Router.Order","3,2,1,0 junk")==-1);
    remove("test-router-config.ini");remove("test-router-project.ini");
}

static void real_dsp_orders(void)
{
    TsRouterControls route;ts_router_default(&route);double fingerprints[24];int count=0;
    do {
        TsSisterRuntime r;ts_sister_runtime_init(&r);char error[160];
        assert(ts_sister_runtime_enable(&r,8000,2,2,1,error,sizeof(error)));
        TsSisterParameters p=r.parameters;
        p.monitor_dry=0;p.monitor_wet=.6f;p.head1_time_ms=11;p.head1_level=.7f;p.head2_level=.3f;
        p.prism.enabled=1;p.prism.lenses=24;p.prism.spread=.6f;p.prism.drift=0;p.prism.mix=.7f;
        p.fx.slot[0]=(TsSisterFxSlotControls){TS_SISTER_FX_DISTORTION,1,TS_SISTER_FX_PLACE_POST,0,.8f,.6f,.5f,.7f};
        p.fx.slot[1]=(TsSisterFxSlotControls){TS_SISTER_FX_DELAY,1,TS_SISTER_FX_PLACE_POST,0,.1f,.3f,.5f,.25f};
        p.fx.enabled=1;p.fx.master_transition=ts_sister_fx_transition_normalized(10);
        p.fx.fallout.enabled=1;p.fx.fallout.mix=.4f;p.fx.fallout.noise=.12f;
        ts_sister_runtime_set_parameters(&r,&p);ts_sister_runtime_set_sources(&r,TS_SISTER_SOURCE_PREVIEW);
        ts_sister_runtime_set_monitor(&r,1);
        ts_router_set(&r.router,&route);double energy=0,print=0;
        ts_sister_runtime_begin_audio_block(&r);
        for(int i=0;i<6000;++i) {
            TsSisterSourceFrames s={0};s.preview=(TsStereoFrame){.12f*sinf(i*.11f),.09f*cosf(i*.09f)};
            TsSisterRuntimeFrame frame=ts_sister_runtime_process_frame(&r,&s);
            assert(fabsf(frame.tap[TS_SISTER_TAP_MIX].l)<=1 && fabsf(frame.tap[TS_SISTER_TAP_MIX].r)<=1);
            TsStereoFrame y=ts_sister_runtime_process_output(&r,frame.monitor_return);
            assert(isfinite(y.l+y.r) && fabsf(y.l)<4 && fabsf(y.r)<4);
            if(i>3000){energy+=fabs(y.l)+fabs(y.r);print+=y.l*(1+i%17)+y.r*(1+i%11);}
        }
        ts_sister_runtime_end_audio_block(&r);assert(energy>.1);fingerprints[count++]=print;
        TsSisterRoutingSnapshot snap;assert(ts_sister_runtime_get_snapshot(&r,&snap));
        for(int i=0;i<TS_ROUTER_COUNT;++i)assert(snap.router_peaks[i*2]>.0001f);
        assert(!memcmp(&snap.router,&route,sizeof(route)));ts_sister_runtime_free(&r);
    }while(next_order(route.order,4));
    assert(count==24);
    for(int i=1;i<count;++i)for(int j=0;j<i;++j)assert(fabs(fingerprints[j]-fingerprints[i])>1e-4);
    puts("All 24 real DSP routing orders produce finite, audible, distinct output");
}
int main(void)
{ engine_tests();persistence_tests();real_dsp_orders();puts("Router engine, transitions and persistence passed");return 0; }
