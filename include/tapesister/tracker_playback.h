#ifndef TAPESISTER_TRACKER_PLAYBACK_H
#define TAPESISTER_TRACKER_PLAYBACK_H

#include "tapesister/sample_pages.h"
#include "tapesister/note_bank.h"
#include <stdatomic.h>

/* Immutable audio generations. Only reference counts change in the callback;
   allocation, cloning and destruction are control-thread work. */
typedef struct TsTrackerSource {
    TsTileId id;
    TsSample sample;
    const float *original_data;
    uint32_t original_revision;
    atomic_uint readers;
    struct TsTrackerSource *next;
} TsTrackerSource;

typedef struct {
    TsTileId id;
    TsTrackerSource *source;
    double step[128];
    size_t first, last, crossfade;
    TsLoopMode loop_mode;
    int looping;
} TsTrackerBinding;

typedef struct {
    TsTrackerPattern pattern;
    TsTrackerBinding bindings[TS_TRACKER_ALIASES];
    TsTrackerLane lanes[TS_TRACKER_LANES];
    uint16_t bpm;
    uint8_t ticks_per_line, loop;
    int rate;
    uint64_t stamp;
} TsTrackerPrepared;

typedef struct {
    TsNoteVoice voice;
    TsTrackerSource *source;
    TsTileId default_tile, sounding_tile;
    TsVoiceHandoff handoff;
    float gain;
    uint8_t volume;
    int changed;
    uint64_t last_note_frame, notes_started;
} TsTrackerLanePlayback;

typedef struct {
    TsPatternId pattern;
    int row0,row1,lane0,lane1; /* Inclusive literal bounds, never editor pointers. */
} TsTrackerBlock;

typedef struct {
    TsTrackerPrepared *prepared;
    TsTrackerSource *sources; /* Control-thread cache, never traversed by audio. */
    TsTrackerLanePlayback lanes[TS_TRACKER_LANES];
    int running, paused, rate, row, tick, tail_active;
    uint8_t missing_mask, solo_mask;
    double until_tick, tick_frames;
    uint64_t elapsed_frames;
    TsTrackerBlock block,pending_block;
    int block_active,block_pending;
    uint64_t loop_cycles;
    int loop_seam; /* True for exactly the first audio frame of a cycle. */
    atomic_uint display_running, display_row, display_pattern, display_missing;
    atomic_uint display_block,display_block_rows,display_block_lanes;
} TsTrackerPlayback;

void ts_tracker_playback_init(TsTrackerPlayback *playback);
void ts_tracker_playback_free(TsTrackerPlayback *playback);
/* Control thread: stamp/prepare/collect may inspect banks, allocate and free. */
uint64_t ts_tracker_playback_stamp(const TsSamplePages *pages,
                                  const TsInstrument *active,
                                  TsPatternId pattern, int rate);
TsTrackerPrepared *ts_tracker_playback_prepare(TsTrackerPlayback *playback,
    const TsSamplePages *pages, const TsInstrument *active, TsPatternId pattern,
    int rate, char *error, size_t size);
void ts_tracker_prepared_free(TsTrackerPrepared *prepared);
void ts_tracker_playback_collect(TsTrackerPlayback *playback);
/* Publish under callback exclusion; free the returned old definition outside it. */
TsTrackerPrepared *ts_tracker_playback_publish(TsTrackerPlayback *playback,
                                              TsTrackerPrepared *prepared);
int ts_tracker_playback_start(TsTrackerPlayback *playback);
int ts_tracker_playback_start_block(TsTrackerPlayback *playback,TsTrackerBlock block);
int ts_tracker_playback_queue_block(TsTrackerPlayback *playback,TsTrackerBlock block);
void ts_tracker_playback_stop(TsTrackerPlayback *playback);
void ts_tracker_playback_pause(TsTrackerPlayback *playback, int paused);
void ts_tracker_playback_solo(TsTrackerPlayback *playback, uint8_t mask);
/* Allocation-free callback API. Rate changes preserve remaining tick time. */
TsStereoFrame ts_tracker_playback_read(TsTrackerPlayback *playback, int rate);
void ts_tracker_playback_end_block(TsTrackerPlayback *playback);

#endif
