#include "tapesister/sister_tracker.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Explicit little-endian fields, never a dump of C structs. The
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
    char magic[8] = { 'S','I','S','T','R','K',6,0 };
    char header[8] = {0};
    bytes(s, header, magic, sizeof(header));
    if (s->reading && (memcmp(header, magic, 6) || header[7] ||
        (header[6]!=1 && header[6]!=2 && header[6]!=3 && header[6]!=4 && header[6]!=5 && header[6]!=6))) { s->failed = 1; return; }
    unsigned version=s->reading?(unsigned char)header[6]:(unsigned char)magic[6];
    /* Pre-routing scores historically inherited tiles. Preserve that choice. */
    if(s->reading && version<4)for(int i=0;i<TS_TRACKER_LANES;++i)
        loaded->lanes[i].output_route.mode=TS_SOURCE_INHERIT;
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
    if(version>=3) { SONG(channel_count,1); }
    unsigned lanes=version>=3?TS_TRACKER_LANES:8;
    if (t->pattern_count > TS_TRACKER_PATTERNS || t->order_count > TS_TRACKER_ORDERS) {
        s->failed = 1; return;
    }
    for (int i = 0; i < TS_TRACKER_ALIASES; ++i) SONG(aliases[i], 8);
    for (int i = 0; i < t->order_count; ++i) SONG(orders[i], 4);
    for (int i = 0; i < (int)lanes; ++i) {
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
        if(version>=4) {
            FIELD(l,out,output_route.mode,1);FIELD(l,out,output_route.speaker,1);
            FIELD(l,out,output_route.second,1);
            unsigned pan=number(s,(unsigned)(l->output_route.pan+100),1);
            unsigned width=number(s,(unsigned)(l->output_route.width+100),1);
            if(s->reading) {out->output_route.pan=(int)pan-100;out->output_route.width=(int)width-100;}
        }
        if(s->reading && version<6 && out->output_route.mode==TS_SOURCE_MAIN)
            out->output_route.pan=out->output_route.width=0;
        if(version>=5) {
            FIELD(l,out,output_route.mix_enabled,1);FIELD(l,out,output_route.clean_level,1);
            for(int bus=0;bus<TS_SOURCE_SENDS;++bus)FIELD(l,out,output_route.send_level[bus],1);
        }
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
            for (int lane = 0; lane < (int)lanes; ++lane) {
                const TsTrackerCell *c = &p->cells[row][lane];
                TsTrackerCell *dst = out ? &out->cells[row][lane] : NULL;
                FIELD(c, dst, tile_id, 8); FIELD(c, dst, note_kind, 1);
                FIELD(c, dst, note, 1); FIELD(c, dst, has_volume, 1); FIELD(c, dst, volume, 1);
                FIELD(c, dst, tune_command, 1); FIELD(c, dst, tune_value, 1);
                FIELD(c, dst, fx_command, 1); FIELD(c, dst, fx_value, 1);
            }
    }
    if(version>=3)for(int a=1;a<=128;++a) {
        const TsTrackerInstrument *v=&t->instruments[a];
        TsTrackerInstrument *o=loaded?&loaded->instruments[a]:NULL;
        FIELD(v,o,present,1);FIELD(v,o,sample_count,1);
        bytes(s,o?o->name:NULL,v->name,sizeof(v->name));
        bytes(s,o?o->sample_names:NULL,v->sample_names,sizeof(v->sample_names));
        for(int k=0;k<16;++k) {FIELD(v,o,tiles[k],8);FIELD(v,o,volume[k],1);FIELD(v,o,panning[k],1);FIELD(v,o,relative_note[k],1);FIELD(v,o,finetune[k],1);}
        bytes(s,o?o->note_map:NULL,v->note_map,96);
        for(int k=0;k<12;++k)for(int j=0;j<2;++j) {FIELD(v,o,vol_points[k][j],2);FIELD(v,o,pan_points[k][j],2);}
#define XM_FIELD(f) FIELD(v,o,f,1)
        XM_FIELD(vol_length);XM_FIELD(pan_length);XM_FIELD(vol_sustain);XM_FIELD(vol_start);XM_FIELD(vol_end);
        XM_FIELD(pan_sustain);XM_FIELD(pan_start);XM_FIELD(pan_end);XM_FIELD(vol_flags);XM_FIELD(pan_flags);
        XM_FIELD(vib_type);XM_FIELD(vib_sweep);XM_FIELD(vib_depth);XM_FIELD(vib_rate);FIELD(v,o,fadeout,2);
#undef XM_FIELD
    }
#undef SONG
#undef FIELD
    if(version>=2 && !s->failed) {
        uint32_t count=(uint32_t)number(s,t->embedded_size,4);
        if((!count && version==2) || count>16u*1024u*1024u){s->failed=1;return;}
        if(!count)return;
        if(s->reading) {
            loaded->embedded_data=malloc(count);loaded->embedded_size=count;
            if(!loaded->embedded_data){s->failed=1;return;}
        }
        bytes(s,loaded?loaded->embedded_data:NULL,t->embedded_data,count);
        if(s->reading && !ts_tracker_embedded_validate(loaded->embedded_data,count))s->failed=1;
    }
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
