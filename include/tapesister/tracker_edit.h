#ifndef TAPESISTER_TRACKER_EDIT_H
#define TAPESISTER_TRACKER_EDIT_H
#include "tapesister/sister_tracker.h"

enum { TS_TRACKER_EDIT_NOTE=1, TS_TRACKER_EDIT_TILE=2, TS_TRACKER_EDIT_VOLUME=4,
       TS_TRACKER_EDIT_TUNE=8, TS_TRACKER_EDIT_FX=16, TS_TRACKER_EDIT_ALL=31,
       TS_TRACKER_UNDO_LIMIT=32 };
typedef struct { int row0,row1,lane0,lane1; } TsTrackerRegion;
typedef struct { TsTrackerPattern *before,*after; } TsTrackerUndo;
/* Control-thread editor state. Never shared with the audio callback or saved. */
typedef struct TsTrackerEdit {
    TsTrackerUndo history[TS_TRACKER_UNDO_LIMIT],pending;
    unsigned count,position,mask;
    int selected,anchor_row,anchor_lane,dragging,tools_open;
    TsPatternId selected_pattern;
    int end_row,end_lane,drag_row,drag_lane;
    unsigned clipboard_rows,clipboard_lanes;
    TsTrackerCell clipboard[TS_TRACKER_ROWS][TS_TRACKER_LANES];
} TsTrackerEdit;
TsTrackerEdit *ts_tracker_edit_new(void);
void ts_tracker_edit_reset(TsTrackerEdit *edit);
void ts_tracker_edit_free(TsTrackerEdit *edit);
TsTrackerRegion ts_tracker_edit_region(const TsTrackerEdit *edit,const TsSisterTracker *tracker);
void ts_tracker_edit_anchor(TsTrackerEdit *edit,const TsSisterTracker *tracker);
int ts_tracker_edit_selected(const TsTrackerEdit *edit,const TsSisterTracker *tracker);
void ts_tracker_edit_mark(TsTrackerEdit *edit,const TsSisterTracker *tracker,int row,int lane);
int ts_tracker_edit_extract(TsTrackerEdit *edit,TsSisterTracker *tracker,TsPatternId *created,char *error,size_t size);
int ts_tracker_edit_scale(TsTrackerPattern *pattern,int expand);
int ts_tracker_edit_begin(TsTrackerEdit *edit,const TsTrackerPattern *pattern);
void ts_tracker_edit_commit(TsTrackerEdit *edit,const TsTrackerPattern *pattern);
int ts_tracker_edit_undo(TsTrackerEdit *edit,TsSisterTracker *tracker,int redo);
void ts_tracker_edit_copy(TsTrackerEdit *edit,const TsTrackerPattern *pattern,TsTrackerRegion region);
void ts_tracker_edit_clear(TsTrackerPattern *pattern,TsTrackerRegion region,unsigned mask);
void ts_tracker_edit_paste(const TsTrackerEdit *edit,TsTrackerPattern *pattern,int row,int lane,int mix);
void ts_tracker_edit_shift_rows(TsTrackerPattern *pattern,int row,int lane,int whole,int insert);
void ts_tracker_edit_transpose(TsTrackerPattern *pattern,TsTrackerRegion region,int semitones);
int ts_tracker_edit_interpolate(TsTrackerPattern *pattern,TsTrackerRegion region);
void ts_tracker_edit_fill(TsTrackerPattern *pattern,TsTrackerRegion region,unsigned mask);
void ts_tracker_edit_reverse(TsTrackerPattern *pattern,TsTrackerRegion region,unsigned mask);
#endif
