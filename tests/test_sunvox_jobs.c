#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/sunvox_fx.h"
#include "tapesister/matrix_effects.h"
#include "tapesister/profile.h"
#include <assert.h>
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <fenv.h>
#include <time.h>
#if defined(__SSE__)
#include <xmmintrin.h>
#endif
static atomic_uint measuring,allocations;
#ifdef TS_WRAP_ALLOCATIONS
void *__real_malloc(size_t);void *__real_calloc(size_t,size_t);void *__real_realloc(void *,size_t);void __real_free(void *);
void *__wrap_malloc(size_t n){if(atomic_load(&measuring))atomic_fetch_add(&allocations,1);return __real_malloc(n);}
void *__wrap_calloc(size_t a,size_t b){if(atomic_load(&measuring))atomic_fetch_add(&allocations,1);return __real_calloc(a,b);}
void *__wrap_realloc(void *p,size_t n){if(atomic_load(&measuring))atomic_fetch_add(&allocations,1);return __real_realloc(p,n);}
void __wrap_free(void *p){if(atomic_load(&measuring))atomic_fetch_add(&allocations,1);__real_free(p);}
#endif
static atomic_uint_fast64_t ticks;
static uint64_t tick(void){return atomic_fetch_add(&ticks,1)+1;}
static TsSunVoxControls simple(const char *name)
{
    FILE *f=fopen("jobs.sunsynth","wb");assert(f);unsigned size=(unsigned)strlen(name)+1;
    unsigned char n[4]={size,0,0,0};
    assert(fwrite("SSYN\0\0\0\0STYP",1,12,f)==12 && fwrite(n,1,4,f)==4 && fwrite(name,1,size,f)==size);
    assert(fwrite("SEND\0\0\0\0",1,8,f)==8);assert(!fclose(f));
    TsSunVoxControls c;assert(ts_sunvox_import(&c,"jobs.sunsynth"));remove("jobs.sunsynth");return c;
}
static double wall_now(void)
{struct timespec t;assert(timespec_get(&t,TIME_UTC)==TIME_UTC);return t.tv_sec+t.tv_nsec*1e-9;}
static void profile_workers(TsSunVoxControls patch)
{
    TsMatrixControls c;ts_matrix_default(&c);c.enabled=1;patch.quantum=1024;
    for(int i=0;i<2;++i){assert(ts_matrix_effect_add(&c,TS_MATRIX_FX_SUNVOX)==i);c.effect[i].sunvox=patch;}
    ts_sunvox_workers_configure(1);assert(ts_profile_init(tick,1000000));
    uint64_t reference=0;
    for(int profiled=0;profiled<2;++profiled) {
        TsMatrixEffects bank;ts_matrix_effects_init(&bank);assert(ts_matrix_effects_restore(&bank,&c,48000));
        ts_profile_enable(profiled);uint64_t hash=UINT64_C(14695981039346656037),before=atomic_load(&ticks);
        for(int block=0;block<128;++block) {
            ts_profile_lane_begin(1);
            for(int frame=0;frame<512;++frame) {
                ts_matrix_effects_begin_frame(&bank);
                for(int i=0;i<2;++i) {
                    TsStereoFrame out=ts_matrix_effects_process(&bank,i,(TsStereoFrame){.1f,-.2f},(TsStereoFrame){0},1);
                    const unsigned char *p=(const unsigned char *)&out;
                    for(unsigned b=0;b<sizeof(out);++b)hash=(hash^p[b])*UINT64_C(1099511628211);
                }
                ts_matrix_effects_end_frame(&bank);
            }
            ts_profile_lane_end(1);
        }
        if(!profiled){reference=hash;assert(atomic_load(&ticks)==before);}
        else {
            assert(hash==reference);TsProfileSnapshot s;ts_profile_snapshot(&s);
            for(int i=0;i<2;++i){assert(s.effect[i].samples==4 && s.effect[i].blocks.ticks>0);assert(s.effect[i].identity.id==c.effect[i].id);}
        }
        ts_matrix_effects_free(&bank);
    }
    ts_profile_enable(0);ts_sunvox_workers_configure(0);
}
static void benchmark(TsSunVoxControls c)
{
    for(int round=0;round<3;++round)for(unsigned q=256;q<=1024;q*=4)for(int pass=0;pass<2;++pass) {
        int workers=(round+pass)&1;c.quantum=q;TsSunVoxEngine *e[4];
        for(int i=0;i<4;++i){e[i]=ts_sunvox_new(&c,48000);assert(e[i]);}
        TsSunVoxBatch *pool=ts_sunvox_batch_new(workers);
        double elapsed=0;uint64_t hash=UINT64_C(14695981039346656037);
        for(unsigned block=0;block<512;++block) {
            TsSunVoxJob jobs[4];
            for(unsigned frame=0;frame<q;++frame)for(int i=0;i<4;++i) {
                float out[2],dry[2],v=((int)((block*q+frame)%251)-125)*.001f;
                int ready=ts_sunvox_stage_frame(e[i],v,-v,out,dry);assert(ready==(frame+1==q));
                const unsigned char *p=(const unsigned char *)out;
                for(unsigned b=0;b<sizeof(out);++b)hash=(hash^p[b])*UINT64_C(1099511628211);
                jobs[i]=(TsSunVoxJob){.engine=e[i]};
            }
            double start=wall_now();ts_sunvox_batch_render(pool,jobs,4,NULL);elapsed+=wall_now()-start;
        }
        printf("round=%d q=%u workers=%d render_ms=%.3f hash=%016llx dispatch=%llu audio_claimed=%llu\n",round,q,workers,elapsed*1000,
            (unsigned long long)hash,(unsigned long long)ts_sunvox_batch_dispatched(pool),(unsigned long long)ts_sunvox_batch_stolen(pool));
        ts_sunvox_batch_free(pool);for(int i=0;i<4;++i)ts_sunvox_free(e[i]);
    }
}
int main(int argc,char **argv)
{
    TsSunVoxControls reverb=simple("Reverb"),filter=simple("Filter"),amp=simple("Amplifier"),velvet,nested;
    if(argc==2 && !strcmp(argv[1],"--benchmark")){benchmark(reverb);return 0;}
    int factory=-1;for(int i=0;i<ts_sunvox_factory_count();++i)if(!strcmp(ts_sunvox_factory_name(i),"echo2 Velvet Reverb 3"))factory=i;
    assert(factory>=0 && ts_sunvox_select(&velvet,factory));
    factory=-1;for(int i=0;i<ts_sunvox_factory_count();++i)if(!strcmp(ts_sunvox_factory_name(i),"SYBERIA CHORUS"))factory=i;
    assert(factory>=0 && ts_sunvox_select(&nested,factory));
    TsSunVoxEngine *unsafe=ts_sunvox_new(&filter,48000);assert(unsafe && !ts_sunvox_parallel_cost(unsafe));ts_sunvox_free(unsafe);
    for(int scenario=0;scenario<5;++scenario) {
        unsigned q=scenario==0?64:scenario==1?256:1024;
        TsSunVoxBatch *pool=ts_sunvox_batch_new(1);
        TsSunVoxEngine *engines[2][4];TsSunVoxControls c[4];
        for(int i=0;i<4;++i){c[i]=i==3?amp:scenario==4 && i==0?velvet:scenario==3?nested:reverb;c[i].quantum=q;
            for(int lane=0;lane<2;++lane){engines[lane][i]=ts_sunvox_new(&c[i],48000);assert(engines[lane][i]);}}
        if(scenario==3)assert(ts_sunvox_parallel_cost(engines[0][0])>=16);
        if(scenario==4)assert(!ts_sunvox_parallel_cost(engines[0][0]));
#if defined(__SSE__)
        unsigned saved=_mm_getcsr();_mm_setcsr(saved|0x8040u);
#endif
        // Nondefault rounding verifies that a worker adopts the caller's FP environment.
        int rounding=fegetround();assert(!fesetround(FE_DOWNWARD));
        double energy=0;
        for(unsigned frame=0;frame<65536;++frame) {
            if(frame==16384 || frame==32768)for(int i=0;i<3;++i) {
                c[i].value[0]=frame==16384?0:16384;
                for(int lane=0;lane<2;++lane)ts_sunvox_set(engines[lane][i],&c[i]);
            }
            TsSunVoxJob jobs[4];unsigned count=0;float out[2][4][4];
            atomic_store(&measuring,1);
            for(int lane=0;lane<2;++lane)for(int i=0;i<4;++i) {
                float l=frame<48000?((int)(frame%251)-125)*.001f:0,r=-l;
                // A serial dependency shares the same existing quantum latency.
                if(i==2){l+=out[lane][0][0];r+=out[lane][0][1];}
                if(!lane)ts_sunvox_process(engines[lane][i],l,r,out[lane][i],out[lane][i]+2);
                else if(ts_sunvox_stage_frame(engines[lane][i],l,r,out[lane][i],out[lane][i]+2))
                    jobs[count++]=(TsSunVoxJob){.engine=engines[lane][i],.timed=1};
            }
            ts_sunvox_batch_render(pool,jobs,count,tick);atomic_store(&measuring,0);
            for(unsigned i=0;i<count;++i)assert(jobs[i].ticks>0);
            assert(!memcmp(out[0],out[1],sizeof(out[0])));assert(!atomic_load(&allocations));
            energy+=fabsf(out[1][0][0]);
        }
        assert(energy>1);assert(fegetround()==FE_DOWNWARD);fesetround(rounding);
#if defined(__SSE__)
        _mm_setcsr(saved);
#endif
        if(pool){assert(q<=256?!ts_sunvox_batch_dispatched(pool):ts_sunvox_batch_dispatched(pool)>0);
            printf("q=%u dispatch=%llu audio-claimed=%llu\n",q,(unsigned long long)ts_sunvox_batch_dispatched(pool),(unsigned long long)ts_sunvox_batch_stolen(pool));}
        ts_sunvox_batch_free(pool);
        for(int lane=0;lane<2;++lane)for(int i=0;i<4;++i)ts_sunvox_free(engines[lane][i]);
    }
    profile_workers(reverb);
    puts("Serial/worker PCM and dry latency exact; nested patches, tails, controls, FP modes, allocation checks, profiling and shutdown passed");
}
