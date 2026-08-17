#include "tapesister/dsp_recipe.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#define PCT(label_, def_) \
    {label_, 0.0f, 1.0f, def_, 0, TS_DSP_RECIPE_VALUE_PERCENT}
#define SEC(label_, lo_, hi_, def_) \
    {label_, lo_, hi_, def_, 0, TS_DSP_RECIPE_VALUE_SECONDS}
#define HZ(label_, lo_, hi_, def_) \
    {label_, lo_, hi_, def_, 1, TS_DSP_RECIPE_VALUE_HERTZ}
#define DRIVE(label_, lo_, hi_, def_) \
    {label_, lo_, hi_, def_, 0, TS_DSP_RECIPE_VALUE_DRIVE}
#define RATIO(label_, lo_, hi_, def_) \
    {label_, lo_, hi_, def_, 1, TS_DSP_RECIPE_VALUE_RATIO}
#define BITS(label_, lo_, hi_, def_) \
    {label_, lo_, hi_, def_, 0, TS_DSP_RECIPE_VALUE_BITS}
#define MS(label_, lo_, hi_, def_) \
    {label_, lo_, hi_, def_, 1, TS_DSP_RECIPE_VALUE_MILLISECONDS}

#define PROCESS(kind_, id_, name_, desc_, a_, b_, c_, d_) \
    {id_, name_, desc_, "PROCESS", kind_, 0, kind_, 4, 0, {a_, b_, c_, d_}, 1, 1}
#define PRIMITIVE(kind_, id_, name_, desc_, a_, b_, c_, d_) \
    {id_, name_, desc_, "PRIMITIVE", kind_, 1, (kind_) - TS_DSP_RECIPE_SINE, 4, 1, \
     {a_, b_, c_, d_}, 1, 1}

