#ifndef TAPESISTER_MANDALA_SDL_H
#define TAPESISTER_MANDALA_SDL_H
#include "tapesister/mandala.h"
#include "tapesister/sister_runtime.h"
#include "tapesister/realtime_diagnostics.h"

/* All functions except audio() run on the host UI thread. No renderer object,
   process handle or shared-memory mapping is reachable from audio(). */
typedef struct { int quality, display, fullscreen; } TsMandalaPreferences;
void ts_mandala_host_init(void);
/* UI-only: opt into the existing atomic runtime snapshot; never read DSP state. */
void ts_mandala_follow_effects(TsSisterRuntime *runtime);
/* UI-only: read existing timing counters without excluding audio. NULL means
   telemetry is unavailable. Visual backoff never changes the saved quality. */
void ts_mandala_follow_audio(const TsRealtimeDiagnostics *diagnostics);
void ts_mandala_report(char *text, size_t size);
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
