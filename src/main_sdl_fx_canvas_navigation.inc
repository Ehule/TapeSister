/* View-only cable bundles. Their members remain exact routes; selecting a
   bundle selects every member and expands it for gain/reroute/delete operations.
   Group/movement, custom color, sidechain and control identity never coalesce. */
typedef struct {
    int destination,count,expanded;uint64_t movement;uint32_t group,color;
    float x,y,xx,yy;
} FxNavigationBundle;
static struct {
    FxNavigationBundle *items;int count,capacity;
} fx_navigation;
static void fx_navigation_free(void)
{free(fx_navigation.items);memset(&fx_navigation,0,sizeof(fx_navigation));}
static uint32_t fx_navigation_group(const FxCanvasNode *n)
{TsFxCanvasPosition *p=fx_position(n->row,0);return p?p->group:0;}
static int fx_navigation_source(const FxCanvasNode *n)
{return n->row.kind==0 || n->row.kind==1 || n->row.kind==3;}
static FxNavigationBundle *fx_navigation_bundle(const FxCanvasNode *n,int d)
{
    if(!fx_canvas.bundles || !fx_navigation_source(n))return NULL;
    uint32_t group=fx_navigation_group(n),color=fx_wire_color(n->row,d);
    for(int i=0;i<fx_navigation.count;++i) {
        FxNavigationBundle *b=&fx_navigation.items[i];
        if(b->destination==d && b->movement==n->row.canvas && b->group==group && b->color==color)return b;
    }
    return NULL;
}
static void fx_navigation_prepare(TsSamplePages *pages,TsInstrument *bank)
{
    fx_navigation.count=0;if(!fx_canvas.bundles)return;
    for(int i=0;i<fx_canvas.count;++i) {
        FxCanvasNode *n=&fx_canvas.node[i];if(!fx_navigation_source(n))continue;
        for(int d=0;d<TS_FX_DESTINATIONS;++d)if(fx_canvas.dest[d]>=0 && fx_visible_gain(n->row,d,pages,bank)) {
            FxNavigationBundle *b=fx_navigation_bundle(n,d);
            if(!b) {
                if(fx_navigation.count==fx_navigation.capacity) {
                    int cap=fx_navigation.capacity?fx_navigation.capacity*2:64;
                    FxNavigationBundle *items=realloc(fx_navigation.items,(size_t)cap*sizeof(*items));
                    if(!items){fx_canvas.bundles=0;fx_navigation.count=0;return;}
                    fx_navigation.items=items;fx_navigation.capacity=cap;
                }
                b=&fx_navigation.items[fx_navigation.count++];
                FxCanvasNode *to=&fx_canvas.node[fx_canvas.dest[d]];
                *b=(FxNavigationBundle){.destination=d,.movement=n->row.canvas,.group=fx_navigation_group(n),.color=fx_wire_color(n->row,d),
                    .xx=fx_xf(to->x),.yy=fx_yf(to->y+fx_node_h(to)*(d==TS_FX_MOSAIC_BUS || d==to->destination?.5f:.8f))};
            }
            b->x+=fx_xf(n->x+fx_node_w(n));b->y+=fx_yf(n->y+fx_node_h(n)*.5f);++b->count;
            b->expanded|=fx_wire_selected(n->row,d)>=0 || fx_chosen(n->row);
        }
    }
    for(int i=0;i<fx_navigation.count;++i){FxNavigationBundle *b=&fx_navigation.items[i];b->x/=b->count;b->y/=b->count;}
}
static void fx_navigation_endpoints(const FxCanvasNode *n,int d,float *x,float *y,float *xx,float *yy)
{
    FxCanvasNode *to=&fx_canvas.node[fx_canvas.dest[d]];
    *x=fx_xf(n->x+fx_node_w(n));*y=fx_yf(n->y+fx_node_h(n)*.5f);
    *xx=fx_xf(to->x);*yy=fx_yf(to->y+fx_node_h(to)*(d==TS_FX_MOSAIC_BUS || d==to->destination?.5f:.8f));
    FxNavigationBundle *b=fx_navigation_bundle(n,d);
    if(b && b->count>1 && !b->expanded){*x=b->x;*y=b->y;*xx=b->xx;*yy=b->yy;}
}
static int fx_focus_contains(MatrixRow row)
{
    if(!fx_canvas.focus_count)return 1;
    for(int i=0;i<fx_canvas.focus_count;++i)if(mx_same(row,fx_canvas.focus_rows[i]))return 1;
    return 0;
}
static uint32_t fx_focus_color(uint32_t color,const FxCanvasNode *from,const FxCanvasNode *to)
{
    if(fx_focus_contains(from->row) || fx_focus_contains(to->row))return color;
    return 0xff000000u|(((color&0x00fcfcfcu)>>2)+3*((FX_BG&0x00fcfcfcu)>>2));
}
static void fx_navigation_badges(void)
{
    for(int i=0;i<fx_navigation.count && fx_canvas.bundles;++i) {
        FxNavigationBundle *b=&fx_navigation.items[i];if(b->count<2 || b->expanded)continue;
        char text[40];snprintf(text,sizeof(text),"%d CABLES",b->count);
        int x=(int)b->x,y=(int)b->y-18;
        if(x>=24 && x+120<fx_right() && y>=FX_TOP && y+18<FX_BOTTOM) {
            mx_rect(x-3,y-2,120,18,FX_PANEL);mx_text(x,y,text,b->color?b->color:FX_FOCUS,1);
        }
    }
}
static void fx_navigation_select_bundle(const FxCanvasNode *n,int d,int toggle)
{
    FxNavigationBundle *b=fx_navigation_bundle(n,d);
    if(!b || b->count<2 || b->expanded){fx_select_wire(n->row,d,toggle);return;}
    for(int i=0;i<fx_canvas.count;++i) {
        FxCanvasNode *source=&fx_canvas.node[i];
        /* Membership was established by prepare; a source must actually have
           this destination. See the prepared route snapshot in graph cache. */
        if(fx_navigation_bundle(source,d)!=b || source->at<0)continue;
        TsSourceRoute raw=fx_graph_cache.routes[source->at],route=mx_display_route(raw);
        if((d==TS_FX_MOSAIC_BUS && source->row.kind==3 && !raw.matrix_enabled) ||
           (d<TS_MATRIX_DESTINATIONS && route.matrix_gain[d]))fx_select_wire(source->row,d,toggle);
    }
}
