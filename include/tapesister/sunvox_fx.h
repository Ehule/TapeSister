#ifndef TAPESISTER_SUNVOX_FX_H
#define TAPESISTER_SUNVOX_FX_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { TS_SUNVOX_CONTROLS=128, TS_SUNVOX_QUANTUM=64, TS_SUNVOX_DEFAULT_QUANTUM=256, TS_SUNVOX_MAX_QUANTUM=1024 };
typedef struct {
    uint64_t patch;
    int value[TS_SUNVOX_CONTROLS]; /* -1 retains the patch's saved value. */
    int note, velocity, held;
    int quantum; /* 0/64 legacy, 256 balanced, 1024 economy; fixed for an engine. */
    int ready; /* Codec completion is per instance, including cached imports. */
    size_t received;
} TsSunVoxControls;
typedef struct {
    char name[80], unit[160];
    int minimum, maximum, initial, offset, discrete;
} TsSunVoxParameter;
typedef struct TsSunVoxEngine TsSunVoxEngine;
int ts_sunvox_factory_count(void);
const char *ts_sunvox_factory_name(int index);
const char *ts_sunvox_factory_category(int index);
void ts_sunvox_default(TsSunVoxControls *c);
unsigned ts_sunvox_quantum(const TsSunVoxControls *c);
int ts_sunvox_select(TsSunVoxControls *c,int index);
int ts_sunvox_import(TsSunVoxControls *c,const char *path);
/* UTF-8 paths; accepts quoted clipboard paths. Failure leaves c unchanged. */
int ts_sunvox_import_ex(TsSunVoxControls *c,const char *path,char *error,size_t error_size);
const char *ts_sunvox_name(const TsSunVoxControls *c);
const char *ts_sunvox_category(const TsSunVoxControls *c);
int ts_sunvox_valid(const TsSunVoxControls *c);
int ts_sunvox_parameter_count(const TsSunVoxControls *c);
int ts_sunvox_parameter(const TsSunVoxControls *c,int index,TsSunVoxParameter *out);
int ts_sunvox_write(FILE *file,const char *prefix,const TsSunVoxControls *c);
int ts_sunvox_read(TsSunVoxControls *c,const char *key,const char *value);
TsSunVoxEngine *ts_sunvox_new(const TsSunVoxControls *c,unsigned rate);
void ts_sunvox_free(TsSunVoxEngine *s);
/* A quantum change requires a newly staged engine, like a patch change. */
void ts_sunvox_set(TsSunVoxEngine *s,const TsSunVoxControls *c);
/* Returns delayed wet and dry together so wet/dry and bypass stay aligned. */
void ts_sunvox_process(TsSunVoxEngine *s,float left,float right,float wet[2],float dry[2]);
/* Buffered boundary: stage returns 1 when a complete input quantum is ready.
   Render it before staging the next sample. Independent engines may render in
   any order; output latency remains exactly one existing quantum. */
int ts_sunvox_stage_frame(TsSunVoxEngine *s,float left,float right,float wet[2],float dry[2]);
void ts_sunvox_render_pending(TsSunVoxEngine *s);
size_t ts_sunvox_memory(const TsSunVoxEngine *s);
#ifdef __cplusplus
}
#endif
#endif
