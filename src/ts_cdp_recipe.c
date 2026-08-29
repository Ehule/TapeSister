#include "tapesister/cdp_recipe.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

_Static_assert(TS_CDP_FACTORY_RECIPE_COUNT <= TS_CDP_CATALOG_CAPACITY,
               "CDP catalog exceeds configured storage capacity");

/* PR-31 factory recipes are compiled data, not user-provided command text.  The
   four control positions stay stable in the UI while control_count lets a
   recipe honestly expose fewer controls. */
#define CONT(ID, LABEL, MIN, MAX, DEF, STEP, UNIT) \
    {ID, LABEL, TS_CDP_CONTROL_CONTINUOUS, MIN, MAX, DEF, STEP, NULL, 0u, NULL, UNIT}
#define BIP(ID, LABEL, MIN, MAX, DEF, STEP, UNIT) \
    {ID, LABEL, TS_CDP_CONTROL_BIPOLAR, MIN, MAX, DEF, STEP, NULL, 0u, NULL, UNIT}
#define STEP(ID, LABEL, MIN, MAX, DEF, AMOUNT, UNIT) \
    {ID, LABEL, TS_CDP_CONTROL_STEPPED, MIN, MAX, DEF, AMOUNT, NULL, 0u, NULL, UNIT}
#define ENUM(ID, LABEL, VALUES, NAMES, DEF) \
    {ID, LABEL, TS_CDP_CONTROL_ENUMERATED, VALUES[0], \
     VALUES[(sizeof(VALUES) / sizeof((VALUES)[0])) - 1u], DEF, 0.0f, VALUES, \
     sizeof(VALUES) / sizeof((VALUES)[0]), NAMES, ""}
#define DIRECT(ID, NAME, DESC, CATEGORY, BANK, SLOT, EXE, COUNT, CHANGE, SEED, OUTCH, ...) \
    {.id=ID, .display_name=NAME, .description=DESC, .category=CATEGORY, \
     .schema_version=2u, .recipe_version=1u, .default_enabled=1, \
     .bank=BANK, .slot=SLOT, \
     .control_count=COUNT, .stages={{EXE,TS_CDP_IO_WAV,TS_CDP_IO_WAV}}, \
     .stage_count=1u, .controls={__VA_ARGS__}, .mix_policy=(CHANGE) ? \
     TS_CDP_MIX_UNSUPPORTED : TS_CDP_MIX_EXACT_FRAMES, \
     .duration_may_change=CHANGE, .required_input_channels=1u, \
     .expected_output_channels=OUTCH, .preserve_sample_rate=1, \
     .safety_policy=TS_CDP_SAFETY_ANALYZE_ONLY, .seed_supported=(SEED)>0, \
     .deterministic=(SEED)>=0, .minimum_input_ms=40u, .provenance_version=2u}
#define SPECTRAL(ID, NAME, DESC, BANK, SLOT, EXE, COUNT, CHANGE, DET, ...) \
    {.id=ID, .display_name=NAME, .description=DESC, .category="SPECTRAL", \
     .schema_version=2u, .recipe_version=1u, .default_enabled=1, \
     .bank=BANK, .slot=SLOT, \
     .control_count=COUNT, .stages={{"pvoc",TS_CDP_IO_WAV,TS_CDP_IO_ANALYSIS}, \
     {EXE,TS_CDP_IO_ANALYSIS,TS_CDP_IO_ANALYSIS}, \
     {"pvoc",TS_CDP_IO_ANALYSIS,TS_CDP_IO_WAV}}, .stage_count=3u, \
     .controls={__VA_ARGS__}, .mix_policy=TS_CDP_MIX_UNSUPPORTED, \
     .duration_may_change=CHANGE, .required_input_channels=1u, \
     .expected_output_channels=1u, .preserve_sample_rate=1, \
     .safety_policy=TS_CDP_SAFETY_ANALYZE_ONLY, .seed_supported=0, \
     .deterministic=DET, .analysis_points=1024u, .analysis_overlap=3u, \
     .minimum_analysis_windows=8u, .minimum_input_ms=40u, .provenance_version=2u}

static const float modes_1_4[] = {1, 2, 3, 4};
static const char *const sorter_modes[] = {"CRESC", "DECRESC", "ACCEL", "RITARD"};
static const float grev_modes[] = {1, 2, 3, 4, 5};
static const char *const grev_names[] = {"REVERSE", "REPEAT", "DELETE", "OMIT", "STRETCH"};
static const float scrub_directions[] = {0, 1};
static const char *const scrub_names[] = {"FORWARD", "BOUNCE"};
static const float wave_modes[] = {1, 2, 3, 4};
static const char *const wave_names[] = {"RANDOM", "PERMUTE", "FALLING", "RISING"};
static const float reform_shapes[] = {2, 4, 6, 7};
static const char *const reform_names[] = {"SQUARE", "TRIANGLE", "CLICK", "SINE"};
static const float distshift_modes[] = {1, 2};
static const char *const distshift_names[] = {"SHIFT", "SWAP"};
static const float overload_modes[] = {1, 2};
static const char *const overload_names[] = {"CLIP", "PULSE"};
static const float filter_modes[] = {1, 2, 3};
static const char *const filter_names[] = {"HARMONIC", "ODD", "SUBHARM"};
static const float glisten_divisions[] = {2, 4, 8, 16, 32, 64};
static const float telescope_modes[] = {0, 1};
static const char *const telescope_names[] = {"LONGEST", "AVERAGE"};
static const float segzig_curves[] = {0, 1};
static const char *const segzig_names[] = {"LINEAR", "LOG"};
static const float shuffle_patterns[] = {0, 1, 2, 3};
static const char *const shuffle_names[] = {"SWAP", "ROTATE", "MIRROR", "STUTTER"};

