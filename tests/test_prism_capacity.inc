static void check_extended_capacity(void)
{
    TsPrism p={0};assert(ts_prism_prepare(&p,48000));
    TsPrismControls c=settings();c.lenses=TS_PRISM_LENSES;c.stereo=0;c.solo_mask=1<<23;
    float reference=0;
    for(int step=0;step<5;++step) {
        c.trim_db[23]=step==1?-6:0;
        c.mute_mask=step==2?1<<23:0;
        c.lenses=step==3?12:24;
        ts_prism_set_controls(&p,&c);TsStereoFrame out={0};
        for(int n=0;n<48000;++n)out=ts_prism_process(&p,(TsStereoFrame){.1f,-.1f});
        if(step==0){reference=out.l;assert(reference>.005f);}
        if(step==1)assert(fabsf(out.l/reference-powf(10,-6.f/20))<.0001f);
        if(step==2)assert(out.l==0 && out.r==0);
        if(step==3)assert(out.l>.02f); /* An inactive high solo does not mute the first twelve. */
        if(step==4)assert(fabsf(out.l-reference)<.00001f);
    }
    c.pitch_offset[23]=750;c.pan_offset[23]=-.4f;c.trim_db[23]=9;
    c.trim_db[12]=-8;c.mute_mask=1<<13;c.solo_mask=(1<<22)|(1<<23);
    char error[160];int present;TsSisterProjectState state,loaded;
    ts_sister_project_state_init(&state,48000);state.parameters.prism=c;
    assert(ts_sister_project_state_save_file(&state,"prism-24.ini",error,sizeof(error)));
    assert(ts_sister_project_state_load_file(&loaded,"prism-24.ini",48000,&present,error,sizeof(error)));
    assert(present && !memcmp(&c,&loaded.parameters.prism,sizeof(c)));remove("prism-24.ini");
    TsSisterPresetBank bank,restored;ts_sister_preset_bank_init(&bank,48000);
    assert(ts_sister_preset_save_new(&bank,"TWENTY FOUR",&state.parameters,48000,error,sizeof(error)));
    assert(ts_sister_preset_save(&bank,"prism-24-presets.ini",error,sizeof(error)));
    ts_sister_preset_bank_init(&restored,48000);
    assert(ts_sister_preset_load(&restored,"prism-24-presets.ini",48000,error,sizeof(error)));
    assert(!memcmp(&c,&restored.entries[restored.count-1].parameters.prism,sizeof(c)));
    remove("prism-24-presets.ini");
    /* A pre-expansion shaped project keeps its count and defaults extra edits. */
    FILE *f=fopen("prism-24.ini","wb");assert(f);
    fputs("TapeSister Sister Project State\nVersion=16\nPageCount=1\nActivePage=0\nRoutes=0\n"
          "PrismEnabled=1\nPrismLenses=12\nPrismPitchOffset11=400\nPrismInputShape=3\n"
          "PrismOutputShape=1\nPrismColor=0.75\nPrismMuteMask=2048\n",f);fclose(f);
    assert(ts_sister_project_state_load_file(&loaded,"prism-24.ini",48000,&present,error,sizeof(error)));
    assert(loaded.parameters.prism.lenses==12 && loaded.parameters.prism.pitch_offset[11]==400);
    assert(loaded.parameters.prism.input_shape==3 && loaded.parameters.prism.output_shape==1 && loaded.parameters.prism.color==.75f);
    assert(loaded.parameters.prism.mute_mask==2048);
    for(int i=12;i<24;++i)assert(!loaded.parameters.prism.pitch_offset[i] &&
        !loaded.parameters.prism.pan_offset[i] && !loaded.parameters.prism.trim_db[i]);
    remove("prism-24.ini");
    ts_prism_reset_lenses(&c);
    assert(!c.mute_mask && !c.solo_mask && !c.pitch_offset[23] && !c.trim_db[12]);
    c.lenses=100;c.mute_mask=c.solo_mask=-1;ts_prism_controls_sanitize(&c);
    assert(c.lenses==24 && c.mute_mask==0xffffff && c.solo_mask==0xffffff);
    ts_prism_free(&p);
}
