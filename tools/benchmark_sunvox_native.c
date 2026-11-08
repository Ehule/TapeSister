/* Matched DSP comparison against the official, separately supplied SunVox SDK.
   This measures the effects boundary, not the complete application or driver.
   The SDK header/library are downloaded and verified by run_sunvox_audit.py. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#include <dlfcn.h>
#else
#include <windows.h>
#endif
#define SUNVOX_MAIN
#include "sunvox.h"
#include "tapesister/sunvox_fx.h"
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <xmmintrin.h>

static double thread_seconds(void)
{
#ifdef _WIN32
    FILETIME created,exited,kernel,user;
    assert(GetThreadTimes(GetCurrentThread(),&created,&exited,&kernel,&user));
    ULARGE_INTEGER k,u;k.LowPart=kernel.dwLowDateTime;k.HighPart=kernel.dwHighDateTime;
    u.LowPart=user.dwLowDateTime;u.HighPart=user.dwHighDateTime;
    return (k.QuadPart+u.QuadPart)*1e-7;
#else
    struct timespec t;assert(!clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t));
    return t.tv_sec+t.tv_nsec*1e-9;
#endif
}
static const char *paths[]={"chorus/RAGINGCOAST Ensemble.sunsynth",
    "reverb/echo2 Velvet Reverb 3.sunsynth","reverb/cube48 ShimmerClassic.sunsynth"};
static const char *names[]={"RAGINGCOAST Ensemble","echo2 Velvet Reverb 3","cube48 ShimmerClassic"};

int main(int argc,char **argv)
{
    assert(argc==5);
    int q=atoi(argv[3]),seconds=atoi(argv[4]);
    assert((q==64 || q==256 || q==1024) && seconds>=20);
    assert(!sv_load_dll2(argv[1]));
    assert(sv_init(NULL,48000,2,SV_INIT_FLAG_AUDIO_FLOAT32|SV_INIT_FLAG_ONE_THREAD|
        SV_INIT_FLAG_USER_AUDIO_CALLBACK|SV_INIT_FLAG_NO_DEBUG_OUTPUT)>0);
    unsigned saved_mxcsr=_mm_getcsr();_mm_setcsr(saved_mxcsr|0x8040u);
    float input[2048],output[2048];
    for(int i=0;i<1024;i++){input[i*2]=.1f*sinf(i*.04f);input[i*2+1]=.1f*sinf(((i+240)%1024)*.04f);}
    for(int j=0;j<3;j++)for(int mode=0;mode<2;mode++) {
        TsSunVoxControls c;int factory=-1;
        for(int i=0;i<ts_sunvox_factory_count();i++)if(!strcmp(names[j],ts_sunvox_factory_name(i)))factory=i;
        assert(factory>=0 && ts_sunvox_select(&c,factory));c.quantum=q;
        if(j==0){c.value[7]=1800;c.value[10]=5600;c.value[11]=1800;}
        if(j==1)c.value[9]=18200;
        if(j==2){c.value[7]=775;c.value[10]=22016;c.value[11]=21000;}
        TsSunVoxEngine *engine=NULL;
        if(!mode){engine=ts_sunvox_new(&c,48000);assert(engine);}
        else {
            assert(!sv_open_slot(0));sv_volume(0,256);sv_lock_slot(0);
            char path[2048];snprintf(path,sizeof(path),"%s/%s",argv[2],paths[j]);
            int patch=sv_load_module(0,path,0,0,0),in=sv_new_module(0,"Input","Input",0,0,0);
            assert(patch>0 && in>0);
            assert(!sv_connect_module(0,in,patch));assert(!sv_connect_module(0,patch,0));
            sv_set_event_t(0,1,0);sv_set_module_ctl_value(0,in,1,1,0);
            for(int i=0;i<TS_SUNVOX_CONTROLS;i++)if(c.value[i]>=0) {
                int v=c.value[i];
                if(sv_get_module_ctl_type(0,patch,i)==1) {
                    int lo=sv_get_module_ctl_min(0,patch,i,0),hi=sv_get_module_ctl_max(0,patch,i,0);
                    v=lo+(int)((long long)v*(hi-lo)/32768);
                }
                sv_set_module_ctl_value(0,patch,i,v,1);
            }
            sv_unlock_slot(0);sv_update_input();
        }
        double started=0,energy=0;float wet[2],dry[2];
        int warmup=48000*10/q,count=48000*seconds/q;
        for(int b=0;b<warmup+count;b++) {
            if(b==warmup)started=thread_seconds();
            float *p=input+(b*q%1024)*2;
            if(mode)sv_audio_callback2(output,q,0,(uint32_t)((long long)b*q*sv_get_ticks_per_second()/48000),1,2,p);
            else for(int n=0;n<q;n++) {
                ts_sunvox_process(engine,p[2*n],p[2*n+1],wet,dry);
                output[2*n]=wet[0];output[2*n+1]=wet[1];
            }
            for(int n=0;n<q*2;n++){assert(isfinite(output[n]));if(b>=warmup)energy+=(double)output[n]*output[n];}
        }
        double elapsed=thread_seconds()-started;
        assert(energy>1e-6); /* A silent/miswired reference is not a comparison. */
        printf("{\"patch\":\"%s\",\"engine\":\"%s\",\"quantum\":%d,\"audio_seconds\":%.6f,\"cpu_percent\":%.6f,\"rms\":%.9g}\n",
            names[j],mode?"official-sdk":"tapesister",q,(double)count*q/48000,elapsed/((double)count*q/48000)*100,sqrt(energy/(count*q*2)));
        fflush(stdout);
        if(mode)sv_close_slot(0);else ts_sunvox_free(engine);
    }
    sv_deinit();sv_unload_dll();_mm_setcsr(saved_mxcsr);return 0;
}
