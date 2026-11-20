static void eq_editor_feed(int slot,unsigned bin)
{
    for(unsigned i=0;i<TS_EQ_SPECTRUM_FRAMES;i+=64) {
        TsStereoFrame in[64],out[64],key[64]={{0}};
        for(unsigned j=0;j<64;++j){float x=.2f*sinf((float)(6.283185307179586*bin*(i+j)/TS_EQ_SPECTRUM_FRAMES));in[j]=(TsStereoFrame){x,-x};}
        ts_matrix_effects_process_span(&audio.sister.matrix_effects,slot,in,key,out,64,1);
    }
}
static void eq_spectrum_button(void)
{
    int w,h;SDL_GetWindowSize(eq_editor.window,&w,&h);
    SDL_Event e;SDL_zero(e);e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=eq_editor.id;
    e.button.button=SDL_BUTTON_LEFT;e.button.x=340*w/640;e.button.y=55*h/400;
    assert(eq_editor_event(&e,NULL,0,&audio,&ui,&sister,&pages,&bank));
    e.type=SDL_MOUSEBUTTONUP;assert(eq_editor_event(&e,NULL,0,&audio,&ui,&sister,&pages,&bank));
}
static void eq_live_spectrum_ui(void)
{
    ui.canvas_eq_open=1;ui.master_eq_rate=8000;
    master_eq_spectrum_tick(&audio,&ui,1,900);assert(ui.master_eq_rate==8000);
    ui.canvas_eq_open=0;
    static TsMatrixControls c;ts_matrix_default(&c);
    int first=ts_matrix_effect_add(&c,TS_MATRIX_FX_EQ),other=ts_matrix_effect_add(&c,TS_MATRIX_FX_EQ);
    int board=ts_matrix_effect_add(&c,TS_MATRIX_FX_PEDALBOARD);
    c.effect[board].board.slot[0].type=TS_SISTER_FX_EQ;
    c.effect[board].board.slot[0].enabled=1;c.effect[board].board.slot[0].mix=1;
    ts_master_eq_default(&c.effect[board].board.slot[0].eq);c.effect[board].board.slot[0].eq.enabled=1;
    assert(ts_matrix_effects_restore(&audio.sister.matrix_effects,&c,48000));assert(ts_matrix_set(&audio.sister.matrix,&c,48000));
    matrix_window.controls=c;
    eq_editor_open(0,&audio,&ui,1,c.effect[first].id,0,sister.window_id);
    assert(eq_editor.visible && !eq_editor.model->master_eq_spectrum_off);
    eq_editor_spectrum(0,&audio,&ui,1,1000);assert(atomic_load(&audio.sister.effect_eq_spectrum[0].request)&1u);
    eq_editor_feed(first,85);eq_editor_feed(other,341);eq_editor_spectrum(0,&audio,&ui,1,1100);
    TsEqSpectrumView *view=&eq_editor.model->master_eq_spectrum;assert(view->valid);
    int peak=0;for(int i=1;i<TS_EQ_SPECTRUM_POINTS;++i)if(view->db[i]>view->db[peak])peak=i;
    double hz=20*pow(ts_master_eq_max_hz(48000)/20.,peak/(double)(TS_EQ_SPECTRUM_POINTS-1));assert(hz>950 && hz<1050);
    int history=matrix_window.history_cursor;
    eq_spectrum_button();eq_editor_spectrum(0,&audio,&ui,1,1200);
    assert(eq_editor.model->master_eq_spectrum_off && !view->valid && !(atomic_load(&audio.sister.effect_eq_spectrum[0].request)&1u));
    assert(matrix_window.history_cursor==history);
    eq_spectrum_button();eq_editor_spectrum(0,&audio,&ui,1,1300);eq_editor_feed(first,85);eq_editor_spectrum(0,&audio,&ui,1,1400);assert(view->valid);
    SDL_HideWindow(eq_editor.window);eq_editor_spectrum(0,&audio,&ui,1,1500);
    assert(!view->valid && !(atomic_load(&audio.sister.effect_eq_spectrum[0].request)&1u));
    SDL_ShowWindow(eq_editor.window);eq_editor_spectrum(0,&audio,&ui,0,1600); /* Independent of the main window. */
    assert(atomic_load(&audio.sister.effect_eq_spectrum[0].request)&1u);
    eq_editor_feed(first,85); /* Leave a queued capture while switching targets. */
    eq_editor_open(0,&audio,&ui,1,c.effect[other].id,0,sister.window_id);eq_editor_spectrum(0,&audio,&ui,1,1700);
    assert(!view->valid);eq_editor_feed(other,341);eq_editor_spectrum(0,&audio,&ui,1,1800);assert(view->valid);
    peak=0;for(int i=1;i<TS_EQ_SPECTRUM_POINTS;++i)if(view->db[i]>view->db[peak])peak=i;
    hz=20*pow(ts_master_eq_max_hz(48000)/20.,peak/(double)(TS_EQ_SPECTRUM_POINTS-1));assert(hz>3800 && hz<4200);
    eq_editor_feed(other,341);audio.output_rate=44100;
    assert(ts_matrix_effects_restore(&audio.sister.matrix_effects,&c,44100));eq_editor_spectrum(0,&audio,&ui,1,1900);
    assert(!view->valid && eq_editor.model->master_eq_rate==44100);
    eq_editor_feed(other,341);eq_editor_spectrum(0,&audio,&ui,1,2000);assert(view->valid && view->rate==44100);
    uint32_t child=c.effect[board].board.slot[0].id;
    eq_editor_open(0,&audio,&ui,2,c.effect[board].id,child,sister.window_id);eq_editor_spectrum(0,&audio,&ui,1,2100);
    TsSisterFxSlotControls swap=c.effect[board].board.slot[0];c.effect[board].board.slot[0]=c.effect[board].board.slot[1];c.effect[board].board.slot[1]=swap;
    assert(ts_matrix_effects_restore(&audio.sister.matrix_effects,&c,44100));assert(ts_matrix_set(&audio.sister.matrix,&c,44100));
    eq_editor_spectrum(0,&audio,&ui,1,2200);eq_editor_feed(board,85);eq_editor_spectrum(0,&audio,&ui,1,2300);assert(view->valid);
    c.effect[board].board.slot[1].type=TS_SISTER_FX_EMPTY;assert(ts_matrix_set(&audio.sister.matrix,&c,44100));
    eq_editor_spectrum(0,&audio,&ui,1,2400);assert(!eq_editor.visible && !view->valid);
    assert(!(atomic_load(&audio.sister.effect_eq_spectrum[0].request)&1u));
    audio.output_rate=48000;assert(ts_sister_post_fx_init(&audio.sister.post_fx,48000));
    TsSisterFxControls fx;ts_sister_fx_controls_default(&fx);fx.slot_count=1;
    for(int i=0;i<TS_SISTER_FX_SLOT_COUNT;++i)fx.slot[i]=ts_sister_fx_empty_slot();
    fx.slot[0].type=TS_SISTER_FX_EQ;fx.slot[0].id=91;fx.slot[0].enabled=1;fx.slot[0].mix=1;fx.slot[0].placement=TS_SISTER_FX_PLACE_POST;
    ts_master_eq_default(&fx.slot[0].eq);fx.slot[0].eq.enabled=1;ts_sister_fx_controls_sanitize(&fx);
    audio.sister.parameters.fx=fx;ts_sister_post_fx_sync_controls(&audio.sister.post_fx,&fx);
    eq_editor_open(0,&audio,&ui,0,0,91,sister.window_id);eq_editor_spectrum(0,&audio,&ui,1,2500);
    for(int i=0;i<TS_EQ_SPECTRUM_FRAMES;++i){TsStereoFrame in={.1f*sinf(i*.13f),-.1f*sinf(i*.13f)};
        ts_sister_post_fx_process_pre(&audio.sister.post_fx,in,0);ts_sister_post_fx_process(&audio.sister.post_fx,3,in,0);}
    eq_editor_spectrum(0,&audio,&ui,1,2600);assert(view->valid && !eq_editor.spectrum[0].valid && eq_editor.spectrum[4].valid && !eq_editor.spectrum[1].valid);
    fx.slot[0].placement=TS_SISTER_FX_PLACE_HEADS;audio.sister.parameters.fx=fx;ts_sister_post_fx_sync_controls(&audio.sister.post_fx,&fx);
    eq_editor_spectrum(0,&audio,&ui,1,2700);assert(!view->valid);
    for(int i=0;i<TS_EQ_SPECTRUM_FRAMES;++i)for(int head=0;head<4;++head)
        ts_sister_post_fx_process(&audio.sister.post_fx,(size_t)head,(TsStereoFrame){.1f*sinf(i*.13f),-.1f*sinf(i*.13f)},0);
    eq_editor_spectrum(0,&audio,&ui,1,2800);
    assert(view->valid && !eq_editor.spectrum[0].valid && !eq_editor.spectrum[4].valid);
    for(int i=1;i<4;++i)assert(eq_editor.spectrum[i].valid);
    eq_editor_spectrum(0,&audio,&ui,1,5600);for(int i=0;i<TS_EQ_SPECTRUM_POINTS;++i)assert(view->db[i]==TS_EQ_SPECTRUM_FLOOR_DB);
    eq_editor_open(0,&audio,&ui,-1,0,0,sister.window_id);eq_editor_spectrum(0,&audio,&ui,1,5700);
    assert(atomic_load(&audio.sister.eq_spectrum.request)&1u);
    for(int i=0;i<TS_SISTER_FX_LOCATION_COUNT;++i)assert(!(atomic_load(&audio.sister.effect_eq_spectrum[i].request)&1u));
    eq_editor_hide();eq_editor_spectrum(0,&audio,&ui,1,5800);assert(!(atomic_load(&audio.sister.eq_spectrum.request)&1u));
    puts("Effect EQ spectra: real audio, independent targets, reorder, hide/toggle, rate change, decay and master restoration passed.");
}
