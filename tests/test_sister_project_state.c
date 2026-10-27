#ifdef NDEBUG
#undef NDEBUG
#endif
#include "sister_test_helpers.h"
#include "tapesister/sister_project_state.h"
#include "tapesister/sister_ui.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define MAKE_DIR(path) _mkdir(path)
#define REMOVE_DIR(path) _rmdir(path)
#else
#include <sys/stat.h>
#include <unistd.h>
#define MAKE_DIR(path) mkdir(path, 0700)
#define REMOVE_DIR(path) rmdir(path)
#endif

static void layout_malformed(void)
{
    TsMatrixLayout *l=malloc(sizeof(*l));assert(l);ts_matrix_layout_default(l);
    assert(ts_matrix_layout_read(l,"MatrixLayout.Row1","0,1")==-1);
    assert(ts_matrix_layout_read(l,"MatrixLayout.Row0","1,18446744073709551616")==-1);
    assert(ts_matrix_layout_read(l,"MatrixLayout.Row0","1,-1")==-1);
    assert(ts_matrix_layout_read(l,"MatrixLayout.Row0","0,1 junk")==-1);
    assert(ts_matrix_layout_read(l,"MatrixLayout.Row0","0,1")==1);
    assert(ts_matrix_layout_read(l,"MatrixLayout.Row1","0,1")==1);
    assert(!ts_matrix_layout_valid(l));
    ts_matrix_layout_default(l);l->column[0]=1;assert(!ts_matrix_layout_valid(l));
    ts_matrix_layout_default(l);l->row_count=1;l->row_kind[0]=2;l->row_id[0]=TS_MATRIX_ROWS;
    assert(!ts_matrix_layout_valid(l));free(l);
}

static float restored_mosaic_energy(TsSisterRuntime *r)
{
    float energy=0;
    TsSisterSourceFrames input={.matrix_program_gain=1};
    for(int i=0;i<1000;++i) {
        float value=.1f*sinf(i*.31f);input.mosaic=(TsStereoFrame){value,-value};
        TsSisterRuntimeFrame frame=ts_sister_runtime_process_frame(r,&input);
        /* Let the downstream limiter's old lookahead drain after recall. */
        if(i>=100)energy+=fabsf(frame.monitor_return.l)+fabsf(frame.monitor_return.r);
    }
    return energy;
}

