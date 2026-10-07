#include "tapesister/spatial.h"
#include "tapesister/sister_fallout.h"
#include "../third_party/atk/foa_transform.h"
#include <errno.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979323846
#define ROOT2 1.4142135623730951f
#define HOP 64u

static float bound(float v, float lo, float hi)
{ return !isfinite(v) ? lo : fmaxf(lo, fminf(hi, v)); }
static float wrap(float v)
{ return isfinite(v) ? v - floorf(v) : .5f; }
static float angle_delta(float a, float b)
{ float d = a - b; return d - floorf(d + .5f); }

const char *ts_spatial_parameter_name(int p)
{
    static const char *names[] = {"ROTATION", "SPREAD", "DIRECTION", "AMOUNT", "DIRECTIVITY"};
    return p >= 0 && p < TS_SPATIAL_PARAMS ? names[p] : "";
}
const char *ts_spatial_transform_name(int p)
{
    static const char *names[] = {"FOCUS", "PRESS", "PUSH", "ZOOM"};
    return p >= 0 && p < 4 ? names[p] : "FOCUS";
}
void ts_spatial_ring(TsSpatialControls *c, int n, int point)
{
    if (!c) return;
    n = n < 3 ? 3 : n > TS_SPATIAL_SPEAKERS ? TS_SPATIAL_SPEAKERS : n;
    c->speakers = n; c->point = !!point;
    for (int i = 0; i < TS_SPATIAL_SPEAKERS; ++i) {
        /* Regular rings run anticlockwise. Quad follows familiar FL/FR/RR/RL. */
        const int quad[] = {0, 3, 2, 1};
        int index = n == 4 ? quad[i % 4] : i;
        float degrees = fmodf((index + (point ? 0.f : .5f)) * 360.f / n, 360.f);
        c->azimuth[i] = degrees > 180.f ? degrees - 360.f : degrees;
        c->output[i] = n == 4 ? (int[]){0,1,3,2}[i % 4] : i; c->trim_db[i] = c->delay_ms[i] = 0.f;
    }
}
void ts_spatial_default(TsSpatialControls *c)
{
    memset(c, 0, sizeof(*c));
    ts_spatial_ring(c, 4, 0);
    c->value[TS_SPATIAL_ROTATE] = c->value[TS_SPATIAL_AIM] = .5f;
    c->value[TS_SPATIAL_SPREAD] = .25f;
    c->value[TS_SPATIAL_DIRECTIVITY] = 1.f;
    c->lfo_rate = ts_sister_fallout_lfo_normalized(1.f / 60.f);
    c->rise_length = ts_sister_fallout_rise_normalized(60.f);
    c->morph_seconds = 240.f;
    for (int i = 0; i < 2; ++i) memcpy(c->state[i], c->value, sizeof(c->value));
}
void ts_spatial_sanitize(TsSpatialControls *c)
{
    c->enabled = !!c->enabled; c->mono = !!c->mono; c->point = !!c->point;
    c->speakers = c->speakers < 3 ? 3 : c->speakers > 16 ? 16 : c->speakers;
    if (c->stereo_pair < 0 || c->stereo_pair >= c->speakers) c->stereo_pair = 0;
    if (c->decoder < 0 || c->decoder > 2) c->decoder = 0;
    if (c->transform < 0 || c->transform > 3) c->transform = 0;
    for (int i = 0; i < TS_SPATIAL_PARAMS; ++i) {
        c->value[i] = bound(c->value[i], 0, 1);
        for (int j = 0; j < 2; ++j) c->state[j][i] = bound(c->state[j][i], 0, 1);
    }
    for (int i = 0; i < TS_SPATIAL_SPEAKERS; ++i) {
        c->azimuth[i] = bound(c->azimuth[i], -180, 180);
        c->trim_db[i] = bound(c->trim_db[i], -60, 6);
        c->delay_ms[i] = bound(c->delay_ms[i], 0, 20);
        c->output[i] = c->output[i] < 0 ? 0 : c->output[i] > 63 ? 63 : c->output[i];
    }
    c->lfo_rate = bound(c->lfo_rate, 0, 1); c->lfo_depth = bound(c->lfo_depth, 0, 1);
    c->rise_length = bound(c->rise_length, 0, 1); c->rise_depth = bound(c->rise_depth, 0, 1);
    c->lfo_targets &= 31u; c->rise_targets &= 31u; c->captured &= 3u;
    c->rise_loop = !!c->rise_loop; c->morph_target = !!c->morph_target;
    c->morph_seconds = bound(c->morph_seconds, .01f, 3600.f);
}

