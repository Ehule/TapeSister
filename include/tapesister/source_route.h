#ifndef TAPESISTER_SOURCE_ROUTE_H
#define TAPESISTER_SOURCE_ROUTE_H

#include "tapesister/sample.h"
#include "tapesister/source_route_types.h"

typedef struct {
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

void ts_source_route_mix_init(TsSourceRouteMix *to,const TsSourceRouteMix *map);
int ts_source_route_valid(const TsSourceRoute *r, int allow_inherit);
TsSourceRoute ts_source_route_resolve(TsSourceRoute tile, TsSourceRoute track);
void ts_source_route_set(TsSourceRouteVoice *v, TsSourceRoute route, unsigned rate);
TsStereoFrame ts_source_route_frame(TsSourceRouteVoice *v, TsStereoFrame input,
                                    TsSourceRouteMix *clean);
void ts_source_route_add(TsSourceRouteMix *to, const TsSourceRouteMix *from, float gain);
TsStereoFrame ts_source_route_take_master(TsSourceRouteMix *mix);

#endif