static const TsDspRecipe recipes[TS_DSP_RECIPE_COUNT] = {
    PROCESS(TS_DSP_RECIPE_SPACE, "space", "SPACE", "OPEN REVERB WITH A CLEAR TAIL",
            PCT("SIZE", .58f), PCT("DECAY", .62f), PCT("DAMP", .45f), PCT("MIX", .42f)),
    PROCESS(TS_DSP_RECIPE_CAVE, "cave", "CAVE", "HUGE DARK REVERBERANT MASS",
            PCT("SIZE", .84f), PCT("DECAY", .86f), PCT("DIFFUSE", .72f), PCT("DARK", .70f)),
    PROCESS(TS_DSP_RECIPE_ROOM, "room", "ROOM", "SHORT REFLECTIVE NATURAL SPACE",
            PCT("SIZE", .32f), PCT("REFLECT", .52f), PCT("DECAY", .34f), PCT("MIX", .34f)),
    PROCESS(TS_DSP_RECIPE_ECHO, "echo", "ECHO", "CLEAN REPEATS WITH MUSICAL CONTROL",
            SEC("TIME", .02f, 1.0f, .36f), PCT("FEEDBACK", .55f), PCT("TONE", .58f), PCT("MIX", .46f)),
    PROCESS(TS_DSP_RECIPE_TAPE, "tape", "TAPE", "WORN MODULATED TAPE DELAY",
            SEC("TIME", .03f, .75f, .38f), PCT("FEEDBACK", .50f), PCT("WOBBLE", .28f), PCT("AGE", .44f)),
    PROCESS(TS_DSP_RECIPE_DUB, "dub", "DUB", "FILTERED DRIVEN FEEDBACK DELAY",
            SEC("TIME", .04f, 1.0f, .42f), PCT("FEEDBACK", .64f), HZ("FILTER", 180.0f, 8000.0f, .45f), DRIVE("DRIVE", 1.0f, 10.0f, .24f)),
    PROCESS(TS_DSP_RECIPE_COMB, "comb", "COMB", "TUNED HOLLOW FEEDBACK RESONANCE",
            HZ("FREQ", 35.0f, 4000.0f, .38f), PCT("FEEDBACK", .62f), PCT("DAMP", .42f), PCT("MIX", .68f)),
    PROCESS(TS_DSP_RECIPE_RESONATE, "resonate", "RESONATE", "FOCUSED BAND RESONANCE",
            HZ("FREQ", 60.0f, 12000.0f, .46f), PCT("RESONANCE", .70f), PCT("DAMP", .35f), PCT("MIX", .72f)),
    PROCESS(TS_DSP_RECIPE_LOW, "low", "LOW", "LOW-PASS WEIGHT AND DRIVE",
            HZ("CUTOFF", 35.0f, 16000.0f, .52f), PCT("RESONANCE", .30f), DRIVE("DRIVE", 1.0f, 8.0f, .10f), PCT("MIX", 1.0f)),
    PROCESS(TS_DSP_RECIPE_HIGH, "high", "HIGH", "HIGH-PASS AIR AND EDGE",
            HZ("CUTOFF", 35.0f, 16000.0f, .38f), PCT("RESONANCE", .24f), DRIVE("DRIVE", 1.0f, 8.0f, .08f), PCT("MIX", 1.0f)),
    PROCESS(TS_DSP_RECIPE_BAND, "band", "BAND", "NARROW OR BROAD BAND FOCUS",
            HZ("CENTER", 40.0f, 16000.0f, .50f), PCT("WIDTH", .55f), DRIVE("DRIVE", 1.0f, 8.0f, .08f), PCT("MIX", .84f)),
    PROCESS(TS_DSP_RECIPE_NOTCH, "notch", "NOTCH", "MOVING SPECTRAL CUT AND PHASE",
            HZ("FREQ", 50.0f, 14000.0f, .48f), PCT("Q", .55f), HZ("MOVE", .03f, 6.0f, .30f), PCT("MIX", .78f)),
    PROCESS(TS_DSP_RECIPE_CHORUS, "chorus", "CHORUS", "WIDE SLOW MODULATED DOUBLING",
            HZ("RATE", .03f, 8.0f, .34f), MS("DEPTH", .4f, 24.0f, .46f), PCT("SPREAD", .56f), PCT("MIX", .52f)),
    PROCESS(TS_DSP_RECIPE_FLANGE, "flange", "FLANGE", "SHORT METALLIC MODULATED DELAY",
            HZ("RATE", .03f, 12.0f, .40f), MS("DEPTH", .1f, 8.0f, .52f), PCT("FEEDBACK", .54f), PCT("MIX", .62f)),
    PROCESS(TS_DSP_RECIPE_DRIVE, "drive", "DRIVE", "SATURATION CLIP AND FOLD PRESSURE",
            DRIVE("GAIN", 1.0f, 16.0f, .42f), PCT("SHAPE", .18f), HZ("TONE", 300.0f, 18000.0f, .72f), PCT("MIX", .82f)),
    PROCESS(TS_DSP_RECIPE_CRUSH, "crush", "CRUSH", "BIT AND SAMPLE-RATE REDUCTION",
            BITS("BITS", 3.0f, 16.0f, .58f), PCT("RATE", .30f), HZ("FILTER", 180.0f, 18000.0f, .72f), PCT("MIX", .82f)),

    PRIMITIVE(TS_DSP_RECIPE_SINE, "sine", "SINE", "PURE FUNDAMENTAL MATERIAL",
              HZ("PITCH", 20.0f, 4000.0f, .42f), PCT("HARMONIC", .12f), PCT("DECAY", .10f), PCT("SOURCE", .0f)),
    PRIMITIVE(TS_DSP_RECIPE_SHAPE, "shape", "SHAPE", "MORPHED OSCILLATOR MATERIAL",
              HZ("PITCH", 20.0f, 4000.0f, .40f), PCT("SHAPE", .48f), PCT("MOTION", .24f), PCT("SOURCE", .0f)),
    PRIMITIVE(TS_DSP_RECIPE_PULSE, "pulse", "PULSE", "WIDTH-MODULATED PULSE MATERIAL",
              HZ("PITCH", 20.0f, 3000.0f, .38f), PCT("WIDTH", .42f), PCT("MOD", .28f), PCT("SOURCE", .0f)),
    PRIMITIVE(TS_DSP_RECIPE_SUB, "sub", "SUB", "DEEP FUNDAMENTAL LAYER",
              HZ("PITCH", 20.0f, 220.0f, .38f), PCT("WEIGHT", .72f), PCT("DECAY", .20f), PCT("SOURCE", .28f)),
    PRIMITIVE(TS_DSP_RECIPE_METAL, "metal", "METAL", "INHARMONIC STRUCK PARTIALS",
              HZ("PITCH", 30.0f, 2400.0f, .40f), RATIO("RATIO", 1.1f, 9.0f, .54f), PCT("MOD", .62f), PCT("SOURCE", .12f)),
    PRIMITIVE(TS_DSP_RECIPE_CHIME, "chime", "CHIME", "BRIGHT DECAYING PARTIAL CLUSTER",
              HZ("PITCH", 80.0f, 5000.0f, .40f), PCT("BRIGHT", .68f), PCT("DECAY", .52f), PCT("SOURCE", .10f)),
    PRIMITIVE(TS_DSP_RECIPE_DRONE, "drone", "DRONE", "SUSTAINED DRIFTING OSCILLATOR MASS",
              HZ("PITCH", 20.0f, 1000.0f, .38f), PCT("SHAPE", .46f), PCT("DRIFT", .38f), PCT("SOURCE", .10f)),
    PRIMITIVE(TS_DSP_RECIPE_BEAT, "beat", "BEAT", "CLOSE OSCILLATORS WITH ACOUSTIC THROB",
              HZ("PITCH", 20.0f, 1600.0f, .40f), HZ("BEATING", .1f, 30.0f, .34f), PCT("COLOR", .34f), PCT("SOURCE", .10f)),
    PRIMITIVE(TS_DSP_RECIPE_RUMBLE, "rumble", "RUMBLE", "LOW MOVING NOISE AND OSCILLATION",
              HZ("PITCH", 18.0f, 180.0f, .40f), PCT("ROUGH", .62f), PCT("MOTION", .52f), PCT("SOURCE", .18f)),
    PRIMITIVE(TS_DSP_RECIPE_HISS, "hiss", "HISS", "FILTERED CONTINUOUS NOISE",
              PCT("COLOR", .58f), HZ("TONE", 300.0f, 18000.0f, .70f), PCT("MOTION", .24f), PCT("SOURCE", .24f)),
    PRIMITIVE(TS_DSP_RECIPE_DUST, "dust", "DUST", "SPARSE PARTICLE IMPULSES",
              HZ("DENSITY", 1.0f, 500.0f, .44f), MS("DECAY", .5f, 90.0f, .34f), PCT("TONE", .62f), PCT("SOURCE", .22f)),
    PRIMITIVE(TS_DSP_RECIPE_KNOCK, "knock", "KNOCK", "SHORT WOODEN BODY IMPULSE",
              HZ("PITCH", 25.0f, 800.0f, .40f), PCT("BODY", .70f), PCT("DECAY", .38f), PCT("SOURCE", .16f)),
    PRIMITIVE(TS_DSP_RECIPE_PING, "ping", "PING", "CLEAN RESONANT TRANSIENT",
              HZ("PITCH", 80.0f, 6000.0f, .46f), PCT("RESONANCE", .68f), PCT("DECAY", .48f), PCT("SOURCE", .16f)),
    PRIMITIVE(TS_DSP_RECIPE_FM, "fm", "FM", "DIRECT TWO-OPERATOR FM MATERIAL",
              HZ("PITCH", 20.0f, 4000.0f, .40f), RATIO("RATIO", .25f, 12.0f, .52f), DRIVE("INDEX", .0f, 12.0f, .42f), PCT("SOURCE", .06f)),
    PRIMITIVE(TS_DSP_RECIPE_AM, "am", "AM", "AMPLITUDE-MODULATED OSCILLATOR MATERIAL",
              HZ("PITCH", 20.0f, 4000.0f, .40f), HZ("RATE", .1f, 800.0f, .48f), PCT("DEPTH", .68f), PCT("SOURCE", .08f)),
    PRIMITIVE(TS_DSP_RECIPE_CHAOS, "chaos", "CHAOS", "BOUNDED UNSTABLE OSCILLATOR AND DUST",
              HZ("PITCH", 18.0f, 1200.0f, .38f), PCT("UNSTABLE", .66f), PCT("DENSITY", .52f), PCT("SOURCE", .08f))
};

