#include "tapesister/meta_fx.h"
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif
static float unit(float x) { return !isfinite(x) || x < 0 ? 0 : x > 1 ? 1 : x; }
const char *ts_meta_parameter_name(int type, int field) {
    static const char *names[][3] = {{"", "", ""},
                                     {"SIZE", "DECAY", ""},
                                     {"TIME", "FDBK", ""},
                                     {"DRIVE", "TONE", ""},
                                     {"SIZE", "DENS", "PITCH"},
                                     {"ATK", "SUS", "RESP"},
                                     {"CUT", "RES", "DRIVE"},
                                     {"FREQ", "Q", "SPREAD"},
                                     {"ROOT", "DECAY", "DAMP"}};
    if (field == 3)
        return "MIX";
    if (field == 4)
        return "OUTPUT";
    return type >= 0 && type < TS_SISTER_FX_SUNVOX && field >= 0 && field < 3 ? names[type][field]
                                                                                  : "FILTER";
}
int ts_meta_target_field(int type, int field) {
    return type == TS_SISTER_FX_FILTER && field < 3
               ? 5 + (int[]){TS_CF_L_CUT, TS_CF_L_RES, TS_CF_L_DRIVE}[field]
               : field;
}
static int name_valid(const char *s, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (!s[i])
            return 1;
        if ((unsigned char)s[i] < 32 || (unsigned char)s[i] > 126 || s[i] == '=')
            return 0;
    }
    return 0;
}
void ts_meta_stage_default(TsMetaStage *s, int type) {
    memset(s, 0, sizeof(*s));
    s->type = type;
    s->enabled = 1;
    s->value[0] = s->value[1] = s->value[2] = s->value[4] = .5f;
    s->value[3] = 1;
    if (type == TS_SISTER_FX_REVERB) {
        s->value[0] = .58f;
        s->value[1] = .42f;
        s->value[3] = .35f;
    }
    if (type == TS_SISTER_FX_DELAY) {
        s->value[0] = .38f;
        s->value[1] = .32f;
        s->value[3] = .3f;
    }
    if (type == TS_SISTER_FX_DISTORTION) {
        s->value[0] = .25f;
        s->value[1] = .55f;
    }
    if (type == TS_SISTER_FX_GRAIN) {
        s->value[0] = .46f;
        s->value[1] = .42f;
        s->value[3] = .5f;
    }
    if (type == TS_SISTER_FX_RESONATOR)
        s->value[0] = .25f;
    TsCharacterFilterControls filter;
    ts_character_filter_default(&filter);
    memcpy(s->filter, filter.value, sizeof(s->filter));
}
float ts_meta_stage_value(const TsMetaStage *s, int field) {
    if (s->type == TS_SISTER_FX_FILTER && field < 3)
        return s->filter[(int[]){TS_CF_L_CUT, TS_CF_L_RES, TS_CF_L_DRIVE}[field]];
    return s->value[field];
}
void ts_meta_stage_set(TsMetaStage *s, int field, float value) {
    if (s->type == TS_SISTER_FX_FILTER && field < 3) {
        TsCharacterFilterControls c;
        ts_character_filter_default(&c);
        memcpy(c.value, s->filter, sizeof(s->filter));
        ts_character_filter_set(&c, (int[]){TS_CF_L_CUT, TS_CF_L_RES, TS_CF_L_DRIVE}[field], unit(value));
        memcpy(s->filter, c.value, sizeof(s->filter));
    } else
        s->value[field] = unit(value);
}
void ts_meta_default(TsMetaControls *c) {
    memset(c, 0, sizeof(*c));
    c->initialized = 1;
    c->preset = -1;
    snprintf(c->name, sizeof(c->name), "UNTITLED RACK");
    for (int i = 0; i < TS_META_MACROS; ++i) {
        snprintf(c->label[i], sizeof(c->label[i]), "MACRO %d", i + 1);
        c->macro[i] = .5f;
    }
    for (int i = 0; i < TS_META_STAGES; ++i)
        ts_meta_stage_default(&c->stage[i], 0);
}
int ts_meta_valid(const TsMetaControls *c) {
    if (!c || c->initialized != 1 || c->count < 0 || c->count > TS_META_STAGES || c->map_count < 0 ||
        c->map_count > TS_META_MAPS || c->preset < -1 || c->preset >= TS_META_PRESETS || c->scene_mode < 0 ||
        c->scene_mode > 1 || c->captured < 0 || c->captured > 3 || !isfinite(c->morph) || c->morph < 0 ||
        c->morph > 1 || !name_valid(c->name, sizeof(c->name)))
        return 0;
    for (int i = 0; i < TS_META_MACROS; ++i) {
        if (!name_valid(c->label[i], sizeof(c->label[i])) || !isfinite(c->macro[i]) || c->macro[i] < 0 ||
            c->macro[i] > 1)
            return 0;
        for (int j = 0; j < 2; ++j)
            if (!isfinite(c->scene[j][i]) || c->scene[j][i] < 0 || c->scene[j][i] > 1)
                return 0;
    }
    for (int i = 0; i < TS_META_STAGES; ++i) {
        const TsMetaStage *s = &c->stage[i];
        if (s->type < 0 || s->type >= TS_SISTER_FX_SUNVOX || s->enabled < 0 || s->enabled > 1 ||
            s->power > 3)
            return 0;
        for (int j = 0; j < 5; ++j)
            if (!isfinite(s->value[j]) || s->value[j] < 0 || s->value[j] > 1)
                return 0;
        for (int j = 0; j < TS_CF_COUNT; ++j)
            if (!isfinite(s->filter[j]) || s->filter[j] < 0 || s->filter[j] > 1)
                return 0;
    }
    for (int i = 0; i < c->map_count; ++i) {
        const TsMetaMap *m = &c->map[i];
        if (m->macro >= TS_META_MACROS || m->stage >= c->count || m->field >= 5 + TS_CF_FOCUS ||
            !isfinite(m->low) || !isfinite(m->high) || m->low < 0 || m->low > 1 || m->high < 0 || m->high > 1)
            return 0;
        if (!c->stage[m->stage].type)
            return 0;
        if (m->field >= 5 && c->stage[m->stage].type != TS_SISTER_FX_FILTER)
            return 0;
        if (m->field < 3 && !*ts_meta_parameter_name(c->stage[m->stage].type, m->field))
            return 0;
        for (int j = 0; j < i; ++j)
            if (c->map[j].stage == m->stage &&
                ts_meta_target_field(c->stage[m->stage].type, c->map[j].field) ==
                    ts_meta_target_field(c->stage[m->stage].type, m->field))
                return 0;
    }
    return 1;
}
int ts_meta_same_topology(const TsMetaControls *a, const TsMetaControls *b) {
    if (a->count != b->count)
        return 0;
    for (int i = 0; i < a->count; ++i)
        if (a->stage[i].type != b->stage[i].type)
            return 0;
    return 1;
}
static void effective(const TsMetaControls *c, float *v) {
    for (int i = 0; i < TS_META_MACROS; ++i)
        v[i] = c->scene_mode && c->captured == 3
                   ? c->scene[0][i] + (c->scene[1][i] - c->scene[0][i]) * c->morph
                   : c->macro[i];
}
void ts_meta_macro_set(TsMetaControls *c, int i, float v) {
    if (i < 0 || i >= TS_META_MACROS)
        return;
    float values[TS_META_MACROS];
    effective(c, values);
    memcpy(c->macro, values, sizeof(values));
    c->scene_mode = 0;
    c->macro[i] = unit(v);
}
void ts_meta_capture(TsMetaControls *c, int i) {
    if (i < 0 || i > 1)
        return;
    float audible[TS_META_MACROS];
    effective(c, audible);
    memcpy(c->scene[i], audible, sizeof(audible));
    memcpy(c->macro, audible, sizeof(audible));
    c->scene_mode = 0;
    c->captured |= 1 << i;
}
static const char *preset_names[] = {"ARC",         "PALE CLOUD",   "SHIMMER ASH", "IRON CHOIR",
                                     "SPRING DUST", "GLASS THREAD", "BROKEN HALO", "SLOW WEATHER",
                                     "CHARCOAL",    "RESONANT ROOM"};
