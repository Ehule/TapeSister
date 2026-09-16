/* Behavioral Matrix coverage: clocks, current/next bank ownership, the existing
   DSP path, exact manual handoff, and all three on-disk formats. */
static TsPrismControls matrix_settings(void)
{
    TsPrismControls c=settings();
    for(int i=0;i<TS_PRISM_STATES;++i) {
        c.lenses=2+i;c.spread=.08f*i;c.drift=0;c.pitch_offset[1]=i*13;
        ts_prism_capture(&c,i);
    }
    ts_prism_recall(&c,0);c.matrix_run=1;c.matrix_restart=1;
    c.matrix.step_seconds=.05f;c.morph_seconds=.05f;c.matrix.loop=0;
    return c;
}
static void matrix_wait_step(TsPrism *p,int step)
{
    unsigned timeout=48000;
    while((!p->matrix.active || p->matrix.waiting || p->matrix.step!=step) && timeout--)
        performance_tick(p,64);
    assert(timeout && p->matrix.running);
}
static void check_prism_matrix(void)
{
    TsPrism p={0};assert(ts_prism_prepare(&p,48000));
    TsPrismControls c=matrix_settings();c.matrix.length=64;
    for(int i=0;i<64;++i)c.matrix.step[i]=i%12;
    ts_prism_set_controls(&p,&c);
    int last=-1,visited=0;
    for(unsigned frame=0;frame<160000;++frame) {
        TsStereoFrame out=ts_prism_process(&p,(TsStereoFrame){.1f,-.2f});
        assert(isfinite(out.l) && isfinite(out.r));
        TsPrismView v=ts_prism_view(&p);
        if(v.matrix_step!=last) {
            assert(v.matrix_step==last+1 && v.matrix_to==v.matrix_step%12);
            /* No accumulated hop-sized timing drift over 64 cells. */
            assert(abs((int)frame-v.matrix_step*2400)<=64);
            last=v.matrix_step;++visited;
        }
    }
    assert(visited==64 && !p.matrix.running && p.matrix.active && p.matrix.position==1);
    ts_prism_set_controls(&p,&c);performance_tick(&p,512);assert(!p.matrix.running); /* Ordinary publication doesn't restart. */
    ++c.matrix_restart;c.matrix.loop=1;c.matrix.length=2;
    ts_prism_set_controls(&p,&c);matrix_wait_step(&p,0);matrix_wait_step(&p,1);matrix_wait_step(&p,0);
    TsPrismControls held=c;assert(ts_prism_matrix_export(&p,&held));
    ts_prism_set_controls(&p,&held);assert(!p.matrix.active && !held.matrix_run);
    assert(!memcmp(&held.a,&c.a,sizeof(c.a)) && held.active_pair_valid);
    ts_prism_free(&p);

    /* A cell holds its current pair; a subsequent visit reads the new bank.
       Uninitialized and blank destinations both hold rather than reset Prism. */
    memset(&p,0,sizeof(p));assert(ts_prism_prepare(&p,48000));
    c=matrix_settings();c.matrix.length=5;c.matrix.step_seconds=.1f;
    c.matrix.step[0]=3;c.matrix.step[1]=-1;c.matrix.step[2]=11;c.matrix.step[3]=3;c.matrix.step[4]=3;
    c.captured&=~(1<<11);ts_prism_set_controls(&p,&c);matrix_wait_step(&p,0);
    TsPrismPatch first=p.matrix.pair[1];
    c.pitch_offset[1]=733;ts_prism_capture(&c,3);ts_prism_set_controls(&p,&c);
    assert(!memcmp(&first,&p.matrix.pair[1],sizeof(first)));
    performance_tick(&p,3000);assert(p.matrix.step==0 && p.matrix.position==1); /* Morph shorter than STEP: hold. */
    matrix_wait_step(&p,1);assert(!p.matrix.missing && p.matrix.letter[1]==3);
    assert(!memcmp(&p.matrix.pair[0],&first,sizeof(first)));
    matrix_wait_step(&p,2);assert(p.matrix.missing && p.matrix.letter[1]==3);
    matrix_wait_step(&p,3);assert(p.matrix.pair[1].pitch_offset[1]==733);
    assert(p.matrix.letter[0]==3 && p.matrix.letter[1]==3 && p.matrix.pair[0].pitch_offset[1]!=733);
    held=c;performance_tick(&p,1200);float position=p.matrix.position;
    assert(ts_prism_matrix_export(&p,&held));ts_prism_set_controls(&p,&held);
    assert(held.endpoint[0]==3 && held.endpoint[1]==3 && p.morph_position==position);
    assert(held.active_pair[0].pitch_offset[1]!=held.active_pair[1].pitch_offset[1]);
    assert(ts_prism_pair_ready(&held));
    ts_prism_free(&p);

    /* Matrix uses the existing interpolation/audio engine. Run the same A>B
       transition through manual transport and Matrix on identical input. */
    TsPrism manual={0},matrix={0};assert(ts_prism_prepare(&manual,48000) && ts_prism_prepare(&matrix,48000));
    c=matrix_settings();c.matrix.length=1;c.matrix.step[0]=1;c.matrix.step_seconds=.1f;c.morph_seconds=.1f;
    TsPrismControls normal=c;normal.matrix_run=0;normal.morph_enabled=1;normal.morph=0;normal.morph_target=1;normal.morph_trigger=1;
    ts_prism_set_controls(&manual,&normal);ts_prism_set_controls(&matrix,&c);
    for(int n=0;n<10000;++n) {
        TsStereoFrame in={.2f*sinf(n*.057f),-.13f*cosf(n*.033f)};
        TsStereoFrame a=ts_prism_process(&manual,in),b=ts_prism_process(&matrix,in);
        assert(fabsf(a.l-b.l)<.0001f && fabsf(a.r-b.r)<.0001f);
    }
    ts_prism_free(&manual);ts_prism_free(&matrix);

    /* Starting in the middle joins the nearer end, retaining exact geometry;
       STOP exports the same frozen endpoints and scalar to the manual fader. */
    memset(&p,0,sizeof(p));assert(ts_prism_prepare(&p,48000));
    c=matrix_settings();c.matrix_run=0;c.morph_enabled=1;c.morph=.3f;c.morph_seconds=.7f;
    c.matrix.step_seconds=.05f;c.matrix.length=1;c.matrix.step[0]=7;
    ts_prism_set_controls(&p,&c);performance_tick(&p,64);
    c.matrix_run=1;ts_prism_set_controls(&p,&c);performance_tick(&p,64);
    assert(p.matrix.waiting && p.matrix.position==.3f && p.matrix.target==0);
    performance_tick(&p,128);position=p.matrix.position;held=c;
    assert(ts_prism_matrix_export(&p,&held));ts_prism_set_controls(&p,&held);
    assert(p.morph_position==position && !p.matrix.active);
    performance_tick(&p,64);assert(p.morph_position==position);
    held.morph=.8f;ts_prism_set_controls(&p,&held);performance_tick(&p,64);assert(p.morph_position==.8f);
    held.matrix_run=1;++held.matrix_restart;ts_prism_set_controls(&p,&held);matrix_wait_step(&p,0);
    assert(p.matrix.morph_duration==p.matrix.duration); /* Long Morph Time capped only for Matrix. */
    assert(p.controls.morph_seconds==.7f);
    held.matrix_run=0;ts_prism_matrix_export(&p,&held);ts_prism_set_controls(&p,&held);
    held.matrix_run=1;++held.matrix_restart;ts_prism_set_controls(&p,&held);
    held.matrix_run=0;ts_prism_set_controls(&p,&held);performance_tick(&p,128);
    assert(!p.matrix.active && !p.matrix.pending); /* PLAY then STOP before audio callback. */
    ts_prism_free(&p);

    /* Config and parked transition round-trip, transport always loads stopped. */
    TsPrismBank *bank=calloc(1,sizeof(*bank)),*loaded=calloc(1,sizeof(*loaded));assert(bank && loaded);
    c=held;for(int i=0;i<64;++i)c.matrix.step[i]=i%13-1;
    c.matrix.length=63;c.matrix.loop=1;c.matrix.step_seconds=3.75f;c.matrix_run=1;c.matrix_restart=99;
    bank->count=1;strcpy(bank->entries[0].name,"MATRIX");bank->entries[0].controls=c;
    TsPrismControls expected=c;expected.matrix_run=0;expected.matrix_restart=0;
    char error[160];assert(ts_prism_bank_save(bank,"matrix-prism.ini",error,sizeof(error)));
    assert(ts_prism_bank_load(loaded,"matrix-prism.ini",error,sizeof(error)));
    assert(!memcmp(&loaded->entries[0].controls,&expected,sizeof(expected)));
    TsSisterProjectState project,restored;int present;ts_sister_project_state_init(&project,48000);project.parameters.prism=c;
    assert(ts_sister_project_state_save_file(&project,"matrix-project.ini",error,sizeof(error)));
    assert(ts_sister_project_state_load_file(&restored,"matrix-project.ini",48000,&present,error,sizeof(error)));
    assert(present && !memcmp(&restored.parameters.prism,&expected,sizeof(expected)));
    TsSisterPresetBank *full=calloc(1,sizeof(*full)),*copy=calloc(1,sizeof(*copy));assert(full && copy);
    ts_sister_preset_bank_init(full,48000);ts_sister_preset_bank_init(copy,48000);
    assert(ts_sister_preset_save_new(full,"MATRIX",&project.parameters,48000,error,sizeof(error)));
    assert(ts_sister_preset_save(full,"matrix-sister.ini",error,sizeof(error)));
    assert(ts_sister_preset_load(copy,"matrix-sister.ini",48000,error,sizeof(error)));
    assert(!memcmp(&copy->entries[copy->count-1].parameters.prism,&expected,sizeof(expected)));
    free(full);free(copy);free(bank);free(loaded);
    remove("matrix-prism.ini");remove("matrix-project.ini");remove("matrix-sister.ini");
    puts("Prism Matrix order, clocks, bank ownership, manual handoff, DSP parity and persistence passed.");
}
