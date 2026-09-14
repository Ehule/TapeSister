static void performance_tick(TsPrism *p,unsigned frames)
{
    for(unsigned n=0;n<frames;++n) {
        TsStereoFrame out=ts_prism_process(p,(TsStereoFrame){.2f*sinf(n*.013f),-.1f*sinf(n*.013f)});
        assert(isfinite(out.l) && isfinite(out.r));
    }
}
static void check_prism_performance(void)
{
    TsPrismControls c=settings();TsPrism p={0};
    assert(ts_prism_prepare(&p,48000));
    c.lenses=24;c.focus=.1f;c.drift=.8f;c.drift_rate=.04f;
    ts_prism_capture(&c,0);
    c.mode=TS_PRISM_HARMONIC;c.lenses=7;c.stereo=.2f;c.input_shape=TS_PRISM_PLANO_CONVEX;
    c.output_shape=TS_PRISM_BICONCAVE;c.color=.8f;c.group_octave=1;c.octave_offset[3]=-1;
    ts_prism_capture(&c,1);
    c.morph_enabled=1;c.morph=.5f;c.drift=0;c.a.drift=c.b.drift=0;
    ts_prism_set_controls(&p,&c);performance_tick(&p,48000);
    TsPrismControls endpoint=c;ts_prism_recall(&endpoint,0);TsPrismView a=ts_prism_control_view(&endpoint);
    ts_prism_recall(&endpoint,1);TsPrismView b=ts_prism_control_view(&endpoint),v=ts_prism_view(&p);
    for(int i=0;i<24;++i) {
        assert(fabsf(v.lens[i].cents-(a.lens[i].cents+b.lens[i].cents)*.5f)<.1f);
        assert(fabsf(v.lens[i].pan-(a.lens[i].pan+b.lens[i].pan)*.5f)<.001f);
        assert(fabsf(v.lens[i].level-(a.lens[i].level+b.lens[i].level)*.5f)<.001f);
    }
    c.morph_seconds=.5f;c.morph_target=1;++c.morph_trigger;ts_prism_set_controls(&p,&c);
    performance_tick(&p,12000);v=ts_prism_view(&p);assert(v.morph>.73f && v.morph<.77f);
    performance_tick(&p,36000);assert(ts_prism_view(&p).morph==1);
    c.morph=0;c.morph_trigger=0;ts_prism_set_controls(&p,&c);performance_tick(&p,24000);
    assert(ts_prism_view(&p).morph==0);
    /* Sequence order is independent of lens number; unselected members sustain. */
    ts_prism_recall(&c,0);c.drift=0;c.lenses=24;c.seq_enabled=1;c.seq_rate=2;c.seq_reset=1;
    ts_prism_sequence_toggle(&c,18);ts_prism_sequence_toggle(&c,2);ts_prism_sequence_toggle(&c,7);
    ts_prism_set_controls(&p,&c);performance_tick(&p,8000);v=ts_prism_view(&p);
    assert(v.seq_lens==19 && v.lens[18].level>.9f && v.lens[2].level<.001f && v.lens[1].level>.9f);
    performance_tick(&p,24000);v=ts_prism_view(&p);assert(v.seq_lens==3 && v.lens[2].level>.9f && v.lens[18].level<.001f);
    c.lenses=5;++c.seq_reset;ts_prism_set_controls(&p,&c);performance_tick(&p,24000);
    v=ts_prism_view(&p);assert(v.seq_lens==3 && c.seq_count==3 && c.sequence[0]==18);
    c.lenses=24;++c.seq_reset;ts_prism_set_controls(&p,&c);performance_tick(&p,8000);assert(ts_prism_view(&p).seq_lens==19);
    ts_prism_sequence_toggle(&c,2);assert(c.seq_count==2 && c.sequence[1]==7);
    c.seq_enabled=0;ts_prism_set_controls(&p,&c);performance_tick(&p,12000);assert(ts_prism_view(&p).lens[7].level>.9f);
    /* Every geometry has distinct actual pitches; octaves survive closed focus. */
    c=settings();float layouts[TS_PRISM_MODE_COUNT];
    for(int mode=0;mode<TS_PRISM_MODE_COUNT;++mode) {
        c.mode=mode;v=ts_prism_control_view(&c);layouts[mode]=v.lens[5].cents;
        for(int j=0;j<mode;++j)assert(fabsf(layouts[mode]-layouts[j])>1);
    }
    c.focus=1;c.group_octave=-1;c.octave_offset[0]=2;
    assert(ts_prism_control_view(&c).lens[0].cents==1200);
    ts_prism_set_controls(&p,&c);performance_tick(&p,48000);assert(fabsf(ts_prism_view(&p).lens[0].cents-1200)<.1f && p.body_shift>.99f);
    /* Rate changes retain phase and high rates produce measurable fast motion. */
    c=settings();c.drift=2;c.drift_rate=40;ts_prism_set_controls(&p,&c);
    float previous=0;int turns=0,sign=0;
    for(int i=0;i<1000;++i) {
        performance_tick(&p,48);float value=ts_prism_view(&p).lens[23].cents;
        int next=value>previous?1:-1;if(i>100 && next!=sign)++turns;previous=value;sign=next;
    }
    assert(turns>20);
    ts_prism_free(&p);
    /* NEW cannot inherit an unlocked previous patch; VARY and locks can. */
    TsPrismControls x=settings(),y=settings();x.mode=TS_PRISM_MICRO;x.group_octave=3;x.mute_mask=123;x.pitch_offset[9]=912;
    ts_prism_generate(&x,982,0);ts_prism_generate(&y,982,0);
    assert(!memcmp(&x,&y,sizeof(x)));
    x.locks=63;TsPrismControls old=x;ts_prism_generate(&x,37,1);
    assert(!memcmp(&x,&old,sizeof(x)));
    x.locks=TS_PRISM_LOCK_PITCH|TS_PRISM_LOCK_MIX;old=x;ts_prism_generate(&x,35,0);
    assert(x.mode==old.mode && x.spread==old.spread && x.mix==old.mix && !memcmp(x.pitch_offset,old.pitch_offset,sizeof(x.pitch_offset)));
    assert(ts_prism_snap_pitch(147,1)==100 && ts_prism_snap_pitch(147,2)==150);
    /* Shared format persists endpoints, locks, octaves and hidden sequence members. */
    c=settings();c.group_octave=2;c.octave_offset[23]=-3;c.drift_rate=13.5f;
    ts_prism_capture(&c,0);c.mode=TS_PRISM_MICRO;ts_prism_capture(&c,1);
    c.morph=.375f;c.morph_enabled=1;c.locks=42;ts_prism_sequence_toggle(&c,23);c.seq_enabled=1;
    TsPrismBank *bank=calloc(1,sizeof(*bank)),*restored=calloc(1,sizeof(*restored));assert(bank && restored);
    bank->count=1;strcpy(bank->entries[0].name,"HIDDEN LENS");bank->entries[0].controls=c;
    char error[160];assert(ts_prism_bank_save(bank,"performance-prism.ini",error,sizeof(error)));
    assert(ts_prism_bank_load(restored,"performance-prism.ini",error,sizeof(error)) && !memcmp(bank,restored,sizeof(*bank)));
    FILE *bad=fopen("performance-prism-bad.ini","wb");assert(bad);fputs("TapeSister Prism Presets 1\nPreset=0\nP2.main.drift_rate.0=nan\n",bad);fclose(bad);
    assert(!ts_prism_bank_load(restored,"performance-prism-bad.ini",error,sizeof(error)) && !memcmp(bank,restored,sizeof(*bank)));
    TsSisterProjectState state,loaded;int present;
    ts_sister_project_state_init(&state,48000);state.parameters.prism=c;
    assert(ts_sister_project_state_save_file(&state,"performance-project.ini",error,sizeof(error)));
    assert(ts_sister_project_state_load_file(&loaded,"performance-project.ini",48000,&present,error,sizeof(error)));
    assert(present && !memcmp(&c,&loaded.parameters.prism,sizeof(c)));
    TsSisterPresetBank *full=calloc(1,sizeof(*full)),*full_copy=calloc(1,sizeof(*full_copy));assert(full && full_copy);
    ts_sister_preset_bank_init(full,48000);ts_sister_preset_bank_init(full_copy,48000);
    assert(ts_sister_preset_save_new(full,"PRISM PERFORMANCE",&state.parameters,48000,error,sizeof(error)));
    assert(ts_sister_preset_save(full,"performance-full.ini",error,sizeof(error)));
    assert(ts_sister_preset_load(full_copy,"performance-full.ini",48000,error,sizeof(error)));
    assert(!memcmp(&c,&full_copy->entries[full_copy->count-1].parameters.prism,sizeof(c)));
    free(full);free(full_copy);remove("performance-full.ini");
    free(bank);free(restored);remove("performance-prism.ini");remove("performance-prism-bad.ini");remove("performance-project.ini");
    puts("Prism performance clocks, morph geometry, sequencer, generation and persistence passed.");
}
