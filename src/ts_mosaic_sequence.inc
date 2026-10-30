/* Only Cycle's control-thread preflight sorts. The callback visits the current
   step directly and advances on audio frames, with fractional carry preserved. */
typedef struct {uint64_t id;int rank,slot;} MosaicSequenceEntry;
static int mosaic_sequence_compare(const void *a,const void *b)
{
    const MosaicSequenceEntry *x=a,*y=b;
    if(x->rank!=y->rank)return x->rank<y->rank?-1:1;
    return x->id<y->id?-1:x->id>y->id;
}
int ts_mosaic_sequence_order(const TsMosaic *m,int *slots)
{
    MosaicSequenceEntry entries[TS_MOSAIC_EVENTS];int count=0;
    for(int i=0;i<TS_MOSAIC_EVENTS;++i) {
        const TsMosaicEvent *e=&m->events[i];
        if(e->id && e->live && !e->source->pattern && e->live_order>=0)
            entries[count++]=(MosaicSequenceEntry){e->id,e->live_order?e->live_order:TS_MOSAIC_EVENTS+1,i};
    }
    qsort(entries,(size_t)count,sizeof(entries[0]),mosaic_sequence_compare);
    for(int i=0;i<count;++i)slots[i]=entries[i].slot;
    return count;
}
int ts_mosaic_sequence_owns(const TsMosaic *m,int slot)
{return m && m->sequence.running && slot>=0 && slot<TS_MOSAIC_EVENTS &&
    m->events[slot].id && m->sequence.members[slot]==m->events[slot].id;}
static void mosaic_live_release_slot(TsMosaic *m,int i)
{
    TsMosaicLive *l=&m->live[i];
    if(m->live_paused) {l->state=TS_MOSAIC_LIVE_STOPPED;m->voices[i].active=0;m->schedule.valid=0;return;}
    if(l->state==TS_MOSAIC_LIVE_RELEASING && l->finish_seconds<=0)return;
    if(l->state==TS_MOSAIC_LIVE_PLAYING || l->state==TS_MOSAIC_LIVE_RELEASING) {
        /* Interrupting an optional long finish keeps its current gain. */
        if(l->finish_seconds>0) {
            l->finish_gain*=fmax(0,1-l->release/fmax(1,m->rate*l->finish_seconds));
            l->finish_seconds=.005;
        }
        l->state=TS_MOSAIC_LIVE_RELEASING;l->release=0;
    } else l->state=TS_MOSAIC_LIVE_STOPPED;
}
void ts_mosaic_sequence_stop(TsMosaic *m)
{
    if(!m || (!m->sequence.running && !m->live_reset_sequence))return;
    m->live_reset_sequence=0;
    for(int i=0;i<m->sequence.count;++i) {
        int slot=m->sequence.slots[i];
        m->live_reset_ids[slot]=0;
        if(m->events[slot].id==m->sequence.ids[i] && m->live[slot].id==m->sequence.ids[i])mosaic_live_release_slot(m,slot);
    }
    m->sequence.running=m->sequence.finishing=0;
    memset(m->sequence.members,0,sizeof(m->sequence.members));m->schedule.valid=0;
}
int ts_mosaic_sequence_cycle(TsMosaic *m)
{
    if(!m)return 0;
    TsMosaicSequence *s=&m->sequence;
    if(s->running){s->finishing=0;return 1;}
    m->live_reset_sequence=0;
    s->count=ts_mosaic_sequence_order(m,s->slots);if(!s->count)return 0;
    memset(s->members,0,sizeof(s->members));s->index=0;s->finishing=0;s->pending=1;s->started=0;s->drain=0;s->duration=0;s->speed=1;
    for(int n=0;n<s->count;++n) {
        int i=s->slots[n];m->live_reset_ids[i]=0;s->ids[n]=m->events[i].id;s->members[i]=s->ids[n];
        if(m->live[i].id==s->ids[n] && m->live[i].state) {
            if(m->live[i].state!=TS_MOSAIC_LIVE_WAITING)s->drain=.005;
            mosaic_live_release_slot(m,i);
        }
    }
    s->running=1;m->schedule.valid=0;return 1;
}
void ts_mosaic_sequence_finish(TsMosaic *m)
{if(m && m->sequence.running)m->sequence.finishing=1;}
static double mosaic_live_speed(const TsMosaic *m,const TsMosaicEvent *e)
{
    double speed=e->live_tempo && m->live_master && e->live_beats>0?
        ts_mosaic_live_cycle(e)/(e->live_beats*m->live_quarter):1;
    return fmax(.03125,fmin(32,speed));
}
static void mosaic_sequence_tick(TsMosaic *m,int rate)
{
    TsMosaicSequence *s=&m->sequence;
    if(s->drain>0){s->drain=fmax(0,s->drain-1./rate);return;}
    double carry=0;
    for(int tries=0;s->running && tries<=s->count;++tries) {
        int i=s->slots[s->index];TsMosaicEvent *e=&m->events[i];TsMosaicLive *l=&m->live[i];
        int valid=e->id==s->ids[s->index] && e->live && !e->source->pattern;
        if(s->pending && valid) {
            s->speed=mosaic_live_speed(m,e);
            s->duration=fmax(e->duration,s->speed/rate); /* At least one output frame per step. */
            *l=(TsMosaicLive){.id=e->id,.elapsed=carry*s->speed,
                .state=m->live_capture.quantize && m->live_master && !s->started?TS_MOSAIC_LIVE_WAITING:TS_MOSAIC_LIVE_PLAYING};
            s->started=1;
            m->voices[i].active=0;s->pending=0;m->schedule.valid=0;
        }
        if(valid && !s->pending && l->id==e->id && l->state &&
           (l->state==TS_MOSAIC_LIVE_WAITING || l->elapsed+1e-10<s->duration))return;
        if(valid && !s->pending && l->id==e->id) {
            carry=fmax(0,l->elapsed-s->duration)/fmax(.03125,s->speed);
            l->state=0;m->voices[i].active=0;
        }
        if(++s->index==s->count) {
            if(s->finishing){s->running=s->finishing=0;memset(s->members,0,sizeof(s->members));m->schedule.valid=0;return;}
            s->index=0;
        }
        s->pending=1;
    }
    /* All queued cards may have been deleted or converted during playback. */
    if(s->pending){s->running=s->finishing=0;memset(s->members,0,sizeof(s->members));m->schedule.valid=0;}
}
