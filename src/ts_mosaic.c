#include "tapesister/mosaic.h"
#include "tapesister/master_eq.h"
#include <errno.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ts_mosaic_pitch.inc"

TsMosaic *ts_mosaic_create(void)
{
    static uint64_t epoch=0;
    TsMosaic *m=calloc(1,sizeof(*m));
    if(m) {m->pattern_engine=ts_pattern_engine_create();m->next_id=1;m->gain=0.7f;m->speed=m->speed_current=1;m->epoch=++epoch;m->volume_current=m->live_envelope_gain=1;m->live_trigger_floor=-1e-9;m->live_capture.length=4;m->live_pause_gain=m->live_pause_base=1;
        for(int i=0;i<TS_MOSAIC_ENVELOPE_POINTS;++i)m->volume[i]=1;}
    return m;
}
void ts_mosaic_free(TsMosaic *m)
{
    if(!m)return;
    while(m->sources) {TsMosaicSource *s=m->sources;m->sources=s->next;ts_sample_free(&s->sample);ts_sample_free(&s->eq_dry);ts_pattern_free(s->pattern);free(s);}
    ts_pattern_engine_free(m->pattern_engine);free(m);
}
TsMosaicEvent *ts_mosaic_find(TsMosaic *m,uint64_t id)
{
    if(m && id)for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(m->events[i].id==id)return &m->events[i];
    return NULL;
}
static void mosaic_reset_voices(TsMosaic *m)
{
    /* voice_begin resets the complete state of a card when it next sounds.
       On short loop wraps, avoid clearing every inactive card's large routing
       matrices. Retired state owns no source pointers or allocated memory. */
    for(int i=0;i<TS_MOSAIC_EVENTS;++i) {
        if(m->events[i].id && m->events[i].live && m->live[i].id==m->events[i].id)continue;
        TsMosaicVoice *v=&m->voices[i];v->active=0;v->attack=0;
        v->output_route.activity_source=v->output_route.activity_send=0;
    }
}
void ts_mosaic_publish_loaded(TsMosaic *m,TsMosaic *loaded)
{
    TsMosaicSource *retired=m->sources;
    void *retired_engine=m->pattern_engine;m->pattern_engine=loaded->pattern_engine;loaded->pattern_engine=retired_engine;
    /* A load has no history or live voices. Keep the unreachable history
       backing store, avoiding 17 MiB of writes/page faults in the audio lock.
       Copy every other field; voice_begin initializes retired slots on demand. */
    memcpy(m->events,loaded->events,sizeof(m->events));
    size_t tail=offsetof(TsMosaic,history_speed);
    memcpy((unsigned char *)m+tail,(const unsigned char *)loaded+tail,sizeof(*m)-tail);
    mosaic_reset_voices(m);
    loaded->sources=retired;
}
TsMosaicSource *ts_mosaic_source_eq(TsMosaic *m,const TsSample *sample,const TsSample *dry,const TsMasterEqControls *eq,char *error,size_t size)
{
    if(!m || !sample || !sample->data || sample->frames<2 || !sample->sample_rate)return NULL;
    if(dry && dry->data && (!eq || dry->frames!=sample->frames || dry->channels!=sample->channels || dry->sample_rate!=sample->sample_rate)) {
        if(error && size)snprintf(error,size,"Editable EQ audio must match the rendered source");return NULL;
    }
    uint64_t hash=ts_sample_hash(sample);
    for(TsMosaicSource *s=m->sources;s;s=s->next)
        if(!s->pattern && (!dry || !dry->data)==(!s->eq_dry.data) &&
           (!dry || !dry->data || (!memcmp(eq,&s->eq,sizeof(*eq)) && dry->frames==s->eq_dry.frames && dry->channels==s->eq_dry.channels &&
             !memcmp(dry->data,s->eq_dry.data,dry->frames*dry->channels*sizeof(float)))) &&
           s->hash==hash && s->sample.frames==sample->frames &&
           s->sample.sample_rate==sample->sample_rate && s->sample.channels==sample->channels &&
           !memcmp(s->sample.data,sample->data,sample->frames*sample->channels*sizeof(float)))return s;
    TsMosaicSource *s=calloc(1,sizeof(*s));
    if(!s){if(error && size)snprintf(error,size,"Not enough memory for event audio");return NULL;}
    if(!ts_sample_clone(&s->sample,sample,error,size)) {free(s);return NULL;}
    ts_master_eq_default(&s->eq);
    if(dry && dry->data){if(!ts_sample_clone(&s->eq_dry,dry,error,size)){ts_sample_free(&s->sample);free(s);return NULL;}s->eq=*eq;}
    s->hash=hash;s->eq_dry_hash=ts_sample_hash(&s->eq_dry);
    /* Built once per immutable source, never on the audio thread. Peak and
       RMS preserve transients and reveal the body of the waveform separately. */
    for(size_t bin=0;bin<TS_MOSAIC_PEAKS;++bin) {
        size_t first=bin*sample->frames/TS_MOSAIC_PEAKS;
        size_t last=(bin+1)*sample->frames/TS_MOSAIC_PEAKS;
        if(last<=first)last=first+1;
        double energy=0;
        for(size_t i=first;i<last;++i) {
            TsStereoFrame f=ts_sample_read_frame(sample,i);
            float peak=fmaxf(fabsf(f.l),fabsf(f.r));
            if(peak>s->peaks[bin])s->peaks[bin]=peak;
            energy+=.5*((double)f.l*f.l+(double)f.r*f.r);
        }
        s->rms[bin]=(float)sqrt(energy/(last-first));
    }
    s->next=m->sources;m->sources=s;return s;
}
TsMosaicSource *ts_mosaic_source(TsMosaic *m,const TsSample *sample,char *error,size_t size)
{return ts_mosaic_source_eq(m,sample,NULL,NULL,error,size);}
TsMosaicSource *ts_mosaic_source_document(TsMosaic *m,const TsInstrument *doc,char *error,size_t size)
{
    if(!doc)return NULL;
    TsMasterEqControls flat,eq=doc->tile_eq;ts_master_eq_default(&flat);
    if(!eq.band[0].frequency)eq=flat;
    if(!memcmp(&flat,&eq,sizeof(flat)))return ts_mosaic_source(m,&doc->current,error,size);
    TsSample dry={0};
    if(!ts_instrument_render_tile_eq(&dry,doc,&flat,error,size)){ts_sample_free(&dry);return NULL;}
    TsMosaicSource *s=ts_mosaic_source_eq(m,&doc->current,&dry,&eq,error,size);ts_sample_free(&dry);return s;
}
int ts_mosaic_import_source(TsInstrument *doc,const TsMosaicSource *s,int loop,size_t first,size_t last,TsLoopMode mode,char *error,size_t size)
{
    if(!ts_instrument_import_sample(doc,s->eq_dry.data?&s->eq_dry:&s->sample,loop,first,last,mode,error,size))return 0;
    if(s->eq_dry.data && !ts_instrument_set_tile_eq(doc,&s->eq,error,size))return 0;
    return 1;
}
void ts_mosaic_collect(TsMosaic *m)
{
    if(!m)return;
    /* Mark references once, instead of rescanning the entire arrangement and
       undo history separately for every immutable source. */
    for(TsMosaicSource *s=m->sources;s;s=s->next)s->retained=s->pins!=0;
    for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(m->pattern_retired[i])m->pattern_retired[i]->retained=1;
    /* The UI owns this reference until it acknowledges an ended preview.
       The callback only retires audition_id, never a pointer collect reads. */
    if(m->audition_source.source)m->audition_source.source->retained=1;
    for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(m->events[i].id)m->events[i].source->retained=1;
    for(int h=0;h<m->history_count;++h)for(int i=0;i<TS_MOSAIC_EVENTS;++i) {
        const TsMosaicEvent *e=&m->history[(m->history_first+h)%TS_MOSAIC_HISTORY][i];
        if(e->id)e->source->retained=1;
    }
    for(TsMosaicSource *s=m->sources;s;s=s->next)if(s->retained && s->pattern)
        for(int i=0;i<s->pattern->asset_count;++i)s->pattern->assets[i].audio->retained=1;
    TsMosaicSource **p=&m->sources;
    while(*p) {
        TsMosaicSource *s=*p;
        if(s->retained)p=&s->next;else {*p=s->next;ts_sample_free(&s->sample);ts_sample_free(&s->eq_dry);ts_pattern_free(s->pattern);free(s);}
    }
}
void ts_mosaic_checkpoint(TsMosaic *m)
{
    if(!m)return;
    if(m->history_cursor<m->history_count)m->history_count=m->history_cursor;
    if(m->history_count==TS_MOSAIC_HISTORY) {
        m->history_first=(m->history_first+1)%TS_MOSAIC_HISTORY;
        --m->history_count;
    }
    int at=(m->history_first+m->history_count++)%TS_MOSAIC_HISTORY;
    m->history_loop_range[at]=m->loop_range;
    m->history_auto_space[at]=m->auto_space;
    m->history_speed[at]=m->speed;
    memcpy(m->history_markers[at],m->markers,sizeof(m->markers));
    m->history_repeat[at]=m->repeat;
    memcpy(m->history_volume[at],m->volume,sizeof(m->volume));
    memcpy(m->history[at],m->events,sizeof(m->events));
    m->history_cursor=m->history_count;++m->revision;
}
int ts_mosaic_undo(TsMosaic *m,int redo)
{
    if(!m)return 0;
    int at=redo?m->history_cursor:m->history_cursor-1;
    if(at<0 || at>=m->history_count)return 0;
    at=(m->history_first+at)%TS_MOSAIC_HISTORY;
    for(int i=0;i<TS_MOSAIC_EVENTS;++i) {
        TsMosaicEvent e=m->events[i];m->events[i]=m->history[at][i];m->history[at][i]=e;
    }
    double speed=m->speed;ts_mosaic_set_speed(m,m->history_speed[at]);m->history_speed[at]=speed;
    for(int i=0;i<TS_MOSAIC_MARKERS;++i) {
        TsMosaicMarker marker=m->markers[i];m->markers[i]=m->history_markers[at][i];m->history_markers[at][i]=marker;
    }
    for(int i=0;i<TS_MOSAIC_ENVELOPE_POINTS;++i) {
        float value=m->volume[i];m->volume[i]=m->history_volume[at][i];m->history_volume[at][i]=value;
    }
    TsMosaicRange range=m->loop_range;m->loop_range=m->history_loop_range[at];m->history_loop_range[at]=range;
    int spacing=m->auto_space;m->auto_space=m->history_auto_space[at];m->history_auto_space[at]=spacing;
    int repeat=m->repeat;m->repeat=m->history_repeat[at];m->history_repeat[at]=repeat;
    for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(!m->events[i].live || m->events[i].id!=m->live[i].id)m->live[i]=(TsMosaicLive){0};
    m->history_cursor+=redo?1:-1;m->route_discontinuity=1;mosaic_reset_voices(m);++m->revision;return 1;
}
/* Called by control-thread mutations under the same exclusion as events.
   Reserve the full chord size per pattern card and one audition chord. Undo
   retains this high-water capacity; the audio callback never allocates. */
