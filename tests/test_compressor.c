#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/compressor.h"
#include "tapesister/config.h"
#include "tapesister/sister_project_state.h"
#include <assert.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <stdlib.h>
#include "tsr_legacy_fixture.h"

static void near(float x,float y,float tolerance){if(fabsf(x-y)>=tolerance)fprintf(stderr,"expected %.9g got %.9g tolerance %.9g\n",y,x,tolerance);assert(fabsf(x-y)<tolerance);}
static void dsp(void)
{
    TsCompressor c;ts_compressor_init(&c);TsStereoFrame in={1,-.25f},out;
    out=ts_compressor_process(&c,in,(TsStereoFrame){0});assert(!memcmp(&in,&out,sizeof(in)));
    TsCompressorControls p=c.controls;p.enabled=1;p.threshold_db=-20;p.attack_ms=1;
    ts_compressor_set(&c,&p);
    for(int i=0;i<48000;++i)out=ts_compressor_process(&c,in,(TsStereoFrame){0});
    near(20*log10f(out.l),-15,.01f);near(out.r/out.l,-.25f,1e-6f);
    near(c.reduction_db,15,.01f);
    p.external=1;ts_compressor_set(&c,&p);
    for(int i=0;i<240000;++i)out=ts_compressor_process(&c,in,(TsStereoFrame){0});
    near(out.l,1,.001f); /* Silent external key never falls back to internal. */
    for(int i=0;i<48000;++i)out=ts_compressor_process(&c,in,(TsStereoFrame){1,-1});
    near(20*log10f(out.l),-15,.01f); /* Opposite stereo polarity cannot cancel the detector. */
    out=ts_compressor_process(&c,(TsStereoFrame){0},(TsStereoFrame){1,1});assert(out.l==0 && out.r==0);
    out=ts_compressor_process(&c,(TsStereoFrame){NAN,INFINITY},(TsStereoFrame){INFINITY,NAN});assert(isfinite(out.l+out.r+c.gain));
    p.ratio=1;p.output_db=6;ts_compressor_set(&c,&p);
    for(int i=0;i<240000;++i)out=ts_compressor_process(&c,in,(TsStereoFrame){1,1});
    near(out.l,powf(10,.3f),.002f);
    p.enabled=0;ts_compressor_set(&c,&p);
    for(int i=0;i<48000;++i)out=ts_compressor_process(&c,in,(TsStereoFrame){1,1});
    assert(!memcmp(&in,&out,sizeof(in)) && c.reduction_db==0);
    /* Bypass and parameter changes slew at all supported device rates. */
    const unsigned rates[]={8000,44100,48000,96000,192000};
    for(unsigned r=0;r<sizeof(rates)/sizeof(*rates);++r) {
        ts_compressor_init(&c);ts_compressor_prepare(&c,rates[r]);p=c.controls;
        p.enabled=1;p.external=1;p.threshold_db=-30;p.ratio=8;p.attack_ms=10;p.release_ms=100;
        ts_compressor_set(&c,&p);float previous=.1f;
        for(unsigned i=0;i<rates[r];++i) {
            if(i==rates[r]/2){p.enabled=0;ts_compressor_set(&c,&p);}
            out=ts_compressor_process(&c,(TsStereoFrame){.1f,.05f},(TsStereoFrame){1,1});
            assert(fabsf(out.l-previous)<.003f);previous=out.l;
        }
        near(out.l,.1f,1e-6f);assert(c.rate==rates[r]);
    }
    /* A warmed-up attack reaches 1 - exp(-1) of its gain change in 10 ms. */
    ts_compressor_init(&c);p=c.controls;p.enabled=1;p.external=1;p.threshold_db=-20;p.release_ms=100;
    ts_compressor_set(&c,&p);
    for(int i=0;i<48000;++i)ts_compressor_process(&c,in,(TsStereoFrame){0});
    for(int i=0;i<480;++i)ts_compressor_process(&c,in,(TsStereoFrame){1,1});
    float target=powf(10,-15*.05f);near(c.gain,target+(1-target)*expf(-1),.001f);
    for(int i=0;i<48000;++i)ts_compressor_process(&c,in,(TsStereoFrame){1,1});
    for(int i=0;i<4800;++i)ts_compressor_process(&c,in,(TsStereoFrame){0});
    near(c.gain,1+(target-1)*expf(-1),.002f);
    for(int i=0;i<5;++i)for(int n=0;n<=100;++n) {
        ts_compressor_set_normalized(&p,i,n/100.f);assert(ts_compressor_valid(&p));
        near(ts_compressor_normalized(&p,i),n/100.f,1e-5f);
    }
}