static const TsCdpRecipe factory_recipes[TS_CDP_FACTORY_RECIPE_COUNT] = {
    DIRECT("drunk", "DRUNK", "A wandering playhead staggers through the sound", "TIME", 0, 0,
           "extend", 4, 1, 1, 1,
           CONT("position","POSITION",0,1,.5f,.01f,"%"), CONT("range","RANGE",.02f,1,.35f,.01f,"%"),
           CONT("step","STEP",.01f,1,.2f,.01f,"%"), CONT("clock","CLOCK",.05f,2,.25f,.01f,"SEC")),
    DIRECT("shred", "SHRED", "Repeated chunks are cut and scattered", "TIME", 0, 1,
           "modify", 3, 0, -1, 1,
           STEP("passes","PASSES",1,64,8,1,""), CONT("chunk","CHUNK",.005f,.25f,.04f,.005f,"SEC"),
           CONT("scatter","SCATTER",0,1,.35f,.01f,"%")),
    DIRECT("hover", "HOVER", "A small region hovers and wanders", "TIME", 0, 2,
           "hover", 4, 1, -1, 1,
           CONT("position","POSITION",0,1,.5f,.01f,"%"), CONT("width","WIDTH",0,1,.18f,.01f,"%"),
           CONT("rate","RATE",.1f,8,1,.05f,"HZ"), CONT("wander","WANDER",0,1,.12f,.01f,"%")),
    DIRECT("scrub", "SCRUB", "Variable-speed scrubbing stretches and bends", "TIME", 0, 3,
           "modify", 4, 1, -1, 1,
           CONT("length","LENGTH",.25f,4,1,.05f,"X"), BIP("down","DOWN",-36,0,-7,.5f,"ST"),
           CONT("up","UP",0,36,7,.5f,"ST"), ENUM("direction","DIRECTION",scrub_directions,scrub_names,0)),
    DIRECT("zigzag", "ZIGZAG", "The playhead darts back and forth", "TIME", 0, 4,
           "extend", 4, 1, 1, 1,
           CONT("start","START",0,.9f,.15f,.01f,"%"), CONT("span","SPAN",.05f,1,.7f,.01f,"%"),
           CONT("length","LENGTH",.25f,4,1.5f,.05f,"X"), CONT("grain","GRAIN",.05f,1,.16f,.01f,"SEC")),
    DIRECT("stutter", "STUTTER", "Marked fragments repeat with gaps and pitch motion", "FRAGMENT", 0, 5,
           "stutter", 4, 1, 1, 1,
           CONT("chunk","CHUNK",.02f,.5f,.11f,.01f,"SEC"), CONT("repeat","REPEAT",.5f,4,1.5f,.05f,"X"),
           CONT("pitch","PITCH",0,12,2,.25f,"ST"), CONT("gap","GAP",0,.8f,.15f,.01f,"%")),
    DIRECT("sorter", "SORTER", "Events are reordered by pitch contour", "STRUCTURE", 0, 6,
           "sorter", 3, 0, 0, 1,
           ENUM("mode","ORDER",modes_1_4,sorter_modes,1), CONT("event","EVENT",.02f,.5f,.1f,.01f,"SEC"),
           CONT("smooth","SMOOTH",0,50,10,1,"MS")),
    DIRECT("splinter", "SPLINTER", "A target instant fractures into accelerating shards", "FRAGMENT", 0, 7,
           "splinter", 4, 1, -1, 1,
           CONT("target","TARGET",.05f,.95f,.5f,.01f,"%"), STEP("waves","WAVES",2,64,12,1,""),
           CONT("pitch","PITCH",200,6000,6000,50,"HZ"), CONT("rate","RATE",0,40,8,.5f,"HZ")),
    DIRECT("scramble_ext", "SCRAMBLE", "Segments are rearranged into a new duration", "FRAGMENT", 0, 8,
           "extend", 3, 1, 1, 1,
           CONT("minimum","MIN SIZE",.01f,.5f,.06f,.005f,"SEC"), CONT("maximum","MAX SIZE",.02f,1,.22f,.01f,"SEC"),
           CONT("length","LENGTH",.25f,4,1,.05f,"X")),
    DIRECT("doublets", "DOUBLETS", "Short phrases repeat in paired clusters", "FRAGMENT", 0, 9,
           "extend", 2, 1, 0, 1,
           CONT("segment","SEGMENT",.01f,1,.1f,.01f,"SEC"), STEP("repeats","REPEATS",2,32,4,1,"")),
    DIRECT("motor", "MOTOR", "Pulsed inner and outer rates make a sound engine", "RHYTHM", 0, 10,
           "motor", 4, 1, 1, 1,
           CONT("inner","INNER",2,100,8,.25f,"HZ"), CONT("outer","OUTER",.1f,10,2,.1f,"HZ"),
           CONT("size","INNER SIZE",.05f,1,.5f,.01f,"%"), CONT("variation","VARIATION",0,1,.15f,.01f,"%")),
    DIRECT("grev", "GREV", "Grain groups reverse repeat omit or stretch", "GRAIN", 0, 11,
           "grain", 4, 1, 0, 1,
           ENUM("mode","MODE",grev_modes,grev_names,1), CONT("window","WINDOW",5,200,35,1,"MS"),
           STEP("group","GROUP",1,32,1,1,""), CONT("trough","TROUGH",.05f,.95f,.5f,.01f,"%")),
    DIRECT("timewarp", "TIMEWARP", "Granular time expands or contracts", "GRAIN", 0, 12,
           "grain", 4, 1, 0, 1,
           CONT("ratio","RATIO",.125f,8,1.5f,.025f,"X"), CONT("buffer","BUFFER",.1f,.25f,.1f,.005f,"SEC"),
           CONT("gate","GATE",0,1,.05f,.01f,"%"), CONT("hole","HOLE",.032f,.2f,.032f,.001f,"SEC")),
    DIRECT("telescope", "TELESCOPE", "Wavecycle groups collapse into one contour", "WAVESET", 0, 13,
           "distort", 3, 1, 0, 1,
           STEP("group","GROUP",2,128,16,1,""), STEP("skip","SKIP",0,256,0,1,"CYC"),
           ENUM("mode","SHAPE",telescope_modes,telescope_names,0)),
    DIRECT("freeze", "FREEZE", "A source window is held and scattered", "FRAGMENT", 0, 14,
           "extend", 4, 1, 1, 1,
           CONT("position","POSITION",0,.95f,.4f,.01f,"%"), CONT("size","SIZE",.05f,.5f,.12f,.01f,"SEC"),
           STEP("repeats","REPEATS",2,64,12,1,""), CONT("drift","DRIFT",0,1,.1f,.01f,"%")),
    DIRECT("iterate", "ITERATE", "The whole gesture repeats with pitch and decay", "TIME", 0, 15,
           "extend", 4, 1, 1, 1,
           STEP("repeats","REPEATS",2,32,5,1,""), CONT("gap","GAP",0,2,.08f,.01f,"SEC"),
           CONT("pitch","PITCH",0,12,1,.25f,"ST"), CONT("fade","FADE",0,1,.15f,.01f,"%")),

    SPECTRAL("glisten", "GLISTEN", "Spectral groups shimmer in a changing sequence", 1, 0,
             "glisten", 4, 1, 0,
             ENUM("divide","DIVIDE",glisten_divisions,NULL,8), STEP("hold","HOLD",1,128,8,1,"WINDOWS"),
             CONT("shift","SHIFT",0,12,3,.25f,"ST"), CONT("scatter","SCATTER",0,1,.28f,.01f,"%")),
    SPECTRAL("spec_smear", "SPEC SMEAR", "Spectral frames blur through time", 1, 1,
             "blur", 1, 0, 1, STEP("blur","BLUR",1,128,8,1,"WINDOWS")),
    DIRECT("wave_scramble", "WAVE SCRAMBLE", "Waveset segments are cut and reordered", "WAVESET", 1, 2,
           "scramble", 4, 1, 1, 1,
           ENUM("mode","MODE",wave_modes,wave_names,1), STEP("groups","GROUP",1,64,8,1,""),
           CONT("pitch","PITCH",0,12,2,.25f,"ST"), CONT("atten","DECAY",0,1,.12f,.01f,"%")),
    DIRECT("brassage", "BRASSAGE", "Clouds of grains move in pitch and stereo space", "GRAIN", 1, 3,
           "modify", 4, 1, -1, 2,
           /* Mode 6 uses fixed 10 ms start/end splices, so grains must stay
              longer than their combined 20 ms boundary treatment. */
           CONT("grain","GRAIN",.025f,.2f,.05f,.005f,"SEC"), CONT("density","DENSITY",.2f,8,2,.05f,"X"),
           BIP("pitch","PITCH",-24,24,0,.25f,"ST"), CONT("spread","SPREAD",0,1,.5f,.01f,"%")),
    DIRECT("fractal", "FRACTAL", "Miniature wavecycles add a spectral sheen", "WAVESET", 1, 4,
           "distort", 2, 0, 0, 1,
           STEP("scale","SCALE",2,64,4,1,"X"), CONT("sheen","SHEEN",.01f,1,.35f,.01f,"%")),
    DIRECT("interpolate", "INTERPOLATE", "Interpolated wavecycles stretch fluidly", "WAVESET", 1, 5,
           "distort", 2, 1, 0, 1,
           STEP("multiplier","STRETCH",2,64,4,1,"X"), STEP("skip","SKIP",0,256,0,1,"CYC")),
    DIRECT("omit", "OMIT", "Selected wavecycles are rhythmically omitted", "WAVESET", 1, 6,
           "distort", 2, 0, 0, 1,
           STEP("omit","OMIT",1,63,3,1,""), STEP("group","GROUP",2,64,8,1,"")),
    DIRECT("replace", "REPLACE", "Wavecycle groups replace their neighbors", "WAVESET", 1, 7,
           "distort", 2, 0, 0, 1,
           STEP("group","GROUP",2,64,8,1,"CYC"), STEP("skip","SKIP",0,256,0,1,"CYC")),
    DIRECT("pitch", "PITCH", "Wavecycle pitch varies within a random range", "WAVESET", 1, 8,
           "distort", 3, 1, -1, 1,
           CONT("range","RANGE",0,4,.5f,.05f,"OCT"), STEP("group","GROUP",1,64,4,1,"CYC"),
           STEP("skip","SKIP",0,256,0,1,"CYC")),
    DIRECT("shuffle", "SHUFFLE", "Wavecycle groups follow a curated permutation", "WAVESET", 1, 9,
           "distort", 3, 1, 0, 1,
           ENUM("pattern","PATTERN",shuffle_patterns,shuffle_names,0), STEP("group","GROUP",1,64,4,1,"CYC"),
           STEP("skip","SKIP",0,256,0,1,"CYC")),
    DIRECT("reform", "REFORM", "Wavecycles are reshaped to a chosen contour", "WAVESET", 1, 10,
           "distort", 1, 0, 0, 1, ENUM("shape","SHAPE",reform_shapes,reform_names,7)),
    DIRECT("distshift", "DISTSHIFT", "Wavecycle groups shift or scatter in time", "WAVESET", 1, 11,
           "distshift", 3, 0, 0, 1,
           ENUM("mode","MODE",distshift_modes,distshift_names,1), STEP("group","GROUP",1,128,12,1,"CYC"),
           STEP("shift","SHIFT",1,64,4,1,"CYC")),
    DIRECT("segzig", "SEGZIG", "The full segment zigzags and contracts", "STRUCTURE", 1, 12,
           "distmore", 4, 1, 0, 1,
           STEP("repeats","REPEATS",1,64,8,1,""), CONT("shrink","SHRINK",0,.5f,.08f,.01f,"SEC"),
           CONT("portion","PORTION",.05f,1,.75f,.01f,"%"), ENUM("curve","CURVE",segzig_curves,segzig_names,0)),
    DIRECT("overload", "OVERLOAD", "Controlled overload adds hard rhythmic energy", "DISTORT", 1, 13,
           "distort", 4, 0, 0, 1,
           ENUM("mode","MODE",overload_modes,overload_names,1), CONT("threshold","THRESH",.01f,1,.65f,.01f,"%"),
           CONT("depth","DEPTH",.01f,1,.55f,.01f,"%"), CONT("frequency","FREQ",1,200,30,1,"HZ")),
    DIRECT("filter_bank", "FILTER BANK", "A tuned resonant bank follows the sample root", "FILTER", 1, 14,
           "filter", 4, 1, 0, 1,
           ENUM("structure","STRUCTURE",filter_modes,filter_names,1), CONT("width","WIDTH",1,6,3,.1f,"OCT"),
           CONT("q","Q",1,9990,400,10,""), CONT("strength","STRENGTH",.02f,8,1.2f,.02f,"X")),
    DIRECT("granulate", "GRANULATE", "A compact grain cloud changes density", "GRAIN", 1, 15,
           "modify", 1, 1, 0, 1, CONT("density","DENSITY",.01f,2,.6f,.01f,"X"))
};

