#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/prism.h"
#include "tapesister/sister_ui.h"
#include "tapesister/sister_project_state.h"
#include "tapesister/sister_preset.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static TsPrismControls settings(void)
{
    TsPrismControls c;
    ts_prism_controls_default(&c);
    c.enabled = 1; c.mix = 1; c.drift = 0;
    return c;
}

static float source(unsigned frame, unsigned rate, int kind)
{
    double phase = fmod(frame * 173.3 / rate, 1);
    unsigned random = frame * 747796405u + 2891336453u;
    random = ((random >> ((random >> 28) + 4)) ^ random) * 277803737u;
    float noise = ((random >> 10) / 4194303.f) * 2 - 1;
    switch (kind) {
    case 0: return (float)sin(phase * 6.283185307179586);
    case 1: return (float)(2 * phase - 1); /* Saw. */
    case 2: return (float)(1 - 4 * fabs(phase - .5));
    case 3: return phase > .21 ? -1 : 1;
    case 4: return noise;
    case 5: return frame % (rate / 7) < 3 ? .9f : 0; /* Transients. */
    default: return (float)(noise * .3 + .7 * sin(phase * 6.283185307179586)) *
                    (float)exp(-6 * fmod((double)frame / rate, .43));
    }
}

static void check_stream(unsigned rate)
{
    TsPrism a = {0}, b = {0};
    TsPrismControls c = settings();
    assert(ts_prism_prepare(&a, rate) && ts_prism_prepare(&b, rate));
    for (unsigned n = 0; n < 3 * rate; ++n) {
        if (n % 1379 == 0) {
            c.enabled = (n / 1379) % 3 != 0;
            c.mode = (n / 1379) % TS_PRISM_MODE_COUNT;
            c.lenses = 2 + (n / 1379) % 11;
            c.spread = (n / 1379) % 10 / 9.f;
            c.drift = 1; c.focus = (n / 1379) % 7 / 6.f;
            ts_prism_set_controls(&a, &c); ts_prism_set_controls(&b, &c);
        }
        TsStereoFrame in = {.l = .7f * source(n, rate, n / (rate / 3)), .r = 0};
        TsStereoFrame x = ts_prism_process(&a, in), y = ts_prism_process(&b, in);
        assert(x.l == y.l && x.r == y.r); /* Fixed seeded trajectories. */
        assert(isfinite(x.l) && fabsf(x.l) <= .70001f && x.r == 0);
    }
    /* Wet zero and disabled both settle to exact original stereo, even with
       nonzero trim. The history must continue to prime during either bypass. */
    c.enabled = 0; c.output_db = 6;
    ts_prism_set_controls(&a, &c);
    for (unsigned n = 0; n < rate; ++n) ts_prism_process(&a, (TsStereoFrame){.17f, -.31f});
    TsStereoFrame x = ts_prism_process(&a, (TsStereoFrame){.1234567f, -.2345678f});
    assert(x.l == .1234567f && x.r == -.2345678f);
    c.enabled = 1; c.mix = 0; ts_prism_set_controls(&a, &c);
    x = ts_prism_process(&a, (TsStereoFrame){.1234567f, -.2345678f});
    assert(x.l == .1234567f && x.r == -.2345678f);
    ts_prism_free(&a); ts_prism_free(&b);
}