const char *ts_meta_preset_name(int i) { return i >= 0 && i < TS_META_PRESETS ? preset_names[i] : "CUSTOM"; }
static void map_add(TsMetaControls *c, int macro, int stage, int field, float low, float high) {
    if (c->map_count < TS_META_MAPS)
        c->map[c->map_count++] = (TsMetaMap){(uint8_t)macro, (uint8_t)stage, (uint8_t)field, low, high};
}
void ts_meta_preset(TsMetaControls *c, int preset) {
    if (preset < 0 || preset >= TS_META_PRESETS)
        return;
    ts_meta_default(c);
    c->preset = preset;
    snprintf(c->name, sizeof(c->name), "%s", preset_names[preset]);
    static const int types[TS_META_PRESETS][8] = {{5, 4, 5, 6, 3, 2, 1, 0}, {4, 7, 4, 2, 1, 0, 0, 0},
                                                  {4, 7, 2, 1, 0, 0, 0, 0}, {8, 6, 7, 1, 0, 0, 0, 0},
                                                  {5, 7, 8, 2, 1, 0, 0, 0}, {7, 7, 4, 2, 0, 0, 0, 0},
                                                  {5, 4, 3, 8, 1, 0, 0, 0}, {6, 4, 2, 1, 0, 0, 0, 0},
                                                  {3, 6, 5, 2, 0, 0, 0, 0}, {8, 7, 2, 1, 0, 0, 0, 0}};
    const char *labels[] = {"IGNITION", "CONTINUITY", "FRACTURE", "DISTANCE"};
    for (int i = 0; i < 4; ++i)
        snprintf(c->label[i], 16, "%s", labels[i]);
    for (int i = 0; i < 8 && types[preset][i]; ++i) {
        ts_meta_stage_default(&c->stage[i], types[preset][i]);
        ++c->count;
        TsMetaStage *s = &c->stage[i];
        if (s->type == TS_SISTER_FX_TRANSIENT) {
            map_add(c, 0, i, 0, .48f, .9f);
            map_add(c, 1, i, 1, .35f, .72f);
            map_add(c, 2, i, 2, .85f, .15f);
            s->power = 1;
        }
        if (s->type == TS_SISTER_FX_GRAIN) {
            map_add(c, 2, i, 0, .7f, .12f);
            map_add(c, 1, i, 1, .25f, .8f);
            s->value[2] = preset == 2 ? .75f : .5f;
        }
        if (s->type == TS_SISTER_FX_FILTER) {
            map_add(c, 3, i, 5 + TS_CF_L_CUT, .92f, .5f);
            map_add(c, 0, i, 5 + TS_CF_L_DRIVE, 0, .35f);
        }
        if (s->type == TS_SISTER_FX_DISTORTION) {
            map_add(c, 0, i, 0, .08f, .65f);
            s->value[4] = .32f;
        }
        if (s->type == TS_SISTER_FX_REVERB) {
            map_add(c, 1, i, 1, .2f, .75f);
            map_add(c, 3, i, 3, .15f, .7f);
            s->value[0] = .55f + preset * .025f;
        }
        if (s->type == TS_SISTER_FX_DELAY) {
            map_add(c, 1, i, 1, .1f, .65f);
            map_add(c, 3, i, 3, .1f, .45f);
        }
        if (s->type == TS_SISTER_FX_DISPERSION) {
            map_add(c, 2, i, 0, .65f, .18f);
            map_add(c, 0, i, 1, .08f, .7f);
        }
        if (s->type == TS_SISTER_FX_RESONATOR) {
            map_add(c, 1, i, 1, .25f, .8f);
            map_add(c, 2, i, 2, .75f, .2f);
            s->value[3] = .45f;
        }
    }
    /* Visible, saved factory output trims, calibrated with sustained noise
       and pitched material. Custom racks keep exact serial-chain gain. */
    static const float makeup_db[TS_META_PRESETS]={0,4,2,6,5,2,4,3,0,5};
    if(c->count)c->stage[c->count-1].value[4]=.5f+makeup_db[preset]/24;
    for (int i = 0; i < 4; ++i) {
        c->macro[i] = .3f;
        c->scene[0][i] = .15f;
        c->scene[1][i] = .75f;
    }
    c->captured = 3;
}
void ts_meta_from_board(TsMetaControls *c, const TsSisterFxControls *b, int first, int count) {
    ts_meta_default(c);
    snprintf(c->name, sizeof(c->name), "PEDALBOARD EXTRACT");
    if (first < 0)
        first = 0;
    if (count > 8)
        count = 8;
    for (int i = first; i < b->slot_count && i < first + count; ++i) {
        const TsSisterFxSlotControls *s = &b->slot[i];
        if (!s->type)
            continue;
        TsMetaStage *v = &c->stage[c->count++];
        ts_meta_stage_default(v, s->type);
        v->enabled = s->enabled;
        v->value[0] = s->parameter_a;
        v->value[1] = s->parameter_b;
        v->value[2] = s->parameter_c;
        v->value[3] = s->mix;
        v->value[4] = (s->gain_db + 12) / 24;
        v->power = s->transient_power;
        if (s->filter.initialized)
            memcpy(v->filter, s->filter.value, sizeof(v->filter));
    }
}
struct TsMetaEngine {
    TsMetaControls controls;
    TsSisterSingleFx *child[TS_META_STAGES];
    float macro[TS_META_MACROS], gate[TS_META_STAGES];
    unsigned clock, step;
    float smooth, gate_smooth;
};
static void refresh(TsMetaEngine *e, int cold) {
    float target[TS_META_MACROS];
    effective(&e->controls, target);
    for (int i = 0; i < 4; ++i)
        e->macro[i] = cold ? target[i] : e->macro[i] + (target[i] - e->macro[i]) * e->smooth;
    for (int i = 0; i < e->controls.count; ++i) {
        TsMetaStage stage = e->controls.stage[i];
        for (int j = 0; j < e->controls.map_count; ++j) {
            TsMetaMap *m = &e->controls.map[j];
            if (m->stage != i)
                continue;
            float v = m->low + (m->high - m->low) * e->macro[m->macro];
            if (m->field < 5)
                ts_meta_stage_set(&stage, m->field, v);
            else
                stage.filter[m->field - 5] = v;
        }
        TsSisterFxSlotControls c = {.type = (TsSisterFxType)stage.type,
                                    .enabled = stage.enabled,
                                    .mix = stage.value[3],
                                    .gain_db = stage.value[4] * 24 - 12,
                                    .parameter_a = stage.value[0],
                                    .parameter_b = stage.value[1],
                                    .parameter_c = stage.value[2],
                                    .transient_power = stage.power};
        ts_character_filter_default(&c.filter);
        memcpy(c.filter.value, stage.filter, sizeof(stage.filter));
        ts_sister_single_fx_set(e->child[i], &c);
    }
}
TsMetaEngine *ts_meta_new(const TsMetaControls *c, unsigned rate, uint32_t seed) {
    if (!ts_meta_valid(c) || rate < 1000 || rate > 384000)
        return NULL;
    TsMetaEngine *e = calloc(1, sizeof(*e));
    if (!e)
        return NULL;
    e->controls = *c;
    e->step = rate / 150;
    if (!e->step)
        e->step = 1;
    e->smooth = 1 - expf(-(float)e->step / (.025f * rate));
    for (int i = 0; i < c->count; ++i)
        if (c->stage[i].type && !(e->child[i] = ts_sister_single_fx_new(
                                      c->stage[i].type, rate, seed + (i + 1) * UINT32_C(0x9e3779b9)))) {
            ts_meta_free(e);
            return NULL;
        }
    for (int i = 0; i < c->count; ++i)
        e->gate[i] = c->stage[i].enabled;
    e->gate_smooth = 1 - expf(-1.f / (.012f * rate));
    refresh(e, 1);
    return e;
}
void ts_meta_free(TsMetaEngine *e) {
    if (!e)
        return;
    for (int i = 0; i < 8; ++i)
        ts_sister_single_fx_free(e->child[i]);
    free(e);
}
int ts_meta_topology_matches(const TsMetaEngine *e, const TsMetaControls *c) {
    return e && ts_meta_same_topology(&e->controls, c);
}
void ts_meta_set(TsMetaEngine *e, const TsMetaControls *c) {
    if (e && ts_meta_valid(c) && ts_meta_same_topology(&e->controls, c))
        e->controls = *c;
}
TsStereoFrame ts_meta_process(TsMetaEngine *e, TsStereoFrame in) {
    if (!e)
        return in;
    if (!e->clock--) {
        e->clock = e->step - 1;
        refresh(e, 0);
    }
    for (int i = 0; i < e->controls.count; ++i)
        if (e->child[i]) {
            e->gate[i] += (e->controls.stage[i].enabled - e->gate[i]) * e->gate_smooth;
            if (fabsf(e->gate[i] - e->controls.stage[i].enabled) < 1e-6f)
                e->gate[i] = e->controls.stage[i].enabled;
            in = ts_sister_single_fx_process(e->child[i], in, e->gate[i]);
        }
    return ts_stereo_frame_sanitize(in);
}
size_t ts_meta_memory(const TsMetaEngine *e) {
    if (!e)
        return 0;
    size_t n = sizeof(*e);
    for (int i = 0; i < 8; ++i)
        n += ts_sister_single_fx_memory(e->child[i]);
    return n;
}

