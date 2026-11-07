/* FX strokes are rasterized in drawable pixels, never enlarged logical pixels.
   Geometry stays fractional through pan/zoom and is shared with selection.
   This code runs only on the UI thread; it does not touch the audio graph. */
typedef struct {float x,y;} FxPoint;
enum { FX_CURVE_DEPTH=10, FX_CURVE_POINTS=(1<<FX_CURVE_DEPTH)+1 };
typedef struct {FxPoint point[FX_CURVE_POINTS];int count;} FxCablePath;
static struct {
    unsigned char *coverage;
    int *left,*right,width,height,top,bottom;
    float scale;
} fx_stroke;

static void fx_stroke_free(void)
{
    free(fx_stroke.coverage);free(fx_stroke.left);free(fx_stroke.right);
    memset(&fx_stroke,0,sizeof(fx_stroke));
}
static int fx_stroke_prepare(void)
{
    int width=mx_pixel_w(),height=mx_pixel_h();
    if(width!=fx_stroke.width || height!=fx_stroke.height) {
        unsigned char *coverage=calloc((size_t)width,height);
        int *left=malloc((size_t)height*sizeof(*left)),*right=calloc((size_t)height,sizeof(*right));
        if(!coverage || !left || !right){free(coverage);free(left);free(right);return 0;}
        fx_stroke_free();fx_stroke.coverage=coverage;fx_stroke.left=left;fx_stroke.right=right;
        fx_stroke.width=width;fx_stroke.height=height;
        for(int y=0;y<height;++y)left[y]=width;
    }
    /* Window enlargement is not DPI scaling. Use drawable/window ratio and
       the current display's density, independently of canvas zoom/aspect. */
    int ww=width,wh=height;float dpi=96;
    if(matrix_window.window) {
        SDL_GetWindowSize(matrix_window.window,&ww,&wh);
        int display=SDL_GetWindowDisplayIndex(matrix_window.window);
        if(display<0 || SDL_GetDisplayDPI(display,&dpi,NULL,NULL)<0 || !isfinite(dpi))dpi=96;
    }
    float density=fmaxf(width/(float)SDL_max(1,ww),height/(float)SDL_max(1,wh));
    fx_stroke.scale=SDL_clamp(fmaxf(density,dpi/96.f),1.f,4.f);
    return 1;
}
static float fx_segment_distance(float x,float y,float ax,float ay,float bx,float by)
{
    float dx=bx-ax,dy=by-ay,n=dx*dx+dy*dy,t=n?SDL_clamp(((x-ax)*dx+(y-ay)*dy)/n,0,1):0;
    dx=x-ax-t*dx;dy=y-ay-t*dy;return dx*dx+dy*dy;
}
static FxPoint fx_midpoint(FxPoint a,FxPoint b)
{return (FxPoint){(a.x+b.x)*.5f,(a.y+b.y)*.5f};}
static FxPoint fx_device_point(FxPoint p)
{return (FxPoint){p.x*mx_pixel_w()/MATRIX_W,p.y*mx_pixel_h()/MATRIX_H};}
static void fx_curve_split(FxCablePath *path,FxPoint a,FxPoint b,FxPoint c,FxPoint d,int depth)
{
    FxPoint pa=fx_device_point(a),pb=fx_device_point(b),pc=fx_device_point(c),pd=fx_device_point(d);
    /* Control points within 1/4 drawable pixel of the finite chord bound the
       whole Bezier. Finite chords also catch collinear turns/backtracking. */
    if(depth==FX_CURVE_DEPTH ||
       (fx_segment_distance(pb.x,pb.y,pa.x,pa.y,pd.x,pd.y)<=.0625f &&
        fx_segment_distance(pc.x,pc.y,pa.x,pa.y,pd.x,pd.y)<=.0625f)) {
        path->point[path->count++]=d;return;
    }
    FxPoint ab=fx_midpoint(a,b),bc=fx_midpoint(b,c),cd=fx_midpoint(c,d);
    FxPoint abc=fx_midpoint(ab,bc),bcd=fx_midpoint(bc,cd),mid=fx_midpoint(abc,bcd);
    fx_curve_split(path,a,ab,abc,mid,depth+1);
    fx_curve_split(path,mid,bcd,cd,d,depth+1);
}
static void fx_cable_path(FxCablePath *path,float x,float y,float xx,float yy)
{
    path->count=1;path->point[0]=(FxPoint){x,y};
    if(matrix_window.layout->canvas_straight){path->point[path->count++]=(FxPoint){xx,yy};return;}
    float bend=fmaxf(40,fabsf(xx-x)*.45f);
    fx_curve_split(path,(FxPoint){x,y},(FxPoint){x+bend,y},(FxPoint){xx-bend,yy},(FxPoint){xx,yy},0);
}
/* Float slab clipping, used by both box selection and the stroke rasterizer. */
static int fx_clip_segment(FxPoint *a,FxPoint *b,float left,float top,float right,float bottom)
{
    double lo=0,hi=1,origin[2]={a->x,a->y},delta[2]={(double)b->x-a->x,(double)b->y-a->y};
    double lower[2]={left,top},upper[2]={right,bottom};
    for(int axis=0;axis<2;++axis) {
        if(fabs(delta[axis])<1e-12){if(origin[axis]<lower[axis] || origin[axis]>upper[axis])return 0;continue;}
        double first=(lower[axis]-origin[axis])/delta[axis],last=(upper[axis]-origin[axis])/delta[axis];
        if(first>last){double tmp=first;first=last;last=tmp;}
        lo=fmax(lo,first);hi=fmin(hi,last);if(lo>hi)return 0;
    }
    *a=(FxPoint){origin[0]+lo*delta[0],origin[1]+lo*delta[1]};
    *b=(FxPoint){origin[0]+hi*delta[0],origin[1]+hi*delta[1]};return 1;
}
static void fx_stroke_begin(void)
{fx_stroke.top=mx_pixel_h();fx_stroke.bottom=0;}
static void fx_stroke_pixel(int x,int y,float coverage)
{
    unsigned alpha=(unsigned)(SDL_clamp(coverage,0,1)*255.f+.5f);
    if(!alpha)return;
    unsigned char *p=fx_stroke.coverage+(size_t)y*fx_stroke.width+x;
    if(alpha>*p)*p=(unsigned char)alpha;
    fx_stroke.left[y]=SDL_min(fx_stroke.left[y],x);fx_stroke.right[y]=SDL_max(fx_stroke.right[y],x+1);
    fx_stroke.top=SDL_min(fx_stroke.top,y);fx_stroke.bottom=SDL_max(fx_stroke.bottom,y+1);
}
static void fx_stroke_segment(FxPoint a,FxPoint b)
{
    float radius=fx_stroke.scale,support=radius+.5f;
    int left=SDL_max(0,mx_px(20)),top=SDL_max(0,mx_py(FX_TOP));
    int right=SDL_min(mx_pixel_w(),mx_px(fx_right())),bottom=SDL_min(mx_pixel_h(),mx_py(FX_BOTTOM));
    if(!fx_clip_segment(&a,&b,left-support,top-support,right+support,bottom+support))return;
    float dx=b.x-a.x,dy=b.y-a.y;
    float squared_length=dx*dx+dy*dy,inverse_length=squared_length>0?1/sqrtf(squared_length):0;
    int vertical=fabsf(dy)>=fabsf(dx);
    float start=vertical?a.y:a.x,end=vertical?b.y:b.x,delta=end-start;
    float minor=vertical?a.x:a.y,minor_delta=vertical?dx:dy;
    float extent=fabsf(delta)>1e-6f?support*hypotf(dx,dy)/fabsf(delta):support;
    int first=SDL_max(vertical?top:left,(int)floorf(fminf(start,end)-support));
    int last=SDL_min(vertical?bottom:right,(int)ceilf(fmaxf(start,end)+support));
    /* Visit only a narrow strip along the segment, not its bounding rectangle. */
    for(int major=first;major<last;++major) {
        float t=fabsf(delta)>1e-6f?SDL_clamp((major+.5f-start)/delta,0,1):0;
        float center=minor+t*minor_delta;
        int low=SDL_max(vertical?left:top,(int)floorf(center-extent));
        int high=SDL_min(vertical?right:bottom,(int)ceilf(center+extent));
        for(int m=low;m<high;++m) {
            int x=vertical?m:major,y=vertical?major:m;
            float rx=x+.5f-a.x,ry=y+.5f-a.y,along=rx*dx+ry*dy;
            /* Inside the segment, perpendicular distance needs no per-pixel
               division or square root. Only the small round caps do. */
            float distance;
            if(squared_length==0 || along<0)distance=hypotf(rx,ry);
            else if(along>squared_length)distance=hypotf(rx-dx,ry-dy);
            else distance=fabsf(rx*dy-ry*dx)*inverse_length;
            fx_stroke_pixel(x,y,support-distance);
        }
    }
}
static void fx_stroke_end(uint32_t color)
{
    /* Union coverage before compositing: segment joins and arrowheads must
       not accumulate alpha and make bright beads along a continuous cable. */
    for(int y=fx_stroke.top;y<fx_stroke.bottom;++y) {
        size_t row=(size_t)y*fx_stroke.width;
        for(int x=fx_stroke.left[y];x<fx_stroke.right[y];++x) {
            unsigned alpha=fx_stroke.coverage[row+x];if(!alpha)continue;
            uint32_t old=matrix_window.pixels[row+x];unsigned inverse=255-alpha;
            unsigned r=(((old>>16)&255)*inverse+((color>>16)&255)*alpha+127)/255;
            unsigned g=(((old>>8)&255)*inverse+((color>>8)&255)*alpha+127)/255;
            unsigned b=((old&255)*inverse+(color&255)*alpha+127)/255;
            matrix_window.pixels[row+x]=0xff000000u|(r<<16)|(g<<8)|b;
            fx_stroke.coverage[row+x]=0;
        }
        fx_stroke.left[y]=fx_stroke.width;fx_stroke.right[y]=0;
    }
}
static void fx_line(float x,float y,float xx,float yy,uint32_t color)
{
    if(!fx_stroke.coverage)return;
    fx_stroke_begin();fx_stroke_segment(fx_device_point((FxPoint){x,y}),fx_device_point((FxPoint){xx,yy}));fx_stroke_end(color);
}
static void fx_stroke_arrow(FxPoint tip,FxPoint tangent)
{
    float length=hypotf(tangent.x,tangent.y);if(length<.001f)return;
    float dx=tangent.x/length,dy=tangent.y/length,back=6*fx_stroke.scale,side=3*fx_stroke.scale;
    fx_stroke_segment((FxPoint){tip.x-back*dx-side*dy,tip.y-back*dy+side*dx},tip);
    fx_stroke_segment((FxPoint){tip.x-back*dx+side*dy,tip.y-back*dy-side*dx},tip);
}
static float fx_cable(float x,float y,float xx,float yy,uint32_t color,int draw,int mx,int my)
{
    if(draw) {
        float bend=matrix_window.layout->canvas_straight?0:fmaxf(40,fabsf(xx-x)*.45f);
        if(fmaxf(x+bend,xx)<12 || fminf(x,xx-bend)>fx_right()+8 ||
           fmaxf(y,yy)<FX_TOP-8 || fminf(y,yy)>FX_BOTTOM+8)return 1e12f;
    }
    FxCablePath path;fx_cable_path(&path,x,y,xx,yy);
    float distance=1e12f;
    draw=draw && fx_stroke.coverage;
    if(draw)fx_stroke_begin();
    for(int i=1;i<path.count;++i) {
        FxPoint a=path.point[i-1],b=path.point[i];
        if(draw)fx_stroke_segment(fx_device_point(a),fx_device_point(b));
        else distance=fminf(distance,fx_segment_distance(mx,my,a.x,a.y,b.x,b.y));
    }
    if(draw) {
        /* Arrow follows the curve tangent, including returns/feedback. Its
           dimensions use the same DPI scale as the stroke, not canvas zoom. */
        float t=.5625f,u=1-t,bend=fmaxf(40,fabsf(xx-x)*.45f);
        FxPoint tip,tangent;
        if(matrix_window.layout->canvas_straight){tip=(FxPoint){(x+xx)*.5f,(y+yy)*.5f};tangent=(FxPoint){xx-x,yy-y};}
        else {
            tip=(FxPoint){u*u*u*x+3*u*u*t*(x+bend)+3*u*t*t*(xx-bend)+t*t*t*xx,u*u*u*y+3*u*u*t*y+3*u*t*t*yy+t*t*t*yy};
            tangent=(FxPoint){3*u*u*bend+6*u*t*(xx-x-2*bend)+3*t*t*bend,6*u*t*(yy-y)};
        }
        tip=fx_device_point(tip);tangent=fx_device_point(tangent);
        FxPoint from=fx_device_point((FxPoint){x,y}),to=fx_device_point((FxPoint){xx,yy});
        if(hypotf(to.x-from.x,to.y-from.y)>20*fx_stroke.scale)fx_stroke_arrow(tip,tangent);
        fx_stroke_end(color);
    }
    return distance;
}

