/* Whole-looper hold is runtime state: no timeline seek, lost voice, sequence
   handover, or route that keeps sounding after its player is paused. */
static void test_mosaic_live_pause(void)
{
    TsMosaic *m=sequence_fixture();TsMosaicEvent *e=&m->events[3];
    assert(ts_mosaic_live_launch(m,e->id,0));assert(ts_mosaic_sequence_cycle(m));live_render(m,16);
    assert(m->sequence.index==1);
    TsMosaicLive live[4];memcpy(live,m->live,sizeof(live));
    double positions[4];for(int i=0;i<4;++i)positions[i]=m->voices[i].position[0];
    TsMosaicSequence sequence=m->sequence;uint64_t hash=ts_mosaic_hash(m);
    ts_mosaic_live_pause(m,1);ts_mosaic_live_pause(m,1);
    live_render(m,10);assert(live_render(m,100)==0);
    assert(!memcmp(live,m->live,sizeof(live)) && !memcmp(&sequence,&m->sequence,sizeof(sequence)));
    for(int i=0;i<4;++i)assert(positions[i]==m->voices[i].position[0]);
    assert(ts_mosaic_hash(m)==hash && m->time==0);
    ts_mosaic_live_pause(m,0);ts_mosaic_live_pause(m,0);assert(live_render(m,3)>0);
    assert(m->live[3].elapsed>live[3].elapsed && m->sequence.index==1);
    /* Home retains only the launched set and frozen sequence membership. */
    TsMosaicEvent *idle=ts_mosaic_copy(m,e->id,10,500);assert(idle);idle->live_order=-1;
    ts_mosaic_live_home(m);assert(!m->live_paused && !m->sequence.running && m->live_reset_sequence && !m->sequence.index);
    assert(m->sequence.pending && m->live[3].elapsed==0 && !m->live[idle-m->events].state);
    live_render(m,20);assert(m->live[3].elapsed==0 && m->sequence.pending && ts_mosaic_live_ready(m));
    ts_mosaic_live_resume(m);live_render(m,2);assert(m->sequence.index==0 && m->live[3].elapsed>0);
    /* Explicit stop must clear held releases, then a future launch can play. */
    ts_mosaic_live_pause(m,1);ts_mosaic_live_stop(m,0);live_render(m,10);
    assert(!ts_mosaic_live_count(m) && !m->sequence.running && !m->live_paused);
    assert(ts_mosaic_live_launch(m,e->id,0));assert(live_render(m,20)>0);
    e->finish_fade=.1;ts_mosaic_live_finish(m,e->id);live_render(m,10);
    TsMosaicLive finishing=m->live[3];ts_mosaic_live_pause(m,1);live_render(m,50);
    assert(!memcmp(&finishing,&m->live[3],sizeof(finishing)));
    ts_mosaic_live_pause(m,0);live_render(m,120);assert(!m->live[3].state);
    /* A quantized launch cannot begin behind the pause overlay. */
    ts_mosaic_live_clock(m,1,0,.5,.5);assert(ts_mosaic_live_launch(m,e->id,1));
    ts_mosaic_live_pause(m,1);live_render(m,300);assert(m->live[3].state==TS_MOSAIC_LIVE_WAITING);
    ts_mosaic_live_pause(m,0);live_render(m,260);assert(m->live[3].state==TS_MOSAIC_LIVE_PLAYING);
    ts_mosaic_free(m);
}
static void test_mosaic_live_pause_routes(void)
{
    TsMosaic *m=live_fixture(),*timeline=live_fixture();
    for(int k=0;k<2;++k) {
        TsMosaic *scene=k?timeline:m;TsMosaicEvent *e=&scene->events[0];
        e->output_route.matrix_enabled=1;e->output_route.matrix_gain[7]=750;
        TsMosaicEvent *ordinary=ts_mosaic_copy(scene,e->id,0,150);assert(ordinary);
        ordinary->live=0;ordinary->output_route.matrix_gain[7]=0;ordinary->output_route.matrix_gain[8]=500;
        scene->playing=1;assert(ts_mosaic_live_launch(scene,e->id,0));
    }
    /* Same timeline in the reference scene, but no independently live player. */
    ts_mosaic_live_stop(timeline,0);
    for(int block=0;block<2;++block)for(int k=0;k<2;++k) {
        TsMosaic *scene=k?timeline:m;ts_mosaic_begin_block(scene,20);
        for(int i=0;i<20;++i){TsSourceRouteMix mix={0};ts_mosaic_read_routed(scene,1000,&mix);}
    }
    assert(fabsf(m->live_pause_routes.graph[7].l)>0);
    double elapsed=m->live[0].elapsed,position=m->voices[0].position[0];ts_mosaic_live_pause(m,1);
    ts_mosaic_begin_block(m,30);ts_mosaic_begin_block(timeline,30);
    for(int i=0;i<30;++i) {
        TsSourceRouteMix mix={0},reference={0};TsStereoFrame out=ts_mosaic_read_routed(m,1000,&mix);
        TsStereoFrame expected=ts_mosaic_read_routed(timeline,1000,&reference);
        assert(fabs(out.l-expected.l)<1e-6 && fabs(mix.graph[8].l-reference.graph[8].l)<1e-6);
        if(i>5)assert(mix.graph[7].l==0 && mix.graph[7].r==0 && fabs(mix.reference.l-reference.reference.l)<1e-6);
    }
    assert(m->time>.06 && m->live[0].elapsed==elapsed && m->voices[0].position[0]==position);
    ts_mosaic_live_pause(m,0);ts_mosaic_begin_block(m,20);double energy=0;
    for(int i=0;i<20;++i){TsSourceRouteMix mix={0};ts_mosaic_read_routed(m,1000,&mix);energy+=fabs(mix.graph[7].l);}
    assert(energy>0 && m->live[0].elapsed>elapsed);
    ts_mosaic_free(m);ts_mosaic_free(timeline);
}

static void test_mosaic_reset_rearm(void)
{
    TsMosaic *m=live_fixture();TsMosaicEvent *e=&m->events[0];e->start=0;
    assert(ts_mosaic_live_launch(m,e->id,0));live_render(m,50);
    ts_mosaic_live_pause(m,1);live_render(m,20);assert(live_render(m,10)==0);
    ts_mosaic_live_home(m);assert(!m->live_paused && !ts_mosaic_live_count(m) && ts_mosaic_live_ready(m));
    assert(live_render(m,10)==0); /* Reset a silent pause cannot replay its stale last frame. */
    uint64_t hash=ts_mosaic_hash(m);ts_mosaic_live_home(m);assert(ts_mosaic_hash(m)==hash);
    ts_mosaic_seek(m,0);m->playing=1;assert(live_render(m,100)>0);
    assert(m->live[0].elapsed>.099 && !ts_mosaic_live_ready(m));
    m->playing=0;ts_mosaic_live_home(m);uint64_t id=e->id;ts_mosaic_delete(m,id);
    e=ts_mosaic_add(m,m->sources,0,0);assert(e);e->live=1;assert(e->id!=id);
    assert(!ts_mosaic_live_ready(m));ts_mosaic_live_resume(m);assert(!ts_mosaic_live_count(m));
    assert(ts_mosaic_live_launch(m,e->id,0));live_render(m,20);ts_mosaic_live_home(m);
    TsMosaic *loaded=ts_mosaic_create();assert(loaded);ts_mosaic_publish_loaded(m,loaded);ts_mosaic_free(loaded);
    assert(!ts_mosaic_live_ready(m) && !m->live_paused && !m->live_reset_remaining);
    ts_mosaic_free(m);
}
