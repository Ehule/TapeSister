#include "tapesister/cdp_portal.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static TsCdpRuntime runtime;
static TsCdpRunResult result;
static TsCdpRunOptions options;
static char error[2048];
static int failures;
static int cancel_at_effect(void *unused)
{ (void)unused;return !strcmp(result.failed_executable,"strange"); }
static int run(const TsPortalRecipe *recipe,const TsSample *s)
{
    int ok=ts_cdp_run_portal(&runtime,recipe,s,&options,&result,error,sizeof(error));
    if(!ok)fprintf(stderr,"FAIL %s: %s\n%s\n",recipe->process_id,error,result.diagnostic);
    if(ok && (!result.finite || result.output.channels!=1 || !result.output.frames ||
              result.output.frames>TS_PORTAL_MAX_FRAMES || result.cleanup_failed || result.job_directory[0]))ok=0;
    if(!ok)++failures;
    return ok;
}
static double tone(const TsSample *s,double hz)
{
    double re=0,im=0;size_t first=s->sample_rate/2,last=s->sample_rate;
    assert(last<=s->frames);
    for(size_t n=first;n<last;++n) {double phase=6.283185307179586*hz*n/s->sample_rate;re+=s->data[n]*cos(phase);im+=s->data[n]*sin(phase);}
    return 2*hypot(re,im)/(last-first);
}
static void measured_checks(TsSample *s)
{
    TsPortalRecipe recipe;
    for(size_t i=0;i<s->frames;++i)s->data[i]=(float)(.1*sin(6.283185307179586*300*i/s->sample_rate)+.1*sin(6.283185307179586*3000*i/s->sample_rate));
    const int modes[]={1,3,7,9};
    for(int m=0;m<4;++m) {
        char id[64];snprintf(id,sizeof(id),"hilite.filter.%d",modes[m]);
        ts_portal_recipe_default(&recipe,ts_portal_process_find(id));
        if(m>=2){recipe.values[0]=2000;recipe.values[1]=4000;}
        if(run(&recipe,s)) {
            double low=tone(&result.output,300),high=tone(&result.output,3000);
            if(m==0 || m==2)assert(high>low*10);else assert(low>high*10);
        }
    }
    ts_portal_recipe_default(&recipe,ts_portal_process_find("strange.shift.1"));
    if(run(&recipe,s))assert(tone(&result.output,400)>tone(&result.output,300)*5);
    for(size_t i=0;i<s->frames;++i)s->data[i]=(float)(.15*sin(6.283185307179586*300*i/s->sample_rate)+.03*sin(6.283185307179586*3000*i/s->sample_rate));
    ts_portal_recipe_default(&recipe,ts_portal_process_find("focus.exag"));
    if(run(&recipe,s)) {
        double ratio=tone(&result.output,3000)/tone(&result.output,300);
        assert(ratio<.1); /* input weak/strong ratio was .2 */
        recipe.values[0]=2;
        if(run(&recipe,s))assert(tone(&result.output,3000)/tone(&result.output,300)>ratio*3);
    }
    /* Real source slices, not merely a valid WAV header. */
    ts_portal_recipe_default(&recipe,ts_portal_process_find("sfedit.cut.1"));
    if(run(&recipe,s)) {
        assert(llabs((long long)result.output.frames-llround(.3*s->sample_rate))<=1);
        size_t at=(size_t)(.15*s->sample_rate),src=at+(size_t)(.1*s->sample_rate);
        for(size_t i=0;i<256;++i)assert(fabs(result.output.data[at+i]-s->data[src+i])<.0001);
    }
    ts_portal_recipe_default(&recipe,ts_portal_process_find("sfedit.cutend.1"));
    if(run(&recipe,s))assert(llabs((long long)result.output.frames-llround(.3*s->sample_rate))<=1);
    ts_portal_recipe_default(&recipe,ts_portal_process_find("sfedit.excise.1"));
    if(run(&recipe,s))assert(result.output.frames<s->frames && result.output.frames>s->frames-s->sample_rate/2);
    ts_portal_recipe_default(&recipe,ts_portal_process_find("extend.doublets"));
    if(run(&recipe,s))assert(result.output.frames>s->frames*1.5 && result.output.frames<=s->frames*2.1);
    /* Two positive-seed renders must reproduce the same sequence. */
    for(int mode=1;mode<=1;++mode) {
        char id[64];snprintf(id,sizeof(id),"extend.scramble.%d",mode);
        ts_portal_recipe_default(&recipe,ts_portal_process_find(id));
        if(run(&recipe,s)) {
            uint64_t hash=ts_sample_hash(&result.output);
            if(run(&recipe,s))assert(ts_sample_hash(&result.output)==hash);
            recipe.values[4]=17;
            if(run(&recipe,s))assert(ts_sample_hash(&result.output)!=hash);
        }
    }
    ts_portal_recipe_default(&recipe,ts_portal_process_find("strange.shift.1"));
    options.cancel_check=cancel_at_effect;
    assert(!ts_cdp_run_portal(&runtime,&recipe,s,&options,&result,error,sizeof(error)));
    assert(result.status==TS_CDP_RUN_CANCELLED && !result.cleanup_failed && !result.job_directory[0]);
    options.cancel_check=NULL;
}
int main(void)
{
    const char *bin=getenv("TS_TEST_CDP_BIN");
    if(!bin){puts("Set TS_TEST_CDP_BIN for final Portal batch real-audio checks");return 0;}
    ts_cdp_runtime_init(&runtime);ts_cdp_run_result_init(&result);ts_cdp_run_options_init(&options);
    options.timeout_ms=10000;
    assert(ts_cdp_runtime_discover(&runtime,bin,NULL,error,sizeof(error)));
    for(int r=0;r<2;++r) {
        unsigned sr=r?48000:44100;TsSample s;ts_sample_init(&s);s.frames=sr*2;s.sample_rate=sr;
        s.data=malloc(s.frames*sizeof(float));assert(s.data);
        for(size_t n=0;n<s.frames;++n) {
            double t=(double)n/sr,amp=.25+.15*sin(6.283185307179586*3*t);
            s.data[n]=(float)(amp*(.6*sin(6.283185307179586*300*t)+.3*sin(6.283185307179586*3000*t)+.1*sin(6.283185307179586*7000*t)));
        }
        unsigned passed=0,rejected=0;
        uint64_t source_hash=ts_sample_hash(&s);
        for(size_t i=88;i<ts_portal_process_count();++i) {
            const TsPortalProcess *p=ts_portal_process_at(i);TsPortalRecipe recipe;
            ts_portal_recipe_default(&recipe,p);
            if(run(&recipe,&s)) {assert(result.peak>0);++passed;printf("DEFAULT %u %s %zu frames\n",sr,p->id,result.output.frames);fflush(stdout);}
            for(unsigned n=0;n<p->parameter_count;++n)for(int edge=0;edge<2;++edge) {
                ts_portal_recipe_default(&recipe,p);recipe.values[n]=edge?p->parameters[n].maximum:p->parameters[n].minimum;
                TsCdpCommand commands[TS_CDP_MAX_STAGES];size_t count;
                if(!ts_portal_build_commands(&recipe,&s,commands,&count,error,sizeof(error))){++rejected;continue;}
                if(!run(&recipe,&s))fprintf(stderr,"  RATE %u PARAM %s = %.9g\n",sr,p->parameters[n].id,recipe.values[n]);else ++passed;
            }
        }
        assert(ts_sample_hash(&s)==source_hash);
        measured_checks(&s);printf("RATE %u: %u accepted renders, %u source/relationship constraints\n",sr,passed,rejected);ts_sample_free(&s);
    }
    ts_cdp_run_result_free(&result);
    if(failures){fprintf(stderr,"%d render failures\n",failures);return 1;}
    puts("Final Portal batch audio checks passed");return 0;
}
