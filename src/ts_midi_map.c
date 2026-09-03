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
    return ts_midi_map_assign_trigger(map, target, source, 0);
}

int ts_midi_map_assign_trigger(TsMidiMap *map, const char *target,
                               TsMidiSource source, int trigger_on_zero)
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
    mapping->trigger_on_zero = trigger_on_zero != 0;
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

int ts_midi_mapping_should_trigger(const TsMidiMapping *mapping,
                                   int value, int maximum)
{
    if (mapping == NULL || maximum <= 0 || value < 0 || value > maximum)
        return 0;
    return mapping->trigger_on_zero ? value == 0 : value > 0;
}

int ts_midi_tile_target_slot(const char *target)
{
    int slot;
    char trailing;
    if (target == NULL ||
        sscanf(target, "tile.%d.launch%c", &slot, &trailing) != 1 ||
        slot < 1 || slot > TS_MIDI_TILE_TARGET_COUNT)
        return -1;
    return slot - 1;
}

int ts_midi_target_is_continuous(const char *target)
{
    return target != NULL &&
           (strncmp(target, "sister.param.", 13u) == 0 ||
            strcmp(target, "main.master_output") == 0 ||
            strcmp(target, "main.tile_fade") == 0);
}

int ts_midi_target_accepts_source(const char *target,
                                  TsMidiSourceKind source_kind)
{
    if (target == NULL) return 0;
    if (ts_midi_target_is_continuous(target))
        return source_kind == TS_MIDI_SOURCE_CC ||
               source_kind == TS_MIDI_SOURCE_PITCH_BEND;
    return source_kind == TS_MIDI_SOURCE_NOTE ||
           source_kind == TS_MIDI_SOURCE_CC;
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

int ts_midi_mapping_source_parse(const char *text, TsMidiSource *source,
                                 int *trigger_on_zero)
{
    static const char zero_suffix[] = ",zero";
    char source_text[40];
    size_t length;
    int zero = 0;
    if (text == NULL || source == NULL || trigger_on_zero == NULL) return 0;
    length = strlen(text);
    if (length >= sizeof(zero_suffix) - 1u &&
        strcmp(text + length - (sizeof(zero_suffix) - 1u), zero_suffix) == 0) {
        length -= sizeof(zero_suffix) - 1u;
        zero = 1;
    }
    if (length == 0u || length >= sizeof(source_text)) return 0;
    memcpy(source_text, text, length);
    source_text[length] = '\0';
    if (!ts_midi_source_parse(source_text, source)) return 0;
    *trigger_on_zero = zero;
    return 1;
}

int ts_midi_mapping_source_format(const TsMidiMapping *mapping, char *text,
                                  size_t text_size)
{
    char source[40];
    int result;
    if (mapping == NULL || text == NULL || text_size == 0u ||
        !ts_midi_source_format(mapping->source, source, sizeof(source)))
        return 0;
    result = snprintf(text, text_size, "%s%s", source,
                      mapping->trigger_on_zero ? ",zero" : "");
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
