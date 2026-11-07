#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/sunvox_fx.h"
#include "tapesister/matrix_effects.h"
#include "tapesister/sister_project_state.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void near(float a,float b) { assert(fabsf(a-b)<1e-5f); }
static void block(FILE *f,const char *id,const void *data,unsigned size) {
    unsigned char n[]={size&255,(size>>8)&255,(size>>16)&255,(size>>24)&255};
    assert(fwrite(id,1,4,f)==4 && fwrite(n,1,4,f)==4);
    if(size)assert(fwrite(data,1,size,f)==size);
}
static TsSunVoxControls amplifier(void) {
    /* A small independently specified patch is a useful numerical oracle:
       unity gain, stereo balance centered; no feedback or random sources. */
    FILE *f=fopen("sunvox-test.sunsynth","wb");assert(f);
    fwrite("SSYN\0\0\0\0",1,8,f);
    block(f,"STYP","Amplifier",10);block(f,"SNAM","Test amplifier",15);
    block(f,"SEND",NULL,0);assert(!fclose(f));
    TsSunVoxControls c;assert(ts_sunvox_import(&c,"sunvox-test.sunsynth"));
    remove("sunvox-test.sunsynth");return c;
}
static void catalog(void) {
    assert(ts_sunvox_factory_count()==235);
    for(int p=0;p<235;++p) {
        TsSunVoxControls c;assert(ts_sunvox_select(&c,p));
        TsSunVoxEngine *s=ts_sunvox_new(&c,48000);assert(s);
        for(int i=0;i<24000;++i) {
            float wet[2],dry[2];
            ts_sunvox_process(s,.03f*sinf(i*.041f),.02f*cosf(i*.031f),wet,dry);
            assert(isfinite(wet[0]) && isfinite(wet[1]));
        }
        ts_sunvox_free(s);
    }
    puts("All 235 factory patches load and render.");
}
static void stereo_controls(void) {
    unsigned rates[]={8000,44100,48000,96000,192000};
    for(unsigned r=0;r<sizeof(rates)/sizeof(*rates);++r) {
        TsSunVoxControls c=amplifier();
        TsSunVoxEngine *a=ts_sunvox_new(&c,rates[r]),*b=ts_sunvox_new(&c,rates[r]);assert(a && b);
        float wet[2],dry[2],other[2],odry[2];
        for(int i=0;i<1024;++i) {
            float l=i==0?.25f:0,rr=i==1?-.125f:0;
            ts_sunvox_process(a,l,rr,wet,dry);ts_sunvox_process(b,0,0,other,odry);
            near(wet[0],i==64?.25f:0);near(wet[1],i==65?-.125f:0);
            near(wet[0],dry[0]);near(wet[1],dry[1]);near(other[0],0);near(other[1],0);
        }
        c.value[0]=4096; /* volume 128 of 1024: half of unity */
        ts_sunvox_set(a,&c);
        for(int i=0;i<4096;++i)ts_sunvox_process(a,.25f,-.125f,wet,dry);
        near(wet[0],.125f);near(wet[1],-.0625f);
        c.value[3]=32768; /* enum control: inversion */
        ts_sunvox_set(a,&c);
        for(int i=0;i<4096;++i)ts_sunvox_process(a,.25f,-.125f,wet,dry);
        near(wet[0],-.125f);near(wet[1],.0625f);
        c.value[0]=c.value[3]=-1;ts_sunvox_set(a,&c);
        for(int i=0;i<4096;++i)ts_sunvox_process(a,.25f,-.125f,wet,dry);
        near(wet[0],.25f);near(wet[1],-.125f);
        ts_sunvox_free(a);ts_sunvox_free(b);
    }
    puts("Stereo isolation, 64-frame alignment, controls and reset passed at five rates.");
}
static TsRoutingMatrix graph;
static TsMatrixEffects engines;
static TsStereoFrame node(void *unused,int n,TsStereoFrame in) {
    (void)unused;int slot=ts_matrix_effect_index(n);
    return slot<0?in:ts_matrix_effects_process(&engines,slot,in,graph.sidechain,1);
}
static void matrix(void) {
    TsMatrixControls c;ts_matrix_default(&c);c.enabled=1;
    for(int i=0;i<TS_MATRIX_ROWS;++i)c.row[i]=(TsSourceRoute){.matrix_enabled=1};
    for(int i=0;i<2;++i){assert(ts_matrix_effect_add(&c,TS_MATRIX_FX_SUNVOX)==i);c.effect[i].sunvox=amplifier();}
    c.effect[0].sunvox.value[0]=4096;
    assert(ts_matrix_connect(&c,TS_MATRIX_NODES,TS_MATRIX_EFFECT_BASE,1000)==1);
    assert(ts_matrix_connect(&c,TS_MATRIX_EFFECT_BASE,TS_MATRIX_EFFECT_BASE+1,1000)==1);
    assert(ts_matrix_connect(&c,TS_MATRIX_EFFECT_BASE+1,ts_matrix_output_destination(0),1000)==1);
    ts_matrix_effects_init(&engines);assert(ts_matrix_effects_restore(&engines,&c,48000));
    ts_matrix_init(&graph);assert(ts_matrix_set(&graph,&c,48000));
    for(int f=0;f<2048;++f) {
        TsSourceRouteMix mix={0};TsStereoFrame live[TS_MATRIX_ROWS-TS_MATRIX_NODES]={0};
        live[0]=(TsStereoFrame){.2f,-.1f};ts_matrix_effects_tick(&engines);
        ts_matrix_process(&graph,&mix,live,node,NULL);
        if(f>1024){near(graph.output[0].l,.1f);near(graph.output[0].r,-.05f);}
    }
    TsMatrixEffectPlan plan;c.effect[0].enabled=0;
    assert(ts_matrix_effects_stage(&engines,&c,48000,&plan) && !plan.changed);
    ts_matrix_effects_commit(&engines,&c,48000,&plan);
    for(int f=0;f<1024;++f)ts_matrix_effects_process(&engines,0,(TsStereoFrame){.2f,-.1f},(TsStereoFrame){0},1);
    TsStereoFrame out=ts_matrix_effects_process(&engines,0,(TsStereoFrame){.2f,-.1f},(TsStereoFrame){0},1);
    near(out.l,.2f);near(out.r,-.1f);
    assert(ts_sunvox_select(&c.effect[0].sunvox,0));
    assert(ts_matrix_effects_stage(&engines,&c,48000,&plan) && plan.changed);
    ts_matrix_effects_commit(&engines,&c,48000,&plan);
    for(int f=0;f<1024;++f){ts_matrix_effects_tick(&engines);ts_matrix_effects_process(&engines,0,(TsStereoFrame){0},(TsStereoFrame){0},0);}
    ts_matrix_effects_collect(&engines,&plan);ts_matrix_effects_discard(&plan);
    assert(c.row[TS_MATRIX_EFFECT_BASE].matrix_gain[TS_MATRIX_EFFECT_BASE+1]==1000);
    ts_matrix_effects_free(&engines);
    puts("Serial Matrix routing, bypass, patch replacement and lifecycle passed.");
}
static void write_project(void) {
    static TsSisterProjectState s;char error[256];
    ts_sister_project_state_init(&s,48000);
    assert(ts_matrix_effect_add(&s.matrix,TS_MATRIX_FX_SUNVOX)==0);
    s.matrix.effect[0].sunvox=amplifier();s.matrix.effect[0].sunvox.value[0]=4096;
    assert(ts_matrix_effect_add(&s.matrix,TS_MATRIX_FX_SUNVOX)==1);
    assert(ts_sunvox_select(&s.matrix.effect[1].sunvox,234));
    assert(ts_sister_project_state_save_file(&s,"sunvox-project.tsr",error,sizeof(error)));
}
static void read_project(void) {
    static TsSisterProjectState s;char error[256];int present;
    assert(ts_sister_project_state_load_file(&s,"sunvox-project.tsr",48000,&present,error,sizeof(error)) && present);
    assert(!strcmp(ts_sunvox_name(&s.matrix.effect[0].sunvox),"Test amplifier"));
    assert(!strcmp(ts_sunvox_name(&s.matrix.effect[1].sunvox),ts_sunvox_factory_name(234)));
    assert(s.matrix.effect[0].sunvox.value[0]==4096);
    TsSunVoxEngine *a=ts_sunvox_new(&s.matrix.effect[0].sunvox,48000);assert(a);
    float wet[2],dry[2];for(int i=0;i<4096;++i)ts_sunvox_process(a,.2f,-.1f,wet,dry);
    near(wet[0],.1f);near(wet[1],-.05f);ts_sunvox_free(a);
    /* Even a previously cached custom resource cannot hide a truncated file. */
    FILE *in=fopen("sunvox-project.tsr","rb"),*out=fopen("sunvox-incomplete.tsr","wb");assert(in && out);
    char line[4096];while(fgets(line,sizeof(line),in))if(strncmp(line,"Matrix.SunVox0.Complete=",22))fputs(line,out);
    fclose(in);fclose(out);
    assert(!ts_sister_project_state_load_file(&s,"sunvox-incomplete.tsr",48000,&present,error,sizeof(error)));
    remove("sunvox-incomplete.tsr");
    puts("Factory and embedded custom project recall, plus truncation rejection, passed.");
}
int main(int argc,char **argv) {
    if(argc>2 && !strcmp(argv[1],"custom")) {
        TsSunVoxControls c;assert(ts_sunvox_import(&c,argv[2]));
        TsSunVoxEngine *s=ts_sunvox_new(&c,48000);assert(s);double energy=0,tail=0;float peak=0;
        for(int i=0;i<48000*15;++i) {
            float wet[2],dry[2];float input=i<48000*7?.08f*sinf(i*.041f)+(i%12000==0?.25f:0):0;
            ts_sunvox_process(s,input,input*.7f,wet,dry);assert(isfinite(wet[0]) && isfinite(wet[1]));
            energy+=wet[0]*wet[0]+wet[1]*wet[1];if(i>48000*8)tail+=wet[0]*wet[0]+wet[1]*wet[1];
            peak=fmaxf(peak,fmaxf(fabsf(wet[0]),fabsf(wet[1])));
        }
        assert(energy>1e-8);printf("Custom patch %s: energy %g, tail %g, peak %g\n",ts_sunvox_name(&c),energy,tail,peak);
        ts_sunvox_free(s);return 0;
    }
    if(argc>1 && !strcmp(argv[1],"write")){write_project();return 0;}
    if(argc>1 && !strcmp(argv[1],"read")){read_project();return 0;}
    stereo_controls();catalog();matrix();write_project();read_project();return 0;
}
