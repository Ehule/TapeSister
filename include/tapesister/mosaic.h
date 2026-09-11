#ifndef TAPESISTER_MOSAIC_H
#define TAPESISTER_MOSAIC_H

#include "tapesister/audition.h"

#define TS_MOSAIC_EVENTS 128
#define TS_MOSAIC_NOTES 5
#define TS_MOSAIC_HISTORY 32
#define TS_MOSAIC_GUTTER 8.0
#define TS_MOSAIC_PEAKS 2048

/* Sources are immutable. Model mutations require the host's audio lock;
   source allocation, collection, project IO and editor work stay on the UI thread. */
typedef struct TsMosaicSource {
    TsSample sample;
    uint64_t hash;
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
    float gain;
} TsMosaicEvent;

typedef struct {
    uint64_t id, revision;
    double start;
    double position[TS_MOSAIC_NOTES], step[TS_MOSAIC_NOTES], travel[TS_MOSAIC_NOTES];
    int direction[TS_MOSAIC_NOTES], intro[TS_MOSAIC_NOTES];
    uint64_t attack;
    int active;
    float audible_gain;
} TsMosaicVoice;

typedef struct TsMosaic {
    TsMosaicEvent events[TS_MOSAIC_EVENTS];
    TsMosaicVoice voices[TS_MOSAIC_EVENTS];
    TsMosaicEvent history[TS_MOSAIC_HISTORY][TS_MOSAIC_EVENTS];
    int history_count, history_cursor;
    TsMosaicSource *sources;
    uint64_t next_id, revision, epoch;
    double time;
    int playing, repeat, rate;
    float gain;
    TsStereoFrame last_output, transition_from;
    int was_playing;
    unsigned transition_remaining, transition_total;
} TsMosaic;

TsMosaic *ts_mosaic_create(void);
void ts_mosaic_free(TsMosaic *m);
TsMosaicEvent *ts_mosaic_find(TsMosaic *m, uint64_t id);
TsMosaicSource *ts_mosaic_source(TsMosaic *m, const TsSample *sample,
                                char *error, size_t size);
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
double ts_mosaic_snap(const TsMosaic *m, uint64_t id, double time, double tolerance);
double ts_mosaic_end(const TsMosaic *m);
void ts_mosaic_seek(TsMosaic *m, double time);
TsStereoFrame ts_mosaic_read(TsMosaic *m, int output_rate);
uint64_t ts_mosaic_hash(const TsMosaic *m);
/* Project-data directory; optional on load for pre-Mosaic projects. */
int ts_mosaic_save(const TsMosaic *m, const char *directory, char *error, size_t size);
int ts_mosaic_load(TsMosaic *m, const char *directory, char *error, size_t size);

#endif
