#include "tapesister/cdp_portal.h"
#include "tapesister/ui.h"
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
    assert(ts_portal_process_count()==12);
    assert(ts_cdp_factory_recipe_count()==32);
    for(size_t i=0;i<ts_portal_process_count();++i) {
        const TsPortalProcess *p=ts_portal_process_at(i);
        assert(p==ts_portal_process_find(p->id));
        assert(p->parameter_count<=TS_PORTAL_PARAMS);
        for(size_t j=0;j<i;++j)assert(strcmp(p->id,ts_portal_process_at(j)->id));
        ts_portal_recipe_default(&recipe,p);
        assert(ts_portal_recipe_validate(&recipe,error,sizeof(error)));
        assert(ts_portal_build_command(&recipe,&instrument.current,&command,error,sizeof(error)));
        assert(!strcmp(command.executable,"distort"));
        assert(!strcmp(command.expected_output,"output.wav"));
        assert(command.argc<TS_CDP_MAX_COMMAND_ARGS);
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
        ts_portal_recipe_default(&ui.portal.recipe,ts_portal_process_at(1));
        ui.portal.recipe.values[0]=3;
        assert(ts_cdp_run_portal(&runtime,&ui.portal.recipe,&instrument.current,&options,&result,error,sizeof(error)));
        ui.portal.open=1;ui.portal.valid=1;ui.portal.source=&instrument.current;
        snprintf(ui.portal.source_name,sizeof(ui.portal.source_name),"TILE 01 METAL");
        ui.portal.result=&result.output;ui.portal.listen_result=1;ui.portal.safety=result.safety;ui.portal.peak=result.peak;
        ts_portal_wave_reset(&ui.portal.waves[0],ui.portal.source);
        ts_portal_wave_reset(&ui.portal.waves[1],ui.portal.result);
        ui.portal.waves[0].playhead=22050;ui.portal.waves[1].playhead=22050;
        ui.portal.history_count=1;ui.portal.history_selected=0;
        snprintf(ui.portal.history_names[0],24,"CYCLE REPEAT");
        snprintf(ui.portal.message,sizeof(ui.portal.message),"REAL CDP PREVIEW READY - SOURCE UNCHANGED - ENTER PREVIEW / SPACE PLAY / TAB A-B");
        ts_ui_render(&fb,&ui,&instrument);
        f=fopen(argv[1],"wb");assert(f);fprintf(f,"P6\n%d %d\n255\n",TS_UI_WIDTH,TS_UI_HEIGHT);
        for(size_t i=0;i<TS_UI_WIDTH*TS_UI_HEIGHT;++i) {unsigned char rgb[3]={(unsigned char)(fb.pixels[i]>>16),(unsigned char)(fb.pixels[i]>>8),(unsigned char)fb.pixels[i]};assert(fwrite(rgb,1,3,f)==3);}
        fclose(f);
    }
    ts_cdp_run_result_free(&result);ts_instrument_free(&instrument);
    puts("CDP Portal tests passed");return 0;
}
