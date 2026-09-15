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
    const char *path="test-prism-zoya.ini";
    static TsConfig config,loaded,unchanged;
    char error[256],line[256];
    remove(path);
    ts_config_init(&config);
    assert(config.prism_zoya_pose==TS_PRISM_ZOYA_POSE_MEDITATION);
    ts_sister_ui_model_init(&model,NULL);
    assert(model.prism_zoya_pose==TS_PRISM_ZOYA_POSE_MEDITATION);
    assert(ts_config_load(&loaded,path,error,sizeof(error)));
    assert(loaded.prism_zoya_pose==TS_PRISM_ZOYA_POSE_MEDITATION);
    FILE *f=fopen(path,"wb");assert(f);
    fputs("[Sister Machine]\nsister_buffer_seconds=35\n",f);assert(!fclose(f));
    assert(ts_config_load(&loaded,path,error,sizeof(error)));
    assert(loaded.sister_buffer_seconds==35);
    assert(loaded.prism_zoya_pose==TS_PRISM_ZOYA_POSE_MEDITATION);
    static const char *const pose_lines[]={"prism_zoya_pose=meditation",
        "prism_zoya_pose=standing","prism_zoya_pose=off"};
    for(int pose=TS_PRISM_ZOYA_POSE_MEDITATION;pose<=TS_PRISM_ZOYA_POSE_OFF;++pose) {
        config.prism_zoya_pose=(TsPrismZoyaPose)pose;
        config.sister_buffer_seconds=45;
        assert(ts_config_save(&config,path,error,sizeof(error)));
        f=fopen(path,"rb");assert(f);int found=0;
        while(fgets(line,sizeof(line),f))
            if(strstr(line,pose_lines[pose]))found=1;
        assert(!fclose(f));assert(found);
        assert(ts_config_load(&loaded,path,error,sizeof(error)));
        assert(loaded.prism_zoya_pose==(TsPrismZoyaPose)pose);
        assert(loaded.sister_buffer_seconds==45);
        ts_sister_ui_model_init(&model,&loaded);
        assert(model.prism_zoya_pose==(TsPrismZoyaPose)pose);
        /* Ordinary INI saves must retain the choice along with other settings. */
        loaded.sister_buffer_seconds=50;
        assert(ts_config_save(&loaded,path,error,sizeof(error)));
        assert(ts_config_load(&loaded,path,error,sizeof(error)));
        assert(loaded.prism_zoya_pose==(TsPrismZoyaPose)pose);
        assert(loaded.sister_buffer_seconds==50);
    }
    f=fopen(path,"wb");assert(f);
    fputs("[Prism]\n prism_zoya_pose = standing \n",f);assert(!fclose(f));
    assert(ts_config_load(&loaded,path,error,sizeof(error)));
    assert(loaded.prism_zoya_pose==TS_PRISM_ZOYA_POSE_STANDING);
    unchanged=loaded;
    f=fopen(path,"wb");assert(f);
    fputs("[Prism]\nprism_zoya_pose=floating\n",f);assert(!fclose(f));
    assert(!ts_config_load(&loaded,path,error,sizeof(error)));
    assert(strstr(error,"meditation, standing or off"));
    assert(!memcmp(&loaded,&unchanged,sizeof(loaded)));
    remove(path);
    puts("Zoya INI: meditation defaults, all three saved options, UI initialization, whitespace and invalid-value handling passed.");
}
static void setup(TsPrismZoyaPose pose)
{
    TsConfig config;ts_config_init(&config);ts_palette_default(&palette);
    config.prism_zoya_pose=pose;
    ts_sister_ui_model_init(&model,&config);model.fx_page=3;model.prism_panel=1;
    model.parameters.prism.enabled=1;model.parameters.prism.lenses=24;
    model.parameters.prism.spread=1.3f;model.parameters.prism.drift=.7f;
    model.parameters.prism.color=.8f;model.parameters.prism.body=1;
    model.prism_selected=3;
    model.prism_hover_valid=1;model.prism_hover_x=55;model.prism_hover_y=140;
    snprintf(model.status,sizeof(model.status),"PRISM: THE SAME ENTITY THROUGH TRANSFORMATION");
    assert(ts_prism_prepare(&dsp,48000));ts_prism_set_controls(&dsp,&model.parameters.prism);
    snapshot(24000);
}
static int figure_area(int x,int y)
{
    return y>=108 && y<230 && ((x>=18 && x<122) || (x>=510 && x<622));
}
static void clock_checks(void)
{
    TsPrismZoyaVisual v={0};
    ts_sister_ui_prism_zoya_tick(&v,100,0,1);assert(!v.visible && !v.introducing);
    ts_sister_ui_prism_zoya_tick(&v,200,1,1);assert(v.visible && v.introducing && !v.elapsed_ms);
    ts_sister_ui_prism_zoya_tick(&v,1250,1,1);assert(v.elapsed_ms==1023);
    ts_sister_ui_prism_zoya_tick(&v,3800,1,1);assert(!v.introducing && v.elapsed_ms==TS_PRISM_ZOYA_INTRO_MS);
    TsPrismZoyaVisual settled=v;
    ts_sister_ui_prism_zoya_tick(&v,999999,1,1);assert(!memcmp(&settled,&v,sizeof(v)));
    ts_sister_ui_prism_zoya_tick(&v,1000000,1,0);assert(!v.visible && !v.introducing);
    ts_sister_ui_prism_zoya_tick(&v,1000100,1,1);assert(v.visible && !v.introducing);
    ts_sister_ui_prism_zoya_tick(&v,1000200,0,1);assert(!v.visible);
    ts_sister_ui_prism_zoya_tick(&v,1000300,1,1);assert(v.introducing && !v.elapsed_ms);
    ts_sister_ui_prism_zoya_tick(&v,1000400,1,0);assert(!v.introducing);
    ts_sister_ui_prism_zoya_tick(&v,1000500,1,1);assert(!v.introducing);
    /* Enabling behind another workspace must not queue a surprise introduction. */
    ts_sister_ui_prism_zoya_tick(&v,1000600,0,0);
    ts_sister_ui_prism_zoya_tick(&v,1000700,1,0);
    ts_sister_ui_prism_zoya_tick(&v,1000800,1,1);assert(!v.introducing);
    v=(TsPrismZoyaVisual){0};
    ts_sister_ui_prism_zoya_tick(&v,UINT32_MAX-100u,1,1);
    ts_sister_ui_prism_zoya_tick(&v,132,1,1);assert(v.introducing && v.elapsed_ms==231);
    puts("Zoya clock: on edge, no idle loop, hidden cancellation, no queued replay, and tick rollover passed.");
}
static void render_checks(void)
{
    model.prism_hover_valid=0;
    ts_sister_ui_render(&baseline,&model,&palette);
    ts_sister_ui_prism_zoya_tick(&model.prism_zoya,0,1,1);
    ts_sister_ui_prism_zoya_tick(&model.prism_zoya,TS_PRISM_ZOYA_INTRO_MS,1,1);
    TsSisterUiModel unchanged=model;
    ts_sister_ui_render(&frame,&model,&palette);
    assert(!memcmp(&model,&unchanged,sizeof(model)));
    int changed[2]={0};
    for(int y=0;y<TS_SISTER_UI_HEIGHT;++y)for(int x=0;x<TS_SISTER_UI_WIDTH;++x) {
        int i=y*TS_SISTER_UI_WIDTH+x;
        if(frame.pixels[i]!=baseline.pixels[i]) {
            assert(figure_area(x,y));++changed[x>320];
        }
    }
    assert(changed[0]>200 && changed[1]>200);
    previous=frame;
    ts_sister_ui_prism_zoya_tick(&model.prism_zoya,888888,1,1);
    ts_sister_ui_render(&frame,&model,&palette);assert(!memcmp(&frame,&previous,sizeof(frame)));
    char help[192];ts_sister_ui_prism_help(&model,55,140,help,sizeof(help));
    assert(strstr(help,"ZOYA:") && strstr(help,"lens state."));
    size_t split=strlen(help)>63?63:strlen(help);
    if(strlen(help)>63)while(split && help[split]!=' ')--split;
    assert(strlen(help)<=split || strlen(help+split+1)<=63);
    /* A real silent DSP stream still moves the output figure through Drift. */
    snapshot(48000*2);ts_sister_ui_render(&frame,&model,&palette);
    int moved=0;
    /* The head is beyond the recombined ray fan: these are figure pixels,
       not movement of the original optical drawing underneath. */
    for(int y=108;y<150;++y)for(int x=565;x<622;++x)
        moved+=frame.pixels[y*640+x]!=previous.pixels[y*640+x];
    assert(moved>20);
    /* The activation layer cannot touch UI rows, labels or footer. */
    model.prism_zoya.introducing=1;
    for(unsigned t=0;t<TS_PRISM_ZOYA_INTRO_MS;t+=165) {
        model.prism_zoya.visible=0;ts_sister_ui_render(&baseline,&model,&palette);
        model.prism_zoya.visible=1;model.prism_zoya.elapsed_ms=t;
        ts_sister_ui_render(&frame,&model,&palette);
        for(int y=0;y<400;++y)if(y<108 || y>=230)
            assert(!memcmp(frame.pixels+y*640,baseline.pixels+y*640,640*sizeof(uint32_t)));
    }
    const unsigned stages[]={900,1650,3300};
    for(int stage=0;stage<3;++stage) {
        model.prism_zoya.elapsed_ms=stages[stage];
        ts_sister_ui_render(&frame,&model,&palette);
        int source=0,output=0,packets=0;
        for(int y=108;y<230;++y)for(int x=18;x<622;++x) {
            if(frame.pixels[y*640+x]==baseline.pixels[y*640+x])continue;
            if(x<122)++source;
            if(x>=565 && y<150)++output;
            if(x>=180 && x<430)++packets;
        }
        if(stage==0)assert(source>100 && output==0 && packets==0);
        if(stage==1)assert(packets>2 && output==0);
        if(stage==2)assert(source>100 && output>50);
    }
    model.prism_zoya.visible=0;ts_sister_ui_render(&frame,&model,&palette);
    assert(!memcmp(&frame,&baseline,sizeof(frame)));
    puts("Zoya render: protected optics/controls, immutable model, stationary snapshots, silent DSP drift and bounded intro passed.");
}
static void off_checks(void)
{
    setup(TS_PRISM_ZOYA_POSE_OFF);
    assert(model.prism_zoya_pose==TS_PRISM_ZOYA_POSE_OFF);
    assert(model.parameters.prism.enabled);
    ts_sister_ui_prism_zoya_tick(&model.prism_zoya,100,1,0);
    assert(!model.prism_zoya.visible && !model.prism_zoya.introducing);
    TsPrismZoyaVisual stopped=model.prism_zoya;
    ts_sister_ui_prism_zoya_tick(&model.prism_zoya,999999,1,0);
    assert(!memcmp(&stopped,&model.prism_zoya,sizeof(stopped)));
    ts_sister_ui_render(&baseline,&model,&palette);
    /* Off remains authoritative even with stale/forced introduction flags. */
    model.prism_zoya.visible=1;model.prism_zoya.introducing=1;
    for(unsigned t=0;t<TS_PRISM_ZOYA_INTRO_MS;t+=165) {
        model.prism_zoya.elapsed_ms=t;
        TsSisterUiModel unchanged=model;
        ts_sister_ui_render(&frame,&model,&palette);
        assert(!memcmp(&frame,&baseline,sizeof(frame)));
        assert(!memcmp(&model,&unchanged,sizeof(model)));
    }
    model.prism_zoya.introducing=0;
    ts_sister_ui_render(&frame,&model,&palette);
    assert(!memcmp(&frame,&baseline,sizeof(frame)));
    char help[192];ts_sister_ui_prism_help(&model,55,140,help,sizeof(help));
    assert(!strstr(help,"ZOYA:"));
    assert(model.parameters.prism.enabled);
    ts_prism_free(&dsp);
    puts("Zoya off: no figures, activation packets or Zoya hover help; original Prism framebuffer and enabled state preserved.");
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
        double total=0;
        for(int n=-50;n<N;++n) {
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
        snapshot(1600);ts_sister_ui_prism_zoya_tick(&model.prism_zoya,(uint32_t)(f*1000/30),1,1);
        char name[32];snprintf(name,sizeof(name),"intro-%03d",f);write_frame(folder,name);
    }
    write_frame(folder,"settled");
    model.parameters.prism.spread=2;model.parameters.prism.drift=2;model.parameters.prism.color=1;
    ts_prism_set_controls(&dsp,&model.parameters.prism);snapshot(48000);
    write_frame(folder,"dispersed");
    model.parameters.prism.focus=1;
    ts_prism_set_controls(&dsp,&model.parameters.prism);snapshot(48000);
    write_frame(folder,"focused");
    model.parameters.prism.mix=0;
    ts_prism_set_controls(&dsp,&model.parameters.prism);snapshot(48000);
    write_frame(folder,"dry");
}
int main(int argc,char **argv)
{
    TsPrismZoyaPose pose=argc>1 && !strcmp(argv[argc-1],"--standing") ?
        TS_PRISM_ZOYA_POSE_STANDING : TS_PRISM_ZOYA_POSE_MEDITATION;
    if(argc>1 && !strcmp(argv[1],"--bench")) {
        setup(pose);benchmark();ts_prism_free(&dsp);
    } else if(argc>2 && !strcmp(argv[1],"--frames")) {
        setup(pose);frames(argv[2]);ts_prism_free(&dsp);
    } else {
        config_checks();clock_checks();
        for(int p=TS_PRISM_ZOYA_POSE_MEDITATION;p<=TS_PRISM_ZOYA_POSE_STANDING;++p) {
            printf("Checking %s pose\n",p==TS_PRISM_ZOYA_POSE_STANDING?"standing":"meditation");
            setup((TsPrismZoyaPose)p);render_checks();ts_prism_free(&dsp);
        }
        off_checks();
    }
    return 0;
}
