#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/matrix_effects.h"
#include "tapesister/matrix_modulation.h"
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static TsMatrixControls c,loaded;
static TsMatrixEffects fx,span;
static int realtime;
#ifdef TS_MOD_TEST_ALLOCATIONS
void *__real_malloc(size_t);void *__real_calloc(size_t,size_t);void *__real_realloc(void *,size_t);void __real_free(void *);
void *__wrap_malloc(size_t n){assert(!realtime);return __real_malloc(n);}
void *__wrap_calloc(size_t n,size_t s){assert(!realtime);return __real_calloc(n,s);}
void *__wrap_realloc(void *p,size_t n){assert(!realtime);return __real_realloc(p,n);}
void __wrap_free(void *p){assert(!realtime);__real_free(p);}
#endif
static void near(double a,double b,double e){if(fabs(a-b)>e)fprintf(stderr,"%.12g != %.12g (%.3g)\n",a,b,e);assert(fabs(a-b)<=e);}
static void curves(void)
{
    TsModControls a,b;ts_mod_default(&a);assert(ts_mod_valid(&a));b=a;
    ts_mod_shape(&a.lane[0],2,8);ts_mod_shape(&b.lane[0],2,8);
    assert(!memcmp(&a,&b,sizeof(a)));ts_mod_shape(&b.lane[0],2,8);assert(memcmp(&a,&b,sizeof(a)));
    TsModLane *l=&a.lane[0];assert(l->point[0].y==l->point[7].y);
    l->count=2;l->point[0]=(TsModPoint){0,0};l->point[1]=(TsModPoint){1,1};l->smooth=0;
    near(ts_mod_value(l,.25),.25,1e-7);l->smooth=1;near(ts_mod_value(l,.25),.15625,1e-7);
    for(int i=0;i<=1000;++i){float v=ts_mod_value(l,i/1000.);assert(v>=0 && v<=1);}
    TsModState s={0};l->seconds=2;ts_mod_advance(&s,&a,48000,48000,.5);near(s.phase[0],.5,1e-12);
    ts_mod_advance(&s,&a,48000,48000,.5);near(s.phase[0],0,1e-12);
    l->timing=TS_MOD_TEMPO;l->beats=4;ts_mod_advance(&s,&a,48000,48000,.25);near(s.phase[0],0,1e-12);
    l->mode=TS_MOD_ONESHOT;ts_mod_advance(&s,&a,96000,48000,.25);near(s.phase[0],1,1e-12);
    ++a.retrigger;ts_mod_advance(&s,&a,0,48000,.25);near(s.phase[0],0,1e-12);
    b=a;assert(ts_mod_read(&a,"Header","1,17,0")==-1 && !memcmp(&a,&b,sizeof(a)));
    assert(ts_mod_read(&a,"Header","1,1,0")==1 && !ts_mod_valid(&a));
    assert(ts_mod_read(&a,"End","1")==-1);assert(ts_mod_read(&a,"Lane0","1,0,0,2,nan,4,0,0")==-1);
}
static void setup(int board)
{
    ts_matrix_default(&c);c.enabled=1;
    assert(ts_matrix_effect_add(&c,TS_MATRIX_FX_MODULATOR)==0);
    assert(ts_matrix_effect_add(&c,board?TS_MATRIX_FX_PEDALBOARD:TS_SISTER_FX_TRANSIENT)==1);
    if(board){c.effect[1].board.slot[0].type=TS_SISTER_FX_TRANSIENT;c.effect[1].board.slot[0].enabled=1;}
    TsModControls *m=&c.effect[0].modulation;m->map_count=1;m->lane[0].seconds=.2f;
    m->map[0]=(TsModMap){.target=c.effect[1].id,.type=board?TS_SISTER_FX_TRANSIENT:TS_SISTER_FX_TRANSIENT,
        .child=board?c.effect[1].board.slot[0].id:0,.field=TS_MOD_GAIN,.enabled=1,.low=0,.high=1};
    assert(ts_matrix_valid(&c));
}
static void codec_and_identity(void)
{
    setup(1);TsModControls *m=&c.effect[0].modulation;
    ts_mod_shape(&m->lane[0],2,64);m->lane[1].mode=TS_MOD_ONESHOT;m->lane[1].timing=TS_MOD_TEMPO;m->lane[1].beats=7.5f;
    snprintf(m->lane[0].name,sizeof(m->lane[0].name),"SLOW WEATHER");
    FILE *f=tmpfile();assert(f && ts_matrix_write(f,&c));rewind(f);ts_matrix_default(&loaded);
    char line[4096];while(fgets(line,sizeof(line),f)) {line[strcspn(line,"\r\n")]=0;char *eq=strchr(line,'=');assert(eq);*eq++=0;assert(ts_matrix_read(&loaded,line,eq)==1);}
    fclose(f);assert(ts_matrix_valid(&loaded));
    assert(!memcmp(m,&loaded.effect[0].modulation,sizeof(*m)));
    assert(ts_mod_target(&c,&m->map[0])==1);
    TsSisterFxSlotControls tmp=c.effect[1].board.slot[0];c.effect[1].board.slot[0]=c.effect[1].board.slot[2];c.effect[1].board.slot[2]=tmp;
    assert(ts_mod_target(&c,&m->map[0])==1);assert(ts_mod_apply(&c.effect[1],&m->map[0],0));near(c.effect[1].board.slot[2].gain_db,-12,1e-6);near(c.effect[1].board.slot[0].gain_db,0,1e-6);
    c.effect[1].board.slot[2].type=TS_SISTER_FX_DELAY;assert(ts_mod_target(&c,&m->map[0])<0);
    c=loaded;assert(ts_matrix_connect(&c,TS_MATRIX_EFFECT_BASE,TS_MATRIX_EFFECT_BASE+1,1000)==-1);
    c.row[TS_MATRIX_EFFECT_BASE+1].matrix_gain[TS_MATRIX_EFFECT_BASE]=1000;assert(!ts_matrix_valid(&c));c=loaded;
    int second=ts_matrix_effect_add(&c,TS_MATRIX_FX_MODULATOR);assert(second==2);
    c.effect[second].modulation.map_count=1;c.effect[second].modulation.map[0]=c.effect[0].modulation.map[0];assert(!ts_matrix_valid(&c));
    c=loaded;ts_matrix_effect_remove(&c,1);assert(ts_matrix_valid(&c) && ts_mod_target(&c,&c.effect[0].modulation.map[0])<0);
    int again=ts_matrix_effect_add(&c,TS_MATRIX_FX_PEDALBOARD);assert(again==1 && ts_mod_target(&c,&c.effect[0].modulation.map[0])<0);
}
static TsStereoFrame sample(TsMatrixEffects *f,int target)
{
    realtime=1;
    ts_matrix_modulation_begin(f);
    TsStereoFrame out=ts_matrix_effects_process(f,target,(TsStereoFrame){.2f,.3f},(TsStereoFrame){0},1);
    ts_matrix_modulation_end(f,1);ts_matrix_effects_tick(f);realtime=0;return out;
}
static void commit(void)
{
    TsMatrixEffectPlan p;assert(ts_matrix_effects_stage(&fx,&c,48000,&p));
    ts_matrix_effects_commit(&fx,&c,48000,&p);ts_matrix_effects_discard(&p);
}
static void playback(int board)
{
    setup(board);ts_matrix_effects_init(&fx);ts_matrix_effects_init(&span);
    assert(ts_matrix_effects_restore(&fx,&c,48000));assert(ts_matrix_effects_restore(&span,&c,48000));
    /* The unconnected control card must not force every audio span to one frame. */
    ts_matrix_effects_suspend(&fx,1u);ts_matrix_effects_suspend(&span,1u);
    assert(ts_matrix_effects_span_limit(&span,512)==320);
    TsStereoFrame in[512],out[512];float smallest=1,largest=0;
    for(int i=0;i<512;++i)in[i]=(TsStereoFrame){.2f,.3f};
    for(unsigned total=0;total<48000;) {
        unsigned frames=ts_matrix_effects_span_limit(&span,TS_MATRIX_DELAY);if(total+frames>48000)frames=48000-total;
        realtime=1;ts_matrix_modulation_begin(&span);ts_matrix_effects_process_span(&span,1,in,NULL,out,frames,1);ts_matrix_modulation_end(&span,frames);realtime=0;
        for(unsigned i=0;i<frames;++i){TsStereoFrame v=sample(&fx,1);if(fabsf(v.l-out[i].l)>1e-6)fprintf(stderr,"frame=%u span=%u offset=%u\n",total+i,frames,i);near(v.l,out[i].l,1e-6);near(v.r,out[i].r,1e-6);if(total>2400){smallest=fminf(smallest,v.l);largest=fmaxf(largest,v.l);}}
        total+=frames;
    }
    assert(largest>smallest*3);near(c.effect[1].gain_db,0,1e-8);near(c.effect[1].board.slot[0].gain_db,0,1e-8);
    near(ts_matrix_modulation_phase(&fx,c.effect[0].id,0),ts_matrix_modulation_phase(&span,c.effect[0].id,0),1e-8);
    double phase=ts_matrix_modulation_phase(&fx,c.effect[0].id,0);
    ts_matrix_effects_suspend(&fx,3u);for(int i=0;i<480;++i)sample(&fx,1);near(ts_matrix_modulation_phase(&fx,c.effect[0].id,0),phase,1e-12);
    ts_matrix_effects_suspend(&fx,1u);c.effect[0].enabled=0;commit();
    TsStereoFrame restored={0};for(int i=0;i<24000;++i)restored=sample(&fx,1);near(restored.l,.2,2e-5);
    c.effect[0].enabled=1;c.effect[0].modulation.lane[0].mode=TS_MOD_ONESHOT;
    c.effect[0].modulation.lane[0].timing=TS_MOD_TEMPO;c.effect[0].modulation.lane[0].beats=1;++c.effect[0].modulation.retrigger;
    commit();fx.mod_quarter=.25;for(int i=0;i<6000;++i)sample(&fx,1);near(ts_matrix_modulation_phase(&fx,c.effect[0].id,0),.5,1e-9);
    for(int i=0;i<18000;++i)sample(&fx,1);near(ts_matrix_modulation_phase(&fx,c.effect[0].id,0),1,1e-12);
    c.effect[0].modulation.map[0].enabled=0;commit();for(int i=0;i<600;++i)sample(&fx,1);near(ts_matrix_modulation_phase(&fx,c.effect[0].id,0),1,1e-12);
    ts_matrix_effects_free(&fx);ts_matrix_effects_free(&span);
}
static void late_assignment(void)
{
    setup(0);c.effect[0].modulation.map_count=0;assert(ts_matrix_effects_restore(&fx,&c,48000));
    c.effect[0].modulation.map_count=1;c.effect[0].modulation.map[0]=(TsModMap){.target=c.effect[1].id,.type=TS_SISTER_FX_TRANSIENT,.field=TS_MOD_GAIN,.enabled=1,.low=0,.high=0};commit();
    TsStereoFrame v={0};for(int i=0;i<24000;++i)v=sample(&fx,1);near(v.l,.2*pow(10,-12*.05),1e-5);
    ts_matrix_effect_remove(&c,0);commit();for(int i=0;i<24000;++i)v=sample(&fx,1);near(v.l,.2,2e-5);ts_matrix_effects_free(&fx);
    setup(0);assert(ts_matrix_effects_restore(&fx,&c,48000));sample(&fx,1);
    ts_matrix_effect_remove(&c,0);ts_matrix_effect_remove(&c,1);commit();
    ts_matrix_modulation_begin(&fx);assert(!fx.mod_mask && !fx.mod_targets && !fx.mod_remaining);ts_matrix_effects_free(&fx);
}
static void benchmark(void)
{
    setup(1);c.effect[1].board.count=8;c.effect[0].modulation.map_count=8;
    for(int i=0;i<8;++i){c.effect[1].board.slot[i].type=TS_SISTER_FX_TRANSIENT;c.effect[0].modulation.map[i]=(TsModMap){.target=c.effect[1].id,.child=c.effect[1].board.slot[i].id,.type=TS_SISTER_FX_TRANSIENT,.field=TS_MOD_GAIN,.lane=(unsigned)i%4,.enabled=1,.low=.4f,.high=.6f};}
    TsStereoFrame input[512],output[512];for(int i=0;i<512;++i)input[i]=(TsStereoFrame){.1,.1};
    for(int mode=0;mode<2;++mode) {
        c.effect[0].enabled=mode;assert(ts_matrix_effects_restore(&fx,&c,48000));
        clock_t start=clock();unsigned total=48000*20;
        while(total){unsigned n=ts_matrix_effects_span_limit(&fx,TS_MATRIX_DELAY);if(n>total)n=total;ts_matrix_modulation_begin(&fx);ts_matrix_effects_process_span(&fx,1,input,NULL,output,n,1);ts_matrix_modulation_end(&fx,n);total-=n;}
        double secs=(double)(clock()-start)/CLOCKS_PER_SEC;
        printf("8 neutral pedals / 4 lanes / 8 maps, modulation %s: %.3f ms per 512 frames (%.3f%% one-core audio budget), memory %.1f KiB\n",mode?"ON":"OFF",secs/20*512/48000*1000,secs/20*100,ts_matrix_effects_memory(&fx)/1024.);
        ts_matrix_effects_free(&fx);
    }
}
int main(int argc,char **argv)
{
    (void)argv;curves();codec_and_identity();playback(0);playback(1);late_assignment();if(argc>1)benchmark();
    puts("Curves, random persistence, stable assignments, scalar/span playback, tempo, sleep and baseline restoration passed.");return 0;
}
