#include "tapesister/mosaic.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

TsMosaic *ts_mosaic_create(void)
{
    static uint64_t epoch=0;
    TsMosaic *m=calloc(1,sizeof(*m));
    if(m) {m->next_id=1;m->gain=0.7f;m->speed=m->speed_current=1;m->epoch=++epoch;}
    return m;
}
void ts_mosaic_free(TsMosaic *m)
{
    if(!m)return;
    while(m->sources) {TsMosaicSource *s=m->sources;m->sources=s->next;ts_sample_free(&s->sample);free(s);}
    free(m);
}
TsMosaicEvent *ts_mosaic_find(TsMosaic *m,uint64_t id)
{
    if(m && id)for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(m->events[i].id==id)return &m->events[i];
    return NULL;
}
TsMosaicSource *ts_mosaic_source(TsMosaic *m,const TsSample *sample,char *error,size_t size)
{
    if(!m || !sample || !sample->data || sample->frames<2 || !sample->sample_rate)return NULL;
    uint64_t hash=ts_sample_hash(sample);
    for(TsMosaicSource *s=m->sources;s;s=s->next)
        if(s->hash==hash && s->sample.frames==sample->frames &&
           s->sample.sample_rate==sample->sample_rate && s->sample.channels==sample->channels &&
           !memcmp(s->sample.data,sample->data,sample->frames*sample->channels*sizeof(float)))return s;
    TsMosaicSource *s=calloc(1,sizeof(*s));
    if(!s){if(error && size)snprintf(error,size,"Not enough memory for event audio");return NULL;}
    if(!ts_sample_clone(&s->sample,sample,error,size)) {free(s);return NULL;}
    s->hash=hash;
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
void ts_mosaic_collect(TsMosaic *m)
{
    if(!m)return;
    TsMosaicSource **p=&m->sources;
    while(*p) {
        TsMosaicSource *s=*p;int used=s->pins!=0;
        for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(m->events[i].id && m->events[i].source==s)used=1;
        for(int h=0;h<m->history_count && !used;++h)
            for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(m->history[h][i].id && m->history[h][i].source==s)used=1;
        if(used)p=&s->next;else {*p=s->next;ts_sample_free(&s->sample);free(s);}
    }
}
void ts_mosaic_checkpoint(TsMosaic *m)
{
    if(!m)return;
    if(m->history_cursor<m->history_count)m->history_count=m->history_cursor;
    if(m->history_count==TS_MOSAIC_HISTORY) {
        memmove(m->history,m->history+1,(TS_MOSAIC_HISTORY-1)*sizeof(m->history[0]));
        memmove(m->history_speed,m->history_speed+1,(TS_MOSAIC_HISTORY-1)*sizeof(m->history_speed[0]));
        --m->history_count;
    }
    m->history_speed[m->history_count]=m->speed;
    memcpy(m->history[m->history_count++],m->events,sizeof(m->events));
    m->history_cursor=m->history_count;++m->revision;
}
int ts_mosaic_undo(TsMosaic *m,int redo)
{
    if(!m)return 0;
    int at=redo?m->history_cursor:m->history_cursor-1;
    if(at<0 || at>=m->history_count)return 0;
    for(int i=0;i<TS_MOSAIC_EVENTS;++i) {
        TsMosaicEvent e=m->events[i];m->events[i]=m->history[at][i];m->history[at][i]=e;
    }
    double speed=m->speed;ts_mosaic_set_speed(m,m->history_speed[at]);m->history_speed[at]=speed;
    m->history_cursor+=redo?1:-1;memset(m->voices,0,sizeof(m->voices));++m->revision;return 1;
}
static int overlap(const TsMosaicEvent *a,const TsMosaicEvent *b)
{return a->start<b->start+b->duration-1e-9 && b->start<a->start+a->duration-1e-9;}
void ts_mosaic_space(TsMosaic *m,TsMosaicEvent *e)
{
    if(!m || !e)return;
    e->x=fmax(0,e->x);
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
TsMosaicEvent *ts_mosaic_add(TsMosaic *m,TsMosaicSource *source,double start,double x)
{
    if(!m || !source || !isfinite(start) || !isfinite(x))return NULL;
    for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(!m->events[i].id) {
        TsMosaicEvent *e=&m->events[i];
        *e=(TsMosaicEvent){.id=m->next_id++,.revision=1,.source=source,
            .start=fmax(0,start),.duration=(double)source->sample.frames/source->sample.sample_rate,
            .x=x,.width=86,.last=source->sample.frames,.looping=1,.note_count=1,
            .notes={60},.gain=1,.tuning={60,0}};
        snprintf(e->name,sizeof(e->name),"%.63s",source->sample.name);
        ts_mosaic_space(m,e);++m->revision;return e;
    }
    return NULL;
}
TsMosaicEvent *ts_mosaic_copy(TsMosaic *m,uint64_t id,double start,double x)
{
    TsMosaicEvent *old=ts_mosaic_find(m,id);if(!old)return NULL;
    TsMosaicEvent copy=*old,*e=ts_mosaic_add(m,old->source,start,x);if(!e)return NULL;
    uint64_t new_id=e->id;*e=copy;e->id=new_id;e->start=fmax(0,start);e->x=x;
    ts_mosaic_space(m,e);return e;
}
void ts_mosaic_delete(TsMosaic *m,uint64_t id)
{TsMosaicEvent *e=ts_mosaic_find(m,id);if(e){memset(e,0,sizeof(*e));++m->revision;}}
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
    if(m){m->time=isfinite(time)?fmax(0,time):0;memset(m->voices,0,sizeof(m->voices));
        m->transition_from=m->last_output;m->transition_total=m->transition_remaining=(unsigned)(m->rate>0?m->rate/500:0);}
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
static float event_fade(const TsMosaicEvent *e,double elapsed)
{
    double in=fmax(0,e->fade_in),out=fmax(0,e->fade_out);
    /* Overlapping fades meet proportionally, including after shortening a tile. */
    if(in+out>e->duration){double scale=e->duration/(in+out);in*=scale;out*=scale;}
    double gain=1;
    if(in>0)gain=fmin(gain,elapsed/in);
    if(out>0)gain=fmin(gain,(e->duration-elapsed)/out);
    return (float)fmax(0,fmin(1,gain));
}
/* Derive loop phase from distance travelled. Reverse uses a full sample
   cycle (the legacy helper drops a frame at each reverse wrap). This also
   makes seeks into long ping-pong/START events exact without replaying them. */
static double voice_position(const TsMosaicEvent *e,double distance,int *direction,int *intro)
{
    TsLoopMode mode=ts_loop_base_mode(e->mode);
    double first=(double)e->first,last=(double)e->last;
    *intro=0;*direction=1;
    if(!e->looping)return first+distance;
    int start=ts_loop_starts_at_sample(e->mode);
    if(start && distance<first){*intro=1;return distance;}
    if(mode==TS_LOOP_FORWARD) {
        double p=start?distance:first+distance;
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
static void voice_begin(TsMosaicVoice *v,const TsMosaicEvent *e,double elapsed,int rate)
{
    memset(v,0,sizeof(*v));v->active=1;v->id=e->id;v->revision=e->revision;v->start=e->start;
    event_balance(e,&v->mix_left,&v->mix_right);
    for(int n=0;n<e->note_count;++n) {
        double pitch=ts_tuning_note_pitch(&e->tuning,e->notes[n]-60);
        v->travel[n]=elapsed*e->source->sample.sample_rate*pitch;
        v->step[n]=(double)e->source->sample.sample_rate/rate*pitch;
    }
}
TsStereoFrame ts_mosaic_read(TsMosaic *m,int rate)
{
    TsStereoFrame out={0,0};if(!m || rate<=0)return out;
    if(!m->playing) {
        if(m->was_playing){m->transition_from=m->last_output;m->transition_total=m->transition_remaining=(unsigned)(rate/500);m->was_playing=0;}
        if(m->transition_remaining && m->transition_total) {
            float gain=(float)--m->transition_remaining/m->transition_total;
            out.l=m->transition_from.l*gain;out.r=m->transition_from.r*gain;
        }
        m->last_output=out;return out;
    }
    if(!m->was_playing){for(int i=0;i<TS_MOSAIC_EVENTS;++i)m->voices[i].attack=0;m->was_playing=1;m->speed_current=m->speed;}
    if(m->rate!=rate){memset(m->voices,0,sizeof(m->voices));m->rate=rate;}
    /* Smooth live changes over at most 20 ms. The same speed advances both
       arrangement time and every source phase, without retriggering voices. */
    double slew=1.5/(rate*.020),difference=m->speed-m->speed_current;
    m->speed_current+=fmax(-slew,fmin(slew,difference));
    double speed=m->speed_current;
    double end=0;int any_solo=0;
    for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(m->events[i].id) {
        end=fmax(end,m->events[i].start+m->events[i].duration);
        any_solo|=m->events[i].solo;
    }
    if(m->time>=end) {
        if(m->repeat && end>0)ts_mosaic_seek(m,fmod(m->time,end));
        else {m->playing=0;return out;}
    }
    for(int i=0;i<TS_MOSAIC_EVENTS;++i) {
        const TsMosaicEvent *e=&m->events[i];TsMosaicVoice *v=&m->voices[i];
        if(!e->id || m->time+1e-10<e->start || m->time>=e->start+e->duration) {v->active=0;continue;}
        if(!v->active || v->id!=e->id || v->revision!=e->revision || v->start!=e->start) {
            voice_begin(v,e,fmax(0,m->time-e->start),rate);
            v->audible_gain=(!e->muted && (!any_solo || e->solo))?1.0f:0.0f;
        }
        float envelope=ts_audition_attack_gain(v->attack++,ts_audition_attack_frames(rate,2));
        double tail=(e->start+e->duration-m->time)*rate/speed;
        if(tail<rate*0.002)envelope*=(float)fmax(0,tail/(rate*0.002));
        float target=(!e->muted && (!any_solo || e->solo))?1.0f:0.0f;
        float ramp=1.0f/(rate*.005f);
        v->audible_gain=target>v->audible_gain?fminf(target,v->audible_gain+ramp):fmaxf(target,v->audible_gain-ramp);
        float left,right;event_balance(e,&left,&right);
        float mix_ramp=2.0f/(rate*.005f);
        v->mix_left+=fmaxf(-mix_ramp,fminf(mix_ramp,left-v->mix_left));
        v->mix_right+=fmaxf(-mix_ramp,fminf(mix_ramp,right-v->mix_right));
        float gain=m->gain*envelope*v->audible_gain*event_fade(e,m->time-e->start)/e->note_count;
        for(int n=0;n<e->note_count;++n) {
            TsStereoFrame f;
            v->position[n]=voice_position(e,v->travel[n],&v->direction[n],&v->intro[n]);
            if(!e->looping) {
                if(v->position[n]>=e->last)continue;
                f=ts_audition_read_frame(&e->source->sample,v->position[n],e->last);
                float release=(float)fmin(1,(e->last-v->position[n])/(v->step[n]*speed*rate*0.002));
                f.l*=release;f.r*=release;
            } else f=v->intro[n]?ts_audition_read_frame(&e->source->sample,v->position[n],e->last):
                ts_audition_read_looped_mode_frame(&e->source->sample,v->position[n],e->first,e->last,e->crossfade,e->mode);
            out.l+=f.l*gain*v->mix_left;out.r+=f.r*gain*v->mix_right;
            v->travel[n]+=v->step[n]*speed;
        }
    }
    if(m->transition_remaining && m->transition_total) {
        float gain=(float)--m->transition_remaining/m->transition_total;
        out.l=out.l*(1-gain)+m->transition_from.l*gain;
        out.r=out.r*(1-gain)+m->transition_from.r*gain;
    }
    m->time+=speed/rate;m->last_output=ts_stereo_frame_sanitize(out);return m->last_output;
}
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
        char mix[128];snprintf(mix,sizeof(mix),"%.9g %.17g %.17g",e->pan,e->fade_in,e->fade_out);
        for(const unsigned char *p=(const unsigned char *)mix;*p;++p){hash^=*p;hash*=1099511628211ull;}
        hash^=(uint64_t)(e->muted | (e->solo<<1));hash*=1099511628211ull;
        for(const unsigned char *p=(const unsigned char *)line;*p;++p){hash^=*p;hash*=1099511628211ull;}
        for(const unsigned char *p=(const unsigned char *)e->name;*p;++p){hash^=*p;hash*=1099511628211ull;}
    }
    char speed[48];snprintf(speed,sizeof(speed),"%.17g",m->speed);
    for(const unsigned char *p=(const unsigned char *)speed;*p;++p){hash^=*p;hash*=1099511628211ull;}
    hash^=(uint64_t)m->repeat;hash*=1099511628211ull;
    return hash;
}

int ts_mosaic_save(const TsMosaic *m,const char *dir,char *error,size_t size)
{
    if(!m)return 1;
    char path[4096];
    if(snprintf(path,sizeof(path),"%s/mosaic.tsm",dir)>=(int)sizeof(path))return 0;
    FILE *f=fopen(path,"w");if(!f)goto failed;
    if(fprintf(f,"TAPESISTER_MOSAIC 3\nSETTINGS %d %.9g %.17g\n",m->repeat,m->gain,m->speed)<0)goto close_failed;
    const TsMosaicSource *sources[TS_MOSAIC_EVENTS];int count=0;
    for(int i=0;i<TS_MOSAIC_EVENTS;++i) {
        const TsMosaicEvent *e=&m->events[i];if(!e->id)continue;
        int s=0;for(;s<count && sources[s]!=e->source;++s);
        if(s==count) {
            sources[count++]=e->source;
            if(snprintf(path,sizeof(path),"%s/mosaic-%03d.wav",dir,s)>=(int)sizeof(path) ||
                !ts_sample_save_wav32f(&e->source->sample,path,error,size))goto close_failed;
        }
        char name[129];
        for(int k=0;k<64;++k)snprintf(name+2*k,3,"%02x",(unsigned char)e->name[k]);
        if(fprintf(f,"%llu %d %.17g %.17g %.17g %.17g %zu %zu %zu %d %d %d %.9g %d %.9g %d %d %d %d %d %s %d %d %.9g %.17g %.17g\n",
            (unsigned long long)e->id,s,e->start,e->duration,e->x,e->width,e->first,e->last,e->crossfade,
            e->mode,e->looping,e->note_count,e->gain,e->tuning.root_note,e->tuning.fine_tune_cents,
            e->notes[0],e->notes[1],e->notes[2],e->notes[3],e->notes[4],name,e->muted,e->solo,e->pan,e->fade_in,e->fade_out)<0)goto close_failed;
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
    FILE *f=fopen(path,"r");
    if(!f && errno==ENOENT)return 1;
    if(!f){snprintf(error,size,"Could not open Mosaic arrangement");return 0;}
    TsMosaic *next=ts_mosaic_create();if(!next){fclose(f);return 0;}
    TsMosaicSource *sources[TS_MOSAIC_EVENTS]={0};int count=0;
    if(!fgets(line,sizeof(line),f))goto failed;
    int version=!strcmp(line,"TAPESISTER_MOSAIC 1\n")?1:!strcmp(line,"TAPESISTER_MOSAIC 2\n")?2:!strcmp(line,"TAPESISTER_MOSAIC 3\n")?3:0;
    if(!version)goto failed;
    char trailing;
    if(!fgets(line,sizeof(line),f))goto failed;
    if(version>=3) {
        if(sscanf(line,"SETTINGS %d %f %lf %c",&next->repeat,&next->gain,&next->speed,&trailing)!=3 ||
           !isfinite(next->speed) || next->speed<.5 || next->speed>2)goto failed;
    } else if(sscanf(line,"SETTINGS %d %f %c",&next->repeat,&next->gain,&trailing)!=2)goto failed;
    if((next->repeat!=0 && next->repeat!=1) || !isfinite(next->gain) || next->gain<0 || next->gain>2)goto failed;
    next->speed_current=next->speed;
    while(fgets(line,sizeof(line),f)) {
        if(count==TS_MOSAIC_EVENTS)goto failed;
        TsMosaicEvent e={0};unsigned long long id;int source,mode;char extra,name[129];
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
            !isfinite(e.x) || e.x<0 || e.x>100000 || !isfinite(e.width) || e.width<24 || e.width>4096 ||
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
        }
        e.source=sources[source];
        if(e.first>=e.last || e.last>e.source->sample.frames || e.crossfade>(e.last-e.first)/2)goto failed;
        next->events[count++]=e;if(e.id>=next->next_id)next->next_id=e.id+1;
    }
    if(ferror(f))goto failed;
    fclose(f);
    /* Caller stops/locks audio before replacing a live model. */
    TsMosaic old=*m;*m=*next;*next=old;ts_mosaic_free(next);return 1;
failed:
    fclose(f);ts_mosaic_free(next);snprintf(error,size,"Invalid or incomplete Mosaic project data");return 0;
}
