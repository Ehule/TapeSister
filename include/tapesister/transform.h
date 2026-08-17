#ifndef TAPESISTER_TRANSFORM_H
#define TAPESISTER_TRANSFORM_H

#include <stddef.h>
#include <stdint.h>

#include "tapesister/cdp_adapter.h"
#include "tapesister/cdp_recipe.h"
#include "tapesister/sample.h"

typedef enum {
    TS_TRANSFORM_SELECTION = 0,
    TS_TRANSFORM_WHOLE
} TsTransformScope;

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
    char recipe_id[32];
    uint32_t recipe_schema_version;
    uint32_t recipe_version;
    TsCdpRecipeValues values;
    uint64_t seed;
    int has_seed;
    uint32_t input_sample_rate;
    uint16_t input_channels;
    size_t expected_stage_count;
    TsCdpIoType expected_output_type;
} TsTransformIdentity;

typedef struct {
    TsTransformIdentity identity;
    TsSample sample;
    size_t replacement_first;
    size_t replacement_last;
    TsCdpSafetyStatus safety;
    float peak;
    double dc_offset;
    int clipped_samples;
    int valid;
} TsTransformPreview;

void ts_transform_preview_init(TsTransformPreview *preview);
void ts_transform_preview_free(TsTransformPreview *preview);
int ts_transform_identity_capture(TsTransformIdentity *identity,
                                  const TsInstrument *instrument,
                                  TsTransformScope scope,
                                  const TsCdpRecipe *recipe,
                                  const TsCdpRecipeValues *values,
                                  uint64_t job_id,
                                  uint64_t render_generation,
                                  char *error, size_t error_size);
int ts_transform_identity_matches(const TsTransformIdentity *identity,
                                  const TsInstrument *instrument,
                                  TsTransformScope scope,
                                  const TsCdpRecipe *recipe,
                                  const TsCdpRecipeValues *values,
                                  uint64_t render_generation,
                                  char *error, size_t error_size);
int ts_transform_extract_input(const TsInstrument *instrument,
                               const TsTransformIdentity *identity,
                               TsSample *input,
                               char *error, size_t error_size);
int ts_transform_prepare_preview(const TsInstrument *instrument,
                                 const TsTransformIdentity *identity,
                                 const TsCdpRecipe *recipe,
                                 const TsCdpRunResult *render,
                                 TsTransformPreview *preview,
                                 char *error, size_t error_size);
int ts_transform_apply_preview(TsInstrument *instrument,
                               const TsTransformPreview *preview,
                               TsTransformScope scope,
                               const TsCdpRecipe *recipe,
                               const TsCdpRecipeValues *values,
                               uint64_t render_generation,
                               char *error, size_t error_size);
int ts_transform_mix_samples(const TsSample *dry, const TsSample *wet,
                             float mix, TsCdpMixPolicy policy,
                             TsSample *output,
                             char *error, size_t error_size);
void ts_transform_boundary_splice(TsSample *replacement, const TsSample *tile,
                                  size_t first, size_t last);

#endif
