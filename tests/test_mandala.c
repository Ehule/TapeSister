#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/mandala.h"
#include "tapesister/sister_project_state.h"
#include "tapesister/config.h"
#include <assert.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define test_mkdir(path) _mkdir(path)
#define test_getcwd _getcwd
#define test_rmdir _rmdir
#else
#include <sys/stat.h>
#include <unistd.h>
#define test_mkdir(path) mkdir(path,0700)
#define test_getcwd getcwd
#define test_rmdir rmdir
#endif
static void portable_video_reference(void)
{
    char cwd[768],media[1024],expected[1024],error[256];int present=0;
    assert(test_getcwd(cwd,sizeof(cwd)) && !test_mkdir("mandala-portable"));
    snprintf(media,sizeof(media),"%s/mandala-portable/clip.mp4",cwd);
    FILE *f=fopen(media,"wb");assert(f);fclose(f);
    TsSisterProjectState *state=malloc(sizeof(*state)),*loaded=malloc(sizeof(*loaded));assert(state && loaded);
    ts_sister_project_state_init(state,48000);state->mandala.video_enabled=1;
    snprintf(state->mandala.video_path,sizeof(state->mandala.video_path),"%s",media);
    assert(ts_sister_project_state_save_file(state,"mandala-portable/state.ini",error,sizeof(error)));
    assert(!rename("mandala-portable","mandala-moved"));
    assert(ts_sister_project_state_load_file(loaded,"mandala-moved/state.ini",48000,&present,error,sizeof(error)) && present);
    snprintf(expected,sizeof(expected),"%s/mandala-moved/clip.mp4",cwd);
    for(char *p=expected;*p;++p)if(*p=='\\')*p='/';
    for(char *p=loaded->mandala.video_path;*p;++p)if(*p=='\\')*p='/';
    assert(!strcmp(expected,loaded->mandala.video_path) && loaded->mandala.video_enabled);
    remove("mandala-moved/clip.mp4");
    assert(ts_sister_project_state_load_file(loaded,"mandala-moved/state.ini",48000,&present,error,sizeof(error)));
    assert(loaded->mandala.video_enabled && strstr(loaded->mandala.video_path,"clip.mp4"));
    remove("mandala-moved/state.ini");assert(!test_rmdir("mandala-moved"));free(state);free(loaded);
}

static void wide_packet_bounds(void)
{
    TsMandalaQueue q;ts_mandala_queue_init(&q);atomic_store(&q.enabled,1);
    float *out=malloc(512*255*sizeof(float));assert(out);
    const unsigned widths[]={1,2,16,17,26,64,128,255},lengths[]={1,73,512};
    for(unsigned width=0;width<sizeof(widths)/sizeof(*widths);++width)
    for(unsigned length=0;length<sizeof(lengths)/sizeof(*lengths);++length) {
        unsigned channels=widths[width],frames=lengths[length];
        for(unsigned i=0;i<frames;++i)for(unsigned c=0;c<channels;++c)
            out[i*channels+c]=i*.001f+c*.000001f;
        ts_mandala_push(&q,out,frames,channels);
        TsMandalaPacket p;assert(ts_mandala_latest(&q,&p));
        assert(p.channels==channels && p.count>0 && p.count<=frames &&
               p.count<=64 && p.count*p.channels<=1024);
        for(unsigned i=0;i<p.count;++i) {
            unsigned source=(unsigned)lrintf(p.samples[i*channels]*1000);
            assert(source>=i*frames/p.count && source<(i+1)*frames/p.count);
            for(unsigned c=0;c<channels;++c)assert(p.samples[i*channels+c]==out[source*channels+c]);
        }
    }
    unsigned w=atomic_load(&q.write);
    ts_mandala_push(&q,out,512,0);ts_mandala_push(&q,out,512,256);
    assert(atomic_load(&q.write)==w);
    TsMandalaPacket invalid={.count=64,.channels=255};TsMandalaFeatures f={0};
    ts_mandala_analyze(&invalid,&f,.05f);assert(f.level==0);
    free(out);
}

