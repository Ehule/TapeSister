#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/sample.h"
#include "tapesister/ui.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const double offsets[9]={0,-7,7,-12,12,-19,19,-26,26};
static double magnitude(const TsSample *s,double hz)
{
    size_t first=s->sample_rate/2, count=s->frames-first-s->sample_rate/2;
    double re=0,im=0;
    for(size_t i=0;i<count;++i) {
        double window=.5-.5*cos(6.283185307179586*(double)i/(count-1));
        double phase=6.283185307179586*hz*i/s->sample_rate;
        re+=s->data[first+i]*window*cos(phase);
        im+=s->data[first+i]*window*sin(phase);
    }
    return 4*hypot(re,im)/count;
}
static TsFmPatch source(void)
{
    TsGeneratorRecipe r={.seed=456,.kind=TS_GENERATOR_FM};TsFmPatch p;
    ts_fm_patch_from_recipe(&r,&p);
    p.ratios[0]=1;p.waveforms[0]=TS_FM_WAVE_SINE;
    p.lfo_rates[0]=.5f;p.lfo_depths[0]=0;p.lfo_types[0]=TS_FM_LFO_OFF;
    p.filter_mode=TS_FILTER_LOWPASS;p.filter_cutoff_hz=6000;
    p.filter_resonance=.19f;p.filter_envelope_amount=0;
    ts_fm_patch_unison(&p);return p;
}
static void check_controls(void)
{
    TsFmPatch p=source(),before=p;
    assert(ts_fm_voice_count(&p)==9 && p.active_mask==511);
    for(int v=0;v<9;++v) {
        assert(fabs(1200*log2(p.ratios[v])-offsets[v])<.001);
        assert(p.waveforms[v]==p.waveforms[0] && p.lfo_rates[v]==p.lfo_rates[0]);
        assert(p.lfo_depths[v]==p.lfo_depths[0] && p.lfo_types[v]==p.lfo_types[0]);
    }
    ts_fm_patch_unison(&p);assert(!memcmp(&p,&before,sizeof(p)));
    p.ratios[0]=2;ts_fm_patch_unison(&p);assert(p.ratios[0]==2);
    assert(fabs(1200*log2(p.ratios[8]/2)-26)<.001);
    before=p;
    assert(ts_fm_step_control(&p,TS_FM_PAGE_PITCH,8,1,1));
    assert(fabs(1200*log2(p.ratios[8]/before.ratios[8])-1)<.001);
    assert(ts_fm_step_control(&p,TS_FM_PAGE_WAVE,8,1,0));
    assert(p.waveforms[8]==TS_FM_WAVE_TRIANGLE && p.waveforms[0]==TS_FM_WAVE_SINE);
    for(int i=0;i<8;++i)assert(p.ratios[i]==before.ratios[i]);
    assert(!ts_fm_set_control_normalized(&p,TS_FM_PAGE_FILTER,8,.5f));
    TsUiState *ui=calloc(1,sizeof(*ui));assert(ui);ts_ui_init(ui);
    ui->fm_patch=p;ui->fm_voice_bank=1;ui->fm_page=TS_FM_PAGE_PITCH;
    assert(ts_ui_fm_control_index(ui,0)==6 && ts_ui_fm_voice_index(ui,2)==8);
    assert(ts_ui_fm_control_index(ui,3)==-1 && ts_ui_fm_voice_index(ui,5)==-1);
    ui->fm_page=TS_FM_PAGE_FILTER;
    assert(ts_ui_fm_control_index(ui,5)==5 && ts_ui_fm_voice_index(ui,0)==6);
    assert(ts_ui_fm_action_from_point(200,48)==TS_UI_FM_ACTION_UNISON);
    assert(ts_ui_fm_action_from_point(320,48)==TS_UI_FM_ACTION_VOICE_BANK);
    ui->fm_patch.structure=0;
    assert(ts_ui_fm_control_index(ui,5)==5 && ts_ui_fm_voice_index(ui,2)==2);
    free(ui);
    for(unsigned seed=1;seed<8;++seed) {
        TsFmPatch v;ts_fm_patch_vary(&p,seed,1,&v);
        assert(v.structure==TS_FM_STRUCTURE_UNISON);
        assert(!memcmp(v.ratios,p.ratios,sizeof(p.ratios)));
    }
}
static void check_audio(unsigned rate)
{
    TsFmPatch p=source();TsSample all={0},again={0},solo={0},other={0};char error[160];
    assert(ts_fm_render_sample(&all,&p,8,880,rate,123,error,sizeof(error)));
    assert(ts_fm_render_sample(&again,&p,8,880,rate,123,error,sizeof(error)));
    assert(ts_sample_hash(&all)==ts_sample_hash(&again));
    for(size_t i=0;i<all.frames;++i)assert(isfinite(all.data[i]) && fabsf(all.data[i])<=.98f);
    for(int v=0;v<9;++v) {
        double target=880*exp2(offsets[v]/1200),best=0,found=0;
        for(int j=-3;j<=3;++j) {
            double hz=target+j*.15,amp=magnitude(&all,hz);
            if(amp>best){best=amp;found=hz;}
        }
        assert(best>.01 && fabs(found-target)<.31);
        /* Every operator can sound alone; other operators never modulate it. */
        p=source();p.active_mask=1u<<v;
        assert(ts_fm_render_sample(&solo,&p,.2f,880,rate,123,error,sizeof(error)));
        p.waveforms[(v+1)%9]=TS_FM_WAVE_NOISE;
        p.interaction=TS_FM_INTERACTION_RING;p.interaction_mix=1;p.depth=12;
        assert(ts_fm_render_sample(&other,&p,.2f,880,rate,123,error,sizeof(error)));
        assert(ts_sample_hash(&solo)==ts_sample_hash(&other));
    }
    ts_sample_free(&all);ts_sample_free(&again);ts_sample_free(&solo);ts_sample_free(&other);
}
static void check_persistence(const char *legacy_path)
{
    TsInstrument *a=calloc(1,sizeof(*a)),*b=calloc(1,sizeof(*b));char error[160];
    assert(a && b);ts_instrument_init(a);ts_instrument_init(b);
    TsFmPatch p=source();p.waveforms[6]=TS_FM_WAVE_TRIANGLE;p.lfo_types[8]=TS_FM_LFO_AMP_SINE;
    p.lfo_rates[8]=.17f;p.lfo_depths[8]=.03f;p.active_mask&=~(1u<<7);
    assert(ts_instrument_apply_fm_patch(a,&p,error,sizeof(error)));
    assert(ts_instrument_save_recipe(a,"test-nine-voice.tsr",error,sizeof(error)));
    assert(ts_instrument_load_recipe(b,"test-nine-voice.tsr",error,sizeof(error)));
    assert(!memcmp(&a->generator.fm_patch,&b->generator.fm_patch,sizeof(p)));
    assert(!memcmp(&a->bank[0].generator.fm_patch,&b->bank[0].generator.fm_patch,sizeof(p)));
    assert(ts_sample_hash(&a->current)==ts_sample_hash(&b->current));
    ts_instrument_free(a);ts_instrument_free(b);remove("test-nine-voice.tsr");
    if(legacy_path) {
        ts_instrument_init(b);
        assert(ts_instrument_load_recipe(b,legacy_path,error,sizeof(error)));
        assert(ts_fm_voice_count(&b->generator.fm_patch)==6);
        for(int i=6;i<9;++i)assert(b->generator.fm_patch.ratios[i]==1 && b->generator.fm_patch.lfo_types[i]==TS_FM_LFO_OFF);
        TsSample old={0};
        assert(ts_sample_generate(&old,&b->generator,error,sizeof(error)));
        assert(ts_sample_hash(&old)==ts_sample_hash(&b->current));
        ts_sample_free(&old);ts_instrument_free(b);
    }
    free(a);free(b);
}
int main(int argc,char **argv)
{
    check_controls();check_audio(44100);check_audio(48000);
    check_persistence(argc>1?argv[1]:NULL);
    puts("Unison: nine pitches and carriers, editing, repeatability and saved patches passed");
    return 0;
}
