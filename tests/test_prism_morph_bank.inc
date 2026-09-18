static void check_prism_morph_bank(void)
{
    TsPrismControls c=settings();
    for(int state=0;state<TS_PRISM_STATES;++state) {
        c.mode=state%TS_PRISM_MODE_COUNT;c.lenses=2+state%(TS_PRISM_LENSES-1);c.drift=0;
        c.pitch_offset[3]=state*31.f;c.pan_offset[3]=state*.025f;
        c.drift_rate=.1f+state;c.octave_offset[23]=state%7-3;
        ts_prism_capture(&c,state);
    }
    assert(c.captured==((1<<TS_PRISM_STATES)-1));
    TsPrismControls stored=c;
    for(int state=0;state<TS_PRISM_STATES;++state) {
        ts_prism_recall(&c,state);
        assert(c.lenses==2+state%(TS_PRISM_LENSES-1) && c.pitch_offset[3]==state*31.f);
        assert(!memcmp(ts_prism_state_const(&c,state),ts_prism_state_const(&stored,state),sizeof(TsPrismPatch)));
    }
    ts_prism_generate(&c,107,0);ts_prism_generate(&c,108,1);
    for(int state=0;state<TS_PRISM_STATES;++state)
        assert(!memcmp(ts_prism_state_const(&c,state),ts_prism_state_const(&stored,state),sizeof(TsPrismPatch)));
    c.endpoint[0]=2;c.endpoint[1]=25;ts_prism_recall(&c,2);
    c.morph_enabled=1;c.morph=.5f;c.morph_trigger=0;
    TsPrism p={0};assert(ts_prism_prepare(&p,48000));ts_prism_set_controls(&p,&c);performance_tick(&p,24000);
    TsPrismControls end=c;ts_prism_recall(&end,2);TsPrismView left=ts_prism_control_view(&end);
    ts_prism_recall(&end,25);TsPrismView right=ts_prism_control_view(&end),middle=ts_prism_view(&p);
    for(int i=0;i<TS_PRISM_LENSES;++i) {
        assert(fabsf(middle.lens[i].cents-(left.lens[i].cents+right.lens[i].cents)*.5f)<.1f);
        assert(fabsf(middle.lens[i].pan-(left.lens[i].pan+right.lens[i].pan)*.5f)<.001f);
    }
    c.morph_target=1;c.morph_seconds=.1f;c.morph_trigger=1;
    ts_prism_set_controls(&p,&c);performance_tick(&p,12000);assert(ts_prism_view(&p).morph==1);
    /* Replace only the silent end: the sounding Z state stays exactly Z. */
    c.endpoint[0]=5;c.morph=1;c.morph_trigger=0;ts_prism_set_controls(&p,&c);performance_tick(&p,12000);
    for(int i=0;i<TS_PRISM_LENSES;++i)assert(fabsf(ts_prism_view(&p).lens[i].cents-right.lens[i].cents)<.1f);
    c.morph_target=0;++c.morph_trigger;ts_prism_set_controls(&p,&c);performance_tick(&p,12000);
    assert(ts_prism_view(&p).morph==0);ts_prism_free(&p);
    c.morph=.375f;c.morph_trigger=0;
    TsPrismBank *bank=calloc(1,sizeof(*bank)),*loaded=calloc(1,sizeof(*loaded));assert(bank && loaded);
    bank->count=1;strcpy(bank->entries[0].name,"ALPHABET STATES");bank->entries[0].controls=c;
    char error[160];assert(ts_prism_bank_save(bank,"prism-alphabet.ini",error,sizeof(error)));
    assert(ts_prism_bank_load(loaded,"prism-alphabet.ini",error,sizeof(error)) && !memcmp(bank,loaded,sizeof(*bank)));
    TsSisterProjectState project,restored;int present;
    ts_sister_project_state_init(&project,48000);project.parameters.prism=c;
    assert(ts_sister_project_state_save_file(&project,"prism-alphabet-project.ini",error,sizeof(error)));
    assert(ts_sister_project_state_load_file(&restored,"prism-alphabet-project.ini",48000,&present,error,sizeof(error)));
    assert(present && !memcmp(&restored.parameters.prism,&c,sizeof(c)));
    TsSisterPresetBank *full=calloc(1,sizeof(*full)),*copy=calloc(1,sizeof(*copy));assert(full && copy);
    ts_sister_preset_bank_init(full,48000);ts_sister_preset_bank_init(copy,48000);
    assert(ts_sister_preset_save_new(full,"ALPHABET STATES",&project.parameters,48000,error,sizeof(error)));
    assert(ts_sister_preset_save(full,"prism-alphabet-sister.ini",error,sizeof(error)));
    assert(ts_sister_preset_load(copy,"prism-alphabet-sister.ini",48000,error,sizeof(error)));
    assert(!memcmp(&copy->entries[copy->count-1].parameters.prism,&c,sizeof(c)));free(full);free(copy);
    /* Previous A-L banks load their original letters and leave M-Z empty.
       Versions 2 (bank) and 3 (Matrix) both used the same P3 letter fields. */
    for(int version=2;version<=3;++version) {
        FILE *source=fopen("prism-alphabet.ini","rb"),*old=fopen("prism-legacy-twelve.ini","wb");
        assert(source && old);char field[512];
        while(fgets(field,sizeof(field),source)) {
            if(!strncmp(field,"P3.",3) && field[3]>='M' && field[3]<='Z' && field[4]=='.')continue;
            if(version==2 && !strncmp(field,"P4.",3))continue;
            if(!strncmp(field,"TapeSister Prism Presets",23))fprintf(old,"TapeSister Prism Presets %d\n",version);
            else if(!strncmp(field,"P2.perf.captured.",17))fputs("P2.perf.captured.0=4095\n",old);
            else if(!strncmp(field,"P3.bank.endpoint.1=",19))fputs("P3.bank.endpoint.1=11\n",old);
            else fputs(field,old);
        }
        fclose(source);fclose(old);
        assert(ts_prism_bank_load(loaded,"prism-legacy-twelve.ini",error,sizeof(error)));
        const TsPrismControls *legacy=&loaded->entries[0].controls;
        assert(legacy->captured==4095 && legacy->endpoint[0]==5 && legacy->endpoint[1]==11);
        TsPrismPatch empty={0};
        for(int state=0;state<26;++state)
            assert(!memcmp(ts_prism_state_const(legacy,state),state<12?ts_prism_state_const(&c,state):&empty,sizeof(empty)));
    }
    remove("prism-legacy-twelve.ini");
    FILE *current=fopen("prism-alphabet.ini","rb"),*legacy=fopen("prism-two.ini","wb");assert(current && legacy);
    char line[512];
    while(fgets(line,sizeof(line),current)) {
        if(!strncmp(line,"P3.",3) || !strncmp(line,"P4.",3))continue;
        if(!strncmp(line,"TapeSister Prism Presets",23))fputs("TapeSister Prism Presets 1\n",legacy);
        else if(!strncmp(line,"P2.perf.captured.",17))fputs("P2.perf.captured.0=3\n",legacy);
        else fputs(line,legacy);
    }
    fclose(current);fclose(legacy);
    assert(ts_prism_bank_load(loaded,"prism-two.ini",error,sizeof(error)));
    assert(loaded->entries[0].controls.captured==3 && loaded->entries[0].controls.endpoint[0]==0 && loaded->entries[0].controls.endpoint[1]==1);
    assert(!memcmp(&loaded->entries[0].controls.a,&c.a,sizeof(c.a)) && !memcmp(&loaded->entries[0].controls.b,&c.b,sizeof(c.b)));
    *bank=*loaded;
    FILE *bad=fopen("prism-alphabet-bad.ini","wb");assert(bad);
    fputs("TapeSister Prism Presets 2\nPreset=0\nP3.[.enabled.0=1\n",bad);fclose(bad);
    assert(!ts_prism_bank_load(loaded,"prism-alphabet-bad.ini",error,sizeof(error)) && !memcmp(bank,loaded,sizeof(*bank)));
    /* Z is the last state; malformed indices cannot write beyond the bank. */
    TsPrismControls invalid=c;invalid.captured=-1;invalid.endpoint[1]=26;invalid.matrix.step[63]=26;
    ts_prism_controls_sanitize(&invalid);
    assert(invalid.captured==67108863 && invalid.endpoint[1]==1 && invalid.matrix.step[63]==-1);
    assert(!ts_prism_state(&invalid,-1) && !ts_prism_state(&invalid,26));
    free(bank);free(loaded);
    remove("prism-alphabet.ini");remove("prism-alphabet-project.ini");remove("prism-alphabet-sister.ini");remove("prism-two.ini");remove("prism-alphabet-bad.ini");
    puts("26 Prism states, selected-pair DSP and legacy/new persistence passed.");
}
