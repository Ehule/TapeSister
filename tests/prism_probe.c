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
static void benchmark(void)
{
    enum { BLOCKS=4000, FRAMES=256 };
    static double times[BLOCKS];
    const int counts[] = {0,2,4,6,8,12};
    for (int full=0; full<2; ++full) for (int k=0; k<6; ++k) {
        TsSisterRuntime *r=calloc(1,sizeof(*r)); assert(r);
        ts_sister_runtime_init(r);
        char error[160];
        assert(ts_sister_runtime_enable(r,48000,2,2,5,error,sizeof(error)));
        TsSisterParameters c=r->parameters;
        c.prism.enabled=counts[k]!=0; c.prism.lenses=counts[k]?counts[k]:12;
        c.prism.mix=1; c.prism.drift=1; c.monitor_dry=1; c.monitor_wet=0;
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

int main(int argc,char **argv)
{
    if (argc>1 && !strcmp(argv[1],"--bench")) {benchmark();return 0;}
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
    model.routing.limiter_enabled=1; model.routing.master_output_gain=1;
    model.routing.limiter_ceiling_db=-1;
    TsPrism prism={0}; assert(ts_prism_prepare(&prism,48000));
    ts_prism_set_controls(&prism,&model.parameters.prism);
    for (int i=0; i<144000; ++i) {
        float input=.3f*sinf(i*.05f);
        ts_prism_process(&prism,(TsStereoFrame){input,input});
    }
    model.routing.prism=ts_prism_view(&prism);
    snprintf(model.status,sizeof(model.status),"PRISM: ONE SOUND, RELATED REFRACTIONS");
    ts_sister_ui_render(&fb,&model,&palette);
    assert(ts_ui_write_ppm(&fb,argc>1?argv[1]:"prism.ppm"));
    ts_prism_free(&prism);
    return 0;
}
