#include "tapesister/source_route.h"
#include <string.h>

void ts_source_route_mix_init(TsSourceRouteMix *to,const TsSourceRouteMix *map)
{
    memset(to,0,sizeof(*to));
    if(map){to->available=map->available;to->check_outputs=map->check_outputs;}
}

int ts_source_route_valid(const TsSourceRoute *r, int inherit)
{
    if(!r || r->matrix_enabled<0 || r->matrix_enabled>1)return 0;
    for(int i=0;i<TS_MATRIX_DESTINATIONS;++i)if(r->matrix_gain[i]<0 || r->matrix_gain[i]>2000)return 0;
    if(r->mix_enabled<0 || r->mix_enabled>1 || r->clean_level<0 || r->clean_level>100 ||
       (r->mix_enabled && r->mode==TS_SOURCE_MAIN))return 0;
    for(int i=0;i<TS_SOURCE_SENDS;++i)if(r->send_level[i]<0 || r->send_level[i]>100)return 0;
    return r->mode>=TS_SOURCE_MAIN && r->mode<=(inherit?TS_SOURCE_INHERIT:TS_SOURCE_SPEAKER) &&
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
    v->route=r;v->rate=rate;v->target_main=!r.matrix_enabled && r.mode==TS_SOURCE_MAIN?1.f:0.f;
    float width=1.f+r.width*.01f;
    float left=r.pan>0?1.f-r.pan*.01f:1.f;
    float right=r.pan<0?1.f+r.pan*.01f:1.f;
    v->target_main_matrix[0]=v->target_main*(.5f+.5f*width)*left;
    v->target_main_matrix[1]=v->target_main*(.5f-.5f*width)*left;
    v->target_main_matrix[2]=v->target_main*(.5f-.5f*width)*right;
    v->target_main_matrix[3]=v->target_main*(.5f+.5f*width)*right;
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
    if(r.mix_enabled) {
        for(int i=0;i<18;++i)for(int ch=0;ch<2;++ch)v->target[i][ch]*=r.clean_level*.01f;
        for(int i=0;i<TS_SOURCE_SENDS;++i) {
            v->target[18+i*2][0]=r.send_level[i]*.01f;
            v->target[19+i*2][1]=r.send_level[i]*.01f;
        }
    }
    if(r.matrix_enabled) {
        memset(v->target,0,sizeof(v->target));
        for(int d=0;d<TS_MATRIX_DESTINATIONS;++d) {
            float gain=r.matrix_gain[d]*.001f;
            v->target[24+d*2][0]=gain*(.5f+.5f*width)*left;
            v->target[24+d*2][1]=gain*(.5f-.5f*width)*left;
            v->target[25+d*2][0]=gain*(.5f-.5f*width)*right;
            v->target[25+d*2][1]=gain*(.5f+.5f*width)*right;
        }
    }
    if(!v->ready) {
        v->main=v->target_main;memcpy(v->matrix,v->target,sizeof(v->matrix));
        memcpy(v->main_matrix,v->target_main_matrix,sizeof(v->main_matrix));
        v->remaining=0;v->ready=1;
    } else v->remaining=rate/200?rate/200:1;
    v->mask=0;
    for(int i=0;i<TS_SOURCE_COEFFICIENTS;++i)
        if(v->matrix[i][0] || v->matrix[i][1] || v->target[i][0] || v->target[i][1])v->mask|=UINT64_C(1)<<i;
}

static TsStereoFrame main_frame(const TsSourceRouteVoice *v,TsStereoFrame in)
{
    return (TsStereoFrame){in.l*v->main_matrix[0]+in.r*v->main_matrix[1],
        in.l*v->main_matrix[2]+in.r*v->main_matrix[3]};
}

