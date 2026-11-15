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
typedef struct TsSunVoxBatch TsSunVoxBatch;
typedef struct { TsSunVoxEngine *engine; uint64_t ticks; int timed; } TsSunVoxJob;
/* Optional one-worker scheduler. Construction/destruction are control-only.
   Default is serial. Every submitted quantum finishes before render returns. */
void ts_sunvox_workers_configure(unsigned workers);
unsigned ts_sunvox_workers_configured(void);
TsSunVoxBatch *ts_sunvox_batch_new(unsigned workers);
void ts_sunvox_batch_free(TsSunVoxBatch *batch);
void ts_sunvox_batch_render(TsSunVoxBatch *batch,TsSunVoxJob *jobs,unsigned count,uint64_t (*clock)(void));
unsigned ts_sunvox_parallel_cost(const TsSunVoxEngine *engine);
uint64_t ts_sunvox_batch_dispatched(const TsSunVoxBatch *batch);
uint64_t ts_sunvox_batch_stolen(const TsSunVoxBatch *batch);
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
float ts_sunvox_normalized(const TsSunVoxControls *,int);
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
   Render it before staging the next sample. Preserve job order unless the
   scheduler's SDK admission check permits parallel work. Output latency remains
   exactly one existing quantum. */
unsigned ts_sunvox_span_limit(const TsSunVoxEngine *s);
/* Interleaved stereo; frames must not exceed the next render boundary. */
int ts_sunvox_stage_span(TsSunVoxEngine *s,const float *input,float *wet,float *dry,unsigned frames);
int ts_sunvox_stage_frame(TsSunVoxEngine *s,float left,float right,float wet[2],float dry[2]);
void ts_sunvox_render_pending(TsSunVoxEngine *s);
size_t ts_sunvox_memory(const TsSunVoxEngine *s);
#ifdef __cplusplus
}
#endif
#endif
