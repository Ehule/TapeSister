#include "tapesister/sister_project_state.h"
#include "tapesister/config.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void close_frame(TsStereoFrame a,TsStereoFrame b)
{ assert(fabsf(a.l-b.l)<.0002f && fabsf(a.r-b.r)<.0002f); }
static TsStereoFrame external(TsStereoFrame x)
{ return (TsStereoFrame){-x.r*.5f,x.l*.75f}; }
static TsStereoFrame internal(int stage,TsStereoFrame x)
{ return (TsStereoFrame){x.l*(.6f+stage*.1f)+.01f*(stage+1),x.r*.7f-.02f*(stage+1)}; }
static int next_order(int *p)
{
    int i=TS_ROUTER_COUNT-2;while(i>=0 && p[i]>p[i+1])--i;if(i<0)return 0;
    int j=TS_ROUTER_COUNT-1;while(p[j]<p[i])--j;
    int v=p[i];p[i]=p[j];p[j]=v;
    for(int a=i+1,b=TS_ROUTER_COUNT-1;a<b;++a,--b){v=p[a];p[a]=p[b];p[b]=v;}
    return 1;
}
typedef struct {TsInsert insert;TsRouter router;} Loop;
static TsStereoFrame stage(void *context,int id,TsStereoFrame in)
{
    Loop *loop=context;
    return id==TS_ROUTER_INSERT?
        ts_insert_process(&loop->insert,in,loop->router.wet[id]*loop->router.gain):internal(id,in);
}
static void prepare(TsInsert *s,unsigned rate)
{
    ts_insert_init(s);ts_insert_prepare(s,rate);
    TsInsertControls c={1,1,0,0};ts_insert_set(s,&c);s->output_channels=8;
    ts_insert_capture_prepare(s,8,rate,32);
}
static void loop_return(TsInsert *s)
{
    TsStereoFrame x=external(s->send);
    float input[8]={.8f,-.8f,x.l,x.r,.5f,-.5f,.4f,-.4f};
    ts_insert_capture(s,input,1,8);
}

static void all_orders(void)
{
    TsRouterControls c;ts_router_default(&c);c.bypass_mask=0;int count=0;
    do {
        Loop loop;prepare(&loop.insert,8000);ts_router_init(&loop.router);ts_router_prepare(&loop.router,8000);
        ts_router_set(&loop.router,&c);TsStereoFrame out={0},source={.06f,-.04f};
        for(int i=0;i<1800;++i) {
            loop_return(&loop.insert);
            out=ts_router_process(&loop.router,source,stage,&loop);
            assert(isfinite(out.l+out.r));
            float physical[8];ts_insert_write_output(&loop.insert,physical,8,out);
            close_frame((TsStereoFrame){physical[0],physical[1]},out);
            close_frame((TsStereoFrame){physical[2],physical[3]},loop.insert.send);
            for(int j=4;j<8;++j)assert(physical[j]==0);
        }
        TsStereoFrame expected=source;
        for(int i=0;i<TS_ROUTER_COUNT;++i)expected=c.order[i]==TS_ROUTER_INSERT?external(expected):internal(c.order[i],expected);
        close_frame(out,expected);assert(loop.insert.send_peak>0 && loop.insert.return_peak>0);
        /* Bypass restores the serial internal path without a return dependency. */
        ts_router_toggle_bypass(&c,TS_ROUTER_INSERT);ts_router_set(&loop.router,&c);
        for(int i=0;i<200;++i)out=ts_router_process(&loop.router,source,stage,&loop);
        expected=source;for(int i=0;i<TS_ROUTER_COUNT;++i)if(c.order[i]!=TS_ROUTER_INSERT)expected=internal(c.order[i],expected);
        close_frame(out,expected);close_frame(loop.insert.send,(TsStereoFrame){0,0});
        ts_router_toggle_bypass(&c,TS_ROUTER_INSERT);++count;
    }while(next_order(c.order));
    assert(count==120);puts("120 serial orders: external replacement, stereo SEND, bypass and activity passed");
}