static void set_error(char *error, size_t error_size, const char *message)
{
    if (error != NULL && error_size > 0u)
        snprintf(error, error_size, "%s", message != NULL ? message : "");
}

static float clampf(float value, float minimum, float maximum)
{
    return value < minimum ? minimum : value > maximum ? maximum : value;
}

size_t ts_cdp_factory_recipe_count(void) { return TS_CDP_FACTORY_RECIPE_COUNT; }

const TsCdpRecipe *ts_cdp_factory_recipe_at(size_t index)
{
    return index < TS_CDP_FACTORY_RECIPE_COUNT ? &factory_recipes[index] : NULL;
}

const TsCdpRecipe *ts_cdp_factory_recipe_for_slot(size_t bank, size_t slot)
{
    return bank < TS_CDP_BANK_COUNT && slot < TS_CDP_BANK_SLOT_COUNT ?
           &factory_recipes[bank * TS_CDP_BANK_SLOT_COUNT + slot] : NULL;
}

const TsCdpRecipe *ts_cdp_recipe_find(const char *id)
{
    if (id == NULL) return NULL;
    for (size_t i = 0; i < TS_CDP_FACTORY_RECIPE_COUNT; ++i)
        if (strcmp(factory_recipes[i].id, id) == 0) return &factory_recipes[i];
    return NULL;
}