#undef PCT
#undef SEC
#undef HZ
#undef DRIVE
#undef RATIO
#undef BITS
#undef MS
#undef PROCESS
#undef PRIMITIVE

static void set_error(char *error, size_t error_size, const char *message)
{
    if (error != NULL && error_size > 0u)
        snprintf(error, error_size, "%s", message != NULL ? message : "");
}

static float clamp_unit(float value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

size_t ts_dsp_factory_recipe_count(void)
{
    return TS_DSP_RECIPE_COUNT;
}

const TsDspRecipe *ts_dsp_factory_recipe_at(size_t index)
{
    return index < TS_DSP_RECIPE_COUNT ? &recipes[index] : NULL;
}

const TsDspRecipe *ts_dsp_factory_recipe_for_slot(size_t bank, size_t slot)
{
    if (bank >= TS_DSP_BANK_COUNT || slot >= TS_DSP_BANK_SLOT_COUNT) return NULL;
    return &recipes[bank * TS_DSP_BANK_SLOT_COUNT + slot];
}

const TsDspRecipe *ts_dsp_recipe_find(const char *id)
{
    if (id == NULL) return NULL;
    for (size_t index = 0; index < TS_DSP_RECIPE_COUNT; ++index)
        if (strcmp(recipes[index].id, id) == 0) return &recipes[index];
    return NULL;
}

int ts_dsp_recipe_validate(const TsDspRecipe *recipe,
                           char *error, size_t error_size)
{
    if (recipe == NULL || recipe->id == NULL || recipe->id[0] == '\0' ||
        recipe->display_name == NULL || recipe->display_name[0] == '\0' ||
        recipe->description == NULL || recipe->description[0] == '\0' ||
        recipe->kind < 0 || recipe->kind >= TS_DSP_RECIPE_COUNT ||
        recipe->bank >= TS_DSP_BANK_COUNT || recipe->slot >= TS_DSP_BANK_SLOT_COUNT ||
        recipe->control_count == 0 || recipe->control_count > TS_DSP_CONTROL_COUNT ||
        recipe->schema_version == 0 || recipe->recipe_version == 0) {
        set_error(error, error_size, "Invalid curated DSP recipe header");
        return 0;
    }
    for (size_t index = 0; index < recipe->control_count; ++index) {
        const TsDspRecipeControl *control = &recipe->controls[index];
        if (control->label == NULL || control->label[0] == '\0' ||
            !isfinite(control->minimum) || !isfinite(control->maximum) ||
            control->maximum <= control->minimum ||
            !isfinite(control->default_normalized) ||
            control->default_normalized < 0.0f || control->default_normalized > 1.0f) {
            set_error(error, error_size, "Invalid curated DSP control");
            return 0;
        }
    }
    set_error(error, error_size, "");
    return 1;
}

void ts_dsp_recipe_values_default(const TsDspRecipe *recipe,
                                  TsDspRecipeValues *values)
{
    if (values == NULL) return;
    memset(values, 0, sizeof(*values));
    if (recipe == NULL) return;
    for (size_t index = 0; index < recipe->control_count; ++index)
        values->controls[index] = recipe->controls[index].default_normalized;
    values->seed = 0x44535031u ^ ((uint32_t)recipe->kind * 0x9e3779b9u);
    values->tuning_hz = 440.0f;
}

int ts_dsp_recipe_values_equal(const TsDspRecipeValues *left,
                               const TsDspRecipeValues *right)
{
    if (left == NULL || right == NULL || left->seed != right->seed ||
        left->tuning_hz != right->tuning_hz) return 0;
    return memcmp(left->controls, right->controls, sizeof(left->controls)) == 0;
}

int ts_dsp_recipe_set_control(const TsDspRecipe *recipe,
                              TsDspRecipeValues *values,
                              size_t index, float normalized)
{
    if (recipe == NULL || values == NULL || index >= recipe->control_count ||
        !isfinite(normalized)) return 0;
    values->controls[index] = clamp_unit(normalized);
    return 1;
}

float ts_dsp_recipe_control_value(const TsDspRecipeControl *control,
                                  float normalized)
{
    if (control == NULL) return clamp_unit(normalized);
    normalized = clamp_unit(normalized);
    if (control->logarithmic && control->minimum > 0.0f)
        return expf(logf(control->minimum) + normalized *
                    logf(control->maximum / control->minimum));
    return control->minimum + normalized * (control->maximum - control->minimum);
}

void ts_dsp_recipe_control_format(const TsDspRecipeControl *control,
                                  float normalized,
                                  char *text, size_t text_size)
{
    float value;
    if (text == NULL || text_size == 0u) return;
    if (control == NULL) {
        snprintf(text, text_size, "---");
        return;
    }
    value = ts_dsp_recipe_control_value(control, normalized);
    switch (control->format) {
    case TS_DSP_RECIPE_VALUE_SECONDS:
        if (value < 1.0f) snprintf(text, text_size, "%dMS", (int)lrintf(value * 1000.0f));
        else snprintf(text, text_size, "%.2FS", value);
        break;
    case TS_DSP_RECIPE_VALUE_HERTZ:
        if (value >= 1000.0f) snprintf(text, text_size, "%.1FK", value / 1000.0f);
        else if (value < 10.0f) snprintf(text, text_size, "%.2FHZ", value);
        else snprintf(text, text_size, "%dHZ", (int)lrintf(value));
        break;
    case TS_DSP_RECIPE_VALUE_DRIVE:
        snprintf(text, text_size, "X%.1F", value);
        break;
    case TS_DSP_RECIPE_VALUE_RATIO:
        snprintf(text, text_size, "%.2F:1", value);
        break;
    case TS_DSP_RECIPE_VALUE_BITS:
        snprintf(text, text_size, "%d BIT", (int)lrintf(value));
        break;
    case TS_DSP_RECIPE_VALUE_MILLISECONDS:
        snprintf(text, text_size, "%.1FMS", value);
        break;
    case TS_DSP_RECIPE_VALUE_PERCENT:
    default:
        snprintf(text, text_size, "%d%%", (int)lrintf(value * 100.0f));
        break;
    }
}
