#ifndef TAPESISTER_MOSAIC_H
#define TAPESISTER_MOSAIC_H

#include "tapesister/audition.h"
#include "tapesister/source_route.h"

#define TS_MOSAIC_EVENTS 128
#define TS_MOSAIC_NOTES 5
#define TS_MOSAIC_HISTORY 32
#define TS_MOSAIC_GUTTER 8.0
#define TS_MOSAIC_PEAKS 2048
#define TS_MOSAIC_ENVELOPE_POINTS 257
#define TS_MOSAIC_VIEWS 5

typedef struct {
    int stored;
    double scroll, xscroll, scale, hscale;
} TsMosaicView;

typedef enum {
    TS_MOSAIC_ENV_RESET, TS_MOSAIC_ENV_SMOOTH, TS_MOSAIC_ENV_MATCH_START,
    TS_MOSAIC_ENV_MATCH_END, TS_MOSAIC_ENV_RAMP
} TsMosaicEnvelopeAction;

/* Sources are immutable. Model mutations require the host's audio lock;
   source allocation, collection, project IO and editor work stay on the UI thread. */
typedef struct TsMosaicSource {
    TsSample sample,eq_dry;
    TsMasterEqControls eq;
    uint64_t hash,eq_dry_hash;
    unsigned pins;
    float peaks[TS_MOSAIC_PEAKS], rms[TS_MOSAIC_PEAKS];
    struct TsMosaicSource *next;
} TsMosaicSource;

typedef struct {
    uint64_t id, revision;
    char name[64];
    TsMosaicSource *source;
    double start, duration, x, width;
    size_t first, last, crossfade;
    TsLoopMode mode;
    TsTuning tuning;
    int looping, note_count, notes[TS_MOSAIC_NOTES];
    int muted, solo;
    float gain, pan;
    double fade_in, fade_out; /* Seconds on the arrangement timeline. */
    TsSourceRoute output_route; /* matrix_enabled: custom; otherwise Mosaic bus. */
} TsMosaicEvent;

typedef struct {
    uint64_t id, revision;
    double start;
    double position[TS_MOSAIC_NOTES], step[TS_MOSAIC_NOTES], travel[TS_MOSAIC_NOTES];
    int direction[TS_MOSAIC_NOTES], intro[TS_MOSAIC_NOTES];
    uint64_t attack;
    int active;
    float audible_gain, mix_left, mix_right;
    TsSourceRouteVoice output_route;
} TsMosaicVoice;

typedef struct TsMosaic {
    TsMosaicEvent events[TS_MOSAIC_EVENTS];
    TsMosaicVoice voices[TS_MOSAIC_EVENTS];
    TsMosaicEvent history[TS_MOSAIC_HISTORY][TS_MOSAIC_EVENTS];
    double history_speed[TS_MOSAIC_HISTORY];
    float volume[TS_MOSAIC_ENVELOPE_POINTS];
    float history_volume[TS_MOSAIC_HISTORY][TS_MOSAIC_ENVELOPE_POINTS];
    int history_repeat[TS_MOSAIC_HISTORY], history_auto_space[TS_MOSAIC_HISTORY];
    int auto_space; /* New arrangements allow free placement; older files retain spacing. */
    TsMosaicView views[TS_MOSAIC_VIEWS]; /* Project navigation only; outside audio/arrangement undo. */
    uint64_t audition_id;
    double audition_time;
    int audition_stopping, audition_rate;
    TsMosaicVoice audition_voice;
    float volume_current;
    int history_count, history_cursor;
    TsMosaicSource *sources;
    uint64_t next_id, revision, epoch;
    double time;
    double speed, speed_current; /* Target and smoothed tape speed, 0.5 .. 2. */
    int playing, repeat, rate;
    float gain;
    TsStereoFrame last_output, transition_from;
    int was_playing;
    unsigned transition_remaining, transition_total;
    TsSourceRouteHandoff route_handoff;
    int route_discontinuity;
} TsMosaic;

TsMosaic *ts_mosaic_create(void);
void ts_mosaic_free(TsMosaic *m);
TsMosaicEvent *ts_mosaic_find(TsMosaic *m, uint64_t id);
TsMosaicSource *ts_mosaic_source(TsMosaic *m, const TsSample *sample,
                                char *error, size_t size);
TsMosaicSource *ts_mosaic_source_eq(TsMosaic *m,const TsSample *sample,const TsSample *dry,const TsMasterEqControls *eq,char *error,size_t size);
TsMosaicSource *ts_mosaic_source_document(TsMosaic *m,const TsInstrument *doc,char *error,size_t size);
int ts_mosaic_import_source(TsInstrument *doc,const TsMosaicSource *source,int loop,size_t first,size_t last,TsLoopMode mode,char *error,size_t size);
/* UI thread, after publishing under the audio lock. Sources absent from
   events/history/pins cannot be retained by callback voices. */
void ts_mosaic_collect(TsMosaic *m);
void ts_mosaic_checkpoint(TsMosaic *m);
int ts_mosaic_undo(TsMosaic *m, int redo);
TsMosaicEvent *ts_mosaic_add(TsMosaic *m, TsMosaicSource *source,
                            double start, double x);
TsMosaicEvent *ts_mosaic_copy(TsMosaic *m, uint64_t id, double start, double x);
void ts_mosaic_delete(TsMosaic *m, uint64_t id);
void ts_mosaic_space(TsMosaic *m, TsMosaicEvent *e);
void ts_mosaic_audition(TsMosaic *m, uint64_t id);
double ts_mosaic_snap(const TsMosaic *m, uint64_t id, double time, double tolerance);
double ts_mosaic_end(const TsMosaic *m);
void ts_mosaic_seek(TsMosaic *m, double time);
void ts_mosaic_set_speed(TsMosaic *m, double speed);
/* Envelope positions are fractions of the whole arrangement, not seconds. */
float ts_mosaic_volume_at(const TsMosaic *m, double position);
void ts_mosaic_volume_draw(TsMosaic *m, double from, float a, double to, float b);
void ts_mosaic_volume_action(TsMosaic *m, TsMosaicEnvelopeAction action);
void ts_mosaic_set_repeat(TsMosaic *m, int repeat);
TsStereoFrame ts_mosaic_read(TsMosaic *m, int output_rate);
/* Returns the Mosaic bus; custom cards go only into routes. NULL ignores routing. */
TsStereoFrame ts_mosaic_read_routed(TsMosaic *m, int output_rate, TsSourceRouteMix *routes);
uint64_t ts_mosaic_hash(const TsMosaic *m);
/* Project-data directory; optional on load for pre-Mosaic projects. */
int ts_mosaic_save(const TsMosaic *m, const char *directory, char *error, size_t size);
int ts_mosaic_load(TsMosaic *m, const char *directory, char *error, size_t size);

#endif