static void levels_failures_and_solo(void)
{
    Loop loop;prepare(&loop.insert,8000);ts_router_init(&loop.router);ts_router_prepare(&loop.router,8000);
    TsRouterControls c=loop.router.controls;c.solo=TS_ROUTER_INSERT+1;ts_router_set(&loop.router,&c);
    TsStereoFrame source={.2f,-.1f},out={0};
    for(int i=0;i<1800;++i){loop_return(&loop.insert);out=ts_router_process(&loop.router,source,stage,&loop);}
    close_frame(out,external(source));assert(c.bypass_mask==(1u<<TS_ROUTER_INSERT));
    TsInsertControls levels=loop.insert.controls;levels.send_db=-6;levels.return_db=6;ts_insert_set(&loop.insert,&levels);
    for(int i=0;i<3000;++i){loop_return(&loop.insert);out=ts_router_process(&loop.router,source,stage,&loop);}
    close_frame(out,external(source));close_frame(loop.insert.send,(TsStereoFrame){source.l*powf(10,-.3f),source.r*powf(10,-.3f)});
    /* Silent / disconnected returns do not restore dry. */
    for(int i=0;i<2000;++i)out=ts_router_process(&loop.router,source,stage,&loop);
    close_frame(out,(TsStereoFrame){0,0});
    ts_insert_capture_prepare(&loop.insert,0,8000,32);
    for(int i=0;i<300;++i)out=ts_router_process(&loop.router,source,stage,&loop);
    close_frame(out,(TsStereoFrame){0,0});
    ts_insert_capture_prepare(&loop.insert,8,8000,32);
    for(int i=0;i<1800;++i){loop_return(&loop.insert);out=ts_router_process(&loop.router,source,stage,&loop);}
    close_frame(out,external(source));
    loop.insert.output_channels=2;
    for(int i=0;i<300;++i){loop_return(&loop.insert);out=ts_router_process(&loop.router,source,stage,&loop);}
    close_frame(out,(TsStereoFrame){0,0});close_frame(loop.insert.send,(TsStereoFrame){0,0});
    loop.insert.output_channels=8;levels.return_pair=3;ts_insert_set(&loop.insert,&levels);
    ts_insert_capture_prepare(&loop.insert,4,8000,32);assert(!ts_insert_return_available(&loop.insert));
    for(int i=0;i<300;++i)out=ts_router_process(&loop.router,source,stage,&loop);
    close_frame(out,(TsStereoFrame){0,0});
    levels.return_pair=1;levels.send_db=levels.return_db=12;ts_insert_set(&loop.insert,&levels);
    ts_insert_capture_prepare(&loop.insert,8,8000,32);
    for(int i=0;i<3000;++i) {
        float bad[8]={0,0,i%3?20:NAN,i%5?-10:INFINITY,0,0,0,0};
        ts_insert_capture(&loop.insert,bad,1,8);
        if(i%137==0){ts_router_move(&c,i%5,(i/137)%5);ts_router_toggle_solo(&c,TS_ROUTER_INSERT);ts_router_set(&loop.router,&c);}
        out=ts_router_process(&loop.router,source,stage,&loop);
        assert(isfinite(out.l+out.r) && fabsf(out.l)<=1 && fabsf(out.r)<=1);
        assert(isfinite(loop.insert.send.l+loop.insert.send.r) && fabsf(loop.insert.send.l)<=1 && fabsf(loop.insert.send.r)<=1);
    }
}

static void isolation_and_persistence(void)
{
    TsInsert insert;prepare(&insert,8000);
    float input[4]={.1f,-.2f,1,-1};
    close_frame(ts_insert_external_frame(&insert,input,4,TS_INPUT_CHANNEL_STEREO),(TsStereoFrame){.1f,-.2f});
    close_frame(ts_insert_external_frame(&insert,input,4,TS_INPUT_CHANNEL_MIX),(TsStereoFrame){-.05f,-.05f});
    TsInsertControls c={1,0,0,0};ts_insert_set(&insert,&c);
    close_frame(ts_insert_external_frame(&insert,input,4,TS_INPUT_CHANNEL_LEFT),(TsStereoFrame){0,0});
    close_frame(ts_insert_external_frame(&insert,input,4,TS_INPUT_CHANNEL_STEREO),(TsStereoFrame){1,-1});
    TsConfig config,loaded;ts_config_init(&config);config.insert=(TsInsertControls){3,2,-4.5f,2.25f};
    ts_router_move(&config.router,4,1);config.router.bypass_mask=0;config.router.solo=5;
    char error[160];assert(ts_config_save(&config,"test-insert.ini",error,sizeof(error)));
    assert(ts_config_load(&loaded,"test-insert.ini",error,sizeof(error)));
    assert(!memcmp(&loaded.insert,&config.insert,sizeof(config.insert)) && !memcmp(&loaded.router,&config.router,sizeof(config.router)));
    TsSisterProjectState state,read;ts_sister_project_state_init(&state,8000);state.insert=config.insert;state.router=config.router;
    assert(ts_sister_project_state_save_file(&state,"test-insert-project.ini",error,sizeof(error)));int present;
    assert(ts_sister_project_state_load_file(&read,"test-insert-project.ini",8000,&present,error,sizeof(error)));
    assert(!memcmp(&read.insert,&state.insert,sizeof(state.insert)) && !memcmp(&read.router,&state.router,sizeof(state.router)));
    FILE *f=fopen("test-insert.ini","wb");assert(f);fputs("Router.Order=3,2,1,0\nRouter.Bypass=0\nRouter.Solo=2\n",f);fclose(f);
    assert(ts_config_load(&loaded,"test-insert.ini",error,sizeof(error)));
    assert(loaded.router.order[4]==TS_ROUTER_INSERT && loaded.router.bypass_mask==16 && loaded.router.solo==2 && !loaded.insert.send_pair);
    f=fopen("test-insert-project.ini","wb");assert(f);
    fputs("TapeSister Sister Project State\nVersion=23\nPageCount=1\nActivePage=0\nRouter.Bypass=0\nRouter.Order=3,2,1,0\n",f);fclose(f);
    assert(ts_sister_project_state_load_file(&read,"test-insert-project.ini",8000,&present,error,sizeof(error)));
    assert(read.router.order[4]==TS_ROUTER_INSERT && read.router.bypass_mask==16 && !read.insert.send_pair);
    assert(ts_insert_read(&c,"Insert.SendPair","-1")==-1);
    assert(ts_insert_read(&c,"Insert.ReturnPair","0.5")==-1);
    assert(ts_insert_read(&c,"Insert.SendDb","nan")==-1);
    assert(ts_insert_read(&c,"Insert.ReturnDb","1000000000")==-1);
    remove("test-insert.ini");remove("test-insert-project.ini");
}

