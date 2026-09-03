#include "tapesister/midi_map.h"

#include <stdio.h>
#include <string.h>

void ts_midi_map_init(TsMidiMap *map)
{
    if (map == NULL) return;
    memset(map, 0, sizeof(*map));
    map->takeover = TS_MIDI_TAKEOVER_PICKUP;
}

int ts_midi_source_equal(TsMidiSource left, TsMidiSource right)
{
    return left.kind == right.kind && left.channel == right.channel &&
           left.number == right.number;
}

TsMidiMapping *ts_midi_map_find_source(TsMidiMap *map, TsMidiSource source)
{
    if (map == NULL) return NULL;
    for (size_t index = 0; index < map->count; ++index)
        if (ts_midi_source_equal(map->mappings[index].source, source))
            return &map->mappings[index];
    return NULL;
}

const TsMidiMapping *ts_midi_map_find_target_const(const TsMidiMap *map,
                                                   const char *target)
{
    if (map == NULL || target == NULL) return NULL;
    for (size_t index = 0; index < map->count; ++index)
        if (strcmp(map->mappings[index].target, target) == 0)
            return &map->mappings[index];
    return NULL;
}

int ts_midi_map_remove_target(TsMidiMap *map, const char *target)
{
    if (map == NULL || target == NULL) return 0;
    for (size_t index = 0; index < map->count; ++index) {
        if (strcmp(map->mappings[index].target, target) != 0) continue;
        if (index + 1u < map->count)
            memmove(&map->mappings[index], &map->mappings[index + 1u],
                    (map->count - index - 1u) * sizeof(map->mappings[0]));
        --map->count;
        memset(&map->mappings[map->count], 0, sizeof(map->mappings[0]));
        return 1;
    }
    return 0;
}

int ts_midi_map_assign(TsMidiMap *map, const char *target,
                       TsMidiSource source)
{
    TsMidiMapping *mapping;
    size_t length;
    if (map == NULL || target == NULL || source.kind == TS_MIDI_SOURCE_NONE ||
        source.channel < 0 || source.channel > 15 || source.number < 0 ||
        source.number > 127)
        return 0;
    length = strlen(target);
    if (length == 0u || length >= TS_MIDI_TARGET_ID_MAX) return 0;
    (void)ts_midi_map_remove_target(map, target);
    mapping = ts_midi_map_find_source(map, source);
    if (mapping == NULL) {
        if (map->count >= TS_MIDI_MAP_CAPACITY) return 0;
        mapping = &map->mappings[map->count++];
    }
    memset(mapping, 0, sizeof(*mapping));
    memcpy(mapping->target, target, length + 1u);
    mapping->source = source;
    mapping->pickup_waiting = 1;
    return 1;
}

void ts_midi_map_rearm_pickup(TsMidiMap *map)
{
    if (map == NULL) return;
    for (size_t index = 0; index < map->count; ++index) {
        map->mappings[index].pickup_waiting = 1;
        map->mappings[index].has_previous = 0;
    }
}

int ts_midi_source_parse(const char *text, TsMidiSource *source)
{
    char kind[16];
    char trailing;
    int channel;
    int number = 0;
    int fields;
    TsMidiSource parsed;
    if (text == NULL || source == NULL) return 0;
    fields = sscanf(text, "%15[^,],%d,%d%c", kind, &channel, &number,
                    &trailing);
    if (fields < 2 || channel < 1 || channel > 16) return 0;
    memset(&parsed, 0, sizeof(parsed));
    parsed.channel = channel - 1;
    if (strcmp(kind, "pitchbend") == 0 && fields == 2) {
        parsed.kind = TS_MIDI_SOURCE_PITCH_BEND;
    } else if (strcmp(kind, "cc") == 0 && fields == 3 &&
               number >= 0 && number <= 127) {
        parsed.kind = TS_MIDI_SOURCE_CC;
        parsed.number = number;
    } else if (strcmp(kind, "note") == 0 && fields == 3 &&
               number >= 0 && number <= 127) {
        parsed.kind = TS_MIDI_SOURCE_NOTE;
        parsed.number = number;
    } else return 0;
    *source = parsed;
    return 1;
}

int ts_midi_source_format(TsMidiSource source, char *text, size_t text_size)
{
    int result;
    const char *kind;
    if (text == NULL || text_size == 0u || source.channel < 0 ||
        source.channel > 15) return 0;
    if (source.kind == TS_MIDI_SOURCE_PITCH_BEND)
        result = snprintf(text, text_size, "pitchbend,%d", source.channel + 1);
    else {
        kind = source.kind == TS_MIDI_SOURCE_CC ? "cc" :
               source.kind == TS_MIDI_SOURCE_NOTE ? "note" : NULL;
        if (kind == NULL || source.number < 0 || source.number > 127) return 0;
        result = snprintf(text, text_size, "%s,%d,%d", kind,
                          source.channel + 1, source.number);
    }
    return result >= 0 && (size_t)result < text_size;
}

const char *ts_midi_takeover_name(TsMidiTakeover takeover)
{
    return takeover == TS_MIDI_TAKEOVER_JUMP ? "jump" : "pickup";
}

int ts_midi_takeover_parse(const char *text, TsMidiTakeover *takeover)
{
    if (text == NULL || takeover == NULL) return 0;
    if (strcmp(text, "jump") == 0) {
        *takeover = TS_MIDI_TAKEOVER_JUMP;
        return 1;
    }
    if (strcmp(text, "pickup") == 0) {
        *takeover = TS_MIDI_TAKEOVER_PICKUP;
        return 1;
    }
    return 0;
}