int ts_cdp_recipe_index_for_id(const char *id)
{
    if (id == NULL) return -1;
    for (size_t i = 0; i < TS_CDP_FACTORY_RECIPE_COUNT; ++i)
        if (strcmp(factory_recipes[i].id, id) == 0) return (int)i;
    return -1;
}

void ts_cdp_catalog_view_build(TsCdpCatalogView *view,
                               const int *enabled, size_t enabled_count)
{
    size_t count;
    if (view == NULL) return;
    memset(view, 0, sizeof(*view));
    for (size_t i = 0; i < TS_CDP_VISIBLE_RECIPE_COUNT; ++i)
        view->recipe_indices[i] = UINT16_MAX;
    count = ts_cdp_factory_recipe_count();
    if (count > TS_CDP_CATALOG_CAPACITY) count = TS_CDP_CATALOG_CAPACITY;
    for (size_t i = 0; i < count; ++i) {
        int selected = enabled != NULL && i < enabled_count ?
                       enabled[i] : factory_recipes[i].default_enabled;
        if (!selected) continue;
        if (view->visible_count < TS_CDP_VISIBLE_RECIPE_COUNT)
            view->recipe_indices[view->visible_count++] = (uint16_t)i;
        ++view->enabled_count;
    }
    view->truncated = view->enabled_count > view->visible_count;
}

int ts_cdp_catalog_index_for_slot(const TsCdpCatalogView *view,
                                  size_t bank, size_t slot)
{
    size_t visible_index;
    uint16_t recipe_index;
    if (view == NULL || bank >= TS_CDP_VISIBLE_BANK_COUNT ||
        slot >= TS_CDP_VISIBLE_BANK_SLOT_COUNT) return -1;
    visible_index = bank * TS_CDP_VISIBLE_BANK_SLOT_COUNT + slot;
    recipe_index = view->recipe_indices[visible_index];
    return recipe_index == UINT16_MAX ? -1 : (int)recipe_index;
}

const TsCdpRecipe *ts_cdp_catalog_recipe_for_slot(const TsCdpCatalogView *view,
                                                  size_t bank, size_t slot)
{
    int index = ts_cdp_catalog_index_for_slot(view, bank, slot);
    return index < 0 ? NULL : ts_cdp_factory_recipe_at((size_t)index);
}

int ts_cdp_recipe_validate(const TsCdpRecipe *recipe, char *error, size_t error_size)
{
    if (recipe == NULL || recipe->id == NULL || recipe->id[0] == '\0' ||
        recipe->display_name == NULL || recipe->schema_version == 0u ||
        recipe->recipe_version == 0u || recipe->provenance_version == 0u ||
        (recipe->default_enabled != 0 && recipe->default_enabled != 1)) {
        set_error(error, error_size, "Recipe identity, default, or version is invalid");
        return 0;
    }
    if (recipe->control_count == 0u || recipe->control_count > TS_CDP_CONTROL_COUNT ||
        recipe->stage_count == 0u || recipe->stage_count > TS_CDP_MAX_STAGES) {
        set_error(error, error_size, "Recipe control or stage count is invalid");
        return 0;
    }
    if (recipe->stages[0].input_type != TS_CDP_IO_WAV ||
        recipe->stages[recipe->stage_count - 1u].output_type != TS_CDP_IO_WAV) {
        set_error(error, error_size, "Recipe must begin and end with WAV audio");
        return 0;
    }
    for (size_t i = 0; i < recipe->stage_count; ++i) {
        if (recipe->stages[i].executable == NULL || recipe->stages[i].executable[0] == '\0' ||
            strlen(recipe->stages[i].executable) >= TS_CDP_TEXT_MAX ||
            strchr(recipe->stages[i].executable, '/') != NULL ||
            strchr(recipe->stages[i].executable, '\\') != NULL ||
            strstr(recipe->stages[i].executable, "..") != NULL ||
            (i > 0u && recipe->stages[i - 1u].output_type != recipe->stages[i].input_type)) {
            set_error(error, error_size, "Recipe stage chain is invalid");
            return 0;
        }
    }
    for (size_t i = 0; i < recipe->control_count; ++i) {
        const TsCdpControlSpec *control = &recipe->controls[i];
        if (control->id == NULL || control->label == NULL ||
            !isfinite(control->minimum) || !isfinite(control->maximum) ||
            !isfinite(control->default_value) || control->minimum > control->maximum ||
            control->default_value < control->minimum || control->default_value > control->maximum ||
            (control->type == TS_CDP_CONTROL_ENUMERATED &&
             (control->valid_values == NULL || control->valid_value_count == 0u)) ||
            (control->type == TS_CDP_CONTROL_STEPPED && control->step <= 0.0f)) {
            set_error(error, error_size, "Recipe control definition is invalid");
            return 0;
        }
        if (control->type == TS_CDP_CONTROL_ENUMERATED) {
            for (size_t value = 0; value < control->valid_value_count; ++value) {
                if (!isfinite(control->valid_values[value]) ||
                    (value > 0u && control->valid_values[value] <=
                                   control->valid_values[value - 1u])) {
                    set_error(error, error_size,
                              "Recipe enumerated values must be finite and ordered");
                    return 0;
                }
            }
        }
    }
    if (recipe->duration_may_change && recipe->mix_policy == TS_CDP_MIX_EXACT_FRAMES) {
        set_error(error, error_size, "Duration-changing recipe needs an explicit MIX policy");
        return 0;
    }
    if (recipe->required_input_channels != 1u ||
        (recipe->expected_output_channels != 1u && recipe->expected_output_channels != 2u) ||
        !recipe->preserve_sample_rate || recipe->safety_policy != TS_CDP_SAFETY_ANALYZE_ONLY) {
        set_error(error, error_size, "Factory CDP recipe has unsupported audio properties");
        return 0;
    }
    set_error(error, error_size, "");
    return 1;
}

void ts_cdp_recipe_values_default(const TsCdpRecipe *recipe, TsCdpRecipeValues *values)
{
    if (values == NULL) return;
    memset(values, 0, sizeof(*values));
    if (recipe == NULL) return;
    for (size_t i = 0; i < recipe->control_count; ++i)
        values->controls[i] = recipe->controls[i].default_value;
    /* Wet is the safest musical default for an offline transform.  It also
       means an otherwise usable process is not rejected merely because CDP
       emitted a natural one-frame length difference. */
    values->mix = 1.0f;
    values->seed = 1u;
    values->tuning_hz = 261.625565f;
}

