static int stereo_cancel(void *data){(void)data;return 1;}
static void test_portal_stereo(const char *bin)
{
    TsCdpRuntime runtime;TsCdpRunOptions options;TsCdpRunResult result,reference;
    TsSample source,mono;TsPortalRecipe recipe;char error[256];
    ts_cdp_runtime_init(&runtime);ts_cdp_run_options_init(&options);
    ts_cdp_run_result_init(&result);ts_cdp_run_result_init(&reference);
    ts_sample_init(&source);ts_sample_init(&mono);
    assert(ts_cdp_runtime_discover(&runtime,bin,".",error,sizeof(error)));
    for(unsigned rate=44100;rate<=48000;rate+=3900) {
        source.frames=rate/2;source.sample_rate=rate;source.channels=2;
        source.data=malloc(source.frames*2*sizeof(float));assert(source.data);
        mono=source;mono.channels=1;mono.data=malloc(source.frames*sizeof(float));assert(mono.data);
        for(size_t i=0;i<source.frames;++i) {
            source.data[2*i]=(float)(.15*sin(6.283185307179586*440*i/rate));
            source.data[2*i+1]=(float)(.06*sin(6.283185307179586*713*i/rate));
        }
        uint64_t hash=ts_sample_hash(&source);unsigned supported=0;
        for(size_t p=0;p<ts_portal_process_count();++p) {
            const TsPortalProcess *proc=ts_portal_process_at(p);ts_portal_recipe_default(&recipe,proc);
            if(ts_portal_stereo_policy(&recipe)!=TS_PORTAL_STEREO_SPLIT)continue;
            ++supported;
            if(!ts_cdp_run_portal(&runtime,&recipe,&source,&options,&result,error,sizeof(error))) {
                fprintf(stderr,"Stereo %s: %s\n",proc->id,error);assert(0);
            }
            assert(result.output.channels==2 && result.output.frames && result.finite && !result.cleanup_failed);
            for(unsigned ch=0;ch<2;++ch) {
                for(size_t i=0;i<source.frames;++i)mono.data[i]=source.data[i*2+ch];
                assert(ts_cdp_run_portal(&runtime,&recipe,&mono,&options,&reference,error,sizeof(error)));
                assert(reference.output.frames==result.output.frames);
                for(size_t i=0;i<reference.output.frames;++i)
                    assert(fabs(reference.output.data[i]-result.output.data[2*i+ch])<1e-6);
                ts_cdp_run_result_free(&reference);
            }
            assert(ts_sample_hash(&source)==hash);ts_cdp_run_result_free(&result);
        }
        assert(supported==13);
        /* Correlation and channel isolation must survive both a chain and the
           waveform cache. Anti-phase audio must not disappear in a mono sum. */
        memset(&recipe,0,sizeof(recipe));snprintf(recipe.process_id,sizeof(recipe.process_id),"@chain");recipe.version=1;recipe.stage_count=2;
        TsPortalRecipe step;ts_portal_recipe_default(&step,ts_portal_process_find("modify.speed.2"));step.values[0]=-12;ts_portal_step_set(&recipe.stages[0],&step);
        ts_portal_recipe_default(&step,ts_portal_process_find("modify.loudness.1"));step.values[0]=.5;ts_portal_step_set(&recipe.stages[1],&step);
        assert(ts_portal_stereo_supported(&recipe));
        for(size_t i=0;i<source.frames;++i)source.data[2*i+1]=-source.data[2*i];
        assert(ts_cdp_run_portal(&runtime,&recipe,&source,&options,&result,error,sizeof(error)));
        for(size_t i=0;i<result.output.frames;++i)assert(fabs(result.output.data[2*i]+result.output.data[2*i+1])<1e-5);
        TsPortalWave wave;ts_portal_wave_reset(&wave,&source);
        assert(wave.maximum[30]>.1 && wave.right_maximum[30]>.1);
        ts_cdp_run_result_free(&result);
        ts_portal_recipe_default(&recipe,ts_portal_process_find("modify.loudness.1"));recipe.values[0]=.5;
        for(size_t i=0;i<source.frames;++i)source.data[2*i+1]=0;
        assert(ts_cdp_run_portal(&runtime,&recipe,&source,&options,&result,error,sizeof(error)));
        for(size_t i=0;i<result.output.frames;++i){assert(result.output.data[2*i+1]==0);assert(fabs(result.output.data[2*i]-.5*source.data[2*i])<4e-5);}
        ts_cdp_run_result_free(&result);
        options.cancel_check=stereo_cancel;
        assert(!ts_cdp_run_portal(&runtime,&recipe,&source,&options,&result,error,sizeof(error)) && result.status==TS_CDP_RUN_CANCELLED && !result.output.data);
        options.cancel_check=NULL;
        source.data[source.frames*2-1]=NAN;
        assert(!ts_cdp_run_portal(&runtime,&recipe,&source,&options,&result,error,sizeof(error)) && !result.output.data);
        source.data[source.frames*2-1]=0;
        assert(ts_portal_instrument_recipe(0,&recipe));assert(!ts_portal_stereo_supported(&recipe));
        assert(!ts_cdp_run_portal(&runtime,&recipe,&source,&options,&result,error,sizeof(error)) && !result.output.data);
        ts_cdp_run_result_free(&result);ts_sample_free(&source);ts_sample_free(&mono);
    }
    puts("Stereo: 13 process defaults match mono channel references at 44.1/48k; chain correlation, isolation, cancellation and validation passed");
}
