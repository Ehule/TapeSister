#ifndef TAPESISTER_ROUTER_H
#define TAPESISTER_ROUTER_H

#include "tapesister/sample.h"
#include <stdint.h>
#include <stdio.h>

/* Stable IDs: add future serial processors here, never persist row indices. */
enum { TS_ROUTER_PRISM, TS_ROUTER_SISTER, TS_ROUTER_FALLOUT,
       TS_ROUTER_PEDALBOARD, TS_ROUTER_COUNT };
typedef struct {
    int order[TS_ROUTER_COUNT];
    unsigned bypass_mask;
    int solo; /* Zero, or one stable processor ID + 1. */
} TsRouterControls;

typedef struct {
    TsRouterControls controls;
    int order[TS_ROUTER_COUNT];
    float wet[TS_ROUTER_COUNT], input_peak[TS_ROUTER_COUNT], output_peak[TS_ROUTER_COUNT];
    float source_peak, master_peak, gain, step, decay;
    unsigned sample_rate;
    int handoff; /* -1 fades out, +1 fades in. Never runs two DSP histories. */
} TsRouter;
typedef TsStereoFrame (*TsRouterProcess)(void *context,int stage,TsStereoFrame input);

void ts_router_default(TsRouterControls *controls);
int ts_router_valid(const TsRouterControls *controls);
void ts_router_sanitize(TsRouterControls *controls);
int ts_router_active(const TsRouterControls *controls,int stage);
void ts_router_move(TsRouterControls *controls,int from,int to);
void ts_router_toggle_bypass(TsRouterControls *controls,int stage);
void ts_router_toggle_solo(TsRouterControls *controls,int stage);
const char *ts_router_name(int stage);
void ts_router_init(TsRouter *router);
void ts_router_prepare(TsRouter *router,unsigned sample_rate);
void ts_router_set(TsRouter *router,const TsRouterControls *controls);
TsStereoFrame ts_router_process(TsRouter *router,TsStereoFrame input,
                                TsRouterProcess process,void *context);
int ts_router_write(FILE *file,const TsRouterControls *controls);
int ts_router_read(TsRouterControls *controls,const char *key,const char *value);

#endif