/* Horizontal least-squares decoder A (A^T A)^-1. Regular rings reduce to
   the familiar 1/N, 2cos(theta)/N, 2sin(theta)/N coefficients. FuMa W
   receives sqrt(2); max-rE and in-phase weight first-order components. */
int ts_spatial_decoder(const TsSpatialControls *c, float out[TS_SPATIAL_SPEAKERS][3])
{
    double a[16][3], g[3][6] = {{0}};
    memset(out, 0, sizeof(float) * TS_SPATIAL_SPEAKERS * 3);
    if (c->speakers < 3 || c->speakers > 16) return 0;
    for (int i = 0; i < c->speakers; ++i) {
        double t = c->azimuth[i] * PI / 180.;
        a[i][0] = 1; a[i][1] = cos(t); a[i][2] = sin(t);
        for (int j = 0; j < 3; ++j)
            for (int k = 0; k < 3; ++k) g[j][k] += a[i][j] * a[i][k];
    }
    for (int j = 0; j < 3; ++j) g[j][j+3] = 1;
    for (int j = 0; j < 3; ++j) {
        int pivot = j;
        for (int k = j+1; k < 3; ++k) if (fabs(g[k][j]) > fabs(g[pivot][j])) pivot = k;
        if (fabs(g[pivot][j]) < .001 * c->speakers) return 0;
        for (int k = 0; k < 6; ++k) { double v = g[j][k]; g[j][k] = g[pivot][k]; g[pivot][k] = v; }
        double divisor = g[j][j];
        for (int k = 0; k < 6; ++k) g[j][k] /= divisor;
        for (int row = 0; row < 3; ++row) if (row != j) {
            double scale = g[row][j];
            for (int k = 0; k < 6; ++k) g[row][k] -= scale * g[j][k];
        }
    }
    float weight = c->decoder == 1 ? .5f : c->decoder == 2 ? 1.f : .70710678f;
    for (int i = 0; i < c->speakers; ++i) for (int j = 0; j < 3; ++j) {
        double v = 0;
        for (int k = 0; k < 3; ++k) v += a[i][k] * g[k][j+3];
        v *= j ? weight : ROOT2;
        if (!isfinite(v) || fabs(v) > 4.) return 0;
        out[i][j] = (float)v;
    }
    return 1;
}
void ts_spatial_transform(float f[4], const float v[TS_SPATIAL_PARAMS], int kind)
{
    float rot = (v[TS_SPATIAL_ROTATE] - .5f) * 2.f * PI;
    float cr = cosf(rot), sr = sinf(rot), x = f[1], y = f[2];
    f[1] = cr*x - sr*y; f[2] = sr*x + cr*y;
    float aim = (v[TS_SPATIAL_AIM] - .5f) * 2.f * PI;
    cr = cosf(aim); sr = sinf(aim); x = f[1]; y = f[2];
    f[1] = cr*x + sr*y; f[2] = -sr*x + cr*y;
    ts_atk_axial(f, kind, v[TS_SPATIAL_AMOUNT] * .5f * PI);
    x = f[1]; y = f[2];
    f[1] = (cr*x - sr*y) * v[TS_SPATIAL_DIRECTIVITY];
    f[2] = (sr*x + cr*y) * v[TS_SPATIAL_DIRECTIVITY];
    f[3] *= v[TS_SPATIAL_DIRECTIVITY];
}
void ts_spatial_init(TsSpatial *s)
{
    memset(s, 0, sizeof(*s)); s->test_speaker = s->view.test_speaker = -1;
    ts_spatial_default(&s->controls);
    ts_spatial_set(s, &s->controls);
}
int ts_spatial_prepare(TsSpatial *s, unsigned rate)
{
    if (rate < 1000 || rate > 384000) return 0;
    size_t frames = (size_t)ceil(rate * .020) + 2;
    if (s->delay_frames != frames || !s->delay) {
        float *next = calloc(frames * TS_SPATIAL_SPEAKERS, sizeof(float));
        if (!next) { s->ready = 0; return 0; }
        free(s->delay); s->delay = next; s->delay_frames = frames; s->write = 0;
    }
    s->sample_rate = rate; s->smoothing = 1.f - expf(-1.f / (.02f * rate));
    s->meter_decay = expf(-1.f / (.20f * rate));
    memset(s->dry,0,sizeof(s->dry));
    s->dry[s->controls.output[s->controls.stereo_pair]][0]=1;
    s->dry[s->controls.output[(s->controls.stereo_pair+1)%s->controls.speakers]][1]=1;
    s->limit_gain = 1; s->ready = 1; s->hop = 0;
    return 1;
}
void ts_spatial_free(TsSpatial *s)
{ free(s->delay); s->delay = NULL; s->ready = 0; s->delay_frames = 0; }
void ts_spatial_set(TsSpatial *s, const TsSpatialControls *controls)
{
    TsSpatialControls c = *controls; ts_spatial_sanitize(&c);
    if (c.morph_trigger != s->morph_seen) {
        s->morph_seen = c.morph_trigger;
        memcpy(s->morph_from, s->base, sizeof(s->base));
        s->morph_elapsed = 0; s->morph_active = c.morph_trigger && (c.captured & (1u << c.morph_target)) != 0;
        if (s->morph_active) memcpy(c.value, c.state[c.morph_target], sizeof(c.value));
    } else if (memcmp(c.value, s->controls.value, sizeof(c.value))) s->morph_active = 0;
    s->controls = c;
    if (!s->morph_active) memcpy(s->base, c.value, sizeof(s->base));
    s->layout_valid = ts_spatial_decoder(&c, s->decoder);
    s->view.layout_valid=s->layout_valid;
    if(!s->ready)memcpy(s->view.value,c.value,sizeof(c.value));
    if (c.rise_trigger != s->rise_seen) {
        s->rise_seen = c.rise_trigger; s->rise_phase = 0; s->rise_active = c.rise_trigger != 0;
    }
    s->hop = 0;
}
void ts_spatial_recall(TsSpatial *s, const TsSpatialControls *controls)
{
    TsSpatialControls c = *controls;
    c.rise_trigger = c.morph_trigger = 0;
    s->rise_seen = s->morph_seen = 0;
    s->rise_active = s->morph_active = 0;
    s->lfo_phase = s->rise_phase = s->morph_elapsed = 0;
    s->view.lfo = s->view.rise = s->view.morph = 0;
    s->test_remaining = 0;
    ts_spatial_set(s, &c);
}
void ts_spatial_test(TsSpatial *s, int speaker)
{
    /* Keep the old destination while STOP fades its tone out. */
    if (speaker >= 0 && speaker < s->controls.speakers) {
        if (speaker != s->test_speaker) s->test_gain = 0;
        s->test_speaker = speaker;
        s->test_remaining = s->sample_rate;
    } else s->test_remaining = 0;
}
static void spatial_tick(TsSpatial *s, unsigned channels, int pair)
{
    const TsSpatialControls *c = &s->controls;
    double dt = (double)HOP / s->sample_rate;
    s->lfo_phase += dt * ts_sister_fallout_lfo_hz(c->lfo_rate);
    s->lfo_phase -= floor(s->lfo_phase);
    s->view.lfo = sinf((float)(2 * PI * s->lfo_phase));
    if (s->rise_active || c->rise_loop) {
        s->rise_phase += dt / ts_sister_fallout_rise_seconds(c->rise_length);
        if (s->rise_phase >= 1.) {
            if (c->rise_loop) s->rise_phase -= floor(s->rise_phase);
            else { s->rise_phase = 1.; s->rise_active = 0; }
        }
    }
    s->view.rise = (float)s->rise_phase;
    if (s->morph_active) {
        s->morph_elapsed += dt;
        float t = bound((float)(s->morph_elapsed / c->morph_seconds), 0, 1);
        for (int i = 0; i < TS_SPATIAL_PARAMS; ++i) {
            float d = c->value[i] - s->morph_from[i];
            if (i == TS_SPATIAL_ROTATE || i == TS_SPATIAL_AIM) d = angle_delta(c->value[i], s->morph_from[i]);
            s->base[i] = s->morph_from[i] + d*t;
        }
        s->view.morph = t;
        if (t >= 1) s->morph_active = 0;
    }
    for (int i = 0; i < TS_SPATIAL_PARAMS; ++i) {
        float v = s->base[i];
        if (c->lfo_targets & (1u << i)) v += .5f * c->lfo_depth * s->view.lfo;
        if (c->rise_targets & (1u << i)) v += c->rise_depth * s->view.rise;
        s->view.value[i] = i == TS_SPATIAL_ROTATE || i == TS_SPATIAL_AIM ? wrap(v) : bound(v, 0, 1);
    }
    int available = s->layout_valid, conflict = 0;
    for (int i = 0; i < c->speakers; ++i) {
        if ((unsigned)c->output[i] >= channels) available = 0;
        if (pair > 0 && (c->output[i] == pair*2 || c->output[i] == pair*2+1)) conflict = 1;
        for (int j = 0; j < i; ++j) if (c->output[i] == c->output[j]) available = 0;
    }
    s->dry_output[0]=c->output[c->stereo_pair];
    s->dry_output[1]=c->output[(c->stereo_pair+1)%c->speakers];
    s->stereo_available=s->dry_output[0]!=s->dry_output[1];
    for(int i=0;i<2;++i) if((unsigned)s->dry_output[i]>=channels ||
        (pair>0 && (s->dry_output[i]==pair*2 || s->dry_output[i]==pair*2+1)))s->stereo_available=0;
    if(!s->stereo_available){s->dry_output[0]=0;s->dry_output[1]=1;}
    s->view.stereo_available = s->stereo_available;
    s->view.available = available && !conflict;
    s->view.conflict = conflict; s->view.channels = channels;
    s->view.layout_valid = s->layout_valid; s->view.morph_active = s->morph_active;
    if(!c->enabled && s->wet<.00001f)return;
    memset(s->target, 0, sizeof(s->target));
    for (int ch = 0; ch < 2; ++ch) {
        float theta = c->mono ? 0 : (ch ? -1.f : 1.f) * s->view.value[TS_SPATIAL_SPREAD] * PI;
        float gain = c->mono ? .5f : .70710678f;
        float f[4] = {gain / ROOT2, gain*cosf(theta), gain*sinf(theta), 0};
        ts_spatial_transform(f, s->view.value, c->transform);
        for (int i = 0; i < c->speakers; ++i) {
            float v = 0;
            for (int j = 0; j < 3; ++j) v += s->decoder[i][j] * f[j];
            s->target[i][ch] = v * powf(10.f, c->trim_db[i] / 20.f);
        }
    }
}
void ts_spatial_process(TsSpatial *s, TsStereoFrame in, float *out, unsigned channels, int pair)
{
    if (!s->ready || channels<2) return;
    const TsSpatialControls *c = &s->controls;
    if (!s->hop--) { spatial_tick(s, channels, pair); s->hop = HOP-1; }
    float active = c->enabled && s->view.available ? 1.f : 0.f;
    s->wet += s->smoothing * (active - s->wet);
    if (fabsf(active-s->wet)<.0001f) s->wet=active;
    s->view.wet = s->wet;
    in = ts_stereo_frame_sanitize(in);
    int field_active=c->enabled || s->wet>=.00001f;
    if(!field_active)s->wet=0;
    float wet[TS_SPATIAL_SPEAKERS] = {0};
    float maximum = 0;
    for (int i = 0; field_active && i < TS_SPATIAL_SPEAKERS; ++i) {
        for (int j = 0; j < 2; ++j) s->matrix[i][j] += s->smoothing * (s->target[i][j] - s->matrix[i][j]);
        float v = in.l * s->matrix[i][0] + in.r * s->matrix[i][1];
        s->delay[(size_t)i*s->delay_frames + s->write] = v;
        float target = c->delay_ms[i] * .001f * s->sample_rate;
        s->delay_current[i] += s->smoothing * (target - s->delay_current[i]);
        float read = (float)s->write - s->delay_current[i];
        if (read < 0) read += (float)s->delay_frames;
        /* Tiny negative offsets can round exactly to the ring length. */
        if (read >= (float)s->delay_frames) read = 0;
        size_t a = (size_t)read, b = (a+1) % s->delay_frames;
        float *line = s->delay + (size_t)i*s->delay_frames;
        wet[i] = line[a] + (line[b]-line[a])*(read-a);
        if (i < c->speakers) maximum = fmaxf(maximum, fabsf(wet[i]));
    }
    if(field_active)s->write = (s->write+1) % s->delay_frames;
    float gain = maximum > .98f ? .98f/maximum : 1.f;
    if (gain < s->limit_gain) s->limit_gain = gain;
    else s->limit_gain += s->smoothing * (gain-s->limit_gain);
    /* Bypass is an ordinary L/R signal on the selected adjacent speaker pair.
       Crossfade dry routing coefficients as well as spatial engage. */
    float spatial[64]={0};
    for (int i=0;i<c->speakers;++i) {
        float v=wet[i]*s->limit_gain*s->wet;
        unsigned ch=(unsigned)c->output[i];
        if(ch<64 && ch<channels)spatial[ch]+=v;
        s->view.peak[i]=fmaxf(fabsf(v),s->view.peak[i]*s->meter_decay);
    }
    for(unsigned ch=0;ch<64 && ch<channels;++ch) {
        int send=pair>0 && (ch==(unsigned)pair*2 || ch==(unsigned)pair*2+1);
        float dry=0;
        for(int j=0;j<2;++j){
            float t=(int)ch==s->dry_output[j]?1.f:0.f;
            s->dry[ch][j]+=s->smoothing*(t-s->dry[ch][j]);
            if(fabsf(s->dry[ch][j]-t)<.0001f)s->dry[ch][j]=t;
            dry+=s->dry[ch][j]*(j?in.r:in.l);
        }
        if(send)continue;
        if(ch<2 || dry!=0 || spatial[ch]!=0)
            out[ch]=bound(dry*(1-s->wet)+spatial[ch],-1.f,1.f);
    }
    int testing = s->test_remaining && s->view.available;
    s->test_gain += s->smoothing * ((testing ? .0631f : 0.f)-s->test_gain);
    if (s->test_remaining) --s->test_remaining;
    s->view.test_speaker = s->test_gain > .0001f ? s->test_speaker : -1;
    if (s->test_speaker >= 0 && s->test_speaker < c->speakers && s->view.available) {
        s->test_phase += 2*PI*440/s->sample_rate;
        if (s->test_phase >= 2*PI) s->test_phase -= 2*PI;
        unsigned ch = (unsigned)c->output[s->test_speaker];
        if (ch < channels) out[ch] = bound(out[ch] + sinf((float)s->test_phase)*s->test_gain, -.98f, .98f);
    }
}

