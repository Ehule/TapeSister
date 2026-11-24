/* Gallery sections are audio-clocked. Parameter edits are read on the next visit;
   live arrangement edits retain the sounding range and in-flight transition. */
void ts_gallery_sequence_default(TsGallerySequence *s,uint64_t id)
{
    memset(s,0,sizeof(*s));s->length=1;
    for(int i=0;i<TS_GALLERY_SEQUENCE_STEPS;++i)
        s->step[i]=(TsGalleryStep){.canvas_id=id,.in_marker=TS_GALLERY_POINT_TIME,
            .out_marker=TS_GALLERY_POINT_END,.fade_seconds=2,.repeats=1};
}
int ts_gallery_sequence_valid(const TsGallerySequence *s)
{
    if(!s || s->length<1 || s->length>TS_GALLERY_SEQUENCE_STEPS || s->loop<0 || s->loop>1 ||
       s->first<0 || s->last<s->first || s->last>=s->length)return 0;
    for(int i=0;i<s->length;++i) {
        const TsGalleryStep *p=&s->step[i];
        if(!p->canvas_id || p->in_marker<TS_GALLERY_POINT_TIME || p->in_marker>=TS_MOSAIC_MARKERS ||
           p->out_marker<TS_GALLERY_POINT_END || p->out_marker>=TS_MOSAIC_MARKERS ||
           !isfinite(p->in_seconds) || !isfinite(p->out_seconds) || !isfinite(p->fade_seconds) ||
           p->in_seconds<0 || p->out_seconds<0 || p->in_seconds>86400 || p->out_seconds>86400 ||
           p->fade_seconds<0 || p->fade_seconds>3600 || p->repeats<1 || p->repeats>999 ||
           p->curve<0 || p->curve>=TS_GALLERY_FADE_CURVES)return 0;
    }
    return 1;
}
int ts_gallery_step_bounds(const TsMosaicGallery *g,const TsGalleryStep *p,int *canvas,double *in,double *out,char *e,size_t n)
{
    int index=-1;
    if(g && p)for(int i=0;i<g->count;++i)if(g->canvas[i].id==p->canvas_id){index=i;break;}
    if(index<0)return gallery_error(e,n,"STEP CANVAS IS MISSING / SELECT A CANVAS");
    const TsMosaic *m=g->canvas[index].scene;
    if(p->in_marker<TS_GALLERY_POINT_TIME || p->in_marker>=TS_MOSAIC_MARKERS ||
       p->out_marker<TS_GALLERY_POINT_END || p->out_marker>=TS_MOSAIC_MARKERS)
        return gallery_error(e,n,"INVALID SECTION ENDPOINT");
    if((p->in_marker>=0 && !m->markers[p->in_marker].stored) ||
       (p->out_marker>=0 && !m->markers[p->out_marker].stored))
        return gallery_error(e,n,"STEP MARKER IS EMPTY / CHOOSE A STORED MARKER");
    double a=p->in_marker>=0?m->markers[p->in_marker].time:p->in_seconds;
    double b=p->out_marker>=0?m->markers[p->out_marker].time:
        p->out_marker==TS_GALLERY_POINT_END?ts_mosaic_end(m):p->out_seconds;
    if(!isfinite(a) || !isfinite(b) || a<0 || b<a+.001 || b>86400)
        return gallery_error(e,n,"SECTION MUST BE AT LEAST 1MS / CHECK START, END AND EMPTY CANVAS");
    if(canvas)*canvas=index;
    if(in)*in=a;
    if(out)*out=b;
    return 1;
}
static void gallery_spare_release(TsMosaicGallery *g)
{
    if(!g->spare_active)return;
    TsMosaic *m=g->spare;
    for(int i=0;i<TS_MOSAIC_EVENTS;++i) {
        if(m->events[i].source)--m->events[i].source->pins;
        if(m->voices[i].pattern_source)--m->voices[i].pattern_source->pins;
    }
    if(m->audition_source.source)--m->audition_source.source->pins;
    if(m->audition_voice.pattern_source)--m->audition_voice.pattern_source->pins;
    g->spare_active=0;m->playing=0;m->section_transport=0;
}
/* Preserve the outgoing playheads, including tracker backend state. Only the
   playback prefix/tail is copied, never the 32 arrangement Undo snapshots. */
