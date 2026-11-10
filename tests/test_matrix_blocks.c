#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/matrix_effects.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "reference_matrix_process.h"

typedef struct {TsRoutingMatrix graph;TsMatrixEffects bank;} Lane;
static Lane lanes[2];
static TsStereoFrame process(void *p,int node,TsStereoFrame in)
{
    Lane *l=p;int i=ts_matrix_effect_index(node);
    if(i<0)return in;
    return ts_matrix_effects_process(&l->bank,i,in,l->graph.sidechain,
        l->graph.voice[node].active_count || l->graph.controls.delayed[node]);
}
static void apply(TsMatrixControls *c)
{
    for(int i=0;i<2;++i) {
        TsMatrixEffectPlan plan;
        ts_matrix_effects_collect(&lanes[i].bank,&plan);ts_matrix_effects_discard(&plan);
        assert(ts_matrix_effects_stage(&lanes[i].bank,c,48000,&plan));
        ts_matrix_effects_commit(&lanes[i].bank,c,48000,&plan);ts_matrix_effects_discard(&plan);
        assert(ts_matrix_set(&lanes[i].graph,c,48000));
    }
}
int main(void)
{
    static const char patch[]="SSYN\0\0\0\0STYP\x0a\0\0\0Amplifier\0SEND\0\0\0\0";
    FILE *f=fopen("blocks.sunsynth","wb");assert(f);
    assert(fwrite(patch,1,sizeof(patch)-1,f)==sizeof(patch)-1);assert(!fclose(f));
    TsSunVoxControls sv;assert(ts_sunvox_import(&sv,"blocks.sunsynth"));remove("blocks.sunsynth");
    const int quantum[]={64,256,1024};
    for(int q=0;q<3;++q) {
        TsMatrixControls c;ts_matrix_default(&c);c.enabled=1;c.feedback=1;
        for(int n=0;n<TS_MATRIX_ROWS;++n)c.row[n]=(TsSourceRoute){.matrix_enabled=1};
        for(int i=0;i<4;++i) {
            assert(ts_matrix_effect_add(&c,i==3?TS_MATRIX_FX_COMPRESSOR:TS_MATRIX_FX_SUNVOX)==i);
            if(i<3){c.effect[i].sunvox=sv;c.effect[i].sunvox.quantum=quantum[q];}
        }
        int b=TS_MATRIX_EFFECT_BASE;
        c.row[TS_MATRIX_MOSAIC].matrix_gain[b]=1000;
        c.row[TS_MATRIX_MOSAIC].matrix_gain[b+1]=700;
        c.row[b].matrix_gain[b+2]=800;c.row[b+1].matrix_gain[b+2]=600;
        c.row[b+2].matrix_gain[b+3]=1000;
        c.row[b].matrix_gain[ts_matrix_sidechain_for(b+3)]=1000;
        c.effect[3].compressor.external=1;c.effect[3].compressor.threshold_db=-20;
        c.row[b+3].matrix_gain[7]=1000;
        assert(ts_matrix_connect(&c,b+2,b,200)==1);
        for(int i=0;i<2;++i){ts_matrix_init(&lanes[i].graph);ts_matrix_effects_init(&lanes[i].bank);}
        apply(&c);double energy=0;
        for(int frame=0;frame<32768;++frame) {
            if(frame==4096){c.row[b+2].pan=-37;apply(&c);}
            if(frame==8192){c.effect[0].sunvox.quantum=quantum[(q+1)%3];apply(&c);}
            if(frame==16384){c.effect[1].enabled=0;apply(&c);}
            if(frame==24576){ts_matrix_effect_remove(&c,1);apply(&c);}
            TsStereoFrame live[TS_MATRIX_ROWS-TS_MATRIX_NODES]={{0}};
            if(frame<20000)live[TS_MATRIX_MOSAIC-TS_MATRIX_NODES]=(TsStereoFrame){.2f*sinf(frame*.13f),.1f*cosf(frame*.017f)};
            for(int i=0;i<2;++i) {
                TsSourceRouteMix mix={0};Lane *l=&lanes[i];ts_matrix_effects_tick(&l->bank);
                if(i)ts_matrix_effects_begin_frame(&l->bank);
                (i?ts_matrix_process:reference_process)(&l->graph,&mix,live,process,l);
                if(i)ts_matrix_effects_end_frame(&l->bank);
                assert(!l->bank.jobs_count);
            }
            assert(!memcmp(lanes[0].graph.output,lanes[1].graph.output,sizeof(lanes[0].graph.output)));
            assert(!memcmp(lanes[0].graph.history,lanes[1].graph.history,sizeof(lanes[0].graph.history)));
            assert(!memcmp(lanes[0].graph.voice,lanes[1].graph.voice,sizeof(lanes[0].graph.voice)));
            assert(!memcmp(lanes[0].graph.input_peak,lanes[1].graph.input_peak,sizeof(lanes[0].graph.input_peak)));
            energy+=fabsf(lanes[1].graph.output[0].l);
        }
        assert(energy>10);
        for(int i=0;i<2;++i)ts_matrix_effects_free(&lanes[i].bank);
    }
    puts("Compiled graph/buffer jobs: exact PCM, feedback, meters, sidechains, tails and lifecycle at all quanta");
}
