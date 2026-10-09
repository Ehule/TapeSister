#ifndef TAPESISTER_TAPEHEAD_EMBED_H
#define TAPESISTER_TAPEHEAD_EMBED_H
#include <SDL2/SDL.h>
#include "tapesister/sample_pages.h"
#include "tapesister/ui.h"
#include "tapesister/note_event.h"

#include "tapesister/tapehead_actions.h"
typedef struct {
    SDL_Window *window;
    void *context;
    void (*lock)(void *context);
    void (*unlock)(void *context);
    void (*present)(void *context, const uint32_t *pixels);
} TsTapeHeadHost;
int ts_tapehead_init(const TsTapeHeadHost *host, unsigned rate, char *error, size_t size);
void ts_tapehead_close(void);
int ts_tapehead_sync(TsSamplePages *pages, const TsInstrument *active, unsigned rate,
                     char *error, size_t size);
int ts_tapehead_event(const SDL_Event *event, int x, int y);
void ts_tapehead_tick(void);
void ts_tapehead_service(int draw);
/* UI suspended its periodic sync; export must preserve a pending host load. */
void ts_tapehead_suspend_sync(void);
void ts_tapehead_status(const TsUiState *host_ui,unsigned capture);
void ts_tapehead_focus_lost(void);
int ts_tapehead_midi(const TsMidiEvent *event,int allow_note_on);
void ts_tapehead_stop(void);
const uint32_t *ts_tapehead_frame(void);
const float *ts_tapehead_render(unsigned frames, unsigned rate);
void ts_tapehead_route_outputs(unsigned available);
const TsSourceRouteMix *ts_tapehead_clean_render(void);
void ts_tapehead_routes_changed(TsTileId tile,TsSourceRoute route);
int ts_tapehead_action(void);
int ts_tapehead_running(void);
int ts_tapehead_following(void);
int ts_tapehead_live_editing(void);
int ts_tapehead_block_active(void);
unsigned ts_tapehead_capture_flags(unsigned frame);
void ts_tapehead_audio_span(unsigned offset,unsigned frames,int seam);
int ts_tapehead_export(TsSisterTracker *tracker, char *error, size_t size);
/* Internal callbacks used only by the imported application sources. */
void ts_tapehead_host_lock(void);
void ts_tapehead_host_unlock(void);
void ts_tapehead_host_present(void);
void ts_tapehead_host_mouse(const SDL_Event *event);
void ts_tapehead_host_redraw(void);
/* UI-only: combine the last synced source pan with the resolved clean route. */
void ts_tapehead_scope_source(int lane,uint64_t tile_id,unsigned route_index);
int ts_tapehead_scope_pan(int lane,int source_pan);
void ts_tapehead_request(int action);
void ts_tapehead_show_config(void);
int ts_tapehead_config_visible(void);
int ts_tapehead_interpolation_active(void);
int ts_tapehead_preferences_load(const char *path,char *error,size_t size);
int ts_tapehead_preferences_save(const char *path,char *error,size_t size);
int ts_tapehead_palette_import(const char *path,char *error,size_t size);
int ts_tapehead_palette_export(const char *path,char *error,size_t size);
/* Stage XM, MOD or IT without changing the current score or tiles. The caller
   owns the returned pages and installs them only after successful validation. */
int ts_tapehead_module_prepare(const char *path,TsSamplePages *staged,char *error,size_t size);
int ts_tapehead_xm_save(const char *path,char *error,size_t size);
#endif
