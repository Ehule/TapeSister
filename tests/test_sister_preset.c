#include "tapesister/sister_preset.h"
#include "tapesister/sister_ui.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    static const char path[] = "test-sister-presets.ini";
    TsSisterPresetBank bank, loaded;
    TsSisterParameters p, recalled;
    uint64_t recalled_locks = 0u;
    uint64_t recalled_locks_high = 0u;
    const uint64_t locks =
        TS_SISTER_UI_PARAMETER_BIT(TS_SISTER_UI_PARAM_FILTER_TYPE) |
        TS_SISTER_UI_PARAMETER_BIT(TS_SISTER_UI_PARAM_H2_RATE);
    const uint64_t locks_high = TS_SISTER_UI_PARAMETER_BIT(
        TS_SISTER_UI_PARAM_DELAY_GAIN - 64);
    char error[160];
    ts_sister_preset_bank_init(&bank, 48000u);
    assert(bank.count == 3u && bank.entries[0].factory);
    assert(strcmp(bank.entries[0].name, "KAFKA START") == 0);
    p = bank.entries[0].parameters;
    p.ghost_tone = 0.43f;
    p.filter_q = 1.25f;
    p.tiles_gain = 1.23f;
    p.fm_gain = 2.34f;
    p.external_gain = 3.45f;
    p.preview_gain = 0.67f;
    p.tapehead_gain = 1.89f;
    p.fx_return_gain = 1.54f;
    p.soak = 0.72f;
    p.bleed = 0.81f;
    p.soak_targets = TS_SISTER_EFFECT_TARGET_H1 |
                     TS_SISTER_EFFECT_TARGET_H3;
    p.fx.reverb_type = TS_SISTER_REVERB_CATHEDRAL;
    p.fx.reverb_size = 0.87f;
    p.fx.enabled = 0;
    p.fx.reverb_enabled = 0;
    p.fx.delay_enabled = 1;
    p.fx.distortion_enabled = 0;
    p.fx.grain_enabled = 1;
    p.fx.transition = 0.82f;
    p.fx.master_transition = 0.64f;
    p.fx.reverb_mix = 0.63f;
    p.fx.reverb_decay = 0.91f;
    p.fx.reverb_gain_db = 6.0f;
    p.fx.reverb_targets = TS_SISTER_EFFECT_TARGET_H2;
    p.fx.delay_time = 0.77f;
    p.fx.delay_feedback = 0.88f;
    p.fx.delay_mix = 0.52f;
    p.fx.delay_gain_db = -4.0f;
    p.fx.delay_targets = TS_SISTER_EFFECT_TARGET_H1 |
                         TS_SISTER_EFFECT_TARGET_H3;
    p.fx.distortion_drive = 0.84f;
    p.fx.distortion_tone = 0.19f;
    p.fx.distortion_mix = 0.73f;
    p.fx.distortion_gain_db = 11.0f;
    p.fx.distortion_targets = TS_SISTER_EFFECT_TARGET_MIX;
    p.fx.grain_size = 0.67f;
    p.fx.grain_density = 0.81f;
    p.fx.grain_pitch = 0.26f;
    p.fx.grain_mix = 0.59f;
    p.fx.grain_gain_db = -3.0f;
    p.fx.grain_targets = TS_SISTER_EFFECT_TARGET_H2 |
                         TS_SISTER_EFFECT_TARGET_H3;
    p.fx.master_feedback = 0.69f;
    p.fx.slot[0] = (TsSisterFxSlotControls){
        TS_SISTER_FX_GRAIN, 1, TS_SISTER_FX_PLACE_PRE,
        4.0f, 0.21f, 0.34f, 0.75f, 0.82f
    };
    p.fx.slot[1] = (TsSisterFxSlotControls){
        TS_SISTER_FX_DISTORTION, 1,
        TS_SISTER_FX_PLACE_H1 | TS_SISTER_FX_PLACE_H3,
        -2.0f, 0.73f, 0.44f, 0.5f, 0.61f
    };
    p.fx.fallout.enabled = 1;
    p.fx.fallout.feedback = 0.58f;
    p.fx.fallout.noise_type = TS_SISTER_FALLOUT_NOISE_BLUE;
    p.fx.fallout.transition = 0.82f;
    p.fx.fallout.component_transition = 0.76f;
    p.fx.fallout.master_transition = 0.58f;
    p.fx.fallout.lfo_rate = 0.24f;
    p.fx.fallout.lfo_intensity = 0.67f;
    p.fx.fallout.lfo_targets = TS_SISTER_FALLOUT_LFO_MIX |
                               TS_SISTER_FALLOUT_LFO_FEEDBACK;
    p.fx.fallout.rise_mode = TS_SISTER_FALLOUT_RISE_SAW;
    p.fx.fallout.rise_length = 0.91f;
    p.fx.fallout.rise_intensity = 0.54f;
    p.fx.fallout.rise_targets = TS_SISTER_FALLOUT_LFO_MIX |
                                TS_SISTER_FALLOUT_LFO_NOISE;
    p.fx.fallout.skip_enabled = 1;
    p.fx.fallout.skip_span = 0.76f;
    p.fx.fallout.bit_enabled = 1;
    p.buffer_seconds = 23.0f;
    assert(ts_sister_preset_save_new_with_lock_words(
        &bank, "MY MEMORY", &p, locks, locks_high,
        48000u, error, sizeof(error)));
    assert(!ts_sister_preset_overwrite(&bank, 0u, &p, 48000u,
                                       error, sizeof(error)));
    assert(ts_sister_preset_save(&bank, path, error, sizeof(error)));
    assert(ts_sister_preset_load(&loaded, path, 48000u, error, sizeof(error)));
    assert(loaded.count == 4u);
    assert(ts_sister_preset_recall_with_lock_words(
        &loaded, 3u, &recalled, &recalled_locks,
        &recalled_locks_high));
    assert(recalled_locks == locks);
    assert(recalled_locks_high == locks_high);
    assert((recalled_locks &
            TS_SISTER_UI_PARAMETER_BIT(TS_SISTER_UI_PARAM_FILTER_CUTOFF)) == 0u);
    assert(recalled.ghost_tone > 0.42f && recalled.ghost_tone < 0.44f);
    assert(recalled.filter_q > 1.24f && recalled.filter_q < 1.26f);
    assert(recalled.tiles_gain > 1.22f && recalled.tiles_gain < 1.24f);
    assert(recalled.fm_gain > 2.33f && recalled.fm_gain < 2.35f);
    assert(recalled.external_gain > 3.44f && recalled.external_gain < 3.46f);
    assert(recalled.preview_gain > 0.66f && recalled.preview_gain < 0.68f);
    assert(recalled.tapehead_gain > 1.88f && recalled.tapehead_gain < 1.90f);
    assert(recalled.fx_return_gain > 1.53f && recalled.fx_return_gain < 1.55f);
    assert(recalled.soak > 0.71f && recalled.soak < 0.73f);
    assert(recalled.bleed > 0.80f && recalled.bleed < 0.82f);
    assert(recalled.soak_targets == (TS_SISTER_EFFECT_TARGET_H1 |
                                     TS_SISTER_EFFECT_TARGET_H3));
    assert(recalled.fx.reverb_type == TS_SISTER_REVERB_CATHEDRAL);
    assert(recalled.fx.reverb_size > 0.86f &&
           recalled.fx.reverb_size < 0.88f);
    assert(recalled.fx.enabled == 0);
    assert(recalled.fx.reverb_enabled == 0);
    assert(recalled.fx.delay_enabled == 1);
    assert(recalled.fx.distortion_enabled == 0);
    assert(recalled.fx.transition > 0.81f);
    assert(recalled.fx.master_transition > 0.63f &&
           recalled.fx.master_transition < 0.65f);
    assert(recalled.fx.reverb_mix > 0.62f && recalled.fx.reverb_mix < 0.64f);
    assert(recalled.fx.reverb_gain_db > 5.99f &&
           recalled.fx.reverb_gain_db < 6.01f);
    assert(recalled.fx.reverb_targets == TS_SISTER_EFFECT_TARGET_H2);
    assert(recalled.fx.delay_targets == (TS_SISTER_EFFECT_TARGET_H1 |
                                          TS_SISTER_EFFECT_TARGET_H3));
    assert(recalled.fx.delay_gain_db < -3.99f &&
           recalled.fx.delay_gain_db > -4.01f);
    assert(recalled.fx.distortion_targets == TS_SISTER_EFFECT_TARGET_MIX);
    assert(recalled.fx.distortion_gain_db > 10.99f &&
           recalled.fx.distortion_gain_db < 11.01f);
    assert(recalled.fx.grain_enabled == 1);
    assert(recalled.fx.grain_size > 0.66f && recalled.fx.grain_size < 0.68f);
    assert(recalled.fx.grain_density > 0.80f &&
           recalled.fx.grain_density < 0.82f);
    assert(recalled.fx.grain_pitch > 0.25f && recalled.fx.grain_pitch < 0.27f);
    assert(recalled.fx.grain_mix > 0.58f && recalled.fx.grain_mix < 0.60f);
    assert(recalled.fx.grain_gain_db < -2.99f &&
           recalled.fx.grain_gain_db > -3.01f);
    assert(recalled.fx.grain_targets == (TS_SISTER_EFFECT_TARGET_H2 |
                                          TS_SISTER_EFFECT_TARGET_H3));
    assert(recalled.fx.slot[0].type == TS_SISTER_FX_GRAIN);
    assert(recalled.fx.slot[0].placement == TS_SISTER_FX_PLACE_PRE);
    assert(recalled.fx.slot[0].gain_db > 3.99f &&
           recalled.fx.slot[0].gain_db < 4.01f);
    assert(recalled.fx.slot[0].mix > 0.81f &&
           recalled.fx.slot[0].mix < 0.83f);
    assert(recalled.fx.slot[1].type == TS_SISTER_FX_DISTORTION);
    assert(recalled.fx.slot[1].placement ==
           (TS_SISTER_FX_PLACE_H1 | TS_SISTER_FX_PLACE_H3));
    assert(recalled.fx.master_feedback > 0.68f);
    assert(recalled.fx.fallout.enabled == 1);
    assert(recalled.fx.fallout.feedback > 0.57f);
    assert(recalled.fx.fallout.noise_type == TS_SISTER_FALLOUT_NOISE_BLUE);
    assert(recalled.fx.fallout.transition > 0.81f);
    assert(recalled.fx.fallout.component_transition > 0.75f);
    assert(recalled.fx.fallout.master_transition > 0.57f &&
           recalled.fx.fallout.master_transition < 0.59f);
    assert(recalled.fx.fallout.lfo_rate > 0.23f);
    assert(recalled.fx.fallout.lfo_intensity > 0.66f);
    assert(recalled.fx.fallout.lfo_targets ==
           (TS_SISTER_FALLOUT_LFO_MIX |
            TS_SISTER_FALLOUT_LFO_FEEDBACK));
    assert(recalled.fx.fallout.rise_mode == TS_SISTER_FALLOUT_RISE_SAW);
    assert(recalled.fx.fallout.rise_length > 0.90f);
    assert(recalled.fx.fallout.rise_intensity > 0.53f);
    assert(recalled.fx.fallout.rise_targets ==
           (TS_SISTER_FALLOUT_LFO_MIX | TS_SISTER_FALLOUT_LFO_NOISE));
    assert(recalled.fx.fallout.skip_enabled == 1);
    assert(recalled.fx.fallout.skip_span > 0.75f);
    assert(recalled.fx.fallout.bit_enabled == 1);
    assert(recalled.buffer_seconds == 23.0f);
    assert(ts_sister_preset_rename(&loaded, 3u, "RENAMED", error, sizeof(error)));
    p.ghost_tone = 0.77f;
    p.fx.delay_feedback = 0.31f;
    assert(ts_sister_preset_overwrite_with_locks(
        &loaded, 3u, &p, locks, 48000u, error, sizeof(error)));
    memset(&recalled, 0, sizeof(recalled));
    recalled_locks = 0u;
    assert(ts_sister_preset_recall_with_locks(
        &loaded, 3u, &recalled, &recalled_locks));
    assert(strcmp(loaded.entries[3].name, "RENAMED") == 0);
    assert(recalled.ghost_tone > 0.76f && recalled.ghost_tone < 0.78f);
    assert(recalled.fx.delay_feedback > 0.30f &&
           recalled.fx.delay_feedback < 0.32f);
    assert(recalled_locks == locks);
    assert(ts_sister_preset_delete(&loaded, 3u, error, sizeof(error)));
    assert(loaded.count == 3u);
    remove(path);

    {
        FILE *file = fopen(path, "wb");
        assert(file != NULL);
        fputs("TapeSister Sister Presets\nVersion=9\n\n[Preset]\n"
              "Name=FUTURE\nlocks=0000000100000000\nlocks_high=0\n"
              "ghost_tone=0.5\nsoak_targets=255\n"
              "reverb_type=3\nnewer_field=42\n", file);
        fclose(file);
        assert(ts_sister_preset_load(&loaded, path, 48000u,
                                     error, sizeof(error)));
        assert(loaded.count == 4u && loaded.entries[3].parameters.ghost_tone == 0.5f);
        assert((loaded.entries[3].parameter_locks &
                TS_SISTER_UI_PARAMETER_BIT(
                    TS_SISTER_UI_PARAM_REVERB_TYPE)) != 0u);
        assert((loaded.entries[3].parameter_locks_high &
                TS_SISTER_UI_PARAMETER_BIT(
                    TS_SISTER_UI_SLOT_PARAMETER(3, 1) - 64)) != 0u);
        assert(loaded.entries[3].parameters.soak == 0.0f);
        assert(loaded.entries[3].parameters.bleed == 0.25f);
        assert(loaded.entries[3].parameters.soak_targets ==
               TS_SISTER_EFFECT_TARGET_MIX);
        assert(loaded.entries[3].parameters.fx.reverb_mix == 0.0f);
        assert(loaded.entries[3].parameters.fx.reverb_size > 0.81f &&
               loaded.entries[3].parameters.fx.reverb_size < 0.83f);
        assert(loaded.entries[3].parameters.fx.delay_mix == 0.0f);
        assert(loaded.entries[3].parameters.fx.distortion_mix == 0.0f);
        assert(loaded.entries[3].parameters.fx.reverb_gain_db == 0.0f);
        assert(loaded.entries[3].parameters.fx.delay_gain_db == 0.0f);
        assert(loaded.entries[3].parameters.fx.distortion_gain_db == 0.0f);
        assert(loaded.entries[3].parameters.fx.grain_mix == 0.0f);
        assert(loaded.entries[3].parameters.fx.grain_gain_db == 0.0f);
        assert(loaded.entries[3].parameters.fx.grain_pitch == 0.5f);
        assert(loaded.entries[3].parameters.fx.grain_targets ==
               TS_SISTER_EFFECT_TARGET_MIX);
        assert(loaded.entries[3].parameters.fx.master_feedback == 0.0f);
        assert(loaded.entries[3].parameters.tiles_gain == 1.0f);
        assert(loaded.entries[3].parameters.fm_gain == 1.0f);
        assert(loaded.entries[3].parameters.external_gain == 1.0f);
        assert(loaded.entries[3].parameters.preview_gain == 1.0f);
        assert(loaded.entries[3].parameters.tapehead_gain == 1.0f);
        assert(loaded.entries[3].parameters.fx_return_gain == 1.0f);
        assert(loaded.entries[3].parameters.fx.reverb_targets ==
               TS_SISTER_EFFECT_TARGET_MIX);
        assert(loaded.entries[3].parameters.fx.slot[3].type ==
               TS_SISTER_FX_REVERB);
        assert(loaded.entries[3].parameters.fx.slot[3].placement ==
               TS_SISTER_FX_PLACE_POST);
        assert(loaded.entries[3].parameters.fx.slot[3].parameter_a > 0.81f &&
               loaded.entries[3].parameters.fx.slot[3].parameter_a < 0.83f);
        assert(loaded.entries[3].parameters.buffer_seconds == 40.0f);
    }
    remove(path);
    {
        FILE *file = fopen(path, "wb");
        assert(file != NULL);
        fputs("not a preset file\n", file);
        fclose(file);
        assert(!ts_sister_preset_load(&loaded, path, 48000u,
                                      error, sizeof(error)));
    }
    remove(path);
    puts("sister preset tests passed");
    return 0;
}