typedef struct {TsRoutingMatrix *m;TsCompressor comp;int seen,compressed;} Graph;
static TsStereoFrame node(void *context,int n,TsStereoFrame in)
{
    Graph *g=context;
    if(n==TS_MATRIX_PRISM){++g->seen;return (TsStereoFrame){1,-1};}
    if(n==TS_MATRIX_COMPRESSOR) {
        assert(g->seen>g->compressed);++g->compressed;
        return ts_compressor_process(&g->comp,in,g->m->sidechain);
    }
    return in;
}
static void graph(void)
{
    TsRoutingMatrix m;ts_matrix_init(&m);TsMatrixControls c=m.controls;c.enabled=1;
    c.row[TS_MATRIX_PRISM].matrix_gain[TS_MATRIX_MAIN]=0;
    assert(ts_matrix_connect(&c,TS_MATRIX_PRISM,TS_MATRIX_SIDECHAIN,1000)==1);
    c.row[TS_MATRIX_MOSAIC].matrix_gain[TS_MATRIX_MAIN]=0;
    assert(ts_matrix_connect(&c,TS_MATRIX_MOSAIC,TS_MATRIX_COMPRESSOR,1000)==1);
    assert(ts_matrix_set(&m,&c,48000));
    Graph g={.m=&m};ts_compressor_init(&g.comp);TsCompressorControls p=g.comp.controls;
    p.enabled=p.external=1;p.threshold_db=-20;ts_compressor_set(&g.comp,&p);
    TsStereoFrame live[TS_MATRIX_ROWS-TS_MATRIX_NODES]={0};live[TS_MATRIX_MOSAIC-TS_MATRIX_NODES]=(TsStereoFrame){.2f,.1f};
    for(int i=0;i<48000;++i){TsSourceRouteMix sums={0};ts_matrix_process(&m,&sums,live,node,&g);}
    near(m.output[0].l,.2f*powf(10,-.75f),.0001f);near(m.sidechain.r,-1,1e-6f);
    assert(g.seen==48000 && g.compressed==48000);
    /* Control dependencies count in cycle detection, including direct self-keying. */
    assert(ts_matrix_connect(&c,TS_MATRIX_COMPRESSOR,TS_MATRIX_PRISM,1000)==0);
    assert(ts_matrix_connect(&c,TS_MATRIX_COMPRESSOR,TS_MATRIX_SIDECHAIN,1000)==0);
    c.feedback=1;assert(ts_matrix_connect(&c,TS_MATRIX_COMPRESSOR,TS_MATRIX_SIDECHAIN,1000)==1);
    assert(c.delayed[TS_MATRIX_COMPRESSOR]&(1u<<TS_MATRIX_SIDECHAIN));
    assert(c.row[TS_MATRIX_COMPRESSOR].matrix_gain[TS_MATRIX_SIDECHAIN]==500);
    ts_matrix_cut_feedback(&c);assert(!c.row[TS_MATRIX_COMPRESSOR].matrix_gain[TS_MATRIX_SIDECHAIN]);
    /* Removing the trigger releases, and key audio is never an audible return. */
    assert(ts_matrix_connect(&c,TS_MATRIX_PRISM,TS_MATRIX_SIDECHAIN,0)==1);assert(ts_matrix_set(&m,&c,48000));
    for(int i=0;i<240000;++i){TsSourceRouteMix sums={0};ts_matrix_process(&m,&sums,live,node,&g);}
    near(m.output[0].l,.2f,.0002f);
    live[TS_MATRIX_MOSAIC-TS_MATRIX_NODES]=(TsStereoFrame){0};
    for(int i=0;i<1000;++i){TsSourceRouteMix sums={0};ts_matrix_process(&m,&sums,live,node,&g);}
    assert(m.output[0].l==0 && m.output[0].r==0);
}
static void persistence(void)
{
    TsConfig a,b;char error[160];ts_config_init(&a);
    a.compressor=(TsCompressorControls){1,1,-31,7,3,680,-2};
    a.matrix.row[TS_MATRIX_PRISM].matrix_gain[TS_MATRIX_SIDECHAIN]=350;
    assert(ts_config_save(&a,"compressor-config.ini",error,sizeof(error)));
    assert(ts_config_load(&b,"compressor-config.ini",error,sizeof(error)));
    assert(!memcmp(&a.compressor,&b.compressor,sizeof(a.compressor)) && !memcmp(&a.matrix,&b.matrix,sizeof(a.matrix)));
    TsSisterProjectState s,t;ts_sister_project_state_init(&s,48000);s.compressor=a.compressor;s.matrix=a.matrix;
    assert(ts_sister_project_state_save_file(&s,"compressor-project.ini",error,sizeof(error)));int present=0;
    assert(ts_sister_project_state_load_file(&t,"compressor-project.ini",48000,&present,error,sizeof(error)) && present);
    assert(!memcmp(&s.compressor,&t.compressor,sizeof(s.compressor)));
    static TsSisterRuntime r;ts_sister_runtime_init(&r);assert(ts_sister_project_state_apply(&t,&r,NULL));
    assert(ts_sister_runtime_reconfigure(&r,96000,2,error,sizeof(error)));
    assert(r.compressor.rate==96000 && !memcmp(&r.compressor.controls,&s.compressor,sizeof(s.compressor)));
    ts_sister_runtime_free(&r);
    TsMatrixControls old;ts_matrix_default(&old);
    assert(ts_matrix_read(&old,"Matrix.Row6","0,0,0,0,0,0,0,0,0,0,1000,0,0,0,0,0,0,0")==1);
    assert(old.row[TS_MATRIX_LIMITER].matrix_gain[7]==1000);
    assert(!old.row[TS_MATRIX_LIMITER].matrix_gain[TS_MATRIX_COMPRESSOR] && !old.row[TS_MATRIX_LIMITER].matrix_gain[TS_MATRIX_SIDECHAIN]);
    assert(ts_matrix_valid(&old));
    assert(ts_compressor_read(&s.compressor,"Compressor.Controls","1,1,nan,4,10,250,0")==-1);
    assert(ts_compressor_read(&s.compressor,"Compressor.Controls","1,1,-20,0,10,250,0")==-1);
    assert(ts_compressor_read(&s.compressor,"Compressor.Controls","1,1,-20,4,10,250,0 junk")==-1);
    TsInstrument *tile=calloc(1,sizeof(*tile)),*copy=calloc(1,sizeof(*copy));assert(tile && copy);
    ts_instrument_init(tile);ts_instrument_init(copy);
    assert(ts_instrument_activate_silence(tile,64,48000,error,sizeof(error)));
    tile->bank[0].output_route.matrix_enabled=1;
    tile->bank[0].output_route.matrix_gain[7]=600;
    tile->bank[0].output_route.matrix_gain[TS_MATRIX_COMPRESSOR]=1000;
    tile->bank[0].output_route.matrix_gain[TS_MATRIX_SIDECHAIN]=300;
    assert(ts_instrument_save_recipe(tile,"compressor-tile.tsr",error,sizeof(error)));
    assert(ts_instrument_load_recipe(copy,"compressor-tile.tsr",error,sizeof(error)));
    assert(!memcmp(&copy->bank[0].output_route,&tile->bank[0].output_route,sizeof(TsSourceRoute)));
    for(int version=37;version<=38;++version) {
        assert(test_write_legacy_single_tile("compressor-tile.tsr","compressor-old.tsr",version));
        assert(ts_instrument_load_recipe(copy,"compressor-old.tsr",error,sizeof(error)));
        assert(copy->bank[0].output_route.matrix_gain[7]==600);
        assert(!copy->bank[0].output_route.matrix_gain[TS_MATRIX_COMPRESSOR]);
        assert(!copy->bank[0].output_route.matrix_gain[TS_MATRIX_SIDECHAIN]);
    }
    ts_instrument_free(tile);ts_instrument_free(copy);free(tile);free(copy);
    remove("compressor-tile.tsr");remove("compressor-old.tsr");
    remove("compressor-config.ini");remove("compressor-project.ini");
}
int main(void)
{
    dsp();graph();persistence();
    TsCompressor c;ts_compressor_init(&c);TsCompressorControls p=c.controls;p.enabled=p.external=1;ts_compressor_set(&c,&p);
    clock_t start=clock();volatile float result=0;
    for(int i=0;i<4800000;++i)result=ts_compressor_process(&c,(TsStereoFrame){.2f,.1f},(TsStereoFrame){i%24000<2400?.8f:0,0}).l;
    printf("Compressor: 100 seconds audio in %.3f CPU seconds; last %.6f\n",(double)(clock()-start)/CLOCKS_PER_SEC,result);
    puts("Compressor transfer, timing, stereo, sidechain graph, controls and persistence passed");return 0;
}
