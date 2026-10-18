/* Run against the same compiler/build before and after a routing change.
   Identity processors isolate graph cost; this is not total project DSP load. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/routing_matrix.h"
#include <assert.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static TsStereoFrame pass(void *context,int node,TsStereoFrame in)
{ (void)context;(void)node;return in; }
static double milliseconds(clock_t start)
{ return 1000.0*(clock()-start)/CLOCKS_PER_SEC; }
int main(void)
{
    TsRoutingMatrix *m=calloc(1,sizeof(*m));assert(m);ts_matrix_init(m);
    TsMatrixControls c=m->controls;c.enabled=1;
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i) {
        int slot=ts_matrix_effect_add(&c,TS_MATRIX_FX_COMPRESSOR);assert(slot==i);
        int n=TS_MATRIX_EFFECT_BASE+i;
        c.row[i?n-1:TS_MATRIX_MOSAIC].matrix_gain[n]=700;
        c.row[n].matrix_gain[TS_MATRIX_EQ]=200;
        if(i)c.row[n-1].matrix_gain[ts_matrix_sidechain_for(n)]=300;
    }
    assert(ts_matrix_set(m,&c,48000));
    clock_t start=clock();
    for(int i=0;i<1000;++i) {
        c.row[TS_MATRIX_EFFECT_BASE].matrix_gain[TS_MATRIX_EQ]=200+(i&1);
        if(!ts_matrix_set(m,&c,48000))return 1;
    }
    printf("16-instance graph edit: %.6f ms/set\n",milliseconds(start)/1000);
    for(int feedback=0;feedback<2;++feedback) {
        c.feedback=feedback;
        if(feedback && ts_matrix_connect(&c,TS_MATRIX_EFFECT_BASE,TS_MATRIX_EFFECT_BASE,250)!=1)return 1;
        if(!ts_matrix_set(m,&c,48000))return 1;
        TsStereoFrame live[TS_MATRIX_ROWS-TS_MATRIX_NODES]={{0}};
        uint64_t hash=UINT64_C(14695981039346656037);start=clock();
        for(unsigned frame=0;frame<480000;++frame) {
            live[TS_MATRIX_MOSAIC-TS_MATRIX_NODES]=(TsStereoFrame){(int)(frame%251)*.001f-.1f,(int)(frame%173)*.001f-.1f};
            TsSourceRouteMix sums={0};ts_matrix_process(m,&sums,live,pass,NULL);
            const unsigned char *bytes=(const unsigned char *)m->output;
            for(size_t i=0;i<sizeof(m->output);++i)hash=(hash^bytes[i])*UINT64_C(1099511628211);
        }
        printf("16-instance graph, feedback %s: %.3f ms/512 frames; output hash %016" PRIx64 "\n",
            feedback?"on":"off",milliseconds(start)*512/480000,hash);
    }
    free(m);return 0;
}
