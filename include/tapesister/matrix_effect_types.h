#ifndef TAPESISTER_MATRIX_EFFECT_TYPES_H
#define TAPESISTER_MATRIX_EFFECT_TYPES_H
#include "tapesister/sister_post_fx.h"
#include "tapesister/compressor.h"
#include "tapesister/meta_fx.h"
#include "tapesister/sunvox_fx.h"

enum { TS_MATRIX_FX_COMPRESSOR=6, TS_MATRIX_FX_FILTER=7, TS_MATRIX_FX_DISPERSION=8, TS_MATRIX_FX_RESONATOR=9, TS_MATRIX_FX_META=10, TS_MATRIX_FX_SUNVOX=11, TS_MATRIX_FX_TYPES=12 };
typedef struct {
    uint32_t id; /* Never reused within the project; zero means unused port. */
    int type,enabled;
    float a,b,c,mix,gain_db,transition;
    TsCompressorControls compressor;
    uint8_t transient_power;
    TsGrainStates transient_states;
    TsCharacterFilterControls filter;
    TsMetaControls meta;
    TsSunVoxControls sunvox;
} TsMatrixEffectControls;
void ts_matrix_effect_default(TsMatrixEffectControls *c,int type,uint32_t id);
int ts_matrix_effect_valid(const TsMatrixEffectControls *c);
const char *ts_matrix_effect_name(int type);
const char *ts_matrix_effect_short_name(int type);
#endif
