#ifndef TAPESISTER_MATRIX_EFFECT_TYPES_H
#define TAPESISTER_MATRIX_EFFECT_TYPES_H
#include "tapesister/sister_post_fx.h"
#include "tapesister/compressor.h"

enum { TS_MATRIX_FX_COMPRESSOR=TS_SISTER_FX_TYPE_COUNT, TS_MATRIX_FX_TYPES };
typedef struct {
    uint32_t id; /* Never reused within the project; zero means unused port. */
    int type,enabled;
    float a,b,c,mix,gain_db,transition;
    TsCompressorControls compressor;
} TsMatrixEffectControls;
void ts_matrix_effect_default(TsMatrixEffectControls *c,int type,uint32_t id);
int ts_matrix_effect_valid(const TsMatrixEffectControls *c);
const char *ts_matrix_effect_name(int type);
const char *ts_matrix_effect_short_name(int type);
#endif
