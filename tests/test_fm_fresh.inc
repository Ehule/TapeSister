static void check_fresh_palette(void)
{
    unsigned drones=0,clean=0,simple=0,extreme=0,transients=0,soft=0;
    unsigned structures=0,waves=0,interactions=0,usable=0;
    char error[160];
    for(unsigned seed=1;seed<=240;++seed) {
        TsFmPatch p,again;ts_fm_patch_fresh(&p,seed);
        memset(&again,0xff,sizeof(again));ts_fm_patch_fresh(&again,seed);
        assert(!memcmp(&p,&again,sizeof(p)));
        assert(p.active_mask && p.mutation_mask==TS_FM_MUTATE_ALL && !p.has_unison_source);
        drones+=p.drone_mode;clean+=p.filter_mode==TS_FM_FILTER_CLEAN;
        simple+=p.active_mask==1;extreme+=p.extreme_mode;transients+=p.transient_mix>0;
        soft+=!p.drone_mode && p.shape<.06f;
        structures|=1u<<p.structure;interactions|=1u<<p.interaction;
        for(int v=0;v<6;++v)if(p.active_mask&(1u<<v))waves|=1u<<p.waveforms[v];
        TsGeneratorRecipe recipe={.kind=TS_GENERATOR_FM,.has_fm_patch=1,.fm_patch=p,.seed=seed};
        ts_fm_patch_from_recipe(&recipe,&again);assert(!memcmp(&p,&again,sizeof(p)));
        if(seed<=48) {
            TsSample sound={0};
            if(ts_fm_render_sample(&sound,&p,.5f,261.625565f,44100,seed,error,sizeof(error))) {
                ++usable;assert(ts_fm_sample_is_usable(&sound));
                assert(ts_sample_peak(&sound)<=.98001f);
            } else assert(strstr(error,"no usable signal"));
            ts_sample_free(&sound);
        }
    }
    /* Test audible dimensions, not a particular order of hidden categories. */
    assert(drones>80 && drones<180 && clean>40 && simple>20 && extreme>20);
    assert(transients>20 && transients<100 && soft>20 && usable>40);
    assert(structures==((1u<<TS_FM_STRUCTURE_UNISON)-1u));
    assert(waves==((1u<<TS_FM_WAVEFORM_COUNT)-1u));
    assert(interactions==((1u<<TS_FM_INTERACTION_COUNT)-1u));
    printf("Fresh palette/240: %u sustained, %u simple, %u clean, %u extreme, %u attacks, %u soft; %u/48 usable first renders\n",
           drones,simple,clean,extreme,transients,soft,usable);
}

static void check_fresh_create_independence(void)
{
    TsInstrument *used=calloc(1,sizeof(*used)),*blank=calloc(1,sizeof(*blank)),*loaded=calloc(1,sizeof(*loaded));
    assert(used && blank && loaded);ts_instrument_init(used);ts_instrument_init(blank);ts_instrument_init(loaded);
    TsFmSeedSequence left,right;char error[160];uint32_t lseed,rseed;
    ts_fm_seed_sequence_init(&left,0x4e4557);ts_fm_seed_sequence_init(&right,0x4e4557);
    assert(ts_instrument_create_basic(used,TS_FM_WAVE_SINE,error,sizeof(error)));
    uint64_t sine=ts_sample_hash(&used->current);
    assert(ts_instrument_select_bank(used,2,error,sizeof(error)));
    for(int n=0;n<8;++n) {
        /* Poison every setting that the old Create path could inherit,
           including a frozen all-off Unison patch, duration and frequency. */
        TsFmPatch poisoned;ts_fm_patch_basic(&poisoned,TS_FM_WAVE_SQUARE);
        ts_fm_toggle_unison(&poisoned);poisoned.active_mask=0;poisoned.mutation_mask=0;
        poisoned.ratios[10]=4;poisoned.extreme_mode=1;
        used->generator.fm_patch=poisoned;used->generator.has_fm_patch=1;
        used->generator.seconds=.1f;used->generator.frequency=33;
        used->family_trajectory=1;used->family_locks=TS_FAMILY_LOCK_ALL;
        assert(ts_instrument_create_selected_fresh(used,&left,&lseed,error,sizeof(error)));
        assert(ts_instrument_create_selected_fresh(blank,&right,&rseed,error,sizeof(error)));
        assert(lseed==rseed && ts_sample_hash(&used->current)==ts_sample_hash(&blank->current));
        assert(!memcmp(&used->generator,&blank->generator,sizeof(used->generator)));
        assert(used->selected_slot==2 && used->bank[2].relation==TS_FAMILY_ROOT && used->bank[2].parent_slot==-1);
        assert(ts_sample_hash(&used->bank[0].sample)==sine);
        TsFmPatch expected;ts_fm_patch_fresh(&expected,lseed);
        assert(!memcmp(&expected,&used->generator.fm_patch,sizeof(expected)));
        assert(ts_instrument_bank_clear(blank,0,error,sizeof(error)));
    }
    assert(ts_instrument_save_recipe(used,"test-fresh-create.tsr",error,sizeof(error)));
    assert(ts_instrument_load_recipe(loaded,"test-fresh-create.tsr",error,sizeof(error)));
    assert(!memcmp(&used->generator,&loaded->generator,sizeof(used->generator)));
    TsSample regenerated={0};assert(ts_sample_generate(&regenerated,&loaded->generator,error,sizeof(error)));
    assert(ts_sample_hash(&regenerated)==ts_sample_hash(&used->current));ts_sample_free(&regenerated);
    /* Vary and Chain still follow the current material and its locks. */
    assert(ts_instrument_create_basic(used,TS_FM_WAVE_TRIANGLE,error,sizeof(error)));
    TsFmPatch original=used->generator.fm_patch;
    used->family_mutation=.3f;int destination;
    assert(ts_instrument_vary_selected(used,0,&destination,error,sizeof(error)) && destination==2);
    TsBankSlot *varied=&used->bank[2];assert(varied->patch_count>0);
    TsGeneratorRecipe *variation=&varied->patches[varied->patch_count-1].generator;
    TsFmPatch expected_variation;
    ts_fm_patch_vary(&original,variation->seed,used->family_mutation,&expected_variation);
    assert(!memcmp(&variation->fm_patch,&expected_variation,sizeof(expected_variation)));
    uint64_t source=ts_sample_hash(&used->current);
    assert(ts_instrument_vary_selected(used,1,&destination,error,sizeof(error)) && destination!=2);
    assert(ts_sample_hash(&used->bank[2].sample)==source);
    assert(used->bank[destination].parent_slot==2);
    TsFmPatch expected_child;
    ts_fm_patch_vary(&expected_variation,used->bank[destination].generator.seed,
                     used->family_mutation,&expected_child);
    assert(!memcmp(&used->bank[destination].generator.fm_patch,&expected_child,sizeof(expected_child)));
    /* Selection Create uses the same independent palette and stores it. */
    ts_instrument_set_selection(used,1000,4000);
    assert(ts_instrument_stamp_create(used,19,error,sizeof(error)));
    TsBankSlot *slot=&used->bank[used->selected_slot];
    TsFmPatch expected;ts_fm_patch_fresh(&expected,19);
    assert(slot->patch_count>0 && slot->patches[slot->patch_count-1].has_generator);
    assert(!memcmp(&slot->patches[slot->patch_count-1].generator.fm_patch,&expected,sizeof(expected)));
    ts_instrument_free(used);ts_instrument_free(blank);ts_instrument_free(loaded);
    free(used);free(blank);free(loaded);remove("test-fresh-create.tsr");
}
