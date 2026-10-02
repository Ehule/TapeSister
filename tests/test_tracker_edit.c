#ifdef NDEBUG
#undef NDEBUG
#endif
#include "tapesister/tracker_edit.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static TsTrackerCell note(unsigned pitch,TsTileId tile)
{
    TsTrackerCell c={0};c.note_kind=TS_TRACKER_NOTE_PITCH;c.note=pitch;c.tile_id=tile;
    c.has_volume=1;c.volume=0;c.tune_command='N';c.tune_value=0x80;c.fx_command='Z';return c;
}
int main(void)
{
    TsSisterTracker *t=malloc(sizeof(*t));TsTrackerEdit *e=ts_tracker_edit_new();assert(t && e);
    ts_sister_tracker_init(t);char error[128];TsPatternId id,other;
    assert(ts_sister_tracker_add_pattern(t,8,&id,error,sizeof(error)));
    assert(ts_sister_tracker_add_pattern(t,8,&other,error,sizeof(error)));t->editor_pattern=id;
    TsTrackerPattern *p=ts_sister_tracker_pattern(t,id),*q=ts_sister_tracker_pattern(t,other);
    TsTileId tile=UINT64_C(0x123456789abc);p->cells[1][2]=note(0,tile);p->cells[2][2]=note(127,tile);
    p->cells[200][2]=note(61,tile);TsTrackerCell hidden=p->cells[200][2];
    t->editor_row=1;t->editor_lane=2;ts_tracker_edit_anchor(e,t);e->selected=1;
    t->editor_row=2;t->editor_lane=3;TsTrackerRegion block=ts_tracker_edit_region(e,t);
    assert(block.row0==1 && block.row1==2 && block.lane0==2 && block.lane1==3);
    ts_tracker_edit_copy(e,p,block);assert(e->clipboard_rows==2 && e->clipboard_lanes==2);
    assert(ts_tracker_edit_begin(e,q));ts_tracker_edit_paste(e,q,7,7,0);ts_tracker_edit_commit(e,q);
    /* Paste clips to active bounds, preserving MIDI 0, volume 00 and Z00. */
    assert(q->cells[7][7].tile_id==tile && q->cells[7][7].note_kind==TS_TRACKER_NOTE_PITCH);
    assert(q->cells[7][7].note==0 && q->cells[7][7].has_volume && !q->cells[7][7].volume);
    assert(q->cells[7][7].fx_command=='Z' && !q->cells[7][7].fx_value);
    assert(!q->cells[8][7].tile_id);
    assert(ts_tracker_edit_undo(e,t,0));assert(!q->cells[7][7].tile_id && t->editor_pattern==other);
    assert(ts_tracker_edit_undo(e,t,1));assert(q->cells[7][7].tile_id==tile);
    /* Field masks cannot disturb another column; mix treats explicit zero as data. */
    e->mask=TS_TRACKER_EDIT_VOLUME|TS_TRACKER_EDIT_FX;q->cells[0][0]=note(70,999);
    ts_tracker_edit_paste(e,q,0,0,1);assert(q->cells[0][0].tile_id==999 && q->cells[0][0].note==70);
    assert(q->cells[0][0].has_volume && !q->cells[0][0].volume && q->cells[0][0].fx_command=='Z');
    e->clipboard[0][0]=(TsTrackerCell){0};ts_tracker_edit_paste(e,q,0,0,1);
    assert(q->cells[0][0].has_volume && q->cells[0][0].fx_command=='Z');
    ts_tracker_edit_clear(q,(TsTrackerRegion){0,0,0,0},TS_TRACKER_EDIT_VOLUME);
    assert(!q->cells[0][0].has_volume && q->cells[0][0].tile_id==999);
    /* Row shifts affect a chosen lane or all lanes, never stored hidden rows. */
    assert(ts_tracker_edit_begin(e,p));ts_tracker_edit_shift_rows(p,1,2,0,1);ts_tracker_edit_commit(e,p);
    assert(!p->cells[1][2].note_kind && p->cells[2][2].note==0 && p->cells[3][2].note==127);
    assert(!memcmp(&p->cells[200][2],&hidden,sizeof(hidden)));
    assert(ts_tracker_edit_undo(e,t,0));assert(p->cells[1][2].note_kind==TS_TRACKER_NOTE_PITCH && p->cells[2][2].note==127);
    assert(ts_tracker_edit_begin(e,p));ts_tracker_edit_shift_rows(p,1,2,1,0);ts_tracker_edit_commit(e,p);
    assert(p->cells[1][2].note==127 && !p->cells[7][2].note_kind && !memcmp(&p->cells[200][2],&hidden,sizeof(hidden)));
    assert(!ts_tracker_edit_undo(e,t,1)); /* New edits discard the redo branch. */
    p->cells[0][0]=note(0,tile);p->cells[1][0]=note(127,tile);
    p->cells[2][0].note_kind=TS_TRACKER_NOTE_OFF;p->cells[3][0].note_kind=TS_TRACKER_NOTE_CUT;
    ts_tracker_edit_transpose(p,(TsTrackerRegion){0,3,0,0},12);
    assert(p->cells[0][0].note==12 && p->cells[1][0].note==127 && p->cells[2][0].note_kind==TS_TRACKER_NOTE_OFF);
    ts_tracker_edit_transpose(p,(TsTrackerRegion){0,3,0,0},-24);assert(!p->cells[0][0].note);
    /* Interpolation rejects the whole operation when any endpoint is absent. */
    p->cells[0][0].volume=0;p->cells[4][0].has_volume=1;p->cells[4][0].volume=64;
    assert(!ts_tracker_edit_interpolate(p,(TsTrackerRegion){0,4,0,1}));assert(!p->cells[2][0].has_volume);
    assert(ts_tracker_edit_interpolate(p,(TsTrackerRegion){0,4,0,0}));
    assert(p->cells[1][0].volume==16 && p->cells[2][0].volume==32 && p->cells[3][0].volume==48);
    ts_tracker_edit_fill(p,(TsTrackerRegion){0,4,0,0},TS_TRACKER_EDIT_TILE);assert(p->cells[4][0].tile_id==tile && p->cells[4][0].volume==64);
    ts_tracker_edit_reverse(p,(TsTrackerRegion){0,4,0,0},TS_TRACKER_EDIT_VOLUME);assert(p->cells[0][0].volume==64 && !p->cells[4][0].volume);
    /* Resizing undo restores hidden data and clamps editor focus; definitions
       alone are in history (aliases/tempo remain project-wide). */
    assert(ts_tracker_edit_begin(e,p));assert(ts_sister_tracker_set_rows(t,id,2));ts_tracker_edit_commit(e,p);
    t->aliases[1]=tile;t->bpm=160;t->editor_row=7;
    assert(ts_tracker_edit_undo(e,t,0));assert(p->rows==8 && p->cells[200][2].tile_id==tile);
    assert(ts_tracker_edit_undo(e,t,1));assert(p->rows==2 && t->editor_row==1 && t->bpm==160 && t->aliases[1]==tile);
    ts_tracker_edit_reset(e);assert(!e->count && !e->clipboard_rows && e->mask==TS_TRACKER_EDIT_ALL);
    for(int i=0;i<TS_TRACKER_UNDO_LIMIT+5;++i) {
        assert(ts_tracker_edit_begin(e,p));p->cells[0][0].note=i;ts_tracker_edit_commit(e,p);
    }
    assert(e->count==TS_TRACKER_UNDO_LIMIT);
    for(int i=0;i<TS_TRACKER_UNDO_LIMIT;++i)assert(ts_tracker_edit_undo(e,t,0));
    assert(!ts_tracker_edit_undo(e,t,0) && p->cells[0][0].note==4);
    ts_tracker_edit_free(e);ts_sister_tracker_free(t);free(t);puts("SisterTracker editing checks passed");return 0;
}
