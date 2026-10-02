#include "tapesister/sister_tracker.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Version 1: explicit little-endian fields, never a dump of C structs. The
   same encoder feeds persistence and the project dirty-state hash. */
typedef struct {
    FILE *file;
    int reading, failed;
    uint64_t hash;
} TrackerStream;

static void bytes(TrackerStream *s, void *destination, const void *source, size_t count)
{
    if (s->failed) return;
    if (s->reading) {
        if (fread(destination, 1, count, s->file) != count) s->failed = 1;
    } else {
        const unsigned char *p = source;
        for (size_t i = 0; i < count; ++i) { s->hash ^= p[i]; s->hash *= UINT64_C(1099511628211); }
        if (s->file && fwrite(source, 1, count, s->file) != count) s->failed = 1;
    }
}

static uint64_t number(TrackerStream *s, uint64_t value, unsigned width)
{
    unsigned char data[8];
    for (unsigned i = 0; i < width; ++i) data[i] = (unsigned char)(value >> (8 * i));
    bytes(s, data, data, width);
    value = 0;
    for (unsigned i = 0; i < width; ++i) value |= (uint64_t)data[i] << (8 * i);
    return value;
}

static void codec(TrackerStream *s, TsSisterTracker *loaded, const TsSisterTracker *saved)
{
    const TsSisterTracker *t = s->reading ? loaded : saved;
    static const char magic[8] = { 'S','I','S','T','R','K',1,0 };
    char header[8] = {0};
    bytes(s, header, magic, sizeof(header));
    if (s->reading && memcmp(header, magic, sizeof(header))) { s->failed = 1; return; }
#define FIELD(owner, target, field, width) do { \
    uint64_t value = number(s, (uint64_t)(owner)->field, width); \
    if (s->reading) (target)->field = value; \
} while (0)
#define SONG(field, width) FIELD(t, loaded, field, width)
    SONG(pattern_count, 2); SONG(next_pattern_id, 4); SONG(order_count, 2);
    SONG(restart_order, 2); SONG(bpm, 2); SONG(ticks_per_line, 1); SONG(loop, 1);
    unsigned control = number(s, (unsigned)(t->control_lane + 1), 1);
    if (s->reading) loaded->control_lane = (int8_t)((int)control - 1);
    SONG(fasttracks_uses_length, 1); SONG(length_bypass, 1); SONG(random_seed, 4);
    SONG(editor_pattern, 4); SONG(editor_row, 2); SONG(editor_lane, 1);
    SONG(edit_step, 1); SONG(follow, 1);
    if (t->pattern_count > TS_TRACKER_PATTERNS || t->order_count > TS_TRACKER_ORDERS) {
        s->failed = 1; return;
    }
    for (int i = 0; i < TS_TRACKER_ALIASES; ++i) SONG(aliases[i], 8);
    for (int i = 0; i < t->order_count; ++i) SONG(orders[i], 4);
    for (int i = 0; i < TS_TRACKER_LANES; ++i) {
        const TsTrackerLane *l = &t->lanes[i];
        TsTrackerLane *out = loaded ? &loaded->lanes[i] : NULL;
        uint32_t trim = 0;
        _Static_assert(sizeof(float) == sizeof(trim), "Tracker persistence requires 32-bit floats");
        bytes(s, out ? out->name : NULL, l->name, sizeof(l->name));
        FIELD(l, out, length, 2); FIELD(l, out, mode, 1); FIELD(l, out, ratio, 1);
        FIELD(l, out, direction, 1); FIELD(l, out, voice_mode, 1);
        memcpy(&trim, &l->trim, sizeof(trim));
        trim = (uint32_t)number(s, trim, 4);
        if (s->reading) memcpy(&out->trim, &trim, sizeof(trim));
        FIELD(l, out, muted, 1); FIELD(l, out, route, 1);
        FIELD(l, out, output_channel, 1); FIELD(l, out, output_stereo, 1);
    }
    for (int i = 0; i < t->pattern_count && !s->failed; ++i) {
        if (s->reading) {
            loaded->patterns[i] = calloc(1, sizeof(*loaded->patterns[i]));
            if (!loaded->patterns[i]) { s->failed = 1; return; }
        }
        const TsTrackerPattern *p = t->patterns[i];
        TsTrackerPattern *out = loaded ? loaded->patterns[i] : NULL;
        FIELD(p, out, id, 4); FIELD(p, out, rows, 2);
        bytes(s, out ? out->name : NULL, p->name, sizeof(p->name));
        for (int row = 0; row < TS_TRACKER_ROWS && !s->failed; ++row)
            for (int lane = 0; lane < TS_TRACKER_LANES; ++lane) {
                const TsTrackerCell *c = &p->cells[row][lane];
                TsTrackerCell *dst = out ? &out->cells[row][lane] : NULL;
                FIELD(c, dst, tile_id, 8); FIELD(c, dst, note_kind, 1);
                FIELD(c, dst, note, 1); FIELD(c, dst, has_volume, 1); FIELD(c, dst, volume, 1);
                FIELD(c, dst, tune_command, 1); FIELD(c, dst, tune_value, 1);
                FIELD(c, dst, fx_command, 1); FIELD(c, dst, fx_value, 1);
            }
    }
#undef SONG
#undef FIELD
}

uint64_t ts_sister_tracker_hash(const TsSisterTracker *t)
{
    TrackerStream s = { NULL, 0, 0, UINT64_C(14695981039346656037) };
    if (!t || t->pattern_count > TS_TRACKER_PATTERNS || t->order_count > TS_TRACKER_ORDERS) return 0;
    codec(&s, NULL, t);
    return s.hash;
}

static int error_message(char *error, size_t size, const char *message)
{
    if (error && size) snprintf(error, size, "%s", message);
    return 0;
}

int ts_sister_tracker_save_file(const TsSisterTracker *t, const char *path,
                                char *error, size_t size)
{
    TrackerStream s = { NULL, 0, 0, UINT64_C(14695981039346656037) };
    if (!ts_sister_tracker_validate(t, error, size)) return 0;
    if (!path || !(s.file = fopen(path, "wb")))
        return error_message(error, size, "Could not create SisterTracker data");
    codec(&s, NULL, t);
    if (fclose(s.file)) s.failed = 1;
    if (s.failed) return error_message(error, size, "Could not write SisterTracker data");
    return 1;
}

int ts_sister_tracker_load_file(TsSisterTracker *t, const char *path,
                                char *error, size_t size)
{
    TrackerStream s = { NULL, 1, 0, 0 };
    TsSisterTracker loaded;
    if (!t || !path || !(s.file = fopen(path, "rb")))
        return error_message(error, size, "Could not open SisterTracker data");
    ts_sister_tracker_init(&loaded);
    codec(&s, &loaded, NULL);
    if (fgetc(s.file) != EOF || ferror(s.file)) s.failed = 1;
    if (fclose(s.file)) s.failed = 1;
    if (s.failed || !ts_sister_tracker_validate(&loaded, error, size)) {
        ts_sister_tracker_free(&loaded);
        if (s.failed) error_message(error, size, "Malformed, truncated or unsupported SisterTracker data");
        return 0;
    }
    ts_sister_tracker_reserve_tile_ids(&loaded);
    ts_sister_tracker_free(t);
    *t = loaded;
    return 1;
}
