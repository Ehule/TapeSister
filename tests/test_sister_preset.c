#include "tapesister/sister_preset.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    static const char path[] = "test-sister-presets.ini";
    TsSisterPresetBank bank, loaded;
    TsSisterParameters p, recalled;
    char error[160];
    ts_sister_preset_bank_init(&bank, 48000u);
    assert(bank.count == 3u && bank.entries[0].factory);
    assert(strcmp(bank.entries[0].name, "KAFKA START") == 0);
    p = bank.entries[0].parameters;
    p.ghost_tone = 0.43f;
    p.filter_q = 1.25f;
    assert(ts_sister_preset_save_new(&bank, "MY MEMORY", &p, 48000u,
                                     error, sizeof(error)));
    assert(!ts_sister_preset_overwrite(&bank, 0u, &p, 48000u,
                                       error, sizeof(error)));
    assert(ts_sister_preset_save(&bank, path, error, sizeof(error)));
    assert(ts_sister_preset_load(&loaded, path, 48000u, error, sizeof(error)));
    assert(loaded.count == 4u);
    assert(ts_sister_preset_recall(&loaded, 3u, &recalled));
    assert(recalled.ghost_tone > 0.42f && recalled.ghost_tone < 0.44f);
    assert(recalled.filter_q > 1.24f && recalled.filter_q < 1.26f);
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
              "Name=FUTURE\nghost_tone=0.5\nnewer_field=42\n", file);
        fclose(file);
        assert(ts_sister_preset_load(&loaded, path, 48000u,
                                     error, sizeof(error)));
        assert(loaded.count == 4u && loaded.entries[3].parameters.ghost_tone == 0.5f);
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
