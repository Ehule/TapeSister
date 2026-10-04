#ifndef TAPESISTER_SISTER_TRACKER_H
#define TAPESISTER_SISTER_TRACKER_H

#include "tapesister/tile_id.h"
#include <stddef.h>
#include <stdint.h>

enum {
    TS_TRACKER_LANES = 8,
    TS_TRACKER_ROWS = 256,
    TS_TRACKER_PATTERNS = 256,
    TS_TRACKER_ORDERS = 256,
    TS_TRACKER_ALIASES = 256, /* 00 reserved; 01-FF usable. */
    TS_TRACKER_RATIOS = 17,
    TS_TRACKER_NAME_SIZE = 32
};

typedef uint32_t TsPatternId;
typedef enum {
    TS_TRACKER_NOTE_NONE, TS_TRACKER_NOTE_PITCH,
    TS_TRACKER_NOTE_OFF, TS_TRACKER_NOTE_CUT
} TsTrackerNoteKind;
typedef enum {
    TS_TRACKER_STANDARD, TS_TRACKER_PATTERN, TS_TRACKER_SONG
} TsTrackerLaneMode;
typedef enum {
    TS_TRACKER_FORWARD, TS_TRACKER_REVERSE, TS_TRACKER_PING_PONG
} TsTrackerDirection;
typedef enum { TS_TRACKER_VOICE_CUT, TS_TRACKER_VOICE_OVERLAP } TsTrackerVoiceMode;
typedef enum {
    TS_TRACKER_ROUTE_MAIN, TS_TRACKER_ROUTE_DIRECT, TS_TRACKER_ROUTE_MAIN_DIRECT
} TsTrackerRoute;

typedef struct {
    TsTileId tile_id; /* Zero inherits; an unresolved nonzero ID stays missing. */
    uint8_t note_kind;
    uint8_t note; /* MIDI 0 is valid when note_kind is PITCH. */
    uint8_t has_volume;
    uint8_t volume; /* 00-40 hexadecimal. */
    uint8_t tune_command; /* Zero, 'M' (root), or 'N' (fine tune). */
    uint8_t tune_value;
    uint8_t fx_command; /* Zero or ASCII 0-9/A-Z; '0' and Z00 are not empty. */
    uint8_t fx_value;
} TsTrackerCell;

typedef struct {
    TsPatternId id;
    char name[TS_TRACKER_NAME_SIZE];
    uint16_t rows;
    /* Hidden rows survive shortening, copying and saving. */
    TsTrackerCell cells[TS_TRACKER_ROWS][TS_TRACKER_LANES];
} TsTrackerPattern;

typedef struct {
    char name[TS_TRACKER_NAME_SIZE];
    uint16_t length; /* Zero = OFF. */
    uint8_t mode, ratio, direction, voice_mode;
    float trim; /* 0-2; event volume is separate. */
    uint8_t muted, route, output_channel, output_stereo;
} TsTrackerLane;

typedef struct {
    TsTrackerPattern *patterns[TS_TRACKER_PATTERNS];
    uint16_t pattern_count;
    TsPatternId next_pattern_id;
    TsPatternId orders[TS_TRACKER_ORDERS];
    uint16_t order_count, restart_order;
    uint16_t bpm;
    uint8_t ticks_per_line, loop;
    int8_t control_lane; /* -1 = none. */
    uint8_t fasttracks_uses_length, length_bypass;
    uint32_t random_seed;
    TsTileId aliases[TS_TRACKER_ALIASES];
    TsTrackerLane lanes[TS_TRACKER_LANES];
    TsPatternId editor_pattern;
    uint16_t editor_row;
    uint8_t editor_lane, edit_step, follow;
    /* Owned versioned TapeHead score extension; contains no audio or pointers. */
    uint8_t *embedded_data;
    uint32_t embedded_size;
    /* Definitions only: no playback state, voice pointers or transient solo. */
} TsSisterTracker;

void ts_sister_tracker_init(TsSisterTracker *tracker);
void ts_sister_tracker_free(TsSisterTracker *tracker);
int ts_sister_tracker_clone(TsSisterTracker *destination,
                            const TsSisterTracker *source,
                            char *error, size_t error_size);
TsTrackerPattern *ts_sister_tracker_pattern(TsSisterTracker *tracker, TsPatternId id);
const TsTrackerPattern *ts_sister_tracker_pattern_const(const TsSisterTracker *tracker,
                                                       TsPatternId id);
int ts_sister_tracker_add_pattern(TsSisterTracker *tracker, uint16_t rows,
                                  TsPatternId *id, char *error, size_t error_size);
int ts_sister_tracker_copy_pattern(TsSisterTracker *tracker, TsPatternId source,
                                   TsPatternId *id, char *error, size_t error_size);
int ts_sister_tracker_remove_pattern(TsSisterTracker *tracker, TsPatternId id,
                                     char *error, size_t error_size);
int ts_sister_tracker_set_rows(TsSisterTracker *tracker, TsPatternId id, uint16_t rows);
int ts_sister_tracker_insert_order(TsSisterTracker *tracker, uint16_t index,
                                   TsPatternId id, char *error, size_t error_size);
int ts_sister_tracker_remove_order(TsSisterTracker *tracker, uint16_t index);
/* Reuses an existing alias, otherwise claims the first free alias. */
int ts_sister_tracker_bind_tile(TsSisterTracker *tracker, TsTileId id,
                                uint8_t *alias, char *error, size_t error_size);
int ts_sister_tracker_validate(const TsSisterTracker *tracker,
                               char *error, size_t error_size);
/* Call after accepting a loaded definition, including missing tile references. */
void ts_sister_tracker_reserve_tile_ids(const TsSisterTracker *tracker);
uint64_t ts_sister_tracker_hash(const TsSisterTracker *tracker);
int ts_tracker_embedded_matches(const TsSisterTracker *tracker);
int ts_tracker_embedded_validate(const uint8_t *data, uint32_t size);
int ts_sister_tracker_save_file(const TsSisterTracker *tracker, const char *path,
                                char *error, size_t error_size);
/* Atomic replacement; missing files are handled by the project loader. */
int ts_sister_tracker_load_file(TsSisterTracker *tracker, const char *path,
                                char *error, size_t error_size);

/* Portable embedded tracker preferences shared by projects and saved defaults. */
int ts_tracker_preferences_validate(const uint8_t *p,uint32_t size);
#endif
