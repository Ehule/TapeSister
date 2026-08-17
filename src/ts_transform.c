#include "tapesister/transform.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_error(char *error, size_t error_size, const char *message)
{
    if (error != NULL && error_size > 0u)
        snprintf(error, error_size, "%s", message != NULL ? message : "");
}

static int values_match(const TsCdpRecipeValues *left,
                        const TsCdpRecipeValues *right)
{
    if (left == NULL || right == NULL || left->mix != right->mix ||
        left->seed != right->seed || left->tuning_hz != right->tuning_hz) return 0;
    for (size_t i = 0; i < TS_CDP_CONTROL_COUNT; ++i)
        if (left->controls[i] != right->controls[i]) return 0;
    return 1;
}

void ts_transform_preview_init(TsTransformPreview *preview)
{
    if (preview == NULL) return;
    memset(preview, 0, sizeof(*preview));
    ts_sample_init(&preview->sample);
    preview->safety = TS_CDP_SAFETY_INVALID;
}

void ts_transform_preview_free(TsTransformPreview *preview)
{
    if (preview == NULL) return;
    ts_sample_free(&preview->sample);
    ts_transform_preview_init(preview);
}

int ts_transform_identity_capture(TsTransformIdentity *identity,
                                  const TsInstrument *instrument,
                                  TsTransformScope scope,
                                  const TsCdpRecipe *recipe,
                                  const TsCdpRecipeValues *values,
                                  uint64_t job_id,
                                  uint64_t render_generation,
                                  char *error, size_t error_size)
{
    if (identity == NULL || instrument == NULL || recipe == NULL || values == NULL ||
        instrument->current.data == NULL || instrument->current.frames == 0u ||
        instrument->selected_slot < 0 ||
        instrument->selected_slot >= TS_BANK_SLOT_COUNT ||
        !instrument->bank[instrument->selected_slot].occupied) {
        set_error(error, error_size, "Transform needs an occupied active tile");
        return 0;
    }
    if (scope == TS_TRANSFORM_SELECTION &&
        (!instrument->has_selection ||
         instrument->selection_last <= instrument->selection_first ||
         instrument->selection_last > instrument->current.frames)) {
        set_error(error, error_size, "SELECTION SCOPE NEEDS A NONEMPTY SELECTION");
        return 0;
    }
    memset(identity, 0, sizeof(*identity));
    identity->job_id = job_id;
    identity->render_generation = render_generation;
    identity->tile_slot = instrument->selected_slot;
    identity->audio_revision = ts_sample_hash(&instrument->current);
    identity->tile_frames = instrument->current.frames;
    identity->selection_first = instrument->selection_first;
    identity->selection_last = instrument->selection_last;
    identity->has_selection = instrument->has_selection;
    identity->scope = scope;
    snprintf(identity->recipe_id, sizeof(identity->recipe_id), "%s", recipe->id);
    identity->recipe_schema_version = recipe->schema_version;
    identity->recipe_version = recipe->recipe_version;
    identity->values = *values;
    for (size_t i = 0; i < recipe->control_count; ++i)
        identity->values.controls[i] = ts_cdp_control_quantize(
            &recipe->controls[i], identity->values.controls[i]);
    if (recipe->mix_policy == TS_CDP_MIX_UNSUPPORTED) identity->values.mix = 1.0f;
    if (identity->values.mix < 0.0f) identity->values.mix = 0.0f;
    if (identity->values.mix > 1.0f) identity->values.mix = 1.0f;
    if (!recipe->seed_supported) identity->values.seed = 0u;
    identity->seed = recipe->seed_supported ? values->seed : 0u;
    identity->has_seed = recipe->seed_supported;
    identity->input_sample_rate = instrument->current.sample_rate;
    identity->input_channels = 1u;
    identity->expected_stage_count = recipe->stage_count;
    identity->expected_output_type = recipe->stages[recipe->stage_count - 1u].output_type;
    set_error(error, error_size, "");
    return 1;
}

