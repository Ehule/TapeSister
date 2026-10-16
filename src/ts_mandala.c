#include "tapesister/mandala.h"
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const uint32_t mandala_palettes[TS_MANDALA_PRESETS][TS_MANDALA_COLORS]={
    {0xD5B270,0x7EA39C,0x8D7296,0xB59E70,0x497074,0x96765A,0x5C5F78,0x04070A},
    {0x9CB9C5,0x9B8EBF,0x6C9EAA,0xC2B4A0,0x4A6D83,0x8693B6,0x4D6372,0x04070A},
    {0xC79470,0x996F6E,0x839279,0xC7B787,0x666958,0x9F7C93,0x705967,0x04070A},
    /* Bleed and Soak: crimson bodies, scarlet edges, incandescent highlights. */
    {0xFF5424,0xD91527,0xA50920,0xFFAD49,0x740A1A,0xF13219,0xBB202F,0x0D0104},
    /* TapeSister splash: illuminated brass, copper, plum and violet. */
    {0xF4BD58,0xBC8041,0x9860BD,0xFFE1A1,0x61346F,0xDA993F,0xC589B7,0x08040D}
};
const char *ts_mandala_palette_name(unsigned palette)
{
    static const char *names[]={"AMBER","LUNAR","EMBER","BLEED AND SOAK","TAPESISTER GOLD","CUSTOM"};
    return palette<=TS_MANDALA_CUSTOM?names[palette]:"UNKNOWN";
}
const char *ts_mandala_shape_name(unsigned shape)
{
    static const char *names[]={"MIXED","TRIANGLES","SQUARES","CIRCLES","HEXAGONS"};
    return shape<TS_MANDALA_SHAPES?names[shape]:"UNKNOWN";
}
uint32_t ts_mandala_color(const TsMandalaConfig *c,unsigned swatch)
{
    if(!c || swatch>=TS_MANDALA_COLORS)return 0;
    return c->palette<TS_MANDALA_PRESETS?mandala_palettes[c->palette][swatch]:c->custom_rgb[swatch];
}
void ts_mandala_set_color(TsMandalaConfig *c,unsigned swatch,uint32_t rgb)
{
    if(!c || swatch>=TS_MANDALA_COLORS)return;
    if(c->palette<TS_MANDALA_PRESETS)memcpy(c->custom_rgb,mandala_palettes[c->palette],sizeof(c->custom_rgb));
    c->palette=TS_MANDALA_CUSTOM;c->custom_rgb[swatch]=rgb&0xffffffu;
}
void ts_mandala_reset_geometry(TsMandalaConfig *c)
{
    c->shape=0;c->density=100;c->scale=100;c->trails=50;c->motion=50;
    c->symmetry=8;c->cycle_seconds=240;
}
void ts_mandala_reseed(TsMandalaConfig *c)
{
    static atomic_uint serial;
    struct timespec now = {0};
    timespec_get(&now, TIME_UTC);
    uint64_t s = (uint64_t)now.tv_sec ^ ((uint64_t)now.tv_nsec << 32) ^
        ((uint64_t)atomic_fetch_add(&serial, 1) * UINT64_C(0x9e3779b97f4a7c15)) ^ c->seed;
    s = (s ^ (s >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
    s = (s ^ (s >> 27)) * UINT64_C(0x94d049bb133111eb);
    c->seed = (s ^ (s >> 31)) | 1;
}
void ts_mandala_default(TsMandalaConfig *c)
{
    memset(c, 0, sizeof(*c));
    c->version = 2; ts_mandala_reset_geometry(c);
    memcpy(c->custom_rgb,mandala_palettes[0],sizeof(c->custom_rgb));
    ts_mandala_reseed(c);
}
int ts_mandala_valid(const TsMandalaConfig *c)
{
    if(!c || c->version!=2 || !c->seed || c->palette>TS_MANDALA_CUSTOM ||
        c->symmetry<4 || c->symmetry>12 || c->cycle_seconds<30 || c->cycle_seconds>1800 ||
        c->shape>=TS_MANDALA_SHAPES || c->density<10 || c->density>100 ||
        c->scale<50 || c->scale>150 || c->trails>100 || c->motion>100)return 0;
    for(unsigned i=0;i<TS_MANDALA_COLORS;++i)if(c->custom_rgb[i]>0xffffffu)return 0;
    return 1;
}
int ts_mandala_write(FILE *f, const TsMandalaConfig *c)
{
    if(!ts_mandala_valid(c) || fprintf(f,
        "Mandala.Version=2\nMandala.Seed=%016" PRIX64 "\nMandala.Palette=%u\nMandala.Symmetry=%u\nMandala.Cycle=%u\n"
        "Mandala.Shape=%u\nMandala.Density=%u\nMandala.Scale=%u\nMandala.Trails=%u\nMandala.Motion=%u\n",
        c->seed,c->palette,c->symmetry,c->cycle_seconds,c->shape,c->density,c->scale,c->trails,c->motion)<=0)return 0;
    for(unsigned i=0;i<TS_MANDALA_COLORS;++i)
        if(fprintf(f,"Mandala.Color%u=%06X\n",i,c->custom_rgb[i])<=0)return 0;
    return 1;
}
int ts_mandala_read(TsMandalaConfig *c, const char *key, const char *v)
{
    if (strncmp(key, "Mandala.", 8)) return 0;
    key += 8;
    char *end; errno = 0;
    if (!*v || *v == '-' || *v == '+') return -1;
    int color=!strncmp(key,"Color",5) && key[5]>='0' && key[5]<'0'+TS_MANDALA_COLORS && !key[6];
    uint64_t n = strtoull(v, &end, (!strcmp(key, "Seed") || color) ? 16 : 10);
    if (errno || *end || end == v) return -1;
    if (!strcmp(key, "Seed")) { if (!n) return -1; c->seed = n; }
    /* Defaults initialize new fields before parsing; v1 keeps its original look. */
    else if (!strcmp(key, "Version")) { if (n != 1 && n != 2) return -1; c->version = 2; }
    else if (!strcmp(key, "Palette")) { if (n > TS_MANDALA_CUSTOM) return -1; c->palette = (uint32_t)n; }
    else if (!strcmp(key, "Symmetry")) { if (n < 4 || n > 12) return -1; c->symmetry = (uint32_t)n; }
    else if (!strcmp(key, "Cycle")) { if (n < 30 || n > 1800) return -1; c->cycle_seconds = (uint32_t)n; }
    else if (!strcmp(key,"Shape")) { if(n>=TS_MANDALA_SHAPES)return -1;c->shape=(uint32_t)n; }
    else if (!strcmp(key,"Density")) { if(n<10 || n>100)return -1;c->density=(uint32_t)n; }
    else if (!strcmp(key,"Scale")) { if(n<50 || n>150)return -1;c->scale=(uint32_t)n; }
    else if (!strcmp(key,"Trails")) { if(n>100)return -1;c->trails=(uint32_t)n; }
    else if (!strcmp(key,"Motion")) { if(n>100)return -1;c->motion=(uint32_t)n; }
    else if (color) { if(n>0xffffffu)return -1;c->custom_rgb[key[5]-'0']=(uint32_t)n; }
    else return -1; /* Unknown visual versions/fields do not silently change identity. */
    return 1;
}
void ts_mandala_queue_init(TsMandalaQueue *q)
{
    memset(q->packet, 0, sizeof(q->packet));
    atomic_init(&q->enabled, 0); atomic_init(&q->write, 0);
    atomic_init(&q->read, 0); atomic_init(&q->dropped, 0);
}
int ts_mandala_lock_free(const TsMandalaQueue *q)
{
#if ATOMIC_INT_LOCK_FREE != 2
    (void)q; return 0;
#else
    return atomic_is_lock_free(&q->enabled) && atomic_is_lock_free(&q->write) &&
        atomic_is_lock_free(&q->read) && atomic_is_lock_free(&q->dropped);
#endif
}
void ts_mandala_push(TsMandalaQueue *q, const float *out, unsigned frames, unsigned channels)
{
#if ATOMIC_INT_LOCK_FREE != 2
    /* Even the disabled gate must never use a library-backed atomic lock. */
    (void)q; (void)out; (void)frames; (void)channels; return;
#else
    if (!atomic_load_explicit(&q->enabled, memory_order_relaxed) || !frames || !channels || channels > TS_MANDALA_CHANNELS) return;
    unsigned w = atomic_load_explicit(&q->write, memory_order_relaxed);
    unsigned r = atomic_load_explicit(&q->read, memory_order_acquire);
    if (w - r >= TS_MANDALA_QUEUE) {
        /* One writer; no atomic read/modify/write instruction needed. */
        unsigned d = atomic_load_explicit(&q->dropped, memory_order_relaxed);
        atomic_store_explicit(&q->dropped, d + 1, memory_order_relaxed); return;
    }
    TsMandalaPacket *p = &q->packet[w % TS_MANDALA_QUEUE];
    p->count = frames < TS_MANDALA_SAMPLES ? frames : TS_MANDALA_SAMPLES;
    p->channels = channels;
    for (unsigned i = 0; i < p->count; ++i) {
        unsigned start = (unsigned)((uint64_t)i * frames / p->count);
        unsigned end = (unsigned)((uint64_t)(i+1) * frames / p->count);
        /* Stratified, changing offsets prevent a coherent tone falling on
           every decimation zero crossing. These are energy samples, not a
           resampled signal or input to a spectrum analyzer. */
        unsigned phase = (i * 2654435761u) ^ (w * 2246822519u);
        unsigned at = start + phase % (end-start);
        memcpy(p->samples[i], out + (size_t)at * channels, channels * sizeof(float));
    }
    atomic_store_explicit(&q->write, w + 1, memory_order_release);
#endif
}
int ts_mandala_latest(TsMandalaQueue *q, TsMandalaPacket *p)
{
    unsigned r = atomic_load_explicit(&q->read, memory_order_relaxed);
    unsigned w = atomic_load_explicit(&q->write, memory_order_acquire);
    if (r == w) return 0;
    *p = q->packet[(w - 1) % TS_MANDALA_QUEUE];
    atomic_store_explicit(&q->read, w, memory_order_release);
    return 1;
}
void ts_mandala_analyze(const TsMandalaPacket *p, TsMandalaFeatures *f, float dt)
{
    double energy = 0, side = 0, left = 0, right = 0;
    float peak = 0;
    if (p && (p->count > TS_MANDALA_SAMPLES || p->channels > TS_MANDALA_CHANNELS)) p = NULL;
    if (p) {
        for (unsigned i = 0; i < p->count; ++i) for (unsigned c = 0; c < p->channels; ++c) {
            float v = p->samples[i][c];
            if (!isfinite(v)) v = 0;
            v = fmaxf(-1, fminf(1, v));
            double e = v * v; energy += e;
            if (c & 1) right += e; else left += e;
            peak = fmaxf(peak, fabsf(v));
            if ((c & 1) && isfinite(p->samples[i][c-1])) {
                float d = v - fmaxf(-1, fminf(1, p->samples[i][c-1])); side += d*d;
            }
        }
    }
    unsigned count = p ? p->count * p->channels : 0;
    float rms = count ? (float)sqrt(energy / count) : 0;
    /* Fixed -60 dB floor, no automatic gain that chases silence. Energy is
       averaged across physical channels; mirrored outputs do not double it. */
    float level = rms > .001f ? fminf(1, sqrtf(rms) * 1.8f) : 0;
    float blend = 1 - expf(-fmaxf(0, fminf(dt, .25f)) / (level > f->level ? .22f : 1.8f));
    f->onset += (fmaxf(0, level - f->level) - f->onset) * blend;
    f->level += (level - f->level) * blend;
    f->peak += ((rms > .001f ? peak : 0) - f->peak) * blend;
    f->width += ((energy > 1e-6 ? fminf(1, (float)(side / (2*energy))) : 0) - f->width) * blend;
    f->balance += ((energy > 1e-6 ? (float)((right-left)/energy) : 0) - f->balance) * blend;
}

void ts_mandala_effects_follow(TsMandalaEffects *current,const TsMandalaEffects *target,float dt)
{
    float blend=1-expf(-fmaxf(0,fminf(.25f,dt))/.45f);
    for(int i=0;i<TS_MANDALA_EFFECTS;++i) {
        float wet=target?target->wet[i]:0,size=target?target->size[i]:0,character=target?target->character[i]:0;
        wet=isfinite(wet)?fmaxf(0,fminf(1,wet)):0;
        size=isfinite(size)?fmaxf(0,fminf(1,size)):0;
        character=isfinite(character)?fmaxf(0,fminf(1,character)):0;
        current->wet[i]+=(wet-current->wet[i])*blend;
        current->size[i]+=(size-current->size[i])*blend;
        current->character[i]+=(character-current->character[i])*blend;
    }
}
