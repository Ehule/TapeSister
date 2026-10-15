#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/matrix_effects.h"
#include "tapesister/sister_runtime.h"
#include "tapesister/sister_project_state.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "tsr_legacy_fixture.h"

static TsRoutingMatrix graph;
static TsMatrixEffects bank;
static TsStereoFrame seen[TS_MATRIX_EFFECT_CAPACITY];
static unsigned calls[TS_MATRIX_EFFECT_CAPACITY];
static TsStereoFrame process(void *unused,int node,TsStereoFrame in)
{
    (void)unused;int i=ts_matrix_effect_index(node);if(i<0)return in;
    ++calls[i];seen[i]=graph.sidechain;
    return ts_matrix_effects_process(&bank,i,in,graph.sidechain,graph.voice[node].active_count!=0 || graph.controls.delayed[node]);
}
static TsStereoFrame frame(TsStereoFrame in)
{
    TsSourceRouteMix mix;ts_source_route_mix_init(&mix,NULL);
    TsStereoFrame live[TS_MATRIX_ROWS-TS_MATRIX_NODES]={0};live[0]=in;
    ts_matrix_effects_tick(&bank);ts_matrix_process(&graph,&mix,live,process,NULL);return graph.output[0];
}
static TsMatrixControls empty(void)
{
    TsMatrixControls c;ts_matrix_default(&c);c.enabled=1;
    for(int i=0;i<TS_MATRIX_ROWS;++i)c.row[i]=(TsSourceRoute){.matrix_enabled=1};
    return c;
}
static void install(const TsMatrixControls *c,unsigned rate)
{
    assert(ts_matrix_effects_restore(&bank,c,rate));
    ts_matrix_init(&graph);assert(ts_matrix_set(&graph,c,rate));memset(calls,0,sizeof(calls));
}
static void close_to(float a,float b,float tolerance){assert(fabsf(a-b)<tolerance);}
static void high_ports_and_neutral_stack(void)
{
    TsMatrixControls c=empty();
    for(int i=0;i<16;++i)assert(ts_matrix_effect_add(&c,TS_SISTER_FX_TRANSIENT)==i);
    assert(ts_matrix_effect_add(&c,TS_SISTER_FX_DELAY)==-1);
    assert(ts_matrix_connect(&c,7,17,1000)==1);
    for(int i=17;i<32;++i)assert(ts_matrix_connect(&c,i,i+1,1000)==1);
    assert(ts_matrix_connect(&c,32,7,1000)==1);install(&c,48000);
    assert(ts_matrix_effects_memory(&bank)<100000);
    for(int f=0;f<10000;++f){TsStereoFrame in={.2f*sinf(f*.01f),.3f*cosf(f*.013f)},out=frame(in);close_to(out.l,in.l,1e-5f);close_to(out.r,in.r,1e-5f);}
    for(int i=0;i<16;++i)assert(calls[i]==10000);
    /* Highest source coefficient (121) and bit (48) survive summing and fades. */
    TsSourceRoute r={.matrix_enabled=1};r.matrix_gain[48]=1000;
    TsSourceRouteVoice v={0};ts_source_route_set(&v,r,48000);
    TsSourceRouteMix sum={0},added={0};ts_source_route_frame(&v,(TsStereoFrame){.2f,.4f},&sum);
    assert(sum.graph_mask==(UINT64_C(1)<<48));ts_source_route_add(&added,&sum,.5f);
    close_to(added.graph[48].l,.1f,1e-6f);close_to(added.graph[48].r,.2f,1e-6f);
    r.matrix_gain[48]=0;ts_source_route_set(&v,r,48000);
    for(int i=0;i<240;++i){memset(&sum,0,sizeof(sum));ts_source_route_frame(&v,(TsStereoFrame){1,1},&sum);}
    assert(!v.active_count && !sum.graph_mask);
}
static void independent_keys(void)
{
    TsMatrixControls c=empty();
    for(int i=0;i<16;++i)assert(ts_matrix_effect_add(&c,i==0?TS_SISTER_FX_TRANSIENT:TS_MATRIX_FX_COMPRESSOR)==i);
    for(int i=1;i<16;++i){c.effect[i].compressor.external=1;c.effect[i].compressor.threshold_db=-24;c.effect[i].compressor.ratio=20;c.effect[i].compressor.attack_ms=.1f;}
    c.row[7].matrix_gain[17]=1000;c.row[7].matrix_gain[31]=1000;c.row[7].matrix_gain[32]=1000;
    /* Only the last compressor gets a key; the other must stay at unity. */
    c.row[17].matrix_gain[48]=1000;c.row[31].matrix_gain[8]=1000;c.row[32].matrix_gain[7]=1000;
    install(&c,48000);
    for(int i=0;i<10000;++i)frame((TsStereoFrame){.5f,.5f});
    close_to(seen[15].l,.5f,1e-6f);assert(seen[14].l==0);
    assert(ts_matrix_effects_reduction(&bank,15)>12 && ts_matrix_effects_reduction(&bank,14)<.01f);
    assert(graph.output[0].l<.12f);close_to(graph.output[1].l,.5f,1e-5f);
    assert(ts_matrix_connect(&c,32,33,1000)==0); /* Key edges participate in cycle detection. */
    c.feedback=1;assert(ts_matrix_connect(&c,32,33,1000)==1);
    assert(c.delayed[32]&(UINT64_C(1)<<33));assert(c.row[32].matrix_gain[33]==500);
    assert(ts_matrix_set(&graph,&c,48000));
    for(int i=0;i<5000;++i){TsStereoFrame out=frame((TsStereoFrame){.5f,.5f});assert(isfinite(out.l));}
    ts_matrix_effect_remove(&c,15);assert(!c.row[17].matrix_gain[48] && !c.row[7].matrix_gain[32] && !c.delayed[32]);
}
static void lifecycle(void)
{
    TsMatrixControls c=empty();assert(ts_matrix_effect_add(&c,TS_SISTER_FX_REVERB)==0);install(&c,48000);
    size_t bytes=ts_matrix_effects_memory(&bank);assert(bytes>100000 && bytes<4000000);
    for(int pass=0;pass<24;++pass) {
        uint32_t id=c.effect[0].id;ts_matrix_effect_remove(&c,0);TsMatrixEffectPlan p;
        assert(ts_matrix_effects_stage(&bank,&c,48000,&p));ts_matrix_effects_commit(&bank,&c,48000,&p);
        assert(ts_matrix_set(&graph,&c,48000));
        assert(ts_matrix_effect_add(&c,TS_SISTER_FX_REVERB)==0 && c.effect[0].id>id);
        assert(!ts_matrix_effects_stage(&bank,&c,48000,&p));
        for(int f=0;f<576;++f)frame((TsStereoFrame){0});
        ts_matrix_effects_collect(&bank,&p);ts_matrix_effects_discard(&p);assert(ts_matrix_effects_memory(&bank)==0);
        assert(ts_matrix_effects_stage(&bank,&c,48000,&p));ts_matrix_effects_commit(&bank,&c,48000,&p);
        assert(ts_matrix_set(&graph,&c,48000));assert(ts_matrix_effects_memory(&bank)==bytes);
        for(int f=0;f<576;++f)frame((TsStereoFrame){0});
    }
    TsMatrixEffectPlan p;assert(!ts_matrix_effects_stage(&bank,&c,0,&p));assert(ts_matrix_effects_memory(&bank)==bytes);
    /* Long fades advance by sample count without float accumulation stalling. */
    c=empty();ts_matrix_effect_add(&c,TS_MATRIX_FX_COMPRESSOR);c.effect[0].compressor.threshold_db=0;c.effect[0].compressor.output_db=6;
    c.effect[0].transition=1;install(&c,1000);
    for(int i=0;i<2000;++i)ts_matrix_effects_process(&bank,0,(TsStereoFrame){.1f,.1f},(TsStereoFrame){0},1);
    c.effect[0].enabled=0;assert(ts_matrix_effects_stage(&bank,&c,1000,&p));ts_matrix_effects_commit(&bank,&c,1000,&p);
    TsStereoFrame out={0};for(int i=0;i<1800000;++i)out=ts_matrix_effects_process(&bank,0,(TsStereoFrame){.1f,.1f},(TsStereoFrame){0},1);
    close_to(ts_matrix_effects_progress(&bank,0),.5f,1e-6f);close_to(out.l,.149763f,.0001f);
}
static void persistence_and_runtime(void)
{
    static TsSisterRuntime runtime;char error[256];ts_sister_runtime_init(&runtime);
    assert(ts_sister_runtime_reconfigure(&runtime,48000,2,error,sizeof(error)));
    TsSisterProjectState *state=malloc(sizeof(*state)),*read=malloc(sizeof(*read));assert(state && read);
    ts_sister_project_state_init(state,48000);state->matrix=empty();
    for(int i=0;i<16;++i)ts_matrix_effect_add(&state->matrix,1+i%6);
    state->matrix.effect[15].type=TS_MATRIX_FX_COMPRESSOR;
    state->matrix.effect[15].compressor.external=1;state->matrix.effect[15].a=.1234567f;
    state->matrix.row[7].matrix_gain[48]=1234;state->matrix.row[7].matrix_gain[21]=1000;
    state->matrix.row[21].matrix_gain[7]=1000; /* neutral transient */
    state->matrix_layout.row_count=1;state->matrix_layout.row_kind[0]=4;state->matrix_layout.row_id[0]=5;
    assert(ts_sister_project_state_save_file(state,"matrix-instances.ini",error,sizeof(error)));
    int present=0;assert(ts_sister_project_state_load_file(read,"matrix-instances.ini",48000,&present,error,sizeof(error)) && present);
    assert(!memcmp(&state->matrix,&read->matrix,sizeof(state->matrix)));
    assert(ts_sister_project_state_apply(read,&runtime,NULL));
    TsSisterSourceFrames in={0};in.matrix_input[0]=(TsStereoFrame){.2f,-.1f};
    for(int f=0;f<2000;++f)ts_sister_runtime_process_frame(&runtime,&in);
    close_to(runtime.matrix.output[0].l,.2f,1e-5f);close_to(runtime.matrix.output[0].r,-.1f,1e-5f);
    assert(ts_sister_runtime_reconfigure(&runtime,96000,2,error,sizeof(error)) && runtime.matrix_effects.rate==96000);
    read->matrix.effect[1].id=read->matrix.effect[0].id;assert(!ts_sister_project_state_apply(read,&runtime,NULL));
    assert(runtime.matrix.controls.effect[1].id!=runtime.matrix.controls.effect[0].id);
    TsInstrument *tile=calloc(1,sizeof(*tile)),*loaded=calloc(1,sizeof(*loaded));assert(tile && loaded);
    ts_instrument_init(tile);ts_instrument_init(loaded);
    assert(ts_instrument_activate_silence(tile,32,48000,error,sizeof(error)));
    tile->bank[0].output_route.matrix_enabled=1;tile->bank[0].output_route.matrix_gain[48]=1234;tile->bank[0].output_route.matrix_gain[16]=500;tile->bank[0].output_route.matrix_gain[65]=875;tile->bank[0].output_route.matrix_gain[49]=1000;
    assert(ts_instrument_save_recipe(tile,"matrix-instance-tile.tsr",error,sizeof(error)));
    assert(ts_instrument_load_recipe(loaded,"matrix-instance-tile.tsr",error,sizeof(error)));
    assert(loaded->bank[0].output_route.matrix_gain[48]==1234 && loaded->bank[0].output_route.matrix_gain[65]==875 && loaded->bank[0].output_route.matrix_gain[49]==1000);
    assert(test_write_legacy_single_tile("matrix-instance-tile.tsr","matrix-instance-old.tsr",40));
    assert(ts_instrument_load_recipe(loaded,"matrix-instance-old.tsr",error,sizeof(error)));
    assert(loaded->bank[0].output_route.matrix_gain[48]==1234 && loaded->bank[0].output_route.matrix_gain[65]==0 && loaded->bank[0].output_route.matrix_gain[49]==0);
    assert(test_write_legacy_single_tile("matrix-instance-tile.tsr","matrix-instance-old.tsr",39));
    assert(ts_instrument_load_recipe(loaded,"matrix-instance-old.tsr",error,sizeof(error)));
    assert(loaded->bank[0].output_route.matrix_gain[16]==500 && !loaded->bank[0].output_route.matrix_gain[48]);
    ts_instrument_free(tile);ts_instrument_free(loaded);free(tile);free(loaded);
    remove("matrix-instance-tile.tsr");remove("matrix-instance-old.tsr");
    remove("matrix-instances.ini");free(state);free(read);ts_sister_runtime_free(&runtime);
}
static void stress(int type,int parallel)
{
    TsMatrixControls c=empty();
    for(int i=0;i<16;++i){ts_matrix_effect_add(&c,type?type:1+i%6);c.effect[i].a=1;c.effect[i].b=1;c.effect[i].c=1;c.effect[i].mix=.7f;}
    for(int i=0;i<16;++i) {
        if(parallel)c.row[7].matrix_gain[17+i]=1000;
        else if(!i)c.row[7].matrix_gain[17]=1000;
        c.row[17+i].matrix_gain[parallel || i==15?7:18+i]=parallel?125:1000;
    }
    c.feedback=1;assert(ts_matrix_connect(&c,32,17,500)==1);install(&c,48000);
    const int frames=96000;clock_t started=clock();double energy=0;float peak=0;
    for(int i=0;i<frames;++i) {
        float x=i<1000?0:i%997==0?10:.1f*sinf(i*.071f);
        TsStereoFrame out=frame((TsStereoFrame){i==1234?NAN:x,i==1235?INFINITY:-x});
        assert(isfinite(out.l) && isfinite(out.r));/* The internal graph intentionally has headroom. Sixteen maximum
           transient boosts can exceed unity enormously; the native callback
           test checks the final hardware guard separately. */
        peak=fmaxf(peak,fmaxf(fabsf(out.l),fabsf(out.r)));assert(peak<1e30f);
        energy+=fabsf(out.l)+fabsf(out.r);
    }
    double seconds=(double)(clock()-started)/CLOCKS_PER_SEC;assert(energy>0);
    printf("%s %s: 16 instances, %.2f MiB, %.3f CPU seconds / 2 s audio (%.1f%% one core on this host), peak %.3g\n",
        type?ts_matrix_effect_name(type):"MIXED",parallel?"parallel":"chain",ts_matrix_effects_memory(&bank)/1048576.0,seconds,seconds*50,peak);
}
int main(void)
{
    ts_matrix_effects_init(&bank);ts_matrix_init(&graph);
    high_ports_and_neutral_stack();independent_keys();lifecycle();persistence_and_runtime();
    for(int type=1;type<TS_MATRIX_FX_TYPES;++type)stress(type,0);
    stress(TS_SISTER_FX_REVERB,1);stress(0,0);ts_matrix_effects_free(&bank);
    puts("Matrix instances: routing, independent keys, lifecycle, long fades, persistence, runtime and pathological DSP passed");return 0;
}
