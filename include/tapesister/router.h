#ifndef TAPESISTER_ROUTER_H
#define TAPESISTER_ROUTER_H

#include "tapesister/sample.h"
#include <stdint.h>
#include <stdio.h>

/* Stable IDs: add future serial processors here, never persist row indices. */
enum { TS_ROUTER_PRISM, TS_ROUTER_SISTER, TS_ROUTER_FALLOUT,
       TS_ROUTER_PEDALBOARD, TS_ROUTER_INSERT, TS_ROUTER_COUNT };
typedef struct {
    int order[TS_ROUTER_COUNT];
    unsigned bypass_mask;
    int solo; /* Zero, or one stable processor ID + 1. */
} TsRouterControls;

enum { TS_ROUTER_STATES=26, TS_ROUTER_STEPS=64 };
#define TS_ROUTER_TIME_MIN .05f
#define TS_ROUTER_TIME_MAX 3600.f
typedef struct { unsigned bypass_mask; int solo; } TsRouterState;
typedef struct { int state; float seconds; } TsRouterStep; /* -1 = HOLD */
typedef struct {
    TsRouterState state[TS_ROUTER_STATES];
    unsigned occupied;
    TsRouterStep step[TS_ROUTER_STEPS];
    int length, loop;
    float max_seconds, timer_seconds[TS_ROUTER_COUNT];
    int timer_after[TS_ROUTER_COUNT];
} TsRouterPerformance;
enum { TS_ROUTER_TIMER_NONE, TS_ROUTER_TIMER_BYPASS, TS_ROUTER_TIMER_SOLO };
typedef struct { int kind, after; uint64_t frames; unsigned priority; } TsRouterTimer;
typedef struct {
    TsRouterState base, before_sequence, sequenced, manual;
    TsRouterTimer timer[TS_ROUTER_COUNT];
    uint64_t frames;
    double frame_fraction; /* Carry fractional samples across steps; no cumulative rounding drift. */
    int running, step, state, missing, restore_valid, manual_override;
    unsigned timer_mask;
} TsRouterTransport;
typedef struct {
    int running, step, state, missing, restore_valid, manual_override;
    float remaining;
    int timer_kind[TS_ROUTER_COUNT], timer_after[TS_ROUTER_COUNT];
    float timer_remaining[TS_ROUTER_COUNT];
} TsRouterView;

typedef struct {
    TsRouterControls controls;
    int order[TS_ROUTER_COUNT];
    float wet[TS_ROUTER_COUNT], input_peak[TS_ROUTER_COUNT], output_peak[TS_ROUTER_COUNT];
    float source_peak, master_peak, gain, step, decay;
    unsigned sample_rate;
    int handoff; /* -1 fades out, +1 fades in. Never runs two DSP histories. */
    TsRouterPerformance performance;
    TsRouterTransport transport;
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
/* Optional boundary preparation occurs before a stage's bypass crossfade. */
TsStereoFrame ts_router_process_with_prepare(TsRouter *router,TsStereoFrame input,
    TsRouterProcess process,TsRouterProcess prepare,void *context);
int ts_router_write(FILE *file,const TsRouterControls *controls);
int ts_router_read(TsRouterControls *controls,const char *key,const char *value);

/* UI commands require the existing audio-device exclusion. No allocation,
   device operations or UI clock are used by the audio-owned transport. */
void ts_router_performance_default(TsRouterPerformance *performance);
int ts_router_performance_valid(const TsRouterPerformance *performance);
int ts_router_performance_set(TsRouter *router,const TsRouterPerformance *performance);
TsRouterControls ts_router_export(const TsRouter *router); /* Underlying state, no temporary overlays. */
void ts_router_takeover(TsRouter *router); /* Hold effective state, cancel all automation. */
void ts_router_reorder(TsRouter *router,int from,int to);
void ts_router_manual(TsRouter *router,int stage,int solo);
int ts_router_store(TsRouter *router,int slot);
int ts_router_recall(TsRouter *router,int slot);
int ts_router_timer_start(TsRouter *router,int stage,int kind);
void ts_router_timer_cancel(TsRouter *router,int stage);
void ts_router_sequence_play(TsRouter *router);
void ts_router_sequence_stop(TsRouter *router);
void ts_router_sequence_reset(TsRouter *router);
int ts_router_sequence_restore(TsRouter *router);
void ts_router_performance_advance(TsRouter *router,uint64_t frames);
void ts_router_performance_rate(TsRouter *router,unsigned rate);
TsRouterView ts_router_view(const TsRouter *router);
int ts_router_performance_write(FILE *file,const TsRouterPerformance *performance);
int ts_router_performance_read(TsRouterPerformance *performance,const char *key,const char *value);

#endif
