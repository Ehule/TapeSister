#include "tapesister/mosaic_gallery.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define GALLERY_MKDIR(p) _mkdir(p)
#else
#define GALLERY_MKDIR(p) mkdir(p,0775)
#endif
static int gallery_error(char *e,size_t n,const char *s)
{if(e && n)snprintf(e,n,"%s",s);return 0;}
TsMosaicGallery *ts_gallery_create(TsMosaic *first)
{
    if(!first)return NULL;
    TsMosaicGallery *g=calloc(1,sizeof(*g));if(!g)return NULL;
    g->root=first;g->count=1;g->next_id=2;g->outgoing=-1;g->fade_seconds=2;
    g->canvas[0]=(TsMosaicCanvas){.scene=first,.id=1,.scale=24,.hscale=1};
    snprintf(g->canvas[0].name,TS_GALLERY_NAME,"Canvas 1");return g;
}
void ts_gallery_free(TsMosaicGallery *g)
{
    if(!g)return;
    for(int i=0;i<g->count;++i)if(g->canvas[i].scene!=g->root)ts_mosaic_free(g->canvas[i].scene);
    free(g);
}
TsMosaic *ts_gallery_active(const TsMosaicGallery *g)
{return g && g->active>=0 && g->active<g->count?g->canvas[g->active].scene:NULL;}
TsMosaic *ts_gallery_outgoing(const TsMosaicGallery *g)
{return g && g->outgoing>=0 && g->outgoing<g->count?g->canvas[g->outgoing].scene:NULL;}
int ts_gallery_append_prepared(TsMosaicGallery *g,TsMosaic *m,char *e,size_t n)
{
    if(!g || !m || g->count>=TS_GALLERY_LIMIT)return gallery_error(e,n,"GALLERY LIMIT: 16 CANVASES");
    if(g->outgoing>=0)return gallery_error(e,n,"WAIT FOR THE CURRENT CROSSFADE");
    if(!g->next_id || g->next_id>=UINT64_MAX-1)return gallery_error(e,n,"CANVAS IDENTITIES EXHAUSTED");
    for(int j=0;j<g->count;++j)if(g->canvas[j].scene==m)return gallery_error(e,n,"CANVAS IS ALREADY IN THIS GALLERY");
    int i=g->count++;g->canvas[i]=(TsMosaicCanvas){.scene=m,.id=g->next_id++,.scale=24,.hscale=1};
    snprintf(g->canvas[i].name,TS_GALLERY_NAME,"Canvas %llu",(unsigned long long)g->canvas[i].id);return 1;
}
int ts_gallery_add(TsMosaicGallery *g,char *e,size_t n)
{
    TsMosaic *m=ts_mosaic_create();if(!m)return gallery_error(e,n,"NOT ENOUGH MEMORY FOR ANOTHER CANVAS");
    if(ts_gallery_append_prepared(g,m,e,n))return 1;
    ts_mosaic_free(m);return 0;
}
int ts_gallery_detach(TsMosaicGallery *g,int i,TsMosaic **retired,char *e,size_t n)
{
    if(retired)*retired=NULL;
    if(!g || i<0 || i>=g->count || g->count<=1)return gallery_error(e,n,"KEEP AT LEAST ONE CANVAS");
    if(i==g->active || g->outgoing>=0)return gallery_error(e,n,"SWITCH AWAY / FINISH THE FADE BEFORE DELETING THIS CANVAS");
    TsMosaic *m=g->canvas[i].scene;
    memmove(g->canvas+i,g->canvas+i+1,(size_t)(g->count-i-1)*sizeof(g->canvas[0]));
    --g->count;if(g->active>i)--g->active;
    if(retired && m!=g->root)*retired=m;return 1;
}
int ts_gallery_remove(TsMosaicGallery *g,int i,char *e,size_t n)
{
    TsMosaic *m=NULL;if(!ts_gallery_detach(g,i,&m,e,n))return 0;ts_mosaic_free(m);return 1;
}
int ts_gallery_rename(TsMosaicGallery *g,int i,const char *name,char *e,size_t n)
{
    if(!g || i<0 || i>=g->count || !name || !*name || strlen(name)>=TS_GALLERY_NAME)
        return gallery_error(e,n,"NAME MUST BE 1-47 PRINTABLE CHARACTERS");
    for(const unsigned char *p=(const unsigned char *)name;*p;++p)
        if(*p<32 || *p>126)return gallery_error(e,n,"USE PRINTABLE ASCII IN CANVAS NAMES");
    snprintf(g->canvas[i].name,TS_GALLERY_NAME,"%s",name);return 1;
}
static void gallery_restart(TsMosaic *m,double position)
{
    m->playing=m->was_playing=0;
    memset(m->live,0,sizeof(m->live));memset(&m->sequence,0,sizeof(m->sequence));
    memset(m->live_reset_ids,0,sizeof(m->live_reset_ids));m->live_reset_sequence=0;
    m->live_paused=0;m->live_pause_gain=m->live_pause_base=1;
    m->live_pause_last=(TsStereoFrame){0};m->live_pause_routes_valid=0;m->live_reset_remaining=0;
    m->live_envelope_running=0;m->live_phase=0;m->live_clock_edge=0;
    ts_mosaic_audition(m,0);m->audition_id=0;m->audition_source=(TsMosaicEvent){0};
    for(int i=0;i<TS_MOSAIC_EVENTS;++i){m->voices[i].active=0;m->voices[i].attack=0;}
    m->audition_voice.active=0;
    m->last_output=m->transition_from=(TsStereoFrame){0};m->transition_remaining=0;
    memset(&m->route_handoff,0,sizeof(m->route_handoff));
    ts_mosaic_seek(m,position);m->schedule.valid=0;
}
static void gallery_park(TsMosaicGallery *g)
{
    TsMosaic *old=ts_gallery_outgoing(g);if(!old)return;
    old->playing=0;old->live_paused=1;old->live_pause_gain=0;
    g->canvas[g->outgoing].parked=1;g->outgoing=-1;
}
int ts_gallery_switch(TsMosaicGallery *g,int i,char *e,size_t n)
{
    if(!g || i<0 || i>=g->count)return gallery_error(e,n,"CANVAS IS NOT AVAILABLE");
    if(g->outgoing>=0)return gallery_error(e,n,"WAIT FOR THE CURRENT CROSSFADE");
    if(i==g->active)return 1;
    TsMosaicCanvas *to=&g->canvas[i],*from=&g->canvas[g->active];
    if(to->entry<TS_GALLERY_START || to->entry>TS_GALLERY_MARKER || !isfinite(g->fade_seconds) || g->fade_seconds<0 || g->fade_seconds>30)
        return gallery_error(e,n,"INVALID ENTRY OR CROSSFADE SETTING");
    double start=0;
    if(to->entry==TS_GALLERY_MARKER) {
        if(to->marker<0 || to->marker>=TS_MOSAIC_MARKERS || !to->scene->markers[to->marker].stored)
            return gallery_error(e,n,"CHOOSE A STORED MARKER ON THE DESTINATION CANVAS");
        start=to->scene->markers[to->marker].time;
    }
    int playing=from->scene->playing;
    from->paused=from->scene->live_paused;from->parked=0;
    if(to->entry!=TS_GALLERY_RESUME){gallery_restart(to->scene,start);if(to->entry==TS_GALLERY_MARKER)to->scene->marker_active=to->marker+1;}
    else if(to->parked) {
        to->scene->live_paused=to->paused;to->scene->live_pause_gain=0;
        to->scene->schedule.valid=0;
    }
    to->parked=0;to->scene->playing=playing;
    g->outgoing=g->active;g->active=i;g->fade_position=0;g->fade_length=g->fade_seconds;
    int audible=from->scene->playing || to->scene->playing || from->scene->audition_id ||
        (ts_mosaic_live_count(from->scene) && !from->paused) ||
        (ts_mosaic_live_count(to->scene) && !to->scene->live_paused);
    if(!g->fade_length || !audible)gallery_park(g);
    return 1;
}
void ts_gallery_begin_block(TsMosaicGallery *g,unsigned frames)
{
    ts_mosaic_begin_block(ts_gallery_active(g),frames);
    ts_mosaic_begin_block(ts_gallery_outgoing(g),frames);
}
void ts_gallery_clock(TsMosaicGallery *g,int enabled,int running,double quarter,double phase)
{
    ts_mosaic_live_clock(ts_gallery_active(g),enabled,running,quarter,phase);
    ts_mosaic_live_clock(ts_gallery_outgoing(g),enabled,running,quarter,phase);
}
TsStereoFrame ts_gallery_read(TsMosaicGallery *g,int rate,TsSourceRouteMix *routes)
{
    TsMosaic *m=ts_gallery_active(g),*old=ts_gallery_outgoing(g);
    if(!old)return ts_mosaic_read_routed(m,rate,routes);
    if(rate<=0)return (TsStereoFrame){0};
    /* Constant-sum fade: identical canvases retain their level. Every route,
       send and reference tap receives the same gain; only two engines render. */
    float t=(float)fmax(0,fmin(1,g->fade_position/g->fade_length));
    TsSourceRouteMix a,b;
    if(routes){ts_source_route_mix_init(&a,routes);ts_source_route_mix_init(&b,routes);}
    TsStereoFrame x=ts_mosaic_read_routed(m,rate,routes?&a:NULL);
    TsStereoFrame y=ts_mosaic_read_routed(old,rate,routes?&b:NULL);
    if(routes){ts_source_route_add(routes,&a,t);ts_source_route_add(routes,&b,1-t);}
    g->fade_position+=1./rate;
    if(g->fade_position+1e-12>=g->fade_length)gallery_park(g);
    return ts_stereo_frame_sanitize((TsStereoFrame){x.l*t+y.l*(1-t),x.r*t+y.r*(1-t)});
}
void ts_gallery_rebind_root(TsMosaicGallery *g,TsMosaic *root)
{
    if(!g)return;
    for(int i=0;i<g->count;++i)if(g->canvas[i].scene==g->root)g->canvas[i].scene=root;
    g->root=root;
}
static void gallery_hash_data(uint64_t *h,const void *p,size_t n)
{const unsigned char *b=p;for(size_t i=0;i<n;++i){*h^=b[i];*h*=1099511628211ull;}}
uint64_t ts_gallery_hash(const TsMosaicGallery *g)
{
    if(!g)return ts_mosaic_hash(NULL);
    uint64_t h=1469598103934665603ull;
    gallery_hash_data(&h,&g->count,sizeof(g->count));gallery_hash_data(&h,&g->active,sizeof(g->active));
    gallery_hash_data(&h,&g->next_id,sizeof(g->next_id));gallery_hash_data(&h,&g->fade_seconds,sizeof(g->fade_seconds));
    for(int i=0;i<g->count;++i) {
        const TsMosaicCanvas *c=&g->canvas[i];uint64_t scene=ts_mosaic_hash(c->scene);
        gallery_hash_data(&h,&scene,sizeof(scene));gallery_hash_data(&h,&c->id,sizeof(c->id));
        gallery_hash_data(&h,c->name,strlen(c->name)+1);gallery_hash_data(&h,&c->entry,sizeof(c->entry));
        gallery_hash_data(&h,&c->marker,sizeof(c->marker));
    }
    return h;
}
#include "ts_mosaic_gallery_storage.inc"
