static void test_factory_recipes(void)
{
    static TsPortalLibrary library,loaded;
    static TsPortalUi ui;
    char error[256];TsPortalRecipe r;TsCdpRecipeValues v,back;
    ts_portal_ui_init(&ui);ui.family=TS_PORTAL_FACTORY+1;
    assert(ts_portal_filter_slot(&ui,31,&r)>=0 && ts_portal_filter_slot(&ui,32,&r)<0);
    for(size_t i=0;i<32;++i) {
        const TsCdpRecipe *f=ts_cdp_factory_recipe_at(i);
        const TsPortalProcess *p=ts_portal_factory_process_at(i);
        assert(p==ts_portal_process_find(p->id) && ts_portal_factory_find(p->id)==f);
        assert(p->parameter_count<13 && p->family==TS_PORTAL_FACTORY);
        ts_cdp_recipe_values_default(f,&v);v.seed=UINT64_MAX;v.tuning_hz=432;
        ts_portal_factory_recipe(f,&v,&r);
        assert(ts_portal_recipe_validate(&r,error,sizeof(error)));
        ts_portal_factory_values(&r,&back);
        assert(back.seed==v.seed && back.tuning_hz==432 && back.mix==v.mix);
        assert(!memcmp(back.controls,v.controls,sizeof(v.controls)));
        if(f->seed_supported) {
            unsigned n=p->parameter_count-1;double original_seed=r.values[n];
            r.values[n]=original_seed==1?2:1;ts_portal_factory_values(&r,&back);
            assert(back.seed==(uint64_t)r.values[n]);r.values[n]=original_seed;
        }
        for(unsigned n=0;n<f->control_count;++n) {
            const TsCdpControlSpec *c=&f->controls[n];
            if(c->type!=TS_CDP_CONTROL_ENUMERATED)continue;
            for(size_t k=0;k<c->valid_value_count;++k) {
                r.values[n]=c->valid_values[k];assert(ts_portal_recipe_validate(&r,error,sizeof(error)));
                if(k+1<c->valid_value_count)assert(ts_portal_parameter_nudge(p,n,r.values[n],1)==c->valid_values[k+1]);
                char label[64];ts_portal_parameter_format(p,n,r.values[n],label,sizeof(label));
                if(c->value_names)assert(!strcmp(label,c->value_names[k]));
            }
        }
        library.recipes[i]=r;
    }
    assert(ts_portal_library_save(&library,"portal-factory-roundtrip.recipes",error,sizeof(error)));
    assert(ts_portal_library_load(&loaded,"portal-factory-roundtrip.recipes",error,sizeof(error)));
    assert(!memcmp(&library,&loaded,sizeof(library)));
    TsPortalRecipe *chain=&library.pins[0];snprintf(chain->process_id,64,"@chain");
    snprintf(chain->name,sizeof(chain->name),"FACTORY CHAIN");chain->version=1;chain->stage_count=2;
    ts_portal_step_set(&chain->stages[0],&library.recipes[0]);
    ts_portal_recipe_default(&r,ts_portal_process_at(0));ts_portal_step_set(&chain->stages[1],&r);
    assert(ts_portal_library_save(&library,"portal-factory-roundtrip.recipes",error,sizeof(error)));
    assert(ts_portal_library_load(&loaded,"portal-factory-roundtrip.recipes",error,sizeof(error)));
    assert(!memcmp(&library,&loaded,sizeof(library)));
    ui.library=library;ui.tab=2;assert(ts_portal_filter_slot(&ui,0,&r)>=0 && ts_portal_filter_slot(&ui,1,&r)<0);
    ts_portal_recipe_default(&r,ts_portal_process_find("factory.reform"));r.values[0]=3;
    assert(!ts_portal_recipe_validate(&r,error,sizeof(error)));
    r=library.recipes[0];r.values[14]=4294967296.;assert(!ts_portal_recipe_validate(&r,error,sizeof(error)));
    r=library.recipes[0];r.values[15]=.5;assert(!ts_portal_recipe_validate(&r,error,sizeof(error)));
    r=library.recipes[0];r.values[13]=NAN;assert(!ts_portal_recipe_validate(&r,error,sizeof(error)));
    remove("portal-factory-roundtrip.recipes");
}

/* Compare the new route with the original instrument renderer, including the
   legacy mix operation. Nonseeded random instruments cannot be hash compared. */
static void test_factory_native(const char *bin)
{
    TsCdpRuntime runtime;TsCdpRunOptions options;TsCdpRunResult legacy,portal;
    TsSample input,mixed;char error[2048];TsPortalRecipe r;TsCdpRecipeValues v;
    ts_cdp_runtime_init(&runtime);ts_cdp_run_options_init(&options);
    ts_cdp_run_result_init(&legacy);ts_cdp_run_result_init(&portal);
    ts_sample_init(&input);ts_sample_init(&mixed);
    assert(ts_cdp_runtime_discover(&runtime,bin,NULL,error,sizeof(error)));
    assert(ts_sample_load_wav(&input,"assets/tapesister_welcome.wav",error,sizeof(error)));
    for(size_t i=0;i<32;++i) {
        const TsCdpRecipe *f=ts_cdp_factory_recipe_at(i);ts_cdp_recipe_values_default(f,&v);
        if(!strcmp(f->id,"splinter"))v.controls[0]=.1f;
        v.seed=UINT64_C(0xfedcba9876543210);ts_portal_factory_recipe(f,&v,&r);
        int old_ok=ts_cdp_run_recipe(&runtime,f,&v,&input,&options,&legacy,error,sizeof(error));
        if(!old_ok)fprintf(stderr,"LEGACY %s: %s\n%s\n",f->id,error,legacy.diagnostic);
        assert(old_ok);
        int ok=ts_cdp_run_portal(&runtime,&r,&input,&options,&portal,error,sizeof(error));
        if(!ok)fprintf(stderr,"FACTORY %s: %s\n%s\n",f->id,error,portal.diagnostic);
        assert(ok && portal.finite && portal.output.channels==1 && !portal.cleanup_failed && !portal.job_directory[0]);
        int repeatable=strcmp(f->id,"granulate") && (f->deterministic || f->seed_supported);
        if(repeatable)assert(ts_sample_hash(&legacy.output)==ts_sample_hash(&portal.output));
        printf("FACTORY %s: %zu frames, %s\n",f->id,portal.output.frames,repeatable?"legacy audio matches":"random render valid");fflush(stdout);
        if(f->mix_policy==TS_CDP_MIX_EXACT_FRAMES && legacy.output.frames==input.frames && f->deterministic) {
            v.mix=.5f;ts_portal_factory_recipe(f,&v,&r);
            assert(ts_transform_mix_samples(&input,&legacy.output,.5f,f->mix_policy,&mixed,error,sizeof(error)));
            assert(ts_cdp_run_portal(&runtime,&r,&input,&options,&portal,error,sizeof(error)));
            assert(ts_sample_hash(&mixed)==ts_sample_hash(&portal.output));ts_sample_free(&mixed);
        }
    }
    ts_portal_recipe_default(&r,ts_portal_process_find("factory.reform"));
    options.cancel_check=cancel_always;
    assert(!ts_cdp_run_portal(&runtime,&r,&input,&options,&portal,error,sizeof(error)));
    assert(portal.status==TS_CDP_RUN_CANCELLED && !portal.cleanup_failed && !portal.job_directory[0]);
    ts_cdp_run_result_free(&legacy);ts_cdp_run_result_free(&portal);ts_sample_free(&input);
}
