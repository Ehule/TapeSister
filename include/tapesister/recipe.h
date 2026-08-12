#ifndef TAPESISTER_RECIPE_H
#define TAPESISTER_RECIPE_H

#include "tapesister/sample.h"

enum {
    TS_RECIPE_SLOT_COUNT = 16,
    TS_FACTORY_RECIPE_COUNT = 8,
    TS_RECIPE_NAME_MAX = 31
};

typedef struct {
    char name[TS_RECIPE_NAME_MAX + 1];
    TsProcessRecipe process;
    int factory;
    int occupied;
} TsPortableRecipe;

typedef struct {
    TsPortableRecipe slots[TS_RECIPE_SLOT_COUNT];
    int active_slot;
} TsRecipeBank;

void ts_recipe_bank_init(TsRecipeBank *bank);
int ts_recipe_bank_capture(TsRecipeBank *bank, int slot, const TsProcessRecipe *process,
                           const char *name, char *error, size_t error_size);
int ts_recipe_bank_clear(TsRecipeBank *bank, int slot, char *error, size_t error_size);
int ts_recipe_bank_add_user(TsRecipeBank *bank, const TsPortableRecipe *recipe,
                            char *error, size_t error_size);
int ts_recipe_save(const TsPortableRecipe *recipe, const char *path,
                   char *error, size_t error_size);
int ts_recipe_load(TsPortableRecipe *recipe, const char *path,
                   char *error, size_t error_size);
int ts_recipe_from_process(TsPortableRecipe *recipe, const TsProcessRecipe *process,
                           const char *name);
int ts_recipe_process_valid(const TsProcessRecipe *process);

#endif
