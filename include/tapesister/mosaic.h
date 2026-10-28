#ifndef TAPESISTER_MOSAIC_H
#define TAPESISTER_MOSAIC_H

#include "tapesister/audition.h"
#include "tapesister/pattern_card.h"
#include "tapesister/source_route.h"

#define TS_MOSAIC_EVENTS 1024
#define TS_MOSAIC_NOTES 5
#define TS_MOSAIC_HISTORY 32
#define TS_MOSAIC_GUTTER 8.0
#define TS_MOSAIC_PEAKS 2048
#define TS_MOSAIC_ENVELOPE_POINTS 257
#define TS_MOSAIC_VIEWS 5
#define TS_MOSAIC_MARKERS 16
/* Horizontal layout is independent of time. Leave room for large spaced rows,
   while bounding native pixel conversions well below their int64_t range. */
#define TS_MOSAIC_LAYOUT_LIMIT 1000000000.0
#define TS_MOSAIC_MIN_HSCALE 0.000000001

typedef enum {
    TS_MOSAIC_PITCH_KEEP, TS_MOSAIC_PITCH_CHORD, TS_MOSAIC_PITCH_SPREAD_CHORD,
    TS_MOSAIC_PITCH_SPREAD_SCALE, TS_MOSAIC_PITCH_MODE_COUNT
} TsMosaicPitchMode;
typedef enum {
    TS_MOSAIC_CHORD_PRIMARY, TS_MOSAIC_CHORD_MAJOR, TS_MOSAIC_CHORD_MINOR,
    TS_MOSAIC_CHORD_MAJ7, TS_MOSAIC_CHORD_MIN7, TS_MOSAIC_CHORD_DOM7,
    TS_MOSAIC_CHORD_SUS2, TS_MOSAIC_CHORD_SUS4, TS_MOSAIC_CHORD_COUNT
} TsMosaicChord;
typedef enum {
    TS_MOSAIC_PITCH_RANDOM, TS_MOSAIC_PITCH_ASCENDING, TS_MOSAIC_PITCH_DESCENDING,
    TS_MOSAIC_PITCH_SHUFFLED, TS_MOSAIC_PITCH_ORDER_COUNT
} TsMosaicPitchOrder;
typedef struct {
    int mode, root, scale, chord, order, low_octave, high_octave;
} TsMosaicPitchSettings;
const char *ts_mosaic_pitch_mode_name(int mode);
const char *ts_mosaic_chord_name(int chord);
const char *ts_mosaic_pitch_order_name(int order);
/* UI-only note planning. Returns sorted note choices (or complete-chord roots)
   inside C(low octave)..B(high octave), clipped to MIDI 0..127. Zero means no
   valid choice. Intervals preserve the primary voicing in CHORD mode. */
int ts_mosaic_pitch_choices(const TsMosaicPitchSettings *settings,
    const int primary[TS_MOSAIC_NOTES], int primary_count,
    int choices[128], int intervals[TS_MOSAIC_NOTES], int *voices);

typedef struct {
    int stored;
    double scroll, xscroll, scale, hscale;
} TsMosaicView;

typedef struct {
    int stored;
    double time;
    char name[32];
} TsMosaicMarker;

typedef struct {
    double start, end; /* Seconds; 0/0 means no time selection. */
} TsMosaicRange;

typedef enum {
    TS_MOSAIC_ENV_RESET, TS_MOSAIC_ENV_SMOOTH, TS_MOSAIC_ENV_MATCH_START,
    TS_MOSAIC_ENV_MATCH_END, TS_MOSAIC_ENV_RAMP
} TsMosaicEnvelopeAction;

/* Sources are immutable. Model mutations require the host's audio lock;
   source allocation, collection, project IO and editor work stay on the UI thread. */
typedef struct TsMosaicSource {
    TsSample sample,eq_dry;
    TsPatternCard *pattern;
    TsMasterEqControls eq;
    uint64_t hash,eq_dry_hash;
    unsigned pins;
    int retained; /* UI-thread source collection mark; never used by playback. */
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
    /* Live audio loops are independent players, never extra timeline voices.
       duration is the finite stop length, or only the visible footprint in INF. */
    int live, live_finite, live_tempo, live_transport, live_position;
    double live_beats; /* Source cycle in quarter notes when tempo follows master. */
} TsMosaicEvent;

enum { TS_MOSAIC_LIVE_STOPPED, TS_MOSAIC_LIVE_WAITING, TS_MOSAIC_LIVE_PLAYING,
       TS_MOSAIC_LIVE_RELEASING };
