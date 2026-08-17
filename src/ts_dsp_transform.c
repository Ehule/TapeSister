#include "tapesister/dsp_transform.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_error(char *error, size_t error_size, const char *message)
{
    if (error != NULL && error_size > 0u)
        snprintf(error, error_size, "%s", message != NULL ? message : "");
}

void ts_dsp_transform_preview_init(TsDspTransformPreview *preview)
{
    if (preview == NULL) return;
    memset(preview, 0, sizeof(*preview));
    ts_sample_init(&preview->sample);
    preview->safety = TS_CDP_SAFETY_INVALID;
}

void ts_dsp_transform_preview_free(TsDspTransformPreview *preview)
{
    if (preview == NULL) return;
    ts_sample_free(&preview->sample);
    ts_dsp_transform_preview_init(preview);
}

int ts_dsp_transform_identity_capture(TsDspTransformIdentity *identity,
                                      const TsInstrument *instrument,
                                      TsTransformScope scope, int preset_slot,
                                      const TsProcessRecipe *process,
                                      uint64_t job_id,
                                      uint64_t render_generation,
                                      char *error, size_t error_size)
{
    if (identity == NULL || instrument == NULL || process == NULL ||
        !ts_recipe_process_valid(process) || instrument->current.data == NULL ||
        instrument->current.frames == 0u || instrument->selected_slot < 0 ||
        instrument->selected_slot >= TS_BANK_SLOT_COUNT ||
        !instrument->bank[instrument->selected_slot].occupied) {
        set_error(error, error_size, "DSP Transform needs an occupied active tile");
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
    identity->preset_slot = preset_slot;
    identity->process = *process;
    set_error(error, error_size, "");
    return 1;
}

int ts_dsp_transform_identity_matches(const TsDspTransformIdentity *identity,
                                      const TsInstrument *instrument,
                                      TsTransformScope scope, int preset_slot,
                                      const TsProcessRecipe *process,
                                      uint64_t render_generation,
                                      char *error, size_t error_size)
{
    if (identity == NULL || instrument == NULL || process == NULL ||
        identity->render_generation != render_generation ||
        identity->tile_slot != instrument->selected_slot ||
        identity->audio_revision != ts_sample_hash(&instrument->current) ||
        identity->tile_frames != instrument->current.frames ||
        identity->selection_first != instrument->selection_first ||
        identity->selection_last != instrument->selection_last ||
        identity->has_selection != instrument->has_selection ||
        identity->scope != scope || identity->preset_slot != preset_slot ||
        !ts_process_recipe_equal(&identity->process, process)) {
        set_error(error, error_size, "DSP TRANSFORM RESULT IS STALE");
        return 0;
    }
    set_error(error, error_size, "");
    return 1;
}

int ts_dsp_transform_extract_input(const TsInstrument *instrument,
                                   const TsDspTransformIdentity *identity,
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
        set_error(error, error_size, "DSP source changed before render");
        return 0;
    }
    first = identity->scope == TS_TRANSFORM_SELECTION ?
            identity->selection_first : 0u;
    last = identity->scope == TS_TRANSFORM_SELECTION ?
           identity->selection_last : instrument->current.frames;
    if (first >= last || last > instrument->current.frames) {
        set_error(error, error_size, "DSP Transform scope is empty");
        return 0;
    }
    frames = last - first;
    if (frames > SIZE_MAX / sizeof(*data)) {
        set_error(error, error_size, "DSP Transform input is too large");
        return 0;
    }
    data = malloc(frames * sizeof(*data));
    if (data == NULL) {
        set_error(error, error_size, "Out of memory snapshotting DSP input");
        return 0;
    }
    memcpy(data, instrument->current.data + first, frames * sizeof(*data));
    ts_sample_free(input);
    input->data = data;
    input->frames = frames;
    input->sample_rate = instrument->current.sample_rate;
    snprintf(input->name, sizeof(input->name), "DSP %.120s", instrument->current.name);
    set_error(error, error_size, "");
    return 1;
}

int ts_dsp_transform_render(const TsSample *input,
                            const TsProcessRecipe *process,
                            TsSample *output,
                            TsCdpSafetyStatus *safety,
                            float *peak, double *dc_offset,
                            int *clipped_samples,
                            char *error, size_t error_size)
{
    double dc = 0.0;
    float maximum = 0.0f;
    int clipped = 0;
    if (input == NULL || input->data == NULL || input->frames == 0u ||
        output == NULL || !ts_recipe_process_valid(process) ||
        !ts_sample_process(output, input, 0u, input->frames, process,
                           error, error_size)) return 0;
    for (size_t i = 0; i < output->frames; ++i) {
        float value = output->data[i];
        float absolute;
        if (!isfinite(value)) {
            ts_sample_free(output);
            if (safety != NULL) *safety = TS_CDP_SAFETY_INVALID;
            set_error(error, error_size, "DSP preview produced non-finite audio");
            return 0;
        }
        absolute = fabsf(value);
        if (absolute > maximum) maximum = absolute;
        if (absolute >= 0.9999f) ++clipped;
        dc += value;
    }
    dc /= (double)output->frames;
    if (peak != NULL) *peak = maximum;
    if (dc_offset != NULL) *dc_offset = dc;
    if (clipped_samples != NULL) *clipped_samples = clipped;
    if (safety != NULL)
        *safety = maximum < 0.00001f ? TS_CDP_SAFETY_SILENT :
                  clipped > 0 ? TS_CDP_SAFETY_HOT : TS_CDP_SAFETY_SAFE;
    set_error(error, error_size, "");
    return 1;
}