TsStereoFrame ts_source_route_frame(TsSourceRouteVoice *v, TsStereoFrame in, TsSourceRouteMix *clean)
{
    if(!clean || !v->ready)return in;
    if(!v->mask && !v->remaining) {
        TsStereoFrame main=main_frame(v,in);
        clean->reference.l+=in.l-main.l;clean->reference.r+=in.r-main.r;
        clean->tape_input.l+=in.l*(1-v->main);clean->tape_input.r+=in.r*(1-v->main);
        return main;
    }
    if(v->remaining) {
        float step=1.f/v->remaining;
        v->main+=(v->target_main-v->main)*step;
        for(int i=0;i<4;++i)v->main_matrix[i]+=(v->target_main_matrix[i]-v->main_matrix[i])*step;
        for(int i=0;i<TS_SOURCE_COEFFICIENTS;++i)if(v->mask&(UINT64_C(1)<<i))
            for(int j=0;j<2;++j)v->matrix[i][j]+=(v->target[i][j]-v->matrix[i][j])*step;
        if(!--v->remaining) {
            v->main=v->target_main;memcpy(v->matrix,v->target,sizeof(v->matrix));
            memcpy(v->main_matrix,v->target_main_matrix,sizeof(v->main_matrix));
            v->mask=0;for(int i=0;i<TS_SOURCE_COEFFICIENTS;++i)
                if(v->matrix[i][0] || v->matrix[i][1])v->mask|=UINT64_C(1)<<i;
        }
    }
    unsigned missing=clean->check_outputs?(v->mask&65535u)&~clean->available:0;
    TsStereoFrame main=main_frame(v,in);
    /* DRY capture reconstructs the source before route pan/width as well as
       before clean/send levels. This residual is never mixed into playback. */
    clean->reference.l+=in.l-main.l;clean->reference.r+=in.r-main.r;
    clean->tape_input.l+=in.l*(1-v->main);clean->tape_input.r+=in.r*(1-v->main);
    for(int i=0;i<TS_SOURCE_COEFFICIENTS;++i)if(v->mask&(UINT64_C(1)<<i)) {
        float value=in.l*v->matrix[i][0]+in.r*v->matrix[i][1];
        if(i<16) {if(!missing){clean->speaker[i]+=value;if(v->route.mix_enabled)clean->matrix_speaker[i]+=value;}}
        else if(i==16){clean->monitor.l+=value;if(missing)clean->fallback.l+=value;
            if(v->route.mix_enabled){clean->matrix_monitor.l+=value;if(missing)clean->matrix_fallback.l+=value;}}
        else if(i==17){clean->monitor.r+=value;if(missing)clean->fallback.r+=value;
            if(v->route.mix_enabled){clean->matrix_monitor.r+=value;if(missing)clean->matrix_fallback.r+=value;}}
        else if(i>=TS_SOURCE_GRAPH_OFFSET) {
            int d=(i-TS_SOURCE_GRAPH_OFFSET)/2;
            if(i&1)clean->graph[d].r+=value;else clean->graph[d].l+=value;
            clean->graph_mask|=1u<<d;
        } else {
            int bus=(i-18)/2;
            if(i&1)clean->send[bus].r+=value;else clean->send[bus].l+=value;
            clean->send_mask|=1u<<bus;
        }
    }
    clean->mask|=v->mask&65535u;clean->missing|=missing;
    return main;
}
void ts_source_route_add(TsSourceRouteMix *to, const TsSourceRouteMix *from, float gain)
{
    if(!to || !from || (!from->graph_mask && !from->mask && !from->send_mask && !from->reference.l && !from->reference.r &&
                       !from->tape_input.l && !from->tape_input.r))return;
    for(int d=0;d<TS_MATRIX_DESTINATIONS;++d)if(from->graph_mask&(1u<<d)) {
        to->graph[d].l+=from->graph[d].l*gain;to->graph[d].r+=from->graph[d].r*gain;
    }
    to->graph_mask|=from->graph_mask;
    for(int i=0;i<TS_SOURCE_SPEAKERS;++i)if(from->mask&(UINT64_C(1)<<i)) {
        to->speaker[i]+=from->speaker[i]*gain;
        to->matrix_speaker[i]+=from->matrix_speaker[i]*gain;
    }
    to->matrix_monitor.l+=from->matrix_monitor.l*gain;to->matrix_monitor.r+=from->matrix_monitor.r*gain;
    to->matrix_fallback.l+=from->matrix_fallback.l*gain;to->matrix_fallback.r+=from->matrix_fallback.r*gain;
    to->monitor.l+=from->monitor.l*gain;to->monitor.r+=from->monitor.r*gain;to->mask|=from->mask;
    to->fallback.l+=from->fallback.l*gain;to->fallback.r+=from->fallback.r*gain;to->missing|=from->missing;
    for(int i=0;i<TS_SOURCE_SENDS;++i)if(from->send_mask&(UINT64_C(1)<<i)) {
        to->send[i].l+=from->send[i].l*gain;to->send[i].r+=from->send[i].r*gain;
    }
    to->send_mask|=from->send_mask;
    to->reference.l+=from->reference.l*gain;to->reference.r+=from->reference.r*gain;
    to->tape_input.l+=from->tape_input.l*gain;to->tape_input.r+=from->tape_input.r*gain;
}

TsStereoFrame ts_source_route_take_master(TsSourceRouteMix *mix)
{
    TsStereoFrame master=mix->matrix_monitor;
    for(int i=0;i<TS_SOURCE_SPEAKERS;++i) {
        mix->speaker[i]-=mix->matrix_speaker[i];mix->matrix_speaker[i]=0;
    }
    mix->monitor.l-=master.l;mix->monitor.r-=master.r;
    mix->fallback.l-=mix->matrix_fallback.l;mix->fallback.r-=mix->matrix_fallback.r;
    mix->matrix_monitor=mix->matrix_fallback=(TsStereoFrame){0,0};
    return master;
}

/* Match Main's residual fade on voice removal and normalization changes. */
void ts_source_route_handoff(TsSourceRouteHandoff *h,TsSourceRouteMix *mix,int changed,unsigned frames)
{
    int previous=h->last.graph_mask || h->last.mask || h->last.send_mask || h->last.reference.l || h->last.reference.r ||
        h->last.tape_input.l || h->last.tape_input.r;
    if(!mix->graph_mask && !mix->mask && !mix->send_mask && !mix->reference.l && !mix->reference.r &&
       !mix->tape_input.l && !mix->tape_input.r && !previous)return;
    if(changed && frames && previous) {
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
        int active=mix->monitor.l!=0 || mix->monitor.r!=0 || mix->fallback.l!=0 || mix->fallback.r!=0 ||
            mix->reference.l!=0 || mix->reference.r!=0 || mix->tape_input.l!=0 || mix->tape_input.r!=0;
        for(int i=0;i<16;++i)active|=mix->speaker[i]!=0;
        for(int i=0;i<TS_SOURCE_SENDS;++i)active|=mix->send[i].l!=0 || mix->send[i].r!=0;
        for(int i=0;i<TS_MATRIX_DESTINATIONS;++i)active|=mix->graph[i].l!=0 || mix->graph[i].r!=0;
        if(!active)h->last.graph_mask=h->last.mask=h->last.send_mask=0;
    }
}