int ts_transform_identity_matches(const TsTransformIdentity *identity,
                                  const TsInstrument *instrument,
                                  TsTransformScope scope,
                                  const TsCdpRecipe *recipe,
                                  const TsCdpRecipeValues *values,
                                  uint64_t render_generation,
                                  char *error, size_t error_size)
{
    TsCdpRecipeValues current;
    if (identity == NULL || instrument == NULL || recipe == NULL || values == NULL ||
        identity->render_generation != render_generation ||
        identity->tile_slot != instrument->selected_slot ||
        identity->tile_frames != instrument->current.frames ||
        identity->audio_revision != ts_sample_hash(&instrument->current) ||
        identity->selection_first != instrument->selection_first ||
        identity->selection_last != instrument->selection_last ||
        identity->has_selection != instrument->has_selection ||
        identity->scope != scope || strcmp(identity->recipe_id, recipe->id) != 0 ||
        identity->recipe_schema_version != recipe->schema_version ||
        identity->recipe_version != recipe->recipe_version ||
        identity->has_seed != recipe->seed_supported ||
        identity->input_sample_rate != instrument->current.sample_rate ||
        identity->input_channels != recipe->required_input_channels ||
        identity->expected_stage_count != recipe->stage_count ||
        identity->expected_output_type !=
        recipe->stages[recipe->stage_count - 1u].output_type) {
        set_error(error, error_size, "TRANSFORM RESULT IS STALE");
        return 0;
    }
    current = *values;
    for (size_t i = 0; i < TS_CDP_CONTROL_COUNT; ++i)
        current.controls[i] = ts_cdp_control_quantize(&recipe->controls[i],
                                                      current.controls[i]);
    if (recipe->mix_policy == TS_CDP_MIX_UNSUPPORTED) current.mix = 1.0f;
    if (current.mix < 0.0f) current.mix = 0.0f;
    if (current.mix > 1.0f) current.mix = 1.0f;
    if (!recipe->seed_supported) current.seed = 0u;
    if (!values_match(&identity->values, &current)) {
        set_error(error, error_size, "TRANSFORM CONTROLS CHANGED - RENDER AGAIN");
        return 0;
    }
    set_error(error, error_size, "");
    return 1;
}

int ts_transform_extract_input(const TsInstrument *instrument,
                               const TsTransformIdentity *identity,
                               TsSample *input,
                               char *error, size_t error_size)
{
    size_t first;
    size_t last;
    size_t frames;
    float *data;
    if (instrument == NULL || identity == NULL || input == NULL ||
        identity->tile_slot != instrument->selected_slot ||
        identity->audio_revision != ts_sample_hash(&instrument->current)) {
        set_error(error, error_size, "Transform source changed before export");
        return 0;
    }
    first = identity->scope == TS_TRANSFORM_SELECTION ?
            identity->selection_first : 0u;
    last = identity->scope == TS_TRANSFORM_SELECTION ?
           identity->selection_last : instrument->current.frames;
    if (first >= last || last > instrument->current.frames) {
        set_error(error, error_size, "Transform scope is empty");
        return 0;
    }
    frames = last - first;
    if (frames > SIZE_MAX / sizeof(*data)) {
        set_error(error, error_size, "Transform input is too large");
        return 0;
    }
    data = malloc(frames * sizeof(*data));
    if (data == NULL) {
        set_error(error, error_size, "Out of memory snapshotting Transform input");
        return 0;
    }
    memcpy(data, instrument->current.data + first, frames * sizeof(*data));
    ts_sample_free(input);
    input->data = data;
    input->frames = frames;
    input->sample_rate = instrument->current.sample_rate;
    snprintf(input->name, sizeof(input->name), "TRANSFORM %.116s",
             instrument->current.name);
    set_error(error, error_size, "");
    return 1;
}

int ts_transform_mix_samples(const TsSample *dry, const TsSample *wet,
                             float mix, TsCdpMixPolicy policy,
                             TsSample *output,
                             char *error, size_t error_size)
{
    float *data;
    if (dry == NULL || wet == NULL || output == NULL || dry->data == NULL ||
        wet->data == NULL || dry->sample_rate == 0u ||
        wet->sample_rate != dry->sample_rate || !isfinite(mix)) {
        set_error(error, error_size, "Invalid Transform MIX input");
        return 0;
    }
    if (mix < 0.0f) mix = 0.0f;
    if (mix > 1.0f) mix = 1.0f;
    if (policy == TS_CDP_MIX_UNSUPPORTED && mix != 1.0f) {
        set_error(error, error_size, "MIX IS DISABLED FOR THIS RECIPE");
        return 0;
    }
    if (mix == 0.0f) return ts_sample_clone(output, dry, error, error_size);
    if (mix == 1.0f) return ts_sample_clone(output, wet, error, error_size);
    if (policy != TS_CDP_MIX_EXACT_FRAMES || wet->frames != dry->frames) {
        set_error(error, error_size,
                  "MIX NEEDS EXACT DRY/WET LENGTH - USE 100% WET");
        return 0;
    }
    if (dry->frames > SIZE_MAX / sizeof(*data)) {
        set_error(error, error_size, "Transform MIX is too large");
        return 0;
    }
    data = malloc(dry->frames * sizeof(*data));
    if (data == NULL) {
        set_error(error, error_size, "Out of memory mixing Transform preview");
        return 0;
    }
    for (size_t i = 0; i < dry->frames; ++i)
        data[i] = dry->data[i] * (1.0f - mix) + wet->data[i] * mix;
    ts_sample_free(output);
    output->data = data;
    output->frames = dry->frames;
    output->sample_rate = dry->sample_rate;
    snprintf(output->name, sizeof(output->name), "MIX %.122s", wet->name);
    set_error(error, error_size, "");
    return 1;
}