int ts_dsp_transform_prepare_preview(const TsInstrument *instrument,
                                     const TsDspTransformIdentity *identity,
                                     const TsSample *rendered,
                                     TsCdpSafetyStatus safety,
                                     float peak, double dc_offset,
                                     int clipped_samples,
                                     TsDspTransformPreview *preview,
                                     char *error, size_t error_size)
{
    size_t first;
    size_t last;
    if (instrument == NULL || identity == NULL || rendered == NULL ||
        rendered->data == NULL || rendered->frames == 0u || preview == NULL ||
        identity->tile_slot != instrument->selected_slot ||
        identity->audio_revision != ts_sample_hash(&instrument->current) ||
        identity->tile_frames != instrument->current.frames ||
        identity->selection_first != instrument->selection_first ||
        identity->selection_last != instrument->selection_last ||
        identity->has_selection != instrument->has_selection) {
        set_error(error, error_size, "No current native DSP render is ready");
        return 0;
    }
    first = identity->scope == TS_TRANSFORM_SELECTION ? identity->selection_first : 0u;
    last = identity->scope == TS_TRANSFORM_SELECTION ?
           identity->selection_last : instrument->current.frames;
    if (rendered->frames != last - first ||
        rendered->sample_rate != instrument->current.sample_rate) {
        set_error(error, error_size, "Native DSP preview has an invalid length or rate");
        return 0;
    }
    ts_dsp_transform_preview_free(preview);
    if (!ts_sample_clone(&preview->sample, rendered, error, error_size)) return 0;
    if (identity->scope == TS_TRANSFORM_SELECTION)
        ts_transform_boundary_splice(&preview->sample, &instrument->current, first, last);
    preview->identity = *identity;
    preview->replacement_first = first;
    preview->replacement_last = last;
    preview->safety = safety;
    preview->peak = peak;
    preview->dc_offset = dc_offset;
    preview->clipped_samples = clipped_samples;
    preview->valid = 1;
    set_error(error, error_size, "");
    return 1;
}

int ts_dsp_transform_apply_preview(TsInstrument *instrument,
                                   const TsDspTransformPreview *preview,
                                   TsTransformScope scope, int preset_slot,
                                   const TsProcessRecipe *process,
                                   uint64_t render_generation,
                                   char *error, size_t error_size)
{
    if (preview == NULL || !preview->valid || preview->sample.data == NULL) {
        set_error(error, error_size, "RENDER A VALID DSP PREVIEW BEFORE APPLY");
        return 0;
    }
    if (!ts_dsp_transform_identity_matches(&preview->identity, instrument, scope,
                                           preset_slot, process,
                                           render_generation,
                                           error, error_size)) return 0;
    return ts_instrument_apply_rendered_replacement(
        instrument, &preview->sample, preview->replacement_first,
        preview->replacement_last, error, error_size);
}

int ts_dsp_transform_apply_direct(TsInstrument *instrument, int preset_slot,
                                  const TsProcessRecipe *process,
                                  TsTransformScope scope,
                                  char *error, size_t error_size)
{
    TsDspTransformIdentity identity;
    TsDspTransformPreview preview;
    TsSample input;
    TsSample rendered;
    TsCdpSafetyStatus safety = TS_CDP_SAFETY_INVALID;
    float peak = 0.0f;
    double dc = 0.0;
    int clipped = 0;
    int ok;
    ts_sample_init(&input);
    ts_sample_init(&rendered);
    ts_dsp_transform_preview_init(&preview);
    ok = ts_dsp_transform_identity_capture(&identity, instrument, scope,
                                            preset_slot, process, 1u, 1u,
                                            error, error_size) &&
         ts_dsp_transform_extract_input(instrument, &identity, &input,
                                        error, error_size) &&
         ts_dsp_transform_render(&input, process, &rendered, &safety,
                                 &peak, &dc, &clipped, error, error_size) &&
         ts_dsp_transform_prepare_preview(instrument, &identity, &rendered,
                                          safety, peak, dc, clipped, &preview,
                                          error, error_size) &&
         ts_dsp_transform_apply_preview(instrument, &preview, scope,
                                        preset_slot, process, 1u,
                                        error, error_size);
    ts_dsp_transform_preview_free(&preview);
    ts_sample_free(&rendered);
    ts_sample_free(&input);
    return ok;
}
