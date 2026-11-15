#ifndef TAPESISTER_MATRIX_MODULATION_H
#define TAPESISTER_MATRIX_MODULATION_H
#include "tapesister/routing_matrix.h"
/* Missing/replaced identities remain inert and visible for repair/undo. */
int ts_mod_target(const TsMatrixControls *c,const TsModMap *m);
int ts_mod_field_available(const TsMatrixEffectControls *c,uint32_t child,unsigned field);
unsigned ts_mod_target_type(const TsMatrixEffectControls *c,uint32_t child);
void ts_mod_parameter_name(const TsMatrixEffectControls *c,uint32_t child,unsigned field,char *text,size_t size);
void ts_mod_parameter_value(const TsMatrixEffectControls *c,uint32_t child,unsigned field,float value,char *text,size_t size);
int ts_mod_apply(TsMatrixEffectControls *c,const TsModMap *map,float value);
int ts_mod_assignments_valid(const TsMatrixControls *c);
#endif