int ts_meta_write(FILE *f, const char *p, const TsMetaControls *c) {
    if (!ts_meta_valid(c))
        return 0;
    if (fprintf(f, "%s.Header=1,%d,%d,%d,%d,%d,%.9g\n%s.Name=%s\n", p, c->count, c->map_count, c->preset,
                c->scene_mode, c->captured, c->morph, p, c->name) < 0)
        return 0;
    for (int i = 0; i < 4; ++i)
        if (fprintf(f, "%s.Label%d=%s\n%s.Macro%d=%.9g,%.9g,%.9g\n", p, i, c->label[i], p, i, c->macro[i],
                    c->scene[0][i], c->scene[1][i]) < 0)
            return 0;
    for (int i = 0; i < 8; ++i) {
        const TsMetaStage *s = &c->stage[i];
        if (fprintf(f, "%s.Stage%d=%d,%d,%u", p, i, s->type, s->enabled, (unsigned)s->power) < 0)
            return 0;
        for (int j = 0; j < 5; ++j)
            if (fprintf(f, ",%.9g", s->value[j]) < 0)
                return 0;
        for (int j = 0; j < TS_CF_COUNT; ++j)
            if (fprintf(f, ",%.9g", s->filter[j]) < 0)
                return 0;
        if (fputc('\n', f) == EOF)
            return 0;
    }
    for (int i = 0; i < c->map_count; ++i) {
        const TsMetaMap *m = &c->map[i];
        if (fprintf(f, "%s.Map%d=%u,%u,%u,%.9g,%.9g\n", p, i, m->macro, m->stage, m->field, m->low, m->high) <
            0)
            return 0;
    }
    return fprintf(f, "%s.End=1\n", p) >= 0;
}
static int numbers(const char *s, double *v, int n) {
    for (int i = 0; i < n; ++i) {
        char *end;
        errno = 0;
        v[i] = strtod(s, &end);
        if (errno || end == s || !isfinite(v[i]) || (i < n - 1 ? *end != ',' : *end != 0))
            return 0;
        s = end + (i < n - 1);
    }
    return 1;
}
static int whole(double x, int low, int high) { return x >= low && x <= high && floor(x) == x; }
int ts_meta_read(TsMetaControls *c, const char *key, const char *value) {
    if (!c->initialized)
        ts_meta_default(c);
    int i = -1, n = 0;
    double v[3 + 5 + TS_CF_COUNT];
    if (!strcmp(key, "Header")) {
        if (!numbers(value, v, 7) || v[0] != 1 || !whole(v[1], 0, 8) || !whole(v[2], 0, TS_META_MAPS) ||
            !whole(v[3], -1, TS_META_PRESETS - 1) || !whole(v[4], 0, 1) || !whole(v[5], 0, 3) || v[6] < 0 ||
            v[6] > 1)
            return -1;
        c->initialized = 2; /* Incomplete until the End record commits the rack. */
        c->count = v[1];
        c->map_count = v[2];
        c->preset = v[3];
        c->scene_mode = v[4];
        c->captured = v[5];
        c->morph = v[6];
        return 1;
    }
    if (!strcmp(key, "Name")) {
        if (!name_valid(value, 32))
            return -1;
        snprintf(c->name, 32, "%s", value);
        return 1;
    }
    if (sscanf(key, "Label%d%n", &i, &n) == 1 && !key[n] && i >= 0 && i < 4) {
        if (!name_valid(value, 16))
            return -1;
        snprintf(c->label[i], 16, "%s", value);
        return 1;
    }
    if (sscanf(key, "Macro%d%n", &i, &n) == 1 && !key[n] && i >= 0 && i < 4) {
        if (!numbers(value, v, 3))
            return -1;
        for (int j = 0; j < 3; ++j)
            if (v[j] < 0 || v[j] > 1)
                return -1;
        c->macro[i] = v[0];
        c->scene[0][i] = v[1];
        c->scene[1][i] = v[2];
        return 1;
    }
    if (sscanf(key, "Stage%d%n", &i, &n) == 1 && !key[n] && i >= 0 && i < 8) {
        if (!numbers(value, v, 8 + TS_CF_COUNT) || !whole(v[0], 0, TS_SISTER_FX_SUNVOX - 1) ||
            !whole(v[1], 0, 1) || !whole(v[2], 0, 3))
            return -1;
        for (int j = 3; j < 8 + TS_CF_COUNT; ++j)
            if (v[j] < 0 || v[j] > 1)
                return -1;
        TsMetaStage *s = &c->stage[i];
        s->type = v[0];
        s->enabled = v[1];
        s->power = v[2];
        for (int j = 0; j < 5; ++j)
            s->value[j] = v[j + 3];
        for (int j = 0; j < TS_CF_COUNT; ++j)
            s->filter[j] = v[j + 8];
        return 1;
    }
    if (sscanf(key, "Map%d%n", &i, &n) == 1 && !key[n] && i >= 0 && i < TS_META_MAPS) {
        if (!numbers(value, v, 5) || !whole(v[0], 0, 3) || !whole(v[1], 0, 7) ||
            !whole(v[2], 0, 4 + TS_CF_COUNT) || v[3] < 0 || v[3] > 1 || v[4] < 0 || v[4] > 1)
            return -1;
        c->map[i] = (TsMetaMap){v[0], v[1], v[2], v[3], v[4]};
        return 1;
    }
    if (!strcmp(key, "End")) {
        if (strcmp(value, "1"))
            return -1;
        c->initialized = 1;
        return 1;
    }
    return 0;
}
int ts_meta_save(const TsMetaControls *c, const char *path, char *error, size_t size) {
    char temp[2048];
    int ok = ts_meta_valid(c) && path && *path && strlen(path) < 2000;
    if (ok) {
        snprintf(temp, sizeof(temp), "%s.tmp", path);
        FILE *f = fopen(temp, "wb");
        ok = f != NULL;
        if (f) {
            ok = fprintf(f, "TapeSister Meta FX 1\n") >= 0 && ts_meta_write(f, "Meta", c);
            if (fclose(f))
                ok = 0;
#ifdef _WIN32
            if (ok)
                ok = MoveFileExA(temp, path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
            if (ok)
                ok = rename(temp, path) == 0;
#endif
            if (!ok)
                remove(temp);
        }
    }
    if (error && size)
        snprintf(error, size, "%s", ok ? "SAVED META FX" : "COULD NOT SAVE META FX");
    return ok;
}
int ts_meta_load(TsMetaControls *c, const char *path, char *error, size_t size) {
    FILE *f = fopen(path, "rb");
    int ok = 0, header = 0, end = 0;
    char line[4096];
    TsMetaControls next;
    ts_meta_default(&next);
    if (f) {
        if (fgets(line, sizeof(line), f) &&
            (!strcmp(line, "TapeSister Meta FX 1\n") || !strcmp(line, "TapeSister Meta FX 1\r\n"))) {
            ok = 1;
            while (fgets(line, sizeof(line), f)) {
                if (!strchr(line, '\n') && !feof(f)) {
                    ok = 0;
                    break;
                }
                line[strcspn(line, "\r\n")] = 0;
                char *equal = strchr(line, '=');
                if (!equal || strncmp(line, "Meta.", 5)) {
                    ok = 0;
                    break;
                }
                *equal++ = 0;
                if (end || ts_meta_read(&next, line + 5, equal) != 1) {
                    ok = 0;
                    break;
                }
                header |= !strcmp(line + 5, "Header");
                end = !strcmp(line + 5, "End");
            }
        }
        if (ferror(f))
            ok = 0;
        fclose(f);
    }
    ok = ok && header && end && ts_meta_valid(&next);
    if (ok)
        *c = next;
    if (error && size)
        snprintf(error, size, "%s", ok ? "LOADED META FX" : "INVALID OR UNAVAILABLE META FX");
    return ok;
}
