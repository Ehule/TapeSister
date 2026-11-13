#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/matrix_effects.h"
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
        s->type=1+i%(TS_SISTER_FX_TYPE_COUNT-1);s->parameter_a=.03f*i;
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
int main(void)
{codec();capacity_and_sleep();spans_and_instances();matrix_span_aliasing();puts("pedalboard instances: codecs, 320 children, isolation, sleep and span parity passed");return 0;}
