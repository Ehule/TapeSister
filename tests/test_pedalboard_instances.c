#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/matrix_effects.h"
#include "tapesister/sister_project_state.h"
#include "tapesister/profile.h"
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static TsMatrixControls controls, loaded;
static TsMatrixEffects engines;
static void codec(void)
{
    ts_matrix_default(&controls);
    int a=ts_matrix_effect_add(&controls,TS_MATRIX_FX_PEDALBOARD);
    int b=ts_matrix_effect_add(&controls,TS_MATRIX_FX_PEDALBOARD);
    assert(a==0 && b==1);
    controls.effect[a].board.count=20;
    for(int i=0;i<20;++i) {
        TsSisterFxSlotControls *s=&controls.effect[a].board.slot[i];
        s->type=1+i%(TS_SISTER_FX_SUNVOX-1);s->parameter_a=.03f*i;
        s->gain_db=-6+.1f*i;s->parameter_locks=i%32;
    }
    controls.effect[a].board.slot[3].grain_states.seconds=12;
    ts_grain_state_capture(&controls.effect[a].board.slot[3],2);
    controls.effect[a].board.slot[4].transient_power=3;
    ts_grain_state_capture(&controls.effect[a].board.slot[4],4);
    controls.effect[b].board.slot[0].type=TS_SISTER_FX_DISTORTION;
    controls.effect[b].board.slot[0].parameter_a=.83f;
    FILE *f=tmpfile();assert(f && ts_matrix_write(f,&controls));rewind(f);
    ts_matrix_default(&loaded);char line[4096];
    while(fgets(line,sizeof(line),f)) {
        line[strcspn(line,"\r\n")]=0;char *eq=strchr(line,'=');assert(eq);*eq++=0;
        assert(ts_matrix_read(&loaded,line,eq)==1);
    }
    fclose(f);assert(ts_matrix_valid(&loaded));
    for(int i=0;i<20;++i) {
        assert(loaded.effect[a].board.slot[i].type==controls.effect[a].board.slot[i].type);
        assert(loaded.effect[a].board.slot[i].parameter_locks==controls.effect[a].board.slot[i].parameter_locks);
        assert(fabsf(loaded.effect[a].board.slot[i].parameter_a-controls.effect[a].board.slot[i].parameter_a)<1e-6);
    }
    assert(loaded.effect[a].board.slot[3].grain_states.captured==4);
    assert(loaded.effect[a].board.slot[4].transient_power==3);
    assert(loaded.effect[b].board.slot[0].parameter_a==.83f);
    /* Value snapshots do not alias another board or the original bank. */
    loaded.effect[a].board.slot[0].parameter_a=.99f;
    assert(controls.effect[a].board.slot[0].parameter_a==0);
    assert(loaded.effect[b].board.slot[0].parameter_a==.83f);
    TsPedalboardControls bad,before;ts_pedalboard_default(&bad);before=bad;
    assert(ts_pedalboard_read(&bad,"Header","1,21,0")==-1 && !memcmp(&bad,&before,sizeof(bad)));
    assert(ts_pedalboard_read(&bad,"Header","1,20,0")==1 && !ts_pedalboard_valid(&bad));
    before=bad;
    assert(ts_pedalboard_read(&bad,"Slot0","1,1,nan,0,0,0,1,0")==-1 && !memcmp(&bad,&before,sizeof(bad)));
    assert(ts_pedalboard_read(&bad,"Slot20","1,1,0,0,0,0,1,0")==-1);
    assert(ts_pedalboard_read(&bad,"End","1")==1 && ts_pedalboard_valid(&bad));
}
static void capacity_and_sleep(void)
{
    ts_matrix_default(&controls);ts_matrix_effects_init(&engines);
    for(int i=0;i<16;++i) {
        assert(ts_matrix_effect_add(&controls,TS_MATRIX_FX_PEDALBOARD)==i);
        controls.effect[i].board.count=20;
        for(int j=0;j<20;++j)controls.effect[i].board.slot[j].type=TS_SISTER_FX_TRANSIENT;
    }
    assert(ts_matrix_effect_add(&controls,TS_MATRIX_FX_PEDALBOARD)==-1);
    assert(ts_matrix_effects_restore(&engines,&controls,48000));
    /* 320 neutral processors occupy only 16 outer ports and no unused
       delay/reverb buffers. This is an allocation bound, not a CPU promise. */
    assert(ts_matrix_effects_memory(&engines)<4*1024*1024);
    for(int f=0;f<1000;++f)for(int i=0;i<16;++i) {
        TsStereoFrame in={.1f*sinf(f*.02f),.2f*cosf(f*.03f)};
        TsStereoFrame out=ts_matrix_effects_process(&engines,i,in,(TsStereoFrame){0},1);
        assert(fabsf(in.l-out.l)<1e-5f && fabsf(in.r-out.r)<1e-5f);
    }
    ts_matrix_effects_suspend(&engines,1u<<15);
    for(int f=0;f<300;++f)ts_matrix_effects_process(&engines,15,(TsStereoFrame){.1f,.2f},(TsStereoFrame){0},1);
    TsStereoFrame asleep=ts_matrix_effects_process(&engines,15,(TsStereoFrame){.1f,.2f},(TsStereoFrame){0},1);
    assert(asleep.l==0 && asleep.r==0);
    TsStereoFrame awake=ts_matrix_effects_process(&engines,0,(TsStereoFrame){.1f,.2f},(TsStereoFrame){0},1);
    assert(fabsf(awake.l-.1f)<1e-5f);
    ts_matrix_effects_free(&engines);
}
static void spans_and_instances(void)
{
    TsPedalboardControls c;ts_pedalboard_default(&c);c.count=20;
    c.slot[0].type=TS_SISTER_FX_DISTORTION;c.slot[0].parameter_a=.7f;
    c.slot[1].type=TS_SISTER_FX_DELAY;c.slot[1].parameter_a=.1f;c.slot[1].parameter_b=.4f;
    c.slot[19].type=TS_SISTER_FX_TRANSIENT;
    TsPedalboard *a=ts_pedalboard_new(&c,48000,1),*b=ts_pedalboard_new(&c,48000,1),*silent=ts_pedalboard_new(&c,48000,1);
    assert(a && b && silent);
    for(int block=0;block<100;++block) {
        TsStereoFrame in[64],out[64];
        for(int j=0;j<64;++j)in[j]=(TsStereoFrame){.3f*sinf((block*64+j)*.03f),.17f*cosf((block*64+j)*.02f)};
        if(block==40){c.slot[0].enabled=0;ts_pedalboard_set(a,&c);ts_pedalboard_set(b,&c);}
        ts_pedalboard_process_span(b,in,out,64);
        for(int j=0;j<64;++j) {
            TsStereoFrame expected=ts_pedalboard_process(a,in[j]);
            assert(fabsf(out[j].l-expected.l)<1e-6f && fabsf(out[j].r-expected.r)<1e-6f);
            TsStereoFrame zero=ts_pedalboard_process(silent,(TsStereoFrame){0});assert(zero.l==0 && zero.r==0);
        }
    }
    c.slot[1].type=TS_SISTER_FX_REVERB;assert(!ts_pedalboard_topology_matches(a,&c));
    ts_pedalboard_free(a);ts_pedalboard_free(b);ts_pedalboard_free(silent);
}
static void matrix_span_aliasing(void)
{
    static TsMatrixEffects scalar,block,inplace;
    ts_matrix_default(&controls);
    assert(ts_matrix_effect_add(&controls,TS_MATRIX_FX_PEDALBOARD)==0);
    TsMatrixEffectControls *c=&controls.effect[0];
    c->mix=.4f;c->gain_db=-3;c->board.count=5;
    c->board.slot[0].type=TS_SISTER_FX_DISTORTION;c->board.slot[0].parameter_a=.8f;
    c->board.slot[1].type=TS_SISTER_FX_DELAY;c->board.slot[1].parameter_a=.01f;
    TsPedalboard *b=ts_pedalboard_new(&c->board,48000,1);assert(b);
    c->board.count=20;
    assert(ts_pedalboard_topology_matches(b,&c->board));
    ts_pedalboard_set(b,&c->board);c->board.count=5;
    assert(ts_pedalboard_topology_matches(b,&c->board));ts_pedalboard_free(b);
    ts_matrix_effects_init(&scalar);ts_matrix_effects_init(&block);ts_matrix_effects_init(&inplace);
    assert(ts_matrix_effects_restore(&scalar,&controls,48000));
    assert(ts_matrix_effects_restore(&block,&controls,48000));
    assert(ts_matrix_effects_restore(&inplace,&controls,48000));
    for(int n=0;n<100;++n) {
        TsStereoFrame in[64],out[64],alias[64],key[64]={{0}};
        for(int j=0;j<64;++j)in[j]=alias[j]=(TsStereoFrame){.4f*sinf((n*64+j)*.03f),.1f};
        ts_matrix_effects_process_span(&block,0,in,key,out,64,1);
        ts_matrix_effects_process_span(&inplace,0,alias,key,alias,64,1);
        for(int j=0;j<64;++j) {
            TsStereoFrame expected=ts_matrix_effects_process(&scalar,0,in[j],key[j],1);
            assert(fabsf(out[j].l-expected.l)<1e-6f && fabsf(out[j].r-expected.r)<1e-6f);
            assert(fabsf(alias[j].l-expected.l)<1e-6f && fabsf(alias[j].r-expected.r)<1e-6f);
        }
    }
    ts_matrix_effects_free(&scalar);ts_matrix_effects_free(&block);ts_matrix_effects_free(&inplace);
}

