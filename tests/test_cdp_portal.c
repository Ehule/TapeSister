#include "tapesister/cdp_portal.h"
#include "tapesister/ui.h"
#include "tapesister/transform.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#else
#include <unistd.h>
#endif

static int cancel_always(void *unused) {(void)unused;return 1;}
static int cancel_spectral_stage(void *data)
{return !strcmp(((TsCdpRunResult*)data)->failed_executable,"blur");}

/* Measure a steady tone after the filter's startup transient. */
static double tone_level(const TsSample *s,double hz)
{
    double re=0,im=0;size_t first=s->sample_rate/2,last=s->sample_rate;
    assert(s->frames>=last);
    for(size_t n=first;n<last;++n) {
        double phase=6.283185307179586*hz*(double)n/s->sample_rate;
        re+=s->data[n]*cos(phase);im+=s->data[n]*sin(phase);
    }
    return 2*hypot(re,im)/(double)(last-first);
}

/* Grain jitter broadens a tone. Measure a narrow band, not one FFT bin. */
static double grain_band_power(const TsSample *s,double center)
{
    size_t first=s->sample_rate/4,last=s->sample_rate*3/4;
    double power=0;assert(s->frames>=last);
    for(int offset=-40;offset<=40;offset+=10) {
        double re=0,im=0;
        for(size_t n=first;n<last;++n) {
            double phase=6.283185307179586*(center+offset)*n/s->sample_rate;
            re+=s->data[n]*cos(phase);im+=s->data[n]*sin(phase);
        }
        power+=re*re+im*im;
    }
    return power;
}

static void render_checked(const TsCdpRuntime *runtime, const TsPortalRecipe *recipe,
                           const TsSample *input, TsCdpRunResult *result)
{
    char error[2048];TsCdpRunOptions options;ts_cdp_run_options_init(&options);
    int ok=ts_cdp_run_portal(runtime,recipe,input,&options,result,error,sizeof(error));
    if(!ok)fprintf(stderr,"SEMANTICS %s: %s\n%s\n",recipe->process_id,error,result->diagnostic);
    assert(ok && result->finite && !result->cleanup_failed && !result->job_directory[0]);
}

/* Independent signal checks catch valid commands routed to the wrong native mode. */
static void test_sound_families(const TsCdpRuntime *runtime)
{
    TsPortalRecipe r;TsCdpRunResult out;ts_cdp_run_result_init(&out);
    for(unsigned sr=44100;sr<=48000;sr+=3900) {
        TsSample s={.frames=sr*2,.sample_rate=sr,.channels=1};
        s.data=calloc(s.frames,sizeof(float));assert(s.data);
        for(size_t n=0;n<s.frames;++n)s.data[n]=(float)(.1*sin(6.283185307179586*500*n/sr));
        uint64_t hash=ts_sample_hash(&s);
        const char *gain_ids[]={"modify.loudness.1","modify.loudness.2","modify.loudness.3","modify.loudness.4","modify.loudness.6"};
        const double gain_values[]={.5,-6,.2,.05,0};
        const double gains[]={.5,.5011872336,2,.5,-1};
        for(int i=0;i<5;++i) {
            ts_portal_recipe_default(&r,ts_portal_process_find(gain_ids[i]));
            if(i<4)r.values[0]=gain_values[i];
            render_checked(runtime,&r,&s,&out);assert(out.output.frames==s.frames);
            for(size_t n=0;n<s.frames;n+=137)assert(fabs(out.output.data[n]-s.data[n]*gains[i])<.0002);
        }
        ts_portal_recipe_default(&r,ts_portal_process_find("modify.radical.5"));r.values[0]=150;
        render_checked(runtime,&r,&s,&out);
        assert(out.output.frames==s.frames);
        assert(tone_level(&out.output,350)>.045 && tone_level(&out.output,650)>.045);
        assert(tone_level(&out.output,500)<.001);
        ts_portal_recipe_default(&r,ts_portal_process_find("modify.radical.4"));r.values[0]=4;r.values[1]=3;
        s.frames--;render_checked(runtime,&r,&s,&out);
        assert(out.output.frames==(s.frames/3)*3);
        for(size_t n=0;n<out.output.frames;n+=3)
            assert(out.output.data[n]==out.output.data[n+1] && out.output.data[n]==out.output.data[n+2]);
        s.frames++;
        ts_portal_recipe_default(&r,ts_portal_process_find("modify.radical.7"));r.values[0]=3;
        render_checked(runtime,&r,&s,&out);assert(out.output.frames==s.frames);
        unsigned levels=0;float seen[16]={0};
        for(size_t n=0;n<out.output.frames;++n) {
            unsigned i=0;while(i<levels && seen[i]!=out.output.data[n])++i;
            if(i==levels) {assert(levels<8);seen[levels++]=out.output.data[n];}
        }
        assert(levels>=2);
        /* Square reform adds odd harmonics; sinusoidal reform does not. */
        ts_portal_recipe_default(&r,ts_portal_process_find("distort.reform.2"));
        render_checked(runtime,&r,&s,&out);assert(tone_level(&out.output,1500)>.025);
        ts_portal_recipe_default(&r,ts_portal_process_find("distort.reform.7"));
        render_checked(runtime,&r,&s,&out);assert(tone_level(&out.output,1500)<.004);
        /* Amplitude-only chorus at one agrees with a PVOC round trip. Frequency
           modes still re-bin partials at one, but must retain the tone's pitch. */
        ts_portal_recipe_default(&r,ts_portal_process_find("blur.noise"));r.values[0]=0;
        render_checked(runtime,&r,&s,&out);
        size_t neutral_frames=out.output.frames;
        float *neutral=malloc(neutral_frames*sizeof(float));assert(neutral);
        memcpy(neutral,out.output.data,neutral_frames*sizeof(float));
        for(int mode=1;mode<=7;++mode) {
            char id[32];snprintf(id,sizeof(id),"blur.chorus.%d",mode);
            const TsPortalProcess *p=ts_portal_process_find(id);ts_portal_recipe_default(&r,p);
            for(unsigned i=0;i<p->parameter_count;++i)r.values[i]=1;
            render_checked(runtime,&r,&s,&out);assert(out.output.frames==neutral_frames);
            if(mode==1)for(size_t n=0;n<neutral_frames;n+=137)assert(fabs(neutral[n]-out.output.data[n])<.0001);
            assert(tone_level(&out.output,500)>.02 && tone_level(&out.output,1500)<.001);
        }
        free(neutral);
        for(size_t n=0;n<s.frames;++n)s.data[n]=(float)(.05*(sin(6.283185307179586*500*n/sr)+sin(6.283185307179586*5000*n/sr)));
        ts_portal_recipe_default(&r,ts_portal_process_find("stretch.spectrum.1"));
        render_checked(runtime,&r,&s,&out);
        assert(tone_level(&out.output,500)>.04 && tone_level(&out.output,5000)<.01);
        ts_portal_recipe_default(&r,ts_portal_process_find("stretch.spectrum.2"));
        render_checked(runtime,&r,&s,&out);
        assert(tone_level(&out.output,500)<.01 && tone_level(&out.output,5000)>.04);
        for(size_t n=0;n<s.frames;++n)s.data[n]=(float)(.1*sin(6.283185307179586*500*n/sr));
        assert(ts_sample_hash(&s)==hash);
        /* Fixed EQ is measured in bands well away from the shelf transition. */
        for(size_t n=0;n<s.frames;++n)s.data[n]=(float)(.02*(sin(6.283185307179586*100*n/sr)+sin(6.283185307179586*1000*n/sr)+sin(6.283185307179586*5000*n/sr)));
        for(int mode=1;mode<=3;++mode) {
            char id[32];snprintf(id,sizeof(id),"filter.fixed.%d",mode);
            for(int sign=-1;sign<=1;sign+=2) {
                ts_portal_recipe_default(&r,ts_portal_process_find(id));
                r.values[mode==3?1:0]=sign*12;r.values[mode==3?4:3]=1;
                render_checked(runtime,&r,&s,&out);
                double selected=tone_level(&out.output,mode==1?100:mode==2?5000:1000);
                assert(sign>0?selected>.055:selected<.008);
            }
        }
        /* A zero-feedback, wet-only delay must place one impulse exactly at delay time. */
        memset(s.data,0,s.frames*sizeof(float));s.data[0]=.25f;
        ts_portal_recipe_default(&r,ts_portal_process_find("modify.revecho.1"));
        r.values[0]=10;r.values[1]=1;r.values[2]=0;r.values[3]=.1;r.values[4]=1;
        render_checked(runtime,&r,&s,&out);
        size_t offset=(size_t)round(.01*sr);
        assert(fabs((double)out.output.frames-s.frames-round(.1*sr))<=1);
        assert(fabs(out.output.data[offset]-.25)<.0001);
        for(size_t n=0;n<out.output.frames;++n)if(n!=offset)assert(fabs(out.output.data[n])<.0001);
        r.values[1]=0;r.values[5]=1;render_checked(runtime,&r,&s,&out);
        assert(fabs(out.output.data[0]+.25)<.0001 && fabs(out.output.data[offset])<.0001);
        ts_portal_recipe_default(&r,ts_portal_process_find("filter.phasing.1"));
        r.values[0]=0;r.values[1]=10;render_checked(runtime,&r,&s,&out);
        assert(fabs(out.output.data[offset]-.25)<.0001 && fabs(out.output.data[0])<.0001);
        free(s.data);
    }
    ts_cdp_run_result_free(&out);
}

