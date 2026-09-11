#include "tapesister/cdp_portal.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static TsCdpRuntime runtime;
static TsCdpRunResult output;
static TsCdpRunOptions options;
static char error[2048];
static unsigned renders,rejected,failures;
static double right_gain=-.25;
static int left_silent;

static int checked(const TsPortalRecipe *r,const TsSample *s,const char *label)
{
    uint64_t hash=ts_sample_hash(s);
    ++renders;
    int ok=ts_cdp_run_portal(&runtime,r,s,&options,&output,error,sizeof(error));
    if(!ok && s->channels==2 && strstr(error,"STEREO OUTPUT WOULD CLIP")) {
        assert(!output.output.data && hash==ts_sample_hash(s));
        ++rejected;return 0;
    }
    if(!ok || !output.finite || !output.output.frames || output.output.channels!=s->channels ||
       output.output.sample_rate!=s->sample_rate || output.output.frames>TS_PORTAL_MAX_FRAMES ||
       output.cleanup_failed || output.job_directory[0] || hash!=ts_sample_hash(s)) {
        fprintf(stderr,"FAIL %s %uHz %uch %s: %s\n%s\n",r->process_id,s->sample_rate,s->channels,label,error,output.diagnostic);
        ++failures;return 0;
    }
    if(s->channels==2 && strcmp(r->process_id,"modify.radical.4")) {
        double link=0;
        for(size_t i=0;i<output.output.frames;++i)
            link=fmax(link,fabs(left_silent?output.output.data[2*i]:output.output.data[2*i+1]-right_gain*output.output.data[2*i]));
        if(link>.0001) {
            fprintf(stderr,"LINK %s %uHz %s error %.9g peak %.5g\n",r->process_id,s->sample_rate,label,link,output.peak);
            ++failures;return 0;
        }
    }
    return 1;
}
static int cancel(void *unused){(void)unused;return 1;}
int main(void)
{
    const char *bin=getenv("TS_TEST_CDP_BIN");
    assert(bin && *bin);
    ts_cdp_runtime_init(&runtime);ts_cdp_run_result_init(&output);ts_cdp_run_options_init(&options);
    assert(ts_cdp_runtime_discover(&runtime,bin,".",error,sizeof(error)));
    options.timeout_ms=5000;
    int edges=getenv("TS_TEST_CDP_EDGES")!=NULL;
    for(unsigned sr=44100;sr<=48000;sr+=3900)for(unsigned channels=1;channels<=2;++channels) {
        TsSample s;ts_sample_init(&s);s.sample_rate=sr;s.frames=2*sr;s.channels=channels;
        s.data=calloc(s.frames*channels,sizeof(float));assert(s.data);
        for(size_t n=0;n<s.frames;++n) {
            double t=(double)n/sr,e=fmax(0,sin(6.283185307179586*3*t));
            double value=e*e*(.2*sin(6.283185307179586*220*t)+.07*sin(6.283185307179586*660*t)+.03*sin(6.283185307179586*1760*t));
            float v=(float)(((int)(value*32768)/4)*4)/32768;
            s.data[n*channels]=v;if(channels==2)s.data[n*2+1]=-.25f*v;
        }
        for(size_t index=0;index<ts_portal_process_count();++index) {
            const TsPortalProcess *p=ts_portal_process_at(index);
            TsPortalRecipe r;ts_portal_recipe_default(&r,p);
            int added=index>=131,native=ts_portal_stereo_policy(&r)==TS_PORTAL_STEREO_NATIVE;
            if(!added && !native)continue;
            if(channels==2 && !native)continue;
            if(!checked(&r,&s,"default"))continue;
            if(channels==2) {
                right_gain=0;for(size_t n=0;n<s.frames;++n)s.data[2*n+1]=0;
                checked(&r,&s,"silent right");
                right_gain=-1;for(size_t n=0;n<s.frames;++n)s.data[2*n+1]=-s.data[2*n];
                checked(&r,&s,"anti-phase");
                left_silent=1;
                for(size_t n=0;n<s.frames;++n){s.data[2*n+1]=s.data[2*n];s.data[2*n]=0;}
                checked(&r,&s,"silent left");left_silent=0;
                for(size_t n=0;n<s.frames;++n){s.data[2*n]=s.data[2*n+1]*.5f;s.data[2*n+1]=-4*s.data[2*n];}
                right_gain=-4;checked(&r,&s,"right dominant");
                for(size_t n=0;n<s.frames;++n){s.data[2*n]*=2;s.data[2*n+1]=-.25f*s.data[2*n];}
                right_gain=-.25;
            }
            for(unsigned param=0;param<p->parameter_count;++param)if(!strcmp(p->parameters[param].id,"seed")) {
                if(checked(&r,&s,"seed reference")) {
                    uint64_t repeat=ts_sample_hash(&output.output);
                    if(checked(&r,&s,"seed repeat"))assert(ts_sample_hash(&output.output)==repeat);
                }
            }
            if(edges)for(unsigned param=0;param<p->parameter_count;++param)for(int side=0;side<2;++side) {
                double initial=r.values[param];
                r.values[param]=side?p->parameters[param].maximum:p->parameters[param].minimum;
                TsCdpCommand commands[TS_CDP_MAX_STAGES];size_t count;
                char label[96];snprintf(label,sizeof(label),"%s=%g",p->parameters[param].id,r.values[param]);
                if(ts_portal_build_commands(&r,&s,commands,&count,error,sizeof(error)))
                    checked(&r,&s,label);
                else ++rejected;
                r.values[param]=initial;
            }
        }
        /* Seeded CREATE choices remain ordinary recipes and render with the
           same channel safety as explicit library selection. */
        for(uint32_t seed=12;seed<48;++seed) {
            TsPortalRecipe roll,again;
            assert(ts_portal_create_recipe(seed,&s,&roll,error,sizeof(error)));
            assert(ts_portal_create_recipe(seed,&s,&again,error,sizeof(error)));
            assert(!memcmp(&roll,&again,sizeof(roll)));
            checked(&roll,&s,"CREATE roll");
        }
        /* A mixed chain must normalize the stereo pair together between split
           processing stages, not normalize two separate mono chains. */
        TsPortalRecipe chain={0},step;snprintf(chain.process_id,sizeof(chain.process_id),"@chain");
        chain.version=1;chain.stage_count=3;
        const char *ids[]={"modify.loudness.1","modify.loudness.4","modify.radical.1"};
        for(unsigned n=0;n<3;++n) {
            ts_portal_recipe_default(&step,ts_portal_process_find(ids[n]));
            if(n<2)step.values[0]=.5;
            ts_portal_step_set(&chain.stages[n],&step);
        }
        if(checked(&chain,&s,"mixed chain"))assert(fabs(output.peak-.5)<.0001);
        options.cancel_check=cancel;
        assert(!ts_cdp_run_portal(&runtime,&chain,&s,&options,&output,error,sizeof(error)));
        assert(output.status==TS_CDP_RUN_CANCELLED && !output.output.data);
        options.cancel_check=NULL;
        ts_portal_recipe_default(&step,ts_portal_process_find("modify.loudness.4"));
        options.fault=TS_CDP_FAULT_MALFORMED_WAV;
        assert(!ts_cdp_run_portal(&runtime,&step,&s,&options,&output,error,sizeof(error)));
        assert(!output.output.data && !output.cleanup_failed && !output.job_directory[0]);
        options.fault=TS_CDP_FAULT_NONE;
        s.data[s.frames*channels-1]=NAN;
        assert(!ts_cdp_run_portal(&runtime,&step,&s,&options,&output,error,sizeof(error)) && !output.output.data);
        ts_sample_free(&s);
    }
    ts_cdp_run_result_free(&output);
    printf("Expansion: %u renders, %u coupled/source-bound rejections, %u failures\n",renders,rejected,failures);
    return failures?1:0;
}