float ts_cdp_control_quantize(const TsCdpControlSpec *control, float value)
{
    if (control == NULL || !isfinite(value))
        return control != NULL ? control->default_value : 0.0f;
    value = clampf(value, control->minimum, control->maximum);
    if (control->type == TS_CDP_CONTROL_ENUMERATED) {
        float closest = control->valid_values[0];
        float distance = fabsf(value - closest);
        for (size_t i = 1; i < control->valid_value_count; ++i) {
            float candidate_distance = fabsf(value - control->valid_values[i]);
            if (candidate_distance < distance) {
                closest = control->valid_values[i];
                distance = candidate_distance;
            }
        }
        return closest;
    }
    if (control->step > 0.0f) {
        value = control->minimum + roundf((value - control->minimum) / control->step) * control->step;
        value = clampf(value, control->minimum, control->maximum);
    }
    return value;
}

int ts_cdp_recipe_set_control(const TsCdpRecipe *recipe, TsCdpRecipeValues *values,
                              size_t index, float value)
{
    if (recipe == NULL || values == NULL || index >= recipe->control_count) return 0;
    values->controls[index] = ts_cdp_control_quantize(&recipe->controls[index], value);
    return 1;
}

void ts_cdp_control_format(const TsCdpControlSpec *control, float value,
                           uint32_t sample_rate, uint32_t analysis_points,
                           uint32_t analysis_overlap, char *text, size_t text_size)
{
    float actual;
    if (text == NULL || text_size == 0u) return;
    if (control == NULL) { snprintf(text, text_size, "-"); return; }
    actual = ts_cdp_control_quantize(control, value);
    if (control->type == TS_CDP_CONTROL_ENUMERATED && control->value_names != NULL) {
        size_t at = 0u;
        for (size_t i = 0; i < control->valid_value_count; ++i)
            if (control->valid_values[i] == actual) at = i;
        snprintf(text, text_size, "%s", control->value_names[at]);
    } else if (strcmp(control->id, "divide") == 0) {
        snprintf(text, text_size, "%d GROUPS", (int)lrintf(actual));
    } else if (strcmp(control->id, "hold") == 0 && sample_rate > 0u &&
               analysis_points > 0u && analysis_overlap > 0u) {
        uint32_t divisor = analysis_overlap == 1u ? 1u : analysis_overlap == 2u ? 2u :
                           analysis_overlap == 3u ? 4u : 8u;
        double ms = actual * (double)analysis_points * 1000.0 /
                    ((double)sample_rate * divisor);
        snprintf(text, text_size, "%d / %.0fMS", (int)lrintf(actual), ms);
    } else if (strcmp(control->unit != NULL ? control->unit : "", "%") == 0 &&
               control->maximum <= 1.0f && control->minimum >= 0.0f) {
        snprintf(text, text_size, "%d%%", (int)lrintf(actual * 100.0f));
    } else if (control->type == TS_CDP_CONTROL_ENUMERATED ||
               control->type == TS_CDP_CONTROL_STEPPED) {
        snprintf(text, text_size, "%d %s", (int)lrintf(actual),
                 control->unit != NULL ? control->unit : "");
    } else {
        snprintf(text, text_size, "%.2f %s", actual,
                 control->unit != NULL ? control->unit : "");
    }
}

int ts_cdp_glisten_map(const TsCdpRecipe *recipe, const TsCdpRecipeValues *values,
                       TsCdpGlistenMapping *mapping, char *error, size_t error_size)
{
    TsCdpRecipeValues safe;
    float scatter;
    if (recipe == NULL || values == NULL || mapping == NULL || strcmp(recipe->id,"glisten") != 0) {
        set_error(error, error_size, "GLISTEN mapping needs the GLISTEN recipe");
        return 0;
    }
    safe = *values;
    for (size_t i = 0; i < recipe->control_count; ++i)
        safe.controls[i] = ts_cdp_control_quantize(&recipe->controls[i], safe.controls[i]);
    scatter = safe.controls[3];
    mapping->divide = (int)lrintf(safe.controls[0]);
    mapping->hold_windows = (int)lrintf(safe.controls[1]);
    mapping->shift_semitones = safe.controls[2];
    mapping->duration_randomization = scatter;
    mapping->division_randomization = scatter * scatter;
    set_error(error, error_size, "");
    return 1;
}

static void command_begin(TsCdpCommand *command, const char *executable,
                          const char *output, TsCdpIoType output_type)
{
    memset(command, 0, sizeof(*command));
    snprintf(command->executable, sizeof(command->executable), "%s", executable);
    snprintf(command->expected_output, sizeof(command->expected_output), "%s", output);
    command->expected_output_type = output_type;
}

static int command_arg(TsCdpCommand *command, const char *format, ...)
{
    va_list args;
    int written;
    if (command->argc >= TS_CDP_MAX_COMMAND_ARGS) return 0;
    va_start(args, format);
    written = vsnprintf(command->arguments[command->argc], TS_CDP_TEXT_MAX, format, args);
    va_end(args);
    if (written < 0 || written >= TS_CDP_TEXT_MAX) return 0;
    ++command->argc;
    return 1;
}

static void build_pvoc_analysis(const TsCdpRecipe *recipe, TsCdpCommand *command)
{
    command_begin(command, "pvoc", "input.ana", TS_CDP_IO_ANALYSIS);
    command_arg(command,"anal"); command_arg(command,"1"); command_arg(command,"input.wav");
    command_arg(command,"input.ana"); command_arg(command,"-c%u",recipe->analysis_points);
    command_arg(command,"-o%u",recipe->analysis_overlap);
}

static void build_pvoc_synthesis(TsCdpCommand *command, const char *analysis)
{
    command_begin(command, "pvoc", "output.wav", TS_CDP_IO_WAV);
    command_arg(command,"synth"); command_arg(command,"%s",analysis); command_arg(command,"output.wav");
}

int ts_cdp_glisten_build_commands(const TsCdpRecipe *recipe,
                                  const TsCdpRecipeValues *values,
                                  TsCdpCommand commands[3], char *error, size_t error_size)
{
    TsCdpGlistenMapping mapped;
    if (commands == NULL || !ts_cdp_glisten_map(recipe,values,&mapped,error,error_size)) return 0;
    build_pvoc_analysis(recipe,&commands[0]);
    command_begin(&commands[1],"glisten","effect.ana",TS_CDP_IO_ANALYSIS);
    command_arg(&commands[1],"glisten"); command_arg(&commands[1],"input.ana");
    command_arg(&commands[1],"effect.ana"); command_arg(&commands[1],"%d",mapped.divide);
    command_arg(&commands[1],"%d",mapped.hold_windows);
    command_arg(&commands[1],"-p%.6g",mapped.shift_semitones);
    command_arg(&commands[1],"-d%.6g",mapped.duration_randomization);
    command_arg(&commands[1],"-v%.6g",mapped.division_randomization);
    build_pvoc_synthesis(&commands[2],"effect.ana");
    set_error(error,error_size,"");
    return 1;
}

