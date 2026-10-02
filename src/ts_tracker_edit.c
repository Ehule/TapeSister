#include "tapesister/tracker_edit.h"
#include <stdlib.h>
#include <string.h>

static void discard(TsTrackerUndo *u)
{ free(u->before);free(u->after);memset(u,0,sizeof(*u)); }
TsTrackerEdit *ts_tracker_edit_new(void)
{ TsTrackerEdit *e=calloc(1,sizeof(*e));if(e)e->mask=TS_TRACKER_EDIT_ALL;return e; }
void ts_tracker_edit_reset(TsTrackerEdit *e)
{
    if(!e)return;
    for(unsigned i=0;i<e->count;++i)discard(&e->history[i]);
    discard(&e->pending);memset(e,0,sizeof(*e));e->mask=TS_TRACKER_EDIT_ALL;
}
void ts_tracker_edit_free(TsTrackerEdit *e)
{ if(e){ts_tracker_edit_reset(e);free(e);} }
void ts_tracker_edit_anchor(TsTrackerEdit *e,const TsSisterTracker *t)
{ e->anchor_row=t->editor_row;e->anchor_lane=t->editor_lane;e->selected=0; }
TsTrackerRegion ts_tracker_edit_region(const TsTrackerEdit *e,const TsSisterTracker *t)
{
    const TsTrackerPattern *p=ts_sister_tracker_pattern_const(t,t->editor_pattern);
    int row=t->editor_row,lane=t->editor_lane;
    int a=e && e->selected?e->anchor_row:row,b=e && e->selected?e->anchor_lane:lane;
    TsTrackerRegion r={a<row?a:row,a>row?a:row,b<lane?b:lane,b>lane?b:lane};
    int max=p?p->rows-1:0;
    if(r.row0<0)r.row0=0;
    if(r.row0>max)r.row0=max;
    if(r.row1>max)r.row1=max;
    if(r.lane0<0)r.lane0=0;
    if(r.lane1>=TS_TRACKER_LANES)r.lane1=TS_TRACKER_LANES-1;
    return r;
}
int ts_tracker_edit_begin(TsTrackerEdit *e,const TsTrackerPattern *p)
{
    if(!e || !p)return 0;
    if(e->pending.before)return e->pending.before->id==p->id;
    e->pending.before=malloc(sizeof(*p));e->pending.after=malloc(sizeof(*p));
    if(!e->pending.before || !e->pending.after){discard(&e->pending);return 0;}
    *e->pending.before=*p;return 1;
}
void ts_tracker_edit_commit(TsTrackerEdit *e,const TsTrackerPattern *p)
{
    if(!e || !e->pending.before)return;
    if(!p || e->pending.before->id!=p->id || !memcmp(e->pending.before,p,sizeof(*p))) {
        discard(&e->pending);return;
    }
    *e->pending.after=*p;
    for(unsigned i=e->position;i<e->count;++i)discard(&e->history[i]);
    e->count=e->position;
    if(e->count==TS_TRACKER_UNDO_LIMIT) {
        discard(&e->history[0]);memmove(e->history,e->history+1,(TS_TRACKER_UNDO_LIMIT-1)*sizeof(*e->history));
        --e->count;
    }
    e->history[e->count++]=e->pending;memset(&e->pending,0,sizeof(e->pending));e->position=e->count;
}
int ts_tracker_edit_undo(TsTrackerEdit *e,TsSisterTracker *t,int redo)
{
    if(!e || e->pending.before || (redo?e->position==e->count:!e->position))return 0;
    TsTrackerUndo *u=&e->history[redo?e->position:e->position-1];
    const TsTrackerPattern *saved=redo?u->after:u->before;
    TsTrackerPattern *p=ts_sister_tracker_pattern(t,saved->id);if(!p)return 0;
    *p=*saved;e->position+=redo?1:-1;t->editor_pattern=p->id;
    if(t->editor_row>=p->rows)t->editor_row=p->rows-1;
    ts_tracker_edit_anchor(e,t);return 1;
}
static void assign(TsTrackerCell *to,const TsTrackerCell *from,unsigned mask,int mix)
{
    if((mask&TS_TRACKER_EDIT_NOTE) && (!mix || from->note_kind)) {
        to->note_kind=from->note_kind;to->note=from->note;
    }
    if((mask&TS_TRACKER_EDIT_TILE) && (!mix || from->tile_id))to->tile_id=from->tile_id;
    if((mask&TS_TRACKER_EDIT_VOLUME) && (!mix || from->has_volume)) {
        to->has_volume=from->has_volume;to->volume=from->volume;
    }
    if((mask&TS_TRACKER_EDIT_TUNE) && (!mix || from->tune_command)) {
        to->tune_command=from->tune_command;to->tune_value=from->tune_value;
    }
    if((mask&TS_TRACKER_EDIT_FX) && (!mix || from->fx_command)) {
        to->fx_command=from->fx_command;to->fx_value=from->fx_value;
    }
}
void ts_tracker_edit_copy(TsTrackerEdit *e,const TsTrackerPattern *p,TsTrackerRegion r)
{
    e->clipboard_rows=r.row1-r.row0+1;e->clipboard_lanes=r.lane1-r.lane0+1;
    for(unsigned row=0;row<e->clipboard_rows;++row)
        for(unsigned lane=0;lane<e->clipboard_lanes;++lane)e->clipboard[row][lane]=p->cells[r.row0+row][r.lane0+lane];
}
void ts_tracker_edit_clear(TsTrackerPattern *p,TsTrackerRegion r,unsigned mask)
{
    const TsTrackerCell empty={0};
    for(int row=r.row0;row<=r.row1;++row)for(int lane=r.lane0;lane<=r.lane1;++lane)assign(&p->cells[row][lane],&empty,mask,0);
}
void ts_tracker_edit_paste(const TsTrackerEdit *e,TsTrackerPattern *p,int row,int lane,int mix)
{
    for(unsigned y=0;y<e->clipboard_rows && row+(int)y<p->rows;++y)
        for(unsigned x=0;x<e->clipboard_lanes && lane+(int)x<TS_TRACKER_LANES;++x)
            assign(&p->cells[row+y][lane+x],&e->clipboard[y][x],e->mask,mix);
}
void ts_tracker_edit_shift_rows(TsTrackerPattern *p,int row,int lane,int whole,int insert)
{
    for(int x=whole?0:lane;x<=(whole?TS_TRACKER_LANES-1:lane);++x) {
        if(insert) {
            for(int y=p->rows-1;y>row;--y)p->cells[y][x]=p->cells[y-1][x];
            memset(&p->cells[row][x],0,sizeof(TsTrackerCell));
        } else {
            for(int y=row;y<p->rows-1;++y)p->cells[y][x]=p->cells[y+1][x];
            memset(&p->cells[p->rows-1][x],0,sizeof(TsTrackerCell));
        }
    }
}
void ts_tracker_edit_transpose(TsTrackerPattern *p,TsTrackerRegion r,int delta)
{
    for(int y=r.row0;y<=r.row1;++y)for(int x=r.lane0;x<=r.lane1;++x) {
        TsTrackerCell *c=&p->cells[y][x];if(c->note_kind!=TS_TRACKER_NOTE_PITCH)continue;
        int n=c->note+delta;c->note=n<0?0:n>127?127:n;
    }
}
int ts_tracker_edit_interpolate(TsTrackerPattern *p,TsTrackerRegion r)
{
    if(r.row0==r.row1)return 0;
    /* Validate all endpoints before changing any lane. Explicit 00 is valid. */
    for(int x=r.lane0;x<=r.lane1;++x)
        if(!p->cells[r.row0][x].has_volume || !p->cells[r.row1][x].has_volume)return 0;
    int length=r.row1-r.row0;
    for(int x=r.lane0;x<=r.lane1;++x) {
        int a=p->cells[r.row0][x].volume,b=p->cells[r.row1][x].volume;
        for(int y=r.row0;y<=r.row1;++y) {
            TsTrackerCell *c=&p->cells[y][x];c->has_volume=1;
            c->volume=(a*(length-y+r.row0)+b*(y-r.row0)+length/2)/length;
        }
    }return 1;
}
void ts_tracker_edit_fill(TsTrackerPattern *p,TsTrackerRegion r,unsigned mask)
{
    for(int x=r.lane0;x<=r.lane1;++x)for(int y=r.row0+1;y<=r.row1;++y)
        assign(&p->cells[y][x],&p->cells[r.row0][x],mask,0);
}
void ts_tracker_edit_reverse(TsTrackerPattern *p,TsTrackerRegion r,unsigned mask)
{
    for(int x=r.lane0;x<=r.lane1;++x)for(int a=r.row0,b=r.row1;a<b;++a,--b) {
        TsTrackerCell saved=p->cells[a][x];assign(&p->cells[a][x],&p->cells[b][x],mask,0);
        assign(&p->cells[b][x],&saved,mask,0);
    }
}
