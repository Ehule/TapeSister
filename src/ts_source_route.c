#include "tapesister/source_route.h"
#include <string.h>

void ts_source_route_mix_init(TsSourceRouteMix *to,const TsSourceRouteMix *map)
{
    memset(to,0,sizeof(*to));
    if(map){to->available=map->available;to->check_outputs=map->check_outputs;}
}

int ts_source_route_valid(const TsSourceRoute *r, int inherit)
{
    return r && r->mode>=TS_SOURCE_MAIN && r->mode<=(inherit?TS_SOURCE_INHERIT:TS_SOURCE_SPEAKER) &&
        r->speaker>=0 && r->speaker<TS_SOURCE_SPEAKERS && r->second>=0 && r->second<TS_SOURCE_SPEAKERS &&
        (r->mode!=TS_SOURCE_PAIR || r->speaker!=r->second) && r->pan>=-100 && r->pan<=100 &&
        r->width>=-100 && r->width<=100;
}
TsSourceRoute ts_source_route_resolve(TsSourceRoute tile, TsSourceRoute track)
{ return track.mode==TS_SOURCE_INHERIT?tile:track; }

void ts_source_route_set(TsSourceRouteVoice *v, TsSourceRoute r, unsigned rate)
{
    if(!ts_source_route_valid(&r,0))r=(TsSourceRoute){0};
    if(v->ready && !memcmp(&r,&v->route,sizeof(r)) && v->rate==rate)return;
    v->route=r;v->rate=rate;v->target_main=r.mode==TS_SOURCE_MAIN?1.f:0.f;
    memset(v->target,0,sizeof(v->target));
    if(r.mode==TS_SOURCE_SPEAKER) {
        v->target[r.speaker][0]=v->target[r.speaker][1]=.5f;
        for(int i=16;i<18;++i)v->target[i][0]=v->target[i][1]=.5f;
    } else if(r.mode==TS_SOURCE_PAIR) {
        int right=r.second;
        float width=1.f+r.width*.01f;
        float l=r.pan>0?1.f-r.pan*.01f:1.f;
        float rr=r.pan<0?1.f+r.pan*.01f:1.f;
        v->target[r.speaker][0]=(.5f+.5f*width)*l;
        v->target[r.speaker][1]=(.5f-.5f*width)*l;
        v->target[right][0]=(.5f-.5f*width)*rr;
        v->target[right][1]=(.5f+.5f*width)*rr;
        memcpy(v->target[16],v->target[r.speaker],sizeof(v->target[16]));
        memcpy(v->target[17],v->target[right],sizeof(v->target[17]));
    }
    if(!v->ready) {
        v->main=v->target_main;memcpy(v->matrix,v->target,sizeof(v->matrix));
        v->remaining=0;v->ready=1;
    } else v->remaining=rate/200?rate/200:1;
    v->mask=0;
    for(int i=0;i<TS_SOURCE_SPEAKERS+2;++i)
        if(v->matrix[i][0] || v->matrix[i][1] || v->target[i][0] || v->target[i][1])v->mask|=1u<<i;
}

TsStereoFrame ts_source_route_frame(TsSourceRouteVoice *v, TsStereoFrame in, TsSourceRouteMix *clean)
{
    if(!clean || !v->ready || (!v->mask && !v->remaining))return in;
    if(v->remaining) {
        float step=1.f/v->remaining;
        v->main+=(v->target_main-v->main)*step;
        for(int i=0;i<TS_SOURCE_SPEAKERS+2;++i)if(v->mask&(1u<<i))
            for(int j=0;j<2;++j)v->matrix[i][j]+=(v->target[i][j]-v->matrix[i][j])*step;
        if(!--v->remaining) {
            v->main=v->target_main;memcpy(v->matrix,v->target,sizeof(v->matrix));
            v->mask=0;for(int i=0;i<TS_SOURCE_SPEAKERS+2;++i)
                if(v->matrix[i][0] || v->matrix[i][1])v->mask|=1u<<i;
        }
    }
    unsigned missing=clean->check_outputs?(v->mask&65535u)&~clean->available:0;
    for(int i=0;i<TS_SOURCE_SPEAKERS+2;++i)if(v->mask&(1u<<i)) {
        float value=in.l*v->matrix[i][0]+in.r*v->matrix[i][1];
        if(i<16) {if(!missing)clean->speaker[i]+=value;}
        else if(i==16){clean->monitor.l+=value;if(missing)clean->fallback.l+=value;}
        else {clean->monitor.r+=value;if(missing)clean->fallback.r+=value;}
    }
    clean->mask|=v->mask&65535u;clean->missing|=missing;
    return (TsStereoFrame){in.l*v->main,in.r*v->main};
}
void ts_source_route_add(TsSourceRouteMix *to, const TsSourceRouteMix *from, float gain)
{
    if(!to || !from || !from->mask)return;
    for(int i=0;i<TS_SOURCE_SPEAKERS;++i)if(from->mask&(1u<<i))to->speaker[i]+=from->speaker[i]*gain;
    to->monitor.l+=from->monitor.l*gain;to->monitor.r+=from->monitor.r*gain;to->mask|=from->mask;
    to->fallback.l+=from->fallback.l*gain;to->fallback.r+=from->fallback.r*gain;to->missing|=from->missing;
}

/* Match Main's residual fade on voice removal and normalization changes. */
void ts_source_route_handoff(TsSourceRouteHandoff *h,TsSourceRouteMix *mix,int changed,unsigned frames)
{
    if(!mix->mask && !h->last.mask)return;
    if(changed && frames && h->last.mask) {
        ts_source_route_mix_init(&h->residual,mix);
        ts_source_route_add(&h->residual,&h->last,1);
        ts_source_route_add(&h->residual,mix,-1);
        h->remaining=frames;
    }
    if(h->remaining && frames) {
        float amount=(float)h->remaining/frames;if(amount>1)amount=1;
        ts_source_route_add(mix,&h->residual,amount);--h->remaining;
    }
    h->last=*mix;
    if(!h->remaining) {
        int active=mix->monitor.l!=0 || mix->monitor.r!=0 || mix->fallback.l!=0 || mix->fallback.r!=0;
        for(int i=0;i<16;++i)active|=mix->speaker[i]!=0;
        if(!active)h->last.mask=0;
    }
}
