static void test_clock_controller(const char *directory)
{
    static float baseline[512*4],out[512*4];
    ts_master_clock_default(&a.sister.clock_controls);
    TsMasterClockControls *c=&a.sister.clock_controls;
    TsMosaic *m=a.mosaic=ts_mosaic_create();assert(m);
    TsMosaicSource *source=ts_mosaic_source(m,&bank.current,error,sizeof(error));assert(source);
    TsMosaicEvent *card=ts_mosaic_add(m,source,0,0);assert(card);card->duration=60;
    ts_tapehead_stop();m->playing=1;
    for(int i=0;i<4;++i)audio_callback(&a,(Uint8*)baseline,sizeof(baseline));
    c->enabled=1; /* An unused master clock must not affect PCM or routing. */
    for(int i=0;i<4;++i)audio_callback(&a,(Uint8*)out,sizeof(out));
    assert(!memcmp(out,baseline,sizeof(out)));
    m->playing=0;ts_master_clock_default(c);c->enabled=1;c->bpm=160;c->tempo_mask=TS_CLOCK_TRACKER;
    pattern[editor.editPattern][0]=(note_t){.note=49,.instr=1,.efx=0x0f,.efxData=96};
    pattern[editor.editPattern][MAX_CHANNELS]=(note_t){.efx=0x0f,.efxData=160};
    startPlaying(PLAYMODE_SONG,0);
    audio_callback(&a,(Uint8*)out,sizeof(out));
    TsTapeHeadPerformance status;ts_tapehead_performance_locked(&status);
    assert(status.playing && status.bpm==160 && status.score_bpm==96 && ts_tapehead_tempo(0)==160);
    int row=song.row,tick=song.tick,period=channel[0].finalPeriod;
    double fraction=(double)audio.tickSampleCounter/(audio.samplesPerTickInt+(double)audio.samplesPerTickFrac/BPM_FRAC_SCALE);
    ts_tapehead_clock(80);c->bpm=80;
    double after=(double)audio.tickSampleCounter/(audio.samplesPerTickInt+(double)audio.samplesPerTickFrac/BPM_FRAC_SCALE);
    assert(fabs(fraction-after)<1.0/audio.samplesPerTickInt && song.row==row && song.tick==tick);
    audio_callback(&a,(Uint8*)out,sizeof(out));
    assert(channel[0].finalPeriod==period && song.row==row); /* Tempo does not transpose or restart a note. */
    c->enabled=0;audio_callback(&a,(Uint8*)out,sizeof(out));
    ts_tapehead_performance_locked(&status);assert(status.bpm==96 && status.score_bpm==96 && song.row==row);
    ts_tapehead_tick();assert(ts_tapehead_export(&pages.tracker,error,sizeof(error)));
    assert(pages.tracker.bpm!=160 && pages.tracker.bpm!=80);
    /* Tempo source follows the actual native timing table, including tempo
       commands within a rendered block, not an occasional UI BPM poll. */
    assert(ts_note_bank_start(&a.notes,&bank,TS_AUDITION_CURRENT,12,0,48000)==TS_NOTE_STARTED);
    TsKeyboardSequenceSource *arp=calloc(1,sizeof(*arp));assert(arp);
    assert(ts_sample_clone(&arp->sample,&bank.current,error,sizeof(error)));
    arp->count=1;arp->voices[0]=a.notes.voices[0];arp->voices[0].sample=&arp->sample;
    TsKeyboardSequenceSettings settings=a.keyboard_sequence.settings;
    settings.notes[0]=60;settings.count=1;settings.seconds=.04;settings.gate=1;
    ts_keyboard_sequence_set(&a.keyboard_sequence,&settings);ts_keyboard_sequence_source(&a.keyboard_sequence,arp);
    ts_master_clock_default(c);c->enabled=1;c->source=TS_CLOCK_TRACKER_SOURCE;c->tempo_mask=TS_CLOCK_ARP;
    ts_master_clock_transport(&a.sister.master_clock,1);
    startPlaying(PLAYMODE_SONG,0);assert(ts_keyboard_sequence_play(&a.keyboard_sequence));
    int changed=0;
    for(int block=0;block<30 && !changed;++block) {
        double beats=a.sister.master_clock.beats;
        audio_callback(&a,(Uint8*)out,sizeof(out));double expected=0;
        for(unsigned f=0;f<512;++f)expected+=1.0/(48000*ts_tapehead_native_quarter(ts_tapehead_tempo(f)));
        assert(fabs(a.sister.master_clock.beats-beats-expected)<1e-10);
        unsigned last=ts_tapehead_tempo(511);assert(fabs(a.keyboard_sequence.clock_seconds-ts_tapehead_native_quarter(last)/2)<1e-12);
        changed=ts_tapehead_tempo(0)!=last;
    }
    assert(changed);
    ts_tapehead_stop();a.sister.performance_transport=PERF_MOSAIC;
    assert(performance_command(0,window,0,&a,&u,&pages,&bank,NULL,48000));
    double elapsed=a.keyboard_sequence.elapsed,seconds=a.sister.master_clock.seconds;
    audio_callback(&a,(Uint8*)out,sizeof(out));
    assert(a.keyboard_sequence.running && a.keyboard_sequence.elapsed!=elapsed && a.sister.master_clock.seconds==seconds);
    assert(performance_status(0,&a).notes>0); /* Manual held notes also survive. */
    ts_keyboard_sequence_stop(&a.keyboard_sequence);
    settings.seconds=.5;ts_keyboard_sequence_set(&a.keyboard_sequence,&settings);assert(ts_keyboard_sequence_play(&a.keyboard_sequence));
    audio_callback(&a,(Uint8*)out,sizeof(out));
    double phase=a.keyboard_sequence.elapsed/a.keyboard_sequence.clock_seconds;
    c->source=TS_CLOCK_INTERNAL;c->bpm=120;
    audio_callback(&a,(Uint8*)out,sizeof(out));
    assert(fabs(a.keyboard_sequence.elapsed/.25-phase-512.0/(48000*.25))<1e-8);
    phase=a.keyboard_sequence.elapsed/.25;c->enabled=0;
    audio_callback(&a,(Uint8*)out,sizeof(out));
    assert(fabs(a.keyboard_sequence.elapsed/.5-phase-512.0/(48000*.5))<1e-8);
    /* Position following is independent of membership, and is opt-in. */
    ts_keyboard_sequence_stop(&a.keyboard_sequence);ts_note_bank_clear(&a.notes);
    c->position_custom=1;c->home_mask=PERF_TRACKER;m->time=4.5;m->playing=1;
    startPlaying(PLAYMODE_SONG,8);
    assert(performance_command(2,window,0,&a,&u,&pages,&bank,NULL,48000));
    assert(ts_tapehead_running() && song.row==0 && !m->playing && m->time==4.5);
    c->home_mask=PERF_MOSAIC;a.sister.performance_transport=PERF_TRACKER;m->playing=1;
    setSongPos(0,8,RESET_SONG_TICK);
    assert(performance_command(2,window,0,&a,&u,&pages,&bank,NULL,48000));
    assert(!ts_tapehead_running() && song.row==8 && m->playing && !m->time);
    a.sister.performance_transport=0;c->enabled=1;
    assert(performance_command(1,window,0,&a,&u,&pages,&bank,NULL,48000) && a.sister.master_clock.running);
    assert(performance_command(0,window,0,&a,&u,&pages,&bank,NULL,48000) && !a.sister.master_clock.running && m->playing);
    c->home_mask=PERF_ARP;assert(ts_keyboard_sequence_play(&a.keyboard_sequence));
    audio_callback(&a,(Uint8*)out,sizeof(out));assert(a.keyboard_sequence.elapsed>0);
    assert(performance_command(2,window,0,&a,&u,&pages,&bank,NULL,48000));
    assert(a.keyboard_sequence.running && !a.keyboard_sequence.elapsed && !a.keyboard_sequence.cursor);
    ts_keyboard_sequence_stop(&a.keyboard_sequence);
    /* Real clock-page controls change configuration without moving players. */
    performance_show(&u,&a);performance_page_set(&a,PERF_CLOCKS);
    double time=m->time;int playing=m->playing;
    perf_input_click(480,292,SDL_BUTTON_LEFT);assert(m->time==time && m->playing==playing);
    perf_input_click(640,292,SDL_BUTTON_LEFT);assert(m->time==time && m->playing==playing);
    perf_input_click(800,352,SDL_BUTTON_LEFT);assert(!(c->tempo_mask&PERF_TRACKER) && !ts_tapehead_running());
    perf_input_click(560,176,SDL_BUTTON_LEFT);assert(c->source==TS_CLOCK_TRACKER_SOURCE && !(c->tempo_mask&PERF_TRACKER));
    uint64_t hash=paged_project_state_hash(&pages,&bank,NULL,&a.sister,&a.keyboard_sequence);
    c->bpm=121;assert(hash!=paged_project_state_hash(&pages,&bank,NULL,&a.sister,&a.keyboard_sequence));
    c->source=TS_CLOCK_INTERNAL;c->tempo_mask=PERF_ARP|PERF_TRACKER;c->bpm=120;
    a.sister.performance_transport=PERF_MOSAIC|PERF_TRACKER;c->home_mask=PERF_TRACKER;
    /* Cue jumps and loop wraps remain local while master time advances. */
    m->markers[0]=(TsMosaicMarker){.stored=1,.time=2};m->markers[1]=(TsMosaicMarker){.stored=1,.time=5};
    performance_page_set(&a,PERF_CUES);double beats=a.sister.master_clock.beats;row=song.row;
    perf_input_click(32,200,SDL_BUTTON_LEFT);assert(m->time==2 && song.row==row && a.sister.master_clock.beats==beats);
    perf_input_click(40,380,SDL_BUTTON_LEFT);assert(m->marker_loop);
    m->time=4.999;m->playing=1;ts_master_clock_transport(&a.sister.master_clock,1);
    audio_callback(&a,(Uint8*)out,sizeof(out));
    assert(m->time>=2 && m->time<2.02 && song.row==row);
    assert(fabs(a.sister.master_clock.beats-beats-512.0/(48000*.5))<1e-10);
    time=m->time;perf_input_click(40,380,SDL_BUTTON_LEFT);assert(!m->marker_loop && m->time==time);
    performance_page_set(&a,PERF_CLOCKS);
    perf_input_shot(directory,"control-center-clock-routing");
    performance_close();ts_tapehead_clock(0);ts_tapehead_stop();
    /* A restored tracker tempo source must initialize/service the tracker
       even when its workspace is closed and the old ARP link is disabled. */
    c->source=TS_CLOCK_TRACKER_SOURCE;c->tempo_mask=PERF_ARP;a.sister.performance_mixer.arp_sync=0;
    u.tracker_open=0;ts_tapehead_close();
    tracker_refresh(0,&a,&u,&pages,&bank,48000,NULL);
    assert(performance_status(0,&a).tracker.ready && !ts_tapehead_running());
    ts_tapehead_project_replaced();song.BPM=222;
    tracker_refresh(0,&a,&u,&pages,&bank,48000,NULL);
    assert(performance_status(0,&a).tracker.ready && song.BPM!=222);
    /* Home also binds an unopened/stopped destination before rewinding it. */
    ts_tapehead_close();c->enabled=0;c->home_mask=PERF_TRACKER;
    a.sister.performance_transport=PERF_MOSAIC;pages.tracker.editor_row=8;
    m->time=4.5;m->playing=1;
    assert(performance_command(2,window,0,&a,&u,&pages,&bank,NULL,48000));
    assert(performance_status(0,&a).tracker.ready && !ts_tapehead_running() && song.row==0);
    assert(!m->playing && m->time==4.5);
    ts_keyboard_sequence_source_free(ts_keyboard_sequence_source(&a.keyboard_sequence,NULL));
    a.mosaic=NULL;ts_mosaic_free(m);ts_master_clock_default(c);
    puts("Master-clock routing, native score preservation, tempo continuity and independent position tests passed.");
}