static void project_restores_pedalboard_without_live_fade(void)
{
    TsSisterRuntime *r=calloc(1,sizeof(*r));
    TsSisterProjectState *s=calloc(1,sizeof(*s));assert(r && s);
    char error[160];ts_sister_runtime_init(r);
    assert(ts_sister_runtime_reconfigure(r,1000,2,error,sizeof(error)));
    ts_sister_project_state_init(s,1000);
    s->matrix.enabled=1;
    s->matrix.row[TS_MATRIX_MOSAIC].matrix_gain[TS_MATRIX_MAIN]=0;
    s->matrix.row[TS_MATRIX_MOSAIC].matrix_gain[TS_MATRIX_BOARD]=1000;
    s->parameters.fx.enabled=0;
    s->parameters.fx.master_transition=ts_sister_fx_transition_normalized(240000);
    s->parameters.fx.transition=ts_sister_fx_transition_normalized(180000);
    for(int i=0;i<TS_SISTER_FX_SLOT_COUNT;++i)s->parameters.fx.slot[i].type=TS_SISTER_FX_EMPTY;
    s->parameters.fx.slot[0]=(TsSisterFxSlotControls){
        .type=TS_SISTER_FX_DISTORTION,.enabled=1,.placement=TS_SISTER_FX_PLACE_POST,
        .parameter_a=.5f,.parameter_b=.5f,.mix=1};
    /* Opening the saved OFF/SEND project must not briefly play from the old
       engine state and fade away minutes later. Restoring ON is immediate too. */
    assert(ts_sister_project_state_apply(s,r,NULL));
    assert(restored_mosaic_energy(r)==0);
    assert(!ts_sister_post_fx_master_transition_status(&r->post_fx).active);
    s->parameters.fx.enabled=1;assert(ts_sister_project_state_apply(s,r,NULL));
    assert(restored_mosaic_energy(r)>1);
    assert(!ts_sister_post_fx_transition_status(&r->post_fx).active);
    /* Deliberate live changes still use the configured four-minute clock. */
    TsSisterParameters p=r->parameters;p.fx.enabled=0;
    ts_sister_runtime_set_parameters(r,&p);
    assert(ts_sister_post_fx_master_transition_status(&r->post_fx).active);
    assert(restored_mosaic_energy(r)>1);
    s->parameters.fx.enabled=0;assert(ts_sister_project_state_apply(s,r,NULL));
    assert(restored_mosaic_energy(r)==0);
    assert(!ts_sister_post_fx_master_transition_status(&r->post_fx).active);
    /* INSERT preserves dry sound with master OFF; loading retains this mode. */
    s->matrix.board_insert=1;assert(ts_sister_project_state_apply(s,r,NULL));
    assert(restored_mosaic_energy(r)>100 && r->matrix.controls.board_insert);
    ts_sister_runtime_free(r);free(r);free(s);
}
#include "test_project_view.inc"
int main(void)
{
    project_view_storage();
    layout_malformed();
    project_restores_pedalboard_without_live_fade();
    static const char project[] = "test-sister-project.tsr";
    static const char directory[] = "test-sister-project.tsr.samples";
    static const char state_path[] = "test-sister-project.tsr.samples/sister-state.ini";
    TsSisterRuntime runtime, restored;
    TsInstrument instrument;
    TsSisterProjectState state, loaded;
    char error[160];
    int present;
    (void)MAKE_DIR(directory);
    assert(sister_test_make_tiles(&instrument, 2, 0, 1000u, 32u));
    assert(sister_test_enable(&runtime, 1000u, 2u, 0.1));
    ts_sister_runtime_set_sources(&runtime,
        TS_SISTER_SOURCE_TILES | TS_SISTER_SOURCE_FM | TS_SISTER_SOURCE_EXT);
    assert(ts_sister_runtime_set_source_slot(&runtime, &instrument, 0, 1));
    assert(ts_sister_runtime_set_page(&runtime, 1u, &instrument));
    assert(ts_sister_runtime_set_source_slot(&runtime, &instrument, 1, 1));
    runtime.parameters.ghost_tone = 0.61f;
    runtime.parameters.tiles_gain = 1.23f;
    runtime.parameters.fm_gain = 2.34f;
    runtime.parameters.external_gain = 3.45f;
    runtime.parameters.preview_gain = 0.67f;
    runtime.parameters.tapehead_gain = 1.89f;
    runtime.parameters.fx_return_gain = 1.54f;
    runtime.parameters.soak = 0.68f;
    runtime.parameters.bleed = 0.74f;
    runtime.parameters.soak_targets = TS_SISTER_EFFECT_TARGET_H1 |
                                      TS_SISTER_EFFECT_TARGET_H2;
    runtime.parameters.fx.reverb_type = TS_SISTER_REVERB_SPRING;
    runtime.parameters.fx.reverb_size = 0.29f;
    runtime.parameters.fx.reverb_mix = 0.57f;
    runtime.parameters.fx.reverb_gain_db = 7.0f;
    runtime.parameters.fx.reverb_targets = TS_SISTER_EFFECT_TARGET_H3;
    runtime.parameters.fx.delay_mix = 0.43f;
    runtime.parameters.fx.delay_gain_db = -5.0f;
    runtime.parameters.fx.delay_targets = TS_SISTER_EFFECT_TARGET_H1 |
                                           TS_SISTER_EFFECT_TARGET_H2;
    runtime.parameters.fx.distortion_mix = 0.71f;
    runtime.parameters.fx.distortion_gain_db = 3.0f;
    runtime.parameters.fx.distortion_targets = TS_SISTER_EFFECT_TARGET_MIX;
    runtime.parameters.fx.grain_size = 0.64f;
    runtime.parameters.fx.grain_density = 0.78f;
    runtime.parameters.fx.grain_pitch = 0.31f;
    runtime.parameters.fx.grain_mix = 0.56f;
    runtime.parameters.fx.grain_gain_db = 5.0f;
    runtime.parameters.fx.grain_targets = TS_SISTER_EFFECT_TARGET_H2 |
                                          TS_SISTER_EFFECT_TARGET_H3;
    runtime.parameters.fx.master_feedback = 0.66f;
    runtime.parameters.fx.slot[0] = (TsSisterFxSlotControls){
        TS_SISTER_FX_REVERB, 1, TS_SISTER_FX_PLACE_PRE,
        2.0f, 0.83f, 0.74f, 0.5f, 0.68f
    };
    runtime.parameters.fx.slot[3] = (TsSisterFxSlotControls){
        TS_SISTER_FX_GRAIN, 1,
        TS_SISTER_FX_PLACE_H1 | TS_SISTER_FX_PLACE_H2,
        -6.0f, 0.22f, 0.91f, 0.36f, 1.0f
    };
    runtime.parameters.fx.enabled = 0;
    runtime.parameters.fx.reverb_enabled = 0;
    runtime.parameters.fx.delay_enabled = 1;
    runtime.parameters.fx.distortion_enabled = 0;
    runtime.parameters.fx.grain_enabled = 1;
    runtime.parameters.fx.transition = 0.73f;
    runtime.parameters.fx.master_transition = 0.62f;
    runtime.parameters.fx.fallout.enabled = 1;
    runtime.parameters.fx.fallout.feedback = 0.64f;
    runtime.parameters.fx.fallout.noise_type = TS_SISTER_FALLOUT_NOISE_PINK;
    runtime.parameters.fx.fallout.transition = 0.71f;
    runtime.parameters.fx.fallout.component_transition = 0.67f;
    runtime.parameters.fx.fallout.master_transition = 0.56f;
    runtime.parameters.fx.fallout.lfo_rate = 0.18f;
    runtime.parameters.fx.fallout.lfo_intensity = 0.42f;
    runtime.parameters.fx.fallout.lfo_targets =
        TS_SISTER_FALLOUT_LFO_NOISE | TS_SISTER_FALLOUT_LFO_PITCH_RATE;
    runtime.parameters.fx.fallout.rise_mode = TS_SISTER_FALLOUT_RISE_SAW;
    runtime.parameters.fx.fallout.rise_length = 0.88f;
    runtime.parameters.fx.fallout.rise_intensity = 0.61f;
    runtime.parameters.fx.fallout.rise_targets =
        TS_SISTER_FALLOUT_LFO_NOISE | TS_SISTER_FALLOUT_LFO_FEEDBACK;
    runtime.parameters.fx.fallout.pitch_enabled = 1;
    runtime.parameters.fx.fallout.pitch_ramp = 0.37f;
    runtime.parameters.buffer_seconds = 23.0f;
    runtime.parameter_locks =
        TS_SISTER_UI_PARAMETER_BIT(TS_SISTER_UI_PARAM_FILTER_TYPE) |
        TS_SISTER_UI_PARAMETER_BIT(TS_SISTER_UI_PARAM_EXT_GAIN);
    runtime.parameter_locks_high = TS_SISTER_UI_PARAMETER_BIT(
        TS_SISTER_UI_PARAM_DELAY_GAIN - 64);
    ts_sister_runtime_set_parameters(&runtime, &runtime.parameters);
    ts_sister_runtime_set_selected_preset(&runtime, "GHOST FIELD");
    ts_sister_runtime_mark_selected_preset_modified(&runtime);
    runtime.machine.buffer.data[0] = 0.75f;
    runtime.matrix_layout.row_count=4;
    runtime.matrix_layout.row_kind[0]=2;runtime.matrix_layout.row_id[0]=TS_MATRIX_TRACKER;
    runtime.matrix_layout.row_kind[1]=0;runtime.matrix_layout.row_id[1]=1;
    runtime.matrix_layout.row_kind[2]=1;runtime.matrix_layout.row_id[2]=UINT64_MAX;
    runtime.matrix_layout.row_kind[3]=3;runtime.matrix_layout.row_id[3]=UINT64_MAX;
    runtime.matrix_layout.column[0]=TS_MATRIX_SIDECHAIN;
    runtime.matrix_layout.column[TS_MATRIX_SIDECHAIN]=0;
    assert(ts_matrix_layout_valid(&runtime.matrix_layout));
    runtime.performance_transport = 6u;
    runtime.performance_mixer=(TsPerformanceMixer){{0,.5f,2,.25f},1,3,1};
    for(int i=0;i<TS_PERFORMANCE_INPUTS;++i)runtime.performance_mixer.input_gain[i]=1;
    runtime.performance_mixer.input_enabled[0]=runtime.performance_mixer.input_enabled[254]=1;
    runtime.performance_mixer.input_gain[0]=0;runtime.performance_mixer.input_gain[254]=1.75f;
    ts_sister_project_state_capture(&state, &runtime, 2u, "GHOST FIELD");
    assert(ts_sister_project_state_save(&state, project, error, sizeof(error)));
    assert(ts_sister_project_state_load(&loaded, project, 1000u, &present,
                                        error, sizeof(error)) && present);
    assert(!memcmp(&loaded.matrix_layout,&runtime.matrix_layout,sizeof(TsMatrixLayout)));
    assert(loaded.performance_transport == 6u);
    assert(loaded.performance_mixer.input_enabled[0] && loaded.performance_mixer.input_gain[0]==0);
    assert(loaded.performance_mixer.input_enabled[254] && loaded.performance_mixer.input_gain[254]==1.75f);
    assert(!loaded.performance_mixer.input_enabled[100] && loaded.performance_mixer.input_gain[100]==1);
    assert(loaded.performance_mixer.gain[0]==0 && loaded.performance_mixer.gain[1]==.5f && loaded.performance_mixer.gain[2]==2 && loaded.performance_mixer.gain[3]==.25f);
    assert(loaded.performance_mixer.restart && loaded.performance_mixer.arp_sync==3 && loaded.performance_mixer.always_on_top);
    assert(loaded.page_masks[0] == 1u && loaded.page_masks[1] == 2u);
    assert(loaded.parameters.ghost_tone > 0.60f);
    assert(loaded.parameters.tiles_gain > 1.22f && loaded.parameters.tiles_gain < 1.24f);
    assert(loaded.parameters.fm_gain > 2.33f && loaded.parameters.fm_gain < 2.35f);
    assert(loaded.parameters.external_gain > 3.44f && loaded.parameters.external_gain < 3.46f);
    assert(loaded.parameters.preview_gain > 0.66f && loaded.parameters.preview_gain < 0.68f);
    assert(loaded.parameters.tapehead_gain > 1.88f && loaded.parameters.tapehead_gain < 1.90f);
    assert(loaded.parameters.fx_return_gain > 1.53f && loaded.parameters.fx_return_gain < 1.55f);
    assert(loaded.parameters.soak > 0.67f && loaded.parameters.soak < 0.69f);
    assert(loaded.parameters.bleed > 0.73f && loaded.parameters.bleed < 0.75f);
    assert(loaded.parameters.soak_targets ==
           (TS_SISTER_EFFECT_TARGET_H1 | TS_SISTER_EFFECT_TARGET_H2));
    assert(loaded.parameters.fx.reverb_type == TS_SISTER_REVERB_SPRING);
    assert(loaded.parameters.fx.reverb_size > 0.28f &&
           loaded.parameters.fx.reverb_size < 0.30f);
    assert(loaded.parameters.fx.reverb_targets == TS_SISTER_EFFECT_TARGET_H3);
    assert(loaded.parameters.fx.reverb_gain_db == 7.0f);
    assert(loaded.parameters.fx.delay_targets ==
           (TS_SISTER_EFFECT_TARGET_H1 | TS_SISTER_EFFECT_TARGET_H2));
    assert(loaded.parameters.fx.delay_gain_db == -5.0f);
    assert(loaded.parameters.fx.distortion_targets ==
           TS_SISTER_EFFECT_TARGET_MIX);
    assert(loaded.parameters.fx.distortion_gain_db == 3.0f);
    assert(loaded.parameters.fx.grain_enabled == 1);
    assert(loaded.parameters.fx.grain_size > 0.63f &&
           loaded.parameters.fx.grain_size < 0.65f);
    assert(loaded.parameters.fx.grain_density > 0.77f &&
           loaded.parameters.fx.grain_density < 0.79f);
    assert(loaded.parameters.fx.grain_pitch > 0.30f &&
           loaded.parameters.fx.grain_pitch < 0.32f);
    assert(loaded.parameters.fx.grain_mix > 0.55f &&
           loaded.parameters.fx.grain_mix < 0.57f);
    assert(loaded.parameters.fx.grain_gain_db == 5.0f);
    assert(loaded.parameters.fx.grain_targets ==
           (TS_SISTER_EFFECT_TARGET_H2 | TS_SISTER_EFFECT_TARGET_H3));
    assert(loaded.parameters.fx.master_feedback > 0.65f);
    assert(loaded.parameters.fx.slot[0].type == TS_SISTER_FX_REVERB);
    assert(loaded.parameters.fx.slot[0].placement == TS_SISTER_FX_PLACE_PRE);
    assert(loaded.parameters.fx.slot[0].parameter_a > 0.82f &&
           loaded.parameters.fx.slot[0].parameter_a < 0.84f);
    assert(loaded.parameters.fx.slot[3].type == TS_SISTER_FX_GRAIN);
    assert(loaded.parameters.fx.slot[3].placement ==
           (TS_SISTER_FX_PLACE_H1 | TS_SISTER_FX_PLACE_H2));
    assert(loaded.parameters.fx.slot[3].mix == 1.0f);
    assert(loaded.parameters.fx.enabled == 0);
    assert(loaded.parameters.fx.reverb_enabled == 0);
    assert(loaded.parameters.fx.delay_enabled == 1);
    assert(loaded.parameters.fx.distortion_enabled == 0);
    assert(loaded.parameters.fx.transition > 0.72f);
    assert(loaded.parameters.fx.master_transition > 0.61f &&
           loaded.parameters.fx.master_transition < 0.63f);
    assert(loaded.parameters.fx.fallout.enabled == 1);
    assert(loaded.parameters.fx.fallout.feedback > 0.63f);
    assert(loaded.parameters.fx.fallout.noise_type ==
           TS_SISTER_FALLOUT_NOISE_PINK);
    assert(loaded.parameters.fx.fallout.transition > 0.70f);
    assert(loaded.parameters.fx.fallout.component_transition > 0.66f);
    assert(loaded.parameters.fx.fallout.master_transition > 0.55f &&
           loaded.parameters.fx.fallout.master_transition < 0.57f);
    assert(loaded.parameters.fx.fallout.lfo_rate > 0.17f);
    assert(loaded.parameters.fx.fallout.lfo_intensity > 0.41f);
    assert(loaded.parameters.fx.fallout.lfo_targets ==
           (TS_SISTER_FALLOUT_LFO_NOISE |
            TS_SISTER_FALLOUT_LFO_PITCH_RATE));
    assert(loaded.parameters.fx.fallout.rise_mode ==
           TS_SISTER_FALLOUT_RISE_SAW);
    assert(loaded.parameters.fx.fallout.rise_length > 0.87f);
    assert(loaded.parameters.fx.fallout.rise_intensity > 0.60f);
    assert(loaded.parameters.fx.fallout.rise_targets ==
           (TS_SISTER_FALLOUT_LFO_NOISE |
            TS_SISTER_FALLOUT_LFO_FEEDBACK));
    assert(loaded.parameters.fx.fallout.pitch_enabled == 1);
    assert(loaded.parameters.fx.fallout.pitch_ramp > 0.36f);
    assert(loaded.parameters.buffer_seconds == 23.0f);
    assert(loaded.parameter_locks == runtime.parameter_locks);
    assert(loaded.parameter_locks_high == runtime.parameter_locks_high);
    assert(strcmp(loaded.selected_preset, "GHOST FIELD") == 0);
    assert(loaded.selected_preset_modified == 1);
    ts_sister_runtime_init(&restored);
    assert(ts_sister_project_state_apply(&loaded, &restored, &instrument));
    assert(restored.performance_transport == 6u);
    assert(!memcmp(restored.performance_mixer.input_gain,loaded.performance_mixer.input_gain,sizeof(loaded.performance_mixer.input_gain)));
    assert(!memcmp(restored.performance_mixer.input_enabled,loaded.performance_mixer.input_enabled,sizeof(loaded.performance_mixer.input_enabled)));
    assert(restored.performance_mixer.gain[0]==0 && restored.performance_mixer.gain[1]==.5f && restored.performance_mixer.gain[2]==2 && restored.performance_mixer.gain[3]==.25f);
    assert(restored.performance_mixer.restart && restored.performance_mixer.arp_sync==3 && restored.performance_mixer.always_on_top);
    assert(restored.source_switches == loaded.source_switches);
    assert(restored.parameter_locks == loaded.parameter_locks);
    assert(restored.parameter_locks_high == loaded.parameter_locks_high);
    assert(restored.selected_preset_modified == 1);
    assert(restored.active_page == 1u && restored.page_source_masks[0] == 1u);
    assert(restored.machine.buffer.data == NULL); /* live tape was not serialized */
    ts_sister_runtime_free(&restored);
    ts_sister_runtime_free(&runtime);
    ts_instrument_free(&instrument);
    remove(state_path);

    {
        FILE *file = fopen(state_path, "wb");
        assert(file != NULL);
        fputs("TapeSister Sister Project State\nVersion=9\nPageCount=1\n"
              "ActivePage=0\nRoutes=2\nSelectedPreset=FUTURE\n"
              "ParameterLocks=0000000100000000\nParameterLocksHigh=0\n"
              "Mask.0=0000\nGhostTone=0.25\nSoakTargets=255\nReverbType=2\n"
              "FutureField=17\n", file);
        fclose(file);
        assert(ts_sister_project_state_load(&loaded, project, 48000u,
                                            &present, error, sizeof(error)));
        assert(present && loaded.source_switches == TS_SISTER_SOURCE_FM &&
               loaded.parameters.ghost_tone == 0.25f);
        assert(loaded.selected_preset_modified == 0);
        assert(loaded.performance_transport == 1u);
        for(int i=0;i<4;++i)assert(loaded.performance_mixer.gain[i]==1);
        assert(loaded.matrix.row[TS_MATRIX_TILES].matrix_gain[TS_MATRIX_MAIN]==1000);
        assert(!loaded.performance_mixer.restart && !loaded.performance_mixer.arp_sync && !loaded.performance_mixer.always_on_top);
        assert((loaded.parameter_locks & TS_SISTER_UI_PARAMETER_BIT(
                    TS_SISTER_UI_PARAM_REVERB_TYPE)) != 0u);
        assert((loaded.parameter_locks_high & TS_SISTER_UI_PARAMETER_BIT(
                    TS_SISTER_UI_SLOT_PARAMETER(3, 1) - 64)) != 0u);
        assert(loaded.parameters.soak == 0.0f &&
               loaded.parameters.bleed == 0.25f &&
               loaded.parameters.soak_targets == TS_SISTER_EFFECT_TARGET_MIX);
        assert(loaded.parameters.fx.reverb_mix == 0.0f &&
               loaded.parameters.fx.reverb_size > 0.21f &&
               loaded.parameters.fx.reverb_size < 0.23f &&
               loaded.parameters.fx.delay_mix == 0.0f &&
               loaded.parameters.fx.distortion_mix == 0.0f &&
               loaded.parameters.fx.reverb_gain_db == 0.0f &&
               loaded.parameters.fx.delay_gain_db == 0.0f &&
               loaded.parameters.fx.distortion_gain_db == 0.0f &&
               loaded.parameters.fx.grain_mix == 0.0f &&
               loaded.parameters.fx.grain_gain_db == 0.0f &&
               loaded.parameters.fx.grain_pitch == 0.5f &&
               loaded.parameters.fx.grain_targets ==
                   TS_SISTER_EFFECT_TARGET_MIX &&
               loaded.parameters.fx.master_feedback == 0.0f &&
               loaded.parameters.tiles_gain == 1.0f &&
               loaded.parameters.fx_return_gain == 1.0f &&
               loaded.parameters.fx.reverb_targets == TS_SISTER_EFFECT_TARGET_MIX);
        assert(loaded.parameters.fx.slot[3].type == TS_SISTER_FX_REVERB);
        assert(loaded.parameters.fx.slot[3].placement ==
               TS_SISTER_FX_PLACE_POST);
        assert(loaded.parameters.fx.slot[3].parameter_a > 0.21f &&
               loaded.parameters.fx.slot[3].parameter_a < 0.23f);
        assert(loaded.parameters.buffer_seconds == 40.0f);
    }
    remove(state_path);
    {
        FILE *file = fopen(state_path, "wb");
        assert(file != NULL);
        fputs("TapeSister Sister Project State\nVersion=1\nPageCount=0\n", file);
        fclose(file);
        assert(!ts_sister_project_state_load(&loaded, project, 48000u,
                                             &present, error, sizeof(error)));
    }
    remove(state_path);
    REMOVE_DIR(directory);

    ts_sister_project_state_init(&loaded, 48000u);
    assert(ts_sister_project_state_load(&loaded, "missing-project.tsr", 48000u,
                                        &present, error, sizeof(error)));
    assert(!present && loaded.source_switches == 0u &&
           loaded.parameters.soak == 0.0f &&
           loaded.parameters.bleed == 0.25f &&
           loaded.parameters.soak_targets == TS_SISTER_EFFECT_TARGET_MIX &&
           loaded.parameters.fx.reverb_mix == 0.0f &&
           loaded.parameters.fx.delay_mix == 0.0f &&
           loaded.parameters.fx.distortion_mix == 0.0f &&
           loaded.parameters.fx.master_feedback == 0.0f &&
           loaded.parameters.tiles_gain == 1.0f &&
           loaded.parameters.fm_gain == 1.0f &&
           loaded.parameters.external_gain == 1.0f &&
           loaded.parameters.preview_gain == 1.0f &&
           loaded.parameters.tapehead_gain == 1.0f &&
           loaded.parameters.fx_return_gain == 1.0f &&
           loaded.parameters.buffer_seconds == 40.0f);
    puts("sister project-state tests passed");
    return 0;
}
