static void test_f11_looper(const char *directory)
{
    TsMosaic *saved=a.mosaic,*m=ts_mosaic_create();assert(m);a.mosaic=m;
    unsigned selected=a.sister.performance_transport;a.sister.performance_transport=PERF_MOSAIC;
    TsMosaicSource *source=ts_mosaic_source(m,&bank.current,error,sizeof(error));assert(source);
    TsMosaicEvent *first=ts_mosaic_add(m,source,0,0);assert(first);first->live=1;first->duration=.1;
    TsMosaicEvent *next=ts_mosaic_copy(m,first->id,1,150);assert(next);next->duration=.13;
    TsMosaicEvent *bg=ts_mosaic_copy(m,first->id,0,300);assert(bg);bg->live_order=-1;
    TsMosaicEvent *idle=ts_mosaic_copy(m,bg->id,10,450);assert(idle);
    assert(ts_mosaic_sequence_cycle(m));assert(ts_mosaic_live_launch(m,bg->id,0));
    performance_page_set(&a,PERF_OVERVIEW);performance_transport.selected=4;
    assert(!(performance_status(0,&a).selected&PERF_LOOPS));
    double energy[4];render(energy);m->time=2;m->playing=1;
    double elapsed=m->live[2].elapsed;
    perf_input_click(230,64,SDL_BUTTON_LEFT);assert(!m->playing && !m->live_paused && m->time==2);
    render(energy);assert(m->live[2].elapsed>elapsed); /* Out: independent timeline. */
    int y=PERF_ROWS_Y+4*PERF_ROW_STEP+16;
    assert(ts_note_bank_start(&a.notes,&bank,TS_AUDITION_CURRENT,12,0,48000)==TS_NOTE_STARTED);
    perf_input_click(640,y,SDL_BUTTON_LEFT);assert(m->live_paused);
    TsMosaicLive held=m->live[2];int index=m->sequence.index;
    double position=m->voices[2].position[0];render(energy);render(energy);
    assert(!memcmp(&held,&m->live[2],sizeof(held)) && m->sequence.index==index && m->voices[2].position[0]==position);
    assert(performance_status(0,&a).notes>0); /* Manual keyboard remains alive. */
    perf_input_shot(directory,"control-center-looper-paused");
    perf_input_click(640,y,SDL_BUTTON_LEFT);assert(!m->live_paused);render(energy);assert(m->live[2].elapsed>held.elapsed);
    /* IN participates only in F11/Shift+Space. No saved per-card Follow flags. */
    uint64_t hash=ts_mosaic_hash(m);perf_input_click(210,y,SDL_BUTTON_LEFT);
    assert(performance_status(0,&a).selected==(PERF_MOSAIC|PERF_LOOPS));
    assert(ts_mosaic_hash(m)==hash && a.sister.performance_transport==PERF_MOSAIC);
    m->playing=1;perf_input_click(230,64,SDL_BUTTON_LEFT);assert(!m->playing && m->live_paused);
    elapsed=m->live[2].elapsed;render(energy);assert(m->live[2].elapsed==elapsed);
    perf_input_click(30,64,SDL_BUTTON_LEFT);assert(m->playing && !m->live_paused && m->live[2].elapsed==elapsed);
    render(energy);perf_input_shot(directory,"control-center-looper-playing");
    double time=m->time;
    assert(performance_command_for(PERF_MOSAIC,0,window,0,&a,&u,&pages,&bank,NULL,48000));
    assert(!m->live_paused);render(energy);assert(m->time==time);
    /* Local Home leaves the main cursor and idle cards alone. */
    perf_input_click(730,y,SDL_BUTTON_LEFT);assert(m->live_paused && m->time==time && m->live[2].elapsed==0);
    assert(m->sequence.pending && m->sequence.index==0 && !m->live[3].state);
    /* Group Home explicitly returns both selected starts. */
    perf_input_click(480,64,SDL_BUTTON_LEFT);assert(m->time==0 && m->live_paused);
    perf_input_click(30,64,SDL_BUTTON_LEFT);render(energy);assert(m->playing && !m->live_paused && m->live[2].elapsed>0);
    perf_input_click(210,y,SDL_BUTTON_LEFT);assert(!performance_transport.loops_joined);
    /* A new project/load cannot inherit this temporary group membership. */
    perf_input_click(210,y,SDL_BUTTON_LEFT);assert(performance_transport.loops_joined);
    TsMosaic *loaded=ts_mosaic_create();assert(loaded);ts_mosaic_publish_loaded(m,loaded);ts_mosaic_free(loaded);
    assert(!(performance_status(0,&a).selected&PERF_LOOPS) && !m->live_paused);
    ts_note_bank_clear(&a.notes);a.mosaic=saved;ts_mosaic_free(m);a.sister.performance_transport=selected;
    performance_loop_owner(&a);performance_transport.selected=0;
}
