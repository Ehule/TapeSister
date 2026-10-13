#ifndef TAPESISTER_MANDALA_H
#define TAPESISTER_MANDALA_H

#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>

/* Project identity, independent of filenames, displays and frame history. */
typedef struct {
    uint64_t seed;
    uint32_t version, palette, symmetry, cycle_seconds;
} TsMandalaConfig;
void ts_mandala_default(TsMandalaConfig *config);
void ts_mandala_reseed(TsMandalaConfig *config);
int ts_mandala_valid(const TsMandalaConfig *config);
int ts_mandala_write(FILE *file, const TsMandalaConfig *config);
int ts_mandala_read(TsMandalaConfig *config, const char *key, const char *value);

enum { TS_MANDALA_SAMPLES = 64, TS_MANDALA_CHANNELS = 16, TS_MANDALA_QUEUE = 4 };
typedef struct {
    unsigned count, channels;
    float samples[TS_MANDALA_SAMPLES][TS_MANDALA_CHANNELS];
} TsMandalaPacket;
/* Exactly one serialized callback writer and one UI reader. Neither resets
   indices during use. release/acquire transfers exclusive ownership of a slot.
   A full queue drops the new packet. Consumer drains at most four slots. */
typedef struct {
    atomic_uint enabled, write, read, dropped;
    TsMandalaPacket packet[TS_MANDALA_QUEUE];
} TsMandalaQueue;
void ts_mandala_queue_init(TsMandalaQueue *queue);
int ts_mandala_lock_free(const TsMandalaQueue *queue);
void ts_mandala_push(TsMandalaQueue *queue, const float *output, unsigned frames, unsigned channels);
int ts_mandala_latest(TsMandalaQueue *queue, TsMandalaPacket *packet);

typedef struct { float level, peak, width, balance, onset; } TsMandalaFeatures;
void ts_mandala_analyze(const TsMandalaPacket *packet, TsMandalaFeatures *features, float dt);

enum { TS_MANDALA_REVERB, TS_MANDALA_DELAY, TS_MANDALA_DISTORTION,
       TS_MANDALA_GRAIN, TS_MANDALA_PRISM, TS_MANDALA_EFFECTS };
/* Artistic controls, reduced on the UI thread and smoothed in the renderer. */
typedef struct { float wet[TS_MANDALA_EFFECTS], size[TS_MANDALA_EFFECTS], character[TS_MANDALA_EFFECTS]; } TsMandalaEffects;
void ts_mandala_effects_follow(TsMandalaEffects *current, const TsMandalaEffects *target, float dt);

#endif
