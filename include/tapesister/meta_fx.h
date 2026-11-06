#ifndef TAPESISTER_META_FX_H
#define TAPESISTER_META_FX_H
#include "tapesister/sister_post_fx.h"
enum { TS_META_STAGES = 8, TS_META_MACROS = 4, TS_META_MAPS = 24, TS_META_PRESETS = 10 };
/* Flat, bounded racks: a child is an existing pedal, never another rack. */
typedef struct {
    int type, enabled;
    float value[5]; /* A, B, C, Mix, normalized Output (-12..+12 dB). */
    float filter[TS_CF_COUNT];
    uint8_t power;
} TsMetaStage;
typedef struct {
    uint8_t macro, stage, field;
    float low, high;
} TsMetaMap;
typedef struct {
    char name[32], label[TS_META_MACROS][16];
    int initialized, count, map_count, preset, scene_mode, captured;
    float macro[TS_META_MACROS], scene[2][TS_META_MACROS], morph;
    TsMetaStage stage[TS_META_STAGES];
    TsMetaMap map[TS_META_MAPS];
} TsMetaControls;
typedef struct TsMetaEngine TsMetaEngine;
void ts_meta_default(TsMetaControls *);
int ts_meta_valid(const TsMetaControls *);
int ts_meta_same_topology(const TsMetaControls *, const TsMetaControls *);
const char *ts_meta_preset_name(int);
void ts_meta_preset(TsMetaControls *, int);
void ts_meta_macro_set(TsMetaControls *, int, float);
void ts_meta_capture(TsMetaControls *, int);
void ts_meta_stage_default(TsMetaStage *, int);
float ts_meta_stage_value(const TsMetaStage *, int);
void ts_meta_stage_set(TsMetaStage *, int, float);
const char *ts_meta_parameter_name(int type, int field);
int ts_meta_target_field(int type, int field);
void ts_meta_from_board(TsMetaControls *, const TsSisterFxControls *, int, int);
/* One line per record, all lines under the ordinary 4096-byte project limit. */
int ts_meta_write(FILE *, const char *prefix, const TsMetaControls *);
int ts_meta_read(TsMetaControls *, const char *field, const char *value);
int ts_meta_save(const TsMetaControls *, const char *, char *, size_t);
int ts_meta_load(TsMetaControls *, const char *, char *, size_t);
TsMetaEngine *ts_meta_new(const TsMetaControls *, unsigned, uint32_t);
void ts_meta_free(TsMetaEngine *);
void ts_meta_set(TsMetaEngine *, const TsMetaControls *);
int ts_meta_topology_matches(const TsMetaEngine *, const TsMetaControls *);
TsStereoFrame ts_meta_process(TsMetaEngine *, TsStereoFrame);
size_t ts_meta_memory(const TsMetaEngine *);
#endif
