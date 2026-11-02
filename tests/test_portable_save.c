#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/sample_pages.h"
#include "tapesister/mosaic.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/resource.h>
#include <unistd.h>
#endif

static int stages,progress;
static void observe(void *data,const char *stage,const char *asset,uint64_t bytes,size_t completed,size_t total)
{(void)data;(void)asset;(void)bytes;assert(completed<=total);if(!strcmp(stage,"Committing"))++stages;++progress;}
static int cancel(void *data){return ((TsSaveControl *)data)->bytes>65536;}
static double seconds(void)
{
#ifdef _WIN32
    LARGE_INTEGER counter,frequency;QueryPerformanceCounter(&counter);QueryPerformanceFrequency(&frequency);return (double)counter.QuadPart/frequency.QuadPart;
#else
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;
#endif
}
static void saved(TsSamplePages *p,TsInstrument *bank,TsSaveControl *control,const char *path,const char *kind)
{
    char error[512];double start=seconds();
    int ok=ts_sample_pages_save_project_controlled(p,bank,NULL,NULL,path,control,error,sizeof(error));
    if(!ok)fprintf(stderr,"%s: %s\n",kind,error);
    assert(ok);printf("%s: %.3f seconds, %llu bytes written, %zu/%zu bank assets\n",kind,seconds()-start,(unsigned long long)control->bytes,control->completed,control->total);fflush(stdout);
}
int main(int argc,char **argv)
{
    int large=argc>1 && !strcmp(argv[1],"--large");
    char work[128];
#ifdef _WIN32
    snprintf(work,sizeof(work),"portable-test-%lu-%lu",(unsigned long)GetCurrentProcessId(),(unsigned long)GetTickCount());
    assert(CreateDirectoryA(work,NULL));assert(SetCurrentDirectoryA(work));
#else
    snprintf(work,sizeof(work),"portable-test-XXXXXX");assert(mkdtemp(work));assert(!chdir(work));
#endif
    static TsInstrument bank;TsSamplePages pages;char error[512];
    ts_instrument_init(&bank);assert(ts_sample_pages_init(&pages,error,sizeof(error)));
    pages.mosaic=ts_mosaic_create();assert(pages.mosaic);
    TsSample sample={.frames=large?2750000:1024,.channels=2,.sample_rate=48000};
    sample.data=malloc(sample.frames*2*sizeof(float));assert(sample.data);
    uint64_t hashes[75];
    for(int n=0;n<75;++n) {
        if(n && n%16==0)assert(ts_sample_pages_append_and_switch(&pages,&bank,NULL,error,sizeof(error)));
        for(size_t i=0;i<sample.frames*2;++i)sample.data[i]=.2f*sinf((float)(i%8192)*.01f+n);
        snprintf(sample.name,sizeof(sample.name),"PORTABLE %02d",n);
        assert(ts_instrument_select_bank(&bank,n%16,error,sizeof(error)));
        assert(ts_instrument_import_sample(&bank,&sample,0,0,0,TS_LOOP_FORWARD,error,sizeof(error)));
        hashes[n]=ts_sample_save_signature(&bank.current);
        if(n==0) {
            TsMosaicSource *s=ts_mosaic_source(pages.mosaic,&sample,error,sizeof(error));assert(s);
            for(int i=0;i<28;++i) {TsMosaicEvent *e=ts_mosaic_add(pages.mosaic,s,i*2,50);assert(e);e->live=i<4;e->live_finite=0;}
        }
    }
    ts_sample_free(&sample);
    printf("75 imported sounds; %llu source PCM bytes; 28 cards share one Mosaic source\n",(unsigned long long)(75ull*(large?2750000:1024)*8));fflush(stdout);
    TsSaveControl control={.progress=observe};
    saved(&pages,&bank,&control,"portable/portable.tsr","initial");
    uint64_t first_bytes=control.bytes;
#ifndef _WIN32
    struct stat before,after;assert(!stat("portable/project-data/page-002.tsr",&before));
#endif
    saved(&pages,&bank,&control,"portable/portable.tsr","unchanged");
    assert(control.bytes<first_bytes/10);
#ifndef _WIN32
    assert(!stat("portable/project-data/page-002.tsr",&after));assert(before.st_ino==after.st_ino);
#endif
    pages.mosaic->events[0].gain=.3f;
    saved(&pages,&bank,&control,"portable/portable.tsr","arrangement edit");
    assert(control.bytes<first_bytes/10);
    /* Exact float changes below the old quantized hash resolution must survive. */
    bank.current.data[0]=nextafterf(bank.current.data[0],1.0f);
    hashes[74]=ts_sample_save_signature(&bank.current);
    saved(&pages,&bank,&control,"portable/portable.tsr","sample edit");
    assert(control.bytes>0 && control.bytes<first_bytes);
    uint64_t mosaic_hash=ts_mosaic_hash(pages.mosaic);
    TsSaveControl stopped={.cancelled=cancel};stopped.userdata=&stopped;
    ++bank.family_sequence;
    assert(!ts_sample_pages_save_project_controlled(&pages,&bank,NULL,NULL,"portable/portable.tsr",&stopped,error,sizeof(error)));
    assert(stopped.was_cancelled && stopped.recovery[0]);
    FILE *recovery=fopen(stopped.recovery,"rb");if(recovery)fclose(recovery);
    ts_mosaic_free(pages.mosaic);pages.mosaic=NULL;ts_sample_pages_free(&pages);ts_instrument_free(&bank);
    assert(!rename("portable","relocated"));
    assert(ts_sample_pages_init(&pages,error,sizeof(error)));pages.mosaic=ts_mosaic_create();assert(pages.mosaic);
    static TsInstrument record;ts_instrument_init(&record);
    if(!ts_sample_pages_load_project(&pages,&bank,&record,"relocated/portable.tsr",error,sizeof(error))) {fprintf(stderr,"reopen: %s\n",error);abort();}
    assert(pages.page_count==5 && ts_mosaic_hash(pages.mosaic)==mosaic_hash);
    for(int n=0;n<75;++n) {
        const TsInstrument *i=ts_sample_pages_page(&pages,&bank,(size_t)n/16);
        const TsSample *s=n==74?&bank.current:&i->bank[n%16].sample;
        assert(ts_sample_save_signature(s)==hashes[n]);
    }
    assert(stages && progress);
    ts_mosaic_free(pages.mosaic);pages.mosaic=NULL;ts_sample_pages_free(&pages);ts_instrument_free(&bank);ts_instrument_free(&record);
#ifndef _WIN32
    struct rusage usage;assert(!getrusage(RUSAGE_SELF,&usage));printf("peak RSS: %ld KiB\n",usage.ru_maxrss);
#endif
    puts("portable reopen, unused sounds, shared loops, incremental edits, relocation and cancellation passed");
    /* Run in a disposable test directory; retained projects are useful for inspection. */
    return 0;
}
