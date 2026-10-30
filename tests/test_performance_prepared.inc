/* Prepared groups represent sounds from arbitrary browser pages. Their audio
   ownership and note lifecycle must not depend on the current Canvas bank. */
static void test_prepared_performance(void)
{
    TsPerformanceBank *p=calloc(1,sizeof(*p));assert(p);ts_performance_init(p);
    ts_performance_set_attack_ms(p,0);
    TsSample s={.frames=128,.sample_rate=48000,.channels=2};s.data=malloc(256*sizeof(float));assert(s.data);
    TsPerformancePreparedSource sources[4];memset(sources,0,sizeof(sources));
    for(int n=0;n<4;++n) {
        for(int i=0;i<128;++i){s.data[i*2]=.04f*(n+1);s.data[i*2+1]=.02f*(n+1);}
        sources[n]=(TsPerformancePreparedSource){.generation=ts_performance_prepare_sample(p,&s),
            .tuning={60,0},.first=4,.last=120,.mode=TS_LOOP_FORWARD,.looping=1};
        assert(sources[n].generation);
    }
    ts_sample_free(&s); /* Generations own their copies. */
    TsNoteEvent q;assert(ts_note_event_qwerty(&q,0,60));
    assert(ts_performance_trigger_prepared(p,sources,4,&q,0,48000)==4);
    assert(ts_performance_count(p)==4);
    TsStereoFrame value={0,0};for(int n=0;n<400;++n)value=ts_performance_read_stereo(p,NULL);
    assert(fabsf(value.l-.2f)<1e-6f && fabsf(value.r-.1f)<1e-6f);
    /* QWERTY and MIDI triggers have independent release ownership. */
    TsNoteEvent midi;assert(ts_note_event_midi(&midi,72,127,3));
    assert(ts_performance_trigger_prepared(p,sources,4,&midi,0,48000)==4);
    assert(ts_performance_count(p)==8);
    ts_performance_release_event(p,&q);assert(ts_performance_count(p)==4);
    ts_performance_release_midi_channel(p,2);assert(ts_performance_count(p)==4);
    ts_performance_release_midi_channel(p,3);assert(!ts_performance_count(p));
    /* Sustain and latch release the complete layered trigger. */
    ts_performance_set_sustain(p,1);
    assert(ts_performance_trigger_prepared(p,sources,4,&q,0,48000)==4);
    ts_performance_release_event(p,&q);assert(ts_performance_count(p)==4);
    ts_performance_set_sustain(p,0);assert(!ts_performance_count(p));
    assert(ts_performance_trigger_prepared(p,sources,4,&q,1,48000)==4);
    ts_performance_release_event(p,&q);assert(ts_performance_count(p)==4);
    assert(ts_performance_trigger_prepared(p,sources,4,&q,1,48000)==4);assert(!ts_performance_count(p));
    /* Fill the real fixed pool, then reject a whole additional note. Retrigger
       a smaller source set without losing capacity to its old four layers. */
    for(int note=0;note<TS_PERFORMANCE_VOICE_LIMIT/4;++note) {
        assert(ts_note_event_midi(&midi,note,127,0));
        assert(ts_performance_trigger_prepared(p,sources,4,&midi,0,48000)==4);
    }
    assert(ts_performance_count(p)==TS_PERFORMANCE_VOICE_LIMIT);
    assert(ts_note_event_midi(&midi,100,127,0));
    assert(!ts_performance_trigger_prepared(p,sources,4,&midi,0,48000));
    assert(ts_performance_count(p)==TS_PERFORMANCE_VOICE_LIMIT);
    assert(ts_note_event_midi(&midi,0,127,0));
    assert(ts_performance_trigger_prepared(p,sources,1,&midi,0,48000)==1);
    assert(ts_performance_count(p)==TS_PERFORMANCE_VOICE_LIMIT-3);
    ts_performance_clear(p);
    /* Deselecting a set must not free a held generation. */
    assert(ts_performance_trigger_prepared(p,sources,4,&q,0,48000)==4);
    for(int n=0;n<4;++n)ts_performance_sample_release(sources[n].generation);
    ts_performance_collect_retired(p);assert(p->retired_generations);
    for(int n=0;n<400;++n)value=ts_performance_read_stereo(p,NULL);
    assert(fabsf(value.l-.2f)<1e-6f);
    ts_performance_release_event(p,&q);ts_performance_collect_retired(p);
    assert(!p->retired_generations);ts_performance_free(p);free(p);
}