int ts_mosaic_reserve_patterns(TsMosaic *m,int extra)
{
    if(!m || extra<0 || extra>TS_MOSAIC_EVENTS)return 0;
    unsigned count=1+extra;
    for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(m->events[i].id && m->events[i].source->pattern)++count;
    return ts_pattern_engine_reserve(m->pattern_engine,count*TS_MOSAIC_NOTES);
}
static int overlap(const TsMosaicEvent *a,const TsMosaicEvent *b)
{return a->start<b->start+b->duration-1e-9 && b->start<a->start+a->duration-1e-9;}
void ts_mosaic_space(TsMosaic *m,TsMosaicEvent *e)
{
    if(!m || !e)return;
    e->x=fmax(0,e->x);
    if(!m->auto_space)return;
    /* Only horizontal displacement: event timing and other tiles are untouched. */
    for(int pass=0;pass<TS_MOSAIC_EVENTS;++pass) {
        int changed=0;
        for(int i=0;i<TS_MOSAIC_EVENTS;++i) {
            TsMosaicEvent *b=&m->events[i];
            if(!b->id || b==e || !overlap(e,b))continue;
            if(e->x<b->x+b->width+TS_MOSAIC_GUTTER && b->x<e->x+e->width+TS_MOSAIC_GUTTER) {
                e->x=b->x+b->width+TS_MOSAIC_GUTTER;changed=1;
            }
        }
        if(!changed)break;
    }
}
static TsMosaicEvent *mosaic_add(TsMosaic *m,TsMosaicSource *source,double start,double x,int space)
{
    if(!m || !source || !isfinite(start) || !isfinite(x))return NULL;
    if(source->pattern && !ts_mosaic_reserve_patterns(m,1))return NULL;
    for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(!m->events[i].id) {
        TsMosaicEvent *e=&m->events[i];
        *e=(TsMosaicEvent){.id=m->next_id++,.revision=1,.source=source,
            .start=fmax(0,start),.duration=source->pattern?source->pattern->duration:(double)source->sample.frames/source->sample.sample_rate,
            .x=x,.width=86,.last=source->sample.frames,.looping=1,.note_count=1,
            .notes={60},.gain=1,.tuning={60,0}};
        snprintf(e->name,sizeof(e->name),"%.63s",source->sample.name);
        if(space)ts_mosaic_space(m,e);
        ++m->revision;return e;
    }
    return NULL;
}
TsMosaicEvent *ts_mosaic_add(TsMosaic *m,TsMosaicSource *source,double start,double x)
{return mosaic_add(m,source,start,x,1);}
TsMosaicEvent *ts_mosaic_copy_unspaced(TsMosaic *m,uint64_t id,double start,double x)
{
    TsMosaicEvent *old=ts_mosaic_find(m,id);if(!old)return NULL;
    TsMosaicEvent copy=*old,*e=mosaic_add(m,old->source,start,x,0);if(!e)return NULL;
    uint64_t new_id=e->id;*e=copy;e->id=new_id;e->start=fmax(0,start);e->x=fmax(0,x);
    return e;
}
TsMosaicEvent *ts_mosaic_copy(TsMosaic *m,uint64_t id,double start,double x)
{TsMosaicEvent *e=ts_mosaic_copy_unspaced(m,id,start,x);if(e)ts_mosaic_space(m,e);return e;}
void ts_mosaic_delete(TsMosaic *m,uint64_t id)
{TsMosaicEvent *e=ts_mosaic_find(m,id);if(e){m->live[e-m->events]=(TsMosaicLive){0};m->voices[e-m->events].active=0;memset(e,0,sizeof(*e));m->route_discontinuity=1;++m->revision;}}
double ts_mosaic_snap(const TsMosaic *m,uint64_t id,double time,double tolerance)
{
    double result=time,best=tolerance;
    if(fabs(time)<best){best=fabs(time);result=0;}
    for(int i=0;i<TS_MOSAIC_EVENTS;++i) {
        const TsMosaicEvent *e=&m->events[i];if(!e->id || e->id==id)continue;
        for(int k=0;k<3;++k){double t=e->start+e->duration*k*0.5,d=fabs(t-time);if(d<best){best=d;result=t;}}
    }
    return result;
}
double ts_mosaic_end(const TsMosaic *m)
{
    double end=0;if(m)for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(m->events[i].id)
        end=fmax(end,m->events[i].start+m->events[i].duration);
    return end;
}
void ts_mosaic_seek(TsMosaic *m,double time)
{
    if(m){m->marker_active=0;m->schedule.valid=0;m->route_discontinuity=1;m->time=isfinite(time)?fmax(0,time):0;m->live_trigger_floor=m->time-1e-9;
        ts_mosaic_live_transport(m,2);mosaic_reset_voices(m);
        m->transition_from=m->last_output;m->transition_total=m->transition_remaining=(unsigned)(m->rate>0?m->rate/500:0);
        double end=ts_mosaic_end(m);m->volume_current=ts_mosaic_volume_at(m,end>0?m->time/end:0);}
}
static TsMosaicRange mosaic_marker_range_at(const TsMosaic *m,int index,double end)
{
    if(!m || index<0 || index>=TS_MOSAIC_MARKERS || !m->markers[index].stored)return (TsMosaicRange){0,0};
    double start=m->markers[index].time,next=INFINITY;
    for(int i=0;i<TS_MOSAIC_MARKERS;++i)if(m->markers[i].stored && m->markers[i].time>start)
        next=fmin(next,m->markers[i].time);
    if(!isfinite(next))next=end;
    return next>start?(TsMosaicRange){start,next}:(TsMosaicRange){0,0};
}
TsMosaicRange ts_mosaic_marker_range(const TsMosaic *m,int index)
{return mosaic_marker_range_at(m,index,ts_mosaic_end(m));}
int ts_mosaic_trigger_marker(TsMosaic *m,int index)
{
    if(!m || index<0 || index>=TS_MOSAIC_MARKERS || !m->markers[index].stored)return 0;
    ts_mosaic_seek(m,m->markers[index].time);m->marker_active=index+1;return 1;
}
void ts_mosaic_set_marker_loop(TsMosaic *m,int enabled)
{
    if(!m)return;
    /* Switching off never seeks, resets voices, changes the envelope or restores
       an old range. The current phrase simply runs through its next boundary. */
    m->marker_loop=enabled!=0;m->repeat=0;
    if(enabled) {
        double latest=-1;m->marker_active=0;
        for(int i=0;i<TS_MOSAIC_MARKERS;++i)if(m->markers[i].stored &&
            m->markers[i].time<=m->time && m->markers[i].time>latest) {
            latest=m->markers[i].time;m->marker_active=i+1;
        }
    }
    m->schedule.valid=0;
}
void ts_mosaic_set_speed(TsMosaic *m,double speed)
{
    if(!m || !isfinite(speed))return;
    m->speed=fmax(.5,fmin(2,speed));
    if(!m->playing)m->speed_current=m->speed;
    ++m->revision;
}
static void event_balance(const TsMosaicEvent *e,float *left,float *right)
{
    /* Stereo balance retains the original channels at center, with no boost
       or collapse to mono. Mono sources naturally feed the same signal to both. */
    *left=e->gain*(e->pan>0?sqrtf(fmaxf(0,1-e->pan)):1);
    *right=e->gain*(e->pan<0?sqrtf(fmaxf(0,1+e->pan)):1);
}
void ts_mosaic_event_fades(const TsMosaicEvent *e,double *in,double *out)
{
    *in=fmax(0,e->fade_in);*out=e->live && !e->live_finite?0:fmax(0,e->fade_out);
    if(*in+*out>e->duration) {
        double scale=e->duration/(*in+*out);*in*=scale;*out*=scale;
    }
}
static float event_fade(const TsMosaicEvent *e,const TsMosaicVoice *v,double elapsed)
{
    double in=v->fade_in,out=v->fade_out;
    if(in==0 && out==0)return 1;
    double gain=1;
    if(in>0)gain=fmin(gain,elapsed/in);
    if(out>0)gain=fmin(gain,(e->duration-elapsed)/out);
    return (float)fmax(0,fmin(1,gain));
}
/* Derive loop phase from distance travelled. Reverse uses a full sample
   cycle (the legacy helper drops a frame at each reverse wrap). This also
   makes seeks into long ping-pong/START events exact without replaying them. */
