#include "ts_pattern_instance.h"
#include "ft2_structs.h"
#include "ft2_tables.h"
#include "ft2_sample_ed.h"
#include "tapesister/mosaic.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
/* Control-thread reservation grows with placed cards. Each Scatter chord
   has private performances; there is no shared transport or voice stealing. */
#define PATTERN_BUFFER 64
typedef struct {
    thPatternState state;
    const TsPatternCard *source;
    const void *owner;
    uint64_t generation,stamp;
    int rate,note,looping;
    double position,until_tick,last_target;
    uint64_t rendered;
    float left[PATTERN_BUFFER],right[PATTERN_BUFFER];
    unsigned at,count;
    TsStereoFrame previous,current;
} PatternPlayer;
typedef struct { PatternPlayer *players;unsigned capacity;uint64_t stamp;int overflow; } PatternEngine;
static const TsPatternAsset *asset(const TsPatternCard *p,TsTileId id){for(int i=0;i<p->asset_count;++i)if(p->assets[i].tile==id)return &p->assets[i];return NULL;}
static void source_free(void *ptr){thPatternSource *s=ptr;if(!s)return;for(int i=0;i<132;++i)free(s->instruments[i]);free(s);}
static void *source_prepare(const TsPatternCard *p)
{
    thPatternSource *s=calloc(1,sizeof(*s));if(!s)return NULL;s->definition=p;
    for(int a=0;a<129;++a){
        const TsTrackerInstrument *meta=&p->score.instruments[a];
        if(a && !meta->present && !p->score.aliases[a])continue;
        instr_t *ins=s->instruments[a]=calloc(1,sizeof(*ins));if(!ins){source_free(s);return NULL;}
        ins->numSamples=meta->present?meta->sample_count:1;
        if(meta->present){
            memcpy(ins->note2SampleLUT,meta->note_map,96);
            for(int k=0;k<12;++k)for(int j=0;j<2;++j){ins->volEnvPoints[k][j]=meta->vol_points[k][j];ins->panEnvPoints[k][j]=meta->pan_points[k][j];}
#define FIELD(host,ft) ins->ft=meta->host
            FIELD(vol_length,volEnvLength);FIELD(pan_length,panEnvLength);FIELD(vol_sustain,volEnvSustain);FIELD(vol_start,volEnvLoopStart);FIELD(vol_end,volEnvLoopEnd);
            FIELD(pan_sustain,panEnvSustain);FIELD(pan_start,panEnvLoopStart);FIELD(pan_end,panEnvLoopEnd);FIELD(vol_flags,volEnvFlags);FIELD(pan_flags,panEnvFlags);FIELD(fadeout,fadeout);
            FIELD(vib_type,autoVibType);FIELD(vib_sweep,autoVibSweep);FIELD(vib_depth,autoVibDepth);FIELD(vib_rate,autoVibRate);
#undef FIELD
        }
        for(int k=0;k<ins->numSamples;++k){
            const TsPatternAsset *binding=asset(p,meta->present?meta->tiles[k]:p->score.aliases[a]);if(!binding)continue;
            const TsSample *sample=&binding->audio->sample;sample_t *v=&ins->smp[k];
            v->length=(int32_t)sample->frames;v->dataPtr=(int8_t*)sample->data;v->tileData=sample->data;v->tileChannels=sample->channels;v->flags=SAMPLE_16BIT;
            v->volume=meta->present?meta->volume[k]:64;v->panning=meta->present?meta->panning[k]:128;
            v->tileLoopMode=binding->mode;v->tileCrossfade=binding->crossfade;
            if(binding->looping){v->loopStart=binding->first;v->loopLength=binding->last-binding->first;v->flags|=binding->mode==TS_LOOP_PING_PONG || binding->mode==TS_LOOP_START_PING_PONG?LOOP_PINGPONG:LOOP_FORWARD;if(ts_loop_base_mode(binding->mode)==TS_LOOP_REVERSE)v->flags|=SAMPLE_REVERSE_LOOP;}
            double hz=sample->sample_rate*exp2((60-binding->tuning.root_note)/12.0+binding->tuning.fine_tune_cents/1200.0);
            setSampleC4Hz(v,hz);v->tileRateCorrection=hz/getSampleC4Hz(v);
            if(meta->present){v->relativeNote=meta->relative_note[k];v->finetune=meta->finetune[k];v->tileRateCorrection=hz/(8363*exp2(v->relativeNote/12.0+v->finetune/1536.0));}
        }
    }
    return s;
}
static void player_reset(PatternPlayer *v,const TsPatternCard *p,const void *owner,uint64_t gen,int rate,int note,int looping)
{
    memset(v,0,sizeof(*v));v->source=p;v->owner=owner;v->generation=gen;v->rate=rate;v->note=note;v->looping=looping;
    memcpy(v->state.lanes,p->score.lanes,sizeof(v->state.lanes));v->state.global_volume=p->global_volume;v->state.enabled=p->enabled;v->state.transmission=true;v->state.random=p->id;
    thPatternSource *source=p->prepared;
    for(int i=0;i<32;++i){channel_t *c=&v->state.channels[i];c->instrPtr=source->instruments[0];c->oldPan=c->outPan=c->finalPan=128;c->channelOff=p->score.lanes[i].muted;microtonalReset(&c->microtonal,i);
        v->state.clutch[i]=true;
        v->state.direction[i]=p->score.lanes[i].direction==TS_TRACKER_REVERSE?-1:1;
    }
}
static void player_fill(PatternPlayer *v)
{
    const TsPatternCard *p=v->source;unsigned n=PATTERN_BUFFER;
    if(v->until_tick<=0){
        if(!v->looping && v->rendered>=(uint64_t)llround(p->duration*v->rate) && !v->state.released)v->state.released=1;
        tapeheadPatternStep(&v->state,p->prepared);
        v->until_tick+=v->rate*2.5/p->score.bpm;
    }
    if(n>(unsigned)ceil(v->until_tick))n=(unsigned)ceil(v->until_tick);if(!n)n=1;
    memset(v->left,0,n*sizeof(float));memset(v->right,0,n*sizeof(float));
    /* Transposition scales sample pitch, never clock/row traversal. */
    double transpose=exp2((v->note-60)/12.0);
    for(int i=0;i<64;++i)v->state.voices[i].tileRateCorrection*=transpose;
    tapeheadPatternMix(&v->state,v->left,v->right,n,p->score.channel_count);
    for(int i=0;i<64;++i)v->state.voices[i].tileRateCorrection/=transpose;
    v->until_tick-=n;v->rendered+=n;v->count=n;v->at=0;
}
/* Silent advancement is proportional to tracker ticks, not elapsed PCM
   frames. It uses the same loop normalization and ramp endpoints as the tile
   mixer, retaining effect memory and deterministic seek phase. */
