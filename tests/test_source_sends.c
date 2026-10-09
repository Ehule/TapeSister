#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/source_route.h"
#include "tapesister/sister_runtime.h"
#include "tapesister/sister_project_state.h"
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define NEAR(a,b) assert(fabsf((a)-(b))<.00001f)
static void source_matrix(void)
{
    TsSourceRoute route={.mode=TS_SOURCE_PAIR,.second=1,.pan=100,.mix_enabled=1,
        .clean_level=25,.send_level={100,50,25}};
    TsSourceRouteVoice v={0};TsSourceRouteMix mix={0};TsStereoFrame in={.2f,.4f};
    ts_source_route_set(&v,route,48000);
    TsStereoFrame main=ts_source_route_frame(&v,in,&mix);
    NEAR(main.l,0);NEAR(main.r,0);NEAR(mix.speaker[0],0);NEAR(mix.speaker[1],.1f);
    for(int bus=0;bus<3;++bus) {NEAR(mix.send[bus].l,in.l*route.send_level[bus]*.01f);NEAR(mix.send[bus].r,in.r*route.send_level[bus]*.01f);}
    /* A send-only source survives summing and removal fades with no speaker mask. */
    route.clean_level=0;memset(&v,0,sizeof(v));memset(&mix,0,sizeof(mix));ts_source_route_set(&v,route,48000);
    ts_source_route_frame(&v,in,&mix);assert(!mix.mask && mix.send_mask==7);
    TsSourceRouteMix combined={0};ts_source_route_add(&combined,&mix,.5f);NEAR(combined.send[0].l,.1f);
    TsSourceRouteHandoff handoff={0};ts_source_route_handoff(&handoff,&mix,0,240);
    combined=(TsSourceRouteMix){0};ts_source_route_handoff(&handoff,&combined,1,240);NEAR(combined.send[0].l,.2f);
    for(int i=0;i<241;++i){combined=(TsSourceRouteMix){0};ts_source_route_handoff(&handoff,&combined,0,240);}
    NEAR(combined.send[0].l,0);assert(!handoff.last.send_mask);
    memset(route.send_level,0,sizeof(route.send_level));ts_source_route_set(&v,route,48000);
    for(int i=0;i<300;++i){mix=(TsSourceRouteMix){0};main=ts_source_route_frame(&v,in,&mix);}
    NEAR(main.l,0);assert(!mix.mask && !mix.send_mask); /* All faders down is silence, never Main. */
    TsSourceRoute track={.mode=TS_SOURCE_INHERIT};assert(ts_source_route_resolve(route,track).mix_enabled);
    track=(TsSourceRoute){0};assert(!ts_source_route_resolve(route,track).mix_enabled);
}
static void source_storage(void)
{
    TsInstrument *source=malloc(sizeof(*source)),*loaded=malloc(sizeof(*loaded));assert(source&&loaded);
    ts_instrument_init(source);ts_instrument_init(loaded);char error[256];
    assert(ts_instrument_activate_silence(source,512,48000,error,sizeof(error)));
    TsSourceRoute route={.mode=TS_SOURCE_PAIR,.second=1,.mix_enabled=1,.clean_level=17,.send_level={34,67,100}};
    source->bank[0].output_route=route;
    assert(ts_instrument_save_recipe(source,"send-tile.tsr",error,sizeof(error)));
    assert(ts_instrument_load_recipe(loaded,"send-tile.tsr",error,sizeof(error)));
    assert(!memcmp(&loaded->bank[0].output_route,&route,sizeof(route)));
    /* TSR34 has the same bank layout except the five new 32-bit mix fields. */
    FILE *in=fopen("send-tile.tsr","rb"),*out=fopen("send-legacy34.tsr","wb");assert(in&&out);
    unsigned char prefix[112];assert(fread(prefix,1,112,in)==112);prefix[4]='4';
    assert(fwrite(prefix,1,92,out)==92);int ch;
    while((ch=fgetc(in))!=EOF)assert(fputc(ch,out)!=EOF);
    assert(!fclose(in)&&!fclose(out));
    assert(ts_instrument_load_recipe(loaded,"send-legacy34.tsr",error,sizeof(error)));
    assert(loaded->bank[0].output_route.mode==TS_SOURCE_PAIR && !loaded->bank[0].output_route.mix_enabled);
    for(int bus=0;bus<3;++bus)assert(!loaded->bank[0].output_route.send_level[bus]);
    ts_instrument_free(source);ts_instrument_free(loaded);free(source);free(loaded);
    remove("send-tile.tsr");remove("send-legacy34.tsr");
}
static void wet_only_board(void)
{
    TsSisterPostFxEngine *fx=calloc(1,sizeof(*fx));assert(fx && ts_sister_post_fx_init(fx,48000));
    TsSisterFxControls c;ts_sister_fx_controls_default(&c);c.enabled=1;
    for(int i=0;i<4;++i)c.slot[i]=(TsSisterFxSlotControls){0};
    ts_sister_post_fx_sync_controls(fx,&c);
    for(int i=0;i<1000;++i){TsStereoFrame f=ts_sister_post_fx_process_send(fx,(TsStereoFrame){.3f,-.2f});NEAR(f.l,0);NEAR(f.r,0);}
    c.slot[0]=(TsSisterFxSlotControls){.type=TS_SISTER_FX_DELAY,.enabled=1,
        .placement=TS_SISTER_FX_PLACE_POST,.mix=.3f,.gain_db=6,.parameter_a=0,.parameter_b=.2f};
    ts_sister_post_fx_sync_controls(fx,&c);
    double energy=0;
    for(int i=0;i<4800;++i) {
        TsStereoFrame f=ts_sister_post_fx_process_send(fx,(TsStereoFrame){i==0?.3f:0,i==0?-.2f:0});
        if(i<20){NEAR(f.l,0);NEAR(f.r,0);} /* No zero-latency dry copy at 30% MIX. */
        energy+=fabsf(f.l)+fabsf(f.r);
    }
    assert(energy>.01);
    c.enabled=0;ts_sister_post_fx_sync_controls(fx,&c);
    TsStereoFrame f=ts_sister_post_fx_process_send(fx,(TsStereoFrame){.4f,.2f});NEAR(f.l,0);NEAR(f.r,0);
    ts_sister_post_fx_free(fx);free(fx);
}
static TsStereoFrame send_frame(TsSisterRuntime *r,TsStereoFrame source)
{
    TsSisterSourceFrames input={0};input.send[0]=source;
    ts_sister_runtime_process_frame(r,&input);
    return ts_sister_runtime_process_ordinary_post_fx(r,(TsStereoFrame){.07f,-.11f});
}
static void shared_runtime_and_storage(void)
{
    TsSisterRuntime *r=calloc(1,sizeof(*r));assert(r);ts_sister_runtime_init(r);char error[256];
    assert(ts_sister_runtime_reconfigure(r,48000,2,error,sizeof(error)));
    TsSisterParameters p=r->parameters;p.prism.enabled=1;p.prism.lenses=2;p.prism.mix=.25f;p.prism.body=0;p.prism.stereo=0;p.prism.color=0;p.prism.focus=1;
    p.prism.morph_enabled=0;p.prism.group_octave=0;p.prism.output_db=0;p.prism.spread=p.prism.drift=0;
    ts_sister_runtime_set_parameters(r,&p);
    size_t memory=ts_sister_post_fx_memory_bytes(&r->post_fx);void *history=r->prism.history;
    TsSourceRoute destination={.mode=TS_SOURCE_PAIR,.speaker=2,.second=3};
    ts_router_set_return(&r->router,0,1,destination,100);
    TsStereoFrame main={0};uint64_t clock=r->prism.clock;
    for(int i=0;i<24000;++i)main=send_frame(r,(TsStereoFrame){.2f,.4f});
    assert(r->prism.clock-clock==24000 && history==r->prism.history);
    assert(ts_sister_post_fx_memory_bytes(&r->post_fx)==memory);
    NEAR(main.l,.07f);NEAR(main.r,-.11f); /* Main never enters the Prism send. */
    NEAR(r->effect_returns.speaker[2],.05f*sqrtf(2));NEAR(r->effect_returns.speaker[3],.1f*sqrtf(2));
    NEAR(r->effect_returns.speaker[0],0);assert(r->effect_returns.mask==12);
    destination.pan=-100;ts_router_set_return(&r->router,0,1,destination,50);
    for(int i=0;i<1000;++i)send_frame(r,(TsStereoFrame){.2f,.4f});
    NEAR(r->effect_returns.speaker[2],.025f*sqrtf(2));NEAR(r->effect_returns.speaker[3],0);
    ts_router_manual(&r->router,TS_ROUTER_PRISM,0);
    for(int i=0;i<1000;++i)main=send_frame(r,(TsStereoFrame){.2f,.4f});
    NEAR(main.l,.07f);NEAR(r->effect_returns.monitor.l,0);NEAR(r->effect_returns.monitor.r,0);
    ts_router_manual(&r->router,TS_ROUTER_PRISM,0);p.prism.enabled=0;ts_sister_runtime_set_parameters(r,&p);
    for(int i=0;i<16000;++i)send_frame(r,(TsStereoFrame){.2f,.4f});
    NEAR(r->effect_returns.monitor.l,0);NEAR(r->effect_returns.monitor.r,0);
    /* Fallout has its own wet return, with Master FX and MIX still respected. */
    p.fx.enabled=1;p.fx.fallout.enabled=1;p.fx.fallout.mix=.5f;
    p.fx.fallout.drop_enabled=p.fx.fallout.pan_enabled=p.fx.fallout.skip_enabled=0;
    p.fx.fallout.bit_enabled=p.fx.fallout.pitch_enabled=0;p.fx.fallout.noise=0;
    ts_sister_runtime_set_parameters(r,&p);
    ts_router_set_return(&r->router,TS_SEND_FALLOUT,1,(TsSourceRoute){.mode=TS_SOURCE_SPEAKER,.speaker=3},100);
    for(int i=0;i<12000;++i) {
        TsSisterSourceFrames source={0};source.send[2]=(TsStereoFrame){.2f,.4f};
        ts_sister_runtime_process_frame(r,&source);
        main=ts_sister_runtime_process_ordinary_post_fx(r,(TsStereoFrame){.07f,-.11f});
    }
    NEAR(main.l,.07f);NEAR(main.r,-.11f);assert(r->effect_returns.speaker[3]>.01f);
    NEAR(r->effect_returns.speaker[2],0);NEAR(r->effect_returns.speaker[0],0);
    p.fx.fallout.mix=0;ts_sister_runtime_set_parameters(r,&p);
    for(int i=0;i<12000;++i) {
        TsSisterSourceFrames source={0};source.send[2]=(TsStereoFrame){.2f,.4f};
        ts_sister_runtime_process_frame(r,&source);ts_sister_runtime_process_ordinary_post_fx(r,(TsStereoFrame){0});
    }
    NEAR(r->effect_returns.monitor.l,0);NEAR(r->effect_returns.monitor.r,0);
    ts_sister_runtime_process_frame(r,NULL);
    for(int bus=0;bus<3;++bus){NEAR(r->send_input[bus].l,0);NEAR(r->send_input[bus].r,0);}
    /* All topology, placement and level fields survive project persistence. */
    TsSisterProjectState *state=malloc(sizeof(*state)),*loaded=malloc(sizeof(*loaded));assert(state&&loaded);
    ts_sister_project_state_capture(state,r,1,NULL);
    assert(ts_sister_project_state_save_file(state,"shared-send-state.ini",error,sizeof(error)));
    int present=0;assert(ts_sister_project_state_load_file(loaded,"shared-send-state.ini",48000,&present,error,sizeof(error))&&present);
    assert(!memcmp(&state->router,&loaded->router,sizeof(state->router)));
    TsRouterControls old;ts_router_default(&old);assert(ts_router_read(&old,"Router.Order","0,1,2,3")==1 && old.send_mask==0);
    assert(ts_router_read(&old,"Router.Return0","1,0,0,0,0,100")==-1);
    assert(ts_router_read(&old,"Router.SendMask","8")==-1);
    free(state);free(loaded);remove("shared-send-state.ini");ts_sister_runtime_free(r);free(r);
}
int main(void)
{
    source_matrix();source_storage();wet_only_board();shared_runtime_and_storage();
    puts("Shared sends: independent source levels, wet-only returns, one DSP history, bypass, placement and persistence passed");return 0;
}
