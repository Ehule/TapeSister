#ifndef TAPESISTER_MOSAIC_LAYOUT_H
#define TAPESISTER_MOSAIC_LAYOUT_H
#include "tapesister/ui_metrics.h"

/* Presentation only. One table drives paint and inverse pointer mapping.
   Arrangement coordinates and the independently saved canvas zoom never change. */
enum { TS_MOSAIC_LAYOUT_PARTS=5 };
typedef struct {TsUiRect source[TS_MOSAIC_LAYOUT_PARTS],destination[TS_MOSAIC_LAYOUT_PARTS];} TsMosaicLayout;
static inline int ts_mosaic_layout_percent(int value)
{return value>=50 && value<=100?value:100;}
static inline TsMosaicLayout ts_mosaic_layout_make(int toolbar,int sources)
{
    toolbar=ts_mosaic_layout_percent(toolbar);sources=ts_mosaic_layout_percent(sources);
    int top=32*toolbar/100,bar=34*toolbar/100,side=113*sources/100;
    TsMosaicLayout layout={
        .source={{0,0,640,32},{0,32,113,326},{113,32,527,34},{113,66,527,292},{0,358,640,42}},
        .destination={{0,0,640*toolbar/100,top},{0,top,side,326*sources/100},
            {side,top,527*toolbar/100,bar},{side,top+bar,640-side,358-top-bar},{0,358,640,42}}};
    return layout;
}
static inline int ts_mosaic_layout_inverse(const TsMosaicLayout *layout,int *x,int *y)
{
    for(int i=0;i<TS_MOSAIC_LAYOUT_PARTS;++i) {
        TsUiRect d=layout->destination[i],s=layout->source[i];
        if(!ts_ui_rect_contains(d,*x,*y))continue;
        *x=s.x+(*x-d.x)*s.w/d.w;*y=s.y+(*y-d.y)*s.h/d.h;return 1;
    }
    *x=*y=-1;return 0;
}
#endif