typedef struct {
    uint64_t id;
    double elapsed, phase;
    int state;
    unsigned release;
} TsMosaicLive;

typedef struct {
    uint64_t id, revision, generation;
    TsMosaicSource *pattern_source;
    double pattern_origin, pattern_switch;
    double start;
    double position[TS_MOSAIC_NOTES], step[TS_MOSAIC_NOTES], travel[TS_MOSAIC_NOTES];
    int direction[TS_MOSAIC_NOTES], intro[TS_MOSAIC_NOTES];
    uint64_t attack;
    int active;
    float audible_gain, mix_left, mix_right;
    float target_left, target_right;
    double fade_in, fade_out, release_frames;
    float audible_ramp, mix_ramp;
    size_t attack_frames;
    TsLoopMode base_mode;
    int starts_at_sample;
    int route_passthrough;
    uint64_t control_serial;
    unsigned pattern_player[TS_MOSAIC_NOTES]; /* Runtime-only backend index hints. */
    TsSourceRouteVoice output_route;
} TsMosaicVoice;

/* Audio-owned, allocation-free working set. Slots stay in storage order when
   mixing, preserving floating-point summation and per-card route ownership. */
typedef struct {
    int active[TS_MOSAIC_EVENTS], pending[TS_MOSAIC_EVENTS];
    int active_count, pending_count, any_solo, valid;
    int live[TS_MOSAIC_EVENTS], live_count;
    double end, expected_time;
    TsMosaicRange marker_range;
    uint64_t revision;
} TsMosaicSchedule;

typedef struct TsMosaic {
    void *pattern_engine;
    TsMosaicEvent events[TS_MOSAIC_EVENTS];
    TsMosaicVoice voices[TS_MOSAIC_EVENTS];
    TsMosaicEvent history[TS_MOSAIC_HISTORY][TS_MOSAIC_EVENTS];
    double history_speed[TS_MOSAIC_HISTORY];
    TsMosaicMarker markers[TS_MOSAIC_MARKERS];
    TsMosaicMarker history_markers[TS_MOSAIC_HISTORY][TS_MOSAIC_MARKERS];
    float volume[TS_MOSAIC_ENVELOPE_POINTS];
    float history_volume[TS_MOSAIC_HISTORY][TS_MOSAIC_ENVELOPE_POINTS];
    int history_repeat[TS_MOSAIC_HISTORY], history_auto_space[TS_MOSAIC_HISTORY];
    TsMosaicRange loop_range, history_loop_range[TS_MOSAIC_HISTORY];
    int auto_space; /* New arrangements allow free placement; older files retain spacing. */
    TsMosaicView views[TS_MOSAIC_VIEWS]; /* Project navigation only; outside audio/arrangement undo. */
    uint64_t audition_id;
    TsMosaicEvent audition_source; /* Transient source preview, outside arrangement/history. */
    double audition_time;
    int audition_stopping, audition_rate;
    TsMosaicVoice audition_voice;
    float volume_current;
    int history_count, history_cursor, history_first;
    TsMosaicSource *sources;
    TsMosaicSource *pattern_retired[TS_MOSAIC_EVENTS]; /* UI-owned lifetime pins. */
    uint64_t next_id, revision, epoch;
    double time;
    double speed, speed_current; /* Target and smoothed tape speed, 0.5 .. 2. */
    int playing, repeat, rate;
    int marker_loop, marker_active; /* Performance state; active is a 1-based cue, not saved. */
    float gain;
    TsStereoFrame last_output, transition_from;
    int was_playing;
    unsigned transition_remaining, transition_total;
    TsSourceRouteHandoff route_handoff;
    int route_discontinuity;
    TsMosaicSchedule schedule;
    unsigned block_remaining;
    uint64_t control_serial;
    int audition_slot;
    TsMosaicLive live[TS_MOSAIC_EVENTS]; /* Audio-owned; not restored as running. */
    double live_trigger_floor, live_quarter, live_phase;
    int live_master, live_master_running, live_clock_edge;
    struct { int source,continuous,units,length,quantize; } live_capture;
} TsMosaic;

/* Control commands require the same exclusion as event edits. Launch is
   idempotent; restart is an explicit stop+launch. Quantize waits for a master
   quarter-note edge, and works while the master transport cursor is paused. */