static double voice_position(const TsMosaicEvent *e,const TsMosaicVoice *v,double distance,int *direction,int *intro)
{
    TsLoopMode mode=v->base_mode;
    double first=(double)e->first,last=(double)e->last;
    *intro=0;*direction=1;
    if(!e->looping)return first+distance;
    int start=v->starts_at_sample;
    if(start && distance<first){*intro=1;return distance;}
    if(mode==TS_LOOP_FORWARD) {
        double p=start?distance:first+distance;
        if(e->last>e->first+1 && p>=first && p<last)return p;
        return ts_audition_wrap_position(p,e->first,e->last,e->crossfade);
    }
    if(mode==TS_LOOP_REVERSE) {
        if(start && distance<=last-1){*intro=1;return distance;}
        *direction=-1;
        double p=start?2*(last-1)-distance:last-1-distance;
        double cycle=last-first-e->crossfade;
        return p>=first?p:first+fmod(fmod(p-first,cycle)+cycle,cycle);
    }
    double span=last-first-1;
    if(span<=0)return first;
    double phase=fmod(start?distance-first:distance,2*span);
    *direction=phase<span?1:-1;
    return first+(phase<span?phase:2*span-phase);
}
void ts_mosaic_pattern_retire(TsMosaic *m)
{
    for(int i=0;i<TS_MOSAIC_EVENTS;++i){
        TsMosaicVoice *v=&m->voices[i];const TsMosaicEvent *e=&m->events[i];
        if(!m->playing && m->pattern_retired[i])v->active=0;
        if(!m->playing || !v->active || v->id!=e->id || v->revision!=e->revision || v->pattern_source==e->source)
            m->pattern_retired[i]=NULL;
    }
}
void ts_mosaic_update_pattern(TsMosaic *m,TsMosaicEvent *e,TsMosaicSource *source)
{
    if(!e || !e->id || !source || !source->pattern || !e->source->pattern || e->source==source)return;
    if(m->audition_id==e->id && !m->audition_source.source){m->audition_source=*e;}
    int i=(int)(e-m->events);TsMosaicVoice *v=&m->voices[i];
    if(m->playing && v->active && v->id==e->id && v->revision==e->revision && v->pattern_source){
        m->pattern_retired[i]=v->pattern_source;
        double cycle=v->pattern_source->pattern->duration;
        v->pattern_switch=v->pattern_origin+(floor(fmax(0,m->time-e->start-v->pattern_origin)/cycle)+1)*cycle;
    }else{v->active=0;m->pattern_retired[i]=NULL;}
    e->source=source;++m->revision;
}
static void voice_begin(TsMosaicVoice *v,const TsMosaicEvent *e,double elapsed,int rate)
{
    static uint64_t generation;
    memset(v,0,sizeof(*v));v->generation=++generation;v->active=1;v->id=e->id;v->revision=e->revision;v->start=e->start;
    v->pattern_source=e->source->pattern?e->source:NULL;
    v->attack_frames=ts_audition_attack_frames(rate,2);v->release_frames=rate*.002;
    v->audible_ramp=1.0f/(rate*.005f);v->mix_ramp=2.0f/(rate*.005f);
    event_balance(e,&v->mix_left,&v->mix_right);
    for(int n=0;n<e->note_count;++n) {
        double pitch=ts_tuning_note_pitch(&e->tuning,e->notes[n]-60);
        v->travel[n]=elapsed*e->source->sample.sample_rate*pitch;
        v->step[n]=(double)e->source->sample.sample_rate/rate*pitch;
    }
}
float ts_mosaic_volume_at(const TsMosaic *m,double position)
{
    if(!m || !isfinite(position))return 1;
    double point=fmax(0,fmin(1,position))*(TS_MOSAIC_ENVELOPE_POINTS-1);
    int a=(int)point,b=a<TS_MOSAIC_ENVELOPE_POINTS-1?a+1:a;
    float last=m->repeat && b==TS_MOSAIC_ENVELOPE_POINTS-1?m->volume[0]:m->volume[b];
    float first=m->repeat && a==TS_MOSAIC_ENVELOPE_POINTS-1?m->volume[0]:m->volume[a];
    return first+(last-first)*(float)(point-a);
}
int ts_mosaic_set_loop_range(TsMosaic *m,double start,double end)
{
    if(!m || !isfinite(start) || !isfinite(end) || start<0 || end>1000000 ||
       !((start==0 && end==0) || end>=start+.001))return 0;
    m->marker_loop=0;m->loop_range=(TsMosaicRange){start,end};++m->revision;return 1;
}
void ts_mosaic_set_repeat(TsMosaic *m,int repeat)
{
    if(!m)return;
    m->marker_loop=0;m->repeat=repeat!=0;
    if(m->repeat)m->volume[TS_MOSAIC_ENVELOPE_POINTS-1]=m->volume[0];
    ++m->revision;
}
void ts_mosaic_volume_draw(TsMosaic *m,double from,float a,double to,float b)
{
    if(!m || !isfinite(from) || !isfinite(to) || !isfinite(a) || !isfinite(b))return;
    int first=(int)llround(fmax(0,fmin(1,from))*(TS_MOSAIC_ENVELOPE_POINTS-1));
    int last=(int)llround(fmax(0,fmin(1,to))*(TS_MOSAIC_ENVELOPE_POINTS-1));
    a=fmaxf(0,fminf(1,a));b=fmaxf(0,fminf(1,b));
    int direction=last>=first?1:-1;
    for(int i=first;;i+=direction) {
        m->volume[i]=first==last?b:a+(b-a)*(float)(i-first)/(last-first);
        if(i==last)break;
    }
    if(m->repeat) {
        if(last==TS_MOSAIC_ENVELOPE_POINTS-1)m->volume[0]=m->volume[last];
        m->volume[TS_MOSAIC_ENVELOPE_POINTS-1]=m->volume[0];
    }
    ++m->revision;
}
void ts_mosaic_volume_action(TsMosaic *m,TsMosaicEnvelopeAction action)
{
    if(!m)return;
    int last=TS_MOSAIC_ENVELOPE_POINTS-1;
    if(action==TS_MOSAIC_ENV_RESET)for(int i=0;i<=last;++i)m->volume[i]=1;
    else if(action==TS_MOSAIC_ENV_MATCH_START)m->volume[0]=m->volume[last];
    else if(action==TS_MOSAIC_ENV_MATCH_END)m->volume[last]=m->volume[0];
    else if(action==TS_MOSAIC_ENV_RAMP)
        ts_mosaic_volume_draw(m,0,m->volume[0],1,m->volume[last]);
    else if(action==TS_MOSAIC_ENV_SMOOTH) {
        float old[TS_MOSAIC_ENVELOPE_POINTS];memcpy(old,m->volume,sizeof(old));
        for(int i=1;i<last;++i)m->volume[i]=(old[i-1]+2*old[i]+old[i+1])*.25f;
    }
    if(m->repeat)m->volume[last]=m->volume[0];
    ++m->revision;
}

/* Most reads are inside a sample, outside either loop splice. Keep that path
   local so each note does not repeat the generic reader's validation and
   function calls. Boundaries, crossfades and unusual data retain the shared
   reader. Sanitize each endpoint before interpolation, exactly as it does. */
