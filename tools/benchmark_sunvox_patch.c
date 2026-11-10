/* Device-free, repeatable embedded-effect benchmark. Keep the compiler flags,
   patch controls, input, rate and quantum equal when comparing two revisions.
   Hashing, file I/O and finite checks happen outside the timed render loop. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#else
#include <windows.h>
#endif
#include "tapesister/sunvox_fx.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(__SSE__) || defined(_M_X64)
#include <xmmintrin.h>
#endif
#ifdef TS_SUNVOX_BENCHMARK_SEED
void ts_sunvox_benchmark_seed(void);
#endif

static void require(int ok, const char *message)
{
    if(!ok) { fprintf(stderr,"%s\n",message); exit(1); }
}
static double thread_seconds(void)
{
#ifdef _WIN32
    FILETIME created,exited,kernel,user;
    require(GetThreadTimes(GetCurrentThread(),&created,&exited,&kernel,&user),"thread timer failed");
    ULARGE_INTEGER k,u;
    k.LowPart=kernel.dwLowDateTime;k.HighPart=kernel.dwHighDateTime;
    u.LowPart=user.dwLowDateTime;u.HighPart=user.dwHighDateTime;
    return (k.QuadPart+u.QuadPart)*1e-7;
#elif defined(CLOCK_THREAD_CPUTIME_ID)
    struct timespec t;
    require(!clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t),"thread timer failed");
    return t.tv_sec+t.tv_nsec*1e-9;
#else
    return (double)clock()/CLOCKS_PER_SEC;
#endif
}
static unsigned number(const char *s)
{
    char *end; unsigned long n=strtoul(s,&end,10);
    require(*s && !*end && n<=192000,"invalid numeric argument");
    return (unsigned)n;
}
int main(int argc,char **argv)
{
    require(argc==6 || argc==7,
        "usage: benchmark_sunvox_patch PATCH RATE QUANTUM SECONDS defaults|audit [stereo-f32-file]");
    unsigned rate=number(argv[2]),q=number(argv[3]),seconds=number(argv[4]);
    require(rate>=8000 && rate<=192000 && (q==64 || q==256 || q==1024) &&
        seconds>=1 && seconds<=600,"unsupported rate, quantum or duration");
    require(!strcmp(argv[5],"defaults") || !strcmp(argv[5],"audit"),"unknown controls mode");
    int at=-1;
    for(int i=0;i<ts_sunvox_factory_count();++i)
        if(!strcmp(ts_sunvox_factory_name(i),argv[1])) at=i;
    TsSunVoxControls c;
    require(at>=0 && ts_sunvox_select(&c,at),"unknown factory patch");
    c.quantum=q;
    if(!strcmp(argv[5],"audit")) {
        // The earlier native audit's settings; not a reconstruction of a user's project.
        if(!strcmp(argv[1],"echo2 Velvet Reverb 3")) c.value[9]=18200;
        else if(!strcmp(argv[1],"RAGINGCOAST Ensemble")) { c.value[7]=1800;c.value[10]=5600;c.value[11]=1800; }
        else if(!strcmp(argv[1],"cube48 ShimmerClassic")) { c.value[7]=775;c.value[10]=22016;c.value[11]=21000; }
        else require(0,"no audit settings for this patch");
    }
#ifdef TS_SUNVOX_BENCHMARK_SEED
    ts_sunvox_benchmark_seed();
#endif
    TsSunVoxEngine *s=ts_sunvox_new(&c,rate);
    require(s!=NULL,"engine creation failed");
#if defined(__SSE__) || defined(_M_X64)
    unsigned saved=_mm_getcsr();_mm_setcsr(saved|0x8040u);
#endif
    float input[2048];
    for(int i=0;i<1024;++i) {
        input[2*i]=.1f*sinf(i*.04f);
        input[2*i+1]=.1f*sinf(((i+240)%1024)*.04f);
    }
    size_t frames=(size_t)seconds*rate;
    float *output=malloc(frames*2*sizeof(float)),wet[2],dry[2];
    require(output!=NULL,"output allocation failed");
    for(unsigned i=0;i<rate*5;++i)
        ts_sunvox_process(s,input[2*(i%1024)],input[2*(i%1024)+1],wet,dry);
    double start=thread_seconds();
    for(size_t i=0;i<frames;++i) {
        float l=i<frames*2/3 ? input[2*(i%1024)] : 0;
        float r=i<frames*2/3 ? input[2*(i%1024)+1] : 0;
        ts_sunvox_process(s,l,r,wet,dry);
        output[2*i]=wet[0];output[2*i+1]=wet[1];
    }
    double elapsed=thread_seconds()-start,energy=0;
    uint64_t hash=UINT64_C(14695981039346656037);
    for(size_t i=0;i<frames*2;++i) {
        require(isfinite(output[i]),"nonfinite output");
        energy+=(double)output[i]*output[i];
    }
    for(size_t i=0;i<frames*2*sizeof(float);++i)
        hash=(hash^((unsigned char*)output)[i])*UINT64_C(1099511628211);
    require(energy>1e-6,"silent output");
    printf("rate=%u quantum=%u audio_seconds=%u cpu_percent=%.6f thread_seconds=%.6f rms=%.9f fnv64=%016llx",
        rate,q,seconds,elapsed/seconds*100,elapsed,sqrt(energy/(frames*2)),(unsigned long long)hash);
#ifdef TS_SUNVOX_BENCHMARK_SEED
    printf(" fixed_seed=1\n");
#else
    printf(" fixed_seed=0\n");
#endif
    if(argc==7) {
        FILE *file=fopen(argv[6],"wb");require(file!=NULL,"cannot open output file");
        require(fwrite(output,2*sizeof(float),frames,file)==frames,"cannot write output");
        require(!fclose(file),"cannot close output");
    }
    free(output);ts_sunvox_free(s);
#if defined(__SSE__) || defined(_M_X64)
    _mm_setcsr(saved);
#endif
    return 0;
}
