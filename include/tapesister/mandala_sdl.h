#ifndef TAPESISTER_MANDALA_SDL_H
#define TAPESISTER_MANDALA_SDL_H
#include "tapesister/mandala.h"

/* All functions except audio() run on the host UI thread. No renderer object,
   process handle or shared-memory mapping is reachable from audio(). */
typedef struct { int quality, display, fullscreen; } TsMandalaPreferences;
void ts_mandala_host_init(void);
void ts_mandala_audio(const float *output, unsigned frames, unsigned channels);
int ts_mandala_open(const TsMandalaConfig *config, const TsMandalaPreferences *prefs, char *error, size_t size);
int ts_mandala_poll(const TsMandalaConfig *config, TsMandalaPreferences *prefs);
int ts_mandala_active(void);
int ts_mandala_frozen(void);
void ts_mandala_freeze(void);
void ts_mandala_close(void);
void ts_mandala_shutdown(void);
/* Dispatch before initializing any audio, MIDI, tracker or application state.
   Returns -1 for normal app arguments. */
int ts_mandala_dispatch(int argc, char **argv);
#endif
