#ifndef TAPESISTER_PR13_H
#define TAPESISTER_PR13_H

#include "tapesister/sample.h"

float ts_pr13_family_similarity(TsFamilyRelation relation, float mutation);
int ts_pr13_slot_locked(const TsInstrument *instrument, int slot);
int ts_pr13_set_slot_locked(TsInstrument *instrument, int slot, int locked,
                            char *error, size_t error_size);
int ts_pr13_toggle_slot_lock(TsInstrument *instrument, int slot,
                             char *error, size_t error_size);
int ts_pr13_apply_body_edge_drift(TsSample *sample, float body, float edge,
                                  float drift, char *error, size_t error_size);
int ts_pr13_sync_active_slot(TsInstrument *instrument, int active_slot,
                             char *error, size_t error_size);
int ts_pr13_set_process(TsInstrument *instrument, int active_slot,
                        const TsProcessRecipe *process,
                        char *error, size_t error_size);
int ts_pr13_set_process_and_tuning(TsInstrument *instrument, int active_slot,
                                   const TsProcessRecipe *process,
                                   const TsTuning *tuning,
                                   char *error, size_t error_size);
int ts_pr13_set_process_and_tunings(TsInstrument *instrument, int active_slot,
                                    const TsProcessRecipe *process,
                                    const TsTuning *tuning,
                                    const TsTuning *audible_tuning,
                                    char *error, size_t error_size);
int ts_pr13_rerender(TsInstrument *instrument, int active_slot,
                     char *error, size_t error_size);
int ts_pr13_generate_family_candidate(TsInstrument *instrument, int active_slot,
                                      int reseed, int *created_slot,
                                      char *error, size_t error_size);
int ts_pr13_generate_parent_in_slot(TsInstrument *instrument, int active_slot,
                                    int reseed,
                                    char *error, size_t error_size);
int ts_pr13_save_project(const TsInstrument *instrument, const char *path,
                         char *error, size_t error_size);
int ts_pr13_load_project(TsInstrument *instrument, const char *path,
                         char *error, size_t error_size);

#endif
