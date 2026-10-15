#ifndef TAPESISTER_ROUTING_MATRIX_H
#define TAPESISTER_ROUTING_MATRIX_H
#include "tapesister/source_route.h"
#include "tapesister/matrix_effect_types.h"
#include <stdio.h>

/* Saved IDs 0..6 are the original processors, destinations 7..14 are physical
   outputs. Source rows 7..14 are the original inputs/buses. Compressor is
   appended at row/destination 15; 16 is a detector-only destination and,
   independently, the TrackSister source bus row. Destination IDs are unchanged.
   TS_MATRIX_NODES remains the legacy output/input offset, not a node count. */
enum { TS_MATRIX_MAIN, TS_MATRIX_SISTER, TS_MATRIX_PRISM, TS_MATRIX_BOARD,
       TS_MATRIX_FALLOUT, TS_MATRIX_EQ, TS_MATRIX_LIMITER, TS_MATRIX_NODES };
enum { TS_MATRIX_INPUTS=4, TS_MATRIX_OUTPUTS=8,
       TS_MATRIX_FM=TS_MATRIX_NODES+TS_MATRIX_INPUTS, TS_MATRIX_LINK, TS_MATRIX_MOSAIC, TS_MATRIX_TILES,
       TS_MATRIX_COMPRESSOR, TS_MATRIX_TRACKER, TS_MATRIX_SHARED_ROWS, TS_MATRIX_SIDECHAIN=16,
       TS_MATRIX_EFFECT_BASE=17, TS_MATRIX_ROWS=TS_MATRIX_EFFECT_BASE+TS_MATRIX_EFFECT_CAPACITY,
       TS_MATRIX_EFFECT_SC_BASE=TS_MATRIX_ROWS,
       TS_MATRIX_PROCESSORS=8+TS_MATRIX_EFFECT_CAPACITY, TS_MATRIX_DELAY=64, TS_MATRIX_NAME=24 };
typedef struct {
    int enabled, feedback;
    /* Imported serial effects retain their full dry/wet output. Bit MAIN
       retains the ordinary program input ceiling; other nodes stay wet-only. */
    unsigned serial_mask;
    /* Explicit full-signal pedalboard mode; independent of legacy imports. */
    int board_insert;
    TsSourceRoute row[TS_MATRIX_ROWS];
    uint64_t delayed[TS_MATRIX_ROWS];
    uint32_t next_effect_id;
    TsMatrixEffectControls effect[TS_MATRIX_EFFECT_CAPACITY];
    char input_name[TS_MATRIX_INPUTS][TS_MATRIX_NAME];
    char output_name[TS_MATRIX_OUTPUTS][TS_MATRIX_NAME];
} TsMatrixControls;
typedef struct {
    TsMatrixControls controls;
    TsSourceRouteVoice voice[TS_MATRIX_ROWS];
    int order[TS_MATRIX_PROCESSORS], ready;
    unsigned position;
    TsStereoFrame history[TS_MATRIX_ROWS][TS_MATRIX_DELAY];
    float input_peak[TS_MATRIX_DESTINATIONS], output_peak[TS_MATRIX_ROWS], decay;
    float board_insert_current, mode_follow;
    float hardware_peak[TS_MATRIX_OUTPUTS]; /* After OUT gain and hardware guard. */
    TsStereoFrame output[TS_MATRIX_OUTPUTS], sidechain, spatial_input;
    float speaker_output[TS_SOURCE_SPEAKERS];
} TsRoutingMatrix;
typedef TsStereoFrame (*TsMatrixProcess)(void *,int,TsStereoFrame);
/* Only the original processor/sidechain destinations can be feedback edges. */
static inline uint64_t ts_matrix_feedback_bit(int d)
{ return d>=0 && d<TS_MATRIX_INSTANCE_DESTINATIONS ? UINT64_C(1)<<d : 0; }
void ts_matrix_default(TsMatrixControls *c);
int ts_matrix_valid(const TsMatrixControls *c);
/* Returns 0 for an unapproved loop, -1 for invalid arguments, 1 on success. */
int ts_matrix_connect(TsMatrixControls *c,int row,int destination,int gain);
void ts_matrix_cut_feedback(TsMatrixControls *c);
void ts_matrix_init(TsRoutingMatrix *m);
int ts_matrix_set(TsRoutingMatrix *m,const TsMatrixControls *c,unsigned rate);
void ts_matrix_process(TsRoutingMatrix *m,TsSourceRouteMix *sums,
    const TsStereoFrame live[TS_MATRIX_ROWS-TS_MATRIX_NODES],TsMatrixProcess fn,void *context);
int ts_matrix_write(FILE *f,const TsMatrixControls *c);
int ts_matrix_read(TsMatrixControls *c,const char *key,const char *value);
const char *ts_matrix_node_name(int node);
/* IDs 0..14 retain their original saved meaning. New ports are appended. */
int ts_matrix_is_processor(int id);
int ts_matrix_is_output(int id);
int ts_matrix_processor(int index);
int ts_matrix_effect_index(int node);
int ts_matrix_sidechain_for(int node);
int ts_matrix_destination_active(const TsMatrixControls *c,int destination);
int ts_matrix_effect_add(TsMatrixControls *c,int type);
void ts_matrix_effect_remove(TsMatrixControls *c,int slot);
/* Translate a source's old routing into the matrix without changing the source. */
TsSourceRoute ts_matrix_source_route(TsSourceRoute route);
#endif
