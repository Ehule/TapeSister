#ifndef TAPESISTER_MATRIX_LAYOUT_H
#define TAPESISTER_MATRIX_LAYOUT_H

#include "tapesister/routing_matrix.h"
#include "tapesister/mosaic.h"
#include "tapesister/sister_tracker.h"

/* Presentation metadata only. Neither the graph nor the audio callback reads it.
   Tracks and fixed buses use their index; tiles/cards use their stable identity. */
enum { TS_MATRIX_LAYOUT_PAGES=1024,
       TS_MATRIX_LAYOUT_ROWS=TS_TRACKER_LANES+TS_MATRIX_LAYOUT_PAGES*TS_BANK_SLOT_COUNT+TS_MATRIX_ROWS+TS_MOSAIC_EVENTS };
typedef struct {
    uint64_t row_id[TS_MATRIX_LAYOUT_ROWS];
    unsigned char row_kind[TS_MATRIX_LAYOUT_ROWS];
    int row_count;
    unsigned char column[TS_MATRIX_DESTINATIONS];
} TsMatrixLayout;

void ts_matrix_layout_default(TsMatrixLayout *layout);
int ts_matrix_layout_valid(const TsMatrixLayout *layout);
int ts_matrix_layout_write(FILE *file,const TsMatrixLayout *layout);
int ts_matrix_layout_read(TsMatrixLayout *layout,const char *key,const char *value);
#endif