/* Test source-dependent rejection separately from successful endpoint renders. */
static void envelope_batch_checks(const TsCdpRuntime *runtime)
{
    TsPortalRecipe r;TsCdpRunResult out;ts_cdp_run_result_init(&out);
    char error[2048];TsCdpCommand plan[TS_CDP_MAX_STAGES];size_t count;
    for(unsigned sr=44100;sr<=48000;sr+=3900) {
        TsSample s={.frames=sr*6,.sample_rate=sr,.channels=1};
        s.data=malloc(s.frames*sizeof(float));assert(s.data);
        for(size_t n=0;n<s.frames;++n) {
            double amplitude=.25+.15*sin(6.283185307179586*2*n/sr);
            s.data[n]=(float)(amplitude*sin(6.283185307179586*500*n/sr));
        }
        uint64_t hash=ts_sample_hash(&s);
        for(size_t i=64;i<88;++i) {
            const TsPortalProcess *p=ts_portal_process_at(i);
            ts_portal_recipe_default(&r,p);render_checked(runtime,&r,&s,&out);
            for(unsigned n=0;n<p->parameter_count;++n)for(int edge=0;edge<2;++edge) {
                ts_portal_recipe_default(&r,p);
                r.values[n]=edge?p->parameters[n].maximum:p->parameters[n].minimum;
                if(!ts_portal_build_commands(&r,&s,plan,&count,error,sizeof(error))) {
                    /* Coupled gate/threshold, cycle skips, and window counts
                       can make a scalar endpoint invalid for this source. */
                    assert(!count && error[0]);
                    printf("CONTEXT LIMIT %s %s %.9g: %s\n",p->id,p->parameters[n].id,r.values[n],error);
                    continue;
                }
                render_checked(runtime,&r,&s,&out);
                assert(out.output.sample_rate==sr && out.output.channels==1);
                if(p->family==TS_PORTAL_ENVELOPE)assert(out.output.frames==s.frames);
                printf("ENVELOPE EDGE %s %s %.9g rate %u\n",p->id,p->parameters[n].id,r.values[n],sr);
            }
        }
        assert(ts_sample_hash(&s)==hash);
        /* A stepped loudness contour distinguishes envelope reversal from
           audio reversal, gating from amplification, and ducking from limiting. */
        s.frames=sr*2;
        for(size_t n=0;n<s.frames;++n)s.data[n]=(float)((n<sr?.1:.5)*sin(6.283185307179586*500*n/sr));
        const char *ids[]={"envel.warp.2","envel.warp.3","envel.warp.5","envel.warp.8","envel.warp.14","envel.warp.15"};
        const double expected[][2]={{.5,.1},{.01,.25},{.2,.6},{0,.5},{.5,.5},{.1,.1}};
        for(int i=0;i<6;++i) {
            ts_portal_recipe_default(&r,ts_portal_process_find(ids[i]));
            if(i==3)r.values[1]=.2;
            render_checked(runtime,&r,&s,&out);
            for(int half=0;half<2;++half) {
                double re=0,im=0;
                size_t first=half*sr+sr/4,last=half*sr+sr*3/4;
                for(size_t n=first;n<last;++n) {
                    double phase=6.283185307179586*500*n/sr;
                    re+=out.output.data[n]*cos(phase);im+=out.output.data[n]*sin(phase);
                }
                double measured=2*hypot(re,im)/(last-first);
                if(fabs(measured-expected[i][half])>=.015)
                    fprintf(stderr,"ENVELOPE LEVEL %s half %d: %.6f expected %.3f\n",ids[i],half,measured,expected[i][half]);
                assert(fabs(measured-expected[i][half])<.015);
            }
        }
        for(size_t n=0;n<s.frames;++n)s.data[n]=(float)(.2*sin(6.283185307179586*500*n/sr));
        ts_portal_recipe_default(&r,ts_portal_process_find("envel.tremolo.1"));
        r.values[1]=0;r.values[2]=.5;render_checked(runtime,&r,&s,&out);
        for(size_t n=0;n<s.frames;n+=137)assert(fabs(out.output.data[n]-.5*s.data[n])<.0002);
        r.values[0]=20;r.values[1]=1;r.values[2]=1;render_checked(runtime,&r,&s,&out);
        assert(fabs(tone_level(&out.output,500)-.1)<.002);
        assert(fabs(tone_level(&out.output,480)-.05)<.002 && fabs(tone_level(&out.output,520)-.05)<.002);
        for(int mode=1;mode<=2;++mode) {
            char id[32];snprintf(id,sizeof(id),"envel.dovetail.%d",mode);
            ts_portal_recipe_default(&r,ts_portal_process_find(id));r.values[0]=r.values[1]=.2;
            render_checked(runtime,&r,&s,&out);
            assert(fabs(tone_level(&out.output,500)-.2)<.002);
            double early=0,late=0;
            for(size_t n=0;n<sr/100;++n) {early+=fabs(out.output.data[n]);late+=fabs(out.output.data[s.frames-1-n]);}
            assert(early/(sr/100)<.01 && late/(sr/100)<.01);
        }
        /* Invalid source lengths, nonfinite input, and coupled controls are
           rejected before CDP starts. */
        const char *coupled[]={"envel.warp.9","envel.warp.10","envel.warp.11","envel.warp.12"};
        for(int i=0;i<4;++i) {
            ts_portal_recipe_default(&r,ts_portal_process_find(coupled[i]));r.values[1]=r.values[2];
            assert(!ts_portal_recipe_validate(&r,error,sizeof(error)));
        }
        ts_portal_recipe_default(&r,ts_portal_process_find("envel.swell"));
        TsSample short_s=s;short_s.frames=sr/10;
        assert(!ts_portal_build_commands(&r,&short_s,plan,&count,error,sizeof(error)) && !count);
        ts_portal_recipe_default(&r,ts_portal_process_find("envel.dovetail.1"));r.values[0]=r.values[1]=.06;
        assert(!ts_portal_build_commands(&r,&short_s,plan,&count,error,sizeof(error)));
        ts_portal_recipe_default(&r,ts_portal_process_find("envel.warp.7"));r.values[1]=64;
        assert(!ts_portal_build_commands(&r,&short_s,plan,&count,error,sizeof(error)));
        ts_portal_recipe_default(&r,ts_portal_process_find("envel.warp.2"));short_s.channels=2;
        assert(!ts_portal_build_commands(&r,&short_s,plan,&count,error,sizeof(error)));
        short_s.channels=1;short_s.data[0]=NAN;
        assert(!ts_portal_build_commands(&r,&short_s,plan,&count,error,sizeof(error)));
        short_s.data[0]=0;
        ts_portal_recipe_default(&r,ts_portal_process_find("distort.overload.2"));
        short_s.sample_rate=22050;r.values[2]=12000;
        assert(!ts_portal_build_commands(&r,&short_s,plan,&count,error,sizeof(error)));
        free(s.data);
    }
    ts_cdp_run_result_free(&out);
}


