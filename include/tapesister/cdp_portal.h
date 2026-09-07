#ifndef TAPESISTER_CDP_PORTAL_H
#define TAPESISTER_CDP_PORTAL_H

#include "tapesister/cdp_adapter.h"

/* Portal identities are independent of the fixed 32 factory instrument slots.
   Recipes contain data, never executable names, paths, or shell fragments. */
enum { TS_PORTAL_CHAIN_STAGES = 8, TS_PORTAL_PARAMS = 16, TS_PORTAL_SLOTS = 32,
       TS_PORTAL_HISTORY = 4, TS_PORTAL_WAVE_COLUMNS = 310,
       TS_PORTAL_MAX_FRAMES = 8000000 };
typedef enum { TS_PORTAL_INTEGER, TS_PORTAL_REAL, TS_PORTAL_SWITCH, TS_PORTAL_ODD_INTEGER, TS_PORTAL_ENUMERATED } TsPortalParamType;
typedef enum { TS_PORTAL_WAVESET, TS_PORTAL_SPECTRAL, TS_PORTAL_TIME, TS_PORTAL_FILTER, TS_PORTAL_GRAIN, TS_PORTAL_LOFI, TS_PORTAL_LEVEL, TS_PORTAL_DELAY, TS_PORTAL_ENVELOPE, TS_PORTAL_STRUCTURE, TS_PORTAL_FACTORY, TS_PORTAL_FAMILIES } TsPortalFamily;
typedef enum { TS_PORTAL_RENAME=1, TS_PORTAL_UPDATE, TS_PORTAL_REPLACE, TS_PORTAL_REMOVE } TsPortalEdit;
typedef struct {
    const char *id, *label, *help, *flag;
    TsPortalParamType type;
    double minimum, maximum, initial;
} TsPortalParam;
typedef struct {
    const char *id, *title, *description, *command;
    unsigned version, mode, parameter_count;
    int changes_duration;
    TsPortalParam parameters[TS_PORTAL_PARAMS];
    TsPortalFamily family;
    const char *executable;
} TsPortalProcess;
typedef struct {
    char process_id[64], name[40];
    unsigned version, exposed;
    double values[TS_PORTAL_PARAMS];
    int bypass;
} TsPortalStep;
typedef struct {
    char process_id[64], name[40];
    unsigned version, exposed;
    double values[TS_PORTAL_PARAMS];
    unsigned stage_count; /* zero is a legacy single-process recipe */
    TsPortalStep stages[TS_PORTAL_CHAIN_STAGES];
} TsPortalRecipe;
typedef struct {
    TsPortalRecipe recipes[TS_PORTAL_SLOTS], pins[TS_PORTAL_SLOTS];
} TsPortalLibrary;

typedef struct {
    size_t first, last, selection_first, selection_last, playhead;
    int has_selection;
    float minimum[TS_PORTAL_WAVE_COLUMNS], maximum[TS_PORTAL_WAVE_COLUMNS];
} TsPortalWave;
typedef struct {
    int selection;
    size_t first, last, result_last;
} TsPortalRegion;
typedef struct {
    int open, busy, valid, playing, listen_result, loop;
    int tab, scroll, search_focus, name_focus, exact_pin, pin_slot, macro_view;
    int parameter_scroll, dragging_parameter, dragging_wave, drag_x;
    int number_focus, wave_dragged;
    int note_count;
    int load_selection, process_selection, full_action;
    TsPortalRegion rendered_region;
    int family, selected_tab, selected_slot;
    int manage_open, manage_tab, manage_slot, manage_scroll, manage_action, manage_name_focus;
    char manage_name[40];
    TsPortalRecipe manage_recipe;
    char number_text[32];
    size_t drag_anchor;
    char query[32], message[160], source_name[64];
    int chain_active, chain_stage, chain_add, chain_audition;
    unsigned chain_cached;
    TsPortalRecipe chain;
    TsPortalRecipe recipe;
    TsPortalLibrary library;
    const TsSample *source, *result;
    TsPortalWave waves[2];
    TsCdpSafetyStatus safety;
    float peak;
    int history_count, history_selected;
    char history_names[TS_PORTAL_HISTORY][24];
} TsPortalUi;

void ts_portal_step_get(const TsPortalStep *step, TsPortalRecipe *recipe);
void ts_portal_step_set(TsPortalStep *step, const TsPortalRecipe *recipe);
void ts_portal_recipe_exact(TsPortalRecipe *recipe);
int ts_portal_step_equal(const TsPortalStep *a, const TsPortalStep *b);
const TsCdpRecipe *ts_portal_factory_find(const char *id);
const TsPortalProcess *ts_portal_factory_process_at(size_t index);
void ts_portal_factory_recipe(const TsCdpRecipe *factory,const TsCdpRecipeValues *values,TsPortalRecipe *recipe);
void ts_portal_factory_values(const TsPortalRecipe *recipe,TsCdpRecipeValues *values);
double ts_portal_parameter_quantize(const TsPortalProcess *process,unsigned index,double value);
double ts_portal_parameter_nudge(const TsPortalProcess *process,unsigned index,double value,int direction);
void ts_portal_parameter_format(const TsPortalProcess *process,unsigned index,double value,char *text,size_t size);
size_t ts_portal_process_count(void);
const TsPortalProcess *ts_portal_process_at(size_t index);
const TsPortalProcess *ts_portal_process_find(const char *id);
void ts_portal_recipe_default(TsPortalRecipe *recipe, const TsPortalProcess *process);
int ts_portal_recipe_validate(const TsPortalRecipe *recipe, char *error, size_t size);
int ts_portal_build_command(const TsPortalRecipe *recipe, const TsSample *input,
                           TsCdpCommand *command, char *error, size_t size);
int ts_portal_build_commands(const TsPortalRecipe *recipe, const TsSample *input,
                            TsCdpCommand commands[TS_CDP_MAX_STAGES], size_t *count,
                            char *error, size_t size);
int ts_cdp_run_portal(const TsCdpRuntime *runtime, const TsPortalRecipe *recipe,
                      const TsSample *input, const TsCdpRunOptions *options,
                      TsCdpRunResult *result, char *error, size_t size);
int ts_portal_library_save(const TsPortalLibrary *library, const char *path,
                           char *error, size_t size);
int ts_portal_library_load(TsPortalLibrary *library, const char *path,
                           char *error, size_t size);
int ts_portal_library_edit(TsPortalLibrary *library, const char *path, int pin, int slot,
                           TsPortalEdit edit, const TsPortalRecipe *recipe, const char *name,
                           char *error, size_t size);
const char *ts_portal_family_name(int family);
int ts_portal_filter_slot(const TsPortalUi *ui, int row, TsPortalRecipe *recipe);
void ts_portal_ui_init(TsPortalUi *ui);
int ts_portal_filter(const TsPortalUi *ui, int row, TsPortalRecipe *recipe);
void ts_portal_wave_reset(TsPortalWave *wave, const TsSample *sample);
void ts_portal_wave_refresh(TsPortalWave *wave, const TsSample *sample);
size_t ts_portal_wave_frame(const TsPortalWave *wave, int x);
void ts_portal_wave_zoom(TsPortalWave *wave, const TsSample *sample, int x, int direction);
void ts_portal_wave_pan(TsPortalWave *wave, const TsSample *sample, int direction);

#endif
