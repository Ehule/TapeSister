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
    if(!e || !e->live || (e->source->pattern || e->source->video_path[0]))return 0;
    int slot=(int)(e-m->events);TsMosaicLive *l=&m->live[slot];
    m->live_reset_ids[slot]=0;
    /* A fresh timeline/manual trigger takes over from a reset sequence plan. */
    if(m->live_reset_sequence && m->sequence.members[slot]==id)m->live_reset_sequence=0;
    if(ts_mosaic_sequence_owns(m,slot))return 1; /* Sequence owns its launch boundaries. */
    if(l->id==id && l->state!=TS_MOSAIC_LIVE_STOPPED)return 1;
    *l=(TsMosaicLive){.id=id,.state=quantize && m->live_master?TS_MOSAIC_LIVE_WAITING:TS_MOSAIC_LIVE_PLAYING};
    m->voices[slot].active=0;m->schedule.valid=0;return 1;
}
void ts_mosaic_live_stop(TsMosaic *m,uint64_t id)
{
    if(!m)return;
    if(id && m->live_reset_sequence) {
        TsMosaicEvent *e=ts_mosaic_find(m,id);
        if(e && m->sequence.members[e-m->events]==id)ts_mosaic_sequence_stop(m);
    }
    if(!id)m->live_reset_sequence=0;
    if(!id || (ts_mosaic_find(m,id) && ts_mosaic_sequence_owns(m,(int)(ts_mosaic_find(m,id)-m->events))))ts_mosaic_sequence_stop(m);
    for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(m->events[i].id && m->events[i].live && (!id || m->events[i].id==id)) {
        m->live_reset_ids[i]=0;
        TsMosaicLive *l=&m->live[i];
        if(l->id!=m->events[i].id)*l=(TsMosaicLive){.id=m->events[i].id};
        mosaic_live_release_slot(m,i);
    }
}
void ts_mosaic_live_restart(TsMosaic *m,uint64_t id)
{
    TsMosaicEvent *e=ts_mosaic_find(m,id);if(!e || !e->live)return;
    if(ts_mosaic_sequence_owns(m,(int)(e-m->events)))ts_mosaic_sequence_stop(m);
    m->live[e-m->events].state=TS_MOSAIC_LIVE_STOPPED;
    (void)ts_mosaic_live_launch(m,id,0);
}
void ts_mosaic_live_pause(TsMosaic *m,int paused)
{
    if(!m || m->live_paused==(paused!=0))return;
    if(paused && !m->sequence.running && !ts_mosaic_live_count(m))return;
    if(paused){m->live_pause_base=m->live_pause_gain;m->live_reset_remaining=0;}
    m->live_paused=paused!=0;
}
int ts_mosaic_live_ready(const TsMosaic *m)
{
    if(!m)return 0;
    int count=0;
    for(int i=0;i<TS_MOSAIC_EVENTS;++i) {
        uint64_t id=m->events[i].id;
        if(id && m->events[i].live && (m->live_reset_ids[i]==id ||
            (m->live_reset_sequence && m->sequence.members[i]==id)))++count;
    }
    return count;
}
void ts_mosaic_live_home(TsMosaic *m)
{
    if(!m)return;
    /* Keep a bounded restart plan, but release ownership so timeline crossings
       can trigger normally even if F11 Play is never used again. */
    if(m->sequence.running)m->live_reset_sequence=1;
    for(int i=0;i<TS_MOSAIC_EVENTS;++i) {
        const TsMosaicEvent *e=&m->events[i];TsMosaicLive *l=&m->live[i];
        if(!e->id || !e->live){m->live_reset_ids[i]=0;continue;}
        if(l->id==e->id && l->state)m->live_reset_ids[i]=e->id;
        *l=(TsMosaicLive){.id=e->id};m->voices[i].active=0;
    }
    TsMosaicSequence *seq=&m->sequence;
    seq->running=seq->finishing=seq->started=seq->index=0;seq->pending=1;
    seq->duration=seq->drain=0;seq->speed=1;
    float audible=m->live_paused?(m->live_pause_base>0?(float)(m->live_pause_gain/m->live_pause_base):0):1;
    m->live_reset_from=(TsStereoFrame){m->live_pause_last.l*audible,m->live_pause_last.r*audible};
    memset(&m->live_reset_routes,0,sizeof(m->live_reset_routes));
    if(m->live_pause_routes_valid && audible>0)ts_source_route_add(&m->live_reset_routes,&m->live_pause_routes,audible);
    m->live_reset_total=m->live_reset_remaining=(unsigned)fmax(1,m->rate*.005);
    m->live_paused=0;m->live_pause_gain=m->live_pause_base=1;
    m->live_phase=0;m->live_clock_edge=0;
    m->live_trigger_floor=m->time-1e-9;m->schedule.valid=0;
}
void ts_mosaic_live_resume(TsMosaic *m)
{
    if(!m)return;
    if(m->live_paused){ts_mosaic_live_pause(m,0);return;}
    if(m->live_reset_sequence) {
        m->sequence.running=1;m->sequence.pending=1;m->sequence.index=0;
        m->live_reset_sequence=0;
    }
    for(int i=0;i<TS_MOSAIC_EVENTS;++i) {
        uint64_t id=m->live_reset_ids[i];m->live_reset_ids[i]=0;
        if(id && m->events[i].id==id && m->events[i].live)
            (void)ts_mosaic_live_launch(m,id,m->live_capture.quantize);
    }
    m->schedule.valid=0;
}
void ts_mosaic_live_transport(TsMosaic *m,int command)
{
    if(!m)return;
    for(int i=0;i<TS_MOSAIC_EVENTS;++i) {
        TsMosaicEvent *e=&m->events[i];if(!e->id || !e->live)continue;
        if(ts_mosaic_sequence_owns(m,i))continue;
        if(command==2){if(e->live_position && m->live[i].state)ts_mosaic_live_restart(m,e->id);}
        else if(e->live_transport) {
            if(command==1)(void)ts_mosaic_live_launch(m,e->id,0);
            else ts_mosaic_live_stop(m,e->id);
        }
    }
}
void ts_mosaic_live_finish(TsMosaic *m,uint64_t id)
{
    TsMosaicEvent *e=ts_mosaic_find(m,id);if(!e || !e->live)return;
    int i=(int)(e-m->events);if(ts_mosaic_sequence_owns(m,i))return;
    TsMosaicLive *l=&m->live[i];if(l->id!=id || !l->state || l->state==TS_MOSAIC_LIVE_RELEASING)return;
    if(l->state==TS_MOSAIC_LIVE_WAITING){l->state=0;return;}
    double in,out;ts_mosaic_event_fades(e,&in,&out);
    l->finish_gain=(float)fmax(0,fmin(1,fmin(in>0?l->elapsed/in:1,out>0?(e->duration-l->elapsed)/out:1)));
    TsMosaicVoice *v=&m->voices[i];
    if(v->attack<v->attack_frames)l->finish_gain*=ts_audition_attack_gain(v->attack,v->attack_frames);
    v->attack=v->attack_frames;
    l->finish_seconds=fmax(.005,e->finish_fade);l->release=0;l->state=TS_MOSAIC_LIVE_RELEASING;
    m->voices[i].control_serial=0;
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
    double step=1./fmax(1,rate*.005);
    if(m->live_paused) {
        m->live_pause_gain=fmax(0,m->live_pause_gain-step);
        float fade=m->live_pause_base>0?(float)(m->live_pause_gain/m->live_pause_base):0;
        if(routes && fade>0 && m->live_pause_routes_valid)ts_source_route_add(routes,&m->live_pause_routes,fade);
        out=(TsStereoFrame){m->live_pause_last.l*fade,m->live_pause_last.r*fade};
        if(!m->schedule.live_count && !m->sequence.running && !m->live_pause_gain) {
            m->live_paused=0;m->live_pause_gain=m->live_pause_base=1;
        }
        return out;
    }
    m->live_pause_gain=fmin(1,m->live_pause_gain+step);
    /* Main time can hold, seek or repeat while each loop keeps its own phase.
       Slew both policy switches and envelope edits before every output route. */
    float target=m->live_envelope?ts_mosaic_volume_at(m,m->schedule.end>0?m->time/m->schedule.end:0):1;
    float envelope_step=1.f/fmaxf(1,rate*.020f);
    if(!m->live_envelope_running)m->live_envelope_gain=target;
    else m->live_envelope_gain+=fmaxf(-envelope_step,fminf(envelope_step,target-m->live_envelope_gain));
    /* UI commands exclude the whole callback. Capture routes only on its
       final frame (or each standalone read), keeping normal mix order intact. */
    int capture=routes && !m->block_remaining && (m->schedule.live_count || m->live_reset_remaining);
    TsSourceRouteMix before;if(capture)before=*routes;
    if((!m->schedule.live_count && !m->live_reset_remaining) || (!routes && !m->block_remaining))m->live_pause_routes_valid=0;
    if(m->live_reset_remaining) {
        float gain=(float)--m->live_reset_remaining/m->live_reset_total;
        out=(TsStereoFrame){m->live_reset_from.l*gain,m->live_reset_from.r*gain};
        if(routes && gain>0)ts_source_route_add(routes,&m->live_reset_routes,gain);
    }
    m->live_envelope_running=0;
    for(int at=0;at<m->schedule.live_count;++at) {
        int i=m->schedule.live[at];const TsMosaicEvent *saved=&m->events[i];TsMosaicLive *l=&m->live[i];
        if(l->id!=saved->id || !saved->live || saved->source->pattern || !l->state)continue;
        if(l->state==TS_MOSAIC_LIVE_WAITING) {
            if(m->live_master && !m->live_clock_edge)continue;
            l->state=TS_MOSAIC_LIVE_PLAYING;
        }
        TsMosaicEvent e=*saved;e.start=0;e.looping=1;e.mode=TS_LOOP_FORWARD;e.crossfade=0;
        int sequenced=ts_mosaic_sequence_owns(m,i) && !m->sequence.pending && m->sequence.slots[m->sequence.index]==i;
        if(sequenced){e.live_finite=1;e.duration=m->sequence.duration;}
        if(e.live_finite && l->elapsed+1e-10>=e.duration){l->state=0;m->voices[i].active=0;continue;}
        if(!e.live_finite)e.fade_out=0;
        /* Deliberate tape-rate following changes pitch; no hidden stretching.
           Preserve phase when clock following or tempo is edited. */
        double speed=mosaic_live_speed(m,&e);if(sequenced)m->sequence.speed=speed;
        float release=1;
        if(l->state==TS_MOSAIC_LIVE_RELEASING) {
            release=fmaxf(0,1-(float)++l->release/fmax(1,rate*(l->finish_seconds>0?l->finish_seconds:.005)));
            if(l->finish_seconds>0){release*=l->finish_gain;e.fade_in=e.fade_out=0;}
            if(release==0){l->state=0;m->voices[i].active=0;continue;}
        }
        m->live_envelope_running=1;
        TsStereoFrame card=mosaic_event_read(m->pattern_engine,&e,&m->voices[i],l->elapsed,rate,speed,
            m->gain*release*(float)m->live_pause_gain*m->live_envelope_gain,!e.muted && (!m->schedule.any_solo || e.solo),routes,m->control_serial);
        out.l+=card.l;out.r+=card.r;
        l->elapsed+=speed/rate;l->phase=(m->voices[i].position[0]-e.first)/(e.last-e.first);
    }
    m->live_pause_last=out;
    if(capture) {m->live_pause_routes_valid=1;m->live_pause_routes=*routes;ts_source_route_add(&m->live_pause_routes,&before,-1.f);}
    if(m->live_master && rate>0) {
        m->live_phase+=1.0/(rate*m->live_quarter);
        m->live_clock_edge=m->live_phase>=1;
        m->live_phase-=floor(m->live_phase);
    } else m->live_clock_edge=0;
    return out;
}
