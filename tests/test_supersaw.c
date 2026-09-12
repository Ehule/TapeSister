#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/cdp_portal.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Measure rendered frequencies, not just the recipe's interval arithmetic.
   Six seconds of Hann-windowed source, sampled at a stride of four, resolves
   even the tightest voicing's neighboring tones at this test pitch. */
static double tone(const TsSample *s,double hz)
{
    double re=0,im=0,weight=0;
    size_t first=s->sample_rate/2,last=first+6*s->sample_rate;
    assert(last<s->frames);
    for(size_t n=first;n<last;n+=4) {
        double w=.5-.5*cos(6.283185307179586*(n-first)/(last-first));
        double phase=6.283185307179586*hz*n/s->sample_rate;
        double v=s->data[n*s->channels]*w;
        re+=v*cos(phase);im+=v*sin(phase);weight+=w;
    }
    return 2*hypot(re,im)/weight;
}
static void run(TsCdpRuntime *runtime,TsPortalRecipe *r,TsSample *s,TsCdpRunResult *out)
{
    char error[2048];TsCdpRunOptions options;ts_cdp_run_options_init(&options);
    uint64_t original=ts_sample_hash(s);
    if(!ts_cdp_run_portal(runtime,r,s,&options,out,error,sizeof(error))) {
        fprintf(stderr,"%s: %s\n%s\n",r->name,error,out->diagnostic);assert(0);
    }
    assert(out->finite && !out->cleanup_failed && !out->job_directory[0]);
    assert(out->output.channels==s->channels && out->output.sample_rate==s->sample_rate);
    assert(ts_sample_hash(s)==original);
    assert(fabs(out->peak-r->stages[4].values[0])<.0001);
    assert(out->output.frames>s->frames*.99 && out->output.frames<s->frames*1.04);
}
int main(void)
{
    const char *bin=getenv("TS_TEST_CDP_BIN");
    if(!bin || !*bin){puts("Set TS_TEST_CDP_BIN for native SUPERSAW signal checks");return 0;}
    TsCdpRuntime runtime;TsCdpRunResult out;char error[256];
    ts_cdp_runtime_init(&runtime);ts_cdp_run_result_init(&out);
    assert(ts_cdp_runtime_discover(&runtime,bin,NULL,error,sizeof(error)));
    const double offsets[]={-26,-19,-12,-7,0,7,12,19,26},spread[]={.5,1,1.6};
    for(unsigned rate=44100;rate<=48000;rate+=3900) {
        TsSample s={0};s.sample_rate=rate;s.channels=1;s.frames=8*rate;
        s.data=malloc(s.frames*sizeof(float));assert(s.data);
        for(size_t n=0;n<s.frames;++n)s.data[n]=(float)(.2*sin(6.283185307179586*880*n/rate));
        for(unsigned preset=0;preset<3;++preset) {
            TsPortalRecipe r;assert(ts_portal_instrument_recipe(8+preset,&r));
            assert(ts_portal_stereo_supported(&r));
            run(&runtime,&r,&s,&out);
            for(unsigned n=0;n<9;++n) {
                double hz=880*exp2(offsets[n]*spread[preset]/1200);
                double level=tone(&out.output,hz),between=tone(&out.output,hz+.7);
                if(level<.025 || level<between*5) {
                    fprintf(stderr,"%s %uHz layer %gHz: %g versus %g\n",r.name,rate,hz,level,between);assert(0);
                }
            }
            uint64_t hash=ts_sample_hash(&out.output);run(&runtime,&r,&s,&out);
            assert(hash==ts_sample_hash(&out.output));
            /* Reachable tone/output endpoints remain finite and pitched. */
            r.stages[3].values[1]=400;r.stages[4].values[0]=.1;run(&runtime,&r,&s,&out);
            r.stages[3].values[1]=6000;r.stages[4].values[0]=.85;run(&runtime,&r,&s,&out);
        }
        /* Preserve native stereo relationships through both stacks, split
           transpose, native filter, and one shared final peak adjustment. */
        TsSample stereo={0};stereo.sample_rate=rate;stereo.channels=2;stereo.frames=s.frames;
        stereo.data=calloc(stereo.frames*2,sizeof(float));assert(stereo.data);
        TsPortalRecipe r;assert(ts_portal_instrument_recipe(9,&r));
        for(int silent=0;silent<2;++silent) {
            for(size_t n=0;n<s.frames;++n){stereo.data[2*n]=s.data[n];stereo.data[2*n+1]=silent?0:-.25f*s.data[n];}
            run(&runtime,&r,&stereo,&out);
            for(size_t n=0;n<out.output.frames;++n) {
                float expected=silent?0:-.25f*out.output.data[2*n];
                assert(fabsf(out.output.data[2*n+1]-expected)<.0003f);
            }
        }
        ts_sample_free(&stereo);ts_sample_free(&s);
    }
    ts_cdp_run_result_free(&out);
    puts("SUPERSAW: nine centered frequencies, repeatability, macro endpoints and stereo passed at 44.1/48 kHz");
    return 0;
}