static void sister_serial_monitor(void)
{
    for(int position=0;position<TS_ROUTER_COUNT;++position) {
        TsSisterRuntime r;ts_sister_runtime_init(&r);
        assert(ts_sister_runtime_enable(&r,8000,2,2,1,NULL,0));
        TsSisterParameters p=r.parameters;p.monitor_dry=1;p.monitor_wet=0;p.prism.enabled=0;p.fx.enabled=0;
        ts_sister_runtime_set_parameters(&r,&p);ts_sister_runtime_set_sources(&r,TS_SISTER_SOURCE_PREVIEW);ts_sister_runtime_set_monitor(&r,1);
        TsInsertControls ports={1,1,0,0};ts_sister_runtime_set_insert(&r,&ports);r.insert.output_channels=8;
        ts_insert_capture_prepare(&r.insert,8,8000,32);
        TsRouterControls c=r.router.controls;c.bypass_mask=0;ts_router_move(&c,4,position);ts_sister_runtime_set_router(&r,&c);
        TsSisterSourceFrames sources={0};sources.preview=(TsStereoFrame){.12f,-.08f};TsSisterRuntimeFrame frame={0};
        ts_sister_runtime_begin_audio_block(&r);
        for(int i=0;i<3000;++i)frame=ts_sister_runtime_process_frame(&r,&sources);
        close_frame(frame.monitor_return,(TsStereoFrame){0,0}); /* DRY cannot leak around missing RETURN. */
        for(int i=0;i<4000;++i){loop_return(&r.insert);frame=ts_sister_runtime_process_frame(&r,&sources);}
        assert(fabsf(frame.monitor_return.l)+fabsf(frame.monitor_return.r)>.01f);
        assert(frame.monitor_return.l>0 && frame.monitor_return.r>0); /* swapped/inverted external stereo */
        ts_sister_runtime_end_audio_block(&r);
        TsSisterRoutingSnapshot snapshot;assert(ts_sister_runtime_get_snapshot(&r,&snapshot));
        assert(snapshot.insert_send_peak>.01f && snapshot.insert_return_peak>.01f);
        assert(ts_capture_arm_channels(&r.capture,1,128,8000,2,NULL,0));
        assert(ts_capture_set_source(&r.capture,TS_CAPTURE_SOURCE_SISTER,NULL,0));
        assert(ts_capture_trigger(&r.capture,NULL,0));
        for(int i=0;i<64;++i) {
            loop_return(&r.insert);frame=ts_sister_runtime_process_frame(&r,&sources);
            close_frame((TsStereoFrame){r.capture.buffer[i*2],r.capture.buffer[i*2+1]},frame.tap[TS_SISTER_TAP_MIX]);
        }
        assert(r.capture.recorded_frames==64);
        ts_sister_runtime_free(&r);
    }
}

int main(void)
{
    all_orders();levels_failures_and_solo();isolation_and_persistence();sister_serial_monitor();
    puts("Insert I/O boundary, safety, monitor isolation and compatibility passed");return 0;
}
