#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/sunvox_fx.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* GNU ld wrappers observe actual callback allocations, without logging or
   allocating inside the measurement. The same audio exercise runs on Windows. */
static int measuring;
static unsigned allocations;
#ifdef TS_WRAP_ALLOCATIONS
void *__real_malloc(size_t);void *__real_calloc(size_t,size_t);
void *__real_realloc(void *,size_t);void __real_free(void *);
void *__wrap_malloc(size_t n){if(measuring)++allocations;return __real_malloc(n);}
void *__wrap_calloc(size_t a,size_t b){if(measuring)++allocations;return __real_calloc(a,b);}
void *__wrap_realloc(void *p,size_t n){if(measuring)++allocations;return __real_realloc(p,n);}
void __wrap_free(void *p){if(measuring)++allocations;__real_free(p);}
#endif
static void block(FILE *f,const char *tag,const void *data,unsigned size) {
    unsigned char n[]={size&255,(size>>8)&255,(size>>16)&255,(size>>24)&255};
    assert(fwrite(tag,1,4,f)==4 && fwrite(n,1,4,f)==4);
    if(size)assert(fwrite(data,1,size,f)==size);
}
static TsSunVoxControls reverb(void) {
    FILE *f=fopen("sunvox-rt-test.sunsynth","wb");assert(f);
    assert(fwrite("SSYN\0\0\0\0",1,8,f)==8);
    block(f,"STYP","Reverb",7);block(f,"SEND",NULL,0);assert(!fclose(f));
    TsSunVoxControls c;assert(ts_sunvox_import(&c,"sunvox-rt-test.sunsynth"));
    remove("sunvox-rt-test.sunsynth");return c;
}
static double render(TsSunVoxEngine *s,unsigned frames,int tail,int dump) {
    double energy=0;allocations=0;
    for(unsigned i=0;i<frames;++i) {
        float wet[2],dry[2],l=tail?0:.1f*sinf(i*.013f),r=tail?0:.07f*cosf(i*.019f);
        measuring=1;ts_sunvox_process(s,l,r,wet,dry);measuring=0;
        assert(isfinite(wet[0]) && isfinite(wet[1]));
        energy+=(double)wet[0]*wet[0]+(double)wet[1]*wet[1];
        if(dump)assert(fwrite(wet,sizeof(float),2,stdout)==2);
    }
    if(!dump)assert(allocations==0);
    return energy;
}
int main(int argc,char **argv) {
    int dump=argc>1 && !strcmp(argv[1],"--render");
    unsigned rates[]={8000,44100,48000,96000,192000};
    for(unsigned r=0;r<sizeof(rates)/sizeof(*rates);++r) {
        TsSunVoxControls c=reverb();
        TsSunVoxEngine *s=ts_sunvox_new(&c,rates[r]);assert(s);
        render(s,rates[r],0,dump);
        /* Full burst, then room growth/shrink/growth with changing seed. This
           exercises capacity independently of logical length and retained tail. */
        for(int k=0;k<6;++k) {
            c.value[0]=0;c.value[1]=32768;c.value[2]=30000;
            c.value[8]=(k&1)?2048:32768;c.value[9]=k?32768:0;
            ts_sunvox_set(s,&c);render(s,4096,0,dump);
        }
        c.value[5]=32768;ts_sunvox_set(s,&c); /* Freeze: silence must retain tail. */
        assert(render(s,rates[r],1,dump)>1e-10);
        ts_sunvox_free(s);
    }
    if(dump)return 0;
    const char *names[]={"RAGINGCOAST Ensemble","echo2 Velvet Reverb 3","cube48 ShimmerClassic"};
    for(unsigned k=0;k<3;++k) {
        TsSunVoxControls c;int found=-1;
        for(int i=0;i<ts_sunvox_factory_count();++i)
            if(!strcmp(names[k],ts_sunvox_factory_name(i)))found=i;
        assert(found>=0 && ts_sunvox_select(&c,found));
        TsSunVoxEngine *s=ts_sunvox_new(&c,48000);assert(s);
        render(s,48000,0,0);c.value[0]=16384;ts_sunvox_set(s,&c);render(s,256,0,0);
        if(k==2){c.value[10]=32768;ts_sunvox_set(s,&c);render(s,256,0,0);}
        ts_sunvox_free(s);
    }
    puts("Reverb growth, seed changes, frozen tails and nested control events passed.");
}
