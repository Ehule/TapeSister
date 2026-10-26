#include "tapesister/pattern_card.h"
#include "tapesister/mosaic.h"
#include "tapesister/sample_pages.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static TsPatternBackend backend;
void ts_pattern_backend(const TsPatternBackend *b){backend=*b;}
void *ts_pattern_engine_create(void){return backend.create?backend.create():NULL;}
int ts_pattern_engine_reserve(void *p,unsigned n){return !p || !backend.reserve || backend.reserve(p,n);}
void ts_pattern_engine_frame(void *p){if(p && backend.frame)backend.frame(p);}
int ts_pattern_engine_overflow(const void *p){return p && backend.overflow?backend.overflow(p):0;}
void ts_pattern_engine_free(void *p){if(p && backend.free_engine)backend.free_engine(p);}
TsStereoFrame ts_pattern_read(void *engine,const TsPatternCard *p,const void *voice,uint64_t generation,double time,int rate,double speed,int note,int looping)
{return engine && p && p->prepared && backend.read?backend.read(engine,p,voice,generation,time,rate,speed,note,looping):(TsStereoFrame){0};}
int ts_pattern_prepare(TsPatternCard *p){if(p->prepared)return 1;if(!backend.prepare)return 1;p->prepared=backend.prepare(p);return p->prepared!=NULL;}
void ts_pattern_free(TsPatternCard *p){if(!p)return;if(p->prepared && backend.free_source)backend.free_source(p->prepared);ts_sister_tracker_free(&p->score);free(p->assets);free(p);}
static unsigned read_le(const unsigned char *p,int n){unsigned v=0;for(int i=0;i<n;++i)v|=(unsigned)p[i]<<(i*8);return v;}
static void pattern_cells(TsPatternCard *p,const TsSisterTracker *t,const TsTrackerPattern *pattern)
{
    p->linear=1;p->enabled=1;p->global_volume=64;
    if(t->embedded_size && ts_tracker_embedded_matches(t)) {
        const unsigned char *b=t->embedded_data;int lanes=b[3]=='3'?32:8;
        const unsigned char *records=b+52+lanes*10+256;int total=read_le(b+8,2);
        size_t bytes=7+256*lanes*7;
        for(int i=0;i<total;++i)if(read_le(records+i*bytes+3,4)==pattern->id){
            for(int r=0;r<256;++r)memcpy(p->cells[r],records+i*bytes+7+r*lanes*7,lanes*7);
            /* STH3 frequency table byte, see embedded codec. */
            p->linear=b[6]!=0;p->enabled=b[24]!=0;p->global_volume=b[13];
            for(int lane=0;lane<lanes;++lane)if(b[52+lane*10+5])p->selected|=1u<<lane;
            return;
        }
    }
    for(int r=0;r<256;++r)for(int l=0;l<32;++l) {
        const TsTrackerCell *c=&pattern->cells[r][l];unsigned char *d=p->cells[r][l];
        d[0]=c->note_kind==TS_TRACKER_NOTE_OFF?97:c->note_kind==TS_TRACKER_NOTE_PITCH?(c->note>=12 && c->note<=107?c->note-11:0):0;
        for(int a=1;a<129;++a)if(c->tile_id && t->aliases[a]==c->tile_id){d[1]=a;break;}
        d[2]=c->has_volume?c->volume+16:0;
        if(c->fx_command){d[3]=c->fx_command<='9'?c->fx_command-'0':c->fx_command-'A'+10;d[4]=c->fx_value;}
        d[5]=c->tune_command=='M'?0x16:c->tune_command=='N'?0x17:0;d[6]=c->tune_value;
    }
}
static int add_asset(TsMosaic *m,TsPatternCard *p,const TsSamplePages *pages,const TsInstrument *active,TsTileId tile,char *error,size_t n)
{
    if(!tile)return 1;
    for(int i=0;i<p->asset_count;++i)if(p->assets[i].tile==tile)return 1;
    TsTileLocation loc;const TsBankSlot *slot=ts_sample_pages_find_tile(pages,active,tile,&loc);
    if(!slot)return 1;
    const TsInstrument *bank=ts_sample_pages_page(pages,active,loc.page);int live=loc.slot==bank->selected_slot;
    const TsSample *s=live?&bank->current:&slot->sample;if(!s->data)return 1;
    TsMosaicSource *audio=ts_mosaic_source(m,s,error,n);if(!audio)return 0;
    TsPatternAsset a={.tile=tile,.audio=audio,.tuning=live?bank->audible_tuning:slot->audible_tuning,
        .looping=live?bank->has_loop:slot->has_loop,.first=live?bank->loop_first:slot->loop_first,
        .last=live?bank->loop_last:slot->loop_last,.mode=live?bank->loop_mode:slot->loop_mode};
    if(!a.looping){a.first=0;a.last=s->frames;}
    TsAuditionPlan plan={s,a.first,a.last};a.crossfade=a.looping?ts_audition_crossfade_frames(&plan,live?bank->loop_crossfade_ms:slot->loop_crossfade_ms):0;
    p->assets[p->asset_count++]=a;return 1;
}
static double pattern_duration(const TsSisterTracker *t)
{
    int rows=t->patterns[0]->rows;
    if(!t->length_bypass){
        if(t->control_lane>=0 && t->control_lane<t->channel_count){
            if(t->lanes[t->control_lane].length)rows=t->lanes[t->control_lane].length;
        }else{
            int longest=0;for(int i=0;i<t->channel_count;++i)if(t->lanes[i].length>longest)longest=t->lanes[i].length;
            if(longest)rows=longest;
        }
    }
    return rows*t->ticks_per_line*2.5/t->bpm;
}
uint64_t ts_pattern_hash(const TsPatternCard *p)
{
    uint64_t hash=ts_sister_tracker_hash(&p->score);
    for(size_t i=0;i<sizeof(p->cells);++i)hash=(hash^((unsigned char*)p->cells)[i])*1099511628211ull;
    hash=(hash^(p->linear+2*p->enabled+4*p->global_volume))*1099511628211ull;
    hash=(hash^p->selected)*1099511628211ull;
    for(int i=0;i<p->asset_count;++i){
        TsPatternAsset key=p->assets[i];key.audio=NULL;
        /* Hash explicit fields rather than structure padding. */
        char fields[256];int count=snprintf(fields,sizeof(fields),"%llu:%d:%.9g:%zu:%zu:%zu:%d:%d",
            (unsigned long long)key.tile,key.tuning.root_note,key.tuning.fine_tune_cents,key.first,key.last,key.crossfade,key.looping,key.mode);
        for(int j=0;j<count;++j)hash=(hash^(unsigned char)fields[j])*1099511628211ull;
        hash=(hash^p->assets[i].audio->hash)*1099511628211ull;
    }
    return hash;
}
TsMosaicSource *ts_pattern_source(TsMosaic *m,const TsSamplePages *pages,const TsInstrument *active,TsPatternId id,char *error,size_t n)
{
    const TsSisterTracker *t=&pages->tracker;const TsTrackerPattern *pattern=ts_sister_tracker_pattern_const(t,id);if(!pattern)return NULL;
    TsPatternCard *p=calloc(1,sizeof(*p));if(!p)return NULL;
    ts_sister_tracker_init(&p->score);p->id=id;p->root=60;
    p->score.bpm=t->bpm;p->score.ticks_per_line=t->ticks_per_line;p->score.channel_count=t->channel_count;
    p->score.fasttracks_uses_length=t->fasttracks_uses_length;p->score.length_bypass=t->length_bypass;p->score.control_lane=t->control_lane;
    memcpy(p->score.aliases,t->aliases,sizeof(t->aliases));memcpy(p->score.instruments,t->instruments,sizeof(t->instruments));memcpy(p->score.lanes,t->lanes,sizeof(t->lanes));
    /* init owns a default pattern; replace it with the one shared definition. */
    for(int i=0;i<256;++i){free(p->score.patterns[i]);p->score.patterns[i]=NULL;}
    p->score.patterns[0]=malloc(sizeof(*pattern));if(!p->score.patterns[0])goto fail;*p->score.patterns[0]=*pattern;
    p->score.pattern_count=1;p->score.next_pattern_id=id+1;p->score.editor_pattern=id;p->score.orders[0]=id;p->score.order_count=1;
    pattern_cells(p,t,pattern);
    p->assets=calloc(128*16,sizeof(*p->assets));if(!p->assets)goto fail;
    int root=128;
    unsigned char used[129]={0};for(int r=0;r<pattern->rows;++r)for(int l=0;l<t->channel_count;++l){unsigned char *c=p->cells[r][l];if(c[1]<=128)used[c[1]]=1;if(c[0] && c[0]<=96 && c[0]+11<root)root=c[0]+11;}
    if(root<128)p->root=root;
    for(int a=1;a<129;++a)if(used[a]){
        if(t->instruments[a].present){for(int k=0;k<t->instruments[a].sample_count;++k)if(!add_asset(m,p,pages,active,t->instruments[a].tiles[k],error,n))goto fail;}
        else if(!add_asset(m,p,pages,active,t->aliases[a],error,n))goto fail;
    }
    p->duration=pattern_duration(&p->score);
    if(!ts_pattern_prepare(p))goto fail;
    TsMosaicSource *s=calloc(1,sizeof(*s));if(!s)goto fail;
    s->sample.data=calloc(2,sizeof(float));if(!s->sample.data){free(s);goto fail;}
    s->sample.frames=2;s->sample.channels=1;s->sample.sample_rate=48000;
    snprintf(s->sample.name,sizeof(s->sample.name),"P%03u %.40s",id,pattern->name);
    s->pattern=p;s->hash=ts_pattern_hash(p);
    for(TsMosaicSource *old=m->sources;old;old=old->next)if(old->pattern && old->hash==s->hash && old->pattern->id==id && !memcmp(old->pattern->cells,p->cells,sizeof(p->cells))){ts_sample_free(&s->sample);free(s);ts_pattern_free(p);return old;}
    s->next=m->sources;m->sources=s;return s;
fail:ts_pattern_free(p);snprintf(error,n,"Unable to prepare pattern card");return NULL;
}
int ts_pattern_save(const TsPatternCard *p,const char *dir,int index,char *e,size_t n)
{
    char path[4096];snprintf(path,sizeof(path),"%s/pattern-%03d.score",dir,index);
    if(!ts_sister_tracker_save_file(&p->score,path,e,n))return 0;
    snprintf(path,sizeof(path),"%s/pattern-%03d.cells",dir,index);FILE *f=fopen(path,"wb");if(!f)return 0;
    int ok=fwrite(p->cells,1,sizeof(p->cells),f)==sizeof(p->cells);if(fclose(f))ok=0;if(!ok)return 0;
    snprintf(path,sizeof(path),"%s/pattern-%03d.assets",dir,index);f=fopen(path,"wb");if(!f)return 0;
    ok=fprintf(f,"TSPATTERN 1 %u %d %d %.17g %d %d %d %u\n",p->id,p->root,p->linear,p->duration,p->asset_count,p->enabled,p->global_volume,p->selected)>0;
    for(int i=0;ok && i<p->asset_count;++i){const TsPatternAsset *a=&p->assets[i];
        ok=fprintf(f,"%llu %d %.9g %zu %zu %zu %d %d\n",(unsigned long long)a->tile,a->tuning.root_note,a->tuning.fine_tune_cents,a->first,a->last,a->crossfade,a->looping,a->mode)>0;
        snprintf(path,sizeof(path),"%s/pattern-%03d-%04d.wav",dir,index,i);if(ok)ok=ts_sample_save_wav32f(&a->audio->sample,path,e,n);
    }
    if(fclose(f))ok=0;
    return ok;
}
TsPatternCard *ts_pattern_load(TsMosaic *m,const char *dir,int index,char *e,size_t n)
{
    char path[4096],line[256],tail;TsPatternCard *p=calloc(1,sizeof(*p));if(!p)return NULL;ts_sister_tracker_init(&p->score);
    snprintf(path,sizeof(path),"%s/pattern-%03d.score",dir,index);if(!ts_sister_tracker_load_file(&p->score,path,e,n))goto fail;
    snprintf(path,sizeof(path),"%s/pattern-%03d.cells",dir,index);FILE *f=fopen(path,"rb");if(!f)goto fail;
    int ok=fread(p->cells,1,sizeof(p->cells),f)==sizeof(p->cells) && fgetc(f)==EOF;fclose(f);if(!ok)goto fail;
    if(p->score.pattern_count!=1 || !p->score.patterns[0])goto fail;
    for(int row=0;row<256;++row)for(int lane=0;lane<32;++lane){
        const unsigned char *c=p->cells[row][lane];
        if(c[0]>97 || c[1]>128 || c[3]>35 || (c[5] && c[5]!=0x16 && c[5]!=0x17))goto fail;
    }
    snprintf(path,sizeof(path),"%s/pattern-%03d.assets",dir,index);f=fopen(path,"rb");if(!f)goto fail;
    if(!fgets(line,sizeof(line),f) || sscanf(line,"TSPATTERN 1 %u %d %d %lf %d %d %d %u %c",&p->id,&p->root,&p->linear,&p->duration,&p->asset_count,&p->enabled,&p->global_volume,&p->selected,&tail)!=8 ||
       p->enabled<0 || p->enabled>1 || p->global_volume<0 || p->global_volume>64 || p->asset_count<0 || p->asset_count>2048 || p->root<0 || p->root>127 || (p->linear!=0 && p->linear!=1) || !isfinite(p->duration) || p->duration<=0 || p->duration>86400 || fabs(p->duration-pattern_duration(&p->score))>1e-9 || !ts_sister_tracker_pattern(&p->score,p->id)){fclose(f);goto fail;}
    p->assets=calloc(p->asset_count?p->asset_count:1,sizeof(*p->assets));if(!p->assets){fclose(f);goto fail;}
    for(int i=0;i<p->asset_count;++i){TsPatternAsset *a=&p->assets[i];unsigned long long id;int mode;
        if(!fgets(line,sizeof(line),f) || sscanf(line,"%llu %d %f %zu %zu %zu %d %d %c",&id,&a->tuning.root_note,&a->tuning.fine_tune_cents,&a->first,&a->last,&a->crossfade,&a->looping,&mode,&tail)!=8 || !id || a->tuning.root_note<0 || a->tuning.root_note>127 || !isfinite(a->tuning.fine_tune_cents) || fabsf(a->tuning.fine_tune_cents)>1200 || a->looping<0 || a->looping>1 || mode<0 || mode>TS_LOOP_START_PING_PONG){fclose(f);goto fail;}
        a->tile=id;a->mode=mode;TsSample sample={0};snprintf(path,sizeof(path),"%s/pattern-%03d-%04d.wav",dir,index,i);
        if(!ts_sample_load_wav(&sample,path,e,n)){fclose(f);goto fail;}
        a->audio=ts_mosaic_source(m,&sample,e,n);ts_sample_free(&sample);
        if(!a->audio || a->first>=a->last || a->last>a->audio->sample.frames || a->crossfade>(a->last-a->first)/2){fclose(f);goto fail;}
    }
    ok=fgetc(f)==EOF;fclose(f);if(!ok || !ts_pattern_prepare(p))goto fail;return p;
fail:ts_pattern_free(p);snprintf(e,n,"Invalid pattern card source");return NULL;
}
