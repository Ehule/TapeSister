/* Explicit CPU timings, not a timing-sensitive CI gate. Run in a disposable
   directory: this writes a synthetic project bundle named load-benchmark. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/sample_pages.h"
#include "tapesister/mosaic.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double elapsed(clock_t start)
{return (double)(clock()-start)*1000/CLOCKS_PER_SEC;}
int main(void)
{
    TsSamplePages pages;char error[160];
    TsInstrument *active=malloc(sizeof(*active)),*record=malloc(sizeof(*record));assert(active && record);
    ts_instrument_init(active);ts_instrument_init(record);
    assert(ts_sample_pages_init(&pages,error,sizeof(error)));
    pages.mosaic=ts_mosaic_create();assert(pages.mosaic);
    TsSample sample={.frames=48000,.sample_rate=48000,.channels=2};
    sample.data=malloc(sample.frames*2*sizeof(float));assert(sample.data);
    for(int n=0;n<16;++n) {
        if(n && n%4==0)assert(ts_sample_pages_append_and_switch(&pages,active,NULL,error,sizeof(error)));
        for(size_t i=0;i<sample.frames*2;++i)sample.data[i]=.2f*sinf(i*(.01f+n*.001f));
        snprintf(sample.name,sizeof(sample.name),"LOAD SOURCE %02d",n);
        assert(ts_instrument_select_bank(active,n%4,error,sizeof(error)));
        assert(ts_instrument_import_sample(active,&sample,0,0,0,TS_LOOP_FORWARD,error,sizeof(error)));
        TsMosaicSource *source=ts_mosaic_source(pages.mosaic,&sample,error,sizeof(error));assert(source);
        for(int i=0;i<64;++i)assert(ts_mosaic_add(pages.mosaic,source,n*64+i,0));
    }
    assert(ts_instrument_import_sample(record,&sample,0,0,0,TS_LOOP_FORWARD,error,sizeof(error)));
    const char *path="load-benchmark/load-benchmark.tsr";
    assert(ts_sample_pages_save_project(&pages,active,record,NULL,path,error,sizeof(error)));
    uint64_t hash=ts_mosaic_hash(pages.mosaic);
    for(int trial=0;trial<3;++trial) {
        clock_t start=clock();
#ifdef TS_BENCHMARK_LEGACY
        assert(ts_sample_pages_load_project(&pages,active,record,path,error,sizeof(error)));
        printf("trial %d: legacy locked load %.3f ms CPU\n",trial+1,elapsed(start));
#else
        TsSampleProjectLoad *load=ts_sample_pages_prepare_project(path,1,error,sizeof(error));assert(load);
        double prepare=elapsed(start);start=clock();
        assert(ts_sample_pages_publish_project(&pages,active,record,load,error,sizeof(error)));
        double publish=elapsed(start);start=clock();
        ts_sample_pages_dispose_project(load);
        printf("trial %d: prepare %.3f / publish %.3f / retire %.3f ms CPU\n",trial+1,prepare,publish,elapsed(start));
#endif
        assert(ts_mosaic_hash(pages.mosaic)==hash && pages.page_count==4 && pages.active_page==3);
    }
    ts_sample_free(&sample);ts_mosaic_free(pages.mosaic);ts_sample_pages_free(&pages);
    ts_instrument_free(active);ts_instrument_free(record);free(active);free(record);return 0;
}
