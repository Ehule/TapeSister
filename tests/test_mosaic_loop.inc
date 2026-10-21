static void test_mosaic_loop_audio(void)
{
    /* Compare every wrap against an explicit seek, including all sample loop
       modes, chords, tape speeds, source offsets and custom route handoffs. */
    for(int rate=44100;rate<=48000;rate+=3900)for(int mode=0;mode<TS_LOOP_MODE_COUNT;++mode)
    for(int speed=0;speed<3;++speed)for(int routed=0;routed<2;++routed) {
        TsMosaic *m=fixture(),*reference=fixture();
        m->events[0].mode=reference->events[0].mode=(TsLoopMode)mode;
        m->events[0].first=reference->events[0].first=100;
        m->events[0].crossfade=reference->events[0].crossfade=32;
        if(routed) {
            m->events[0].output_route.matrix_enabled=reference->events[0].output_route.matrix_enabled=1;
            m->events[0].output_route.matrix_gain[3]=reference->events[0].output_route.matrix_gain[3]=1000;
        }
        double factor=speed==0?.5:speed==1?1:2;
        ts_mosaic_set_speed(m,factor);ts_mosaic_set_speed(reference,factor);
        assert(ts_mosaic_set_loop_range(m,.031,.044));ts_mosaic_set_repeat(m,1);
        ts_mosaic_seek(m,.031);ts_mosaic_seek(reference,.031);
        int wraps=0;
        for(int i=0;i<4000;++i) {
            if(reference->time>=.044) {
                ts_mosaic_seek(reference,.031+fmod(reference->time-.031,.013));++wraps;
            }
            TsSourceRouteMix actual_mix={0},expected_mix={0};
            TsStereoFrame actual=ts_mosaic_read_routed(m,rate,&actual_mix),expected=ts_mosaic_read_routed(reference,rate,&expected_mix);
            assert(fabs(m->time-reference->time)<1e-10);
            assert(fabs(actual.l-expected.l)<1e-5 && fabs(actual.r-expected.r)<1e-5);
            assert(fabs(actual_mix.graph[3].l-expected_mix.graph[3].l)<1e-5);
            for(int n=0;n<5;++n)assert(fabs(m->voices[0].position[n]-reference->voices[0].position[n])<1e-5);
        }
        assert(wraps>=3);ts_mosaic_free(m);ts_mosaic_free(reference);
    }
    TsMosaic *m=fixture();assert(ts_mosaic_set_loop_range(m,.03,.06));ts_mosaic_set_repeat(m,1);
    m->events[0].looping=0;m->events[0].note_count=1;m->events[0].notes[0]=60;
    ts_mosaic_read(m,48000);assert(fabs(m->voices[0].position[0]-1440)<1e-6);
    ts_mosaic_seek(m,.06);ts_mosaic_read(m,48000);assert(fabs(m->voices[0].position[0]-1440)<1e-6);
    /* A selected tail may include intentional silence beyond the last card. */
    assert(ts_mosaic_set_loop_range(m,4.9,5.2));ts_mosaic_seek(m,5.1);
    ts_mosaic_read(m,48000);assert(m->playing && m->time>5.1);
    ts_mosaic_seek(m,5.2);ts_mosaic_read(m,48000);assert(m->time>=4.9 && m->time<4.901);
    ts_mosaic_set_repeat(m,0);ts_mosaic_seek(m,5.2);ts_mosaic_read(m,48000);assert(!m->playing);
    /* Group transport starts outside the range without using the UI helper. */
    ts_mosaic_read(m,48000);assert(!m->was_playing);
    ts_mosaic_set_repeat(m,1);m->playing=1;ts_mosaic_seek(m,6);
    ts_mosaic_read(m,48000);assert(m->time>=4.9 && m->time<4.901);
    /* Clearing restores the existing whole-arrangement repeat. */
    assert(ts_mosaic_set_loop_range(m,0,0));ts_mosaic_set_repeat(m,1);m->playing=1;
    ts_mosaic_seek(m,5.25);ts_mosaic_read(m,48000);assert(m->time>=.25 && m->time<.251);
    assert(ts_mosaic_set_loop_range(m,1,1.001));ts_mosaic_seek(m,1);
    for(int i=0;i<500;++i) {ts_mosaic_read(m,48000);assert(m->time>=1 && m->time<1.0011);}
    ts_mosaic_free(m);
}
static void test_mosaic_loop_history(void)
{
    TsMosaic *m=fixture();uint64_t original=ts_mosaic_hash(m);
    ts_mosaic_checkpoint(m);assert(ts_mosaic_set_loop_range(m,1,2));uint64_t selected=ts_mosaic_hash(m);
    assert(selected!=original);assert(ts_mosaic_undo(m,0));assert(ts_mosaic_hash(m)==original);
    assert(ts_mosaic_undo(m,1));assert(ts_mosaic_hash(m)==selected);
    assert(!ts_mosaic_set_loop_range(m,NAN,2));assert(!ts_mosaic_set_loop_range(m,3,2));
    assert(!ts_mosaic_set_loop_range(m,1,1));assert(!ts_mosaic_set_loop_range(m,0,INFINITY));
    assert(!ts_mosaic_set_loop_range(m,-1,2));assert(ts_mosaic_hash(m)==selected);
    for(int i=0;i<40;++i){ts_mosaic_checkpoint(m);assert(ts_mosaic_set_loop_range(m,i,i+1));}
    for(int i=0;i<TS_MOSAIC_HISTORY;++i)assert(ts_mosaic_undo(m,0));
    assert(m->loop_range.start==7 && m->loop_range.end==8);
    for(int i=0;i<TS_MOSAIC_HISTORY;++i)assert(ts_mosaic_undo(m,1));
    assert(m->loop_range.start==39 && m->loop_range.end==40);ts_mosaic_free(m);
}
