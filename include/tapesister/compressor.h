#ifndef TAPESISTER_COMPRESSOR_H
#define TAPESISTER_COMPRESSOR_H
#include "tapesister/sample.h"
#include <stdio.h>

typedef struct {
    int enabled, external;
    float threshold_db, ratio, attack_ms, release_ms, output_db;
} TsCompressorControls;
typedef struct {
    TsCompressorControls controls;
    unsigned rate;
    double threshold, slope, attack, release, output, external, wet;
    double target_attack, target_release, target_output, smooth;
    double gain;
    float reduction_db, key_peak;
} TsCompressor;
void ts_compressor_default(TsCompressorControls *c);
int ts_compressor_valid(const TsCompressorControls *c);
void ts_compressor_init(TsCompressor *c);
void ts_compressor_prepare(TsCompressor *c,unsigned rate);
/* Control thread under the existing device exclusion; no allocation. */
void ts_compressor_set(TsCompressor *c,const TsCompressorControls *controls);
/* Stereo-linked, feed-forward, fixed 6 dB knee, no lookahead or audio delay. */
TsStereoFrame ts_compressor_process(TsCompressor *c,TsStereoFrame input,TsStereoFrame key);
float ts_compressor_normalized(const TsCompressorControls *c,int control);
void ts_compressor_set_normalized(TsCompressorControls *c,int control,float value);
int ts_compressor_write(FILE *f,const TsCompressorControls *c);
int ts_compressor_read(TsCompressorControls *c,const char *key,const char *value);
#endif
