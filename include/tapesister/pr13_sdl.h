#ifndef TAPESISTER_PR13_SDL_H
#define TAPESISTER_PR13_SDL_H

#include <SDL2/SDL.h>
#include "tapesister/pr13.h"
#include "tapesister/ui.h"

int ts_pr13_active_slot(void);
void ts_pr13_set_active_slot(int slot);
TsInstrument *ts_pr13_live_instrument(void);
TsUiState *ts_pr13_live_ui(void);
void ts_pr13_render_ui(TsFramebuffer *fb, const TsUiState *ui,
                       const TsInstrument *instrument);
int ts_pr13_poll_event(SDL_Event *event);

int ts_pr13_ui_set_process(TsInstrument *instrument, const TsProcessRecipe *process,
                           char *error, size_t error_size);
int ts_pr13_ui_set_process_tuning(TsInstrument *instrument,
                                  const TsProcessRecipe *process,
                                  const TsTuning *tuning,
                                  char *error, size_t error_size);
int ts_pr13_ui_set_process_tunings(TsInstrument *instrument,
                                   const TsProcessRecipe *process,
                                   const TsTuning *tuning,
                                   const TsTuning *audible_tuning,
                                   char *error, size_t error_size);
int ts_pr13_ui_generate_family(TsInstrument *instrument, int anchor_slot, int reseed,
                               int *created_slot, char *error, size_t error_size);

#endif
