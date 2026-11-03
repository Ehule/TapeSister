#ifndef TAPESISTER_CHARACTER_FILTER_H
#define TAPESISTER_CHARACTER_FILTER_H
#include "tapesister/sample.h"
#include <stdint.h>
#include <stdio.h>
enum { TS_CF_LINKED, TS_CF_SPREAD, TS_CF_DUAL };
enum { TS_CF_MS20, TS_CF_POLIVOKS };
enum {
 TS_CF_ROUTE, TS_CF_L_MODEL, TS_CF_L_MODE, TS_CF_L_CUT, TS_CF_L_RES,
 TS_CF_L_DRIVE, TS_CF_L_HP, TS_CF_L_HP_RES, TS_CF_L_TRIM,
 TS_CF_R_MODEL, TS_CF_R_MODE, TS_CF_R_CUT, TS_CF_R_RES,
 TS_CF_R_DRIVE, TS_CF_R_HP, TS_CF_R_HP_RES, TS_CF_R_TRIM,
 TS_CF_SPREAD_AMOUNT, TS_CF_LFO_RATE, TS_CF_LFO_DEPTH, TS_CF_LFO_PHASE,
 TS_CF_ENV_AMOUNT, TS_CF_ENV_ATTACK, TS_CF_ENV_RELEASE, TS_CF_FOCUS, TS_CF_COUNT
};
enum { TS_CF_MIDI_BASE=4000, TS_CF_MIDI_STRIDE=32, TS_CF_PRESETS=7, TS_CF_STATES=5 };
typedef struct {
 float value[TS_CF_COUNT], state[TS_CF_STATES][TS_CF_COUNT];
 uint32_t locks;
 uint8_t initialized, preset, captured, selected;
} TsCharacterFilterControls;
typedef struct { float band, low; } TsCharacterFilterPole;
typedef struct {
 TsCharacterFilterPole ms_lp,ms_hp,polivoks;
 float model_mix,mode_mix,previous,dc_x,dc_y;
 float g_lp,g_hp,g_poli,drive,trim,res,hp_res;
 float target_lp,target_hp,target_drive,target_trim,target_res,target_hp_res;
} TsCharacterFilterChannel;
typedef struct {
 TsCharacterFilterChannel channel[2];
 float smooth[TS_CF_COUNT], smoothing, envelope[2],attack,release,phase_step,gain_target;
 double phase;
 float mix,gain,dc_radius; unsigned rate,clock;
 int initialized;
} TsCharacterFilter;
void ts_character_filter_default(TsCharacterFilterControls *c);
void ts_character_filter_sanitize(TsCharacterFilterControls *c);
int ts_character_filter_valid(const TsCharacterFilterControls *c);
int ts_character_filter_write(FILE *f,const char *key,const TsCharacterFilterControls *c);
int ts_character_filter_read(TsCharacterFilterControls *c,const char *text);
const char *ts_character_filter_preset_name(int index);
void ts_character_filter_preset(TsCharacterFilterControls *c,int index);
void ts_character_filter_capture(TsCharacterFilterControls *c,int index);
int ts_character_filter_recall(TsCharacterFilterControls *c,int index);
void ts_character_filter_set(TsCharacterFilterControls *c,int field,float normalized);
float ts_character_filter_cutoff(float normalized);
float ts_character_filter_cutoff_normalized(float hz);
void ts_character_filter_label(const TsCharacterFilterControls *c,int field,char *s,unsigned size);
/* Editor commands are negative; fields are TS_CF_* or 100/101 for mix/output. */
int ts_character_filter_hit(const TsCharacterFilterControls *c,int x,int y,float *value);
void ts_character_filter_init(TsCharacterFilter *f,unsigned rate);
TsStereoFrame ts_character_filter_process(TsCharacterFilter *f,const TsCharacterFilterControls *c,
 TsStereoFrame input,float mix,float gain_db,float gate);
#endif