int main(void)
{
    wide_packet_bounds();portable_video_reference();
    TsMandalaConfig a,b;
    ts_mandala_default(&a);ts_mandala_default(&b);
    assert(ts_mandala_valid(&a) && a.seed!=b.seed);
    uint64_t seed=a.seed;ts_mandala_reseed(&a);assert(a.seed!=seed);
    assert(ts_mandala_read(&a,"Mandala.Palette","6")==-1);
    assert(ts_mandala_read(&a,"Mandala.Symmetry","0")==-1);
    assert(ts_mandala_read(&a,"Mandala.Cycle","nan")==-1);
    assert(ts_mandala_read(&a,"Mandala.Seed","-1")==-1);
    assert(ts_mandala_read(&a,"Mandala.Version","6")==-1);
    assert(ts_mandala_read(&a,"Mandala.Future","1")==-1);
    assert(ts_mandala_read(&a,"Mandala.Shape","5")==-1);
    assert(ts_mandala_read(&a,"Mandala.Density","9")==-1);
    assert(ts_mandala_read(&a,"Mandala.Scale","151")==-1);
    assert(ts_mandala_read(&a,"Mandala.Trails","101")==-1);
    assert(ts_mandala_read(&a,"Mandala.Motion","-1")==-1);
    assert(ts_mandala_read(&a,"Mandala.Color0","1000000")==-1);
    assert(ts_mandala_read(&a,"Mandala.Color8","ABCDEF")==-1);
    assert(ts_mandala_read(&a,"Mandala.Color1","FFZZFF")==-1);
    assert(ts_mandala_read(&a,"Mandala.VideoPath","61003D")==-1);
    assert(ts_mandala_read(&a,"Mandala.VideoPath","1")==-1);
    assert(ts_mandala_read(&a,"Mandala.VideoPath","ZZ")==-1);
    assert(ts_mandala_read(&a,"Mandala.VideoMix","101")==-1);
    assert(ts_mandala_read(&a,"Mandala.VideoLoop","2")==-1);
    assert(ts_mandala_read(&a,"Mandala.VideoEnabled","-1")==-1);
    assert(ts_mandala_read(&a,"Mandala.VideoFilter","101")==-1);
    assert(ts_mandala_read(&a,"Mandala.VideoFilter","-1")==-1);
    a.video_filter=101;assert(!ts_mandala_valid(&a));a.video_filter=63;
    a.geometry_enabled=0;a.video_monochrome=1;
    a.video_enabled=1;a.video_loop=0;a.video_mix=91;a.video_rotation=32;a.video_scale=0;
    snprintf(a.video_path,sizeof(a.video_path),"/missing/clip = test \xC3\xA9.mp4");
    a.palette=3;uint32_t red=ts_mandala_color(&a,0);
    assert((red>>16)>(red&255));
    a.palette=4;uint32_t gold=ts_mandala_color(&a,0),violet=ts_mandala_color(&a,2);
    assert((gold>>16)>(gold&255) && (violet&255)>((violet>>8)&255));
    ts_mandala_set_color(&a,2,0x123456);assert(a.palette==TS_MANDALA_CUSTOM);
    assert(ts_mandala_color(&a,0)==gold && ts_mandala_color(&a,2)==0x123456);
    a.palette=0;a.palette=TS_MANDALA_CUSTOM;assert(ts_mandala_color(&a,2)==0x123456);
    TsSisterProjectState *state=malloc(sizeof(*state)),*loaded=malloc(sizeof(*loaded));
    assert(state && loaded);ts_sister_project_state_init(state,48000);
    state->mandala=a;state->mandala.palette=TS_MANDALA_CUSTOM;state->mandala.symmetry=12;state->mandala.cycle_seconds=1800;
    state->mandala.shape=1;state->mandala.density=31;state->mandala.scale=149;state->mandala.trails=87;state->mandala.motion=0;
    char error[256];int present=0;
    const char *path="mandala-state-test.ini";
    assert(ts_sister_project_state_save_file(state,path,error,sizeof(error)));
    assert(ts_sister_project_state_load_file(loaded,path,48000,&present,error,sizeof(error)) && present);
    assert(!memcmp(&state->mandala,&loaded->mandala,sizeof(a)));
    /* Worst-case path must survive the complete sidecar line reader. */
    memset(state->mandala.video_path,'x',sizeof(state->mandala.video_path)-1);
    state->mandala.video_path[0]='/';state->mandala.video_path[1023]=0;
    assert(ts_sister_project_state_save_file(state,path,error,sizeof(error)));
    assert(ts_sister_project_state_load_file(loaded,path,48000,&present,error,sizeof(error)));
    assert(!strcmp(state->mandala.video_path,loaded->mandala.video_path));
    /* Filename changes never change identity. */
    assert(!rename(path,"mandala-renamed-test.ini"));
    assert(ts_sister_project_state_load_file(loaded,"mandala-renamed-test.ini",48000,&present,error,sizeof(error)));
    assert(loaded->mandala.seed==a.seed);
    FILE *f=fopen("mandala-renamed-test.ini","ab");assert(f);fputs("Mandala.Version=999\n",f);fclose(f);
    assert(!ts_sister_project_state_load_file(loaded,"mandala-renamed-test.ini",48000,&present,error,sizeof(error)));
    assert(loaded->mandala.seed==a.seed);remove("mandala-renamed-test.ini");
    f=fopen(path,"wb");assert(f);fputs("TapeSister Sister Project State\nVersion=34\nPageCount=1\nActivePage=0\n",f);fclose(f);
    assert(ts_sister_project_state_load_file(loaded,path,48000,&present,error,sizeof(error)));
    assert(ts_mandala_valid(&loaded->mandala));seed=loaded->mandala.seed;
    assert(ts_sister_project_state_save_file(loaded,path,error,sizeof(error)));
    assert(ts_sister_project_state_load_file(loaded,path,48000,&present,error,sizeof(error)) && loaded->mandala.seed==seed);
    f=fopen(path,"wb");assert(f);fputs("TapeSister Sister Project State\nVersion=35\nPageCount=1\nActivePage=0\nMandala.Version=1\nMandala.Seed=314159\nMandala.Palette=2\nMandala.Symmetry=6\nMandala.Cycle=120\n",f);fclose(f);
    assert(ts_sister_project_state_load_file(loaded,path,48000,&present,error,sizeof(error)));
    assert(loaded->mandala.version==5 && loaded->mandala.seed==0x314159 && loaded->mandala.palette==2);
    assert(!loaded->mandala.shape && loaded->mandala.density==100 && loaded->mandala.scale==100 && loaded->mandala.motion==50 && loaded->mandala.trails==50);
    for(int version=2;version<=4;++version) {
        f=fopen(path,"wb");assert(f);fprintf(f,"TapeSister Sister Project State\nVersion=35\nPageCount=1\nActivePage=0\nMandala.Version=%d\nMandala.Seed=314159\nMandala.VideoScale=50\n",version);fclose(f);
        assert(ts_sister_project_state_load_file(loaded,path,48000,&present,error,sizeof(error)));
        assert(loaded->mandala.version==5 && loaded->mandala.geometry_enabled && !loaded->mandala.video_monochrome && !loaded->mandala.video_filter && loaded->mandala.video_scale==50);
    }
    remove(path);free(state);free(loaded);
    TsConfig prefs,again;ts_config_init(&prefs);
    assert(!prefs.visual_quality && !prefs.visual_fullscreen);
    prefs.visual_quality=2;prefs.visual_display=7;prefs.visual_fullscreen=1;
    assert(ts_config_save(&prefs,path,error,sizeof(error)));
    assert(ts_config_load(&again,path,error,sizeof(error)));
    assert(again.visual_quality==2 && again.visual_display==7 && again.visual_fullscreen==1);remove(path);

    TsMandalaQueue q;ts_mandala_queue_init(&q);assert(ts_mandala_lock_free(&q));
    float out[512*16];for(unsigned i=0;i<512*16;++i)out[i]=.25f;
    ts_mandala_push(&q,out,512,2);assert(!atomic_load(&q.write));
    atomic_store(&q.enabled,1);
    for(unsigned i=0;i<10000;++i)ts_mandala_push(&q,out,512,16);
    assert(atomic_load(&q.write)==4 && atomic_load(&q.dropped)==9996);
    TsMandalaPacket packet;assert(ts_mandala_latest(&q,&packet));assert(packet.count==64 && packet.channels==16);
    assert(!ts_mandala_latest(&q,&packet));
    TsMandalaFeatures features={0};
    for(int i=0;i<200;++i)ts_mandala_analyze(&packet,&features,.05f);
    assert(features.level>.85f && features.width==0 && features.balance==0);
    float multi=features.level;
    packet.channels=2;features=(TsMandalaFeatures){0};
    for(int i=0;i<200;++i)ts_mandala_analyze(&packet,&features,.05f);
    assert(fabsf(multi-features.level)<1e-6f);
    for(unsigned i=0;i<64;++i)packet.samples[i*packet.channels+1]=-.25f;
    for(int i=0;i<200;++i)ts_mandala_analyze(&packet,&features,.05f);
    assert(features.width>.99f && features.level>.85f); /* Anti-phase cannot cancel the mandala. */
    for(unsigned i=0;i<64;++i)packet.samples[i*packet.channels]=packet.samples[i*packet.channels+1]=.0005f;
    for(int i=0;i<800;++i)ts_mandala_analyze(&packet,&features,.05f);
    assert(features.level<.00001f);
    packet.samples[0]=NAN;packet.samples[1]=INFINITY;
    ts_mandala_analyze(&packet,&features,.05f);assert(isfinite(features.level));
    atomic_store(&q.enabled,0);unsigned w=atomic_load(&q.write);
    ts_mandala_push(&q,out,512,2);assert(atomic_load(&q.write)==w);
    /* Unsigned indices are safe across wraparound. */
    atomic_store(&q.write,UINT32_MAX-1);atomic_store(&q.read,UINT32_MAX-1);atomic_store(&q.enabled,1);
    for(int i=0;i<4;++i)ts_mandala_push(&q,out,512,2);
    assert(ts_mandala_latest(&q,&packet));
    /* A 26-channel ASIO device must feed the visualizer, including a signal
       present only beyond the former 16-channel ceiling. */
    float wide[73*26]={0};
    for(unsigned i=0;i<73;++i){wide[i*26+24]=.25f;wide[i*26+25]=-.25f;}
    ts_mandala_push(&q,wide,73,26);assert(ts_mandala_latest(&q,&packet));
    assert(packet.channels==26);
    features=(TsMandalaFeatures){0};
    for(int i=0;i<200;++i)ts_mandala_analyze(&packet,&features,.05f);
    assert(features.level>.4f && features.width>.99f && fabsf(features.balance)<1e-6f);
    TsMandalaEffects fx={0},target={0};
    for(int i=0;i<TS_MANDALA_EFFECTS;++i){target.wet[i]=1;target.size[i]=.8f;target.character[i]=.6f;}
    ts_mandala_effects_follow(&fx,&target,0);assert(fx.wet[0]==0);
    ts_mandala_effects_follow(&fx,&target,1.f/30);assert(fx.wet[0]>0 && fx.wet[0]<.1f);
    for(int i=0;i<200;++i)ts_mandala_effects_follow(&fx,&target,1.f/30);
    assert(fx.wet[0]>.999f && fabsf(fx.size[3]-.8f)<.001f);
    target.wet[0]=NAN;target.size[0]=INFINITY;target.character[0]=-1;
    for(int i=0;i<200;++i)ts_mandala_effects_follow(&fx,&target,1.f/30);
    assert(isfinite(fx.wet[0]) && fx.wet[0]<.001f && fx.size[0]<.001f);
    for(int i=0;i<200;++i)ts_mandala_effects_follow(&fx,NULL,1.f/30);
    for(int i=0;i<TS_MANDALA_EFFECTS;++i)assert(fx.wet[i]<.001f);
    puts("mandala identity, legacy migration, bounds, presentation, queue and analysis: PASS");
    return 0;
}
