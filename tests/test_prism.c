#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/prism.h"
#include "tapesister/sister_ui.h"
#include "tapesister/sister_limiter.h"
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
            c.pitch_offset[1] = (n / 1379) % 2 ? -1200 : 1200;
            c.pan_offset[1] = (n / 1379) % 2 ? -2 : 2;
            c.drift = 1; c.focus = (n / 1379) % 7 / 6.f;
            ts_prism_set_controls(&a, &c); ts_prism_set_controls(&b, &c);
        }
        TsStereoFrame in = {.l = .7f * source(n, rate, n / (rate / 3)), .r = 0};
        TsStereoFrame x = ts_prism_process(&a, in), y = ts_prism_process(&b, in);
        assert(x.l == y.l && x.r == y.r); /* Fixed seeded trajectories. */
        assert(isfinite(x.l) && fabsf(x.l) <= .70001f * sqrtf(TS_PRISM_LENSES) && x.r == 0);
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
    state.parameters.prism = (TsPrismControls){1,1,8,.21f,.77f,.62f,.3f,.8f,.66f,3,{0},{0}};
    for (int i=1; i<12; ++i) {
        state.parameters.prism.pitch_offset[i] = (i - 6) * 190.f;
        state.parameters.prism.pan_offset[i] = (i - 6) * .125f;
    }
    state.parameters.fx.slot[0].type = TS_SISTER_FX_GRAIN;
    state.parameters.fx.slot[0].parameter_a = .876f;
    assert(ts_sister_project_state_save_file(&state,"prism-state.ini",error,sizeof(error)));
    assert(ts_sister_project_state_load_file(&loaded,"prism-state.ini",48000,&present,error,sizeof(error)));
    assert(present && !memcmp(&state.parameters.prism,&loaded.parameters.prism,sizeof(TsPrismControls)));
    /* Previous version's explicit slots must not be migrated as legacy FX. */
    FILE *f = fopen("prism-state.ini","r+b"); assert(f);
    char data[16384]; size_t size = fread(data,1,sizeof(data)-1,f); data[size]=0;
    char *version=strstr(data,"Version=14"); assert(version); version[9]='2';
    rewind(f); assert(fwrite(data,1,size,f)==size); fclose(f);
    assert(ts_sister_project_state_load_file(&loaded,"prism-state.ini",48000,&present,error,sizeof(error)));
    assert(loaded.parameters.fx.slot[0].type == TS_SISTER_FX_GRAIN);
    assert(loaded.parameters.fx.slot[0].parameter_a == .876f);
    f=fopen("prism-state.ini","wb"); assert(f);
    fputs("TapeSister Sister Project State\nVersion=12\nPageCount=1\nActivePage=0\nRoutes=0\n",f); fclose(f);
    assert(ts_sister_project_state_load_file(&loaded,"prism-state.ini",48000,&present,error,sizeof(error)));
    assert(!loaded.parameters.prism.enabled && loaded.parameters.prism.lenses == 12);
    for (int i=0; i<12; ++i) assert(!loaded.parameters.prism.pitch_offset[i] && !loaded.parameters.prism.pan_offset[i]);
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