static TsStereoFrame mosaic_sample_read(const TsMosaicEvent *e,const TsMosaicVoice *v,int n)
{
    const TsSample *s=&e->source->sample;double position=v->position[n];
    int plain=!e->looping || v->intro[n];
    int interior=plain || (v->base_mode==TS_LOOP_FORWARD?position<(double)(e->last-e->crossfade):
        v->base_mode==TS_LOOP_REVERSE?position>=(double)(e->first+e->crossfade):1);
    if(interior && s->data && (s->channels==1 || s->channels==2) && e->last<=s->frames && e->last>1 &&
       position>=(plain?0:(double)e->first) && position<(double)(e->last-1)) {
        size_t at=(size_t)position,offset=at*s->channels;
        float fraction=(float)(position-(double)at);
        float a=s->data[offset],b=s->data[offset+s->channels];
        a=isfinite(a)?a:0;b=isfinite(b)?b:0;
        TsStereoFrame f;f.l=a+(b-a)*fraction;
        if(s->channels==2) {
            a=s->data[offset+1];b=s->data[offset+3];a=isfinite(a)?a:0;b=isfinite(b)?b:0;
            f.r=a+(b-a)*fraction;
        } else f.r=f.l;
        f.l=isfinite(f.l)?f.l:0;f.r=isfinite(f.r)?f.r:0;return f;
    }
    return plain?ts_audition_read_frame(s,position,e->last):
        ts_audition_read_looped_mode_frame(s,position,e->first,e->last,e->crossfade,e->mode);
}

static TsStereoFrame mosaic_event_read(void *pattern_engine,const TsMosaicEvent *e,TsMosaicVoice *v,double time,
    int rate,double speed,float level,int audible,TsSourceRouteMix *routes,uint64_t serial)
{
    if(!v->active || v->id!=e->id || v->revision!=e->revision || v->start!=e->start) {
        voice_begin(v,e,fmax(0,time-e->start),rate);
        v->audible_gain=audible?1.0f:0.0f;
    }
    if(e->source->pattern && v->pattern_source!=e->source && e->looping && time-e->start+1e-9>=v->pattern_switch){
        v->pattern_source=e->source;v->pattern_origin=v->pattern_switch;
    }
    if(v->control_serial!=serial) {
        event_balance(e,&v->target_left,&v->target_right);
        v->base_mode=ts_loop_base_mode(e->mode);v->starts_at_sample=ts_loop_starts_at_sample(e->mode);
        /* Fades are stable within this buffer; editing them retains voice phase. */
        ts_mosaic_event_fades(e,&v->fade_in,&v->fade_out);
        if(routes && (!v->output_route.ready || v->output_route.rate!=(unsigned)rate ||
           memcmp(&v->output_route.route,&e->output_route,sizeof(e->output_route))))
            ts_source_route_set(&v->output_route,e->output_route,(unsigned)rate);
        const TsSourceRouteVoice *r=&v->output_route;
        v->route_passthrough=r->ready && !r->remaining && !r->active_count && r->main==1 &&
            r->main_matrix[0]==1 && r->main_matrix[1]==0 && r->main_matrix[2]==0 && r->main_matrix[3]==1;
        v->control_serial=serial;
    }
    float envelope=v->attack>=v->attack_frames?1:ts_audition_attack_gain(v->attack,v->attack_frames);
    ++v->attack;
    double remaining=e->live && !e->live_finite?INFINITY:e->start+e->duration-time;
    /* The release is 2 ms. Only calculate its division close to the end;
       the conservative 3 ms gate retains the original boundary arithmetic. */
    if(remaining<.003*speed) {
        double tail=remaining*rate/speed;
        if(tail<v->release_frames)envelope*=(float)fmax(0,tail/v->release_frames);
    }
    float target=audible?1.0f:0.0f;
    if(v->audible_gain!=target)
        v->audible_gain=target>v->audible_gain?fminf(target,v->audible_gain+v->audible_ramp):fmaxf(target,v->audible_gain-v->audible_ramp);
    if(v->mix_left!=v->target_left)v->mix_left+=fmaxf(-v->mix_ramp,fminf(v->mix_ramp,v->target_left-v->mix_left));
    if(v->mix_right!=v->target_right)v->mix_right+=fmaxf(-v->mix_ramp,fminf(v->mix_ramp,v->target_right-v->mix_right));
    float gain=level*envelope*v->audible_gain*event_fade(e,v,time-e->start);
    if(e->note_count!=1)gain/=e->note_count;
    TsStereoFrame card={0,0};
    for(int n=0;n<e->note_count;++n) {
        if(e->source->pattern){
            TsStereoFrame f=ts_pattern_read_cached(pattern_engine,v->pattern_source->pattern,v,v->generation,fmax(0,time-e->start-v->pattern_origin),rate,speed,e->notes[n],e->looping,&v->pattern_player[n]);
            card.l+=f.l*gain*v->mix_left;card.r+=f.r*gain*v->mix_right;continue;
        }
        TsStereoFrame f;
        v->position[n]=voice_position(e,v,v->travel[n],&v->direction[n],&v->intro[n]);
        if(!e->looping) {
            if(v->position[n]>=e->last)continue;
            f=mosaic_sample_read(e,v,n);
            float release=(float)fmin(1,(e->last-v->position[n])/(v->step[n]*speed*rate*0.002));
            f.l*=release;f.r*=release;
        } else f=mosaic_sample_read(e,v,n);
        if(e->live) {
            /* Preserve the exact loop period. Each pitched voice gets its own
               short edge fade instead of an overlapping splice that shortens it. */
            double splice=fmin((e->last-e->first)*.25,e->source->sample.sample_rate*.002);
            float edge=splice>0?(float)fmax(0,fmin(1,fmin((v->position[n]-e->first)/splice,(e->last-v->position[n])/splice))):1;
            f.l*=edge;f.r*=edge;
        }
        card.l+=f.l*gain*v->mix_left;card.r+=f.r*gain*v->mix_right;
        v->travel[n]+=v->step[n]*speed;
    }
    if(routes) {
        /* A settled identity route changes no bus or recording tap. Preserve
           its meters and signed-zero arithmetic; all transitions/custom routes
           (and nonfinite input) continue through the general routing code. */
        if(v->route_passthrough && isfinite(card.l) && isfinite(card.r)) {
            float left=fabsf(card.l),right=fabsf(card.r),peak=left>right?left:right;
            if(peak>v->output_route.activity_source)v->output_route.activity_source=peak;
            if(peak>v->output_route.activity_send)v->output_route.activity_send=peak;
            return (TsStereoFrame){card.l+card.r*0,card.l*0+card.r};
        }
        if(!v->output_route.ready)
            ts_source_route_set(&v->output_route,e->output_route,(unsigned)rate);
        card=ts_source_route_frame(&v->output_route,card,routes);
    }
    return card;
}

#include "ts_mosaic_sequence.inc"
#include "ts_mosaic_live.inc"
#include "ts_mosaic_schedule.inc"

