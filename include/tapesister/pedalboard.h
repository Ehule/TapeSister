#ifndef TAPESISTER_PEDALBOARD_H
#define TAPESISTER_PEDALBOARD_H
#include "tapesister/sister_post_fx.h"

/* A serial canvas board owns its children, not extra Matrix ports. Controls
   remain values so project snapshots, clipboard and undo make deep copies. */
typedef struct {
    int initialized, count;
    float transition;
    TsSisterFxSlotControls slot[TS_SISTER_FX_SLOT_COUNT];
    uint32_t next_id;
} TsPedalboardControls;
typedef struct TsPedalboard TsPedalboard;
typedef struct TsPedalboardSlot TsPedalboardSlot;
typedef struct {
    TsPedalboardSlot *fresh[TS_SISTER_FX_SLOT_COUNT];
    TsPedalboardSlot *garbage[2*TS_SISTER_FX_SLOT_COUNT];
    int reuse[TS_SISTER_FX_SLOT_COUNT], prepared;
} TsPedalboardPlan;
void ts_pedalboard_identify(TsPedalboardControls *);
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
int ts_pedalboard_stage(const TsPedalboard *, const TsPedalboardControls *, TsPedalboardPlan *);
void ts_pedalboard_commit(TsPedalboard *, const TsPedalboardControls *, TsPedalboardPlan *);
void ts_pedalboard_discard(TsPedalboardPlan *);
void ts_pedalboard_collect(TsPedalboard *, TsPedalboardPlan *, int offline);
int ts_pedalboard_collect_ready(const TsPedalboard *);
void ts_pedalboard_finish_edits(TsPedalboard *);
void ts_pedalboard_profile_owner(TsPedalboard *, unsigned owner, uint32_t board_id);
void ts_pedalboard_process_aligned(TsPedalboard *, const TsStereoFrame *, TsStereoFrame *, TsStereoFrame *, unsigned);
unsigned ts_pedalboard_latency(const TsPedalboard *);
/* Shared SunVox host used by canvas boards and the original P1 locations. */
TsPedalboardSlot *ts_pedalboard_slot_new(const TsSisterFxSlotControls *,unsigned,uint32_t);
void ts_pedalboard_slot_free(TsPedalboardSlot *);
int ts_pedalboard_slot_matches(const TsPedalboardSlot *,const TsSisterFxSlotControls *);
void ts_pedalboard_slot_set(TsPedalboardSlot *,const TsSisterFxSlotControls *);
void ts_pedalboard_slot_span(TsPedalboardSlot *,const TsStereoFrame *,const float *,TsStereoFrame *,unsigned);
void ts_pedalboard_slot_dry(TsPedalboardSlot *,const TsStereoFrame *,TsStereoFrame *,unsigned);
float ts_pedalboard_slot_direct(const TsPedalboardSlot *,float);
size_t ts_pedalboard_slot_memory(const TsPedalboardSlot *);
unsigned ts_pedalboard_slot_latency(const TsPedalboardSlot *);
#endif
