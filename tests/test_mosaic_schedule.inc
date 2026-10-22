static TsMosaic *schedule_fixture(void)
{
    TsMosaic *m=fixture();m->auto_space=0;
    TsMosaicSource *source=m->events[0].source;
    for(int i=0;i<257;++i) {
        TsMosaicEvent *e=i?ts_mosaic_add(m,source,0,0):&m->events[0];assert(e);
        e->start=((i*37)%113)*.001+.0000053;e->duration=.002+(i%7)*.003;
        e->first=100;e->crossfade=32;e->mode=(TsLoopMode)(i%TS_LOOP_MODE_COUNT);
        e->note_count=1+i%5;for(int n=0;n<e->note_count;++n)e->notes[n]=48+(i+n)%25;
        e->gain=.1f+(i%3)*.1f;e->pan=(i%3-1)*.4f;e->fade_in=.001;e->fade_out=.002;
        e->looping=i%4!=0;
        if(i%3==0){e->output_route.matrix_enabled=1;e->output_route.matrix_gain[i%TS_MATRIX_DESTINATIONS]=750;}
    }
    return m;
}
static void schedule_edit(TsMosaic *m,int block)
{
    TsMosaicEvent *e=&m->events[0];
    switch(block) {
    case 1: e->start=m->time;e->duration=.5;break; /* No revision: live drag. */
    case 2: e->gain=.33f;e->pan=.6f;e->fade_out=.1;break;
    case 3: m->events[256].solo=1;break; /* Solo includes future cards. */
    case 4: m->events[256].solo=0;e->muted=1;break;
    case 5: e->muted=0;e->output_route.matrix_enabled=1;e->output_route.matrix_gain[7]=1000;break;
    case 6: ts_mosaic_checkpoint(m);ts_mosaic_delete(m,m->events[17].id);break;
    case 7: assert(ts_mosaic_undo(m,0));break;
    case 8: assert(ts_mosaic_undo(m,1));break;
    case 9: assert(ts_mosaic_copy(m,e->id,m->time+.00001,0));break;
    case 10: ts_mosaic_set_speed(m,2);break;
    case 11: ts_mosaic_seek(m,.012345);break;
    case 12: assert(ts_mosaic_set_loop_range(m,.02,.031));ts_mosaic_set_repeat(m,1);break;
    case 13: ts_mosaic_audition(m,m->events[255].id);break;
    case 14: m->playing=0;break;
    case 15: m->playing=1;ts_mosaic_audition(m,0);break;
    case 16: e->first=150;e->notes[0]=67;++e->revision;break;
    case 17: assert(ts_mosaic_set_loop_range(m,.65,.68));ts_mosaic_seek(m,.66);break;
    case 18: ts_mosaic_set_repeat(m,0);break;
    case 19: ts_mosaic_seek(m,0);assert(ts_mosaic_set_loop_range(m,0,0));m->playing=1;break;
    case 21: assert(ts_mosaic_set_loop_range(m,.02,.021));ts_mosaic_set_repeat(m,1);break;
    }
}
static void test_mosaic_schedule(void)
{
    const unsigned sizes[]={1,17,512,2048};
    for(int size=0;size<4;++size) {
        TsMosaic *buffered=schedule_fixture(),*scalar=schedule_fixture();
        for(int block=0;block<22;++block) {
            int rate=block<16?44100:48000;
            schedule_edit(buffered,block);schedule_edit(scalar,block);
            ts_mosaic_begin_block(buffered,sizes[size]);
            for(unsigned n=0;n<sizes[size];++n) {
                /* Explicit seeks may occur inside a buffer at transport/loop
                   boundaries; scheduling must be rebuilt without allocation. */
                if(block==20 && n==sizes[size]/2){ts_mosaic_seek(buffered,.017);ts_mosaic_seek(scalar,.017);}
                TsSourceRouteMix a={0},b={0};
                TsStereoFrame x=ts_mosaic_read_routed(buffered,rate,&a),y=ts_mosaic_read_routed(scalar,rate,&b);
                assert(!memcmp(&x,&y,sizeof(x)) && !memcmp(&a,&b,sizeof(a)));
                assert(buffered->time==scalar->time && buffered->playing==scalar->playing);
                assert(buffered->audition_id==scalar->audition_id);
                for(int i=0;i<257;++i) {
                    assert(buffered->voices[i].active==scalar->voices[i].active);
                    assert(buffered->voices[i].attack==scalar->voices[i].attack);
                    assert(!memcmp(buffered->voices[i].travel,scalar->voices[i].travel,sizeof(buffered->voices[i].travel)));
                }
            }
        }
        ts_mosaic_free(buffered);ts_mosaic_free(scalar);
    }
}
static void test_mosaic_large_storage(void)
{
    const char *dir="mosaic-large-test";MKDIR(dir);
    TsMosaic *m=fixture(),*loaded=ts_mosaic_create();assert(loaded);m->auto_space=0;
    TsMosaicSource *source=m->events[0].source;
    for(int i=1;i<TS_MOSAIC_EVENTS;++i) {
        TsMosaicEvent *e=ts_mosaic_copy(m,m->events[0].id,i*.5,0);assert(e && e->source==source);
    }
    assert(!ts_mosaic_add(m,source,0,0));
    uint64_t hash=ts_mosaic_hash(m);char error[160];
    assert(ts_mosaic_save(m,dir,error,sizeof(error)) && ts_mosaic_load(loaded,dir,error,sizeof(error)));
    assert(ts_mosaic_hash(loaded)==hash);
    for(int i=0;i<TS_MOSAIC_EVENTS;++i)assert(loaded->events[i].id && loaded->events[i].source==loaded->events[0].source);
    assert(!loaded->sources->next); /* Copies save a single audio source. */
    /* Auto-Space can put a legal dense row beyond the old 100,000-unit
       reader limit. Include maximum-width legacy cards and a saved far view. */
    m->auto_space=1;
    for(int i=0;i<TS_MOSAIC_EVENTS;++i){m->events[i].start=0;m->events[i].x=12000;m->events[i].width=4096;}
    for(int i=0;i<TS_MOSAIC_EVENTS;++i)ts_mosaic_space(m,&m->events[i]);
    double far=0;for(int i=0;i<TS_MOSAIC_EVENTS;++i)far=fmax(far,m->events[i].x);assert(far>1000000);
    m->views[0]=(TsMosaicView){1,0,far,24,400/(far+4096)};
    hash=ts_mosaic_hash(m);
    assert(ts_mosaic_save(m,dir,error,sizeof(error)));storage_portable_load(loaded,dir);
    assert(ts_mosaic_hash(loaded)==hash && loaded->views[0].xscroll==far);
    for(int i=0;i<TS_MOSAIC_EVENTS;++i)assert(loaded->events[i].x==m->events[i].x);
    const double bad_x[]={-1,NAN,INFINITY,TS_MOSAIC_LAYOUT_LIMIT+1};
    for(unsigned i=0;i<sizeof(bad_x)/sizeof(bad_x[0]);++i) {
        double saved=m->events[0].x;m->events[0].x=bad_x[i];
        assert(!ts_mosaic_save(m,dir,error,sizeof(error)));m->events[0].x=saved;
        assert(ts_mosaic_load(loaded,dir,error,sizeof(error)) && ts_mosaic_hash(loaded)==hash);
    }
    /* A guard-edge position and its fit view also survive a round trip. */
    m->events[0].x=TS_MOSAIC_LAYOUT_LIMIT;m->views[0].xscroll=TS_MOSAIC_LAYOUT_LIMIT;
    m->views[0].hscale=400/(TS_MOSAIC_LAYOUT_LIMIT+4096);
    assert(ts_mosaic_save(m,dir,error,sizeof(error)) && ts_mosaic_load(loaded,dir,error,sizeof(error)));
    assert(ts_mosaic_hash(m)==ts_mosaic_hash(loaded));
    for(int i=0;i<40;++i){ts_mosaic_checkpoint(m);m->events[TS_MOSAIC_EVENTS-1].start=1000+i;}
    for(int i=0;i<TS_MOSAIC_HISTORY;++i)assert(ts_mosaic_undo(m,0));
    assert(m->events[TS_MOSAIC_EVENTS-1].start==1007);
    for(int i=0;i<12;++i)assert(ts_mosaic_undo(m,1));
    ts_mosaic_checkpoint(m);m->events[TS_MOSAIC_EVENTS-1].start=2000;
    assert(!ts_mosaic_undo(m,1));assert(ts_mosaic_undo(m,0));
    assert(m->events[TS_MOSAIC_EVENTS-1].start==1019);
    assert(ts_mosaic_undo(m,1) && m->events[TS_MOSAIC_EVENTS-1].start==2000);
    /* A source referenced only by the wrapped history must survive collection. */
    for(int i=0;i<TS_MOSAIC_EVENTS;++i)ts_mosaic_delete(m,m->events[i].id);
    ts_mosaic_collect(m);assert(m->sources==source);
    for(int i=0;i<TS_MOSAIC_HISTORY;++i)ts_mosaic_checkpoint(m);
    ts_mosaic_collect(m);assert(!m->sources);
    ts_mosaic_free(m);ts_mosaic_free(loaded);
    remove("mosaic-large-test/mosaic.tsm");remove("mosaic-large-test/mosaic-000.wav");RMDIR(dir);
}
