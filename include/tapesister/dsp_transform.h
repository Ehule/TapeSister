#ifndef TAPESISTER_DSP_TRANSFORM_H
#define TAPESISTER_DSP_TRANSFORM_H

#include <stddef.h>
#include <stdint.h>

#include "tapesister/recipe.h"
#include "tapesister/transform.h"

typedef struct {
    uint64_t job_id;
    uint64_t render_generation;
    int tile_slot;
    uint64_t audio_revision;
    size_t tile_frames;
    size_t selection_first;
    size_t selection_last;
    int has_selection;
    TsTransformScope scope;
    int preset_slot;
    TsProcessRecipe process;
} TsDspTransformIdentity;

typedef struct {
    TsDspTransformIdentity identity;
    TsSample sample;
    size_t replacement_first;
    size_t replacement_last;
    TsCdpSafetyStatus safety;
    float peak;
    double dc_offset;
    int clipped_samples;
    int valid;
} TsDspTransformPreview;

void ts_dsp_transform_preview_init(TsDspTransformPreview *preview);
void ts_dsp_transform_preview_free(TsDspTransformPreview *preview);
int ts_dsp_transform_identity_capture(TsDspTransformIdentity *identity,
                                      const TsInstrument *instrument,
                                      TsTransformScope scope, int preset_slot,
                                      const TsProcessRecipe *process,
                                      uint64_t job_id,
                                      uint64_t render_generation,
                                      char *error, size_t error_size);
int ts_dsp_transform_identity_matches(const TsDspTransformIdentity *identity,
                                      const TsInstrument *instrument,
                                      TsTransformScope scope, int preset_slot,
                                      const TsProcessRecipe *process,
                                      uint64_t render_generation,
                                      char *error, size_t error_size);
int ts_dsp_transform_extract_input(const TsInstrument *instrument,
                                   const TsDspTransformIdentity *identity,
                                   TsSample *input,
                                   char *error, size_t error_size);
int ts_dsp_transform_render(const TsSample *input,
                            const TsProcessRecipe *process,
                            TsSample *output,
                            TsCdpSafetyStatus *safety,
                            float *peak, double *dc_offset,
                            int *clipped_samples,
                            char *error, size_t error_size);
int ts_dsp_transform_prepare_preview(const TsInstrument *instrument,
                                     const TsDspTransformIdentity *identity,
                                     const TsSample *rendered,
                                     TsCdpSafetyStatus safety,
                                     float peak, double dc_offset,
                                     int clipped_samples,
                                     TsDspTransformPreview *preview,
                                     char *error, size_t error_size);
int ts_dsp_transform_apply_preview(TsInstrument *instrument,
                                   const TsDspTransformPreview *preview,
                                   TsTransformScope scope, int preset_slot,
                                   const TsProcessRecipe *process,
                                   uint64_t render_generation,
                                   char *error, size_t error_size);
int ts_dsp_transform_apply_direct(TsInstrument *instrument, int preset_slot,
                                  const TsProcessRecipe *process,
                                  TsTransformScope scope,
                                  char *error, size_t error_size);

#endif