void ts_transform_boundary_splice(TsSample *replacement, const TsSample *tile,
                                  size_t first, size_t last)
{
    size_t fade;
    if (replacement == NULL || tile == NULL || replacement->data == NULL ||
        tile->data == NULL || replacement->frames < 2u || first > last ||
        last > tile->frames) return;
    fade = replacement->sample_rate / 1000u;
    if (fade < 8u) fade = 8u;
    if (fade > 64u) fade = 64u;
    if (fade > replacement->frames / 2u) fade = replacement->frames / 2u;
    if (first > 0u) {
        float edge = tile->data[first - 1u];
        for (size_t i = 0; i < fade; ++i) {
            float wet = (float)(i + 1u) / (float)(fade + 1u);
            replacement->data[i] = edge * (1.0f - wet) + replacement->data[i] * wet;
        }
    }
    if (last < tile->frames) {
        float edge = tile->data[last];
        for (size_t i = 0; i < fade; ++i) {
            size_t at = replacement->frames - 1u - i;
            float wet = (float)(i + 1u) / (float)(fade + 1u);
            replacement->data[at] = edge * (1.0f - wet) + replacement->data[at] * wet;
        }
    }
}

int ts_transform_prepare_preview(const TsInstrument *instrument,
                                 const TsTransformIdentity *identity,
                                 const TsCdpRecipe *recipe,
                                 const TsCdpRunResult *render,
                                 TsTransformPreview *preview,
                                 char *error, size_t error_size)
{
    TsSample dry;
    TsSample mixed;
    size_t first;
    size_t last;
    if (instrument == NULL || identity == NULL || recipe == NULL || render == NULL ||
        preview == NULL || render->status != TS_CDP_RUN_OK ||
        render->output.data == NULL || render->output.frames == 0u) {
        set_error(error, error_size, "No valid CDP render is ready");
        return 0;
    }
    if (identity->tile_slot != instrument->selected_slot ||
        identity->audio_revision != ts_sample_hash(&instrument->current) ||
        identity->tile_frames != instrument->current.frames ||
        identity->selection_first != instrument->selection_first ||
        identity->selection_last != instrument->selection_last ||
        identity->has_selection != instrument->has_selection) {
        set_error(error, error_size, "CDP render completed for stale tile state");
        return 0;
    }
    ts_sample_init(&dry);
    ts_sample_init(&mixed);
    if (!ts_transform_extract_input(instrument, identity, &dry, error, error_size) ||
        !ts_transform_mix_samples(&dry, &render->output, identity->values.mix,
                                  recipe->mix_policy, &mixed,
                                  error, error_size)) {
        ts_sample_free(&dry);
        ts_sample_free(&mixed);
        return 0;
    }
    first = identity->scope == TS_TRANSFORM_SELECTION ? identity->selection_first : 0u;
    last = identity->scope == TS_TRANSFORM_SELECTION ?
           identity->selection_last : instrument->current.frames;
    if (identity->scope == TS_TRANSFORM_SELECTION && identity->values.mix > 0.0f)
        ts_transform_boundary_splice(&mixed, &instrument->current, first, last);
    ts_transform_preview_free(preview);
    preview->identity = *identity;
    preview->sample = mixed;
    ts_sample_init(&mixed);
    preview->replacement_first = first;
    preview->replacement_last = last;
    preview->safety = render->safety;
    preview->peak = render->peak;
    preview->dc_offset = render->dc_offset;
    preview->clipped_samples = render->clipped_samples;
    preview->valid = 1;
    ts_sample_free(&dry);
    set_error(error, error_size, "");
    return 1;
}

int ts_transform_apply_preview(TsInstrument *instrument,
                               const TsTransformPreview *preview,
                               TsTransformScope scope,
                               const TsCdpRecipe *recipe,
                               const TsCdpRecipeValues *values,
                               uint64_t render_generation,
                               char *error, size_t error_size)
{
    if (preview == NULL || !preview->valid || preview->sample.data == NULL) {
        set_error(error, error_size, "RENDER A VALID PREVIEW BEFORE APPLY");
        return 0;
    }
    if (!ts_transform_identity_matches(&preview->identity, instrument, scope,
                                       recipe, values, render_generation,
                                       error, error_size)) return 0;
    return ts_instrument_apply_rendered_replacement(
        instrument, &preview->sample, preview->replacement_first,
        preview->replacement_last, error, error_size);
}
