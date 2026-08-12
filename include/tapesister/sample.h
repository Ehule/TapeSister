#ifndef TAPESISTER_SAMPLE_H
#define TAPESISTER_SAMPLE_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    float *data;
    size_t frames;
    uint32_t sample_rate;
    char name[128];
} TsSample;

typedef struct {
    uint32_t seed;
    float body;
    float edge;
    float drift;
    float seconds;
    float frequency;
} TsRecipe;

void ts_sample_init(TsSample *sample);
void ts_sample_free(TsSample *sample);
int ts_sample_load_wav(TsSample *sample, const char *path, char *error, size_t error_size);
int ts_sample_save_wav16(const TsSample *sample, const char *path, char *error, size_t error_size);
int ts_sample_generate(TsSample *sample, const TsRecipe *recipe, char *error, size_t error_size);
float ts_sample_peak(const TsSample *sample);
uint64_t ts_sample_hash(const TsSample *sample);

#endif
