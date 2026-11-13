#ifndef TAPESISTER_UI_METRICS_H
#define TAPESISTER_UI_METRICS_H
#include <string.h>

/* Device-independent metrics at the 14-pixel (2 x 5x7) body font. The legacy
   640-pixel surfaces use glyph_scale=1; native auxiliary surfaces use 2.
   Window size and canvas zoom never enter control measurement. */
enum { TS_UI_TOOLBAR_HEIGHT=28, TS_UI_ACTION_HEIGHT=32,
       TS_UI_CONTROL_PADDING=10, TS_UI_ACTION_PADDING=12,
       TS_UI_CONTROL_GAP=6, TS_UI_GROUP_GAP=12, TS_UI_PANEL_PADDING=12,
       TS_UI_PARAMETER_HEIGHT=40, TS_UI_PARAMETER_WRAPPED_HEIGHT=52 };
typedef struct { int x,y,w,h; } TsUiRect;
static inline int ts_ui_rect_contains(TsUiRect r,int x,int y)
{return x>=r.x && y>=r.y && x<r.x+r.w && y<r.y+r.h;}
static inline int ts_ui_label_width(const char *label,int glyph_scale)
{return (int)strlen(label)*6*glyph_scale;}
static inline int ts_ui_control_width(const char *longest_label,int glyph_scale,int action)
{
    int width=ts_ui_label_width(longest_label,glyph_scale)+
        (action?TS_UI_ACTION_PADDING:TS_UI_CONTROL_PADDING)*glyph_scale;
    int minimum=(action?36:28)*glyph_scale;
    return width>minimum?width:minimum;
}
static inline TsUiRect ts_ui_control_rect(int x,int y,const char *longest_label,int glyph_scale,int action)
{
    return (TsUiRect){x,y,ts_ui_control_width(longest_label,glyph_scale,action),
        (action?TS_UI_ACTION_HEIGHT:TS_UI_TOOLBAR_HEIGHT)*glyph_scale/2};
}
/* One definition feeds both Gallery paint and input, including the longer
   CONFIRM state. Empty space between actions is deliberately not clickable. */
enum { TS_GALLERY_NEW,TS_GALLERY_RENAME,TS_GALLERY_DELETE,TS_GALLERY_ENTER,
       TS_GALLERY_CLOSE,TS_GALLERY_IMPORT,TS_GALLERY_EXPORT,
       TS_GALLERY_EARLIER,TS_GALLERY_LATER,TS_GALLERY_ACTION_COUNT };
static inline TsUiRect ts_ui_gallery_action(int action)
{
    static const int x[]={128,188,248,484,544,496,550,128,212};
    int y=action<5?319:action<7?79:350;
    const char *label=action<5?"CONFIRM":action<7?"IMPORT":action==TS_GALLERY_EARLIER?"MOVE EARLIER":"MOVE LATER";
    return ts_ui_control_rect(x[action],y,label,1,1);
}
static inline int ts_ui_gallery_hit(int action,int x,int y)
{return ts_ui_rect_contains(ts_ui_gallery_action(action),x,y);}
#endif
