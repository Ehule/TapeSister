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

static double now(void)
{
    struct timespec t; timespec_get(&t, TIME_UTC);
    return t.tv_sec + t.tv_nsec * 1e-9;
}
static int compare(const void *a, const void *b)
{
    double x=*(const double *)a, y=*(const double *)b;
    return (x > y) - (x < y);
}
/* Same-host before/after compatibility check for the original voice counts. */
static void signatures(void)
{
    for(int count=2;count<=12;count+=2)for(int mode=0;mode<2;++mode) {
        TsPrism p={0};assert(ts_prism_prepare(&p,48000));TsPrismControls c;
        ts_prism_controls_default(&c);c.enabled=1;c.lenses=count;c.mode=mode;
        c.spread=1.2f;c.drift=1.1f;c.input_shape=2;c.output_shape=5;c.color=.75f;
        ts_prism_set_controls(&p,&c);uint64_t hash=1469598103934665603ull;
        for(int n=0;n<48000;++n) {
            TsStereoFrame y=ts_prism_process(&p,(TsStereoFrame){.3f*sinf(n*.023f),.2f*cosf(n*.057f)});
            uint32_t bits[2];memcpy(bits,&y,sizeof(bits));
            hash=(hash^bits[0])*1099511628211ull;hash=(hash^bits[1])*1099511628211ull;
        }
        printf("count=%d mode=%d hash=%016llx\n",count,mode,(unsigned long long)hash);
        ts_prism_free(&p);
    }
}
static void benchmark(int colored)
{
    enum { BLOCKS=4000, FRAMES=256 };
    static double times[BLOCKS];
    const int counts[] = {0,2,4,6,8,12,16,20,24};
    for (int full=0; full<2; ++full) for (int k=0; k<(int)(sizeof(counts)/sizeof(counts[0])); ++k) {
        TsSisterRuntime *r=calloc(1,sizeof(*r)); assert(r);
        ts_sister_runtime_init(r);
        char error[160];
        assert(ts_sister_runtime_enable(r,48000,2,2,5,error,sizeof(error)));
        TsSisterParameters c=r->parameters;
        c.prism.enabled=counts[k]!=0; c.prism.lenses=counts[k]?counts[k]:12;
        c.prism.mix=1; c.prism.drift=1; c.monitor_dry=1; c.monitor_wet=0;
        if(colored) {c.prism.input_shape=TS_PRISM_MENISCUS_POSITIVE;c.prism.output_shape=TS_PRISM_MENISCUS_NEGATIVE;c.prism.color=1;}
        ts_sister_runtime_set_parameters(r,&c);
        ts_sister_runtime_set_sources(r,TS_SISTER_SOURCE_EXT);
        ts_sister_runtime_set_monitor(r,1);
        TsStereoFrame input[FRAMES];
        for (int i=0; i<FRAMES; ++i) input[i]=(TsStereoFrame){.25f*sinf(i*.1f),.18f*cosf(i*.073f)};
        double checksum=0,total=0;
        for (int b=-100; b<BLOCKS; ++b) {
            double start=now();
            if (full) ts_sister_runtime_begin_audio_block(r);
            for (int i=0; i<FRAMES; ++i) {
                TsStereoFrame out;
                if (full) {
                    TsSisterSourceFrames sources={0}; sources.external=input[i];
                    out=ts_sister_runtime_process_frame(r,&sources).monitor_return;
                    out=ts_sister_runtime_process_output(r,out);
                } else out=ts_prism_process(&r->prism,input[i]);
                checksum+=out.l;
            }
            if (full) ts_sister_runtime_end_audio_block(r);
            double elapsed=now()-start;
            if (b>=0) total+=times[b]=elapsed;
        }
        qsort(times,BLOCKS,sizeof(*times),compare);
        printf("%s,%d,mean_us=%.2f,p99_us=%.2f,max_us=%.2f,budget_percent=%.2f,check=%.2f\n",
            full?"sister+prism+limiter":"prism",counts[k],total/BLOCKS*1e6,
            times[(int)(BLOCKS*.99)]*1e6,times[BLOCKS-1]*1e6,
            total/BLOCKS/(FRAMES/48000.)*100,checksum);
        ts_sister_runtime_free(r); free(r);
    }
}

/* Reproduce the reported wide-spread patch from audio-owned snapshots, without
   a UI animation clock. Optional prefix writes a 15 fps native-frame sequence. */
