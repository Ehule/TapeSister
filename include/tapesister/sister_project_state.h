#ifndef TAPESISTER_SISTER_PROJECT_STATE_H
#define TAPESISTER_SISTER_PROJECT_STATE_H

#include "tapesister/compressor.h"
#include <stddef.h>
#include <stdint.h>

#include "tapesister/sister_runtime.h"
#include "tapesister/keyboard_sequence.h"
#include "tapesister/mosaic.h"

enum {
    TS_SISTER_PROJECT_STATE_VERSION = 45,
    TS_SISTER_PROJECT_PRESET_NAME_MAX = 47
};

/* Saved editor location, separate from running transport and audio state. */
enum { TS_PROJECT_VIEW_CANVAS, TS_PROJECT_VIEW_MOSAIC, TS_PROJECT_VIEW_TRACKER };
typedef struct {
    int present, workspace, keyboard, ingredients, recipes;
    size_t parent_first, parent_last;
    double scroll, xscroll, scale, hscale, time;
    int follow, pattern_tab, source_page;
    uint64_t selected, selection[TS_MOSAIC_EVENTS];
    unsigned selection_count;
    int tracker_scroll, tracker_expanded, tracker_field;
} TsProjectView;

typedef struct {
    uint8_t source_switches;
    size_t page_count;
    size_t active_page;
    uint16_t page_masks[TS_SISTER_RUNTIME_PAGE_LIMIT];
    TsSisterParameters parameters;
    TsMasterEqControls master_eq;
    TsEqMorph eq_morph;
    TsCompressorControls compressor;
    TsRouterControls router;
    TsMatrixControls matrix;
    TsMatrixLayout matrix_layout;
    TsMandalaConfig mandala;
    unsigned performance_transport;
    TsPerformanceMixer performance_mixer;
    TsMasterClockControls clock_controls;
    /* Restore the displayed clock location, always paused. */
    double clock_seconds, clock_beats, clock_phase;
    TsRouterPerformance router_performance;
    TsKeyboardSequenceBank keyboard_sequence;
    TsInsertControls insert;
    TsSpatialControls spatial;
    uint64_t parameter_locks;
    uint64_t parameter_locks_high;
    char selected_preset[TS_SISTER_PROJECT_PRESET_NAME_MAX + 1];
    int selected_preset_modified;
    TsProjectView view;
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
int ts_sister_project_state_load_file(TsSisterProjectState *state,
                                      const char *path,
                                      uint32_t sample_rate, int *present,
                                      char *error, size_t error_size);
int ts_sister_project_state_save(const TsSisterProjectState *state,
                                 const char *project_path,
                                 char *error, size_t error_size);
int ts_sister_project_state_save_file(const TsSisterProjectState *state,
                                      const char *path,
                                      char *error, size_t error_size);

#endif
