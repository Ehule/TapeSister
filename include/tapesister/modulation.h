#ifndef TAPESISTER_MODULATION_H
#define TAPESISTER_MODULATION_H
#include <stdint.h>
#include <stdio.h>

/* Value-owned controls participate in the existing project/undo snapshots.
   Four lanes are initially visible; additional lanes use the same editor. */
enum { TS_MOD_LANES=16, TS_MOD_POINTS=64, TS_MOD_MAPS=64, TS_MOD_VISIBLE=4 };
enum { TS_MOD_LOOP, TS_MOD_ONESHOT };
enum { TS_MOD_FREE, TS_MOD_TEMPO };
enum { TS_MOD_A, TS_MOD_B, TS_MOD_C, TS_MOD_MIX, TS_MOD_GAIN,
       TS_MOD_FILTER=16, TS_MOD_MACRO=64, TS_MOD_MORPH=68, TS_MOD_COMP=80 };
typedef struct { float x,y; } TsModPoint;
typedef struct {
    char name[24];
    unsigned enabled,mode,timing,count;
    float seconds,beats,smooth;
    unsigned unlinked;float frequency,cycles; /* Authored template cycles; seconds defines the visible window. */
    uint32_t seed;
    TsModPoint point[TS_MOD_POINTS];
    unsigned detail_count;TsModPoint detail[TS_MOD_POINTS];
} TsModLane;
typedef struct {
    uint32_t target,child; /* Stable Matrix and pedal identities, never row indices. */
    unsigned type,field,lane,enabled;
    float low,high;
} TsModMap;
typedef struct {
    unsigned initialized,count,map_count;float audio_amount;
    uint32_t retrigger; /* Live command serial; deliberately not serialized. */
    TsModLane lane[TS_MOD_LANES];
    TsModMap map[TS_MOD_MAPS];
} TsModControls;
typedef struct { double phase[TS_MOD_LANES]; uint32_t retrigger; } TsModState;
void ts_mod_default(TsModControls *c);
void ts_mod_curve_changed(TsModLane *lane);
void ts_mod_bake(TsModLane *lane);
void ts_mod_resize(TsModLane *lane,unsigned points);
void ts_mod_lane_default(TsModLane *lane,unsigned index);
int ts_mod_valid(const TsModControls *c);
float ts_mod_value(const TsModLane *lane,double phase);
void ts_mod_advance(TsModState *s,const TsModControls *c,unsigned frames,unsigned rate,double quarter);
void ts_mod_shape(TsModLane *lane,int shape,unsigned points); /* 0 sine, 1 triangle, 2 random, 3 square */
int ts_mod_write(FILE *f,const char *prefix,const TsModControls *c);
int ts_mod_read(TsModControls *c,const char *key,const char *value);
#endif
