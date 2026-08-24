#ifndef TAPESISTER_SISTER_PRESET_H
#define TAPESISTER_SISTER_PRESET_H

#include <stddef.h>
#include <stdint.h>

#include "tapesister/sister_machine.h"

enum {
    TS_SISTER_PRESET_VERSION = 2,
    TS_SISTER_PRESET_NAME_MAX = 47,
    TS_SISTER_PRESET_LIMIT = 64,
    TS_SISTER_FACTORY_PRESET_COUNT = 3
};

typedef struct {
    char name[TS_SISTER_PRESET_NAME_MAX + 1];
    TsSisterParameters parameters;
    int factory;
} TsSisterPreset;

typedef struct {
    TsSisterPreset entries[TS_SISTER_PRESET_LIMIT];
    size_t count;
} TsSisterPresetBank;

void ts_sister_preset_bank_init(TsSisterPresetBank *bank,
                                uint32_t sample_rate);
int ts_sister_preset_recall(const TsSisterPresetBank *bank, size_t index,
                            TsSisterParameters *parameters);
int ts_sister_preset_save_new(TsSisterPresetBank *bank, const char *name,
                              const TsSisterParameters *parameters,
                              uint32_t sample_rate,
                              char *error, size_t error_size);
int ts_sister_preset_overwrite(TsSisterPresetBank *bank, size_t index,
                               const TsSisterParameters *parameters,
                               uint32_t sample_rate,
                               char *error, size_t error_size);
int ts_sister_preset_rename(TsSisterPresetBank *bank, size_t index,
                            const char *name,
                            char *error, size_t error_size);
int ts_sister_preset_delete(TsSisterPresetBank *bank, size_t index,
                            char *error, size_t error_size);
int ts_sister_preset_load(TsSisterPresetBank *bank, const char *path,
                          uint32_t sample_rate,
                          char *error, size_t error_size);
int ts_sister_preset_save(const TsSisterPresetBank *bank, const char *path,
                          char *error, size_t error_size);

#endif
