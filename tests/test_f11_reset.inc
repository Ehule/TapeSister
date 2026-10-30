/* Real row dispatch: Reset must rearm a previously stopped timeline looper,
   and manual/ARP/tracker reset must rewind the correct runtime owner. */
static void test_f11_reset(const char *directory)
{
    TsMosaic *saved=a.mosaic,*m=ts_mosaic_create();assert(m);a.mosaic=m;
    unsigned selected=a.sister.performance_transport;a.sister.performance_transport=PERF_MOSAIC;
    TsMosaicSource *source=ts_mosaic_source(m,&bank.current,error,sizeof(error));assert(source);
    TsMosaicEvent *first=ts_mosaic_add(m,source,0,0);assert(first);first->live=1;
    TsMosaicEvent *future=ts_mosaic_copy(m,first->id,.25,150);assert(future);
    performance_page_set(&a,PERF_OVERVIEW);performance_loop_owner(&a);
    int y=PERF_ROWS_Y+4*PERF_ROW_STEP+16;double energy[4];
    assert(ts_mosaic_live_launch(m,first->id,0));render(energy);
    perf_input_click(640,y,SDL_BUTTON_LEFT);assert(m->live_paused);
    render(energy);double time=m->time;
    perf_input_click(730,y,SDL_BUTTON_LEFT);
    assert(!m->live_paused && !ts_mosaic_live_count(m) && ts_mosaic_live_ready(m) && m->time==time);
    assert(!m->live[0].elapsed && !m->live[1].state);
    /* Explicit Play from READY restarts the prior set, never idle future cards. */
    perf_input_click(640,y,SDL_BUTTON_LEFT);assert(m->live[0].state && !m->live[0].elapsed && !m->live[1].state);
    render(energy);assert(m->live[0].elapsed>0);
    ts_mosaic_live_stop(m,0);render(energy);assert(!ts_mosaic_live_ready(m));
    /* Reset is meaningful while idle, unlike the old active-player-only hit gate. */
    m->live_trigger_floor=7;perf_input_click(730,y,SDL_BUTTON_LEFT);
    assert(m->live_trigger_floor<m->time && !ts_mosaic_live_ready(m));
    perf_input_click(730,PERF_ROWS_Y+16,SDL_BUTTON_LEFT);assert(m->time==0 && !m->playing && !m->live_paused);
    perf_input_click(640,PERF_ROWS_Y+16,SDL_BUTTON_LEFT);render(energy);
    assert(m->playing && m->live[0].state && m->live[0].elapsed>0 && !m->live[1].state);
    for(int i=0;i<6;++i)render(energy);
    assert(m->live[1].state && m->live[1].elapsed>0); /* Later crossings remain armed. */
    perf_input_click(730,y,SDL_BUTTON_LEFT);render(energy);
    assert(!m->live_paused && !ts_mosaic_live_count(m));
    /* Timeline seek must take over from a reset sequence, without stale ownership. */
    assert(ts_mosaic_sequence_cycle(m));render(energy);
    perf_input_click(730,y,SDL_BUTTON_LEFT);assert(!m->sequence.running && m->live_reset_sequence);
    ts_mosaic_seek(m,0);m->playing=1;render(energy);
    assert(!m->live_reset_sequence && !m->sequence.running && m->live[0].elapsed>0);
    m->playing=0;ts_mosaic_live_stop(m,0);render(energy);
    /* Every Live Tiles backend rewinds, retaining key-up/latch/generation ownership. */
    assert(ts_note_bank_start(&a.notes,&bank,TS_AUDITION_CURRENT,12,1,48000)==TS_NOTE_STARTED);
    TsPerformanceBank *groups[]={&a.performance,&a.mosaic_source_performance,&a.tile_launchers,&a.sister.performance};
    TsNoteEvent note;assert(ts_note_event_qwerty(&note,0,60));
    for(int k=0;k<4;++k) {
        TsPerformancePreparedSource prepared={.generation=ts_performance_prepare_sample(groups[k],&bank.current),
            .tuning={60,0},.first=0,.last=bank.current.frames,.looping=1,.mode=TS_LOOP_FORWARD};
        assert(prepared.generation && ts_performance_trigger_prepared(groups[k],&prepared,1,&note,1,48000)==1);
        ts_performance_sample_release(prepared.generation);
        for(int i=0;i<100;++i)ts_performance_read_stereo(groups[k],NULL);
    }
    for(int i=0;i<100;++i)ts_note_bank_read_stereo(&a.notes);
    uint64_t ids[4];TsPerformanceGeneration *generations[4];
    for(int k=0;k<4;++k){assert(groups[k]->voices[0].position>0);ids[k]=groups[k]->voices[0].group_id;generations[k]=groups[k]->voices[0].generation;}
    perf_input_click(730,PERF_ROWS_Y+3*PERF_ROW_STEP+16,SDL_BUTTON_LEFT);
    assert(a.notes.voices[0].active && a.notes.voices[0].latched && a.notes.voices[0].position==0);
    for(int k=0;k<4;++k) {
        assert(groups[k]->voices[0].active && groups[k]->voices[0].latched && groups[k]->voices[0].position==0);
        assert(groups[k]->voices[0].group_id==ids[k] && groups[k]->voices[0].generation==generations[k]);
        ts_performance_release_event(groups[k],&note); /* Latched note still owns its physical trigger. */
        assert(groups[k]->voices[0].active);ts_performance_clear(groups[k]);ts_performance_collect_retired(groups[k]);
    }
    ts_note_bank_clear(&a.notes);
    /* ARP Reset rewinds the manually selected anchor, note, chain and LFO. */
    TsKeyboardSequenceBank sequence=ts_keyboard_sequence_export(&a.keyboard_sequence);
    a.keyboard_sequence.bank.slot[0].count=1;a.keyboard_sequence.bank.slot[0].notes[0]=60;
    a.keyboard_sequence.bank.selected=0;a.keyboard_sequence.active_slot=2;
    a.keyboard_sequence.slot_cursor=3;a.keyboard_sequence.cursor=4;
    a.keyboard_sequence.lfo_phase=.6;a.keyboard_sequence.slot_elapsed=2;
    perf_input_click(730,PERF_ROWS_Y+2*PERF_ROW_STEP+16,SDL_BUTTON_LEFT);
    assert(!ts_keyboard_sequence_active(&a.keyboard_sequence) && a.keyboard_sequence.active_slot==0);
    assert(!a.keyboard_sequence.cursor && !a.keyboard_sequence.slot_cursor && !a.keyboard_sequence.slot_elapsed && !a.keyboard_sequence.lfo_phase);
    ts_keyboard_sequence_set_bank(&a.keyboard_sequence,&sequence);
    /* Song start is order zero, even when its pattern is not Pattern 00. */
    int order=song.orders[0];song.orders[0]=7;
    perf_input_click(730,PERF_ROWS_Y+PERF_ROW_STEP+16,SDL_BUTTON_LEFT);
    PerformanceStatus status=performance_status(0,&a);
    assert(!ts_tapehead_running() && status.tracker.order==0 && status.tracker.row==0 && status.tracker.pattern==7);
    song.orders[0]=order;assert(ts_tapehead_transport_locked(2));
    assert(ts_mosaic_live_launch(m,first->id,0));render(energy);
    perf_input_click(730,y,SDL_BUTTON_LEFT);assert(ts_mosaic_live_ready(m));
    performance_transport.selected=4;perf_input_shot(directory,"control-center-reset-ready");
    a.sister.performance_transport=selected;a.mosaic=saved;ts_mosaic_free(m);performance_loop_owner(&a);
}
