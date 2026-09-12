static void test_instrument_recipes(void)
{
    static TsPortalLibrary lib,loaded,before;static TsPortalUi ui;TsPortalRecipe r,step;char error[256];
    const char *path="portal-instruments-test.recipes";
    assert(ts_portal_instrument_count()==11);
    for(unsigned n=0;n<ts_portal_instrument_count();++n) {
        assert(ts_portal_instrument_recipe(n,&r));assert(ts_portal_recipe_validate(&r,error,sizeof(error)));
        lib.recipes[n]=r;lib.pins[n]=r;ts_portal_recipe_exact(&lib.pins[n]);
        for(unsigned i=0;i<r.stage_count;++i)assert(!lib.pins[n].stages[i].exposed);
        if(n<8)for(unsigned i=0;i<3;++i)assert(lib.pins[n].stages[i].macros[0].configured);
    }
    ts_portal_step_get(&lib.recipes[0].stages[0],&step);
    TsPortalRecipe original=step;
    assert(!ts_portal_macro_set(&step,0,"BAD",NAN,100,error,sizeof(error)));
    assert(!ts_portal_macro_set(&step,0,"BAD",100,25,error,sizeof(error)));
    assert(!ts_portal_macro_set(&step,0,"BAD|NAME",25,100,error,sizeof(error)));
    assert(!memcmp(&original,&step,sizeof(step)));
    assert(ts_portal_macro_set(&step,0,"SHARDS",25,100,error,sizeof(error)));
    lib.recipes[12]=step;
    assert(ts_portal_library_save(&lib,path,error,sizeof(error)));
    FILE *f=fopen(path,"rb");char header[64];assert(f && fgets(header,sizeof(header),f));fclose(f);
    assert(!strcmp(header,"TSCDPPORTAL 3\n"));assert(ts_portal_library_load(&loaded,path,error,sizeof(error)));
    assert(!memcmp(&lib,&loaded,sizeof(lib)));
    before=loaded;
    const char *bad[]={"TSCDPPORTAL 3\nC R 0 1 |TRUNCATED\n", "TSCDPPORTAL 3\nC R 0 1 |BAD\nM 99 |\n",
                      "TSCDPPORTAL 3\nC R 0 1 |BAD\nM 2 |\nK 0 1 2 |A\nK 0 1 2 |B\n"};
    for(unsigned i=0;i<3;++i) {
        f=fopen(path,"wb");assert(f);fputs(bad[i],f);fclose(f);
        assert(!ts_portal_library_load(&loaded,path,error,sizeof(error)) && !memcmp(&loaded,&before,sizeof(loaded)));
    }
    ts_portal_ui_init(&ui);ui.chain=lib.recipes[0];ui.chain_active=1;ui.chain_stage=0;ui.macro_view=1;
    ts_portal_step_get(&ui.chain.stages[0],&ui.recipe);
    assert(ts_portal_control_count(&ui)==4);TsPortalControl control;
    assert(ts_portal_control_at(&ui,3,&control) && control.stage==2 && !strcmp(control.macro->name,"OUTPUT"));
    ui.recipe.values[0]=77;assert(ts_portal_control_at(&ui,0,&control) && control.value==77);
    TsPortalStep changed=ui.chain.stages[0];snprintf(changed.macros[0].name,sizeof(changed.macros[0].name),"RENAMED");
    assert(ts_portal_step_equal(&changed,&ui.chain.stages[0]));changed.values[0]=90;
    assert(!ts_portal_step_equal(&changed,&ui.chain.stages[0]));
    ui.family=TS_PORTAL_INSTRUMENTS+1;assert(ts_portal_filter(&ui,3,&r));assert(!ts_portal_filter(&ui,11,&r));
    snprintf(ui.query,sizeof(ui.query),"sidebands");assert(ts_portal_filter(&ui,0,&r) && !strcmp(r.name,"BROKEN SIGNAL"));
    remove(path);
}
static void test_instrument_audio(const char *bin)
{
    TsCdpRuntime runtime;TsCdpRunOptions options;TsCdpRunResult result;TsSample source;TsPortalRecipe r;
    char error[256];ts_cdp_runtime_init(&runtime);ts_cdp_run_options_init(&options);ts_cdp_run_result_init(&result);ts_sample_init(&source);
    assert(ts_cdp_runtime_discover(&runtime,bin,".",error,sizeof(error)));
    for(int rate=44100;rate<=48000;rate+=3900) {
        source.sample_rate=(uint32_t)rate;source.frames=(size_t)rate;source.channels=1;source.data=malloc(source.frames*sizeof(float));assert(source.data);
        for(size_t i=0;i<source.frames;++i)source.data[i]=(float)(.12*sin(6.283185307179586*440*i/rate)+.04*sin(6.283185307179586*713*i/rate));
        for(unsigned n=0;n<ts_portal_instrument_count();++n) {
            assert(ts_portal_instrument_recipe(n,&r));
            /* New syllable instruments are designed for articulated material. */
            for(size_t i=0;i<source.frames;++i) {
                double t=(double)i/rate,envelope=n<4?1:.1+.9*pow(fmax(0,sin(6.283185307179586*6*t)),2);
                source.data[i]=(float)(envelope*(.12*sin(6.283185307179586*440*t)+.04*sin(6.283185307179586*713*t)));
            }
            assert(ts_cdp_run_portal(&runtime,&r,&source,&options,&result,error,sizeof(error)));
            assert(result.output.channels==1 && result.output.frames>0 && !result.cleanup_failed);
            double energy=0;for(size_t i=0;i<result.output.frames;++i){assert(isfinite(result.output.data[i]));energy+=fabs(result.output.data[i]);}assert(energy>1);
            if(n==1 || n==3)assert(result.output.frames>source.frames*1.7 && result.output.frames<source.frames*2.3);
            ts_cdp_run_result_free(&result);
            for(unsigned stage=0;stage<r.stage_count;++stage)for(unsigned param=0;param<TS_PORTAL_PARAMS;++param) {
                TsPortalMacro *macro=&r.stages[stage].macros[param];if(!macro->configured)continue;
                double initial=r.stages[stage].values[param];
                for(int edge=0;edge<2;++edge) {
                    r.stages[stage].values[param]=edge?macro->maximum:macro->minimum;
                    if(!ts_cdp_run_portal(&runtime,&r,&source,&options,&result,error,sizeof(error))) {
                        fprintf(stderr,"Instrument %s stage %u param %u edge %d: %s\n",r.name,stage,param,edge,error);assert(0);
                    }
                    assert(result.output.frames && !result.cleanup_failed);
                    for(size_t i=0;i<result.output.frames;++i)assert(isfinite(result.output.data[i]));
                    ts_cdp_run_result_free(&result);
                }
                r.stages[stage].values[param]=initial;
            }
        }
        ts_sample_free(&source);
    }
    puts("Factory chains: defaults, macro endpoints and duration checks passed at 44.1/48 kHz");
}
