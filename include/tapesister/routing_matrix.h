#ifndef TAPESISTER_ROUTING_MATRIX_H
#define TAPESISTER_ROUTING_MATRIX_H
#include "tapesister/source_route.h"
#include <stdio.h>

/* Stable node/port IDs. Hardware ports are physical, never speaker-array slots. */
enum { TS_MATRIX_MAIN, TS_MATRIX_SISTER, TS_MATRIX_PRISM, TS_MATRIX_BOARD,
       TS_MATRIX_FALLOUT, TS_MATRIX_EQ, TS_MATRIX_LIMITER, TS_MATRIX_NODES };
enum { TS_MATRIX_INPUTS=4, TS_MATRIX_OUTPUTS=8,
       TS_MATRIX_FM=TS_MATRIX_NODES+TS_MATRIX_INPUTS, TS_MATRIX_LINK,
       TS_MATRIX_ROWS, TS_MATRIX_DELAY=64, TS_MATRIX_NAME=24 };
typedef struct {
    int enabled, feedback;
    TsSourceRoute row[TS_MATRIX_ROWS];
    unsigned delayed[TS_MATRIX_NODES];
    char input_name[TS_MATRIX_INPUTS][TS_MATRIX_NAME];
    char output_name[TS_MATRIX_OUTPUTS][TS_MATRIX_NAME];
} TsMatrixControls;
typedef struct {
    TsMatrixControls controls;
    TsSourceRouteVoice voice[TS_MATRIX_ROWS];
    int order[TS_MATRIX_NODES], ready;
    unsigned position;
    TsStereoFrame history[TS_MATRIX_NODES][TS_MATRIX_DELAY];
    float input_peak[TS_MATRIX_NODES], output_peak[TS_MATRIX_ROWS], decay;
    TsStereoFrame output[TS_MATRIX_OUTPUTS];
} TsRoutingMatrix;
typedef TsStereoFrame (*TsMatrixProcess)(void *,int,TsStereoFrame);
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
/* Translate a source's old routing into the matrix without changing the source. */
TsSourceRoute ts_matrix_source_route(TsSourceRoute route);
#endif
