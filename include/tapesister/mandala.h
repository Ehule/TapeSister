#ifndef TAPESISTER_MANDALA_H
#define TAPESISTER_MANDALA_H

#include <stdint.h>
#include <stdatomic.h>
#include <stdio.h>

enum { TS_MANDALA_PRESETS = 5, TS_MANDALA_CUSTOM = 5, TS_MANDALA_COLORS = 8,
       TS_MANDALA_BACKGROUND = 7, TS_MANDALA_SHAPES = 5 };
/* Project identity and appearance, independent of displays and frame history.
   RGB colors are 0xRRGGBB; the eighth swatch is the background. */
typedef struct {
    uint64_t seed;
    uint32_t version, palette, symmetry, cycle_seconds;
    uint32_t shape, density, scale, trails, motion;
    uint32_t custom_rgb[TS_MANDALA_COLORS];
    uint32_t video_enabled, video_loop, video_mix, video_scale, video_rotation, video_response;
    char video_path[1024];
} TsMandalaConfig;
const char *ts_mandala_palette_name(unsigned palette);
const char *ts_mandala_shape_name(unsigned shape);
uint32_t ts_mandala_color(const TsMandalaConfig *config, unsigned swatch);
void ts_mandala_set_color(TsMandalaConfig *config, unsigned swatch, uint32_t rgb);
void ts_mandala_reset_geometry(TsMandalaConfig *config);
void ts_mandala_default(TsMandalaConfig *config);
void ts_mandala_reseed(TsMandalaConfig *config);
int ts_mandala_valid(const TsMandalaConfig *config);
int ts_mandala_write(FILE *file, const TsMandalaConfig *config);
int ts_mandala_read(TsMandalaConfig *config, const char *key, const char *value);

enum { TS_MANDALA_SAMPLES = 64, TS_MANDALA_CHANNELS = 255,
       TS_MANDALA_VALUES = 64*16, TS_MANDALA_QUEUE = 4 };
typedef struct {
    unsigned count, channels;
    /* Interleaved physical channels. Wider devices use fewer sampled frames,
       retaining the original 1024-float copy budget and packet size. */
    float samples[TS_MANDALA_VALUES];
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
       TS_MANDALA_GRAIN, TS_MANDALA_PRISM, TS_MANDALA_TRANSIENT, TS_MANDALA_EFFECTS };
/* Artistic controls, reduced on the UI thread and smoothed in the renderer. */
typedef struct { float wet[TS_MANDALA_EFFECTS], size[TS_MANDALA_EFFECTS], character[TS_MANDALA_EFFECTS]; } TsMandalaEffects;
void ts_mandala_effects_follow(TsMandalaEffects *current, const TsMandalaEffects *target, float dt);

#endif