int ts_spatial_write(FILE *f, const TsSpatialControls *c)
{
    if (fprintf(f,"Spatial.StereoPair=%d\n",c->stereo_pair)<0)return 0;
    if (fprintf(f, "Spatial.Enabled=%d\nSpatial.Mono=%d\nSpatial.Speakers=%d\nSpatial.Point=%d\nSpatial.Decoder=%d\nSpatial.Transform=%d\n",
        c->enabled,c->mono,c->speakers,c->point,c->decoder,c->transform)<0) return 0;
    for (int i=0;i<5;++i) if(fprintf(f,"Spatial.Value%d=%.9g\nSpatial.A%d=%.9g\nSpatial.B%d=%.9g\n",i,c->value[i],i,c->state[0][i],i,c->state[1][i])<0)return 0;
    for (int i=0;i<16;++i) if(fprintf(f,"Spatial.Angle%d=%.9g\nSpatial.Output%d=%d\nSpatial.Trim%d=%.9g\nSpatial.Delay%d=%.9g\n",i,c->azimuth[i],i,c->output[i],i,c->trim_db[i],i,c->delay_ms[i])<0)return 0;
    return fprintf(f,"Spatial.LfoRate=%.9g\nSpatial.LfoDepth=%.9g\nSpatial.LfoTargets=%u\nSpatial.RiseLength=%.9g\nSpatial.RiseDepth=%.9g\nSpatial.RiseTargets=%u\nSpatial.RiseLoop=%d\nSpatial.Captured=%u\nSpatial.MorphSeconds=%.9g\nSpatial.MorphTarget=%d\n",
        c->lfo_rate,c->lfo_depth,c->lfo_targets,c->rise_length,c->rise_depth,c->rise_targets,c->rise_loop,c->captured,c->morph_seconds,c->morph_target)>=0;
}
int ts_spatial_read(TsSpatialControls *c, const char *key, const char *value)
{
    if (strncmp(key,"Spatial.",8)) return 0;
    char *end; errno=0; double n=strtod(value,&end);
    if(errno || end==value || *end || !isfinite(n))return -1;
    const char *k=key+8;
#define INT_FIELD(name,field,lo,hi) if(!strcmp(k,name)){if(n<lo||n>hi||n!=floor(n))return -1;c->field=(int)n;return 1;}
#define FLOAT_FIELD(name,field,lo,hi) if(!strcmp(k,name)){if(n<lo||n>hi)return -1;c->field=(float)n;return 1;}
    INT_FIELD("StereoPair",stereo_pair,0,15)
    INT_FIELD("Enabled",enabled,0,1) INT_FIELD("Mono",mono,0,1)
    INT_FIELD("Speakers",speakers,3,16) INT_FIELD("Point",point,0,1)
    INT_FIELD("Decoder",decoder,0,2) INT_FIELD("Transform",transform,0,3)
    INT_FIELD("LfoTargets",lfo_targets,0,31) INT_FIELD("RiseTargets",rise_targets,0,31)
    INT_FIELD("RiseLoop",rise_loop,0,1) INT_FIELD("Captured",captured,0,3)
    FLOAT_FIELD("LfoRate",lfo_rate,0,1) FLOAT_FIELD("LfoDepth",lfo_depth,0,1)
    FLOAT_FIELD("RiseLength",rise_length,0,1) FLOAT_FIELD("RiseDepth",rise_depth,0,1)
    INT_FIELD("MorphTarget",morph_target,0,1)
    FLOAT_FIELD("MorphSeconds",morph_seconds,.01,3600)
    for(int i=0;i<16;++i){char name[32];
        snprintf(name,sizeof(name),"Angle%d",i); FLOAT_FIELD(name,azimuth[i],-180,180)
        snprintf(name,sizeof(name),"Output%d",i); INT_FIELD(name,output[i],0,63)
        snprintf(name,sizeof(name),"Trim%d",i); FLOAT_FIELD(name,trim_db[i],-60,6)
        snprintf(name,sizeof(name),"Delay%d",i); FLOAT_FIELD(name,delay_ms[i],0,20)
        if(i<5){
            snprintf(name,sizeof(name),"Value%d",i); FLOAT_FIELD(name,value[i],0,1)
            snprintf(name,sizeof(name),"A%d",i); FLOAT_FIELD(name,state[0][i],0,1)
            snprintf(name,sizeof(name),"B%d",i); FLOAT_FIELD(name,state[1][i],0,1)
        }
    }
#undef INT_FIELD
#undef FLOAT_FIELD
    return 0;
}
