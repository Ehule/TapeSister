#ifndef TAPESISTER_MATRIX_LAYOUT_H
#define TAPESISTER_MATRIX_LAYOUT_H

#include "tapesister/routing_matrix.h"
#include "tapesister/mosaic_gallery.h"
#include "tapesister/sister_tracker.h"

/* Presentation metadata only. Neither the graph nor the audio callback reads it.
   Tracks and fixed buses use their index; tiles/cards/effect instances use their stable identity. */
enum { TS_MATRIX_LAYOUT_PAGES=1024,
       TS_MATRIX_LAYOUT_ROWS=TS_TRACKER_LANES+TS_MATRIX_LAYOUT_PAGES*TS_BANK_SLOT_COUNT+TS_MATRIX_ROWS+TS_MOSAIC_EVENTS };
/* Keep room for all processors and interface endpoints even when the player
   has positioned the full complement of individual track/tile/card sources. */
enum { TS_FX_CANVAS_SOURCE_POSITIONS=TS_GALLERY_LIMIT*TS_MOSAIC_EVENTS+256,
       TS_FX_CANVAS_POSITIONS=TS_FX_CANVAS_SOURCE_POSITIONS+TS_MATRIX_ROWS+TS_MATRIX_DESTINATIONS };
enum { TS_FX_CANVAS_SPLIT_MIN=640, TS_FX_CANVAS_SPLIT_DEFAULT=1000, TS_FX_CANVAS_SPLIT_MAX=1040 };
/* Zero scale is the legacy/default size; saved custom sizes use .25..4. */
typedef struct { uint64_t id; int kind; float x,y,scale; uint64_t canvas; } TsFxCanvasPosition;
enum { TS_FX_LABEL_LIMIT=64, TS_FX_LABEL_TEXT=256 };
typedef struct {float x,y,scale;char text[TS_FX_LABEL_TEXT];} TsFxCanvasLabel;
typedef struct {
    uint64_t row_id[TS_MATRIX_LAYOUT_ROWS];
    unsigned char row_kind[TS_MATRIX_LAYOUT_ROWS];
    int row_count;
    unsigned char column[TS_MATRIX_DESTINATIONS];
    /* View metadata; stable effect IDs survive removal and slot reuse. */
    int canvas_count,canvas_view,canvas_straight,canvas_snap,canvas_split;
    float canvas_x,canvas_y,canvas_zoom;
    TsFxCanvasPosition canvas[TS_FX_CANVAS_POSITIONS];
    int label_count;
    TsFxCanvasLabel labels[TS_FX_LABEL_LIMIT];
} TsMatrixLayout;

void ts_matrix_layout_default(TsMatrixLayout *layout);
int ts_matrix_layout_valid(const TsMatrixLayout *layout);
int ts_matrix_layout_write(FILE *file,const TsMatrixLayout *layout);
int ts_matrix_layout_read(TsMatrixLayout *layout,const char *key,const char *value);
#endif
