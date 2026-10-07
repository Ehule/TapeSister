#ifndef TAPESISTER_SPATIAL_H
#define TAPESISTER_SPATIAL_H

#include "tapesister/sample.h"
#include <stdio.h>

enum { TS_SPATIAL_SPEAKERS = 16, TS_SPATIAL_PARAMS = 5 };
enum { TS_SPATIAL_ROTATE, TS_SPATIAL_SPREAD, TS_SPATIAL_AIM,
       TS_SPATIAL_AMOUNT, TS_SPATIAL_DIRECTIVITY };
enum { TS_SPATIAL_FOCUS, TS_SPATIAL_PRESS, TS_SPATIAL_PUSH, TS_SPATIAL_ZOOM };
/* Normalized performance values: rotation/aim 0..1 = -180..180 degrees,
   spread 0..1 = 0..360 degrees, amount 0..1 = 0..90 degrees. */
typedef struct {
    int enabled, mono, speakers, point, decoder, transform, stereo_pair;
    float value[TS_SPATIAL_PARAMS];
    float azimuth[TS_SPATIAL_SPEAKERS], trim_db[TS_SPATIAL_SPEAKERS];
    float delay_ms[TS_SPATIAL_SPEAKERS];
    int output[TS_SPATIAL_SPEAKERS]; /* Zero-based hardware channel. */
    float lfo_rate, lfo_depth, rise_length, rise_depth;
    unsigned lfo_targets, rise_targets, rise_trigger;
    int rise_loop;
    float state[2][TS_SPATIAL_PARAMS], morph_seconds;
    unsigned captured, morph_trigger;
    int morph_target;
} TsSpatialControls;

typedef struct {
    float value[TS_SPATIAL_PARAMS], peak[TS_SPATIAL_SPEAKERS];
    float lfo, rise, morph, wet;
    int available, stereo_available, layout_valid, conflict, morph_active, test_speaker;
    unsigned channels;
} TsSpatialView;

typedef struct {
    TsSpatialControls controls;
    TsSpatialView view;
    float decoder[TS_SPATIAL_SPEAKERS][3];
    float matrix[TS_SPATIAL_SPEAKERS][2], target[TS_SPATIAL_SPEAKERS][2];
    float dry[64][2];
    int dry_output[2], stereo_available;
    float delay_current[TS_SPATIAL_SPEAKERS];
    float morph_from[TS_SPATIAL_PARAMS], base[TS_SPATIAL_PARAMS];
    float *delay;
    size_t delay_frames, write;
    unsigned sample_rate, hop, rise_seen, morph_seen;
    double lfo_phase, rise_phase, morph_elapsed, test_phase;
    float wet, smoothing, meter_decay, limit_gain, test_gain;
    unsigned test_remaining;
    int test_speaker, layout_valid, morph_active, rise_active, ready;
} TsSpatial;

void ts_spatial_default(TsSpatialControls *c);
void ts_spatial_sanitize(TsSpatialControls *c);
void ts_spatial_ring(TsSpatialControls *c, int count, int point);
int ts_spatial_decoder(const TsSpatialControls *c, float matrix[TS_SPATIAL_SPEAKERS][3]);
void ts_spatial_init(TsSpatial *s);
int ts_spatial_prepare(TsSpatial *s, unsigned sample_rate);
void ts_spatial_free(TsSpatial *s);
void ts_spatial_set(TsSpatial *s, const TsSpatialControls *c);
/* Project recall starts modulation from a defined phase without firing a trigger. */
void ts_spatial_recall(TsSpatial *s, const TsSpatialControls *c);
void ts_spatial_test(TsSpatial *s, int speaker);
/* Render into an existing hardware frame; unassigned Insert channels are
   preserved. Caller has already written legacy stereo + external Insert. */
void ts_spatial_process(TsSpatial *s, TsStereoFrame input, float *output,
                        unsigned channels, int shared_send_pair);
/* FuMa WXYZ, for the field diagram and future independently encoded sources. */
void ts_spatial_transform(float field[4], const float value[TS_SPATIAL_PARAMS], int transform);
const char *ts_spatial_parameter_name(int parameter);
const char *ts_spatial_transform_name(int transform);
int ts_spatial_write(FILE *f, const TsSpatialControls *c);
int ts_spatial_read(TsSpatialControls *c, const char *key, const char *value);

#endif