static TsSunVoxControls imported(void)
{
    FILE *f=fopen("board-test.sunsynth","wb");assert(f);
    const unsigned char bytes[]={ 'S','S','Y','N',0,0,0,0,'S','T','Y','P',10,0,0,0,'A','m','p','l','i','f','i','e','r',0,'S','E','N','D',0,0,0,0 };
    assert(fwrite(bytes,1,sizeof(bytes),f)==sizeof(bytes));fclose(f);
    TsSunVoxControls c;assert(ts_sunvox_import(&c,"board-test.sunsynth"));remove("board-test.sunsynth");return c;
}
static void nested_sunvox(void)
{
    TsPedalboardControls c;ts_pedalboard_default(&c);c.count=20;
    for(int i=0;i<20;++i){c.slot[i].type=TS_SISTER_FX_SUNVOX;c.slot[i].sunvox=imported();c.slot[i].sunvox.quantum=i%3==0?64:i%3==1?256:1024;c.slot[i].mix=0;}
    TsPedalboard *a=ts_pedalboard_new(&c,48000,1),*b=ts_pedalboard_new(&c,48000,1);assert(a && b);
    unsigned delay=0;for(int i=0;i<20;++i)delay+=ts_sunvox_quantum(&c.slot[i].sunvox);
    assert(ts_pedalboard_latency(a)==delay);
    for(unsigned n=0;n<delay+1024;n+=64) {
        TsStereoFrame input[64],out[64],dry[64];
        for(unsigned j=0;j<64;++j)input[j]=(TsStereoFrame){sinf((n+j)*.071f)*.2f,cosf((n+j)*.037f)*.1f};
        ts_pedalboard_process_aligned(a,input,out,dry,64);
        for(unsigned j=0;j<64;++j){TsStereoFrame scalar=ts_pedalboard_process(b,input[j]);
            float expected=n+j>=delay?sinf((n+j-delay)*.071f)*.2f:0;
            assert(fabsf(out[j].l-expected)<1e-6f && fabsf(dry[j].l-expected)<1e-6f);
            assert(fabsf(out[j].l-scalar.l)<1e-6f && fabsf(out[j].r-scalar.r)<1e-6f);
        }
    }
    c.slot[0].sunvox.quantum=1024;TsPedalboardPlan edit;
    assert(ts_pedalboard_stage(a,&c,&edit));ts_pedalboard_commit(a,&c,&edit);ts_pedalboard_discard(&edit);
    for(int n=0;n<3000;++n){TsStereoFrame in={sinf(n*.05f)*.2f,.1f},out,dry;ts_pedalboard_process_aligned(a,&in,&out,&dry,1);assert(fabsf(out.l-dry.l)<1e-6f && fabsf(out.r-dry.r)<1e-6f);}
    TsPedalboardPlan garbage;ts_pedalboard_collect(a,&garbage,0);ts_pedalboard_discard(&garbage);
    TsPedalboardControls invalid=c;invalid.slot[0].sunvox.ready=0;assert(!ts_pedalboard_stage(a,&invalid,&edit));assert(ts_pedalboard_topology_matches(a,&c));
    ts_pedalboard_free(a);ts_pedalboard_free(b);
    c.slot[0].sunvox.quantum=64;
    /* Embedded assets survive deleting the source; project codec includes P1
       as well as nested instances, including independent quantum/parameters. */
    static TsSisterProjectState project,restored;ts_sister_project_state_init(&project,48000);
    project.parameters.fx.slot[0]=c.slot[0];project.parameters.fx.slot[0].sunvox.value[0]=12345;
    int at=ts_matrix_effect_add(&project.matrix,TS_MATRIX_FX_PEDALBOARD);assert(at>=0);project.matrix.effect[at].board=c;
    char error[256];assert(ts_sister_project_state_save_file(&project,"board-test.state",error,sizeof(error)));
    FILE *f=fopen("board-test.state","rb");assert(f);fseek(f,0,SEEK_END);assert(ftell(f)>10000);fclose(f);
    int present=0;assert(ts_sister_project_state_load_file(&restored,"board-test.state",48000,&present,error,sizeof(error)) && present);remove("board-test.state");
    assert(restored.parameters.fx.slot[0].type==TS_SISTER_FX_SUNVOX);
    assert(restored.parameters.fx.slot[0].sunvox.patch==c.slot[0].sunvox.patch && restored.parameters.fx.slot[0].sunvox.value[0]==12345);
    for(int i=0;i<20;++i){assert(restored.matrix.effect[at].board.slot[i].id==c.slot[i].id);assert(ts_sunvox_quantum(&restored.matrix.effect[at].board.slot[i].sunvox)==ts_sunvox_quantum(&c.slot[i].sunvox));}
    static TsSisterPostFxEngine p1;assert(ts_sister_post_fx_init(&p1,48000));
    TsSisterFxControls fx;ts_sister_fx_controls_default(&fx);fx.enabled=1;
    for(int i=0;i<TS_SISTER_FX_SLOT_COUNT;++i){uint32_t id=fx.slot[i].id;fx.slot[i]=ts_sister_fx_empty_slot();fx.slot[i].id=id;}
    fx.slot[0]=c.slot[0];fx.slot[1]=c.slot[1];assert(ts_sister_post_fx_sync_controls(&p1,&fx));
    delay=320;
    for(unsigned n=0;n<1024;++n){TsStereoFrame in={sinf(n*.071f)*.2f,.1f};TsStereoFrame out=ts_sister_post_fx_process_matrix(&p1,in,1);
        float expected=n>=delay?sinf((n-delay)*.071f)*.2f:0;assert(fabsf(out.l-expected)<1e-6f);}
    fx.slot[0].sunvox.quantum=1024;TsSisterSunVoxPlan plan;
    assert(ts_sister_sunvox_stage(&p1,&fx,&plan));ts_sister_sunvox_commit(&p1,&plan);ts_sister_post_fx_set_controls(&p1,&fx);
    for(int n=0;n<3000;++n){TsStereoFrame out=ts_sister_post_fx_process_matrix(&p1,(TsStereoFrame){sinf(n*.031f)*.2f,.1f},0);assert(fabsf(out.l)<1e-6f && fabsf(out.r)<1e-6f);}
    ts_sister_post_fx_free(&p1);
}
static void retained_children(void)
{
    TsPedalboardControls c;ts_pedalboard_default(&c);c.slot[0].type=TS_SISTER_FX_DELAY;c.slot[0].parameter_a=.01f;c.slot[0].parameter_b=.8f;c.slot[0].mix=1;
    c.slot[1].type=TS_SISTER_FX_TRANSIENT;c.slot[1].parameter_a=c.slot[1].parameter_b=c.slot[1].parameter_c=.5f;
    TsPedalboard *a=ts_pedalboard_new(&c,48000,1),*b=ts_pedalboard_new(&c,48000,1);assert(a && b);
    double tail=0;
    for(int n=0;n<12000;++n) {
        if(n==4096){c.slot[1].type=TS_SISTER_FX_DISPERSION;c.slot[1].mix=0;
            TsPedalboardPlan plan;assert(ts_pedalboard_stage(a,&c,&plan));ts_pedalboard_commit(a,&c,&plan);ts_pedalboard_discard(&plan);}
        if(n==6000){TsPedalboardPlan garbage;ts_pedalboard_collect(a,&garbage,0);ts_pedalboard_discard(&garbage);
            TsSisterFxSlotControls swap=c.slot[0];c.slot[0]=c.slot[4];c.slot[4]=swap;
            TsPedalboardPlan plan;assert(ts_pedalboard_stage(a,&c,&plan));ts_pedalboard_commit(a,&c,&plan);ts_pedalboard_discard(&plan);}
        TsStereoFrame in=n<3000?(TsStereoFrame){.1f*sinf(n*.05f),.2f*cosf(n*.07f)}:(TsStereoFrame){0};
        TsStereoFrame x=ts_pedalboard_process(a,in),y=ts_pedalboard_process(b,in);
        assert(fabsf(x.l-y.l)<1e-6f && fabsf(x.r-y.r)<1e-6f);if(n>6000)tail+=fabsf(x.l);
    }
    assert(tail>.001);ts_pedalboard_free(a);ts_pedalboard_free(b);
}
static uint64_t clock_tick;
static uint64_t fake_clock(void){return ++clock_tick;}
static void child_profile(void)
{
    assert(ts_profile_init(fake_clock,1000000));ts_profile_enable(1);
    TsPedalboardControls c;ts_pedalboard_default(&c);c.slot[0].type=TS_SISTER_FX_TRANSIENT;
    TsPedalboard *b=ts_pedalboard_new(&c,48000,1);assert(b);ts_pedalboard_profile_owner(b,2,42);
    for(int n=0;n<2560;++n){ts_profile_lane_begin(1);ts_pedalboard_process(b,(TsStereoFrame){.1f,.2f});ts_profile_lane_end(1);}
    TsProfileSnapshot s;ts_profile_snapshot(&s);const TsProfileChildValue *v=&s.child[40];assert(v->board==42 && v->effect.identity.id==c.slot[0].id && v->effect.samples>0 && v->effect.blocks.ticks>0);
    ts_profile_reset();ts_profile_lane_begin(1);ts_profile_lane_end(1);ts_profile_snapshot(&s);assert(!s.child[40].effect.samples);
    ts_profile_enable(0);ts_pedalboard_free(b);
}
int main(void)
{codec();capacity_and_sleep();spans_and_instances();matrix_span_aliasing();nested_sunvox();retained_children();child_profile();puts("pedalboard instances: codecs, 320 children, isolation, sleep and span parity passed");return 0;}
