#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/autopan.h"
#include "tapesister/matrix_effects.h"
#include "tapesister/sister_project_state.h"
#include "tapesister/sister_preset.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void waves(void)
{
    const unsigned rates[]={8000,44100,48000,96000,192000};
    TsStereoFrame in[257],out[257],scalar[257];
    for(unsigned r=0;r<sizeof(rates)/sizeof(*rates);++r)for(int mode=0;mode<2;++mode)for(int wave=0;wave<6;++wave) {
        TsAutoPan a,b;ts_autopan_init(&a,rates[r],1234);ts_autopan_init(&b,rates[r],1234);
        TsAutoPanControls c;ts_autopan_default(&c);c.mode=mode;c.wave=wave;c.rate_hz=100;
        ts_autopan_set(&a,&c);ts_autopan_set(&b,&c);
        float previous_l=1,previous_r=1;double movement=0;
        for(int block=0;block<20;++block) {
            for(unsigned j=0;j<257;++j)in[j]=(TsStereoFrame){1,1};
            ts_autopan_process_span(&a,in,out,257);
            memcpy(scalar,in,sizeof(in));for(unsigned j=0;j<257;++j)ts_autopan_process_span(&b,&scalar[j],&scalar[j],1);
            assert(!memcmp(out,scalar,sizeof(out)));
            for(unsigned j=0;j<257;++j) {
                assert(isfinite(out[j].l) && isfinite(out[j].r));
                assert(out[j].l>=0 && out[j].l<=1.415f && out[j].r>=0 && out[j].r<=1.415f);
                /* A discontinuous waveform cannot jump directly between gains. */
                assert(fabsf(out[j].l-previous_l)<=1.415f*a.slew+.00001f);
                assert(fabsf(out[j].r-previous_r)<=1.415f*a.slew+.00001f);
                movement+=fabsf(out[j].l-previous_l)+fabsf(out[j].r-previous_r);
                previous_l=out[j].l;previous_r=out[j].r;
            }
        }
        assert(movement>1);
        /* Stereo remains stereo: left input cannot appear in the right channel. */
        TsStereoFrame mono={.3f,0},test;ts_autopan_process_span(&a,&mono,&test,1);assert(test.r==0);
    }
    TsAutoPan a;ts_autopan_init(&a,48000,4);a.controls.depth=0;
    for(int i=0;i<257;++i)in[i]=(TsStereoFrame){.25f,-.3f};
    ts_autopan_process_span(&a,in,out,257);assert(!memcmp(in,out,sizeof(out)));
    TsAutoPanSettings s;ts_autopan_settings_default(&s);ts_autopan_configure(&a,&s,1,1,.5f,0,0);
    TsStereoFrame dry=ts_autopan_process(&a,in[0],1,NULL);assert(dry.l==in[0].l && dry.r==in[0].r);
    s.value[0]=NAN;ts_autopan_configure(&a,&s,NAN,INFINITY,-5,0,0);
    assert(isfinite(a.step));
}
static void clock_and_reset(void)
{
    TsAutoPan p;TsAutoPanSettings s;ts_autopan_init(&p,48000,42);ts_autopan_settings_default(&s);
    s.value[3]=1;ts_autopan_configure(&p,&s,.5f,1,.5f,1,0);
    ts_autopan_transport(&p,.5,1);assert(fabs(p.step-2./48000)<1e-12);
    ts_autopan_transport(&p,.25,1);assert(fabs(p.step-4./48000)<1e-12);
    p.phase=.6;ts_autopan_transport(&p,.25,2);assert(p.phase==0);
    float random_a=p.random_a,random_b=p.random_b;
    TsStereoFrame in={1,1},out;for(int i=0;i<24000;++i)ts_autopan_process_span(&p,&in,&out,1);
    assert(p.random_a!=random_a);ts_autopan_transport(&p,.25,3);
    assert(p.phase==0 && p.random_a==random_a && p.random_b==random_b);
    s.value[5]=0;ts_autopan_configure(&p,&s,.5f,1,.5f,1,0);p.phase=.6;ts_autopan_transport(&p,.25,4);assert(p.phase==.6);
    s.value[3]=0;ts_autopan_configure(&p,&s,0,1,.5f,1,0);assert(fabs(p.step-.01/48000)<1e-12);
    ts_autopan_configure(&p,&s,1,1,.5f,1,0);assert(fabs(p.step-100./48000)<1e-12);
    TsMasterClock clock={0};ts_master_clock_transport(&clock,1);unsigned first=clock.reset_serial;
    ts_master_clock_transport(&clock,1);assert(clock.reset_serial==first);
    ts_master_clock_transport(&clock,0);ts_master_clock_transport(&clock,1);assert(clock.reset_serial==first+1);
    ts_master_clock_transport(&clock,2);assert(clock.reset_serial==first+2);
}
static TsSisterFxSlotControls slot(void)
{
    TsSisterFxSlotControls c=ts_sister_fx_empty_slot();c.id=1;c.type=TS_SISTER_FX_AUTOPAN;c.enabled=1;
    c.parameter_a=ts_autopan_default_rate();c.parameter_b=1;c.parameter_c=.5f;c.mix=1;
    ts_autopan_settings_default(&c.autopan);return c;
}
static void hosts(void)
{
    TsSisterFxSlotControls control=slot();TsSisterSingleFx *native=ts_sister_single_fx_new(control.type,48000,42);assert(native);ts_sister_single_fx_set(native,&control);
    TsPedalboardControls board;ts_pedalboard_default(&board);board.slot[0]=control;
    TsPedalboard *b=ts_pedalboard_new(&board,48000,42);assert(b);
    TsMetaControls meta;ts_meta_default(&meta);meta.count=1;ts_meta_stage_default(&meta.stage[0],TS_SISTER_FX_AUTOPAN);
    TsMetaEngine *m=ts_meta_new(&meta,48000,42);assert(m);
    static TsMatrixControls matrix;ts_matrix_default(&matrix);int index=ts_matrix_effect_add(&matrix,TS_MATRIX_FX_AUTOPAN);assert(index>=0);
    TsMatrixEffects fx;ts_matrix_effects_init(&fx);assert(ts_matrix_effects_restore(&fx,&matrix,48000));
    static TsSisterPostFxEngine original;assert(ts_sister_post_fx_init(&original,48000));
    TsSisterFxControls original_controls;ts_sister_fx_controls_default(&original_controls);
    for(int i=0;i<TS_SISTER_FX_SLOT_COUNT;++i)original_controls.slot[i]=ts_sister_fx_empty_slot();
    original_controls.enabled=1;original_controls.slot[0]=control;assert(ts_sister_post_fx_sync_controls(&original,&original_controls));
    size_t memory=ts_matrix_effects_memory(&fx);double energy=0;
    for(int i=0;i<48000;++i) {
        if(i==24000){ts_sister_single_fx_transport(native,.25,1);ts_pedalboard_transport(b,.25,1);ts_meta_transport(m,.25,1);fx.mod_quarter=.25;fx.transport_serial=1;original.timing.quarter=.25;original.timing.transport_serial=1;}
        TsStereoFrame in={.2f,-.1f},a=ts_sister_single_fx_process(native,in,1),bb=ts_pedalboard_process(b,in),mm=ts_meta_process(m,in);
        ts_matrix_effects_tick(&fx);TsStereoFrame xx=ts_matrix_effects_process(&fx,index,in,(TsStereoFrame){0},1);
        assert(fabsf(a.l-bb.l)<1e-6f && fabsf(a.r-bb.r)<1e-6f);
        assert(fabsf(a.l-mm.l)<1e-6f && fabsf(a.r-mm.r)<1e-6f);
        assert(fabsf(a.l-xx.l)<1e-6f && fabsf(a.r-xx.r)<1e-6f);
        TsStereoFrame p1=ts_sister_post_fx_process_matrix(&original,in,1);
        assert(fabsf(a.l-p1.l)<1e-6f && fabsf(a.r-p1.r)<1e-6f);
        energy+=a.l*a.l+a.r*a.r;
    }
    assert(energy>100 && ts_matrix_effects_memory(&fx)==memory);
    TsStereoFrame bypass=ts_sister_single_fx_process(native,(TsStereoFrame){.2f,-.1f},0);assert(bypass.l==.2f && bypass.r==-.1f);
    ts_sister_single_fx_free(native);ts_pedalboard_free(b);ts_meta_free(m);ts_matrix_effects_free(&fx);ts_sister_post_fx_free(&original);
}
static void persistence(void)
{
    char error[256];static TsSisterProjectState state,loaded;ts_sister_project_state_init(&state,48000);
    TsSisterFxSlotControls c=slot();assert(ts_autopan_read(&c.autopan,"1,1,0.25,1,0.75,0"));
    TsAutoPanSettings before=c.autopan;assert(!ts_autopan_read(&c.autopan,"nan,1,0,0,0,0") && !memcmp(&before,&c.autopan,sizeof(before)));
    state.parameters.fx.slot[0]=c;
    int pan=ts_matrix_effect_add(&state.matrix,TS_MATRIX_FX_AUTOPAN),board=ts_matrix_effect_add(&state.matrix,TS_MATRIX_FX_PEDALBOARD),meta=ts_matrix_effect_add(&state.matrix,TS_MATRIX_FX_META);
    assert(pan>=0 && board>=0 && meta>=0);
    state.matrix.effect[pan].autopan=c.autopan;state.matrix.effect[board].board.slot[0]=c;
    TsMetaControls *rack=&state.matrix.effect[meta].meta;ts_meta_default(rack);rack->count=1;ts_meta_stage_default(&rack->stage[0],TS_SISTER_FX_AUTOPAN);rack->stage[0].autopan=c.autopan;
    assert(ts_sister_project_state_save_file(&state,"autopan-project.tsr",error,sizeof(error)));int present=0;
    assert(ts_sister_project_state_load_file(&loaded,"autopan-project.tsr",48000,&present,error,sizeof(error)) && present);
    assert(!memcmp(&loaded.parameters.fx.slot[0].autopan,&c.autopan,sizeof(c.autopan)));
    assert(!memcmp(&loaded.matrix.effect[pan].autopan,&c.autopan,sizeof(c.autopan)));
    assert(!memcmp(&loaded.matrix.effect[board].board.slot[0].autopan,&c.autopan,sizeof(c.autopan)));
    assert(!memcmp(&loaded.matrix.effect[meta].meta.stage[0].autopan,&c.autopan,sizeof(c.autopan)));remove("autopan-project.tsr");
    assert(ts_meta_save(rack,"autopan.tsmfx",error,sizeof(error)));TsMetaControls recalled;
    assert(ts_meta_load(&recalled,"autopan.tsmfx",error,sizeof(error)) && !memcmp(rack,&recalled,sizeof(recalled)));remove("autopan.tsmfx");
    static TsSisterPresetBank presets,read;ts_sister_preset_bank_init(&presets,48000);
    assert(ts_sister_preset_save_new(&presets,"AutoPan recall",&state.parameters,48000,error,sizeof(error)));
    assert(ts_sister_preset_save(&presets,"autopan-presets.ini",error,sizeof(error)));
    assert(ts_sister_preset_load(&read,"autopan-presets.ini",48000,error,sizeof(error)));
    TsSisterParameters recalled_parameters;assert(ts_sister_preset_recall(&read,read.count-1,&recalled_parameters));
    assert(!memcmp(&recalled_parameters.fx.slot[0].autopan,&c.autopan,sizeof(c.autopan)));remove("autopan-presets.ini");
}
int main(void)
{waves();clock_and_reset();hosts();persistence();puts("AutoPan waveform, slew, clock, host parity and persistence passed.");return 0;}