#include "test_portal_chain_recipes.inc"
#include "test_portal_factory.inc"

int main(int argc,char **argv)
{
    static TsInstrument instrument;
    static TsUiState ui;
    static TsFramebuffer fb;
    TsPortalRecipe recipe;TsCdpCommand command;
    TsPortalLibrary library={0},loaded={0};
    char error[2048],path[128];
    ts_instrument_init(&instrument);ts_ui_init(&ui);
    assert(ts_instrument_generate(&instrument,TS_GENERATOR_METALLIC,0x54415045,error,sizeof(error)));
    uint64_t original=ts_sample_hash(&instrument.current);
    test_chain_recipes();
    test_factory_recipes();
    if(getenv("TS_TEST_FACTORY_BIN")) {test_factory_native(getenv("TS_TEST_FACTORY_BIN"));ts_instrument_free(&instrument);return 0;}
    assert(ts_portal_process_count()==131);
    assert(ts_cdp_factory_recipe_count()==32);
    for(size_t i=0;i<ts_portal_process_count();++i) {
        const TsPortalProcess *p=ts_portal_process_at(i);
        assert(p==ts_portal_process_find(p->id));
        assert(p->parameter_count<=TS_PORTAL_PARAMS);
        for(size_t j=0;j<i;++j)assert(strcmp(p->id,ts_portal_process_at(j)->id));
        ts_portal_recipe_default(&recipe,p);
        assert(ts_portal_recipe_validate(&recipe,error,sizeof(error)));
        TsCdpCommand commands[TS_CDP_MAX_STAGES];size_t count=0;
        assert(ts_portal_build_commands(&recipe,&instrument.current,commands,&count,error,sizeof(error)));
        assert(count==(p->family==TS_PORTAL_SPECTRAL?3u:1u));
        assert(!strcmp(commands[0].executable,p->family==TS_PORTAL_SPECTRAL?"pvoc":p->executable));
        assert(!strcmp(commands[count-1].expected_output,"output.wav"));
        for(size_t n=0;n<count;++n)assert(commands[n].argc<TS_CDP_MAX_COMMAND_ARGS);
        if(count==3) {
            assert(commands[0].expected_output_type==TS_CDP_IO_ANALYSIS);
            assert(commands[1].expected_output_type==TS_CDP_IO_ANALYSIS);
            assert(!strcmp(commands[1].executable,p->executable));
            assert(!strcmp(commands[1].arguments[0],p->command));
            assert(!strcmp(commands[2].arguments[0],"synth"));
        }
        if(p->parameter_count) {
            recipe.values[0]=NAN;assert(!ts_portal_recipe_validate(&recipe,error,sizeof(error)));
            recipe.values[0]=p->parameters[0].maximum+1;assert(!ts_portal_recipe_validate(&recipe,error,sizeof(error)));
        }
    }
    ts_portal_recipe_default(&recipe,ts_portal_process_find("distort.repeat"));
    assert(ts_portal_build_command(&recipe,&instrument.current,&command,error,sizeof(error)));
    assert(command.argc==6 && !strcmp(command.arguments[4],"-c8") && !strcmp(command.arguments[5],"-s0"));
    recipe.exposed=0;library.pins[0]=recipe;
    recipe.exposed=5;snprintf(recipe.name,sizeof(recipe.name),"METAL STRANDS");library.pins[31]=recipe;
    library.recipes[3]=recipe;
    snprintf(path,sizeof(path),"portal-test-%ld.recipes",(long)getpid());
    assert(ts_portal_library_save(&library,path,error,sizeof(error)));
    assert(ts_portal_library_load(&loaded,path,error,sizeof(error)));
    assert(!memcmp(&library,&loaded,sizeof(library)));
    library.recipes[3].values[0]=INFINITY;
    assert(!ts_portal_library_save(&library,path,error,sizeof(error)));
    assert(ts_portal_library_load(&loaded,path,error,sizeof(error)));
    assert(loaded.recipes[3].values[0]==2);
    library=loaded;
    /* Stable slots, duplicate names, updates versus replacements, and atomic failures. */
    assert(ts_portal_library_edit(&loaded,path,1,0,TS_PORTAL_RENAME,NULL,"MY EXACT PIN",error,sizeof(error)));
    assert(loaded.pins[0].exposed==0 && !strcmp(loaded.pins[0].name,"MY EXACT PIN"));
    recipe=loaded.pins[31];recipe.values[0]=5;recipe.exposed=1;
    assert(ts_portal_library_edit(&loaded,path,1,0,TS_PORTAL_UPDATE,&recipe,NULL,error,sizeof(error)));
    assert(loaded.pins[0].values[0]==5 && loaded.pins[0].exposed==1 && !strcmp(loaded.pins[0].name,"MY EXACT PIN"));
    TsPortalLibrary saved=loaded;
    ts_portal_recipe_default(&recipe,ts_portal_process_find("blur.chorus.5"));
    assert(!ts_portal_library_edit(&loaded,path,1,0,TS_PORTAL_UPDATE,&recipe,NULL,error,sizeof(error)));
    assert(!memcmp(&loaded,&saved,sizeof(saved)));
    assert(ts_portal_library_edit(&loaded,path,1,0,TS_PORTAL_REPLACE,&recipe,NULL,error,sizeof(error)));
    assert(!strcmp(loaded.pins[0].process_id,"blur.chorus.5"));
    assert(ts_portal_library_edit(&loaded,path,0,31,TS_PORTAL_REPLACE,&recipe,NULL,error,sizeof(error)));
    assert(ts_portal_library_edit(&loaded,path,0,3,TS_PORTAL_REMOVE,NULL,NULL,error,sizeof(error)));
    assert(!loaded.recipes[3].process_id[0] && loaded.recipes[31].process_id[0]);
    saved=loaded;
    assert(!ts_portal_library_edit(&loaded,path,1,0,TS_PORTAL_RENAME,NULL,"bad|name",error,sizeof(error)));
    assert(!ts_portal_library_edit(&loaded,path,1,32,TS_PORTAL_REMOVE,NULL,NULL,error,sizeof(error)));
    assert(!ts_portal_library_edit(&loaded,path,0,3,TS_PORTAL_UPDATE,&recipe,NULL,error,sizeof(error)));
    assert(!ts_portal_library_edit(&loaded,"missing-portal-directory/library.recipes",1,0,TS_PORTAL_REMOVE,NULL,NULL,error,sizeof(error)));
    assert(!memcmp(&loaded,&saved,sizeof(saved)));
    assert(ts_portal_library_load(&saved,path,error,sizeof(error)));
    assert(!memcmp(&loaded,&saved,sizeof(saved)));
    loaded=library;
    FILE *f=fopen(path,"wb");assert(f);fputs("TSCDPPORTAL 999\n",f);fclose(f);
    assert(!ts_portal_library_load(&loaded,path,error,sizeof(error)));
    assert(!memcmp(&library,&loaded,sizeof(library)));remove(path);
    ts_portal_recipe_default(&recipe,ts_portal_process_find("distort.omit"));
    recipe.values[0]=recipe.values[1];assert(!ts_portal_recipe_validate(&recipe,error,sizeof(error)));
    snprintf(recipe.process_id,sizeof(recipe.process_id),"../../bin/sh");
    assert(!ts_portal_recipe_validate(&recipe,error,sizeof(error)));
    ts_portal_recipe_default(&recipe,ts_portal_process_at(0));
    recipe.version=99;assert(!ts_portal_recipe_validate(&recipe,error,sizeof(error)));
    recipe.version=1;recipe.exposed=0xffff;assert(!ts_portal_recipe_validate(&recipe,error,sizeof(error)));
    recipe.exposed=1;
    TsSample stereo=instrument.current;stereo.channels=2;
    assert(!ts_portal_build_command(&recipe,&stereo,&command,error,sizeof(error)));
    float dc[100]={0};TsSample flat={.data=dc,.frames=100,.channels=1,.sample_rate=44100};
    assert(!ts_portal_build_command(&recipe,&flat,&command,error,sizeof(error)));
    recipe.values[0]=32767;
    assert(!ts_portal_build_command(&recipe,&instrument.current,&command,error,sizeof(error)));

    ui.portal.library=library;
    ui.portal.tab=2;assert(ts_portal_filter(&ui.portal,1,&recipe));assert(recipe.exposed==5);
    snprintf(ui.portal.query,sizeof(ui.portal.query),"strands");
    assert(ts_portal_filter(&ui.portal,0,&recipe));assert(!strcmp(recipe.name,"METAL STRANDS"));
    assert(!ts_portal_filter(&ui.portal,1,&recipe));
    assert(ts_portal_filter_slot(&ui.portal,0,&recipe)==31);
    ui.portal.family=TS_PORTAL_SPECTRAL+1;
    assert(!ts_portal_filter(&ui.portal,0,&recipe));
    ui.portal.tab=0;snprintf(ui.portal.query,sizeof(ui.portal.query),"partials");
    assert(ts_portal_filter(&ui.portal,0,&recipe));
    assert(ts_portal_process_find(recipe.process_id)->family==TS_PORTAL_SPECTRAL);
    ui.portal.query[0]=0;assert(ts_portal_filter(&ui.portal,49,&recipe));assert(!ts_portal_filter(&ui.portal,50,&recipe));
    ui.portal.family=TS_PORTAL_TIME+1;
    assert(ts_portal_filter(&ui.portal,3,&recipe));assert(!ts_portal_filter(&ui.portal,4,&recipe));
    snprintf(ui.portal.query,sizeof(ui.portal.query),"semitones");
    assert(ts_portal_filter(&ui.portal,0,&recipe) && !strcmp(recipe.process_id,"modify.speed.2"));
    ui.portal.query[0]=0;
    ui.portal.family=TS_PORTAL_FILTER+1;
    assert(ts_portal_filter(&ui.portal,12,&recipe));assert(!ts_portal_filter(&ui.portal,13,&recipe));
    snprintf(ui.portal.query,sizeof(ui.portal.query),"sweeping");
    assert(ts_portal_filter_slot(&ui.portal,0,&recipe)==24);
    assert(!strcmp(recipe.process_id,"filter.sweeping.2"));
    ui.portal.family=TS_PORTAL_GRAIN+1;ui.portal.query[0]=0;
    assert(ts_portal_filter(&ui.portal,3,&recipe));assert(!ts_portal_filter(&ui.portal,4,&recipe));
    snprintf(ui.portal.query,sizeof(ui.portal.query),"scramble");
    assert(ts_portal_filter_slot(&ui.portal,0,&recipe)==28);
    assert(!strcmp(recipe.process_id,"modify.brassage.4"));
    ui.portal.family=0;
    ui.portal.query[0]=0;ui.portal.tab=0;
    TsPortalWave wave;
    ts_portal_wave_reset(&wave,&instrument.current);
    assert(ts_portal_wave_frame(&wave,0)==0);
    assert(ts_portal_wave_frame(&wave,310)<instrument.current.frames);
    for(int i=0;i<50;++i)ts_portal_wave_zoom(&wave,&instrument.current,155,1);
    assert(wave.last-wave.first>=16);
    wave.first=instrument.current.frames-32;wave.last=wave.first+16;
    for(int i=0;i<100;++i)ts_portal_wave_pan(&wave,&instrument.current,1);
    assert(wave.last==instrument.current.frames);
    for(int i=0;i<100;++i)ts_portal_wave_zoom(&wave,&instrument.current,0,-1);
    assert(wave.first==0 && wave.last==instrument.current.frames);

    TsCdpRuntime runtime;ts_cdp_runtime_init(&runtime);
    TsCdpRunResult result;ts_cdp_run_result_init(&result);
    TsCdpRunOptions options;ts_cdp_run_options_init(&options);
    TsCdpCommand stages[TS_CDP_MAX_STAGES];size_t stage_count;
    ts_portal_recipe_default(&recipe,ts_portal_process_find("blur.avrg"));
    recipe.values[0]=12;assert(!ts_portal_recipe_validate(&recipe,error,sizeof(error)) && strstr(error,"ODD"));
    recipe.values[0]=13;assert(ts_portal_recipe_validate(&recipe,error,sizeof(error)));
    ts_portal_recipe_default(&recipe,ts_portal_process_find("blur.spread"));
    assert(ts_portal_build_commands(&recipe,&instrument.current,stages,&stage_count,error,sizeof(error)));
    assert(stage_count==3 && !strcmp(stages[1].arguments[3],"-f4") && !strcmp(stages[1].arguments[4],"-s0.5"));
    ts_portal_recipe_default(&recipe,ts_portal_process_find("stretch.spectrum.1"));
    assert(ts_portal_build_commands(&recipe,&instrument.current,stages,&stage_count,error,sizeof(error)));
    assert(!strcmp(stages[1].arguments[7],"-d1"));
    recipe.values[1]=1;assert(!ts_portal_build_commands(&recipe,&instrument.current,stages,&stage_count,error,sizeof(error)));
    recipe.values[1]=.25;recipe.values[0]=5000;
    TsSample low_rate=instrument.current;low_rate.sample_rate=22050;
    assert(!ts_portal_build_commands(&recipe,&low_rate,stages,&stage_count,error,sizeof(error)) && strstr(error,"INCOMPATIBLE"));
    ts_portal_recipe_default(&recipe,ts_portal_process_find("filter.fixed.1"));recipe.values[1]=16000;
    assert(ts_portal_build_commands(&recipe,&instrument.current,stages,&stage_count,error,sizeof(error)));
    assert(!ts_portal_build_commands(&recipe,&low_rate,stages,&stage_count,error,sizeof(error)) && strstr(error,"NYQUIST"));
    ts_portal_recipe_default(&recipe,ts_portal_process_find("filter.fixed.3"));recipe.values[0]=4000;low_rate.sample_rate=12000;
    assert(!ts_portal_build_commands(&recipe,&low_rate,stages,&stage_count,error,sizeof(error)) && strstr(error,"BANDWIDTH"));
    ts_portal_recipe_default(&recipe,ts_portal_process_find("modify.radical.5"));recipe.values[0]=12000;
    assert(!ts_portal_build_commands(&recipe,&low_rate,stages,&stage_count,error,sizeof(error)) && strstr(error,"NYQUIST"));
    ts_portal_recipe_default(&recipe,ts_portal_process_find("modify.revecho.1"));
    recipe.values[2]=.45;recipe.values[3]=.5;recipe.values[5]=1;recipe.exposed=(1u<<2)|(1u<<5);
    TsPortalLibrary delay_library={0},delay_loaded={0};delay_library.pins[31]=recipe;
    assert(ts_portal_library_save(&delay_library,path,error,sizeof(error)));
    assert(ts_portal_library_load(&delay_loaded,path,error,sizeof(error)));
    assert(!memcmp(&delay_library,&delay_loaded,sizeof(delay_library)));remove(path);
    ts_portal_recipe_default(&recipe,ts_portal_process_find("modify.loudness.3"));recipe.values[0]=.01;
    assert(!ts_portal_build_commands(&recipe,&instrument.current,stages,&stage_count,error,sizeof(error)) && strstr(error,"USE SET PEAK"));
    float silent[2048]={0};TsSample silence={.data=silent,.frames=2048,.sample_rate=44100,.channels=1};
    ts_portal_recipe_default(&recipe,ts_portal_process_find("modify.loudness.4"));
    assert(!ts_portal_build_commands(&recipe,&silence,stages,&stage_count,error,sizeof(error)) && strstr(error,"TOO QUIET"));
    ts_portal_recipe_default(&recipe,ts_portal_process_find("blur.blur"));
    TsSample short_input=instrument.current;short_input.frames=1024;
    assert(!ts_portal_build_commands(&recipe,&short_input,stages,&stage_count,error,sizeof(error)) && stage_count==0);
    short_input.frames=2048;recipe.values[0]=100;
    assert(!ts_portal_build_commands(&recipe,&short_input,stages,&stage_count,error,sizeof(error)));
    recipe.values[0]=8;assert(ts_portal_build_commands(&recipe,&short_input,stages,&stage_count,error,sizeof(error)));
    short_input.channels=2;assert(!ts_portal_build_commands(&recipe,&short_input,stages,&stage_count,error,sizeof(error)));
    ts_portal_recipe_default(&recipe,ts_portal_process_find("modify.speed.1"));
    short_input=instrument.current;short_input.frames=100;
    assert(!ts_portal_build_commands(&recipe,&short_input,stages,&stage_count,error,sizeof(error)));
    short_input=instrument.current;short_input.channels=2;
    assert(!ts_portal_build_commands(&recipe,&short_input,stages,&stage_count,error,sizeof(error)));
    ts_portal_recipe_default(&recipe,ts_portal_process_find("filter.sweeping.2"));
    assert(ts_portal_build_commands(&recipe,&instrument.current,stages,&stage_count,error,sizeof(error)));
    assert(stage_count==1 && stages[0].argc==11);
    assert(!strcmp(stages[0].arguments[9],"-t0.25") && !strcmp(stages[0].arguments[10],"-p0"));
    recipe.values[2]=recipe.values[3];
    assert(!ts_portal_recipe_validate(&recipe,error,sizeof(error)));
    ts_portal_recipe_default(&recipe,ts_portal_process_find("filter.variable.3"));
    short_input=instrument.current;short_input.sample_rate=22050;recipe.values[2]=4000;
    assert(!ts_portal_build_commands(&recipe,&short_input,stages,&stage_count,error,sizeof(error)) && !stage_count);
    assert(strstr(error,"SOURCE RATE / 6"));
    recipe.values[2]=3675;
    assert(ts_portal_build_commands(&recipe,&short_input,stages,&stage_count,error,sizeof(error)));
    short_input=instrument.current;short_input.channels=2;
    assert(!ts_portal_build_commands(&recipe,&short_input,stages,&stage_count,error,sizeof(error)));
    short_input=instrument.current;short_input.frames=100;
    assert(!ts_portal_build_commands(&recipe,&short_input,stages,&stage_count,error,sizeof(error)));
    recipe.values[3]=0;assert(!ts_portal_recipe_validate(&recipe,error,sizeof(error))); /* -t0 means auto-tail in CDP. */
    recipe.values[3]=2;
    short_input=instrument.current;short_input.frames=TS_PORTAL_MAX_FRAMES;
    assert(!ts_portal_build_commands(&recipe,&short_input,stages,&stage_count,error,sizeof(error)));
    assert(strstr(error,"TAIL EXCEEDS")); /* Checked before reading the artificial oversized input. */
    short_input=instrument.current;float first_sample=short_input.data[0];short_input.data[0]=NAN;
    assert(!ts_portal_build_commands(&recipe,&short_input,stages,&stage_count,error,sizeof(error)));
    short_input.data[0]=first_sample;
    ts_portal_recipe_default(&recipe,ts_portal_process_find("filter.phasing.2"));
    short_input=instrument.current;short_input.frames=1764;recipe.values[1]=21;
    assert(!ts_portal_build_commands(&recipe,&short_input,stages,&stage_count,error,sizeof(error)));
    recipe.values[1]=20;assert(ts_portal_build_commands(&recipe,&short_input,stages,&stage_count,error,sizeof(error)));
    short_input.sample_rate=8000;short_input.frames=320;recipe.values[1]=.1;
    assert(!ts_portal_build_commands(&recipe,&short_input,stages,&stage_count,error,sizeof(error)));
    /* Filter recipes round-trip in the existing format with flags and hidden macros intact. */
    ts_portal_recipe_default(&recipe,ts_portal_process_find("filter.sweeping.2"));
    recipe.values[4]=3;recipe.values[5]=.75;recipe.values[6]=.5;recipe.exposed=1u<<6;
    loaded=(TsPortalLibrary){0};loaded.pins[31]=recipe;loaded.recipes[31]=recipe;
    assert(ts_portal_library_save(&loaded,path,error,sizeof(error)));
    assert(ts_portal_library_load(&saved,path,error,sizeof(error)));
    assert(!memcmp(&loaded,&saved,sizeof(saved)));remove(path);
    ts_portal_recipe_default(&recipe,ts_portal_process_find("modify.brassage.4"));
    assert(ts_portal_build_commands(&recipe,&instrument.current,stages,&stage_count,error,sizeof(error)));
    assert(stage_count==1 && stages[0].argc==6);
    assert(!strcmp(stages[0].arguments[1],"4") && !strcmp(stages[0].arguments[4],"50") && !strcmp(stages[0].arguments[5],"-r250"));
    short_input=instrument.current;short_input.channels=2;
    assert(!ts_portal_build_commands(&recipe,&short_input,stages,&stage_count,error,sizeof(error)) && !stage_count);
    short_input=instrument.current;short_input.frames=100;
    assert(!ts_portal_build_commands(&recipe,&short_input,stages,&stage_count,error,sizeof(error)));
    short_input.frames=4410;recipe.values[1]=201;
    assert(!ts_portal_build_commands(&recipe,&short_input,stages,&stage_count,error,sizeof(error)) && strstr(error,"LOOKBACK"));
    recipe.values[1]=200;assert(ts_portal_build_commands(&recipe,&short_input,stages,&stage_count,error,sizeof(error)));
    recipe.values[0]=100;
    assert(!ts_portal_build_commands(&recipe,&short_input,stages,&stage_count,error,sizeof(error)) && strstr(error,"TOO SHORT"));
    ts_portal_recipe_default(&recipe,ts_portal_process_find("modify.brassage.1"));
    recipe.values[0]=24;short_input.frames=8821;
    assert(!ts_portal_build_commands(&recipe,&short_input,stages,&stage_count,error,sizeof(error)) && strstr(error,"TOO SHORT"));
    short_input.frames=8822;
    assert(ts_portal_build_commands(&recipe,&short_input,stages,&stage_count,error,sizeof(error)));
    short_input=instrument.current;first_sample=short_input.data[0];short_input.data[0]=INFINITY;
    assert(!ts_portal_build_commands(&recipe,&short_input,stages,&stage_count,error,sizeof(error)));
    short_input.data[0]=first_sample;
    ts_portal_recipe_default(&recipe,ts_portal_process_find("modify.brassage.2"));
    recipe.values[0]=0;assert(!ts_portal_recipe_validate(&recipe,error,sizeof(error)));
    recipe.values[0]=.125;short_input=instrument.current;short_input.frames=TS_PORTAL_MAX_FRAMES;
    assert(!ts_portal_build_commands(&recipe,&short_input,stages,&stage_count,error,sizeof(error)) && strstr(error,"OUTPUT EXCEEDS"));
    ts_portal_recipe_default(&recipe,ts_portal_process_find("modify.brassage.4"));
    recipe.values[0]=80;recipe.values[1]=450;recipe.exposed=2;
    loaded=(TsPortalLibrary){0};loaded.pins[31]=recipe;loaded.recipes[31]=recipe;
    assert(ts_portal_library_save(&loaded,path,error,sizeof(error)));
    assert(ts_portal_library_load(&saved,path,error,sizeof(error)));
    assert(!memcmp(&loaded,&saved,sizeof(saved)));remove(path);
    const char *bin=getenv("TS_TEST_CDP_BIN");
    if(bin && *bin) {
        assert(ts_cdp_runtime_discover(&runtime,bin,NULL,error,sizeof(error)));
        for(size_t i=0;i<ts_portal_process_count();++i) {
            ts_portal_recipe_default(&recipe,ts_portal_process_at(i));
            int ok=ts_cdp_run_portal(&runtime,&recipe,&instrument.current,&options,&result,error,sizeof(error));
            if(!ok)fprintf(stderr,"%s: %s\n%s\n",recipe.process_id,error,result.diagnostic);
            assert(ok && result.status==TS_CDP_RUN_OK && result.output.frames>0);
            assert(result.finite && result.output.channels==1 && !result.cleanup_failed);
            assert(result.job_directory[0]==0);
            printf("REAL CDP %-22s %zu frames %s\n",recipe.process_id,result.output.frames,ts_cdp_safety_name(result.safety));
            assert(ts_sample_hash(&instrument.current)==original);
        }
        envelope_batch_checks(&runtime);
        /* Exercise native scalar endpoints at both common rates. */
        for(size_t i=12;i<64;++i)for(int rate=0;rate<2;++rate)for(int edge=0;edge<2;++edge) {
            const TsPortalProcess *proc=ts_portal_process_at(i);
            TsSample input=instrument.current;input.sample_rate=rate?48000:44100;
            ts_portal_recipe_default(&recipe,proc);
            for(unsigned n=0;n<proc->parameter_count;++n)recipe.values[n]=edge?proc->parameters[n].maximum:proc->parameters[n].minimum;
            if(!strcmp(proc->id,"blur.blur") && edge)recipe.values[0]=(double)(input.frames/128);
            if(proc->family==TS_PORTAL_FILTER && !strcmp(proc->command,"sweeping")) {
                recipe.values[2]=20;recipe.values[3]=edge?6000:40;
            }
            float *quiet=NULL;
            if(!strcmp(proc->id,"modify.loudness.3")) {
                quiet=malloc(input.frames*sizeof(float));assert(quiet);
                for(size_t n=0;n<input.frames;++n)quiet[n]=input.data[n]*.001f;
                input.data=quiet;
            }
            int ok=ts_cdp_run_portal(&runtime,&recipe,&input,&options,&result,error,sizeof(error));
            free(quiet);
            if(!ok)fprintf(stderr,"PROCESS EDGE %s rate %u edge %d: %s\n%s\n",proc->id,input.sample_rate,edge,error,result.diagnostic);
            assert(ok && result.finite && result.output.sample_rate==input.sample_rate && !result.cleanup_failed);
            assert(result.output.frames<=TS_PORTAL_MAX_FRAMES && result.job_directory[0]==0);
            if(proc->family==TS_PORTAL_FILTER || proc->family==TS_PORTAL_DELAY) {
                double tail=0;
                for(unsigned n=0;n<proc->parameter_count;++n)
                    if(!strcmp(proc->parameters[n].id,"tail"))tail=recipe.values[n];
                assert(fabs((double)result.output.frames-input.frames-round(tail*input.sample_rate))<=1);
            }
            if(proc->family==TS_PORTAL_TIME) {
                double ratio=!strcmp(proc->id,"modify.speed.1")?1/recipe.values[0]:
                             !strcmp(proc->id,"modify.speed.2")?exp2(-recipe.values[0]/12):0;
                if(ratio>0)assert(fabs((double)result.output.frames-(double)input.frames*ratio)<2048);
                if(!strcmp(proc->id,"modify.radical.1")) {
                    assert(result.output.frames==input.frames);
                    for(size_t n=0;n<input.frames;n+=137)
                        assert(fabs(result.output.data[n]-input.data[input.frames-1-n])<.0002);
                }
            }
        }
        /* Known tones distinguish the four native modes and catch wrong mode routing.
           Independent cutoff/acuity corners also cover combinations missed by all-min/all-max. */
        test_sound_families(&runtime);
        for(int rate=0;rate<2;++rate) {
            TsSample tones={.frames=rate?48000:44100,.sample_rate=rate?48000:44100,.channels=1};
            tones.data=malloc(tones.frames*sizeof(float));assert(tones.data);
            for(size_t n=0;n<tones.frames;++n)tones.data[n]=(float)(.025*(
                sin(6.283185307179586*100*n/tones.sample_rate)+
                sin(6.283185307179586*1000*n/tones.sample_rate)+
                sin(6.283185307179586*5000*n/tones.sample_rate)));
            for(unsigned mode=1;mode<=4;++mode) {
                char id[64];snprintf(id,sizeof(id),"filter.variable.%u",mode);
                ts_portal_recipe_default(&recipe,ts_portal_process_find(id));
                recipe.values[0]=.5;
                assert(ts_cdp_run_portal(&runtime,&recipe,&tones,&options,&result,error,sizeof(error)));
                double low=tone_level(&result.output,100),mid=tone_level(&result.output,1000),high=tone_level(&result.output,5000);
                if(mode==1)assert(mid<low*.4 && mid<high*.4);
                if(mode==2)assert(mid>low*4 && mid>high*4);
                if(mode==3)assert(low>high*10);
                if(mode==4)assert(high>low*10);
                for(int corner=0;corner<2;++corner) {
                    recipe.values[0]=corner?.05:1;recipe.values[2]=corner?6000:20;
                    assert(ts_cdp_run_portal(&runtime,&recipe,&tones,&options,&result,error,sizeof(error)));
                    assert(result.finite && !result.cleanup_failed);
                }
            }
            /* Changing sweep rate/phase and phasing gain must actually change the result. */
            const char *moving[]={"filter.sweeping.2","filter.phasing.2"};
            for(unsigned i=0;i<2;++i) {
                ts_portal_recipe_default(&recipe,ts_portal_process_find(moving[i]));
                assert(ts_cdp_run_portal(&runtime,&recipe,&tones,&options,&result,error,sizeof(error)));
                uint64_t hash=ts_sample_hash(&result.output);
                if(i==0) {recipe.values[4]=3;recipe.values[6]=.5;}else recipe.values[0]=-.6;
                assert(ts_cdp_run_portal(&runtime,&recipe,&tones,&options,&result,error,sizeof(error)));
                assert(ts_sample_hash(&result.output)!=hash);
            }
            free(tones.data);
        }
        /* Granular pitch and time must be independent; sparse density must
           leave actual gaps. Use steady audio so no grain detection is needed. */
        for(int rate=0;rate<2;++rate) {
            unsigned sr=rate?48000:44100;
            TsSample tone={.frames=sr*2,.sample_rate=sr,.channels=1};
            tone.data=malloc(tone.frames*sizeof(float));assert(tone.data);
            for(size_t n=0;n<tone.frames;++n)tone.data[n]=(float)(.1*sin(6.283185307179586*500*n/sr));
            ts_portal_recipe_default(&recipe,ts_portal_process_find("modify.brassage.1"));
            for(int direction=-1;direction<=1;direction+=2) {
                recipe.values[0]=direction*12;
                assert(ts_cdp_run_portal(&runtime,&recipe,&tone,&options,&result,error,sizeof(error)));
                assert(grain_band_power(&result.output,direction>0?1000:250)>grain_band_power(&result.output,500)*4);
                assert(fabs((double)result.output.frames-tone.frames)<.3*sr);
            }
            ts_portal_recipe_default(&recipe,ts_portal_process_find("modify.brassage.2"));
            for(int edge=0;edge<2;++edge) {
                recipe.values[0]=edge?2:.5;
                assert(ts_cdp_run_portal(&runtime,&recipe,&tone,&options,&result,error,sizeof(error)));
                assert(fabs((double)result.output.frames-tone.frames/recipe.values[0])<sr*(.12/recipe.values[0]+.06));
                assert(grain_band_power(&result.output,500)>grain_band_power(&result.output,edge?1000:250)*4);
            }
            double quiet[2];
            ts_portal_recipe_default(&recipe,ts_portal_process_find("modify.brassage.5"));
            for(int edge=0;edge<2;++edge) {
                recipe.values[0]=edge?2:.125;
                assert(ts_cdp_run_portal(&runtime,&recipe,&tone,&options,&result,error,sizeof(error)));
                size_t zeros=0;
                for(size_t n=0;n<result.output.frames;++n)if(fabsf(result.output.data[n])<.0001f)++zeros;
                quiet[edge]=(double)zeros/result.output.frames;
            }
            assert(quiet[0]>.6 && quiet[1]<.4 && quiet[0]>quiet[1]+.3);
            /* Source-dependent bounds on short and lower-rate audio are also
               checked by CDP itself, not just by the Portal validator. */
            for(size_t i=26;i<ts_portal_process_count();++i) {
                if(ts_portal_process_at(i)->family!=TS_PORTAL_GRAIN)continue;
                ts_portal_recipe_default(&recipe,ts_portal_process_at(i));
                TsSample short_grain=tone;short_grain.frames=sr/10;
                if(!strcmp(recipe.process_id,"modify.brassage.4"))recipe.values[1]=200;
                assert(ts_cdp_run_portal(&runtime,&recipe,&short_grain,&options,&result,error,sizeof(error)));
                assert(result.finite && result.output.frames>0 && !result.cleanup_failed);
                short_grain=tone;short_grain.sample_rate=22050;
                assert(ts_cdp_run_portal(&runtime,&recipe,&short_grain,&options,&result,error,sizeof(error)));
                assert(result.finite && result.output.sample_rate==22050 && !result.cleanup_failed);
            }
            free(tone.data);
        }
        ts_portal_recipe_default(&recipe,ts_portal_process_find("blur.blur"));
        options.cancel_check=cancel_spectral_stage;options.cancel_userdata=&result;
        assert(!ts_cdp_run_portal(&runtime,&recipe,&instrument.current,&options,&result,error,sizeof(error)));
        assert(result.status==TS_CDP_RUN_CANCELLED && !result.cleanup_failed && !result.job_directory[0]);
        options.cancel_check=NULL;options.cancel_userdata=NULL;
        options.fault=TS_CDP_FAULT_MALFORMED_WAV;
        assert(!ts_cdp_run_portal(&runtime,&recipe,&instrument.current,&options,&result,error,sizeof(error)));
        assert(result.status==TS_CDP_RUN_FAILED && !result.cleanup_failed && !result.job_directory[0]);
        options.fault=TS_CDP_FAULT_NONE;
        assert(ts_sample_hash(&instrument.current)==original);
        ts_portal_recipe_default(&recipe,ts_portal_process_at(1));
        options.cancel_check=cancel_always;
        assert(!ts_cdp_run_portal(&runtime,&recipe,&instrument.current,&options,&result,error,sizeof(error)));
        assert(result.status==TS_CDP_RUN_CANCELLED && !result.cleanup_failed);
        options.cancel_check=NULL;options.fault=TS_CDP_FAULT_NONZERO_EXIT;
        assert(!ts_cdp_run_portal(&runtime,&recipe,&instrument.current,&options,&result,error,sizeof(error)));
        assert(result.status==TS_CDP_RUN_FAILED);
        options.fault=TS_CDP_FAULT_NONE;
    }
    if(argc>1) {
        assert(bin && *bin); /* A screenshot must show a real render. */
        ts_portal_recipe_default(&ui.portal.recipe,ts_portal_process_find("envel.warp.11"));
        ui.portal.family=TS_PORTAL_ENVELOPE+1;ui.portal.selected_tab=0;ui.portal.selected_slot=80;ui.portal.scroll=2;
        assert(ts_cdp_run_portal(&runtime,&ui.portal.recipe,&instrument.current,&options,&result,error,sizeof(error)));
        ui.portal.open=1;ui.portal.valid=1;ui.portal.source=&instrument.current;
        snprintf(ui.portal.source_name,sizeof(ui.portal.source_name),"TILE 01 METAL");
        ui.portal.result=&result.output;ui.portal.listen_result=1;ui.portal.safety=result.safety;ui.portal.peak=result.peak;
        ts_portal_wave_reset(&ui.portal.waves[0],ui.portal.source);
        ts_portal_wave_reset(&ui.portal.waves[1],ui.portal.result);
        ui.portal.waves[0].playhead=22050;ui.portal.waves[1].playhead=22050;
        ui.portal.history_count=1;ui.portal.history_selected=0;
        snprintf(ui.portal.history_names[0],24,"ENVELOPE CORRUGATE");
        snprintf(ui.portal.message,sizeof(ui.portal.message),"REAL CDP PREVIEW READY - SOURCE UNCHANGED - ENTER PREVIEW / SPACE PLAY / TAB A-B");
        ts_ui_render(&fb,&ui,&instrument);
        f=fopen(argv[1],"wb");assert(f);fprintf(f,"P6\n%d %d\n255\n",TS_UI_WIDTH,TS_UI_HEIGHT);
        for(size_t i=0;i<TS_UI_WIDTH*TS_UI_HEIGHT;++i) {unsigned char rgb[3]={(unsigned char)(fb.pixels[i]>>16),(unsigned char)(fb.pixels[i]>>8),(unsigned char)fb.pixels[i]};assert(fwrite(rgb,1,3,f)==3);}
        fclose(f);
        if(argc>2) {
            ui.portal.manage_open=1;ui.portal.manage_tab=1;ui.portal.manage_slot=2;
            ui.portal.manage_recipe=ui.portal.recipe;
            ui.portal.library.pins[2]=ui.portal.recipe;
            snprintf(ui.portal.library.pins[2].name,40,"BROKEN PULSES");
            snprintf(ui.portal.manage_name,40,"BROKEN PULSES");
            ui.portal.manage_action=TS_PORTAL_UPDATE;
            ts_ui_render(&fb,&ui,&instrument);
            f=fopen(argv[2],"wb");assert(f);fprintf(f,"P6\n%d %d\n255\n",TS_UI_WIDTH,TS_UI_HEIGHT);
            for(size_t i=0;i<TS_UI_WIDTH*TS_UI_HEIGHT;++i) {unsigned char rgb[3]={(unsigned char)(fb.pixels[i]>>16),(unsigned char)(fb.pixels[i]>>8),(unsigned char)fb.pixels[i]};assert(fwrite(rgb,1,3,f)==3);}
            fclose(f);
        }
    }
    ts_cdp_run_result_free(&result);ts_instrument_free(&instrument);
    puts("CDP Portal tests passed");return 0;
}