/* Quiet cables need no continuous rasterization. Compare the exact ordered
   draw commands (not a lossy hash), and cache only the graph background/cables.
   Nodes, meters, drag previews and selection outlines remain live. */
typedef struct {float x,y,xx,yy;uint32_t color;} FxCableCommand;
static struct {
    FxCableCommand *commands;
    size_t count,used,capacity;
    uint32_t *pixels;
    SDL_Rect area;
    int width,height,straight,valid,changed,direct;
    uint32_t background,grid;
    float scale;
} fx_cable_cache;
static void fx_cables_free(void)
{
    free(fx_cable_cache.commands);free(fx_cable_cache.pixels);
    memset(&fx_cable_cache,0,sizeof(fx_cable_cache));
}
static SDL_Rect fx_cables_area(void)
{
    return (SDL_Rect){mx_px(20),mx_py(FX_TOP),mx_px(fx_right())-mx_px(20),mx_py(FX_BOTTOM)-mx_py(FX_TOP)};
}
static void fx_cables_begin(void)
{
    SDL_Rect area=fx_cables_area();
    fx_cable_cache.used=0;fx_cable_cache.direct=0;
    fx_cable_cache.changed=!fx_cable_cache.valid ||
        fx_cable_cache.width!=mx_pixel_w() || fx_cable_cache.height!=mx_pixel_h() ||
        fx_cable_cache.area.x!=area.x || fx_cable_cache.area.y!=area.y ||
        fx_cable_cache.area.w!=area.w || fx_cable_cache.area.h!=area.h ||
        fx_cable_cache.straight!=matrix_window.layout->canvas_straight ||
        fx_cable_cache.scale!=fx_stroke.scale ||
        fx_cable_cache.background!=FX_BG || fx_cable_cache.grid!=FX_EDGE;
}
static void fx_cables_draw_command(FxCableCommand c)
{fx_cable(c.x,c.y,c.xx,c.yy,c.color,1,0,0);}
static void fx_cables_add(float x,float y,float xx,float yy,uint32_t color)
{
    FxCableCommand command={x,y,xx,yy,color};
    if(fx_cable_cache.direct){fx_cables_draw_command(command);return;}
    if(fx_cable_cache.used==fx_cable_cache.capacity) {
        size_t capacity=fx_cable_cache.capacity?fx_cable_cache.capacity*2:64;
        FxCableCommand *commands=capacity<=SIZE_MAX/sizeof(*commands)?
            realloc(fx_cable_cache.commands,capacity*sizeof(*commands)):NULL;
        if(!commands) {
            fx_cable_cache.valid=0;fx_cable_cache.direct=1;
            for(size_t i=0;i<fx_cable_cache.used;++i)fx_cables_draw_command(fx_cable_cache.commands[i]);
            fx_cables_draw_command(command);return;
        }
        fx_cable_cache.commands=commands;fx_cable_cache.capacity=capacity;
    }
    size_t i=fx_cable_cache.used++;
    if(i>=fx_cable_cache.count || memcmp(&command,&fx_cable_cache.commands[i],sizeof(command)))fx_cable_cache.changed=1;
    fx_cable_cache.commands[i]=command;
}
static void fx_cables_end(void)
{
    if(fx_cable_cache.direct)return;
    SDL_Rect area=fx_cables_area();int changed=fx_cable_cache.changed || fx_cable_cache.used!=fx_cable_cache.count;
    fx_cable_cache.count=fx_cable_cache.used;
    if(changed) {
        for(size_t i=0;i<fx_cable_cache.count;++i)fx_cables_draw_command(fx_cable_cache.commands[i]);
        fx_cable_cache.valid=0;
        if(!fx_stroke.coverage)return;
        if(!fx_cable_cache.pixels || area.w!=fx_cable_cache.area.w || area.h!=fx_cable_cache.area.h) {
            uint32_t *pixels=malloc((size_t)area.w*area.h*sizeof(*pixels));
            if(!pixels)return;
            free(fx_cable_cache.pixels);fx_cable_cache.pixels=pixels;
        }
        fx_cable_cache.area=area;fx_cable_cache.width=mx_pixel_w();fx_cable_cache.height=mx_pixel_h();
        fx_cable_cache.straight=matrix_window.layout->canvas_straight;fx_cable_cache.scale=fx_stroke.scale;
        fx_cable_cache.background=FX_BG;fx_cable_cache.grid=FX_EDGE;
        fx_cable_cache.valid=1;
    }
    for(int y=0;y<area.h;++y) {
        uint32_t *screen=matrix_window.pixels+(size_t)(area.y+y)*mx_pixel_w()+area.x;
        uint32_t *cache=fx_cable_cache.pixels+(size_t)y*area.w;
        if(changed)memcpy(cache,screen,(size_t)area.w*sizeof(*cache));
        else memcpy(screen,cache,(size_t)area.w*sizeof(*cache));
    }
}
