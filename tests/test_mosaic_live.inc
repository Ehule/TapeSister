static TsMosaic *live_fixture(void)
{
    TsMosaic *m=ts_mosaic_create();assert(m);char error[160];
    float data[1000];for(int i=0;i<1000;++i)data[i]=.2f*sinf(i*.07f)+.05f;
    TsSample sample={.data=data,.frames=1000,.sample_rate=1000,.channels=1};
    TsMosaicSource *source=ts_mosaic_source(m,&sample,error,sizeof(error));assert(source);
    TsMosaicEvent *e=ts_mosaic_add(m,source,.1,0);assert(e);e->live=1;e->live_beats=2;
    return m;
}
static double live_render(TsMosaic *m,int frames)
{
    ts_mosaic_begin_block(m,(unsigned)frames);double sum=0;
    for(int i=0;i<frames;++i){TsStereoFrame f=ts_mosaic_read(m,1000);sum+=fabs(f.l)+fabs(f.r);assert(isfinite(f.l));}
    return sum;
}
static void test_mosaic_live(void)
{
    TsMosaic *m=live_fixture();TsMosaicEvent *e=&m->events[0];
    assert(ts_mosaic_live_launch(m,e->id,0));assert(live_render(m,250)>1);
    assert(!m->playing && m->time==0 && fabs(m->live[0].elapsed-.25)<1e-9);
    uint64_t generation=m->voices[0].generation;
    assert(ts_mosaic_live_launch(m,e->id,0));live_render(m,100);
    assert(m->voices[0].generation==generation && fabs(m->live[0].elapsed-.35)<1e-9);
    /* Seek, main speed, main pause, cue loops and repeat do not move an
       independent loop or create another voice. */
    ts_mosaic_seek(m,10);ts_mosaic_set_speed(m,2);m->playing=1;
    assert(live_render(m,100)>1);assert(fabs(m->live[0].elapsed-.45)<1e-9);
    assert(m->voices[0].generation==generation);
    ts_mosaic_seek(m,0);m->playing=1;ts_mosaic_set_repeat(m,1);e->duration=.2;
    live_render(m,700);assert(m->voices[0].generation==generation && fabs(m->live[0].elapsed-1.15)<1e-9);
    assert(ts_mosaic_set_marker(m,0,&(TsMosaicMarker){.stored=1,.time=0}));
    assert(ts_mosaic_set_marker(m,1,&(TsMosaicMarker){.stored=1,.time=.15}));
    ts_mosaic_trigger_marker(m,0);ts_mosaic_set_marker_loop(m,1);live_render(m,400);
    assert(m->voices[0].generation==generation && fabs(m->live[0].elapsed-1.55)<1e-9);
    m->playing=0;double position=m->time;assert(live_render(m,300)>1 && m->time==position);
    ts_mosaic_live_stop(m,0);live_render(m,10);assert(!ts_mosaic_live_count(m));
    /* Jumping over a launch does not synthesize an encounter. Landing exactly
       at its start does; a later pass can rearm a completed/stopped card. */
    ts_mosaic_seek(m,.2);m->playing=1;live_render(m,30);assert(!m->live[0].state);
    ts_mosaic_seek(m,e->start);m->playing=1;live_render(m,30);assert(m->live[0].state);
    ts_mosaic_live_stop(m,e->id);live_render(m,10);ts_mosaic_seek(m,0);m->playing=1;live_render(m,150);assert(m->live[0].state);
    /* Finite length is local tape time, independent of card position. */
    m->playing=0;e->duration=.125;e->live_finite=1;ts_mosaic_live_restart(m,e->id);
    live_render(m,125);assert(fabs(m->live[0].elapsed-.125)<1e-9);live_render(m,1);assert(!m->live[0].state);
    e->live_finite=0;e->fade_out=.1;double fade_in,fade_out;ts_mosaic_event_fades(e,&fade_in,&fade_out);assert(fade_out==0 && e->fade_out==.1);
    ts_mosaic_live_restart(m,e->id);assert(live_render(m,1100)>1 && m->live[0].state);
    double previous=m->live[0].elapsed;ts_mosaic_begin_block(m,1);ts_mosaic_read(m,2000);
    assert(fabs(m->voices[0].step[0]-.5)<1e-12 && fabs(m->live[0].elapsed-previous-.0005)<1e-9);
    /* Muting and solo use the common smoothing and keep phase advancing. */
    e->muted=1;live_render(m,20);assert(live_render(m,50)==0);e->muted=0;assert(live_render(m,50)>0);
    TsMosaicEvent *other=ts_mosaic_copy(m,e->id,3,100);assert(other);other->solo=1;
    live_render(m,20);assert(live_render(m,50)==0);other->solo=0;
    /* Clock enable/disable and tempo changes keep current phase; explicit
       next-beat launch waits, and group/Home/seek policies are separate. */
    ts_mosaic_live_clock(m,1,0,.5,.5);ts_mosaic_live_stop(m,e->id);live_render(m,10);
    assert(ts_mosaic_live_launch(m,e->id,1));live_render(m,200);assert(m->live[0].state==TS_MOSAIC_LIVE_WAITING);
    live_render(m,60);assert(m->live[0].state==TS_MOSAIC_LIVE_PLAYING);
    double elapsed=m->live[0].elapsed;e->live_tempo=1;
    ts_mosaic_live_clock(m,1,0,.25,m->live_phase);live_render(m,100);assert(fabs(m->live[0].elapsed-elapsed-.2)<1e-9);
    elapsed=m->live[0].elapsed;ts_mosaic_live_clock(m,0,0,.25,0);live_render(m,100);assert(fabs(m->live[0].elapsed-elapsed-.1)<1e-9);
    ts_mosaic_live_transport(m,0);assert(m->live[0].state==TS_MOSAIC_LIVE_PLAYING);
    e->live_transport=1;ts_mosaic_live_transport(m,0);live_render(m,10);assert(!m->live[0].state);
    ts_mosaic_live_transport(m,1);live_render(m,10);assert(m->live[0].state);
    e->live_position=1;ts_mosaic_seek(m,50);assert(m->live[0].elapsed==0);
    elapsed=m->live[0].elapsed;ts_mosaic_checkpoint(m);e->gain=.5f;assert(ts_mosaic_undo(m,0));
    assert(m->live[0].state && m->live[0].elapsed==elapsed);
    /* A deleted/reused slot cannot inherit a running player. Undo/load stop
       deleted players rather than resurrecting sound unexpectedly. */
    uint64_t id=e->id;ts_mosaic_delete(m,id);other=ts_mosaic_add(m,m->sources,0,0);assert(other && other->id!=id);
    other->live=1;live_render(m,20);assert(m->live[0].id!=other->id || !m->live[0].state);
    ts_mosaic_free(m);
}
static void test_mosaic_live_storage(void)
{
    TsMosaic *m=live_fixture(),*loaded=ts_mosaic_create();assert(loaded);
    TsMosaicEvent *e=&m->events[0];e->live_finite=e->live_tempo=e->live_transport=e->live_position=1;
    e->live_beats=10000; /* Long source cycles at high BPM must remain saveable. */
    e->fade_in=.1;e->fade_out=.2;e->pan=-.4f;e->muted=e->solo=1;
    m->live_capture.continuous=1;m->live_capture.units=1;m->live_capture.length=8;
    m->live_capture.quantize=1;ts_mosaic_live_launch(m,e->id,0);live_render(m,5);
    MKDIR("mosaic-live-storage");char error[160];uint64_t hash=ts_mosaic_hash(m);ts_mosaic_live_pause(m,1);assert(ts_mosaic_hash(m)==hash);
    assert(ts_mosaic_save(m,"mosaic-live-storage",error,sizeof(error)));
    storage_portable_load(loaded,"mosaic-live-storage");assert(ts_mosaic_hash(loaded)==hash);
    assert(!loaded->playing && !ts_mosaic_live_count(loaded) && !loaded->live_paused && loaded->live_pause_gain==1);
    assert(loaded->events[0].live_transport && loaded->events[0].live_position);
    /* Invalid live data must not replace a valid in-memory scene. */
    FILE *f=fopen("mosaic-live-storage/mosaic.tsm","wb");assert(f);fputs("TAPESISTER_MOSAIC 15\nSETTINGS 0 .7 1\nSPACING 0\nLOOPER 0 1 0 -1 0\n",f);fclose(f);
    assert(!ts_mosaic_load(loaded,"mosaic-live-storage",error,sizeof(error)) && ts_mosaic_hash(loaded)==hash);
    ts_mosaic_free(m);ts_mosaic_free(loaded);
    remove("mosaic-live-storage/mosaic.tsm");remove("mosaic-live-storage/mosaic-000.wav");RMDIR("mosaic-live-storage");
}
static void test_mosaic_live_mix(void)
{
    TsMosaic *mixed=live_fixture(),*timeline=live_fixture(),*loop=live_fixture();
    TsMosaic *scenes[]={mixed,timeline,loop};
    for(int k=0;k<3;++k) {
        TsMosaicEvent *e=ts_mosaic_copy(scenes[k],scenes[k]->events[0].id,0,100);assert(e);
        e->live=0;e->duration=2;e->notes[0]=67;e->pan=.5f;
        scenes[k]->playing=1;ts_mosaic_begin_block(scenes[k],700);
    }
    timeline->events[0].id=0;loop->events[1].id=0;
    for(int i=0;i<700;++i) {
        TsStereoFrame a=ts_mosaic_read(mixed,1000),b=ts_mosaic_read(timeline,1000),c=ts_mosaic_read(loop,1000);
        assert(fabs(a.l-b.l-c.l)<1e-6 && fabs(a.r-b.r-c.r)<1e-6);
    }
    mixed->playing=0;mixed->events[0].output_route.matrix_enabled=1;
    mixed->events[0].output_route.matrix_gain[7]=750;ts_mosaic_begin_block(mixed,100);
    double graph=0,main=0;
    for(int i=0;i<100;++i) {
        TsSourceRouteMix routes={0};TsStereoFrame f=ts_mosaic_read_routed(mixed,1000,&routes);
        if(i>20){main+=fabs(f.l)+fabs(f.r);graph+=fabs(routes.graph[7].l)+fabs(routes.graph[7].r);}
    }
    assert(main<1e-6 && graph>1); /* Loop retains its card route with main paused. */
    ts_mosaic_free(mixed);ts_mosaic_free(timeline);ts_mosaic_free(loop);
}
