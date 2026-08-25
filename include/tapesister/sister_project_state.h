#ifndef TAPESISTER_SISTER_PROJECT_STATE_H
#define TAPESISTER_SISTER_PROJECT_STATE_H

#include <stddef.h>
#include <stdint.h>

#include "tapesister/sister_runtime.h"

enum {
    TS_SISTER_PROJECT_STATE_VERSION = 4,
    TS_SISTER_PROJECT_PRESET_NAME_MAX = 47
};

typedef struct {
    uint8_t source_switches;
    size_t page_count;
    size_t active_page;
    uint16_t page_masks[TS_SISTER_RUNTIME_PAGE_LIMIT];
    TsSisterParameters parameters;
    char selected_preset[TS_SISTER_PROJECT_PRESET_NAME_MAX + 1];
} TsSisterProjectState;

void ts_sister_project_state_init(TsSisterProjectState *state,
                                  uint32_t sample_rate);
void ts_sister_project_state_capture(TsSisterProjectState *state,
                                     const TsSisterRuntime *runtime,
                                     size_t page_count,
                                     const char *selected_preset);
int ts_sister_project_state_apply(const TsSisterProjectState *state,
                                  TsSisterRuntime *runtime,
                                  const TsInstrument *active_instrument);
int ts_sister_project_state_load(TsSisterProjectState *state,
                                 const char *project_path,
                                 uint32_t sample_rate, int *present,
                                 char *error, size_t error_size);
int ts_sister_project_state_save(const TsSisterProjectState *state,
                                 const char *project_path,
                                 char *error, size_t error_size);

#endif
