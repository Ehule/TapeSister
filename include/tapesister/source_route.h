#ifndef TAPESISTER_SOURCE_ROUTE_H
#define TAPESISTER_SOURCE_ROUTE_H

#include "tapesister/sample.h"
#include "tapesister/source_route_types.h"

typedef struct {
    TsStereoFrame graph[TS_MATRIX_DESTINATIONS];
    uint64_t graph_mask, graph_mask_high, graph_mask_top;
    float speaker[TS_SOURCE_SPEAKERS];
    TsStereoFrame monitor, fallback;
    TsStereoFrame reference; /* Pre-routing dry recording tap, counted once. */
    /* Source fraction diverted from Main, before route pan and clean/send
       levels. Tape input only: never add this tap to the audible dry mix. */
    TsStereoFrame tape_input;
    TsStereoFrame send[TS_SOURCE_SENDS];
    /* Parallel-mix subset of speaker/monitor/fallback, for optional Master
       recombination. Ordinary direct routes are not part of this subset. */
    float matrix_speaker[TS_SOURCE_SPEAKERS];
    TsStereoFrame matrix_monitor, matrix_fallback;
    unsigned send_mask;
    unsigned mask, missing, available;
    int check_outputs;
} TsSourceRouteMix;

typedef struct {
    TsSourceRouteMix last, residual;
    unsigned remaining;
} TsSourceRouteHandoff;
void ts_source_route_handoff(TsSourceRouteHandoff *h,TsSourceRouteMix *mix,int changed,unsigned frames);

/* Reset audio buses, retaining only map's output availability. map may be to. */
void ts_source_route_mix_init(TsSourceRouteMix *to,const TsSourceRouteMix *map);
/* Reuse an initialized mix produced by frame/add/handoff. Unmarked graph
   destinations must be zero. Clears audio and availability, like {0}. */
void ts_source_route_mix_reset(TsSourceRouteMix *mix);
/* Audio-owned reuse, retaining map's hardware availability (map may be mix).
   Same ownership/zero invariant as reset; init is still required for new memory. */
void ts_source_route_mix_reuse(TsSourceRouteMix *mix,const TsSourceRouteMix *map);
/* Graph-only copy into an empty, initialized mix. No gain/addition regrouping.
   Both mixes must obey reset's ownership invariant and must not alias. */
void ts_source_route_graph_copy(TsSourceRouteMix *to,const TsSourceRouteMix *from);
void ts_source_route_graph_scale(TsSourceRouteMix *mix,float gain);
static inline void ts_source_route_graph_mark(TsSourceRouteMix *mix,unsigned d)
{
    if(d<64)mix->graph_mask|=UINT64_C(1)<<d;
    else if(d<128)mix->graph_mask_high|=UINT64_C(1)<<(d-64);
    else if(d<TS_MATRIX_DESTINATIONS)mix->graph_mask_top|=UINT64_C(1)<<(d-128);
}
int ts_source_route_valid(const TsSourceRoute *r, int allow_inherit);
TsSourceRoute ts_source_route_resolve(TsSourceRoute tile, TsSourceRoute track);
void ts_source_route_set(TsSourceRouteVoice *v, TsSourceRoute route, unsigned rate);
TsStereoFrame ts_source_route_frame(TsSourceRouteVoice *v, TsStereoFrame input,
                                    TsSourceRouteMix *clean);
void ts_source_route_add(TsSourceRouteMix *to, const TsSourceRouteMix *from, float gain);
TsStereoFrame ts_source_route_take_master(TsSourceRouteMix *mix);

#endif
