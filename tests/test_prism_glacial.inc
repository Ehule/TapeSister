/* A low supported sample rate accelerates hour-long audio clocks while still
   running the same hop scheduler and transitions used by the real callback. */
static void check_prism_glacial(void)
{
    TsPrismControls c;ts_prism_controls_default(&c);
    assert(c.morph_seconds==240 && c.matrix.step_seconds==240);
    assert(fabsf(ts_prism_time_from_normalized(1)-3600)<.01f);
    char label[24];ts_prism_time_label(label,sizeof(label),240);assert(!strcmp(label,"04:00"));
    for(int matrix=0;matrix<2;++matrix) {
        TsPrism p={0};assert(ts_prism_prepare(&p,441));
        c=settings();c.lenses=2;c.focus=1;c.drift=0;ts_prism_capture(&c,0);
        c.spread=1;ts_prism_capture(&c,1);c.morph_seconds=c.matrix.step_seconds=3600;
        c.morph_enabled=1;c.morph=0;c.morph_target=1;c.morph_trigger=1;
        if(matrix){c.matrix.length=1;c.matrix.loop=0;c.matrix.step[0]=1;c.matrix_run=1;c.matrix_restart=1;}
        ts_prism_set_controls(&p,&c);performance_tick(&p,441*1800);
        TsPrismView v=ts_prism_view(&p);float midpoint=matrix?v.matrix_morph:v.morph;
        assert(fabsf(midpoint-.5f)<.0001f);
        performance_tick(&p,441*1800+TS_PRISM_HOP*2);v=ts_prism_view(&p);
        assert((matrix?v.matrix_morph:v.morph)==1);
        if(matrix)assert(!v.matrix_running);
        ts_prism_free(&p);
    }
    TsPrismBank a={0},b={0};a.count=1;a.entries[0].controls=c;
    snprintf(a.entries[0].name,sizeof(a.entries[0].name),"GLACIAL");char error[160];
    assert(ts_prism_bank_save(&a,"test-prism-glacial.ini",error,sizeof(error)));
    assert(ts_prism_bank_load(&b,"test-prism-glacial.ini",error,sizeof(error)));
    assert(b.entries[0].controls.morph_seconds==3600 && b.entries[0].controls.matrix.step_seconds==3600);
    remove("test-prism-glacial.ini");
}
