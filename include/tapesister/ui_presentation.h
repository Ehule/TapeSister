#ifndef TS_UI_PRESENTATION_H
#define TS_UI_PRESENTATION_H
#include "tapesister/ui_metrics.h"
#include <math.h>

/* Reflow the legacy Mosaic surface without changing stored world coordinates.
   Native cards/ruler use the same destination regions as pointer conversion.
   0: legacy; 1: workspace; 2: centered dialog surface. */
typedef struct {
    int mode,width,height,right,count;
    double scale;
    TsUiRect source[9],target[9];
} TsUiPresentation;
static inline void ts_ui_presentation_region(TsUiPresentation *p,TsUiRect a,TsUiRect b)
{p->source[p->count]=a;p->target[p->count++]=b;}
static inline TsUiPresentation ts_ui_presentation(int mode,int width,int height,double density,int right)
{
    TsUiPresentation p={.mode=mode,.width=width,.height=height,.right=right};
    p.scale=fmin(2*density,fmin(width/640.0,height/400.0));
    if(!mode || width<1 || height<1){p.mode=0;return p;}
    double s=p.scale;int fullw=(int)lround(640*s),fullh=(int)lround(400*s);
    if(mode==2) {
        ts_ui_presentation_region(&p,(TsUiRect){0,0,640,400},(TsUiRect){(width-fullw)/2,(height-fullh)/2,fullw,fullh});return p;
    }
    int top=(int)lround(66*s),bottom=height-(int)lround(42*s);
    int left=(int)lround(151*s),ruler=(int)lround(113*s),edge=width-(int)lround((640-right)*s);
    ts_ui_presentation_region(&p,(TsUiRect){0,0,640,66},(TsUiRect){0,0,fullw,top});
    ts_ui_presentation_region(&p,(TsUiRect){0,358,640,42},(TsUiRect){0,bottom,fullw,height-bottom});
    ts_ui_presentation_region(&p,(TsUiRect){0,66,113,292},(TsUiRect){0,top,ruler,(int)lround(292*s)});
    ts_ui_presentation_region(&p,(TsUiRect){113,66,38,292},(TsUiRect){ruler,top,left-ruler,bottom-top});
    ts_ui_presentation_region(&p,(TsUiRect){151,66,right-151,292},(TsUiRect){left,top,edge-left,bottom-top});
    /* Volume controls keep their heights; only the envelope stretches. */
    int upper=(int)lround(20*s),lower=(int)lround(23*s);
    ts_ui_presentation_region(&p,(TsUiRect){right,66,640-right,20},(TsUiRect){edge,top,width-edge,upper});
    ts_ui_presentation_region(&p,(TsUiRect){right,86,640-right,249},(TsUiRect){edge,top+upper,width-edge,bottom-top-upper-lower});
    ts_ui_presentation_region(&p,(TsUiRect){right,335,640-right,23},(TsUiRect){edge,bottom-lower,width-edge,lower});
    return p;
}
static inline int ts_ui_presentation_pointer(const TsUiPresentation *p,int px,int py,int *x,int *y)
{
    for(int i=0;i<p->count;++i) {
        TsUiRect a=p->source[i],b=p->target[i];
        if(b.w>0 && b.h>0 && ts_ui_rect_contains(b,px,py)) {
            *x=a.x+(int)((long long)(px-b.x)*a.w/b.w);
            *y=a.y+(int)((long long)(py-b.y)*a.h/b.h);return 1;
        }
    }
    *x=*y=-1;return 0;
}
static inline TsUiRect ts_ui_presentation_rect(const TsUiPresentation *p,TsUiRect r)
{
    for(int i=0;i<p->count;++i) {
        TsUiRect a=p->source[i],b=p->target[i];
        if(r.x>=a.x && r.y>=a.y && r.x+r.w<=a.x+a.w && r.y+r.h<=a.y+a.h) {
            int x=b.x+(r.x-a.x)*b.w/a.w,y=b.y+(r.y-a.y)*b.h/a.h;
            return (TsUiRect){x,y,b.x+(r.x+r.w-a.x)*b.w/a.w-x,b.y+(r.y+r.h-a.y)*b.h/a.h-y};
        }
    }
    return (TsUiRect){0};
}
#endif