static void player_skip(PatternPlayer *p,uint64_t frames)
{
    if(p->at<p->count){unsigned n=p->count-p->at;if(n>frames)n=(unsigned)frames;p->at+=n;p->position+=n;frames-=n;}
    while(frames){
        if(p->until_tick<=0){
            if(!p->looping && p->rendered>=(uint64_t)llround(p->source->duration*p->rate) && !p->state.released)p->state.released=1;
            tapeheadPatternStep(&p->state,p->source->prepared);
            p->until_tick+=p->rate*2.5/p->source->score.bpm;
        }
        unsigned n=(unsigned)ceil(p->until_tick);if(n>frames)n=(unsigned)frames;
        double transpose=exp2((p->note-60)/12.0);
        for(int i=0;i<64;++i){voice_t *v=&p->state.voices[i];if(!v->active)continue;
            unsigned advance=n;
            if(v->isFadeOutVoice && v->volumeRampLength && advance>v->volumeRampLength)advance=v->volumeRampLength;
            double distance=((double)v->delta/4294967296.0)*v->tileRateCorrection*transpose*advance;
            v->tilePosition+=distance*v->tileDirection;
            if(v->loopType!=LOOP_DISABLED && !v->oneShot){
                TsLoopMode mode=(TsLoopMode)v->tileLoopMode;
                if(ts_loop_base_mode(mode)==TS_LOOP_PING_PONG && !v->tileIntro && v->sampleEnd>v->loopStart+1){
                    double span=v->sampleEnd-1-v->loopStart,cycle=2*span;
                    double phase=v->tileDirection<0?cycle-(v->tilePosition-v->loopStart):v->tilePosition-v->loopStart;
                    phase=fmod(fmod(phase,cycle)+cycle,cycle);v->tileDirection=phase<span?1:-1;
                    v->tilePosition=v->loopStart+(phase<span?phase:cycle-phase);
                }else{
                    TsSample sample={.data=(float*)v->tileData,.frames=v->tileFrames,.channels=v->tileChannels};
                    (void)ts_audition_loop_frame(&sample,&v->tilePosition,v->loopStart,v->sampleEnd,v->tileCrossfade,mode,&v->tileDirection,&v->tileIntro);
                }
            }else if(v->tilePosition<0 || v->tilePosition>=v->sampleEnd)v->active=false;
            v->position=(int)v->tilePosition;v->positionFrac=(uint64_t)((v->tilePosition-floor(v->tilePosition))*4294967296.0);
            unsigned ramp=n<v->volumeRampLength?n:v->volumeRampLength;
            v->fCurrVolumeL+=v->fVolumeLDelta*ramp;v->fCurrVolumeR+=v->fVolumeRDelta*ramp;v->volumeRampLength-=ramp;
            if(ramp && !v->volumeRampLength){v->fCurrVolumeL=v->fTargetVolumeL;v->fCurrVolumeR=v->fTargetVolumeR;if(v->isFadeOutVoice)v->active=false;}
        }
        p->until_tick-=n;p->rendered+=n;p->position+=n;frames-=n;
    }
}
static TsStereoFrame player_next(PatternPlayer *p){if(p->at==p->count)player_fill(p);unsigned i=p->at++;return (TsStereoFrame){p->left[i],p->right[i]};}
static void *engine_create(void){return calloc(1,sizeof(PatternEngine));}
static int engine_reserve(void *ptr,unsigned n)
{
    PatternEngine *e=ptr;if(n<=e->capacity)return 1;
    PatternPlayer *players=realloc(e->players,(size_t)n*sizeof(*players));if(!players)return 0;
    memset(players+e->capacity,0,(n-e->capacity)*sizeof(*players));e->players=players;e->capacity=n;return 1;
}
static void engine_free(void *ptr){PatternEngine *e=ptr;free(e->players);free(e);}
static void engine_frame(void *p){++((PatternEngine*)p)->stamp;((PatternEngine*)p)->overflow=0;}
static int engine_overflow(const void *p){return ((const PatternEngine*)p)->overflow;}
static TsStereoFrame engine_read_cached(void *ptr,const TsPatternCard *p,const void *owner,uint64_t generation,double time,int rate,double speed,int note,int looping,unsigned *hint)
{
    (void)speed;PatternEngine *engine=ptr;PatternPlayer *v=NULL,*oldest=engine->players;
    if(!engine->capacity){engine->overflow=1;return (TsStereoFrame){0};}
    /* The owning Mosaic voice keeps an index, never a relocatable pointer.
       Validate both parts of the key: seeks, pitch edits, source replacement,
       model swaps and player reuse must retain the original scan semantics. */
    if(hint && *hint && *hint<=engine->capacity){
        PatternPlayer *q=&engine->players[*hint-1];
        if(q->owner==owner && q->note==note)v=q;
    }
    if(!v){
        for(unsigned i=0;i<engine->capacity;++i){PatternPlayer *q=&engine->players[i];if(q->owner==owner && q->note==note){v=q;break;}if(q->stamp<oldest->stamp)oldest=q;}
        if(!v){if(oldest->owner && oldest->stamp+1>=engine->stamp){engine->overflow=1;return (TsStereoFrame){0};}v=oldest;}
        if(hint)*hint=(unsigned)(v-engine->players)+1;
    }
    double local=looping?fmod(time,p->duration):time;double target=local*rate;
    if(v->source!=p || v->owner!=owner || v->generation!=generation || v->rate!=rate || v->note!=note || v->looping!=looping || target+1e-5<v->last_target){player_reset(v,p,owner,generation,rate,note,looping);}
    v->stamp=engine->stamp;v->last_target=target;
    if(target-v->position>128)player_skip(v,(uint64_t)(target-v->position)-2);
    while(v->position<=target){v->previous=v->current;v->current=player_next(v);v->position+=1;}
    float fraction=(float)(target-floor(target));TsStereoFrame out={v->previous.l+(v->current.l-v->previous.l)*fraction,v->previous.r+(v->current.r-v->previous.r)*fraction};
    if(looping){double remain=p->duration-local;if(remain<.002){out.l*=remain/.002;out.r*=remain/.002;}}
    return out;
}
static TsStereoFrame engine_read(void *ptr,const TsPatternCard *p,const void *owner,uint64_t generation,double time,int rate,double speed,int note,int looping)
{
    return engine_read_cached(ptr,p,owner,generation,time,rate,speed,note,looping,NULL);
}
void ts_tapehead_pattern_install(void)
{
    const TsPatternBackend b={source_prepare,source_free,engine_create,engine_reserve,engine_free,engine_frame,engine_overflow,engine_read,engine_read_cached};ts_pattern_backend(&b);
}
