static void test_marker_loop(void)
{
    TsMosaic *m=fixture(),*reference=fixture();
    TsMosaicMarker cue={.stored=1,.time=.2};assert(ts_mosaic_set_marker(m,0,&cue));
    cue.time=.8;assert(ts_mosaic_set_marker(m,1,&cue));
    cue.time=.4;assert(ts_mosaic_set_marker(m,5,&cue));assert(ts_mosaic_set_marker(m,6,&cue));
    TsMosaicRange range=ts_mosaic_marker_range(m,0);assert(range.start==.2 && range.end==.4);
    range=ts_mosaic_marker_range(m,5);assert(range.start==.4 && range.end==.8);
    range=ts_mosaic_marker_range(m,1);assert(range.start==.8 && range.end==5);
    assert(!ts_mosaic_marker_range(m,15).end);
    /* An older manual range must not take over when cue looping is released. */
    assert(ts_mosaic_set_loop_range(m,1,2));ts_mosaic_set_repeat(m,1);
    ts_mosaic_set_marker_loop(m,1);assert(m->marker_loop && !m->repeat);
    assert(ts_mosaic_trigger_marker(m,0));
    assert(ts_mosaic_set_loop_range(reference,.2,.4));ts_mosaic_set_repeat(reference,1);
    ts_mosaic_seek(reference,.2);
    int history=m->history_count;
    for(int i=0;i<31200;++i) {
        TsStereoFrame a=ts_mosaic_read(m,48000),b=ts_mosaic_read(reference,48000);
        assert(isfinite(a.l) && isfinite(a.r) && fabs(a.l-b.l)<1e-6 && fabs(a.r-b.r)<1e-6);
    }
    assert(fabs(m->time-.25)<1e-8 && m->marker_active==1 && m->playing);
    double time=m->time,phase=m->voices[0].position[0];unsigned transition=m->transition_remaining;
    uint64_t hash=ts_mosaic_hash(m);
    ts_mosaic_set_marker_loop(m,0);ts_mosaic_set_repeat(reference,0);
    assert(m->time==time && m->voices[0].position[0]==phase && m->transition_remaining==transition);
    assert(!m->marker_loop && !m->repeat && m->playing && m->history_count==history && ts_mosaic_hash(m)==hash);
    for(int i=0;i<19200;++i) {
        TsStereoFrame a=ts_mosaic_read(m,48000),b=ts_mosaic_read(reference,48000);
        assert(fabs(a.l-b.l)<1e-6 && fabs(a.r-b.r)<1e-6);
    }
    assert(fabs(m->time-.65)<1e-8 && m->playing && m->loop_range.start==1);
    ts_mosaic_set_marker_loop(m,1);assert(m->time>.64 && m->marker_active==6);
    assert(ts_mosaic_trigger_marker(m,1) && m->time==.8);
    assert(!ts_mosaic_trigger_marker(m,15) && m->time==.8);
    m->playing=0;assert(ts_mosaic_trigger_marker(m,5) && !m->playing && m->time==.4);
    /* Endpoint edits are observed at the next schedule rebuild; duplicates do
       not form a zero-duration loop. Undo restores the original boundary. */
    cue.time=.6;assert(ts_mosaic_set_marker(m,1,&cue));
    m->playing=1;ts_mosaic_read(m,48000);assert(m->schedule.marker_range.end==.6);
    assert(ts_mosaic_undo(m,0));ts_mosaic_read(m,48000);assert(m->schedule.marker_range.end==.8);
    assert(ts_mosaic_trigger_marker(m,1));
    assert(ts_mosaic_set_marker(m,1,&(TsMosaicMarker){0}));
    time=m->time;ts_mosaic_read(m,48000);assert(m->time>time && !m->schedule.marker_range.end);
    /* Ordinary seeks cancel the active cue; normal Repeat can take ownership. */
    ts_mosaic_seek(m,0);assert(!m->marker_active);
    ts_mosaic_set_repeat(m,1);assert(!m->marker_loop && m->repeat);
    ts_mosaic_free(reference);ts_mosaic_free(m);
}