int ts_mosaic_live_launch(TsMosaic *,uint64_t id,int quantize);
void ts_mosaic_live_stop(TsMosaic *,uint64_t id); /* id=0 stops all live loops. */
void ts_mosaic_live_restart(TsMosaic *,uint64_t id);
void ts_mosaic_live_transport(TsMosaic *,int command); /* group: 1 play, 0 pause, 2 Home */
int ts_mosaic_live_count(const TsMosaic *);
double ts_mosaic_live_cycle(const TsMosaicEvent *);
/* Host supplies the current audio-frame clock, not a UI timer. */
void ts_mosaic_live_clock(TsMosaic *,int enabled,int running,double quarter,double phase);

/* Called under audio exclusion. Keeps current cycle alive, no event restart. */
void ts_mosaic_update_pattern(TsMosaic *,TsMosaicEvent *,TsMosaicSource *);
void ts_mosaic_pattern_retire(TsMosaic *); /* UI thread under audio exclusion. */
TsMosaic *ts_mosaic_create(void);
void ts_mosaic_free(TsMosaic *m);
/* Publish a freshly loaded, stopped model with no undo history under the audio
   lock. Its source ownership moves to m; loaded receives the retired sources
   and must be freed after unlocking. The loaded object is consumed. */
void ts_mosaic_publish_loaded(TsMosaic *m, TsMosaic *loaded);
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
/* Preflight bulk inserts under the audio lock, before checkpoint/publication.
   Extra is the number of new pattern cards; failure leaves the document intact. */
int ts_mosaic_reserve_patterns(TsMosaic *m, int extra);
TsMosaicEvent *ts_mosaic_copy(TsMosaic *m, uint64_t id, double start, double x);
/* Group placement resolves spacing once for the rigid selection afterwards. */
TsMosaicEvent *ts_mosaic_copy_unspaced(TsMosaic *m, uint64_t id, double start, double x);
void ts_mosaic_delete(TsMosaic *m, uint64_t id);
void ts_mosaic_space(TsMosaic *m, TsMosaicEvent *e);
void ts_mosaic_audition(TsMosaic *m, uint64_t id);
/* Start a source template without inserting a card. Stop with audition(m,0).
   UI calls audition(m,0) after audition_id retires to release its source reference. */
void ts_mosaic_audition_source(TsMosaic *m, const TsMosaicEvent *source);
double ts_mosaic_snap(const TsMosaic *m, uint64_t id, double time, double tolerance);
double ts_mosaic_end(const TsMosaic *m);
void ts_mosaic_seek(TsMosaic *m, double time);
/* UI-thread metadata, under host audio exclusion; one undo step per change. */
int ts_mosaic_set_marker(TsMosaic *m, int index, const TsMosaicMarker *marker);
TsMosaicRange ts_mosaic_marker_range(const TsMosaic *m, int index);
int ts_mosaic_trigger_marker(TsMosaic *m, int index);
void ts_mosaic_set_marker_loop(TsMosaic *m, int enabled);
void ts_mosaic_set_speed(TsMosaic *m, double speed);
/* Effective linear fades: overlapping saved values scale to the card duration.
   Shared by playback, the card envelope and its drag handles. */
void ts_mosaic_event_fades(const TsMosaicEvent *event, double *fade_in, double *fade_out);
/* Envelope positions are fractions of the whole arrangement, not seconds. */
float ts_mosaic_volume_at(const TsMosaic *m, double position);
void ts_mosaic_volume_draw(TsMosaic *m, double from, float a, double to, float b);
void ts_mosaic_volume_action(TsMosaic *m, TsMosaicEnvelopeAction action);
/* Accepts 0/0 to clear, otherwise a finite range at least 1 ms long. */
int ts_mosaic_set_loop_range(TsMosaic *m, double start, double end);
void ts_mosaic_set_repeat(TsMosaic *m, int repeat);
/* Call once before each uninterrupted audio buffer, under the host audio lock.
   No allocation or sorting library calls. Scalar reads without begin_block
   still observe direct card edits immediately. Seeks rebuild within a buffer. */
void ts_mosaic_begin_block(TsMosaic *m, unsigned frames);
TsStereoFrame ts_mosaic_read(TsMosaic *m, int output_rate);
/* Returns the Mosaic bus; custom cards go only into routes. NULL ignores routing. */
TsStereoFrame ts_mosaic_read_routed(TsMosaic *m, int output_rate, TsSourceRouteMix *routes);
uint64_t ts_mosaic_hash(const TsMosaic *m);
/* Project-data directory; optional on load for pre-Mosaic projects. */
int ts_mosaic_save(const TsMosaic *m, const char *directory, char *error, size_t size);
int ts_mosaic_load(TsMosaic *m, const char *directory, char *error, size_t size);

#endif