static TsStereoFrame mosaic_read(TsMosaic *m,int rate,TsSourceRouteMix *routes)
{
    TsStereoFrame out={0,0};if(!m || rate<=0)return out;
    if(!m->playing) {
        if(m->was_playing){m->route_discontinuity=1;m->transition_from=m->last_output;m->transition_total=m->transition_remaining=(unsigned)(rate/500);m->was_playing=0;}
        if(m->transition_remaining && m->transition_total) {
            float gain=(float)--m->transition_remaining/m->transition_total;
            out.l=m->transition_from.l*gain;out.r=m->transition_from.r*gain;
        }
        m->last_output=out;return out;
    }
    int starting=!m->was_playing;
    if(starting){for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(!m->events[i].live)m->voices[i].attack=0;m->was_playing=1;m->speed_current=m->speed;m->schedule.valid=0;}
    if(m->rate!=rate){mosaic_reset_voices(m);m->rate=rate;}
    /* Smooth live changes over at most 20 ms. The same speed advances both
       arrangement time and every source phase, without retriggering voices. */
    double slew=1.5/(rate*.020),difference=m->speed-m->speed_current;
    m->speed_current+=fmax(-slew,fmin(slew,difference));
    double speed=m->speed_current;
    if(!m->schedule.valid || m->schedule.revision!=m->revision || m->time!=m->schedule.expected_time)mosaic_schedule_prepare(m);
    double end=m->schedule.end;int any_solo=m->schedule.any_solo;
    int cue_repeat=m->marker_loop && m->schedule.marker_range.end>m->schedule.marker_range.start;
    TsMosaicRange range=cue_repeat?m->schedule.marker_range:m->loop_range;
    double loop_start=range.start,loop_end=range.end;
    int range_repeat=cue_repeat || (!m->marker_loop && m->repeat && loop_end>loop_start);
    if(range_repeat && (m->time<loop_start || m->time>=loop_end)) {
        /* Preserve fractional overshoot so repeated passages never drift.
           seek reconstructs crossing cards at their elapsed source phase and
           smooths the Mosaic bus plus custom route handoffs. */
        int marker=m->marker_active;
        ts_mosaic_seek(m,starting || m->time<loop_start?loop_start:
            loop_start+fmod(m->time-loop_start,loop_end-loop_start));
        if(cue_repeat)m->marker_active=marker;
    } else if(!range_repeat && m->time>=end) {
        if(!m->marker_loop && m->repeat && end>0)ts_mosaic_seek(m,fmod(m->time,end));
        else {m->playing=0;m->route_discontinuity=1;return out;}
    }
    if(!m->schedule.valid)mosaic_schedule_prepare(m);
    mosaic_schedule_start(m);
    float target_volume=ts_mosaic_volume_at(m,end>0?m->time/end:0);
    if(starting)m->volume_current=target_volume;
    float volume_step=1.0f/(rate*.005f);
    m->volume_current+=fmaxf(-volume_step,fminf(volume_step,target_volume-m->volume_current));
    int retained=0;
    for(int at=0;at<m->schedule.active_count;++at) {
        int i=m->schedule.active[at];
        const TsMosaicEvent *e=&m->events[i];TsMosaicVoice *v=&m->voices[i];
        if(m->time>=e->start+e->duration) {v->active=0;continue;}
        m->schedule.active[retained++]=i;
        TsStereoFrame card=mosaic_event_read(m->pattern_engine,e,v,m->time,rate,speed,m->gain*m->volume_current,
            !e->muted && (!any_solo || e->solo),routes,m->control_serial);
        out.l+=card.l;out.r+=card.r;
    }
    m->schedule.active_count=retained;
    if(m->transition_remaining && m->transition_total) {
        float gain=(float)--m->transition_remaining/m->transition_total;
        out.l=out.l*(1-gain)+m->transition_from.l*gain;
        out.r=out.r*(1-gain)+m->transition_from.r*gain;
    }
    m->live_trigger_floor=m->time;m->time+=speed/rate;m->schedule.expected_time=m->time;m->last_output=ts_stereo_frame_sanitize(out);return m->last_output;
}
void ts_mosaic_audition(TsMosaic *m,uint64_t id)
{
    if(!m)return;
    if(!id || m->audition_id==id) {
        if(!m->audition_id)m->audition_source=(TsMosaicEvent){0};
        m->audition_stopping=1;return;
    }
    if(!ts_mosaic_find(m,id))return;
    m->audition_source=(TsMosaicEvent){0};
    m->audition_id=id;m->audition_time=0;m->audition_stopping=0;m->audition_rate=0;
    memset(&m->audition_voice,0,sizeof(m->audition_voice));
}
void ts_mosaic_audition_source(TsMosaic *m,const TsMosaicEvent *source)
{
    if(!m || !source || !source->source || source->duration<=0)return;
    if(source->source->pattern && !ts_mosaic_reserve_patterns(m,0))return;
    m->audition_source=*source;m->audition_source.id=UINT64_MAX;m->audition_source.start=0;
    m->audition_id=UINT64_MAX;m->audition_time=0;m->audition_stopping=0;m->audition_rate=0;
    memset(&m->audition_voice,0,sizeof(m->audition_voice));
}
static TsStereoFrame mosaic_audition_read(TsMosaic *m,int rate,TsSourceRouteMix *routes)
{
    TsStereoFrame out={0,0};if(!m || !m->audition_id || rate<=0)return out;
    TsMosaicEvent *e=m->audition_source.source?&m->audition_source:
        m->audition_slot>=0 && m->audition_slot<TS_MOSAIC_EVENTS?&m->events[m->audition_slot]:NULL;
    if(!e || e->id!=m->audition_id) {
        e=ts_mosaic_find(m,m->audition_id);m->audition_slot=e?(int)(e-m->events):-1;
    }
    if(!e || m->audition_time>=e->duration) {m->audition_id=0;m->route_discontinuity=1;return out;}
    if(m->audition_rate!=rate){memset(&m->audition_voice,0,sizeof(m->audition_voice));m->audition_rate=rate;}
    /* Audition bypasses arrangement mutes/solos, retaining the card's chord,
       loop, fades, gain, pan and output route. The timeline clock is untouched. */
    out=mosaic_event_read(m->pattern_engine,e,&m->audition_voice,e->start+m->audition_time,rate,m->speed,m->gain,
        !m->audition_stopping,routes,m->control_serial);
    m->audition_time+=m->speed/rate;
    if(m->audition_stopping && m->audition_voice.audible_gain<=0)m->audition_id=0;
    return out;
}
TsStereoFrame ts_mosaic_read_routed(TsMosaic *m,int rate,TsSourceRouteMix *routes)
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
    if(!m->live_pause_routes_valid && !m->live_paused && !m->playing && !m->sequence.running && !m->audition_id && !m->was_playing && !m->transition_remaining && !m->route_handoff.remaining && !m->schedule.live_count && m->schedule.valid)return (TsStereoFrame){0,0};
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
TsStereoFrame ts_mosaic_read(TsMosaic *m,int rate)
{return ts_mosaic_read_routed(m,rate,NULL);}
uint64_t ts_mosaic_hash(const TsMosaic *m)
{
    uint64_t hash=1469598103934665603ull;if(!m)return hash;
    /* Hash values, never pointers, padding or transport position. */
    for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(m->events[i].id) {
        const TsMosaicEvent *e=&m->events[i];char line[512];
        snprintf(line,sizeof(line),"%llu %llu %.17g %.17g %.17g %.17g %zu %zu %zu %d %d %d %.9g %d %.9g %d %d %d %d %d",
            (unsigned long long)e->id,(unsigned long long)e->source->hash,e->start,e->duration,e->x,e->width,
            e->first,e->last,e->crossfade,e->mode,e->looping,e->note_count,e->gain,e->tuning.root_note,
            e->tuning.fine_tune_cents,e->notes[0],e->notes[1],e->notes[2],e->notes[3],e->notes[4]);
        if(e->source->eq_dry.data) {
            hash^=e->source->eq_dry_hash;hash*=1099511628211ull;
            const TsMasterEqControls *eq=&e->source->eq;hash^=eq->enabled;hash*=1099511628211ull;
            for(int b=0;b<5;++b) {
                char value[100];TsEqBand v=eq->band[b];snprintf(value,sizeof(value),"%d %d %.9g %.9g %.9g",v.enabled,v.type,v.frequency,v.gain_db,v.q);
                for(const unsigned char *p=(const unsigned char *)value;*p;++p){hash^=*p;hash*=1099511628211ull;}
            }
        }
        if(e->live) {char live[128];snprintf(live,sizeof(live),"LIVE %d %d %d %d %.17g",e->live_finite,e->live_tempo,e->live_transport,e->live_position,e->live_beats);
            for(const unsigned char *p=(const unsigned char *)live;*p;++p){hash^=*p;hash*=1099511628211ull;}}
        if(e->live_order || e->finish_fade) {char step[96];snprintf(step,sizeof(step),"STEP %d %.17g",e->live_order,e->finish_fade);
            for(const unsigned char *p=(const unsigned char *)step;*p;++p){hash^=*p;hash*=1099511628211ull;}}
        const TsSourceRoute *r=&e->output_route;
        hash^=(uint64_t)(r->matrix_enabled+2*(r->pan+100)+402*(r->width+100));hash*=1099511628211ull;
        for(int d=0;d<TS_MATRIX_DESTINATIONS;++d){hash^=(uint64_t)r->matrix_gain[d];hash*=1099511628211ull;}
        char mix[128];snprintf(mix,sizeof(mix),"%.9g %.17g %.17g",e->pan,e->fade_in,e->fade_out);
        for(const unsigned char *p=(const unsigned char *)mix;*p;++p){hash^=*p;hash*=1099511628211ull;}
        hash^=(uint64_t)(e->muted | (e->solo<<1));hash*=1099511628211ull;
        for(const unsigned char *p=(const unsigned char *)line;*p;++p){hash^=*p;hash*=1099511628211ull;}
        for(const unsigned char *p=(const unsigned char *)e->name;*p;++p){hash^=*p;hash*=1099511628211ull;}
    }
    for(int i=0;i<TS_MOSAIC_ENVELOPE_POINTS;++i) {
        char value[32];snprintf(value,sizeof(value),"%.9g",m->volume[i]);
        for(const unsigned char *p=(const unsigned char *)value;*p;++p){hash^=*p;hash*=1099511628211ull;}
    }
    {char settings[96];snprintf(settings,sizeof(settings),"LOOPER %d %d %d %d %d",m->live_capture.source,m->live_capture.continuous,m->live_capture.units,m->live_capture.length,m->live_capture.quantize);
        for(const unsigned char *p=(const unsigned char *)settings;*p;++p){hash^=*p;hash*=1099511628211ull;}}
    if(m->live_envelope){hash^=UINT64_C(0x4c4f4f50454e56);hash*=1099511628211ull;}
    char speed[48];snprintf(speed,sizeof(speed),"%.17g",m->speed);
    for(const unsigned char *p=(const unsigned char *)speed;*p;++p){hash^=*p;hash*=1099511628211ull;}
    hash^=(uint64_t)m->auto_space;hash*=1099511628211ull;
    hash^=(uint64_t)m->repeat;hash*=1099511628211ull;
    for(int i=0;i<TS_MOSAIC_VIEWS;++i)if(m->views[i].stored) {
        const TsMosaicView *v=&m->views[i];char value[160];
        snprintf(value,sizeof(value),"VIEW %d %.17g %.17g %.17g %.17g",i,v->scroll,v->xscroll,v->scale,v->hscale);
        for(const unsigned char *p=(const unsigned char *)value;*p;++p){hash^=*p;hash*=1099511628211ull;}
    }
    for(int i=0;i<TS_MOSAIC_MARKERS;++i)if(m->markers[i].stored) {
        char value[96];snprintf(value,sizeof(value),"MARKER %d %.17g %s",i,m->markers[i].time,m->markers[i].name);
        for(const unsigned char *p=(const unsigned char *)value;*p;++p){hash^=*p;hash*=1099511628211ull;}
    }
    if(m->loop_range.end>m->loop_range.start) {
        char value[96];snprintf(value,sizeof(value),"LOOP %.17g %.17g",m->loop_range.start,m->loop_range.end);
        for(const unsigned char *p=(const unsigned char *)value;*p;++p){hash^=*p;hash*=1099511628211ull;}
    }
    return hash;
}

