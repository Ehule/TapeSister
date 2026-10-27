/* One existing voice slot per card. No sample scanning, allocation, file I/O,
   or new pattern backend is introduced in the audio callback. */
double ts_mosaic_live_cycle(const TsMosaicEvent *e)
{
    return e && e->source && e->source->sample.sample_rate && e->last>e->first?
        (double)(e->last-e->first)/e->source->sample.sample_rate:0;
}
int ts_mosaic_live_count(const TsMosaic *m)
{
    int count=0;if(m)for(int i=0;i<TS_MOSAIC_EVENTS;++i)
        count+=m->events[i].id && m->events[i].live && m->live[i].id==m->events[i].id &&
            m->live[i].state!=TS_MOSAIC_LIVE_STOPPED;
    return count;
}
int ts_mosaic_live_launch(TsMosaic *m,uint64_t id,int quantize)
{
    TsMosaicEvent *e=ts_mosaic_find(m,id);
    if(!e || !e->live || e->source->pattern)return 0;
    int slot=(int)(e-m->events);TsMosaicLive *l=&m->live[slot];
    if(l->id==id && l->state!=TS_MOSAIC_LIVE_STOPPED)return 1;
    *l=(TsMosaicLive){.id=id,.state=quantize && m->live_master?TS_MOSAIC_LIVE_WAITING:TS_MOSAIC_LIVE_PLAYING};
    m->voices[slot].active=0;m->schedule.valid=0;return 1;
}
void ts_mosaic_live_stop(TsMosaic *m,uint64_t id)
{
    if(!m)return;
    for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(m->events[i].id && m->events[i].live && (!id || m->events[i].id==id)) {
        TsMosaicLive *l=&m->live[i];
        if(l->id!=m->events[i].id)*l=(TsMosaicLive){.id=m->events[i].id};
        if(l->state==TS_MOSAIC_LIVE_PLAYING){l->state=TS_MOSAIC_LIVE_RELEASING;l->release=0;}
        else if(l->state==TS_MOSAIC_LIVE_WAITING)l->state=TS_MOSAIC_LIVE_STOPPED;
    }
}
void ts_mosaic_live_restart(TsMosaic *m,uint64_t id)
{
    TsMosaicEvent *e=ts_mosaic_find(m,id);if(!e || !e->live)return;
    m->live[e-m->events].state=TS_MOSAIC_LIVE_STOPPED;
    (void)ts_mosaic_live_launch(m,id,0);
}
void ts_mosaic_live_transport(TsMosaic *m,int command)
{
    if(!m)return;
    for(int i=0;i<TS_MOSAIC_EVENTS;++i) {
        TsMosaicEvent *e=&m->events[i];if(!e->id || !e->live)continue;
        if(command==2){if(e->live_position && m->live[i].state)ts_mosaic_live_restart(m,e->id);}
        else if(e->live_transport) {
            if(command==1)(void)ts_mosaic_live_launch(m,e->id,0);
            else ts_mosaic_live_stop(m,e->id);
        }
    }
}
void ts_mosaic_live_clock(TsMosaic *m,int enabled,int running,double quarter,double phase)
{
    if(!m)return;
    if(enabled && m->live_master && phase+1e-9<m->live_phase)m->live_clock_edge=1;
    m->live_master=enabled;m->live_master_running=running;
    m->live_quarter=isfinite(quarter) && quarter>0?quarter:.5;m->live_phase=phase;
}
static TsStereoFrame mosaic_live_read(TsMosaic *m,int rate,TsSourceRouteMix *routes)
{
    TsStereoFrame out={0,0};
    for(int at=0;at<m->schedule.live_count;++at) {
        int i=m->schedule.live[at];const TsMosaicEvent *saved=&m->events[i];TsMosaicLive *l=&m->live[i];
        if(l->id!=saved->id || !saved->live || saved->source->pattern || !l->state)continue;
        if(l->state==TS_MOSAIC_LIVE_WAITING) {
            if(m->live_master && !m->live_clock_edge)continue;
            l->state=TS_MOSAIC_LIVE_PLAYING;
        }
        if(saved->live_finite && l->elapsed+1e-10>=saved->duration){l->state=0;m->voices[i].active=0;continue;}
        TsMosaicEvent e=*saved;e.start=0;e.looping=1;e.mode=TS_LOOP_FORWARD;e.crossfade=0;
        if(!e.live_finite)e.fade_out=0;
        double speed=1,cycle=ts_mosaic_live_cycle(&e);
        if(e.live_tempo && m->live_master && e.live_beats>0)
            speed=cycle/(e.live_beats*m->live_quarter);
        /* Deliberate tape-rate following changes pitch; no hidden stretching.
           Preserve phase when clock following or tempo is edited. */
        speed=fmax(.03125,fmin(32,speed));
        float release=1;
        if(l->state==TS_MOSAIC_LIVE_RELEASING) {
            release=fmaxf(0,1-(float)++l->release/fmax(1,rate*.005));
            if(release==0){l->state=0;m->voices[i].active=0;continue;}
        }
        TsStereoFrame card=mosaic_event_read(m->pattern_engine,&e,&m->voices[i],l->elapsed,rate,speed,
            m->gain*release,!e.muted && (!m->schedule.any_solo || e.solo),routes,m->control_serial);
        out.l+=card.l;out.r+=card.r;
        l->elapsed+=speed/rate;l->phase=(m->voices[i].position[0]-e.first)/(e.last-e.first);
    }
    if(m->live_master && rate>0) {
        m->live_phase+=1.0/(rate*m->live_quarter);
        m->live_clock_edge=m->live_phase>=1;
        m->live_phase-=floor(m->live_phase);
    } else m->live_clock_edge=0;
    return out;
}
