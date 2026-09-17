#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/sister_ui.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static TsFramebuffer baseline,frame,previous;
static TsSisterUiModel model;
static TsPalette palette;
static TsPrism dsp;

static void snapshot(unsigned frames)
{
    for(unsigned i=0;i<frames;++i)ts_prism_process(&dsp,(TsStereoFrame){0,0});
    model.routing.prism=ts_prism_view(&dsp);
}
static void config_checks(void)
{
    const char *path="test-prism-nebula.ini";
    static TsConfig config,loaded,unchanged;
    char error[256],line[256];
    remove(path);ts_config_init(&config);
    assert(config.prism_zoya_pose==TS_PRISM_ZOYA_POSE_NEBULA);
    ts_sister_ui_model_init(&model,NULL);
    assert(model.prism_zoya_pose==TS_PRISM_ZOYA_POSE_NEBULA);
    assert(ts_config_load(&loaded,path,error,sizeof(error)));
    assert(loaded.prism_zoya_pose==TS_PRISM_ZOYA_POSE_NEBULA);
    static const char *const values[]={"meditation","standing","off","nebula"};
    for(int i=0;i<4;++i) {
        FILE *f=fopen(path,"wb");assert(f);
        fprintf(f,"[Prism]\n prism_zoya_pose = %s \n",values[i]);assert(!fclose(f));
        assert(ts_config_load(&loaded,path,error,sizeof(error)));
        TsPrismZoyaPose expected=i==2?TS_PRISM_ZOYA_POSE_OFF:TS_PRISM_ZOYA_POSE_NEBULA;
        assert(loaded.prism_zoya_pose==expected);
        ts_sister_ui_model_init(&model,&loaded);assert(model.prism_zoya_pose==expected);
        loaded.sister_buffer_seconds=50;
        assert(ts_config_save(&loaded,path,error,sizeof(error)));
        f=fopen(path,"rb");assert(f);int found=0;
        while(fgets(line,sizeof(line),f))
            if(strstr(line,i==2?"prism_zoya_pose=off":"prism_zoya_pose=nebula"))found=1;
        assert(!fclose(f));assert(found);
        assert(ts_config_load(&loaded,path,error,sizeof(error)));
        assert(loaded.prism_zoya_pose==expected && loaded.sister_buffer_seconds==50);
    }
    unchanged=loaded;
    FILE *f=fopen(path,"wb");assert(f);
    fputs("[Prism]\nprism_zoya_pose=floating\n",f);assert(!fclose(f));
    assert(!ts_config_load(&loaded,path,error,sizeof(error)));
    assert(strstr(error,"nebula or off"));assert(!memcmp(&loaded,&unchanged,sizeof(loaded)));
    remove(path);
    puts("Nebula INI: default, legacy migration, off, saved preference and invalid-value handling passed.");
}
static void setup(TsPrismZoyaPose pose)
{
    TsConfig config;ts_config_init(&config);ts_palette_default(&palette);
    config.prism_zoya_pose=pose;
    ts_sister_ui_model_init(&model,&config);model.fx_page=3;model.prism_panel=0;
    model.parameters.prism.enabled=1;model.parameters.prism.lenses=24;
    model.parameters.prism.spread=1.3f;model.parameters.prism.drift=.7f;
    model.parameters.prism.color=.8f;model.parameters.prism.body=1;
    model.prism_selected=3;model.prism_hover_valid=0;
    snprintf(model.status,sizeof(model.status),"PRISM: REFRACT / RECOMBINE");
    assert(ts_prism_prepare(&dsp,48000));ts_prism_set_controls(&dsp,&model.parameters.prism);
    snapshot(24000);
}
static void clock_checks(void)
{
    TsPrismZoyaVisual v={0};
    ts_sister_ui_prism_zoya_tick(&v,100,0,1);assert(!v.visible && !v.introducing);
    ts_sister_ui_prism_zoya_tick(&v,200,1,1);assert(v.visible && v.introducing && !v.elapsed_ms);
    ts_sister_ui_prism_zoya_tick(&v,1250,1,1);assert(v.elapsed_ms==1023);
    ts_sister_ui_prism_zoya_tick(&v,3800,1,1);assert(!v.introducing);
    uint32_t age=v.ambient_ms;
    ts_sister_ui_prism_zoya_tick(&v,3833,1,1);assert(v.ambient_ms==age+33);
    TsPrismZoyaVisual same=v;
    ts_sister_ui_prism_zoya_tick(&v,3834,1,1);assert(!memcmp(&same,&v,sizeof(v)));
    ts_sister_ui_prism_zoya_tick(&v,3900,1,0);assert(!v.visible && !v.introducing);
    same=v;
    ts_sister_ui_prism_zoya_tick(&v,999999,1,0);assert(!memcmp(&same,&v,sizeof(v)));
    ts_sister_ui_prism_zoya_tick(&v,1000100,1,1);assert(v.visible && !v.introducing && v.ambient_ms==same.ambient_ms);
    ts_sister_ui_prism_zoya_tick(&v,1000200,0,1);
    ts_sister_ui_prism_zoya_tick(&v,1000300,1,1);assert(v.introducing && !v.elapsed_ms);
    ts_sister_ui_prism_zoya_tick(&v,1000400,1,0);
    ts_sister_ui_prism_zoya_tick(&v,1000500,1,1);assert(!v.introducing);
    ts_sister_ui_prism_zoya_tick(&v,1000600,0,0);
    ts_sister_ui_prism_zoya_tick(&v,1000700,1,0);
    ts_sister_ui_prism_zoya_tick(&v,1000800,1,1);assert(!v.introducing);
    v=(TsPrismZoyaVisual){0};
    ts_sister_ui_prism_zoya_tick(&v,UINT32_MAX-100u,1,1);
    ts_sister_ui_prism_zoya_tick(&v,132,1,1);assert(v.introducing && v.elapsed_ms==231);
    puts("Nebula clock: activation, bounded idle, hidden pause, no queued replay and tick rollover passed.");
}
static void style_checks(void)
{
    TsPrismControls *p=&model.parameters.prism;
    TsPrismControls saved=*p;
    p->spread=.3f;p->focus=.1f;ts_prism_capture(p,0);
    p->spread=1.9f;p->focus=.9f;ts_prism_capture(p,1);
    p->morph_enabled=1;model.routing.prism.valid=1;model.routing.prism.morph=.25f;
    ts_sister_ui_prism_nebula_update(&model,0,1,NULL,0);
    assert(fabsf(model.prism_zoya.spread-.7f)<1e-5f);
    assert(fabsf(model.prism_zoya.focus-.3f)<1e-5f);
    TsPrismPatch frozen[2]={p->a,p->b};
    model.routing.prism.matrix_morph=.75f;
    /* A Matrix pair is frozen by the audio engine; live edits cannot replace it. */
    p->spread=0;p->focus=0;p->a.spread=0;p->b.spread=0;
    ts_sister_ui_prism_nebula_update(&model,33,1,frozen,.75f);
    assert(fabsf(model.prism_zoya.spread-1.5f)<1e-5f);
    assert(fabsf(model.prism_zoya.focus-.7f)<1e-5f);
    *p=saved;model.prism_zoya=(TsPrismZoyaVisual){0};
    p->focus=1;p->spread=p->drift=p->dry_level=0;p->mix=1;
    float maximum=0;
    for(uint32_t t=0;t<22000;t+=33) {
        ts_sister_ui_prism_nebula_update(&model,t,1,NULL,0);
        if(t<6500)assert(model.prism_zoya.apparition==0);
        if(model.prism_zoya.apparition>maximum)maximum=model.prism_zoya.apparition;
    }
    assert(maximum==1 && model.prism_zoya.apparition==0 && model.prism_zoya.apparition_spent);
    p->spread=.5f;ts_sister_ui_prism_nebula_update(&model,22000,1,NULL,0);
    assert(!model.prism_zoya.apparition_spent && !model.prism_zoya.coherent_ms);
    p->spread=0;
    for(uint32_t t=22033;t<27000;t+=33)ts_sister_ui_prism_nebula_update(&model,t,1,NULL,0);
    assert(model.prism_zoya.apparition>.5f);
    p->focus=.2f;ts_sister_ui_prism_nebula_update(&model,27000,1,NULL,0);
    assert(model.prism_zoya.apparition>0); /* Gentle departure, not a hard cut. */
    for(uint32_t t=27033;t<28000;t+=33)ts_sister_ui_prism_nebula_update(&model,t,1,NULL,0);
    assert(model.prism_zoya.apparition==0);
    *p=saved;model.prism_zoya=(TsPrismZoyaVisual){0};
    puts("Nebula response: live A/B, frozen Matrix pair, delayed temporary apparition and rearming passed.");
}
static void render_checks(void)
{
    ts_sister_ui_render(&baseline,&model,&palette);
    ts_sister_ui_prism_nebula_update(&model,0,1,NULL,0);
    ts_sister_ui_prism_nebula_update(&model,4000,1,NULL,0);
    TsSisterUiModel unchanged=model;
    ts_sister_ui_render(&frame,&model,&palette);
    assert(!memcmp(&model,&unchanged,sizeof(model)));
    int changed[2]={0},warm=0,cool=0,pink=0;
    for(int y=0;y<400;++y)for(int x=0;x<640;++x) {
        int i=y*640+x;uint32_t c=frame.pixels[i];
        if(c!=baseline.pixels[i]) {
            assert(y>=108 && y<229 && x>=18 && x<622);
            /* Every pre-existing optical/label pixel is drawn over the field. */
            assert(baseline.pixels[i]==0xff070b0fu);
            ++changed[x>320];
            int r=(c>>16)&255,g=(c>>8)&255,b=c&255;
            if(x<128 && r>g*1.3f && g>b*1.3f && r>45)++warm;
            if(x>490 && b>r*1.3f && b>60)++cool;
            if(x>490 && r>g*1.3f && b>g*1.3f && r>50)++pink;
        }
    }
    assert(changed[0]>1000 && changed[1]>1000 && warm>300 && cool>150 && pink>80);
    previous=frame;
    ts_sister_ui_render(&frame,&model,&palette);assert(!memcmp(&frame,&previous,sizeof(frame)));
    for(uint32_t t=4033;t<6200;t+=33)ts_sister_ui_prism_nebula_update(&model,t,1,NULL,0);
    ts_sister_ui_render(&frame,&model,&palette);assert(memcmp(&frame,&previous,sizeof(frame)));
    char help[192];ts_sister_ui_prism_help(&model,55,140,help,sizeof(help));
    assert(strstr(help,"NEBULA:"));
    size_t split=strlen(help)>63?63:strlen(help);
    if(strlen(help)>63)while(split && help[split]!=' ')--split;
    assert(strlen(help)<=split || strlen(help+split+1)<=63);
    model.prism_zoya.introducing=1;
    for(unsigned t=0;t<TS_PRISM_ZOYA_INTRO_MS;t+=165) {
        model.prism_zoya.elapsed_ms=t;ts_sister_ui_render(&frame,&model,&palette);
        for(int y=0;y<400;++y)if(y<108 || y>=229)
            assert(!memcmp(frame.pixels+y*640,baseline.pixels+y*640,640*sizeof(uint32_t)));
    }
    const unsigned stages[]={900,1650,3300};
    for(int stage=0;stage<3;++stage) {
        model.prism_zoya.elapsed_ms=stages[stage];ts_sister_ui_render(&frame,&model,&palette);
        int source=0,output=0,packets=0;
        for(int y=108;y<229;++y)for(int x=18;x<622;++x) {
            if(frame.pixels[y*640+x]==baseline.pixels[y*640+x])continue;
            if(x<122)++source;
            if(x>=565 && y<150)++output;
            if(x>=180 && x<430)++packets;
        }
        if(stage==0)assert(source>100 && output==0 && packets==0);
        if(stage==1)assert(packets>2 && output==0);
        if(stage==2)assert(source>100 && output>50);
    }
    model.prism_zoya.introducing=0;model.prism_zoya.apparition=1;
    ts_sister_ui_render(&frame,&model,&palette);
    for(int y=0;y<400;++y)for(int x=0;x<640;++x) {
        int i=y*640+x;
        if(baseline.pixels[i]!=0xff070b0fu)assert(frame.pixels[i]==baseline.pixels[i]);
    }
    model.prism_zoya.visible=0;ts_sister_ui_render(&frame,&model,&palette);
    assert(!memcmp(&frame,&baseline,sizeof(frame)));
    puts("Nebula render: distinct warm/cool/pink fields, immutable rendering, protected optics/controls, idle and activation passed.");
}
static void off_checks(void)
{
    setup(TS_PRISM_ZOYA_POSE_OFF);
    ts_sister_ui_prism_nebula_update(&model,100,1,NULL,0);
    assert(!model.prism_zoya.visible && !model.prism_zoya.introducing);
    TsPrismZoyaVisual stopped=model.prism_zoya;
    ts_sister_ui_prism_nebula_update(&model,999999,1,NULL,0);
    assert(!memcmp(&stopped,&model.prism_zoya,sizeof(stopped)));
    ts_sister_ui_render(&baseline,&model,&palette);
    model.prism_zoya.visible=1;model.prism_zoya.introducing=1;model.prism_zoya.apparition=1;
    for(unsigned t=0;t<TS_PRISM_ZOYA_INTRO_MS;t+=165) {
        model.prism_zoya.elapsed_ms=t;ts_sister_ui_render(&frame,&model,&palette);
        assert(!memcmp(&frame,&baseline,sizeof(frame)));
    }
    char help[192];ts_sister_ui_prism_help(&model,55,140,help,sizeof(help));
    assert(!strstr(help,"NEBULA:"));assert(model.parameters.prism.enabled);
    ts_prism_free(&dsp);
    puts("Nebula off: exact optics-only framebuffer, no animation/hover help, audio controls preserved.");
}
static double now(void)
{
    struct timespec t;timespec_get(&t,TIME_UTC);return t.tv_sec+t.tv_nsec*1e-9;
}
static int compare(const void *a,const void *b)
{
    double x=*(const double *)a,y=*(const double *)b;return (x>y)-(x<y);
}
static void benchmark(void)
{
    enum { N=1500 };double times[N];
    static const char *const names[]={"optics-only","settled","wide-drift-color","introduction"};
    model.prism_hover_valid=0;
    for(int trial=0;trial<4;++trial) {
        model.prism_zoya.visible=trial!=0;model.prism_zoya.introducing=trial==3;
        model.prism_zoya.elapsed_ms=2200;
        if(trial>=2) {
            model.parameters.prism.spread=2;model.parameters.prism.drift=2;
            model.parameters.prism.body=3;model.parameters.prism.color=1;
            ts_prism_set_controls(&dsp,&model.parameters.prism);snapshot(24000);
        }
        ts_sister_ui_prism_nebula_update(&model,4000+trial*4000,1,NULL,0);
        model.prism_zoya.visible=trial!=0;model.prism_zoya.introducing=trial==3;
        double total=0;
        for(int n=-50;n<N;++n) {
            model.prism_zoya.ambient_ms=(uint32_t)((n+50)*33);
            if(trial==3)model.prism_zoya.elapsed_ms=(uint32_t)(((n+50)%110)*33);
            double start=now();ts_sister_ui_render(&frame,&model,&palette);
            double duration=now()-start;
            if(n>=0)total+=times[n]=duration;
        }
        qsort(times,N,sizeof(*times),compare);
        printf("%s: mean_us=%.2f p99_us=%.2f max_us=%.2f\n",names[trial],total/N*1e6,times[(N*99)/100]*1e6,times[N-1]*1e6);
    }
}
static void write_frame(const char *folder,const char *name)
{
    char path[1024];snprintf(path,sizeof(path),"%s/%s.ppm",folder,name);
    ts_sister_ui_render(&frame,&model,&palette);assert(ts_ui_write_ppm(&frame,path));
}
static void frames(const char *folder)
{
    model.prism_zoya=(TsPrismZoyaVisual){0};
    for(int f=0;f<150;++f) {
        snapshot(1600);ts_sister_ui_prism_nebula_update(&model,(uint32_t)(f*1000/30),1,NULL,0);
        char name[32];snprintf(name,sizeof(name),"intro-%03d",f);write_frame(folder,name);
    }
    write_frame(folder,"settled");
    model.parameters.prism.spread=2;model.parameters.prism.drift=2;model.parameters.prism.color=1;
    ts_prism_set_controls(&dsp,&model.parameters.prism);snapshot(48000);
    ts_sister_ui_prism_nebula_update(&model,5033,1,NULL,0);
    write_frame(folder,"dispersed");
    model.parameters.prism.focus=1;
    ts_prism_set_controls(&dsp,&model.parameters.prism);snapshot(48000);
    ts_sister_ui_prism_nebula_update(&model,5066,1,NULL,0);
    write_frame(folder,"focused");
    model.parameters.prism.mix=0;
    ts_prism_set_controls(&dsp,&model.parameters.prism);snapshot(48000);
    ts_sister_ui_prism_nebula_update(&model,5099,1,NULL,0);
    write_frame(folder,"dry");
    model.parameters.prism.spread=model.parameters.prism.drift=0;
    model.parameters.prism.dry_level=0;model.parameters.prism.mix=1;
    ts_prism_set_controls(&dsp,&model.parameters.prism);snapshot(48000);
    for(uint32_t t=5132;t<10150;t+=33)
        ts_sister_ui_prism_nebula_update(&model,t,1,NULL,0);
    assert(model.prism_zoya.apparition>.99f);
    write_frame(folder,"apparition");
}
int main(int argc,char **argv)
{
    if(argc>1 && !strcmp(argv[1],"--bench")) {
        setup(TS_PRISM_ZOYA_POSE_NEBULA);benchmark();ts_prism_free(&dsp);
    } else if(argc>2 && !strcmp(argv[1],"--frames")) {
        setup(TS_PRISM_ZOYA_POSE_NEBULA);frames(argv[2]);ts_prism_free(&dsp);
    } else {
        config_checks();clock_checks();setup(TS_PRISM_ZOYA_POSE_NEBULA);
        style_checks();render_checks();ts_prism_free(&dsp);off_checks();
    }
    return 0;
}
