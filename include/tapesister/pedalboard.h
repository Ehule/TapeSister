#ifndef TAPESISTER_PEDALBOARD_H
#define TAPESISTER_PEDALBOARD_H
#include "tapesister/sister_post_fx.h"

/* A serial canvas board owns its children, not extra Matrix ports. Controls
   remain values so project snapshots, clipboard and undo make deep copies. */
typedef struct {
    int initialized, count;
    float transition;
    TsSisterFxSlotControls slot[TS_SISTER_FX_SLOT_COUNT];
} TsPedalboardControls;
typedef struct TsPedalboard TsPedalboard;
void ts_pedalboard_default(TsPedalboardControls *);
int ts_pedalboard_valid(const TsPedalboardControls *);
int ts_pedalboard_write(FILE *, const char *, const TsPedalboardControls *);
int ts_pedalboard_read(TsPedalboardControls *, const char *, const char *);
TsPedalboard *ts_pedalboard_new(const TsPedalboardControls *, unsigned, uint32_t);
void ts_pedalboard_free(TsPedalboard *);
int ts_pedalboard_topology_matches(const TsPedalboard *, const TsPedalboardControls *);
void ts_pedalboard_set(TsPedalboard *, const TsPedalboardControls *);
TsStereoFrame ts_pedalboard_process(TsPedalboard *, TsStereoFrame);
void ts_pedalboard_process_span(TsPedalboard *, const TsStereoFrame *, TsStereoFrame *, unsigned);
size_t ts_pedalboard_memory(const TsPedalboard *);
#endif
