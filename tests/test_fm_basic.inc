static void check_all_off_is_silent(void)
{
    TsGeneratorRecipe recipe={.kind=TS_GENERATOR_FM,.seed=0};
    TsFmPatch p;TsSample rendered={0};char error[160];
    ts_fm_patch_from_recipe(&recipe,&p);
    /* The supplied silent tile opened this seed-zero MIRROR patch, whose
       independent transient exciter used to survive an empty voice mask. */
    assert(p.transient_mix>0 && !p.drone_mode);
    for(int structure=0;structure<TS_FM_STRUCTURE_COUNT;++structure)
        for(int mode=0;mode<TS_FM_FILTER_MODE_COUNT;++mode)
            for(int drone=0;drone<2;++drone) {
                p.structure=structure;p.filter_mode=mode;p.drone_mode=drone;
                p.extreme_mode=1;p.transient_mix=1;p.active_mask=0;
                assert(ts_fm_render_preview(&rendered,&p,.1f,440,44100,123,error,sizeof(error)));
                assert(ts_sample_peak(&rendered)==0);
            }
    /* Hidden, inactive second-bank bits cannot activate the exciter in FM. */
    p.structure=0;p.active_mask=1u<<11;p.drone_mode=0;
    assert(ts_fm_render_preview(&rendered,&p,.1f,440,44100,123,error,sizeof(error)));
    assert(ts_sample_peak(&rendered)==0);
    ts_sample_free(&rendered);
}

static void check_basic_oscillators(void)
{
    const int waveforms[]={TS_FM_WAVE_SINE,TS_FM_WAVE_SQUARE,TS_FM_WAVE_SAW,TS_FM_WAVE_TRIANGLE};
    char error[160];
    for(unsigned rate=44100;rate<=48000;rate+=3900)for(int i=0;i<4;++i) {
        TsFmPatch p;TsSample sound={0},again={0};
        ts_fm_patch_basic(&p,(TsFmWaveform)waveforms[i]);
        assert(p.active_mask==1 && p.drone_mode && !p.transient_mix && !p.feedback && !p.interaction_mix);
        assert(p.filter_mode==TS_FM_FILTER_CLEAN);
        assert(ts_fm_render_sample(&sound,&p,2,440,rate,123,error,sizeof(error)));
        assert(ts_fm_render_sample(&again,&p,2,440,rate,456,error,sizeof(error)));
        assert(ts_sample_hash(&sound)==ts_sample_hash(&again));
        double fundamental=magnitude(&sound,440);
        double second=magnitude(&sound,880)/fundamental;
        double third=magnitude(&sound,1320)/fundamental;
        assert(fundamental>.45 && ts_sample_peak(&sound)<=.72001f);
        if(i==0) { assert(fabs(fundamental-.72)<.001 && second<.0001 && third<.0001); }
        if(i==1) { assert(second<.0001 && third>.32 && third<.34); }
        if(i==2) { assert(second>.49 && second<.51 && third>.32 && third<.34); }
        if(i==3) { assert(second<.0001 && third>.10 && third<.12); }
        TsFmPatch original=p;
        assert(ts_fm_toggle_unison(&p) && p.filter_mode==TS_FM_FILTER_CLEAN && p.active_mask==4095);
        if(i==0) {
            for(int v=0;v<TS_FM_UNISON_VOICE_COUNT;++v)p.ratios[v]=1;
            assert(ts_fm_render_sample(&again,&p,2,440,rate,123,error,sizeof(error)));
            /* Even coherent clean carriers must remain sine waves, rather
               than acquiring clipping harmonics when their levels add up. */
            assert(ts_sample_peak(&again)<=.98001f);
            assert(magnitude(&again,1320)/magnitude(&again,440)<.0001);
        }
        p.ratios[11]*=1.3f;
        assert(!ts_fm_toggle_unison(&p) && !memcmp(&p,&original,sizeof(p)));
        assert(!ts_fm_control_available(&p,TS_FM_PAGE_FILTER,0));
        assert(ts_fm_control_available(&p,TS_FM_PAGE_FILTER,5));
        char label[32],value[32];
        ts_fm_control_format(&p,TS_FM_PAGE_FILTER,5,label,sizeof(label),value,sizeof(value));
        assert(!strcmp(value,"CLEAN"));
        assert(ts_fm_step_control(&p,TS_FM_PAGE_FILTER,5,-1,0));
        assert(p.filter_mode==TS_FILTER_BANDPASS && ts_fm_control_available(&p,TS_FM_PAGE_FILTER,0));
        ts_sample_free(&sound);ts_sample_free(&again);
    }
}

static void check_basic_create(void)
{
    TsInstrument *a=calloc(1,sizeof(*a)),*b=calloc(1,sizeof(*b));char error[160];
    assert(a && b);ts_instrument_init(a);ts_instrument_init(b);
    assert(ts_instrument_select_bank(a,2,error,sizeof(error)));
    assert(ts_instrument_create_basic(a,TS_FM_WAVE_SINE,error,sizeof(error)));
    assert(a->selected_slot==2 && !a->bank[0].occupied);
    assert(!strcmp(a->current.name,"OSC SINE") && a->generator.has_fm_patch);
    TsFmPatch p=a->generator.fm_patch;assert(p.filter_mode==TS_FM_FILTER_CLEAN && p.active_mask==1);
    ts_fm_toggle_unison(&p);p.ratios[10]*=2;
    assert(ts_instrument_apply_fm_patch(a,&p,error,sizeof(error)));
    assert(ts_instrument_save_recipe(a,"test-basic-oscillator.tsr",error,sizeof(error)));
    assert(ts_instrument_load_recipe(b,"test-basic-oscillator.tsr",error,sizeof(error)));
    assert(!memcmp(&a->generator.fm_patch,&b->generator.fm_patch,sizeof(p)));
    assert(ts_sample_hash(&a->current)==ts_sample_hash(&b->current));
    p=b->generator.fm_patch;assert(!ts_fm_toggle_unison(&p));
    assert(p.active_mask==1 && p.filter_mode==TS_FM_FILTER_CLEAN && p.waveforms[0]==TS_FM_WAVE_SINE);
    /* Starting a basic preset replaces all edited Unison settings. */
    assert(ts_instrument_create_basic(a,TS_FM_WAVE_SQUARE,error,sizeof(error)));
    assert(!a->generator.fm_patch.has_unison_source && a->generator.fm_patch.active_mask==1);
    uint64_t original=ts_sample_hash(&a->current);
    a->bank[2].locked=1;
    assert(!ts_instrument_create_basic(a,TS_FM_WAVE_SAW,error,sizeof(error)));
    assert(ts_sample_hash(&a->current)==original);a->bank[2].locked=0;
    TsSample before={0};assert(ts_sample_clone(&before,&a->current,error,sizeof(error)));
    a->has_selection=1;a->selection_first=4410;a->selection_last=22050;
    assert(ts_instrument_create_basic(a,TS_FM_WAVE_TRIANGLE,error,sizeof(error)));
    assert(a->current.frames==before.frames && a->has_selection);
    assert(!memcmp(a->current.data,before.data,4410*sizeof(float)));
    assert(!memcmp(a->current.data+22050,before.data+22050,(before.frames-22050)*sizeof(float)));
    assert(memcmp(a->current.data+4410,before.data+4410,(22050-4410)*sizeof(float)));
    assert(ts_instrument_undo(a,error,sizeof(error)) && ts_sample_hash(&a->current)==original);
    ts_sample_free(&before);
    ts_instrument_free(a);ts_instrument_free(b);free(a);free(b);remove("test-basic-oscillator.tsr");
}
