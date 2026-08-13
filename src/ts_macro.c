#include "tapesister/macro.h"
#include <math.h>

static float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
static float shaped(float v, float e) { return powf(clamp01(v), e); }

static const TsMacroPreset presets[TS_MACRO_PRESET_COUNT] = {
    {"CLEAN MATERIAL", {0.42f,0.10f,0.06f,0.04f,0.08f}},
    {"WARM WEIGHT", {0.82f,0.22f,0.10f,0.12f,0.38f}},
    {"DARK BLOOM", {0.88f,0.16f,0.28f,0.62f,0.26f}},
    {"BRIGHT DUST", {0.34f,0.72f,0.22f,0.18f,0.18f}},
    {"TAPE ORBIT", {0.62f,0.30f,0.72f,0.46f,0.42f}},
    {"HOLLOW ROOM", {0.44f,0.28f,0.24f,0.84f,0.16f}},
    {"PRESSED METAL", {0.52f,0.66f,0.16f,0.08f,0.88f}},
    {"WILD SIGNAL", {0.46f,0.86f,0.78f,0.32f,0.76f}}
};

void ts_macro_controls_reset(TsMacroControls *c)
{
    if (!c) return;
    c->body=0.5f; c->texture=0.0f; c->motion=0.0f; c->space=0.0f; c->pressure=0.0f;
}

int ts_macro_controls_valid(const TsMacroControls *c)
{
    return c && isfinite(c->body) && c->body>=0.0f && c->body<=1.0f &&
        isfinite(c->texture) && c->texture>=0.0f && c->texture<=1.0f &&
        isfinite(c->motion) && c->motion>=0.0f && c->motion<=1.0f &&
        isfinite(c->space) && c->space>=0.0f && c->space<=1.0f &&
        isfinite(c->pressure) && c->pressure>=0.0f && c->pressure<=1.0f;
}

int ts_macro_compile(TsProcessRecipe *p, const TsMacroControls *c, uint32_t seed)
{
    float t,m,s,q;
    if (!p || !ts_macro_controls_valid(c)) return 0;
    t=shaped(c->texture,1.35f); m=shaped(c->motion,1.20f);
    s=shaped(c->space,1.25f); q=shaped(c->pressure,1.30f);
    ts_process_recipe_reset(p); p->seed=seed;
    p->body=clamp01(0.18f+c->body*0.82f);
    p->edge=clamp01(t*0.62f+q*0.28f); p->drift=clamp01(m*0.86f+s*0.08f);
    p->noise_enabled=t>0.035f; p->noise_amount=clamp01(t*(0.06f+0.22f*(1.0f-q)));
    p->noise_color=t>0.72f?TS_NOISE_METALLIC:(c->body>0.62f?TS_NOISE_BROWN:TS_NOISE_PINK);
    p->delay_enabled=m>0.10f||s>0.24f; p->delay_seconds=0.045f+0.42f*m+0.12f*s;
    p->delay_feedback=clamp01(0.10f+0.54f*m+0.12f*s); if(p->delay_feedback>0.85f)p->delay_feedback=0.85f;
    p->delay_damping=clamp01(0.78f-0.38f*t+0.12f*c->body); p->delay_mix=clamp01(0.04f+0.24f*m+0.24f*s);
    p->reverb_enabled=s>0.06f; p->reverb_decay=clamp01(0.12f+0.72f*s); if(p->reverb_decay>0.90f)p->reverb_decay=0.90f;
    p->reverb_damping=clamp01(0.76f-0.34f*t+0.14f*c->body); p->reverb_mix=clamp01(0.03f+0.58f*s);
    p->filter_enabled=c->body>0.58f||t>0.46f;
    if(c->body>0.64f&&t<0.58f){p->filter_mode=TS_FILTER_LOWPASS;p->filter_cutoff_hz=9000.0f-7200.0f*shaped(c->body,1.5f);}
    else{p->filter_mode=t>0.78f?TS_FILTER_HIGHPASS:TS_FILTER_BANDPASS;p->filter_cutoff_hz=520.0f+6200.0f*t;}
    if(p->filter_cutoff_hz<20.0f)p->filter_cutoff_hz=20.0f;
    if(p->filter_cutoff_hz>20000.0f)p->filter_cutoff_hz=20000.0f;
    p->filter_resonance=clamp01(0.08f+0.54f*t+0.18f*m);
    p->shaper_enabled=q>0.045f; p->shaper_mode=q>0.82f&&t>0.68f?TS_SHAPER_FOLD:(q>0.72f?TS_SHAPER_CLIP:TS_SHAPER_TAPE);
    p->shaper_drive=1.0f+10.5f*q; p->shaper_mix=clamp01(0.05f+0.78f*q);
    return 1;
}

const TsMacroPreset *ts_macro_preset(int i)
{
    return i>=0&&i<TS_MACRO_PRESET_COUNT?&presets[i]:0;
}
