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
    const uint64_t locks =
        TS_SISTER_UI_PARAMETER_BIT(TS_SISTER_UI_PARAM_FILTER_TYPE) |
        TS_SISTER_UI_PARAMETER_BIT(TS_SISTER_UI_PARAM_H2_RATE);
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
    p.fx_return_gain = 1.54f;
    p.soak = 0.72f;
    p.bleed = 0.81f;
    p.soak_targets = TS_SISTER_EFFECT_TARGET_H1 |
                     TS_SISTER_EFFECT_TARGET_H3;
    p.fx.reverb_type = TS_SISTER_REVERB_CATHEDRAL;
    p.fx.reverb_mix = 0.63f;
    p.fx.reverb_decay = 0.91f;
    p.fx.reverb_targets = TS_SISTER_EFFECT_TARGET_H2;
    p.fx.delay_time = 0.77f;
    p.fx.delay_feedback = 0.88f;
    p.fx.delay_mix = 0.52f;
    p.fx.delay_targets = TS_SISTER_EFFECT_TARGET_H1 |
                         TS_SISTER_EFFECT_TARGET_H3;
    p.fx.distortion_drive = 0.84f;
    p.fx.distortion_tone = 0.19f;
    p.fx.distortion_mix = 0.73f;
    p.fx.distortion_targets = TS_SISTER_EFFECT_TARGET_MIX;
    p.fx.master_feedback = 0.69f;
    p.buffer_seconds = 23.0f;
    assert(ts_sister_preset_save_new_with_locks(
        &bank, "MY MEMORY", &p, locks, 48000u, error, sizeof(error)));
    assert(!ts_sister_preset_overwrite(&bank, 0u, &p, 48000u,
                                       error, sizeof(error)));
    assert(ts_sister_preset_save(&bank, path, error, sizeof(error)));
    assert(ts_sister_preset_load(&loaded, path, 48000u, error, sizeof(error)));
    assert(loaded.count == 4u);
    assert(ts_sister_preset_recall_with_locks(
        &loaded, 3u, &recalled, &recalled_locks));
    assert(recalled_locks == locks);
    assert((recalled_locks &
            TS_SISTER_UI_PARAMETER_BIT(TS_SISTER_UI_PARAM_FILTER_CUTOFF)) == 0u);
    assert(recalled.ghost_tone > 0.42f && recalled.ghost_tone < 0.44f);
    assert(recalled.filter_q > 1.24f && recalled.filter_q < 1.26f);
    assert(recalled.tiles_gain > 1.22f && recalled.tiles_gain < 1.24f);
    assert(recalled.fm_gain > 2.33f && recalled.fm_gain < 2.35f);
    assert(recalled.external_gain > 3.44f && recalled.external_gain < 3.46f);
    assert(recalled.preview_gain > 0.66f && recalled.preview_gain < 0.68f);
    assert(recalled.fx_return_gain > 1.53f && recalled.fx_return_gain < 1.55f);
    assert(recalled.soak > 0.71f && recalled.soak < 0.73f);
    assert(recalled.bleed > 0.80f && recalled.bleed < 0.82f);
    assert(recalled.soak_targets == (TS_SISTER_EFFECT_TARGET_H1 |
                                     TS_SISTER_EFFECT_TARGET_H3));
    assert(recalled.fx.reverb_type == TS_SISTER_REVERB_CATHEDRAL);
    assert(recalled.fx.reverb_mix > 0.62f && recalled.fx.reverb_mix < 0.64f);
    assert(recalled.fx.reverb_targets == TS_SISTER_EFFECT_TARGET_H2);
    assert(recalled.fx.delay_targets == (TS_SISTER_EFFECT_TARGET_H1 |
                                          TS_SISTER_EFFECT_TARGET_H3));
    assert(recalled.fx.distortion_targets == TS_SISTER_EFFECT_TARGET_MIX);
    assert(recalled.fx.master_feedback > 0.68f);
    assert(recalled.buffer_seconds == 23.0f);
    assert(ts_sister_preset_rename(&loaded, 3u, "RENAMED", error, sizeof(error)));
    assert(ts_sister_preset_overwrite(&loaded, 3u, &p, 48000u,
                                      error, sizeof(error)));
    assert(ts_sister_preset_delete(&loaded, 3u, error, sizeof(error)));
    assert(loaded.count == 3u);
    remove(path);

    {
        FILE *file = fopen(path, "wb");
        assert(file != NULL);
        fputs("TapeSister Sister Presets\nVersion=9\n\n[Preset]\n"
              "Name=FUTURE\nghost_tone=0.5\nsoak_targets=255\n"
              "newer_field=42\n", file);
        fclose(file);
        assert(ts_sister_preset_load(&loaded, path, 48000u,
                                     error, sizeof(error)));
        assert(loaded.count == 4u && loaded.entries[3].parameters.ghost_tone == 0.5f);
        assert(loaded.entries[3].parameter_locks == 0u);
        assert(loaded.entries[3].parameters.soak == 0.0f);
        assert(loaded.entries[3].parameters.bleed == 0.25f);
        assert(loaded.entries[3].parameters.soak_targets ==
               TS_SISTER_EFFECT_TARGET_MIX);
        assert(loaded.entries[3].parameters.fx.reverb_mix == 0.0f);
        assert(loaded.entries[3].parameters.fx.delay_mix == 0.0f);
        assert(loaded.entries[3].parameters.fx.distortion_mix == 0.0f);
        assert(loaded.entries[3].parameters.fx.master_feedback == 0.0f);
        assert(loaded.entries[3].parameters.tiles_gain == 1.0f);
        assert(loaded.entries[3].parameters.fm_gain == 1.0f);
        assert(loaded.entries[3].parameters.external_gain == 1.0f);
        assert(loaded.entries[3].parameters.preview_gain == 1.0f);
        assert(loaded.entries[3].parameters.fx_return_gain == 1.0f);
        assert(loaded.entries[3].parameters.fx.reverb_targets ==
               TS_SISTER_EFFECT_TARGET_MIX);
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