static void motion_probe(const char *prefix,float drift,float focus,int lenses)
{
    if(lenses<2)lenses=2;
    if(lenses>TS_PRISM_LENSES)lenses=TS_PRISM_LENSES;
    TsConfig config;TsPalette palette;TsSisterUiModel model;static TsFramebuffer fb;
    TsSisterRuntime *r=calloc(1,sizeof(*r));assert(r);char error[160];
    ts_config_init(&config);ts_palette_default(&palette);ts_sister_ui_model_init(&model,&config);
    ts_sister_runtime_init(r);assert(ts_sister_runtime_reconfigure(r,48000,2,error,sizeof(error)));
    TsSisterParameters p=r->parameters;
    p.prism.enabled=1;p.prism.lenses=lenses;p.prism.spread=2;p.prism.drift=drift;p.prism.focus=focus;
    p.prism.stereo=1;p.prism.body=.9f;p.prism.mix=1;p.prism.dry_level=.6f;
    p.prism.input_shape=TS_PRISM_PLANO_CONVEX;p.prism.output_shape=TS_PRISM_MENISCUS_NEGATIVE;p.prism.color=.95f;
    ts_sister_runtime_set_parameters(r,&p);model.fx_page=3;
    snprintf(model.status,sizeof(model.status),"DRIFT: LIVE AUDIO SNAPSHOTS / NO MOUSE INPUT");
    float lo[TS_PRISM_LENSES],hi[TS_PRISM_LENSES];for(int i=0;i<TS_PRISM_LENSES;++i){lo[i]=1000;hi[i]=-1000;}
    for(int frame=-30;frame<180;++frame) {
        ts_sister_runtime_begin_audio_block(r);
        for(int n=0;n<3200;++n) {
            float x=.2f*sinf((frame*3200+n)*.031f);
            ts_sister_runtime_process_ordinary_post_fx(r,(TsStereoFrame){x,-x});
        }
        ts_sister_runtime_end_audio_block(r);
        TsSisterRoutingSnapshot snapshot;assert(ts_sister_runtime_get_snapshot(r,&snapshot));
        ts_sister_ui_model_update(&model,&snapshot,NULL,NULL,&p);
        if(frame<0)continue;
        for(int i=0;i<TS_PRISM_LENSES;++i) {
            float x,y;ts_sister_ui_prism_point_f(snapshot.prism.lens[i],&x,&y);
            lo[i]=fminf(lo[i],y);hi[i]=fmaxf(hi[i],y);
        }
        if(prefix) {
            char path[1024];snprintf(path,sizeof(path),"%s-%03d.ppm",prefix,frame);
            ts_sister_ui_render(&fb,&model,&palette);assert(ts_ui_write_ppm(&fb,path));
        }
    }
    for(int i=0;i<lenses;++i)printf("lens=%02d travel_native_pixels=%.3f\n",i+1,hi[i]-lo[i]);
    ts_sister_runtime_free(r);free(r);
}

int main(int argc,char **argv)
{
    if(argc>1 && !strcmp(argv[1],"--signatures")){signatures();return 0;}
    if (argc>1 && !strcmp(argv[1],"--bench")) {benchmark(argc>2);return 0;}
    if (argc>1 && !strcmp(argv[1],"--motion")) {
        motion_probe(argc>2?argv[2]:NULL,argc>3?(float)atof(argv[3]):2,argc>4?(float)atof(argv[4]):.6f,argc>5?atoi(argv[5]):12);return 0;
    }
    TsConfig config; TsPalette palette; TsSisterUiModel model;
    static TsFramebuffer fb;
    ts_config_init(&config); ts_palette_default(&palette);
    ts_sister_ui_model_init(&model,&config);
    model.fx_page=3; model.parameters.prism.enabled=1;
    if (argc>2) model.parameters.prism.lenses=atoi(argv[2]);
    if (argc>3) model.parameters.prism.mode=atoi(argv[3]);
    if (argc>4) model.parameters.prism.focus=(float)atof(argv[4]);
    if (argc>5) model.parameters.prism.mix=(float)atof(argv[5]);
    if (argc>6 && !strcmp(argv[6],"custom")) {
        model.parameters.prism.pitch_offset[2]=700;
        model.parameters.prism.pan_offset[2]=.5f;
        model.parameters.prism.pitch_offset[5]=-350;
        model.parameters.prism.pan_offset[5]=-.5f;
        model.parameters.prism.pitch_offset[8]=-20;
        model.prism_selected=3;
    }
    if (argc>6 && !strcmp(argv[6],"mixer")) {
        model.parameters.prism.pitch_offset[2]=700;
        model.parameters.prism.pan_offset[2]=.5f;
        model.parameters.prism.trim_db[2]=9;
        model.parameters.prism.trim_db[8]=-12;
        model.parameters.prism.mute_mask=1 << 5;
        model.parameters.prism.dry_level=1.5f;
        model.prism_selected=9;
    }
    model.routing.limiter_enabled=1; model.routing.master_output_gain=1;
    if(argc>7)model.parameters.prism.input_shape=atoi(argv[7]);
    if(argc>8)model.parameters.prism.output_shape=atoi(argv[8]);
    if(argc>9)model.parameters.prism.color=(float)atof(argv[9]);
    ts_prism_controls_sanitize(&model.parameters.prism);
    model.routing.limiter_ceiling_db=-1;
    TsPrism prism={0}; assert(ts_prism_prepare(&prism,48000));
    ts_prism_set_controls(&prism,&model.parameters.prism);
    for (int i=0; i<144000; ++i) {
        float input=.3f*sinf(i*.05f);
        ts_prism_process(&prism,(TsStereoFrame){input,input});
    }
    model.routing.prism=ts_prism_view(&prism);
    snprintf(model.status,sizeof(model.status),"PRISM: ONE SOUND, RELATED REFRACTIONS");
    if(argc>1 && !strcmp(argv[1],"--render-bench")) {
        double start=now();
        for(int n=0;n<1000;++n)ts_sister_ui_render(&fb,&model,&palette);
        printf("native_prism_mean_ms=%.3f\n",(now()-start));
        ts_prism_free(&prism);return 0;
    }
    ts_sister_ui_render(&fb,&model,&palette);
    assert(ts_ui_write_ppm(&fb,argc>1?argv[1]:"prism.ppm"));
    ts_prism_free(&prism);
    return 0;
}
