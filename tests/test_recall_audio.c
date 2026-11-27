#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/matrix_effects.h"
#include "tapesister/mosaic.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static TsMatrixEffects fx;
static void render(unsigned count,float level,unsigned rate,int span)
{
    for(unsigned i=0;i<count;) {
        unsigned n=span?64:1;if(n>count-i)n=count-i;
        TsStereoFrame in[64],key[64]={{0}},out[64];
        for(unsigned j=0;j<n;++j){float x=level*sinf(6.2831853f*110*(i+j)/rate);in[j]=(TsStereoFrame){x,-x};}
        if(span)ts_matrix_effects_process_span(&fx,0,in,key,out,n,1);
        else out[0]=ts_matrix_effects_process(&fx,0,in[0],key[0],1);
        for(unsigned j=0;j<n;++j)assert(out[j].l==0 && out[j].r==0);
        i+=n;
    }
}
static void check(unsigned rate,int span)
{
    TsMatrixControls c;ts_matrix_default(&c);c.enabled=1;
    assert(ts_matrix_effect_add(&c,TS_MATRIX_FX_RECALL)==0);
    ts_matrix_effects_init(&fx);assert(ts_matrix_effects_restore(&fx,&c,rate));
    render(rate,0,rate,span);assert(ts_matrix_recall_take(&fx,0).hits==0);
    render(rate,.3f,rate,span);TsRecallReading a=ts_matrix_recall_take(&fx,0);
    assert(a.hits==1 && a.level>.29f);
    render(rate,.3f,rate,span);assert(ts_matrix_recall_take(&fx,0).hits==0);
    render(rate,0,rate,span);assert(ts_matrix_recall_take(&fx,0).hits==0);
    render(rate,.3f,rate,span);assert(ts_matrix_recall_take(&fx,0).hits==1);
    render(rate,0,rate,span);ts_matrix_recall_take(&fx,0);
    TsSourceRoute route={.matrix_enabled=1};route.matrix_gain[TS_MATRIX_EFFECT_BASE]=1000;
    ts_matrix_recall_note(&fx,&route);render(rate,.3f,rate,span);
    assert(ts_matrix_recall_take(&fx,0).hits==1);
    render(rate,0,rate,span);ts_matrix_recall_take(&fx,0);
    render(rate,.001f,rate,span);assert(ts_matrix_recall_take(&fx,0).hits==0);
    ts_matrix_effects_free(&fx);
}
static void mosaic_starts(void)
{
    TsMatrixControls c;ts_matrix_default(&c);c.enabled=1;
    assert(ts_matrix_effect_add(&c,TS_MATRIX_FX_RECALL)==0);
    ts_matrix_effects_init(&fx);assert(ts_matrix_effects_restore(&fx,&c,48000));
    TsMosaic *m=ts_mosaic_create();assert(m);
    TsSample s={.frames=4096,.sample_rate=48000,.channels=1};
    s.data=calloc(s.frames,sizeof(float));assert(s.data);strcpy(s.name,"slow entrance");
    char error[160];TsMosaicSource *source=ts_mosaic_source(m,&s,error,sizeof(error));assert(source);ts_sample_free(&s);
    TsMosaicEvent *e=ts_mosaic_add(m,source,0,0);assert(e);e->duration=120;e->fade_in=35;
    e->output_route.matrix_enabled=1;e->output_route.matrix_gain[TS_MATRIX_EFFECT_BASE]=1000;m->playing=1;
    TsSourceRouteMix routes={0};ts_mosaic_recall_capture(&fx);
    ts_mosaic_read_routed(m,48000,&routes);assert(ts_matrix_recall_take(&fx,0).hits==1);
    for(int i=0;i<100;++i){ts_source_route_mix_reset(&routes);ts_mosaic_begin_block(m,1);ts_mosaic_read_routed(m,48000,&routes);}
    assert(ts_matrix_recall_take(&fx,0).hits==0);
    ++e->revision;ts_mosaic_read_routed(m,48000,&routes);assert(ts_matrix_recall_take(&fx,0).hits==0);
    ts_mosaic_seek(m,0);e->muted=1;ts_mosaic_read_routed(m,48000,&routes);assert(ts_matrix_recall_take(&fx,0).hits==0);
    ts_mosaic_seek(m,0);e->muted=0;ts_mosaic_read_routed(m,48000,&routes);assert(ts_matrix_recall_take(&fx,0).hits==1);
    ts_mosaic_recall_capture(NULL);ts_mosaic_free(m);ts_matrix_effects_free(&fx);
}
int main(void)
{
    check(44100,0);check(48000,0);check(48000,1);check(96000,1);
    mosaic_starts();
    puts("Recall audio: silence, sustained input, separate onsets, tracker deduplication, span parity and silent output passed");
    return 0;
}
