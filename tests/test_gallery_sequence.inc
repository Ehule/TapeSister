static TsMosaicGallery *sequence_fixture(void)
{
    char e[160];TsMosaicGallery *g=ts_gallery_create(scene(.25f,0,1));assert(g);
    assert(ts_gallery_append_prepared(g,scene(.5f,0,1),e,sizeof(e)));
    g->canvas[0].scene->markers[2]=(TsMosaicMarker){.stored=1,.time=2};
    g->canvas[0].scene->markers[3]=(TsMosaicMarker){.stored=1,.time=3};
    g->canvas[1].scene->markers[4]=(TsMosaicMarker){.stored=1,.time=4};
    g->canvas[1].scene->markers[5]=(TsMosaicMarker){.stored=1,.time=5};
    for(int i=0;i<2;++i)g->canvas[i].scene->events[0].output_route.mix_enabled=0;
    g->sequence.length=2;g->sequence.last=1;g->sequence.loop=1;
    g->sequence.step[0]=(TsGalleryStep){.canvas_id=g->canvas[0].id,.in_marker=2,.out_marker=3,.fade_seconds=.1,.repeats=1};
    g->sequence.step[1]=(TsGalleryStep){.canvas_id=g->canvas[1].id,.in_marker=4,.out_marker=5,.fade_seconds=.2,.repeats=1,.curve=TS_GALLERY_FADE_SMOOTH};
    assert(ts_gallery_sequence_prepare(g,e,sizeof(e)));assert(ts_gallery_sequence_play(g,0,e,sizeof(e)));
    return g;
}
static void test_section_clock(void)
{
    TsMosaicGallery *g=sequence_fixture();TsMosaic *a=g->canvas[0].scene,*b=g->canvas[1].scene;
    a->repeat=1;a->loop_range=(TsMosaicRange){0,.2};a->marker_loop=1; /* Section owns transport. */
    render(g,900);assert(g->active==0 && fabs(a->time-2.9)<1e-9);
    render(g,1);assert(g->active==1 && g->outgoing==0 && fabs(b->time-4.001)<1e-9);
    assert(g->player.current.canvas_id==g->canvas[1].id);
    ts_gallery_sequence_pause(g,1);double x=a->time,y=b->time,fade=g->fade_position;
    render(g,127);assert(a->time==x && b->time==y && g->fade_position==fade && g->outgoing==0);
    ts_gallery_sequence_pause(g,0);render(g,99);
    assert(g->outgoing<0 && fabs(a->time-3)<1e-9 && !a->playing && a->repeat && a->marker_loop);
    render(g,700);assert(g->active==1);render(g,1);assert(g->active==0 && g->outgoing==1);
    assert(fabs(a->time-2.001)<1e-9);render(g,199);assert(g->outgoing<0 && fabs(b->time-5)<1e-9);
    ts_gallery_sequence_stop(g);assert(!g->player.running && !a->section_transport && !a->playing);
    free_gallery(g);
}
static void test_section_same_canvas(void)
{
    char error[160];TsMosaicGallery *g=sequence_fixture();ts_gallery_sequence_stop(g);
    g->sequence.step[0].repeats=2;assert(ts_gallery_sequence_play(g,0,error,sizeof(error)));
    TsMosaic *a=ts_gallery_active(g);TsMosaicSource *source=a->events[0].source;
    unsigned pins=source->pins;render(g,901);
    assert(g->active==0 && g->outgoing==0 && g->spare_active && g->player.visit==2);
    assert(ts_gallery_outgoing(g)!=a && fabs(a->time-2.001)<1e-9 && source->pins>pins);
    /* Reordering during a same-canvas fade retains both playback identities. */
    assert(ts_gallery_move(g,0,1,error,sizeof(error)));assert(g->active==1 && g->outgoing==1);
    render(g,99);assert(!g->spare_active && g->outgoing<0 && source->pins==pins);
    render(g,801);assert(g->active==0 && g->player.step==1 && g->player.visit==1);
    assert(g->sequence.step[0].canvas_id==g->canvas[1].id);
    ts_gallery_sequence_stop(g);free_gallery(g);
}
static void test_section_cuts_and_loop_range(void)
{
    char error[160];TsMosaicGallery *g=sequence_fixture();ts_gallery_sequence_stop(g);
    g->sequence.length=3;g->sequence.first=1;g->sequence.last=2;
    for(int i=0;i<3;++i)g->sequence.step[i]=(TsGalleryStep){.canvas_id=g->canvas[i%2].id,.in_marker=-1,.out_marker=-1,
        .in_seconds=i,.out_seconds=i+.01,.repeats=1};
    assert(ts_gallery_sequence_play(g,0,error,sizeof(error)));
    render(g,11);assert(g->player.step==1 && fabs(ts_gallery_active(g)->time-1.001)<1e-9);
    render(g,10);assert(g->player.step==2);render(g,10);assert(g->player.step==1);
    for(int i=0;i<500;++i){render(g,10);assert(g->player.step==2);render(g,10);assert(g->player.step==1);}
    g->sequence.loop=0;render(g,20);assert(!g->player.running && fabs(ts_gallery_active(g)->time-2.01)<1e-9);
    /* Empty/missing endpoints reject start without moving playback. */
    g->sequence.step[0].in_marker=15;double before=ts_gallery_active(g)->time;
    assert(!ts_gallery_sequence_play(g,0,error,sizeof(error)) && ts_gallery_active(g)->time==before);
    g->sequence.step[0].in_marker=-1;g->sequence.step[0].canvas_id=999;
    assert(!ts_gallery_sequence_play(g,0,error,sizeof(error)));
    free_gallery(g);
}
static void test_section_edits_storage(void)
{
    char error[160];TsMosaicGallery *g=sequence_fixture();render(g,100);
    g->sequence.step[0].out_marker=-1;g->sequence.step[0].out_seconds=8;
    assert(g->player.end==3); /* Current visit is frozen. */
    g->sequence.step[1].fade_seconds=3600;
    render(g,800);render(g,1);assert(g->player.step==1 && g->fade_length<=.1+1e-8);
    render(g,99);render(g,401);assert(g->player.step==0 && g->player.end==8 && g->fade_length<=.5+1e-8);
    ts_gallery_sequence_stop(g);
    g->sequence.step[0].curve=TS_GALLERY_FADE_POWER;
    assert(MKDIR("sequence-roundtrip")==0);assert(ts_gallery_save(g,"sequence-roundtrip",error,sizeof(error)));
    TsMosaicGallery *loaded=ts_gallery_create(ts_mosaic_create());assert(loaded);
    assert(ts_gallery_load(loaded,"sequence-roundtrip",error,sizeof(error)));
    assert(ts_gallery_hash(g)==ts_gallery_hash(loaded) && !loaded->player.running && !loaded->spare);
    assert(loaded->sequence.step[0].out_seconds==8 && loaded->sequence.step[0].curve==TS_GALLERY_FADE_POWER);
    free_gallery(loaded);
    /* Marker deletion is a visible stop at the boundary, never a jump to zero. */
    g->sequence.step[0].out_marker=3;g->sequence.step[0].fade_seconds=0;
    assert(ts_gallery_sequence_play(g,0,error,sizeof(error)));
    g->canvas[1].scene->markers[4].stored=0;render(g,1001);
    assert(!g->player.running && strstr(g->player.error,"MARKER") && g->active==0);
    free_gallery(g);
}
static void test_section_audio_routes(void)
{
    for(int curve=0;curve<TS_GALLERY_FADE_CURVES;++curve) {
        TsMosaicGallery *g=sequence_fixture();g->player.current.curve=curve;
        render(g,900);double energy=0;
        for(int i=0;i<100;++i) {
            TsSourceRouteMix mix={0};TsStereoFrame f=ts_gallery_read(g,1000,&mix);
            /* Fixture routes its whole signal to graph 7; output and every tap
               must retain the selected curve, including the pre-FX reference. */
            assert(isfinite(f.l) && isfinite(mix.graph[7].l));
            assert(fabsf(mix.reference.l-mix.graph[7].l)<1e-5f);
            energy+=mix.graph[7].l*mix.graph[7].l;
        }
        assert(energy>0 && !g->spare_active);free_gallery(g);
    }
}
static void test_section_live_arrangement(void)
{
    char error[160];TsMosaicGallery *g=sequence_fixture();
    TsGalleryStep a=g->sequence.step[0],b=g->sequence.step[1];
    render(g,100);TsMosaic *playing=ts_gallery_active(g);double time=playing->time;
    /* Appending and inserting retain playback, queue identity, and loop bounds. */
    g->player.queued=1;
    assert(ts_gallery_sequence_insert(g,2,&a));
    assert(ts_gallery_sequence_insert(g,0,&b));
    assert(g->player.step==1 && g->player.queued==2 && g->sequence.first==1 && g->sequence.last==3);
    assert(playing->time==time && g->player.start==2 && g->player.end==3);
    assert(ts_gallery_sequence_move(g,1,2));
    assert(g->player.step==2 && g->player.queued==1 && g->sequence.first==2 && g->sequence.last==3);
    assert(ts_gallery_sequence_remove(g,0));
    assert(g->player.step==1 && g->player.queued==0 && g->sequence.first==1 && g->sequence.last==2);
    assert(ts_gallery_sequence_next(g,-1,error,sizeof(error)));
    assert(g->player.step==0 && g->active==1 && g->outgoing==0);
    /* Delete the incoming step during a fade; neither renderer is restarted. */
    TsMosaic *outgoing=ts_gallery_outgoing(g);double old=outgoing->time,new_time=ts_gallery_active(g)->time;
    double fade_start=g->player.fade_start,fade_end=g->player.fade_end;
    assert(ts_gallery_sequence_remove(g,0));
    assert(g->player.running && g->player.step==-1 && g->player.continuation==0);
    assert(outgoing==ts_gallery_outgoing(g) && outgoing->time==old && ts_gallery_active(g)->time==new_time);
    assert(g->player.fade_start==fade_start && g->player.fade_end==fade_end);
    render(g,101);assert(g->outgoing<0 && g->player.step==-1);
    render(g,700);assert(g->player.step==0 && g->active==0);
    ts_gallery_sequence_pause(g,1);time=ts_gallery_active(g)->time;
    assert(ts_gallery_sequence_insert(g,0,&b));
    assert(ts_gallery_sequence_move(g,1,2));
    assert(g->player.paused && g->player.step==2);
    render(g,50);assert(ts_gallery_active(g)->time==time && g->player.paused);
    ts_gallery_sequence_pause(g,0);render(g,10);assert(ts_gallery_active(g)->time>time);
    free_gallery(g);

    /* A removed step finishes one pass, even if it had more repeats. */
    g=sequence_fixture();g->player.current.repeats=4;
    assert(ts_gallery_sequence_remove(g,0));assert(g->player.step==-1);
    render(g,901);assert(g->player.step==0 && g->player.visit==1 && g->active==1);
    free_gallery(g);

    /* Deleting TO wraps to FROM; deleting a queued step cancels only the queue. */
    g=sequence_fixture();assert(ts_gallery_sequence_play(g,1,error,sizeof(error)));
    assert(ts_gallery_sequence_remove(g,1));
    assert(g->player.continuation==0 && ts_gallery_sequence_valid(&g->sequence));
    render(g,801);assert(g->player.step==0 && g->active==0);free_gallery(g);
    g=sequence_fixture();g->player.queued=1;
    assert(ts_gallery_sequence_remove(g,1));assert(g->player.queued==-1 && g->player.step==0);
    assert(!ts_gallery_sequence_remove(g,0));free_gallery(g);

    /* Adding after a removed final visit extends playback, even without LOOP. */
    g=sequence_fixture();g->sequence.loop=0;assert(ts_gallery_sequence_play(g,1,error,sizeof(error)));
    assert(ts_gallery_sequence_remove(g,1));assert(g->player.next==-1);
    assert(ts_gallery_sequence_insert(g,1,&a));assert(g->player.next==1);
    render(g,801);assert(g->player.running && g->player.step==1 && g->active==0);free_gallery(g);
}
static void test_section_edit_during_fade_audio(void)
{
    for(int same=0;same<2;++same) {
        TsMosaicGallery *g=sequence_fixture(),*reference=sequence_fixture();
        if(same){g->player.current.repeats=2;reference->player.current.repeats=2;}
        render(g,925);render(reference,925);assert(g->outgoing>=0);
        TsMosaic *incoming=ts_gallery_active(g),*outgoing=ts_gallery_outgoing(g);
        double a=incoming->time,b=outgoing->time;
        /* Shift the playing index, reorder, then remove that incoming visit. */
        assert(ts_gallery_sequence_insert(g,0,&g->sequence.step[0]));
        assert(ts_gallery_sequence_move(g,g->player.step,(g->player.step+1)%g->sequence.length));
        assert(ts_gallery_sequence_remove(g,g->player.step));
        assert(g->player.step==-1 && incoming->time==a && outgoing->time==b);
        for(int i=0;i<75;++i) {
            TsSourceRouteMix x={0},y={0};
            TsStereoFrame u=ts_gallery_read(g,1000,&x),v=ts_gallery_read(reference,1000,&y);
            assert(u.l==v.l && u.r==v.r && x.graph[7].l==y.graph[7].l && x.graph[7].r==y.graph[7].r);
        }
        assert(g->outgoing<0 && !g->spare_active);free_gallery(g);free_gallery(reference);
    }
}
