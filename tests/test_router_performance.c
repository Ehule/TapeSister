#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/router.h"
#include "tapesister/config.h"
#include "tapesister/sister_project_state.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void init(TsRouter *r)
{ ts_router_init(r);ts_router_prepare(r,1000); }
static void assert_state(TsRouter *r,unsigned bypass,int solo)
{ assert(r->controls.bypass_mask==bypass && r->controls.solo==solo); }
static void timers(void)
{
    TsRouter r;init(&r);unsigned initial=1u<<TS_ROUTER_INSERT;
    r.performance.timer_seconds[0]=.1f;r.performance.timer_seconds[1]=.05f;
    assert(ts_router_timer_start(&r,0,TS_ROUTER_TIMER_SOLO));assert_state(&r,initial,1);
    ts_router_performance_advance(&r,20);
    assert(ts_router_timer_start(&r,1,TS_ROUTER_TIMER_SOLO));assert_state(&r,initial,2);
    ts_router_performance_advance(&r,49);assert_state(&r,initial,2);
    ts_router_performance_advance(&r,1);assert_state(&r,initial,1);
    ts_router_performance_advance(&r,30);assert_state(&r,initial,0);
    assert(!r.transport.timer_mask);
    /* Older actions may expire underneath a newer action without resurfacing. */
    r.performance.timer_seconds[0]=.05f;r.performance.timer_seconds[1]=.1f;
    ts_router_timer_start(&r,0,TS_ROUTER_TIMER_SOLO);ts_router_timer_start(&r,1,TS_ROUTER_TIMER_SOLO);
    ts_router_performance_advance(&r,50);assert_state(&r,initial,2);
    ts_router_performance_advance(&r,50);assert_state(&r,initial,0);
    /* A later bypass temporarily takes precedence over solo; cancellation
       reveals the still-running solo, rather than changing its saved base. */
    ts_router_timer_start(&r,1,TS_ROUTER_TIMER_SOLO);ts_router_timer_start(&r,0,TS_ROUTER_TIMER_BYPASS);
    assert_state(&r,initial|1,0);ts_router_timer_cancel(&r,0);assert_state(&r,initial,2);
    ts_router_timer_cancel(&r,1);assert_state(&r,initial,0);
    /* Retrigger replaces a stage's event and never snapshots its own overlay. */
    ts_router_timer_start(&r,0,TS_ROUTER_TIMER_SOLO);ts_router_performance_advance(&r,25);
    ts_router_timer_start(&r,0,TS_ROUTER_TIMER_BYPASS);ts_router_performance_advance(&r,49);
    assert_state(&r,initial|1,0);ts_router_performance_advance(&r,1);assert_state(&r,initial,0);
    r.performance.timer_after[0]=1;
    ts_router_timer_start(&r,0,TS_ROUTER_TIMER_BYPASS);ts_router_performance_advance(&r,49);assert_state(&r,initial,0);
    ts_router_performance_advance(&r,1);assert_state(&r,initial|1,0);
    ts_router_timer_start(&r,0,TS_ROUTER_TIMER_SOLO);ts_router_timer_cancel(&r,0);
    ts_router_performance_advance(&r,500);assert_state(&r,initial|1,0);
    ts_router_timer_start(&r,0,TS_ROUTER_TIMER_SOLO);ts_router_performance_advance(&r,50);assert_state(&r,initial|1,1);
    /* Manual takeover owns the current audible state; no stale expiry later. */
    ts_router_timer_start(&r,1,TS_ROUTER_TIMER_SOLO);
    TsRouterControls c=r.controls;ts_router_toggle_bypass(&c,2);ts_router_set(&r,&c);
    assert(!r.transport.timer_mask);ts_router_performance_advance(&r,1000);assert_state(&r,c.bypass_mask,c.solo);
    /* All stages may carry events, including Fallout and Insert. */
    init(&r);
    for(int i=0;i<TS_ROUTER_COUNT;++i){r.performance.timer_seconds[i]=.05f;ts_router_timer_start(&r,i,TS_ROUTER_TIMER_SOLO);}
    assert(r.transport.timer_mask==(1u<<TS_ROUTER_COUNT)-1);assert_state(&r,initial,TS_ROUTER_COUNT);
    for(int i=TS_ROUTER_COUNT-1;i>=0;--i){ts_router_timer_cancel(&r,i);assert_state(&r,initial,i);}
    /* Sample-rate handoff preserves wall duration, not the old frame count. */
    ts_router_timer_start(&r,0,TS_ROUTER_TIMER_SOLO);ts_router_performance_advance(&r,25);
    ts_router_prepare(&r,2000);assert(fabsf(ts_router_view(&r).timer_remaining[0]-.025f)<1e-6f);
    ts_router_performance_advance(&r,49);assert_state(&r,initial,1);ts_router_performance_advance(&r,1);assert_state(&r,initial,0);
}
static void bank(TsRouter *r)
{
    init(r);r->performance.length=3;r->performance.loop=0;
    for(int i=0;i<3;++i)r->performance.step[i]=(TsRouterStep){i,.1f};
    ts_router_store(r,0);
    TsRouterControls c=r->controls;c.solo=1;ts_router_set(r,&c);ts_router_store(r,1);
    c.solo=0;c.bypass_mask|=1;ts_router_set(r,&c);ts_router_store(r,2);
    ts_router_recall(r,0);
}
static void sequence(void)
{
    TsRouter r;bank(&r);TsRouterControls before=r.controls;
    ts_router_sequence_play(&r);assert(r.transport.running && r.transport.step==0);
    ts_router_performance_advance(&r,99);assert(r.transport.step==0);
    ts_router_performance_advance(&r,1);assert(r.transport.step==1);assert_state(&r,16,1);
    TsRouterPerformance p=r.performance;p.step[1]=(TsRouterStep){2,.5f};p.step[2].seconds=.2f;
    assert(ts_router_performance_set(&r,&p));assert(r.transport.frames==100);assert_state(&r,16,1);
    int order[TS_ROUTER_COUNT];memcpy(order,r.controls.order,sizeof(order));
    ts_router_reorder(&r,4,0);assert(!memcmp(order,r.controls.order,sizeof(order)));
    ts_router_performance_advance(&r,100);assert(r.transport.step==2 && r.transport.frames==200);assert_state(&r,17,0);
    ts_router_performance_advance(&r,200);assert(!r.transport.running);assert_state(&r,17,0);
    ts_router_reorder(&r,4,0);memcpy(order,r.controls.order,sizeof(order));
    assert(ts_router_sequence_restore(&r));assert_state(&r,before.bypass_mask,before.solo);
    assert(!memcmp(order,r.controls.order,sizeof(order)));assert(!ts_router_sequence_restore(&r));
    /* STOP retains both the current step state and the pre-sequence restore. */
    ts_router_sequence_play(&r);ts_router_performance_advance(&r,100);ts_router_sequence_stop(&r);
    assert_state(&r,17,0);ts_router_performance_advance(&r,10000);assert_state(&r,17,0);
    ts_router_sequence_reset(&r);assert(!r.transport.running && r.transport.step==0);assert_state(&r,16,0);
    assert(ts_router_sequence_restore(&r));assert_state(&r,16,0);
    /* Empty and HOLD steps retain the previous state for their full duration. */
    r.performance.length=2;r.performance.loop=1;r.performance.step[0]=(TsRouterStep){25,.05f};
    r.performance.step[1]=(TsRouterStep){-1,.05f};
    ts_router_sequence_play(&r);assert(r.transport.missing);ts_router_performance_advance(&r,50);
    assert(r.transport.step==1 && !r.transport.missing);ts_router_performance_advance(&r,50);assert(r.transport.step==0);
    ts_router_store(&r,25);ts_router_performance_advance(&r,100);assert(!r.transport.missing);
    /* Shortening a running sequence takes effect at the next boundary. */
    ts_router_performance_advance(&r,50);p=r.performance;p.length=1;p.loop=0;ts_router_performance_set(&r,&p);
    assert(r.transport.running && r.transport.step==1);ts_router_performance_advance(&r,50);assert(!r.transport.running);
    /* Timer overlays coexist with the evolving sequence base. */
    bank(&r);r.performance.timer_seconds[3]=.25f;ts_router_sequence_play(&r);ts_router_timer_start(&r,3,TS_ROUTER_TIMER_SOLO);
    ts_router_performance_advance(&r,200);assert_state(&r,17,4);
    ts_router_performance_advance(&r,50);assert_state(&r,17,0);
    ts_router_sequence_restore(&r);assert_state(&r,16,0);
    /* Delayed actions win a simultaneous sequence boundary. */
    ts_router_sequence_play(&r);r.performance.timer_seconds[2]=.1f;r.performance.timer_after[2]=1;
    ts_router_timer_start(&r,2,TS_ROUTER_TIMER_SOLO);ts_router_performance_advance(&r,100);assert_state(&r,16,3);
    ts_router_recall(&r,0);assert(r.transport.running && r.transport.manual_override && !r.transport.timer_mask);
    ts_router_performance_advance(&r,100);assert_state(&r,17,0);assert(!r.transport.manual_override);
    /* Full 64-step traversal, looping and exact accounting across chunks. */
    bank(&r);r.performance.length=64;r.performance.loop=1;
    for(int i=0;i<64;++i)r.performance.step[i]=(TsRouterStep){i%3,.05f};
    ts_router_sequence_play(&r);ts_router_performance_advance(&r,50*64+17);
    assert(r.transport.step==0 && r.transport.frames==33);
    ts_router_prepare(&r,2000);assert(r.transport.frames==66);
    ts_router_sequence_reset(&r);assert(r.transport.running && r.transport.step==0 && r.transport.frames==100);
    /* Non-integral sample durations carry their remainder over long loops. */
    ts_router_sequence_stop(&r);ts_router_prepare(&r,1000);
    for(int i=0;i<64;++i)r.performance.step[i].seconds=.05025f;
    ts_router_sequence_play(&r);
    ts_router_performance_advance(&r,(uint64_t)llround((double)r.performance.step[0].seconds*1000*10000));
    assert(r.transport.step==10000%64 && r.transport.frames>=50);
}
static TsStereoFrame doubled(void *context,int stage,TsStereoFrame in)
{ (void)context;return stage==0?(TsStereoFrame){in.l*2,in.r*2}:in; }
static void transitions(void)
{
    TsRouter r;init(&r);r.performance.timer_seconds[0]=.05f;ts_router_timer_start(&r,0,TS_ROUTER_TIMER_BYPASS);
    TsStereoFrame prev={.2f,-.2f};
    for(int i=0;i<80;++i) {
        TsStereoFrame out=ts_router_process(&r,(TsStereoFrame){.1f,-.1f},doubled,NULL);
        assert(isfinite(out.l+out.r) && fabsf(out.l-prev.l)<=.01001f);
        if(i==49)assert(!r.transport.timer_mask);
        prev=out;
    }
    assert(fabsf(prev.l-.2f)<1e-6f && !r.handoff);
}
static void persistence(void)
{
    char error[200];TsRouter r;bank(&r);r.performance.step[63]=(TsRouterStep){25,3600};ts_router_store(&r,25);
    r.performance.timer_seconds[4]=16;r.performance.timer_after[4]=1;
    TsConfig config,loaded;ts_config_init(&config);config.router_performance=r.performance;config.router=r.controls;
    assert(ts_config_save(&config,"test-router-perf.ini",error,sizeof(error)));
    assert(ts_config_load(&loaded,"test-router-perf.ini",error,sizeof(error)));
    assert(!memcmp(&config.router_performance,&loaded.router_performance,sizeof(r.performance)));
    TsSisterRuntime runtime;ts_sister_runtime_init(&runtime);ts_sister_runtime_set_router(&runtime,&r.controls);
    ts_router_performance_set(&runtime.router,&r.performance);ts_router_timer_start(&runtime.router,0,TS_ROUTER_TIMER_SOLO);
    TsSisterProjectState state,read;ts_sister_project_state_capture(&state,&runtime,1,NULL);
    assert(state.router.solo==0); /* Temporary solo must never become permanent on save. */
    assert(ts_sister_project_state_save_file(&state,"test-router-perf-project.ini",error,sizeof(error)));
    int present=0;assert(ts_sister_project_state_load_file(&read,"test-router-perf-project.ini",48000,&present,error,sizeof(error)) && present);
    assert(!memcmp(&read.router_performance,&state.router_performance,sizeof(r.performance)));
    ts_router_sequence_play(&runtime.router);assert(ts_sister_project_state_apply(&read,&runtime,NULL));
    assert(!runtime.router.transport.running && !runtime.router.transport.timer_mask && !runtime.router.transport.restore_valid);
    assert_state(&runtime.router,16,0);
    TsSisterRoutingSnapshot snapshot;assert(ts_sister_runtime_get_snapshot(&runtime,&snapshot));assert(!snapshot.router_view.running);
    FILE *f=fopen("test-router-perf-project.ini","wb");assert(f);
    fputs("TapeSister Sister Project State\nVersion=24\nPageCount=1\nActivePage=0\n",f);fclose(f);
    assert(ts_sister_project_state_load_file(&read,"test-router-perf-project.ini",48000,&present,error,sizeof(error)));
    assert(!read.router_performance.occupied && read.router_performance.length==16);
    const char *bad[][2]={{"RouterPerf.State.A","0,-1"},{"RouterPerf.State.[","0,0"},
        {"RouterPerf.Step.65","0,1"},{"RouterPerf.Step.1","26,1"},{"RouterPerf.Step.1","0,0"},
        {"RouterPerf.Step.1","0,nan"},{"RouterPerf.Timer.5","0,1"},{"RouterPerf.Timer.0","2,1"},
        {"RouterPerf.MaxSeconds","inf"},{"RouterPerf.MaxSeconds","3601"},{"RouterPerf.Length","0"},
        {"RouterPerf.Loop","2"},{"RouterPerf.Step.1","0,1junk"},{"RouterPerf.State.A","0"}};
    for(size_t i=0;i<sizeof(bad)/sizeof(*bad);++i) {
        TsRouterPerformance copy=read.router_performance;
        assert(ts_router_performance_read(&read.router_performance,bad[i][0],bad[i][1])==-1);
        assert(!memcmp(&copy,&read.router_performance,sizeof(copy)));
    }
    TsRouterPerformance future=r.performance;
    assert(ts_router_performance_read(&future,"RouterPerf.State.A","32,0")==1 && !(future.occupied&1));
    assert(ts_router_performance_read(&future,"RouterPerf.State.B","0,6")==1 && !(future.occupied&2));
    TsRouterPerformance p=r.performance;p.max_seconds=2;assert(ts_router_performance_set(&r,&p));assert(r.performance.step[63].seconds==2);
    /* Saving during a step and during live override preserves the manual base. */
    bank(&r);ts_router_sequence_play(&r);ts_router_performance_advance(&r,100);
    TsRouterControls persisted=ts_router_export(&r);assert(persisted.solo==0 && persisted.bypass_mask==16);
    ts_router_manual(&r,4,0);assert(r.transport.running && r.transport.manual_override);
    persisted=ts_router_export(&r);assert(persisted.solo==0 && persisted.bypass_mask==16);
    ts_router_performance_advance(&r,100);assert_state(&r,17,0);
    /* PLAY captures underlying manual state even with an existing timed solo. */
    bank(&r);ts_router_timer_start(&r,3,TS_ROUTER_TIMER_SOLO);ts_router_sequence_play(&r);
    assert(r.transport.timer_mask && r.transport.before_sequence.solo==0);
    ts_router_sequence_restore(&r);assert_state(&r,16,0);assert(!r.transport.timer_mask);
    /* Manual input continues clock, cancels timers, and never edits slots. */
    bank(&r);TsRouterPerformance original=r.performance;ts_router_sequence_play(&r);
    ts_router_performance_advance(&r,20);ts_router_timer_start(&r,0,TS_ROUTER_TIMER_SOLO);
    ts_router_manual(&r,4,0);assert_state(&r,0,0);
    assert(r.transport.running && r.transport.frames==80 && !r.transport.timer_mask);
    assert(!memcmp(&original,&r.performance,sizeof(original)));
    ts_router_performance_advance(&r,80);assert_state(&r,16,1);assert(!r.transport.manual_override);
    ts_router_manual(&r,2,1);assert_state(&r,16,3);ts_router_sequence_stop(&r);
    assert_state(&r,16,3);ts_router_performance_advance(&r,500);assert_state(&r,16,3);
    ts_router_sequence_restore(&r);assert_state(&r,16,0);
    /* Hour-long intervals use wide frame counts, including rate handoff. */
    bank(&r);ts_router_prepare(&r,192000);r.performance.step[0].seconds=3600;
    ts_router_sequence_play(&r);assert(r.transport.frames==UINT64_C(691200000));
    ts_router_performance_advance(&r,UINT64_C(691199999));assert(r.transport.step==0);
    ts_router_performance_advance(&r,1);assert(r.transport.step==1);
    ts_sister_runtime_free(&runtime);remove("test-router-perf.ini");remove("test-router-perf-project.ini");
}
int main(void)
{ timers();sequence();transitions();persistence();puts("Router performance: timers, layering, sequence, clock, transitions and persistence passed");return 0; }