static void gallery_spare_capture(TsMosaicGallery *g,TsMosaic *from)
{
    TsMosaic *m=g->spare;void *idle_engine=m->pattern_engine;
    memcpy(m->events,from->events,sizeof(m->events));
    memcpy(m->voices,from->voices,sizeof(m->voices));
    memcpy(m->volume,from->volume,sizeof(m->volume));
    memcpy(m->markers,from->markers,sizeof(m->markers));
    m->loop_range=from->loop_range;
    size_t offset=offsetof(TsMosaic,audition_id);
    memcpy((unsigned char *)m+offset,(unsigned char *)from+offset,sizeof(*m)-offset);
    m->sources=NULL;m->history_count=m->history_cursor=m->history_first=0;
    memset(m->pattern_retired,0,sizeof(m->pattern_retired));
    m->pattern_engine=from->pattern_engine;from->pattern_engine=idle_engine;
    for(int i=0;i<TS_MOSAIC_EVENTS;++i) {
        if(!m->events[i].id)m->events[i].source=NULL;
        if(!m->voices[i].active)m->voices[i].pattern_source=NULL;
        if(!m->voices[i].pattern_owner)m->voices[i].pattern_owner=&from->voices[i];
        if(m->events[i].source)++m->events[i].source->pins;
        if(m->voices[i].pattern_source)++m->voices[i].pattern_source->pins;
    }
    if(!m->audition_voice.active)m->audition_voice.pattern_source=NULL;
    if(!m->audition_voice.pattern_owner)m->audition_voice.pattern_owner=&from->audition_voice;
    if(m->audition_source.source)++m->audition_source.source->pins;
    if(m->audition_voice.pattern_source)++m->audition_voice.pattern_source->pins;
    g->spare_active=1;
}
int ts_gallery_sequence_prepare(TsMosaicGallery *g,char *e,size_t n)
{
    if(!g)return gallery_error(e,n,"OPEN GALLERY FIRST");
    if(!g->spare) {
        g->spare=ts_mosaic_create();
        if(g->spare) {
            /* Fault in only the playback storage before audio can use it. */
            memset(g->spare->events,0,sizeof(g->spare->events));
            memset(g->spare->voices,0,sizeof(g->spare->voices));
        }
    }
    if(!g->spare)return gallery_error(e,n,"CANNOT PREPARE SECTION CROSSFADES");
    /* After a same-canvas handoff either engine can become the next player. */
    unsigned needed=1;
    for(int c=0;c<g->count;++c) {
        unsigned count=1;const TsMosaic *m=g->canvas[c].scene;
        for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(m->events[i].id && m->events[i].source && m->events[i].source->pattern)++count;
        if(count>needed)needed=count;
    }
    if(!ts_pattern_engine_reserve(g->spare->pattern_engine,needed*TS_MOSAIC_NOTES))
        return gallery_error(e,n,"CANNOT PREPARE PATTERN CROSSFADES");
    for(int c=0;c<g->count;++c)if(!ts_pattern_engine_reserve(g->canvas[c].scene->pattern_engine,needed*TS_MOSAIC_NOTES))
        return gallery_error(e,n,"CANNOT PREPARE PATTERN CROSSFADES");
    return 1;
}
static int gallery_sequence_following(const TsMosaicGallery *g,int *visit)
{
    const TsGallerySequencePlayer *p=&g->player;
    *visit=1;
    if(p->queued>=0 && p->queued<g->sequence.length)return p->queued;
    if(p->step<0)return p->continuation<g->sequence.length?p->continuation:-1;
    if(p->visit<p->current.repeats){*visit=p->visit+1;return p->step;}
    if(g->sequence.loop && p->step==g->sequence.last)return g->sequence.first;
    return p->step+1<g->sequence.length?p->step+1:-1;
}
static double gallery_sequence_fade(const TsMosaicGallery *g,int next)
{
    double in,out;int canvas;
    if(next<0 || !ts_gallery_step_bounds(g,&g->sequence.step[next],&canvas,&in,&out,NULL,0))return 0;
    const TsMosaic *m=ts_gallery_active(g),*to=g->canvas[canvas].scene;
    /* Half of each section leaves room for its other boundary without ever
       requiring a third renderer. The editor displays the effective duration. */
    return fmax(0,fmin(g->player.current.fade_seconds,
        fmin((g->player.end-g->player.start)/fmax(.5,m->speed_current)*.5,
             (out-in)/fmax(.5,to->speed)*.5)));
}
static void gallery_sequence_refresh(TsMosaicGallery *g)
{
    if(!g->player.running)return;
    g->player.next=gallery_sequence_following(g,&g->player.next_visit);
    g->player.next_fade=gallery_sequence_fade(g,g->player.next);
}
int ts_gallery_sequence_insert(TsMosaicGallery *g,int at,const TsGalleryStep *step)
{
    if(!g || !step || at<0 || at>g->sequence.length || g->sequence.length>=TS_GALLERY_SEQUENCE_STEPS)return 0;
    TsGallerySequence *s=&g->sequence;TsGallerySequencePlayer *p=&g->player;
    TsGalleryStep added=*step; /* The source may itself be shifted by insertion. */
    memmove(s->step+at+1,s->step+at,(size_t)(s->length-at)*sizeof(*step));s->step[at]=added;
    if(p->running) {
        if(p->step>=at)++p->step;
        if(p->queued>=at)++p->queued;
        /* Appending after a removed final visit provides a new continuation. */
        if(p->step<0 && p->continuation>=at && at<s->length)++p->continuation;
    }
    if(at<=s->first)++s->first;
    if(at<=s->last+1)++s->last;
    ++s->length;gallery_sequence_refresh(g);return 1;
}
int ts_gallery_sequence_remove(TsMosaicGallery *g,int at)
{
    if(!g || g->sequence.length<=1 || at<0 || at>=g->sequence.length)return 0;
    TsGallerySequence *s=&g->sequence;TsGallerySequencePlayer *p=&g->player;
    int first=s->first,last=s->last;
    if(p->running) {
        if(p->step==at) {
            p->step=-1;
            p->continuation=s->loop && at==last?first:at+1;
        } else if(p->step>at)--p->step;
        if(p->queued==at)p->queued=-1;
        else if(p->queued>at)--p->queued;
    }
    memmove(s->step+at,s->step+at+1,(size_t)(s->length-at-1)*sizeof(s->step[0]));--s->length;
    if(at<s->first)--s->first;
    if(at<=s->last)--s->last;
    if(s->first>=s->length)s->first=s->length-1;
    if(s->last<s->first)s->last=s->first;
    if(p->running && p->step<0) {
        if(p->continuation==at && s->loop && at==last)p->continuation=s->first;
        else if(p->continuation>at)--p->continuation;
    }
    gallery_sequence_refresh(g);return 1;
}
static int gallery_sequence_moved_index(int index,int from,int to)
{
    if(index==from)return to;
    if(from<to && index>from && index<=to)return index-1;
    if(to<from && index>=to && index<from)return index+1;
    return index;
}
int ts_gallery_sequence_move(TsMosaicGallery *g,int from,int to)
{
    if(!g || from<0 || to<0 || from>=g->sequence.length || to>=g->sequence.length || from==to)return 0;
    TsGallerySequence *s=&g->sequence;TsGallerySequencePlayer *p=&g->player;TsGalleryStep moved=s->step[from];
    if(from<to)memmove(s->step+from,s->step+from+1,(size_t)(to-from)*sizeof(moved));
    else memmove(s->step+to+1,s->step+to,(size_t)(from-to)*sizeof(moved));
    s->step[to]=moved;
    if(p->running) {
        p->step=gallery_sequence_moved_index(p->step,from,to);
        p->queued=gallery_sequence_moved_index(p->queued,from,to);
        if(p->step<0)p->continuation=gallery_sequence_moved_index(p->continuation,from,to);
    }
    int first=gallery_sequence_moved_index(s->first,from,to),last=gallery_sequence_moved_index(s->last,from,to);
    s->first=first<last?first:last;s->last=first>last?first:last;
    gallery_sequence_refresh(g);return 1;
}
static void gallery_sequence_enter(TsMosaicGallery *g,int step,int visit,int canvas,double start,double end,double fade)
{
    TsMosaic *old=ts_gallery_active(g),*to=g->canvas[canvas].scene;
    double old_end=g->player.end,old_start=old->time;
    double requested_fade=g->player.current.fade_seconds;
    int from=g->active;
    if(fade>0 && to==old)gallery_spare_capture(g,old);
    if(fade<=0) {old->playing=0;old->live_paused=1;old->section_transport=0;g->canvas[from].parked=1;}
    gallery_restart(to,start);to->section_transport=1;to->section_end=end;to->playing=1;
    to->speed_current=to->speed;to->marker_active=g->sequence.step[step].in_marker+1;
    g->canvas[canvas].parked=0;g->active=canvas;
    g->outgoing=fade>0?from:-1;g->fade_position=0;g->fade_length=fade;
    g->sequence_fade=fade>0;g->fade_curve=g->player.current.curve;
    g->player=(TsGallerySequencePlayer){.running=1,.step=step,.visit=visit,.queued=-1,
        .start=start,.end=end,.fade_start=old_start,.fade_end=old_end,.current=g->sequence.step[step]};
    gallery_diagnostic_begin(g,requested_fade);
    ts_mosaic_begin_block(to,g->block_frames?g->block_frames:1);
    gallery_sequence_refresh(g);
}
int ts_gallery_sequence_play(TsMosaicGallery *g,int step,char *e,size_t n)
{
    if(!g || !g->spare)return gallery_error(e,n,"PREPARE GALLERY SEQUENCER FIRST");
    if(!ts_gallery_sequence_valid(&g->sequence) || step<0 || step>=g->sequence.length)
        return gallery_error(e,n,"INVALID SEQUENCE SETTINGS");
    if(g->outgoing>=0)return gallery_error(e,n,"WAIT FOR THE CURRENT CROSSFADE");
    int canvas=0;double in=0,out=0;
    for(int i=0;i<g->sequence.length;++i)
        if(!ts_gallery_step_bounds(g,&g->sequence.step[i],NULL,NULL,NULL,e,n))return 0;
    ts_gallery_step_bounds(g,&g->sequence.step[step],&canvas,&in,&out,NULL,0);
    gallery_sequence_enter(g,step,1,canvas,in,out,0);return 1;
}
void ts_gallery_sequence_pause(TsMosaicGallery *g,int pause)
{
    if(!g || !g->player.running)return;
    g->player.paused=pause!=0;
    TsMosaic *m[]={ts_gallery_active(g),ts_gallery_outgoing(g)};
    for(int i=0;i<2;++i)if(m[i]){m[i]->playing=!pause;ts_mosaic_live_pause(m[i],pause);}
}
void ts_gallery_sequence_stop(TsMosaicGallery *g)
{
    if(!g)return;
    g->player.running=g->player.paused=0;g->player.queued=-1;
    gallery_park(g);
    TsMosaic *m=ts_gallery_active(g);
    if(m){m->playing=0;m->section_transport=0;ts_mosaic_live_pause(m,1);}
}
int ts_gallery_sequence_next(TsMosaicGallery *g,int selected,char *e,size_t n)
{
    if(!g || !g->player.running)return gallery_error(e,n,"PLAY THE SEQUENCE FIRST");
    if(g->outgoing>=0)return gallery_error(e,n,"WAIT FOR THE CURRENT CROSSFADE");
    int visit=1,next=selected>=0?selected:gallery_sequence_following(g,&visit);
    if(next<0){ts_gallery_sequence_stop(g);return 1;}
    if(next>=g->sequence.length)return gallery_error(e,n,"STEP IS OUTSIDE THE SEQUENCE");
    int canvas;double in,out;
    if(!ts_gallery_step_bounds(g,&g->sequence.step[next],&canvas,&in,&out,e,n))return 0;
    double fade=gallery_sequence_fade(g,next);
    TsMosaic *old=ts_gallery_active(g);
    fade=fmin(fade,fmax(0,(g->player.end-old->time)/fmax(.5,old->speed_current)));
    /* NEXT/LAUNCH can be early: its fade ends after the requested duration. */
    g->player.end=old->time+fade*fmax(.5,old->speed_current);
    gallery_sequence_enter(g,next,visit,canvas,in,out,fade);return 1;
}
void ts_gallery_sequence_tick(TsMosaicGallery *g,int rate)
{
    if(!g || !g->player.running || rate<=0)return;
    TsMosaic *m=ts_gallery_active(g);
    if(!m->playing){g->player.paused=1;return;}
    g->player.paused=0;
    if(g->outgoing>=0)return;
    int next=g->player.next;
    double fade=g->player.next_fade;
    double remaining=(g->player.end-m->time)/fmax(.5,m->speed_current);
    if(remaining>fade+1e-10)return;
    if(next<0){m->time=g->player.end;ts_gallery_sequence_stop(g);return;}
    char error[128];
    if(!ts_gallery_sequence_next(g,-1,error,sizeof(error))) {
        ts_gallery_sequence_stop(g);snprintf(g->player.error,sizeof(g->player.error),"%s",error);
    }
}
unsigned ts_gallery_sequence_span(const TsMosaicGallery *g,int rate,unsigned frames)
{
    if(!g || !g->player.running || g->player.paused || rate<=0)return frames;
    const TsMosaic *m=ts_gallery_active(g);double seconds;
    if(g->sequence_fade && g->outgoing>=0) {
        m=ts_gallery_outgoing(g);seconds=(g->player.fade_end-m->time)/fmax(.5,m->speed_current);
    } else {
        seconds=(g->player.end-m->time)/fmax(.5,fmax(m->speed,m->speed_current))-g->player.next_fade;
    }
    /* Keep the final boundary sample chronological; speed smoothing can move
       the estimate slightly, so leave one sample of margin. */
    double safe=floor(seconds*rate)-1;
    return safe<1?1:safe<frames?(unsigned)safe:frames;
}