/* Real FM Unison feeds the same stream processor as external/program audio. */
static void check_gain(void)
{
    TsGeneratorRecipe recipe = {.seed=456, .kind=TS_GENERATOR_FM};
    TsFmPatch patch; ts_fm_patch_from_recipe(&recipe, &patch);
    patch.ratios[0]=1; patch.waveforms[0]=TS_FM_WAVE_SAW;
    patch.lfo_depths[0]=0; patch.lfo_types[0]=TS_FM_LFO_OFF;
    patch.filter_cutoff_hz=6000; patch.filter_envelope_amount=0;
    ts_fm_patch_unison(&patch);
    assert(ts_fm_voice_count(&patch)==12 && patch.active_mask==4095);
    TsSample fm={0}; char error[160];
    assert(ts_fm_render_sample(&fm,&patch,4,220,48000,345,error,sizeof(error)));
    for (int kind=0; kind<3; ++kind) for (int mode=0; mode<2; ++mode)
    for (int count=2; count<=12; count+=2) {
        TsPrism p={0}; TsPrismControls c=settings(); c.mode=mode; c.lenses=count;
        assert(ts_prism_prepare(&p,48000)); ts_prism_set_controls(&p,&c);
        double input_energy=0, output_energy=0;
        for (unsigned n=0; n<fm.frames; ++n) {
            float in=kind==0 ? .3f*sinf(6.28318530718f*110*n/48000) :
                     kind==1 ? .3f*source(n,48000,4) : .3f*fm.data[n];
            TsStereoFrame out=ts_prism_process(&p,(TsStereoFrame){in,in});
            if (n>=48000) {
                input_energy+=in*in;
                output_energy+=(out.l*out.l+out.r*out.r)*.5;
            }
        }
        double ratio=sqrt(output_energy/input_energy);
        printf("gain: source=%s mode=%d lenses=%d RMS=%+.2f dB\n",
            kind==0?"bass":kind==1?"noise":"FM12",mode,count,20*log10(ratio));
        assert(ratio>.65 && ratio<2.5);
        ts_prism_free(&p);
    }
    ts_sample_free(&fm);

    /* Maximum output and correlated lenses may exceed unity internally;
       verify the actual linked output limiter catches them, including edits. */
    TsPrism p={0}; TsPrismControls c=settings(); c.output_db=12; c.focus=1;
    TsSisterLimiter limiter; ts_sister_limiter_init(&limiter);
    assert(ts_prism_prepare(&p,48000) && ts_sister_limiter_reconfigure(&limiter,48000));
    ts_prism_set_controls(&p,&c);
    float peak_before=0, peak_after=0, reduction=0;
    for (unsigned n=0; n<144000; ++n) {
        if (n==48000) {c.focus=0; c.spread=c.drift=1; c.pitch_offset[1]=1200; ts_prism_set_controls(&p,&c);}
        if (n==96000) {c.lenses=2; c.pitch_offset[1]=-1200; ts_prism_set_controls(&p,&c);}
        float in=.8f*sinf(6.28318530718f*110*n/48000);
        TsStereoFrame out=ts_prism_process(&p,(TsStereoFrame){in,-in});
        peak_before=fmaxf(peak_before,fabsf(out.l));
        out=ts_sister_limiter_process(&limiter,out,&reduction,NULL);
        peak_after=fmaxf(peak_after,fmaxf(fabsf(out.l),fabsf(out.r)));
        assert(isfinite(out.l) && isfinite(out.r));
        assert(fabsf(out.l)<=limiter.ceiling_linear+.00001f);
        assert(fabsf(out.r)<=limiter.ceiling_linear+.00001f);
    }
    assert(peak_before>1 && peak_after>.8f);
    printf("Prism +12 dB peak %.3f -> linked limiter %.3f\n",peak_before,peak_after);
    ts_prism_free(&p); ts_sister_limiter_free(&limiter);
}

static void check_manual_geometry(void)
{
    TsPrismControls c=settings(); c.pitch_offset[1]=700; c.pan_offset[1]=1.2f;
    TsPrism p={0}; assert(ts_prism_prepare(&p,48000)); ts_prism_set_controls(&p,&c);
    for (int n=0; n<48000; ++n) ts_prism_process(&p,(TsStereoFrame){0});
    TsPrismView v=ts_prism_view(&p);
    assert(fabsf(v.lens[1].cents-693)<.06f);
    assert(fabsf(v.lens[2].cents-7)<.06f);
    assert(v.lens[1].pan>.8f);
    /* A hand edit must shift audible energy, not just the optical snapshot. */
    c.lenses=2; ts_prism_set_controls(&p,&c);
    double re=0, im=0, hz=880*exp2(693./1200);
    for (int n=0; n<144000; ++n) {
        float in=.4f*sinf((float)(6.283185307179586*880*n/48000));
        TsStereoFrame out=ts_prism_process(&p,(TsStereoFrame){in,in});
        if (n>=48000) {
            double phase=6.283185307179586*hz*n/48000;
            re+=out.r*cos(phase); im+=out.r*sin(phase);
        }
    }
    assert(2*hypot(re,im)/96000>.03);
    c.lenses=2; ts_prism_set_controls(&p,&c);
    c.lenses=12; ts_prism_set_controls(&p,&c);
    assert(p.controls.pitch_offset[1]==700); /* Stable identity on count changes. */
    c.focus=1; ts_prism_set_controls(&p,&c);
    for (int n=0; n<48000; ++n) ts_prism_process(&p,(TsStereoFrame){0});
    assert(fabsf(ts_prism_view(&p).lens[1].cents)<.06f);
    c.focus=0; c.pitch_offset[1]=0; c.spread=c.drift=1;
    v=ts_prism_control_view(&c); assert(v.lens[8].cents>180);
    c.pitch_offset[0]=999; c.pan_offset[0]=1; c.pitch_offset[2]=INFINITY;
    ts_prism_controls_sanitize(&c);
    assert(!c.pitch_offset[0] && !c.pan_offset[0] && !c.pitch_offset[2]);
    ts_prism_free(&p);
}

int main(void)
{
    TsPrismControls c = settings(); c.spread=NAN; c.mix=INFINITY; c.mode=999; c.lenses=999;
    ts_prism_controls_sanitize(&c); assert(isfinite(c.spread) && isfinite(c.mix) && c.mode==0 && c.lenses==12);
    check_stream(44100); check_stream(48000); check_stream(96000);
    check_pitch_and_focus(44100); check_pitch_and_focus(48000); check_state();
    check_gain(); check_manual_geometry();
    puts("Prism streaming, pitch, stereo, bypass and state checks passed.");
    return 0;
}