static int mosaic_layout_valid(double x)
{return isfinite(x) && x>=0 && x<=TS_MOSAIC_LAYOUT_LIMIT;}
static int mosaic_marker_valid(const TsMosaicMarker *v)
{
    if((v->stored!=0 && v->stored!=1) || !isfinite(v->time) || v->time<0 || v->time>1000000 ||
       !memchr(v->name,0,sizeof(v->name)))return 0;
    for(const unsigned char *p=(const unsigned char *)v->name;*p;++p)if(*p<32 || *p>126)return 0;
    return 1;
}
int ts_mosaic_set_marker(TsMosaic *m,int index,const TsMosaicMarker *marker)
{
    if(!m || index<0 || index>=TS_MOSAIC_MARKERS || !marker || !mosaic_marker_valid(marker))return 0;
    TsMosaicMarker value={0};
    if(marker->stored){value.stored=1;value.time=marker->time;snprintf(value.name,sizeof(value.name),"%s",marker->name);}
    TsMosaicMarker *old=&m->markers[index];
    if(old->stored==value.stored && old->time==value.time && !strcmp(old->name,value.name))return 1;
    ts_mosaic_checkpoint(m);*old=value;return 1;
}
int ts_mosaic_save(const TsMosaic *m,const char *dir,char *error,size_t size)
{
    if(!m)return 1;
    /* Do not truncate a previous good save if an invalid layout reaches the
       numerical guard. The writer and reader use the same horizontal range. */
    for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(m->events[i].id && !mosaic_layout_valid(m->events[i].x)) {
        snprintf(error,size,"Invalid Mosaic horizontal position (card %llu)",(unsigned long long)m->events[i].id);return 0;
    }
    for(int i=0;i<TS_MOSAIC_VIEWS;++i)if(m->views[i].stored &&
        (!mosaic_layout_valid(m->views[i].xscroll) || !isfinite(m->views[i].hscale) ||
         m->views[i].hscale<TS_MOSAIC_MIN_HSCALE || m->views[i].hscale>4)) {
        snprintf(error,size,"Invalid Mosaic horizontal saved view %d",i+1);return 0;
    }
    for(int i=0;i<TS_MOSAIC_MARKERS;++i)if(!mosaic_marker_valid(&m->markers[i])) {
        snprintf(error,size,"Invalid Mosaic marker %d",i+1);return 0;
    }
    for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(m->events[i].id &&
        (m->events[i].live_order < -1 || m->events[i].live_order>TS_MOSAIC_EVENTS ||
         !isfinite(m->events[i].finish_fade) || m->events[i].finish_fade<0 || m->events[i].finish_fade>30)) {
        snprintf(error,size,"Invalid Mosaic sequence/finish settings");return 0;
    }
    if(m->live_envelope<0 || m->live_envelope>1){snprintf(error,size,"Invalid looper envelope policy");return 0;}
    char path[4096];
    if(snprintf(path,sizeof(path),"%s/mosaic.tsm",dir)>=(int)sizeof(path))return 0;
    FILE *f=fopen(path,"wb");if(!f)goto failed;
    int live_format=0;for(int i=0;i<TS_MOSAIC_EVENTS;++i)live_format|=m->events[i].id && m->events[i].live;
    live_format|=m->live_capture.source || m->live_capture.continuous || m->live_capture.units || m->live_capture.length!=4 || m->live_capture.quantize;
    int sequence_format=0;for(int i=0;i<TS_MOSAIC_EVENTS;++i)sequence_format|=m->events[i].id && (m->events[i].live_order || m->events[i].finish_fade);
    live_format|=sequence_format || m->live_envelope;
    if(fprintf(f,"TAPESISTER_MOSAIC %d\nSETTINGS %d %.9g %.17g\nSPACING %d\n",m->live_envelope?17:sequence_format?16:live_format?15:14,m->repeat,m->gain,m->speed,m->auto_space)<0)goto close_failed;
    if(live_format && fprintf(f,"LOOPER %d %d %d %d %d\n",m->live_capture.source,m->live_capture.continuous,m->live_capture.units,m->live_capture.length,m->live_capture.quantize)<0)goto close_failed;
    if(m->live_envelope && fprintf(f,"ENVELOPE %d\n",m->live_envelope)<0)goto close_failed;
    for(int i=0;i<TS_MOSAIC_VIEWS;++i) {
        const TsMosaicView *v=&m->views[i];
        if(fprintf(f,"VIEW %d %.17g %.17g %.17g %.17g\n",v->stored,v->scroll,v->xscroll,v->scale,v->hscale)<0)goto close_failed;
    }
    if(fprintf(f,"LOOP %.17g %.17g\n",m->loop_range.start,m->loop_range.end)<0)goto close_failed;
    for(int i=0;i<TS_MOSAIC_MARKERS;++i) {
        const TsMosaicMarker *v=&m->markers[i];char name[65];
        size_t length=strlen(v->name);
        for(int j=0;j<32;++j)snprintf(name+j*2,3,"%02x",j<(int)length?(unsigned char)v->name[j]:0);
        if(fprintf(f,"MARKER %d %.17g %s\n",v->stored,v->time,name)<0)goto close_failed;
    }
    if(fprintf(f,"VOLUME %d\n",TS_MOSAIC_ENVELOPE_POINTS)<0)goto close_failed;
    for(int i=0;i<TS_MOSAIC_ENVELOPE_POINTS;++i)
        if(fprintf(f,"%.9g\n",m->repeat && i==TS_MOSAIC_ENVELOPE_POINTS-1?m->volume[0]:m->volume[i])<0)goto close_failed;
    const TsMosaicSource *sources[TS_MOSAIC_EVENTS];int count=0;
    for(int i=0;i<TS_MOSAIC_EVENTS;++i) {
        const TsMosaicEvent *e=&m->events[i];if(!e->id)continue;
        int s=0;for(;s<count && sources[s]!=e->source;++s);
        if(s==count) {
            sources[count++]=e->source;
            if(e->source->pattern && !ts_pattern_save(e->source->pattern,dir,s,error,size))goto close_failed;
            if(snprintf(path,sizeof(path),"%s/mosaic-%03d.wav",dir,s)>=(int)sizeof(path) ||
                !ts_sample_save_wav32f(&e->source->sample,path,error,size))goto close_failed;
            if(e->source->eq_dry.data) {
                if(snprintf(path,sizeof(path),"%s/mosaic-%03d-dry.wav",dir,s)>=(int)sizeof(path) ||
                   !ts_sample_save_wav32f(&e->source->eq_dry,path,error,size))goto close_failed;
            }
        }
        if(live_format && fprintf(f,"LIVE %d %d %d %d %d %.17g\n",e->live,e->live_finite,e->live_tempo,e->live_transport,e->live_position,e->live_beats)<0)goto close_failed;
        if((sequence_format || m->live_envelope) && fprintf(f,"STEP %d %.17g\n",e->live_order,e->finish_fade)<0)goto close_failed;
        if(fprintf(f,"PATTERN %d\n",e->source->pattern!=NULL)<0)goto close_failed;
        char name[129];
        for(int k=0;k<64;++k)snprintf(name+2*k,3,"%02x",(unsigned char)e->name[k]);
        if(fprintf(f,"%llu %d %.17g %.17g %.17g %.17g %zu %zu %zu %d %d %d %.9g %d %.9g %d %d %d %d %d %s %d %d %.9g %.17g %.17g\n",
            (unsigned long long)e->id,s,e->start,e->duration,e->x,e->width,e->first,e->last,e->crossfade,
            e->mode,e->looping,e->note_count,e->gain,e->tuning.root_note,e->tuning.fine_tune_cents,
            e->notes[0],e->notes[1],e->notes[2],e->notes[3],e->notes[4],name,e->muted,e->solo,e->pan,e->fade_in,e->fade_out)<0)goto close_failed;
        const TsSourceRoute *r=&e->output_route;
        if(fprintf(f,"ROUTE %d %d %d",r->matrix_enabled,r->pan,r->width)<0)goto close_failed;
        for(int d=0;d<TS_MATRIX_DESTINATIONS;++d)if(fprintf(f," %d",r->matrix_gain[d])<0)goto close_failed;
        if(fputc('\n',f)==EOF)goto close_failed;
        int editable=e->source->eq_dry.data!=NULL;
        if(fprintf(f,"EQ %d %d\n",editable,e->source->eq.enabled)<0)goto close_failed;
        if(editable)for(int b=0;b<5;++b) {
            TsEqBand v=e->source->eq.band[b];
            if(fprintf(f,"%d %d %.9g %.9g %.9g\n",v.enabled,v.type,v.frequency,v.gain_db,v.q)<0)goto close_failed;
        }
    }
    if(fclose(f))goto failed;
    return 1;
close_failed:
    fclose(f);
failed:
    snprintf(error,size,"Could not save Mosaic audio and events");return 0;
}
int ts_mosaic_load(TsMosaic *m,const char *dir,char *error,size_t size)
{
    char path[4096],line[1024];
    if(snprintf(path,sizeof(path),"%s/mosaic.tsm",dir)>=(int)sizeof(path))return 0;
    FILE *f=fopen(path,"rb");
    if(!f && errno==ENOENT)return 1;
    if(!f){snprintf(error,size,"Could not open Mosaic arrangement");return 0;}
    TsMosaic *next=ts_mosaic_create();if(!next){fclose(f);return 0;}
    TsMosaicSource *sources[TS_MOSAIC_EVENTS]={0};int count=0;
    if(!fgets(line,sizeof(line),f))goto failed;
    /* Older Windows saves used CRLF; keep strict header matching on every OS. */
    size_t length=strlen(line);
    if(length>=2 && line[length-2]=='\r' && line[length-1]=='\n') {
        line[length-2]='\n';line[length-1]='\0';
    }
    int version=!strcmp(line,"TAPESISTER_MOSAIC 17\n")?17:!strcmp(line,"TAPESISTER_MOSAIC 16\n")?16:!strcmp(line,"TAPESISTER_MOSAIC 15\n")?15:!strcmp(line,"TAPESISTER_MOSAIC 14\n")?14:!strcmp(line,"TAPESISTER_MOSAIC 13\n")?13:!strcmp(line,"TAPESISTER_MOSAIC 12\n")?12:!strcmp(line,"TAPESISTER_MOSAIC 11\n")?11:!strcmp(line,"TAPESISTER_MOSAIC 10\n")?10:!strcmp(line,"TAPESISTER_MOSAIC 9\n")?9:!strcmp(line,"TAPESISTER_MOSAIC 8\n")?8:!strcmp(line,"TAPESISTER_MOSAIC 7\n")?7:!strcmp(line,"TAPESISTER_MOSAIC 6\n")?6:!strcmp(line,"TAPESISTER_MOSAIC 5\n")?5:!strcmp(line,"TAPESISTER_MOSAIC 4\n")?4:!strcmp(line,"TAPESISTER_MOSAIC 1\n")?1:!strcmp(line,"TAPESISTER_MOSAIC 2\n")?2:!strcmp(line,"TAPESISTER_MOSAIC 3\n")?3:0;
    if(!version)goto failed;
    char trailing;
    if(!fgets(line,sizeof(line),f))goto failed;
    if(version>=3) {
        if(sscanf(line,"SETTINGS %d %f %lf %c",&next->repeat,&next->gain,&next->speed,&trailing)!=3 ||
           !isfinite(next->speed) || next->speed<.5 || next->speed>2)goto failed;
    } else if(sscanf(line,"SETTINGS %d %f %c",&next->repeat,&next->gain,&trailing)!=2)goto failed;
    if((next->repeat!=0 && next->repeat!=1) || !isfinite(next->gain) || next->gain<0 || next->gain>2)goto failed;
    next->auto_space=1;
    if(version>=10 && (!fgets(line,sizeof(line),f) || sscanf(line,"SPACING %d %c",&next->auto_space,&trailing)!=1 ||
       (next->auto_space!=0 && next->auto_space!=1)))goto failed;
    if(version>=15 && (!fgets(line,sizeof(line),f) || sscanf(line,"LOOPER %d %d %d %d %d %c",
        &next->live_capture.source,&next->live_capture.continuous,&next->live_capture.units,&next->live_capture.length,&next->live_capture.quantize,&trailing)!=5 ||
        next->live_capture.source<0 || next->live_capture.source>2 || next->live_capture.continuous<0 || next->live_capture.continuous>1 ||
        next->live_capture.units<0 || next->live_capture.units>1 || next->live_capture.length<0 || next->live_capture.length>60 ||
        next->live_capture.quantize<0 || next->live_capture.quantize>1))goto failed;
    if(version>=17 && (!fgets(line,sizeof(line),f) || sscanf(line,"ENVELOPE %d %c",&next->live_envelope,&trailing)!=1 ||
        next->live_envelope<0 || next->live_envelope>1))goto failed;
    if(version>=11)for(int i=0;i<TS_MOSAIC_VIEWS;++i) {
        TsMosaicView *v=&next->views[i];
        if(!fgets(line,sizeof(line),f) || sscanf(line,"VIEW %d %lf %lf %lf %lf %c",
            &v->stored,&v->scroll,&v->xscroll,&v->scale,&v->hscale,&trailing)!=5 ||
            (v->stored!=0 && v->stored!=1) || !isfinite(v->scroll) || !isfinite(v->xscroll) ||
            !isfinite(v->scale) || !isfinite(v->hscale) || v->scroll<0 || v->scroll>1000000 ||
            !mosaic_layout_valid(v->xscroll) ||
            (v->stored && (v->scale<.000001 || v->scale>1200 || v->hscale<TS_MOSAIC_MIN_HSCALE || v->hscale>4)))goto failed;
        if(!v->stored)*v=(TsMosaicView){0};
    }
    if(version>=12) {
        double start,end;
        if(!fgets(line,sizeof(line),f) || sscanf(line,"LOOP %lf %lf %c",&start,&end,&trailing)!=2 ||
           !ts_mosaic_set_loop_range(next,start,end))goto failed;
    }
    if(version>=14)for(int i=0;i<TS_MOSAIC_MARKERS;++i) {
        TsMosaicMarker *v=&next->markers[i];char name[65];
        if(!fgets(line,sizeof(line),f) || sscanf(line,"MARKER %d %lf %64s %c",&v->stored,&v->time,name,&trailing)!=3 || strlen(name)!=64)goto failed;
        for(int j=0;j<32;++j) {
            unsigned value=0;
            for(int k=0;k<2;++k) {
                unsigned char c=(unsigned char)name[j*2+k];
                if(c>='0' && c<='9')value=value*16+c-'0';
                else if(c>='a' && c<='f')value=value*16+c-'a'+10;
                else if(c>='A' && c<='F')value=value*16+c-'A'+10;
                else goto failed;
            }
            v->name[j]=(char)value;
        }
        if(!mosaic_marker_valid(v))goto failed;
        if(!v->stored)*v=(TsMosaicMarker){0};
    }
    if(version>=4) {
        int points;
        if(!fgets(line,sizeof(line),f) || sscanf(line,"VOLUME %d %c",&points,&trailing)!=1 ||
           points!=TS_MOSAIC_ENVELOPE_POINTS)goto failed;
        for(int i=0;i<TS_MOSAIC_ENVELOPE_POINTS;++i)
            if(!fgets(line,sizeof(line),f) || sscanf(line,"%f %c",&next->volume[i],&trailing)!=1 ||
               !isfinite(next->volume[i]) || next->volume[i]<0 || next->volume[i]>1)goto failed;
        if(next->repeat && next->volume[0]!=next->volume[TS_MOSAIC_ENVELOPE_POINTS-1])goto failed;
    }
    next->speed_current=next->speed;
    while(fgets(line,sizeof(line),f)) {
        if(count==TS_MOSAIC_EVENTS)goto failed;
        TsMosaicEvent e={0};
        if(version>=15) {
            if(sscanf(line,"LIVE %d %d %d %d %d %lf %c",&e.live,&e.live_finite,&e.live_tempo,&e.live_transport,&e.live_position,&e.live_beats,&trailing)!=6 ||
                e.live<0 || e.live>1 || e.live_finite<0 || e.live_finite>1 || e.live_tempo<0 || e.live_tempo>1 ||
                e.live_transport<0 || e.live_transport>1 || e.live_position<0 || e.live_position>1 ||
                !isfinite(e.live_beats) || e.live_beats<0 || e.live_beats>1000000 ||
                (e.live_tempo && e.live_beats<=0) || !fgets(line,sizeof(line),f))goto failed;
        }
        if(version>=16) {
            if(sscanf(line,"STEP %d %lf %c",&e.live_order,&e.finish_fade,&trailing)!=2 ||
                e.live_order < -1 || e.live_order>TS_MOSAIC_EVENTS || !isfinite(e.finish_fade) || e.finish_fade<0 || e.finish_fade>30 ||
                !fgets(line,sizeof(line),f))goto failed;
        }
        int pattern_source=0;
        if(version>=13){if(sscanf(line,"PATTERN %d %c",&pattern_source,&trailing)!=1 || pattern_source<0 || pattern_source>1 || !fgets(line,sizeof(line),f))goto failed;}
        unsigned long long id;int source,mode;char extra,name[129];
        int consumed=0;
        if(sscanf(line,"%llu %d %lf %lf %lf %lf %zu %zu %zu %d %d %d %f %d %f %d %d %d %d %d %128s %n",
            &id,&source,&e.start,&e.duration,&e.x,&e.width,&e.first,&e.last,&e.crossfade,&mode,
            &e.looping,&e.note_count,&e.gain,&e.tuning.root_note,&e.tuning.fine_tune_cents,
            &e.notes[0],&e.notes[1],&e.notes[2],&e.notes[3],&e.notes[4],name,&consumed)!=21)goto failed;
        if(version>=2) {
            int used=0;
            if(sscanf(line+consumed,"%d %d %n",&e.muted,&e.solo,&used)!=2 ||
               (e.muted!=0 && e.muted!=1) || (e.solo!=0 && e.solo!=1))goto failed;
            consumed+=used;
        }
        if(version>=3) {
            if(sscanf(line+consumed,"%f %lf %lf %c",&e.pan,&e.fade_in,&e.fade_out,&extra)!=3 ||
               !isfinite(e.pan) || e.pan<-1 || e.pan>1 || !isfinite(e.fade_in) || e.fade_in<0 || e.fade_in>86400 ||
               !isfinite(e.fade_out) || e.fade_out<0 || e.fade_out>86400)goto failed;
        } else if(sscanf(line+consumed," %c",&extra)==1)goto failed;
        if(strlen(name)!=128 || strspn(name,"0123456789abcdef")!=128)goto failed;
        for(int k=0;k<64;++k){unsigned ch;if(sscanf(name+2*k,"%2x",&ch)!=1)goto failed;e.name[k]=(char)ch;}
        if(e.name[63])goto failed;
        e.id=(uint64_t)id;e.revision=1;e.mode=(TsLoopMode)mode;
        if(!e.id || e.id==UINT64_MAX || ts_mosaic_find(next,e.id) || source<0 || source>=TS_MOSAIC_EVENTS ||
            !isfinite(e.start) || e.start<0 || e.start>86400 || !isfinite(e.duration) || e.duration<=0 || e.duration>86400 ||
            !mosaic_layout_valid(e.x) || !isfinite(e.width) || e.width<24 || e.width>4096 ||
            e.note_count<1 || e.note_count>5 || (e.looping!=0 && e.looping!=1) ||
            mode<0 || mode>TS_LOOP_START_PING_PONG || !isfinite(e.gain) || e.gain<0 || e.gain>2 ||
            e.tuning.root_note<0 || e.tuning.root_note>127 || !isfinite(e.tuning.fine_tune_cents) ||
            fabsf(e.tuning.fine_tune_cents)>1200)goto failed;
        for(int n=0;n<e.note_count;++n) {
            if(e.notes[n]<0 || e.notes[n]>127)goto failed;
            for(int k=0;k<n;++k)if(e.notes[n]==e.notes[k])goto failed;
        }
        if(!sources[source]) {
            TsSample s;ts_sample_init(&s);
            snprintf(path,sizeof(path),"%s/mosaic-%03d.wav",dir,source);
            if(!ts_sample_load_wav(&s,path,error,size)){ts_sample_free(&s);goto failed;}
            sources[source]=ts_mosaic_source(next,&s,error,size);ts_sample_free(&s);
            if(!sources[source])goto failed;
            if(pattern_source){
                /* Never deduplicate pattern proxy audio with real audio or another score. */
                TsMosaicSource *unique=calloc(1,sizeof(*unique));if(!unique)goto failed;
                if(!ts_sample_clone(&unique->sample,&sources[source]->sample,error,size)){free(unique);goto failed;}
                unique->pattern=ts_pattern_load(next,dir,source,error,size);
                if(!unique->pattern){ts_sample_free(&unique->sample);free(unique);goto failed;}
                unique->hash=ts_pattern_hash(unique->pattern);
                unique->next=next->sources;next->sources=unique;sources[source]=unique;
            }
        }
        e.source=sources[source];
        if(pattern_source!=(e.source->pattern!=NULL) || (e.live && pattern_source))goto failed;
        if(e.first>=e.last || e.last>e.source->sample.frames || e.crossfade>(e.last-e.first)/2)goto failed;
        if(version>=5) {
            int used=0,at=0;
            if(!fgets(line,sizeof(line),f) || sscanf(line,"ROUTE %d %d %d %n",
                &e.output_route.matrix_enabled,&e.output_route.pan,&e.output_route.width,&at)!=3)goto failed;
            for(int d=0;d<(version>=9?TS_MATRIX_DESTINATIONS:version>=8?TS_MATRIX_INSTANCE_DESTINATIONS:version>=7?TS_MATRIX_SHARED_DESTINATIONS:TS_MATRIX_LEGACY_DESTINATIONS);++d) {
                if(sscanf(line+at,"%d %n",&e.output_route.matrix_gain[d],&used)!=1)goto failed;
                at+=used;
            }
            if(sscanf(line+at," %c",&extra)==1 || !ts_source_route_valid(&e.output_route,0))goto failed;
        }
        if(version>=6) {
            int editable,enabled;
            if(!fgets(line,sizeof(line),f) || sscanf(line,"EQ %d %d %c",&editable,&enabled,&extra)!=2 ||
               editable<0 || editable>1 || enabled<0 || enabled>1)goto failed;
            if(editable) {
                TsMasterEqControls eq;ts_master_eq_default(&eq);eq.enabled=enabled;
                for(int b=0;b<5;++b) {
                    TsEqBand *v=&eq.band[b];
                    if(!fgets(line,sizeof(line),f) || sscanf(line,"%d %d %f %f %f %c",&v->enabled,&v->type,&v->frequency,&v->gain_db,&v->q,&extra)!=5 ||
                       v->enabled<0 || v->enabled>1 || v->type<0 || v->type>=TS_EQ_TYPE_COUNT ||
                       !isfinite(v->frequency) || v->frequency<20 || v->frequency>20000 || !isfinite(v->gain_db) || v->gain_db<-12 || v->gain_db>12 ||
                       !isfinite(v->q) || v->q<.3f || v->q>8)goto failed;
                }
                if(!sources[source]->eq_dry.data) {
                    TsSample dry={0};snprintf(path,sizeof(path),"%s/mosaic-%03d-dry.wav",dir,source);
                    if(!ts_sample_load_wav(&dry,path,error,size))goto failed;
                    if(dry.frames!=e.source->sample.frames || dry.channels!=e.source->sample.channels || dry.sample_rate!=e.source->sample.sample_rate){ts_sample_free(&dry);goto failed;}
                    sources[source]=ts_mosaic_source_eq(next,&e.source->sample,&dry,&eq,error,size);ts_sample_free(&dry);
                    if(!sources[source])goto failed;
                } else if(memcmp(&eq,&sources[source]->eq,sizeof(eq)))goto failed;
                e.source=sources[source];
                if(pattern_source!=(e.source->pattern!=NULL))goto failed;
            } else if(sources[source]->eq_dry.data)goto failed;
        }
        next->events[count++]=e;if(e.id>=next->next_id)next->next_id=e.id+1;
    }
    if(ferror(f) || !ts_mosaic_reserve_patterns(next,0))goto failed;
    fclose(f);
    /* Caller stops/locks audio before replacing a live model. */
    /* Only sources own allocations. Do not put the full arrangement/history
       on the stack while publishing a loaded model. */
    ts_mosaic_publish_loaded(m,next);ts_mosaic_free(next);return 1;
failed:
    fclose(f);ts_mosaic_free(next);snprintf(error,size,"Invalid or incomplete Mosaic project data");return 0;
}
