/* Build once per buffer, then visit only active cards and pending start times.
   The host excludes UI mutations for the buffer. Rebuilding at its boundary
   also observes direct edits to public card fields (dragging, routing, solo).
   A binary min-heap avoids allocation and unbounded insertion sorting. */
static int mosaic_pending_before(const TsMosaic *m,int a,int b)
{
    double x=m->events[a].start,y=m->events[b].start;
    return x<y || (x==y && a<b);
}
static void mosaic_pending_down(TsMosaic *m,int at)
{
    TsMosaicSchedule *s=&m->schedule;int slot=s->pending[at];
    for(int child=at*2+1;child<s->pending_count;child=at*2+1) {
        if(child+1<s->pending_count && mosaic_pending_before(m,s->pending[child+1],s->pending[child]))++child;
        if(!mosaic_pending_before(m,s->pending[child],slot))break;
        s->pending[at]=s->pending[child];at=child;
    }
    s->pending[at]=slot;
}
static void mosaic_schedule_prepare(TsMosaic *m)
{
    TsMosaicSchedule *s=&m->schedule;
    s->active_count=s->pending_count=s->any_solo=0;s->end=0;
    for(int i=0;i<TS_MOSAIC_EVENTS;++i) {
        const TsMosaicEvent *e=&m->events[i];
        if(e->id) {
            double end=e->start+e->duration;s->end=fmax(s->end,end);s->any_solo|=e->solo;
            if(m->time<end) {
                if(m->time+1e-10>=e->start){s->active[s->active_count++]=i;continue;}
                s->pending[s->pending_count++]=i;
            }
        }
        m->voices[i].active=0;
    }
    for(int at=s->pending_count/2;at-->0;)mosaic_pending_down(m,at);
    s->expected_time=m->time;s->revision=m->revision;s->valid=1;++m->control_serial;
}
static void mosaic_schedule_start(TsMosaic *m)
{
    TsMosaicSchedule *s=&m->schedule;
    while(s->pending_count && m->events[s->pending[0]].start<=m->time+1e-10) {
        int slot=s->pending[0];s->pending[0]=s->pending[--s->pending_count];
        if(s->pending_count)mosaic_pending_down(m,0);
        const TsMosaicEvent *e=&m->events[slot];
        if(m->time>=e->start+e->duration)continue;
        int at=s->active_count++;
        while(at>0 && s->active[at-1]>slot){s->active[at]=s->active[at-1];--at;}
        s->active[at]=slot;
    }
}
void ts_mosaic_begin_block(TsMosaic *m,unsigned frames)
{
    if(!m)return;
    m->block_remaining=frames;m->schedule.valid=0;++m->control_serial;
}