static int id_is(const TsCdpRecipe *recipe, const char *id) { return strcmp(recipe->id,id)==0; }

static unsigned long long mapped_seed(uint64_t seed, uint64_t maximum)
{
    if (seed == 0u) seed = 1u;
    return (unsigned long long)(((seed - 1u) % maximum) + 1u);
}

int ts_cdp_recipe_build_commands(const TsCdpRecipe *recipe,
                                 const TsCdpRecipeValues *values,
                                 size_t input_frames, uint32_t sample_rate,
                                 TsCdpCommand commands[TS_CDP_MAX_STAGES],
                                 size_t *command_count, char *error, size_t error_size)
{
    TsCdpRecipeValues v;
    double duration;
    int mode;
    const char *pattern;
#define A(CMD, ...) do { if (!command_arg((CMD), __VA_ARGS__)) goto too_many; } while (0)
    if (command_count != NULL) *command_count = 0u;
    if (recipe == NULL || values == NULL || commands == NULL || command_count == NULL ||
        !ts_cdp_recipe_validate(recipe,error,error_size) || input_frames == 0u || sample_rate == 0u)
        return 0;
    v = *values;
    for (size_t i=0;i<recipe->control_count;++i)
        v.controls[i]=ts_cdp_control_quantize(&recipe->controls[i],v.controls[i]);
    duration=(double)input_frames/(double)sample_rate;
    memset(commands,0,TS_CDP_MAX_STAGES*sizeof(*commands));
    *command_count=recipe->stage_count;
    if (id_is(recipe,"glisten"))
        return ts_cdp_glisten_build_commands(recipe,&v,commands,error,error_size);
    if (id_is(recipe,"spec_smear")) {
        size_t hop = recipe->analysis_points / 4u;
        size_t windows = input_frames > recipe->analysis_points ?
                         1u + (input_frames - recipe->analysis_points) / hop : 1u;
        int blur = (int)lrintf(v.controls[0]);
        if ((size_t)blur > windows) blur = (int)windows;
        if (blur < 1) blur = 1;
        build_pvoc_analysis(recipe,&commands[0]);
        command_begin(&commands[1],"blur","effect.ana",TS_CDP_IO_ANALYSIS);
        A(&commands[1],"blur"); A(&commands[1],"input.ana"); A(&commands[1],"effect.ana");
        A(&commands[1],"%d",blur);
        build_pvoc_synthesis(&commands[2],"effect.ana");
        set_error(error,error_size,""); return 1;
    }
    command_begin(&commands[0],recipe->stages[0].executable,"output.wav",TS_CDP_IO_WAV);
    if (id_is(recipe,"drunk")) {
        A(&commands[0],"drunk"); A(&commands[0],"1"); A(&commands[0],"input.wav"); A(&commands[0],"output.wav");
        A(&commands[0],"%.6g",duration); A(&commands[0],"%.6g",v.controls[0]*duration);
        A(&commands[0],"%.6g",fmax(.01,v.controls[1]*duration));
        A(&commands[0],"%.6g",fmax(.001,v.controls[2]*v.controls[1]*duration)); A(&commands[0],"%.6g",v.controls[3]);
        A(&commands[0],"-s10"); A(&commands[0],"-c%.6g",v.controls[2]); A(&commands[0],"-o%.6g",v.controls[1]);
        A(&commands[0],"-r%llu",mapped_seed(v.seed,32767u));
    } else if (id_is(recipe,"shred")) {
        A(&commands[0],"radical"); A(&commands[0],"2"); A(&commands[0],"input.wav"); A(&commands[0],"output.wav");
        A(&commands[0],"%d",(int)lrintf(v.controls[0])); A(&commands[0],"%.6g",v.controls[1]);
        A(&commands[0],"-s%.6g",v.controls[2]);
    } else if (id_is(recipe,"hover")) {
        double frequency=fmax(v.controls[2],1.0/(duration*2.0));
        A(&commands[0],"hover"); A(&commands[0],"input.wav"); A(&commands[0],"output.wav");
        A(&commands[0],"%.6g",frequency); A(&commands[0],"%.6g",v.controls[0]*duration);
        A(&commands[0],"%.6g",v.controls[3]); A(&commands[0],"%.6g",v.controls[1]*duration*.5);
        A(&commands[0],"10"); A(&commands[0],"%.6g",duration);
    } else if (id_is(recipe,"scrub")) {
        A(&commands[0],"radical"); A(&commands[0],"3"); A(&commands[0],"input.wav"); A(&commands[0],"output.wav");
        A(&commands[0],"%.6g",duration*v.controls[0]); A(&commands[0],"-l%.6g",v.controls[1]);
        A(&commands[0],"-h%.6g",v.controls[2]); A(&commands[0],"-s0"); A(&commands[0],"-e%.6g",duration);
        if (v.controls[3] > .5f) A(&commands[0],"-f");
    } else if (id_is(recipe,"zigzag")) {
        double start=v.controls[0]*duration, end=fmin(duration,start+v.controls[1]*duration);
        if (end-start<.05) end=fmin(duration,start+.05);
        A(&commands[0],"zigzag"); A(&commands[0],"1"); A(&commands[0],"input.wav"); A(&commands[0],"output.wav");
        A(&commands[0],"%.6g",start); A(&commands[0],"%.6g",end); A(&commands[0],"%.6g",duration*v.controls[2]);
        A(&commands[0],"%.6g",v.controls[3]); A(&commands[0],"-s10"); A(&commands[0],"-m%.6g",fmin(1.0,v.controls[3]*4));
        A(&commands[0],"-r%llu",mapped_seed(v.seed,32767u));
    } else if (id_is(recipe,"stutter")) {
        A(&commands[0],"stutter"); A(&commands[0],"input.wav"); A(&commands[0],"output.wav"); A(&commands[0],"stutter.txt");
        A(&commands[0],"%.6g",duration*v.controls[1]); A(&commands[0],"2"); A(&commands[0],"%.6g",v.controls[3]);
        A(&commands[0],"0.01"); A(&commands[0],"0.12"); A(&commands[0],"%llu",mapped_seed(v.seed,256u));
        A(&commands[0],"-t%.6g",v.controls[2]); A(&commands[0],"-a0.15");
    } else if (id_is(recipe,"sorter")) {
        A(&commands[0],"sorter"); A(&commands[0],"%d",(int)lrintf(v.controls[0]));
        A(&commands[0],"input.wav"); A(&commands[0],"output.wav"); A(&commands[0],"%.6g",v.controls[1]);
        A(&commands[0],"-s%.6g",v.controls[2]);
    } else if (id_is(recipe,"splinter")) {
        int waves=(int)lrintf(v.controls[1]);
        double pitch=fmin(v.controls[2],(double)sample_rate*.2375);
        A(&commands[0],"splinter"); A(&commands[0],"1"); A(&commands[0],"input.wav"); A(&commands[0],"output.wav");
        A(&commands[0],"%.6g",v.controls[0]*duration); A(&commands[0],"%d",waves); A(&commands[0],"8");
        A(&commands[0],"8"); A(&commands[0],"0"); A(&commands[0],"%.6g",v.controls[3]);
        A(&commands[0],"-f%.6g",pitch); A(&commands[0],"-r0.12");
    } else if (id_is(recipe,"scramble_ext")) {
        double lo=fmin(v.controls[0],v.controls[1]), hi=fmax(v.controls[0],v.controls[1]);
        A(&commands[0],"scramble"); A(&commands[0],"1"); A(&commands[0],"input.wav"); A(&commands[0],"output.wav");
        A(&commands[0],"%.6g",lo); A(&commands[0],"%.6g",hi); A(&commands[0],"%.6g",duration*v.controls[2]);
        A(&commands[0],"-w10"); A(&commands[0],"-s%llu",mapped_seed(v.seed,32767u));
    } else if (id_is(recipe,"doublets")) {
        A(&commands[0],"doublets"); A(&commands[0],"input.wav"); A(&commands[0],"output.wav");
        A(&commands[0],"%.6g",v.controls[0]); A(&commands[0],"%d",(int)lrintf(v.controls[1])); A(&commands[0],"-s");
    } else if (id_is(recipe,"motor")) {
        A(&commands[0],"motor"); A(&commands[0],"1"); A(&commands[0],"input.wav"); A(&commands[0],"output.wav");
        A(&commands[0],"%.6g",duration); A(&commands[0],"%.6g",v.controls[0]); A(&commands[0],"%.6g",v.controls[1]);
        A(&commands[0],"%.6g",v.controls[2]); A(&commands[0],"1"); A(&commands[0],".5");
        A(&commands[0],"-f%.6g",v.controls[3]); A(&commands[0],"-p%.6g",v.controls[3]);
        A(&commands[0],"-j%.6g",v.controls[3]); A(&commands[0],"-s%llu",mapped_seed(v.seed,256u));
    } else if (id_is(recipe,"grev")) {
        double window=fmin(v.controls[1],duration*1000.0/3.0);
        mode=(int)lrintf(v.controls[0]);
        A(&commands[0],"grev"); A(&commands[0],"%d",mode); A(&commands[0],"input.wav"); A(&commands[0],"output.wav");
        A(&commands[0],"%.6g",window); A(&commands[0],"%.6g",v.controls[3]); A(&commands[0],"%d",(int)lrintf(v.controls[2]));
        if (mode==2) A(&commands[0],"2");
        else if (mode==3 || mode==4) { A(&commands[0],"1"); A(&commands[0],"3"); }
        else if (mode==5) A(&commands[0],"2");
    } else if (id_is(recipe,"timewarp")) {
        /* CDP derives both upper bounds from the current input duration.
           GR_BLEN is at least 100 ms unless the complete source is shorter;
           GR_MINTIME is fixed at two 15 ms splices plus 2 ms safety. */
        double buffer=fmin(duration,fmax(fmin(duration,.1),v.controls[1]));
        double hole=fmin(duration,fmax(.032,v.controls[3]));
        A(&commands[0],"timewarp"); A(&commands[0],"input.wav"); A(&commands[0],"output.wav"); A(&commands[0],"%.6g",v.controls[0]);
        A(&commands[0],"-b%.6g",buffer); A(&commands[0],"-l%.6g",v.controls[2]); A(&commands[0],"-h%.6g",hole);
    } else if (id_is(recipe,"telescope")) {
        A(&commands[0],"telescope"); A(&commands[0],"input.wav"); A(&commands[0],"output.wav");
        A(&commands[0],"%d",(int)lrintf(v.controls[0])); A(&commands[0],"-s%d",(int)lrintf(v.controls[1]));
        if (v.controls[2]>.5f) A(&commands[0],"-a");
    } else if (id_is(recipe,"freeze")) {
        double start=v.controls[0]*duration, end=fmin(duration,start+v.controls[1]);
        A(&commands[0],"freeze"); A(&commands[0],"2"); A(&commands[0],"input.wav"); A(&commands[0],"output.wav");
        A(&commands[0],"%d",(int)lrintf(v.controls[2])); A(&commands[0],"%.6g",fmax(.001,end-start)); A(&commands[0],"%.6g",v.controls[3]);
        A(&commands[0],"%.6g",v.controls[3]*12); A(&commands[0],".2"); A(&commands[0],"%.6g",start); A(&commands[0],"%.6g",end);
        A(&commands[0],"1"); A(&commands[0],"-s%llu",mapped_seed(v.seed,32767u));
    } else if (id_is(recipe,"iterate")) {
        A(&commands[0],"iterate"); A(&commands[0],"2"); A(&commands[0],"input.wav"); A(&commands[0],"output.wav");
        A(&commands[0],"%d",(int)lrintf(v.controls[0])); A(&commands[0],"-d%.6g",v.controls[1]);
        A(&commands[0],"-p%.6g",v.controls[2]); A(&commands[0],"-f%.6g",v.controls[3]);
        A(&commands[0],"-s%llu",mapped_seed(v.seed,32767u));
    } else if (id_is(recipe,"wave_scramble")) {
        mode=(int)lrintf(v.controls[0]);
        A(&commands[0],"scramble"); A(&commands[0],"%d",mode); A(&commands[0],"input.wav"); A(&commands[0],"output.wav");
        if (mode<=2) A(&commands[0],"%.6g",duration);
        A(&commands[0],"%llu",mapped_seed(v.seed,256u));
        A(&commands[0],"-c%d",(int)lrintf(v.controls[1])); A(&commands[0],"-t%.6g",v.controls[2]); A(&commands[0],"-a%.6g",v.controls[3]);
    } else if (id_is(recipe,"brassage")) {
        A(&commands[0],"brassage"); A(&commands[0],"6"); A(&commands[0],"input.wav"); A(&commands[0],"output.wav");
        A(&commands[0],"1"); A(&commands[0],"%.6g",v.controls[1]); A(&commands[0],"%.6g",v.controls[0]*1000.0f);
        A(&commands[0],"%.6g",v.controls[2]); A(&commands[0],"1"); A(&commands[0],"%.6g",v.controls[3]);
        A(&commands[0],"10"); A(&commands[0],"10"); A(&commands[0],"-r%.6g",v.controls[3]); A(&commands[0],"-j%.6g",v.controls[3]*.5f);
    } else if (id_is(recipe,"fractal")) {
        A(&commands[0],"fractal"); A(&commands[0],"input.wav"); A(&commands[0],"output.wav");
        A(&commands[0],"%d",(int)lrintf(v.controls[0])); A(&commands[0],"%.6g",v.controls[1]); A(&commands[0],"-p%.6g",.75f/(1+v.controls[1]));
    } else if (id_is(recipe,"interpolate")) {
        A(&commands[0],"interpolate"); A(&commands[0],"input.wav"); A(&commands[0],"output.wav");
        A(&commands[0],"%d",(int)lrintf(v.controls[0])); A(&commands[0],"-s%d",(int)lrintf(v.controls[1]));
    } else if (id_is(recipe,"omit")) {
        int keep=(int)lrintf(v.controls[0]), group=(int)lrintf(v.controls[1]); if (keep>=group) keep=group-1;
        A(&commands[0],"omit"); A(&commands[0],"input.wav"); A(&commands[0],"output.wav"); A(&commands[0],"%d",keep); A(&commands[0],"%d",group);
    } else if (id_is(recipe,"replace")) {
        A(&commands[0],"replace"); A(&commands[0],"input.wav"); A(&commands[0],"output.wav");
        A(&commands[0],"%d",(int)lrintf(v.controls[0])); A(&commands[0],"-s%d",(int)lrintf(v.controls[1]));
    } else if (id_is(recipe,"pitch")) {
        A(&commands[0],"pitch"); A(&commands[0],"input.wav"); A(&commands[0],"output.wav"); A(&commands[0],"%.6g",v.controls[0]);
        A(&commands[0],"-c%d",(int)lrintf(v.controls[1])); A(&commands[0],"-s%d",(int)lrintf(v.controls[2]));
    } else if (id_is(recipe,"shuffle")) {
        static const char *patterns[]={"ab-ba","abc-bca","abcd-dcba","ab-aabb"};
        int index=(int)lrintf(v.controls[0]); if(index<0)index=0; if(index>3)index=3; pattern=patterns[index];
        A(&commands[0],"shuffle"); A(&commands[0],"input.wav"); A(&commands[0],"output.wav"); A(&commands[0],"%s",pattern);
        A(&commands[0],"-c%d",(int)lrintf(v.controls[1])); A(&commands[0],"-s%d",(int)lrintf(v.controls[2]));
    } else if (id_is(recipe,"reform")) {
        A(&commands[0],"reform"); A(&commands[0],"%d",(int)lrintf(v.controls[0])); A(&commands[0],"input.wav"); A(&commands[0],"output.wav");
    } else if (id_is(recipe,"distshift")) {
        mode=(int)lrintf(v.controls[0]); A(&commands[0],"distshift"); A(&commands[0],"%d",mode);
        A(&commands[0],"input.wav"); A(&commands[0],"output.wav"); A(&commands[0],"%d",(int)lrintf(v.controls[1]));
        if(mode==1) A(&commands[0],"%d",(int)lrintf(v.controls[2]));
    } else if (id_is(recipe,"segzig")) {
        A(&commands[0],"segszig"); A(&commands[0],"2"); A(&commands[0],"input.wav"); A(&commands[0],"output.wav");
        A(&commands[0],"%d",(int)lrintf(v.controls[0])); A(&commands[0],"-s%.6g",v.controls[1]*1000.0f);
        A(&commands[0],"-p%.6g",v.controls[2]); if(v.controls[3]>.5f) A(&commands[0],"-l");
    } else if (id_is(recipe,"overload")) {
        mode=(int)lrintf(v.controls[0]); A(&commands[0],"overload"); A(&commands[0],"%d",mode);
        A(&commands[0],"input.wav"); A(&commands[0],"output.wav"); A(&commands[0],"%.6g",v.controls[1]); A(&commands[0],"%.6g",v.controls[2]);
        if(mode==2) A(&commands[0],"%.6g",v.controls[3]);
    } else if (id_is(recipe,"filter_bank")) {
        double root=isfinite(v.tuning_hz)&&v.tuning_hz>=20?v.tuning_hz:261.625565;
        double max_hz=(double)sample_rate/3.0, lo, hi;
        mode=(int)lrintf(v.controls[0]);
        if (root > max_hz) root = max_hz;
        if (mode == 3) {
            lo=fmax(20.0,root/pow(2.0,v.controls[1]));
            hi=root;
        } else {
            lo=root;
            hi=fmin(max_hz,root*pow(2.0,v.controls[1]));
        }
        if(hi<=lo) { lo=fmax(20.0,hi/1.01); if(hi<=lo) hi=fmin(max_hz,lo*1.01); }
        A(&commands[0],"bank"); A(&commands[0],"%d",(int)lrintf(v.controls[0])); A(&commands[0],"input.wav"); A(&commands[0],"output.wav");
        A(&commands[0],"%.6g",v.controls[2]); A(&commands[0],"%.6g",v.controls[3]); A(&commands[0],"%.6g",lo); A(&commands[0],"%.6g",hi);
        A(&commands[0],"-t0.1");
    } else if (id_is(recipe,"granulate")) {
        A(&commands[0],"brassage"); A(&commands[0],"5"); A(&commands[0],"input.wav"); A(&commands[0],"output.wav"); A(&commands[0],"%.6g",v.controls[0]);
    } else {
        set_error(error,error_size,"Factory recipe command mapping is missing"); return 0;
    }
    set_error(error,error_size,""); return 1;
too_many:
    set_error(error,error_size,"Factory recipe produced too many command arguments"); return 0;
#undef A
}

int ts_cdp_recipe_input_valid(const TsCdpRecipe *recipe, size_t frames,
                              uint32_t sample_rate, char *error, size_t error_size)
{
    size_t minimum;
    if (recipe == NULL || frames == 0u || sample_rate == 0u) {
        set_error(error,error_size,"Recipe input is empty"); return 0;
    }
    minimum=(size_t)recipe->minimum_input_ms*sample_rate/1000u;
    if(recipe->analysis_points>0u) {
        size_t divisor=recipe->analysis_overlap==1u?1u:recipe->analysis_overlap==2u?2u:
                       recipe->analysis_overlap==3u?4u:8u;
        size_t hop=recipe->analysis_points/divisor;
        size_t spectral=recipe->analysis_points+(recipe->minimum_analysis_windows>1u?
                        (recipe->minimum_analysis_windows-1u)*hop:0u);
        if(spectral>minimum)minimum=spectral;
    }
    if(frames<minimum) {
        char message[128]; snprintf(message,sizeof(message),"%s NEEDS AT LEAST %.0F MS",
                                    recipe->display_name,(double)minimum*1000.0/sample_rate);
        set_error(error,error_size,message); return 0;
    }
    set_error(error,error_size,""); return 1;
}
