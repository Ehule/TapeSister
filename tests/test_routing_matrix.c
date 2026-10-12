#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/routing_matrix.h"
#include "tapesister/sister_runtime.h"
#include <assert.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
static int calls[TS_MATRIX_NODES];
static TsStereoFrame received[TS_MATRIX_NODES];
static TsStereoFrame identity(void *ctx,int n,TsStereoFrame input)
{(void)ctx;++calls[n];received[n]=input;return input;}
static void near(float a,float b){assert(fabsf(a-b)<.0001f);}
static void graph(void)
{
    TsRoutingMatrix m;ts_matrix_init(&m);TsMatrixControls c=m.controls;c.enabled=1;
    assert(ts_matrix_connect(&c,TS_MATRIX_NODES,TS_MATRIX_PRISM,1000)==1);
    assert(ts_matrix_connect(&c,TS_MATRIX_NODES,TS_MATRIX_SISTER,500)==1);
    assert(ts_matrix_connect(&c,TS_MATRIX_NODES,TS_MATRIX_NODES+1,1000)==1);
    assert(ts_matrix_connect(&c,TS_MATRIX_PRISM,TS_MATRIX_PRISM,1000)==0);
    assert(ts_matrix_set(&m,&c,48000));
    TsStereoFrame live[TS_MATRIX_ROWS-TS_MATRIX_NODES]={{.2f,.4f}};
    TsSourceRouteMix sums;
    for(int i=0;i<300;++i){ts_source_route_mix_init(&sums,NULL);ts_matrix_process(&m,&sums,live,identity,NULL);}
    for(int i=0;i<TS_MATRIX_NODES;++i)assert(calls[i]==300);
    near(received[TS_MATRIX_PRISM].l,.2f);near(received[TS_MATRIX_SISTER].r,.2f);
    near(m.output[0].l,.3f);near(m.output[1].r,.4f);
    /* Every processor output is usable as another processor's input. */
    assert(ts_matrix_connect(&c,TS_MATRIX_PRISM,TS_MATRIX_MAIN,0)==1);
    assert(ts_matrix_connect(&c,TS_MATRIX_PRISM,TS_MATRIX_FALLOUT,1000)==1);
    assert(ts_matrix_set(&m,&c,48000));
    for(int i=0;i<300;++i){ts_source_route_mix_init(&sums,NULL);ts_matrix_process(&m,&sums,live,identity,NULL);}
    near(received[TS_MATRIX_FALLOUT].r,.4f);near(m.output[0].l,.3f);
    assert(ts_matrix_connect(&c,TS_MATRIX_MAIN,TS_MATRIX_PRISM,1000)==0);
    c.feedback=1;assert(ts_matrix_connect(&c,TS_MATRIX_PRISM,TS_MATRIX_PRISM,1000)==1);
    assert(c.row[TS_MATRIX_PRISM].matrix_gain[TS_MATRIX_PRISM]==500);
    assert(ts_matrix_set(&m,&c,48000));
    for(int i=0;i<5000;++i){ts_source_route_mix_init(&sums,NULL);ts_matrix_process(&m,&sums,live,identity,NULL);assert(isfinite(m.output[0].l));assert(m.output[0].r<2);}
    ts_matrix_cut_feedback(&c);assert(!c.delayed[TS_MATRIX_PRISM]);assert(ts_matrix_set(&m,&c,48000));
    for(int i=0;i<300;++i){ts_source_route_mix_init(&sums,NULL);ts_matrix_process(&m,&sums,live,identity,NULL);}
    near(received[TS_MATRIX_PRISM].l,.2f);
    /* A hardware return is independent of send assignment and can feed EQ. */
    assert(ts_matrix_connect(&c,TS_MATRIX_MAIN,TS_MATRIX_EQ,0)==1);
    assert(ts_matrix_connect(&c,TS_MATRIX_MAIN,TS_MATRIX_NODES+1,1000)==1);
    assert(ts_matrix_connect(&c,TS_MATRIX_NODES+2,TS_MATRIX_EQ,1000)==1);
    snprintf(c.input_name[2],TS_MATRIX_NAME,"Vulture Return");snprintf(c.output_name[1],TS_MATRIX_NAME,"Vulture Send");
    assert(ts_matrix_set(&m,&c,48000));live[2]=(TsStereoFrame){.1f,-.15f};
    for(int i=0;i<300;++i){ts_source_route_mix_init(&sums,NULL);ts_matrix_process(&m,&sums,live,identity,NULL);}
    near(m.output[0].r,-.15f);near(m.output[1].l,.5f);
    c.row[TS_MATRIX_NODES].width=-100;
    FILE *f=tmpfile();assert(f&&ts_matrix_write(f,&c));rewind(f);TsMatrixControls copy;ts_matrix_default(&copy);
    char line[512];while(fgets(line,sizeof(line),f)){char *v=strchr(line,'=');assert(v);*v++=0;v[strcspn(v,"\r\n")]=0;assert(ts_matrix_read(&copy,line,v)==1);}fclose(f);
    assert(ts_matrix_valid(&copy));assert(!memcmp(&copy,&c,sizeof(c)));
    copy.row[TS_MATRIX_EQ].matrix_gain[TS_MATRIX_EQ]=1000;assert(!ts_matrix_valid(&copy));
    assert(ts_matrix_read(&copy,"Matrix.In0","A name that is much too long for a port")<0);
}
static void runtime(void)
{
    static TsSisterRuntime r;char error[200];ts_sister_runtime_init(&r);
    assert(ts_sister_runtime_reconfigure(&r,48000,2,error,sizeof(error)));
    ts_sister_runtime_configure_limiter(&r,0,-1,1,50);
    TsMatrixControls c=r.matrix.controls;c.enabled=1;assert(ts_matrix_set(&r.matrix,&c,48000));
    TsSisterSourceFrames s={.fm={.15f,.25f}};uint64_t clock=r.prism.clock;
    for(int i=0;i<300;++i)ts_sister_runtime_process_frame(&r,&s);
    near(r.matrix.output[0].l,.15f);near(r.matrix.output[0].r,.25f);
    assert(r.prism.clock-clock==300); /* Shared instance: exactly once per sample. */
    c.row[TS_MATRIX_FM].matrix_gain[TS_MATRIX_MAIN]=0;c.row[TS_MATRIX_FM].matrix_gain[TS_MATRIX_PRISM]=1000;
    assert(ts_matrix_set(&r.matrix,&c,48000));
    for(int i=0;i<300;++i)ts_sister_runtime_process_frame(&r,&s);
    near(r.matrix.output[0].l,0); /* An off wet return does not duplicate dry. */
    TsSisterParameters parameters=r.parameters;parameters.prism.enabled=1;parameters.prism.mix=1;
    ts_sister_runtime_set_parameters(&r,&parameters);double energy=0;clock=r.prism.clock;
    for(int i=0;i<60000;++i){ts_sister_runtime_process_frame(&r,&s);energy+=fabsf(r.matrix.output[0].l);}
    assert(energy>1 && r.prism.clock-clock==60000);
    c.row[TS_MATRIX_FM].matrix_gain[TS_MATRIX_MAIN]=1000;c.row[TS_MATRIX_FM].matrix_gain[TS_MATRIX_PRISM]=0;
    assert(ts_matrix_set(&r.matrix,&c,48000));
    assert(ts_sister_runtime_enable(&r,48000,2,2,1,error,sizeof(error)));
    c=r.matrix.controls;c.enabled=1;c.row[TS_MATRIX_FM].matrix_gain[TS_MATRIX_SISTER]=1000;
    assert(ts_matrix_set(&r.matrix,&c,48000));clock=r.prism.clock;
    for(int i=0;i<300;++i)ts_sister_runtime_process_frame(&r,&s);
    assert(r.prism.clock-clock==300);near(r.last_frame.input.l,.15f);
    assert(r.machine.buffer.data[100*2]!=0); /* No hidden TRACK/EXT source gate. */
    ts_sister_runtime_free(&r);
}
static void physical_inputs(void)
{
    TsInsert *io=malloc(sizeof(*io));assert(io);ts_insert_init(io);
    ts_insert_capture_prepare(io,6,48000,512);ts_insert_begin_output_block(io,512);
    atomic_store(&io->matrix_active,1);static float samples[4096*6];
    for(int f=0;f<4096;++f)for(int ch=0;ch<6;++ch)samples[f*6+ch]=(ch+1)*.1f;
    ts_insert_capture(io,samples,4096,6);TsStereoFrame pairs[4];
    for(int f=0;f<500;++f)ts_insert_matrix_read(io,pairs);
    near(pairs[0].l,.1f);near(pairs[0].r,.2f);near(pairs[2].l,.5f);near(pairs[2].r,.6f);near(pairs[3].l,0);
    assert(!io->controls.send_pair); /* No special insert reservation. */
    ts_insert_duplex_block(io,1,samples,4096,6);ts_insert_matrix_read(io,pairs);
    near(pairs[2].l,.5f);near(pairs[2].r,.6f);
    ts_insert_free(io);free(io);
}
int main(void){graph();runtime();physical_inputs();puts("Unified routing graph, shared DSP, loop isolation, hardware returns and persistence passed");}
