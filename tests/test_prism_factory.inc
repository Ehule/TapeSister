static void check_factory_and_captured_octave(void)
{
    TsPrismControls c=settings();TsPrism dsp={0};
    assert(ts_prism_prepare(&dsp,48000));
    ts_prism_capture(&c,0);TsPrismPatch saved=c.a;
    c.matrix.step[0]=0;c.matrix.length=1;c.seq_count=1;c.sequence[0]=4;
    for(int i=0;i<TS_PRISM_FACTORY_PRESETS;++i) {
        assert(ts_prism_factory_apply(&c,i));
        assert(ts_prism_factory_find(&c)==i);
        assert(strlen(ts_prism_factory_name(i))<=18);
        assert(!memcmp(&saved,&c.a,sizeof(saved)) && c.captured==1);
        assert(c.matrix.step[0]==0 && c.seq_count==1 && c.sequence[0]==4);
        for(int j=0;j<i;++j)assert(strcmp(ts_prism_factory_name(i),ts_prism_factory_name(j)));
        ts_prism_set_controls(&dsp,&c);performance_tick(&dsp,4800);
        TsPrismView v=ts_prism_view(&dsp);
        assert(v.valid && v.group_octave==c.group_octave);
        for(int lens=0;lens<TS_PRISM_LENSES;++lens)
            assert(isfinite(v.lens[lens].cents) && isfinite(v.lens[lens].level));
    }
    c.spread+=.01f;assert(ts_prism_factory_find(&c)==-1);
    assert(!ts_prism_factory_apply(&c,-1) && !ts_prism_factory_apply(&c,28));
    /* Every letter owns its register; display and pitch interpolate together. */
    c=settings();c.drift=0;
    for(int i=0;i<TS_PRISM_STATES;++i) {c.group_octave=i%7-3;ts_prism_capture(&c,i);}
    for(int i=0;i<TS_PRISM_STATES;++i) {
        ts_prism_recall(&c,i);assert(c.group_octave==i%7-3);
    }
    c.endpoint[0]=0;c.endpoint[1]=6;c.morph_enabled=1;c.morph=.25f;
    TsPrismView v=ts_prism_control_view(&c);
    assert(v.group_octave==-1.5f && v.lens[0].cents==-1800);
    ts_prism_set_controls(&dsp,&c);performance_tick(&dsp,48000);
    v=ts_prism_view(&dsp);assert(v.group_octave==-1.5f && fabsf(v.lens[0].cents+1800)<.1f);
    ts_prism_recall(&c,0);c.matrix.step[0]=6;c.matrix.length=1;
    c.matrix.step_seconds=1;c.morph_seconds=1;c.matrix_run=1;++c.matrix_restart;
    ts_prism_set_controls(&dsp,&c);performance_tick(&dsp,24000);
    v=ts_prism_view(&dsp);assert(v.matrix_active && fabsf(v.group_octave)<.03f);
    performance_tick(&dsp,30000);assert(ts_prism_view(&dsp).group_octave==3);
    FILE *file=tmpfile();assert(file);ts_prism_write(file,&c);rewind(file);
    TsPrismControls restored;ts_prism_controls_default(&restored);char line[256];
    while(fgets(line,sizeof(line),file)) {
        char *equal=strchr(line,'=');assert(equal);*equal++=0;
        assert(ts_prism_read_field(&restored,line,equal));
    }
    fclose(file);
    for(int i=0;i<TS_PRISM_STATES;++i)assert(ts_prism_state_const(&restored,i)->group_octave==i%7-3);
    ts_prism_free(&dsp);
    puts("28 factory sounds and captured octave recall/display/morph/Matrix passed");
}
