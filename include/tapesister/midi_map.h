#ifndef TAPESISTER_MIDI_MAP_H
#define TAPESISTER_MIDI_MAP_H

#include <stddef.h>

enum {
    TS_MIDI_MAP_CAPACITY = 192,
    TS_MIDI_TARGET_ID_MAX = 80,
    TS_MIDI_TILE_TARGET_COUNT = 16
};

typedef enum {
    TS_MIDI_SOURCE_NONE = 0,
    TS_MIDI_SOURCE_NOTE,
    TS_MIDI_SOURCE_CC,
    TS_MIDI_SOURCE_PITCH_BEND
} TsMidiSourceKind;

typedef enum {
    TS_MIDI_TAKEOVER_JUMP = 0,
    TS_MIDI_TAKEOVER_PICKUP
} TsMidiTakeover;

typedef struct {
    TsMidiSourceKind kind;
    int channel;
    int number;
} TsMidiSource;

typedef struct {
    char target[TS_MIDI_TARGET_ID_MAX];
    TsMidiSource source;
    int trigger_on_zero;
    int pickup_waiting;
    int has_previous;
    float previous;
} TsMidiMapping;

typedef struct {
    TsMidiMapping mappings[TS_MIDI_MAP_CAPACITY];
    size_t count;
    TsMidiTakeover takeover;
} TsMidiMap;

void ts_midi_map_init(TsMidiMap *map);
int ts_midi_source_equal(TsMidiSource left, TsMidiSource right);
TsMidiMapping *ts_midi_map_find_source(TsMidiMap *map, TsMidiSource source);
const TsMidiMapping *ts_midi_map_find_target_const(const TsMidiMap *map,
                                                   const char *target);
int ts_midi_map_assign(TsMidiMap *map, const char *target,
                       TsMidiSource source);
int ts_midi_map_assign_trigger(TsMidiMap *map, const char *target,
                               TsMidiSource source, int trigger_on_zero);
int ts_midi_map_remove_target(TsMidiMap *map, const char *target);
void ts_midi_map_rearm_pickup(TsMidiMap *map);
int ts_midi_mapping_should_trigger(const TsMidiMapping *mapping,
                                   int value, int maximum);
int ts_midi_tile_target_slot(const char *target);
int ts_midi_target_is_continuous(const char *target);
int ts_midi_target_accepts_source(const char *target,
                                  TsMidiSourceKind source_kind);
int ts_midi_source_parse(const char *text, TsMidiSource *source);
int ts_midi_source_format(TsMidiSource source, char *text, size_t text_size);
int ts_midi_mapping_source_parse(const char *text, TsMidiSource *source,
                                 int *trigger_on_zero);
int ts_midi_mapping_source_format(const TsMidiMapping *mapping, char *text,
                                  size_t text_size);
const char *ts_midi_takeover_name(TsMidiTakeover takeover);
int ts_midi_takeover_parse(const char *text, TsMidiTakeover *takeover);

#endif
