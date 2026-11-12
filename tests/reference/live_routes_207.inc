/* Frozen routing glue from PR #207, 63ea32c84047cb40c03f8839eb02373c25424b91.
   Keep the old full stack workspaces and unity-add passes as the oracle.
   Voice DSP and transport helpers are shared; this change does not edit them. */
static TsStereoFrame reference_mosaic_read(TsMosaic *m,int rate,TsSourceRouteMix *routes)
{
    if(!m || rate<=0)return (TsStereoFrame){0,0};
    if(m->rate!=rate) {
        mosaic_reset_voices(m);
        for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(m->events[i].live)m->voices[i].active=0;
        m->rate=rate;
    }
    ts_pattern_engine_frame(m->pattern_engine);
    if(m->sequence.running && !m->live_paused)mosaic_sequence_tick(m,rate);
    if(m->block_remaining)--m->block_remaining;
    else {m->schedule.valid=0;++m->control_serial;}
    if(!routes) {
        TsStereoFrame out=mosaic_read(m,rate,NULL),preview=mosaic_audition_read(m,rate,NULL);
        if(!m->schedule.valid)mosaic_schedule_prepare(m);
        TsStereoFrame live=mosaic_live_read(m,rate,NULL);
        m->rate=rate;out.l+=preview.l+live.l;out.r+=preview.r+live.r;return ts_stereo_frame_sanitize(out);
    }
    if(!m->live_reset_remaining && !m->live_pause_routes_valid && !m->live_paused && !m->playing && !m->sequence.running && !m->audition_id && !m->was_playing && !m->transition_remaining && !m->route_handoff.remaining && !m->schedule.live_count && m->schedule.valid)return (TsStereoFrame){0,0};
    TsSourceRouteMix mix={0};
    TsStereoFrame out=mosaic_read(m,rate,routes?&mix:NULL);
    TsStereoFrame preview=mosaic_audition_read(m,rate,&mix);
    if(!m->schedule.valid)mosaic_schedule_prepare(m);
    TsStereoFrame live=mosaic_live_read(m,rate,&mix);m->rate=rate;out.l+=preview.l+live.l;out.r+=preview.r+live.r;
    if(m && routes && rate>0) {
        ts_source_route_handoff(&m->route_handoff,&mix,m->route_discontinuity,(unsigned)rate/500);
        m->route_discontinuity=0;ts_source_route_add(routes,&mix,1);
    }
    return out;
}
static void reference_gallery_park(TsMosaicGallery *g)
{
    TsMosaic *old=ts_gallery_outgoing(g);if(!old)return;
    old->playing=0;old->live_paused=1;old->live_pause_gain=0;
    g->canvas[g->outgoing].parked=1;g->outgoing=-1;
}
static TsStereoFrame reference_gallery_read(TsMosaicGallery *g,int rate,TsSourceRouteMix *routes)
{
    TsMosaic *m=ts_gallery_active(g),*old=ts_gallery_outgoing(g);
    if(!old)return reference_mosaic_read(m,rate,routes);
    if(rate<=0)return (TsStereoFrame){0};
    /* Constant-sum fade: identical canvases retain their level. Every route,
       send and reference tap receives the same gain; only two engines render. */
    float t=(float)fmax(0,fmin(1,g->fade_position/g->fade_length));
    TsSourceRouteMix a,b;
    if(routes){ts_source_route_mix_init(&a,routes);ts_source_route_mix_init(&b,routes);}
    TsStereoFrame x=reference_mosaic_read(m,rate,routes?&a:NULL);
    TsStereoFrame y=reference_mosaic_read(old,rate,routes?&b:NULL);
    if(routes){ts_source_route_add(routes,&a,t);ts_source_route_add(routes,&b,1-t);}
    g->fade_position+=1./rate;
    if(g->fade_position+1e-12>=g->fade_length)reference_gallery_park(g);
    return ts_stereo_frame_sanitize((TsStereoFrame){x.l*t+y.l*(1-t),x.r*t+y.r*(1-t)});
}
