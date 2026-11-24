/* Controller/renderer acceptance tests. Virtual wall time makes the complete
   adaptive plan fast, while the real Gallery renderer must advance every
   sample and produce/retire both sides of each crossfade. These are not host
   deadline measurements; --run exercises the canonical device/callback loop. */
static void stress_render(double seconds)
{
    const int rate=1000;unsigned frames=(unsigned)ceil(fmax(0,seconds)*rate);
    while(frames) {
        unsigned block=frames>32?32:frames;frames-=block;
        ts_profile_lane_begin(1);uint64_t timer=ts_profile_begin(TS_PROF_AUDIO);
        ts_gallery_begin_block(pages.gallery,block);
        if(!a.engine_stopped)for(unsigned i=0;i<block;++i) {
            TsSourceRouteMix routes={0};TsStereoFrame x=ts_gallery_read(pages.gallery,rate,&routes);
            assert(isfinite(x.l) && isfinite(x.r));
        }
        ts_profile_end(TS_PROF_AUDIO,timer);ts_profile_lane_end(1);
        ts_realtime_diagnostics_record(&a.realtime_diagnostics,1,1000000,rate,block,0);
        gallery_sequence_follow(0,&a,&u,&bank,&mosaic);
    }
}
static void stress_run(SDL_Window *window)
{
    poll_at(window,SDL_GetPerformanceCounter());assert(live_test.state==LT_ARMED);
    mosaic_transport(0,&a,&u,0);assert(live_test.state==LT_PENDING && !ts_profile_enabled());
    double previous=0;int checked_wait=0;
    while(live_test.next<live_test.count && live_test.state!=LT_DONE && live_test.state!=LT_ABORTED) {
        int step=live_test.next;LiveTestStep planned=live_test.steps[step];
        double due=planned.ms+live_stress.schedule_offset;
        if(planned.action==LT_STRESS_WAIT && !checked_wait) {
            assert(!live_test_stress_ready(&planned,0,&a,&pages));checked_wait=1;
        }
        stress_render((due-previous)/1000);previous=due;
        Uint64 now=live_test.started+(Uint64)ceil(due*live_test.frequency/1000.0);
        poll_at(window,now);
        if(live_test.next!=step+1 || live_test.state==LT_ABORTED)
            fprintf(stderr,"Stress step=%d action=%s state=%d next=%d job=%d trace=%u/%u complete=%u status=%s\n",
                step,live_test_action_name(planned.action),live_test.state,live_test.next,live_stress.current_job,
                live_stress.observed_transitions,live_stress.planned_transitions,live_stress.completed_transitions,u.status);
        assert(live_test.next==step+1 && live_test.state!=LT_ABORTED);
    }
    assert(live_test.state==LT_DONE && !a.gallery->diagnostic_enabled && !ts_profile_enabled());
}
static void test_gallery_stress_diagnostic(SDL_Window *window)
{
    live_log_drain();
    TsMosaicGallery *g=pages.gallery;ts_gallery_sequence_stop(g);
    static float wave[64];for(int i=0;i<64;++i)wave[i]=.05f*sinf(i*.2f);
    source.sample=(TsSample){.frames=64,.channels=1,.sample_rate=1000,.data=wave};
    for(int p=0;p<g->count;++p) {
        TsMosaic *m=g->canvas[p].scene;
        memset(m->events,0,sizeof(m->events));memset(m->voices,0,sizeof(m->voices));
        m->playing=0;ts_mosaic_set_speed(m,1);ts_mosaic_seek(m,0);m->schedule.valid=0;
    }
    TsMosaic *first=g->canvas[0].scene,*second=g->canvas[1].scene;
    first->events[0]=(TsMosaicEvent){.id=100,.source=&source,.duration=8,.width=50,.gain=1,.looping=1,.mode=TS_LOOP_FORWARD};
    first->events[1]=(TsMosaicEvent){.id=101,.source=&source,.start=3,.duration=5,.width=50,.gain=.3f,.note_count=3,.notes={48,60,72},.looping=1,.mode=TS_LOOP_FORWARD};
    second->events[0]=(TsMosaicEvent){.id=200,.source=&source,.duration=4,.width=50,.gain=1,.looping=1,.mode=TS_LOOP_FORWARD};
    ts_mosaic_set_speed(second,.5);
    /* Stable identities must not be assumed equal to array positions. */
    g->canvas[0].id=901;g->canvas[1].id=730;
    g->fade_seconds=0;ts_gallery_sequence_default(&g->sequence,g->canvas[0].id);
    g->sequence.step[0].fade_seconds=0;
    u.config.diagnostic_live_test=4;live_log_drain();live_test_loaded(0,&a,&u,&pages,u.project_path);
    assert(live_test.state==LT_PREPARING && live_test_save_blocked());
    assert(!strcmp(live_test_scenario(),"gallery-stress-v1") && live_stress.playable==2);
    assert(live_stress.midfade_available && live_stress.jobs_count==9);
    assert(live_stress.jobs[0].sequence.length==3 && live_stress.jobs[0].profile==0);
    assert(live_stress.density[0]==4 && live_stress.speed[1]==.5);
    assert(live_stress.planned_transitions==5); /* Full2, long, interrupted, short. */
    for(int i=1;i<live_test.count;++i)assert(live_test.steps[i].ms>live_test.steps[i-1].ms);
    stress_run(window);
    assert(live_stress.full_done[0] && live_stress.full_done[1]);
    assert(live_stress.observed_transitions==5 && live_stress.completed_transitions==4 && live_stress.interrupted_transitions==1);
    assert(live_stress.pause_verified && live_stress.busy_verified && live_stress.stop_verified && live_stress.recovery_verified);
    assert(first->events[1].gain==.3f && second->speed==.5);

    /* One tiny nonempty scene plus empty peers must exercise the real spare
       renderer, and fades shorter than one UI tick must remain evidenced. */
    memset(first->events,0,sizeof(first->events));memset(second->events,0,sizeof(second->events));
    first->events[0]=(TsMosaicEvent){.id=100,.source=&source,.duration=.012,.width=50,.gain=.2f};
    first->events[0].muted=1; /* Silent transport still has measurable coverage. */
    ts_gallery_sequence_default(&g->sequence,g->canvas[0].id);g->sequence.step[0].fade_seconds=0;
    live_log_drain();live_test_loaded(0,&a,&u,&pages,u.project_path);
    assert(live_stress.playable==1 && !live_stress.midfade_available);
    assert(live_stress.jobs[0].sequence.length==2 && live_stress.jobs[0].seconds>.012);
    unsigned source_pins=source.pins;
    stress_run(window);
    assert(live_stress.observed_transitions==3 && live_stress.completed_transitions==3);
    assert(!g->spare_active && g->outgoing<0 && first->events[0].muted && source.pins==source_pins);

    /* Empty content has an explicit incomplete/no-workload result; it cannot
       certify audio capacity just because transport actions executed. */
    memset(first->events,0,sizeof(first->events));
    live_log_drain();live_test_loaded(0,&a,&u,&pages,u.project_path);poll_at(window,SDL_GetPerformanceCounter());
    mosaic_transport(0,&a,&u,0);
    for(int i=0;i<live_test.count && live_test.state!=LT_ABORTED;++i)
        poll_at(window,live_test.started+(Uint64)ceil((live_test.steps[i].ms+live_stress.schedule_offset)*live_test.frequency/1000.0));
    assert(live_test.state==LT_ABORTED && strstr(u.status,"NO PLAYBACK WORKLOAD"));

    /* Invalid native speed, unbounded sections and schedule overflow fail
       before mutating or saving project state. */
    first->events[0]=(TsMosaicEvent){.id=100,.source=&source,.duration=1,.gain=1};
    first->speed=NAN;live_log_drain();live_test_loaded(0,&a,&u,&pages,u.project_path);assert(live_test.state==LT_OFF && !live_test_save_blocked());
    first->speed=1;first->events[0].duration=86401;
    live_log_drain();live_test_loaded(0,&a,&u,&pages,u.project_path);assert(live_test.state==LT_OFF && !live_test_save_blocked());
    first->events[0].duration=1;live_log_drain();live_test_loaded(0,&a,&u,&pages,u.project_path);
    int count=live_test.count;live_stress_add((uint64_t)UINT32_MAX+1,LT_END,0,0);
    assert(live_test.plan_failed && live_test.count==count);
    live_test.plan_failed=0;live_test.count=4096;live_test_add(1,LT_CHECK,0,0);assert(live_test.plan_failed);
    live_test.count=count;
    /* A trace lost to bounded capacity is an explicit incomplete run. */
    live_log_drain();live_test_loaded(0,&a,&u,&pages,u.project_path);poll_at(window,SDL_GetPerformanceCounter());mosaic_transport(0,&a,&u,0);
    poll_at(window,live_test.started);
    ts_gallery_sequence_default(&g->sequence,g->canvas[0].id);
    g->sequence.step[0].out_marker=TS_GALLERY_POINT_TIME;g->sequence.step[0].out_seconds=.002;
    g->sequence.step[0].fade_seconds=.001;g->sequence.step[0].repeats=TS_GALLERY_DIAGNOSTIC_TRANSITIONS+2;
    assert(gallery_sequence_start(0,&a,&u,&bank,&mosaic,0));stress_render(.2);
    assert(g->diagnostic_count==TS_GALLERY_DIAGNOSTIC_TRANSITIONS && g->diagnostic_overflow>0);
    poll_at(window,live_test.started+live_test.frequency);
    assert(live_test.state==LT_ABORTED && !g->diagnostic_enabled);
    g->diagnostic_overflow=0;
}