static void check_pitch_and_focus(unsigned rate)
{
    TsPrism p = {0}; TsPrismControls c = settings();
    c.stereo = 0;
    assert(ts_prism_prepare(&p, rate)); ts_prism_set_controls(&p, &c);
    double re[12] = {0}, im[12] = {0};
    for (unsigned n = 0; n < 5 * rate; ++n) {
        float in = .4f * (float)sin(6.283185307179586 * 880 * n / rate);
        TsStereoFrame out = ts_prism_process(&p, (TsStereoFrame){in, -in});
        assert(out.l == -out.r); /* Anti-phase stereo is retained, not folded. */
        if (n >= rate) for (int i = 0; i < 12; ++i) {
            double t = (double)(n - rate) / (4 * rate - 1);
            double window = .5 - .5 * cos(6.283185307179586 * t);
            double hz = 880 * exp2(ts_prism_unison_cents[i] / 1200);
            double phase = 6.283185307179586 * hz * (n - rate) / rate;
            re[i] += out.l * window * cos(phase);
            im[i] += out.l * window * sin(phase);
        }
    }
    TsPrismView v = ts_prism_view(&p);
    for (int i = 0; i < 12; ++i) {
        assert(fabsf(v.lens[i].cents - ts_prism_unison_cents[i]) < .06f);
        double amplitude = hypot(re[i], im[i]) / rate;
        printf("%u Hz: lens %d, %.1f cents, amplitude %.5f\n", rate, i + 1, v.lens[i].cents, amplitude);
        assert(amplitude > .0005); /* Verify audible energy, not only ratios. */
    }
    c.focus = 1; c.drift = 1; c.spread = 1;
    ts_prism_set_controls(&p, &c);
    for (unsigned n = 0; n < rate; ++n) ts_prism_process(&p, (TsStereoFrame){0});
    v = ts_prism_view(&p);
    for (int i = 0; i < 12; ++i) {
        assert(fabsf(v.lens[i].cents - (i >= 9 ? -1200 : 0)) < .06f);
        assert(v.lens[i].delay_ms < .001f);
    }
    /* Settled controls do not produce unbounded phase or noise from silence. */
    for (unsigned n = 0; n < rate; ++n) {
        TsStereoFrame x = ts_prism_process(&p, (TsStereoFrame){NAN, INFINITY});
        assert(x.l == 0 && x.r == 0);
    }
    ts_prism_free(&p);
}

static void check_state(void)
{
    TsSisterProjectState state, loaded;
    char error[160]; int present;
    ts_sister_project_state_init(&state, 48000);
    assert(!state.parameters.prism.enabled);
    state.parameters.prism = (TsPrismControls){1,1,8,.21f,.77f,.62f,.3f,.8f,.66f,3};
    state.parameters.fx.slot[0].type = TS_SISTER_FX_GRAIN;
    state.parameters.fx.slot[0].parameter_a = .876f;
    assert(ts_sister_project_state_save_file(&state,"prism-state.ini",error,sizeof(error)));
    assert(ts_sister_project_state_load_file(&loaded,"prism-state.ini",48000,&present,error,sizeof(error)));
    assert(present && !memcmp(&state.parameters.prism,&loaded.parameters.prism,sizeof(TsPrismControls)));
    /* Previous version's explicit slots must not be migrated as legacy FX. */
    FILE *f = fopen("prism-state.ini","r+b"); assert(f);
    char data[16384]; size_t size = fread(data,1,sizeof(data)-1,f); data[size]=0;
    char *version=strstr(data,"Version=13"); assert(version); version[9]='2';
    rewind(f); assert(fwrite(data,1,size,f)==size); fclose(f);
    assert(ts_sister_project_state_load_file(&loaded,"prism-state.ini",48000,&present,error,sizeof(error)));
    assert(loaded.parameters.fx.slot[0].type == TS_SISTER_FX_GRAIN);
    assert(loaded.parameters.fx.slot[0].parameter_a == .876f);
    f=fopen("prism-state.ini","wb"); assert(f);
    fputs("TapeSister Sister Project State\nVersion=12\nPageCount=1\nActivePage=0\nRoutes=0\n",f); fclose(f);
    assert(ts_sister_project_state_load_file(&loaded,"prism-state.ini",48000,&present,error,sizeof(error)));
    assert(!loaded.parameters.prism.enabled && loaded.parameters.prism.lenses == 12);
    remove("prism-state.ini");
    TsSisterPresetBank bank, restored;
    ts_sister_preset_bank_init(&bank,48000);
    assert(ts_sister_preset_save_new(&bank,"PRISM TEST",&state.parameters,48000,error,sizeof(error)));
    assert(ts_sister_preset_save(&bank,"prism-presets.ini",error,sizeof(error)));
    ts_sister_preset_bank_init(&restored,48000);
    assert(ts_sister_preset_load(&restored,"prism-presets.ini",48000,error,sizeof(error)));
    assert(!memcmp(&bank.entries[bank.count-1].parameters.prism,
                   &restored.entries[restored.count-1].parameters.prism,sizeof(TsPrismControls)));
    remove("prism-presets.ini");
}

int main(void)
{
    TsPrismControls c = settings(); c.spread=NAN; c.mix=INFINITY; c.mode=999; c.lenses=999;
    ts_prism_controls_sanitize(&c); assert(isfinite(c.spread) && isfinite(c.mix) && c.mode==0 && c.lenses==12);
    check_stream(44100); check_stream(48000); check_stream(96000);
    check_pitch_and_focus(44100); check_pitch_and_focus(48000); check_state();
    puts("Prism streaming, pitch, stereo, bypass and state checks passed.");
    return 0;
}
