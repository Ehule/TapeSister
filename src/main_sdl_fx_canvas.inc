/* A spatial view of the Matrix's exact routes. Only the existing Matrix
   transactions may change audio; positions, pan and zoom are view metadata. */
enum { FX_NODE_W=210, FX_NODE_H=86, FX_TOP=116, FX_BOTTOM=682, FX_RIGHT=1000, FX_DRAG_NODE=100, FX_DRAG_WIRE, FX_DRAG_PAN, FX_DRAG_BOX, FX_DRAG_CONTROL=120, FX_DRAG_MACRO=140 };
typedef struct { MatrixRow row; int at,destination,rank; float x,y; } FxCanvasNode;
static struct {
    FxCanvasNode node[MATRIX_CAPACITY+TS_MATRIX_DESTINATIONS];
    int count,dest[TS_MATRIX_DESTINATIONS],fit,wire,destination,mouse_x,mouse_y;
    MatrixRow source,moving;
    float origin_x,origin_y,pointer_x,pointer_y;
    uint32_t meta;
    TsFxCanvasPosition move[TS_FX_CANVAS_POSITIONS];int move_count;
    MatrixRow before[MATRIX_CAPACITY+TS_MATRIX_DESTINATIONS];int before_count;
    int popup,popup_x,popup_y;float place_x,place_y;
} fx_canvas={.wire=-1,.destination=-1};
static void fx_context_reset(void)
{fx_canvas.meta=0;fx_canvas.wire=fx_canvas.destination=-1;fx_canvas.fit=fx_canvas.popup=0;}
static int fx_source_at(MatrixRow key)
{
    for(int i=0;i<matrix_window.count;++i)if(mx_same(matrix_window.rows[i],key))return i;
    return -1;
}
static TsMatrixEffectControls *fx_meta(void)
{
    if(fx_canvas.meta)for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)
        if(matrix_window.controls.effect[i].id==fx_canvas.meta &&
           matrix_window.controls.effect[i].type==TS_MATRIX_FX_META)return &matrix_window.controls.effect[i];
    fx_canvas.meta=0;return NULL;
}
static TsFxCanvasPosition *fx_position(MatrixRow row,int create)
{
    TsMatrixLayout *l=matrix_window.layout;int kind=row.kind;uint64_t id=mx_row_id(row);
    if(kind==5)id=(uint64_t)row.index;
    for(int i=0;i<l->canvas_count;++i)if(l->canvas[i].kind==kind && l->canvas[i].id==id)return &l->canvas[i];
    if(!create)return NULL;
    if(kind==0 || kind==1 || kind==3) {
        int sources=0;
        for(int i=0;i<l->canvas_count;++i) {
            int k=l->canvas[i].kind;
            if(k==0 || k==1 || k==3)++sources;
        }
        if(sources>=TS_FX_CANVAS_SOURCE_POSITIONS)return NULL;
    }
    if(l->canvas_count==TS_FX_CANVAS_POSITIONS) {
        /* Reclaim positions of deleted instances before reusing a view slot. */
        for(int i=l->canvas_count-1;i>=0;--i)if(l->canvas[i].kind==4) {
            int alive=0;for(int j=0;j<TS_MATRIX_EFFECT_CAPACITY;++j)alive|=matrix_window.controls.effect[j].id==l->canvas[i].id;
            if(!alive) {
                l->canvas[i]=l->canvas[--l->canvas_count];
                memset(&l->canvas[l->canvas_count],0,sizeof(l->canvas[0]));
            }
        }
    }
    if(l->canvas_count==TS_FX_CANVAS_POSITIONS)return NULL;
    TsFxCanvasPosition *p=&l->canvas[l->canvas_count++];memset(p,0,sizeof(*p));p->kind=kind;p->id=id;return p;
}
/* A newly visible module can enter a column already occupied by a hand-placed
   node. Find an open spot once, then save it with the other world positions. */
static void fx_store_initial_position(TsFxCanvasPosition *position,float x,float y)
{
    TsMatrixLayout *layout=matrix_window.layout;
    for(int i=0;i<layout->canvas_count;) {
        TsFxCanvasPosition *other=&layout->canvas[i++];
        if(other==position || fabsf(x-other->x)>=FX_NODE_W+12 ||
           fabsf(y-other->y)>=FX_NODE_H+12)continue;
        y+=120.f;
        i=0;
    }
    position->x=x;
    position->y=y;
}

static void fx_build(TsSamplePages *pages,TsInstrument *bank)
{
    MatrixWindow *w=&matrix_window;int wanted[TS_MATRIX_DESTINATIONS]={0};
    if(w->filter){w->filter=0;mx_rows(pages,bank);}
    fx_canvas.count=0;for(int d=0;d<TS_MATRIX_DESTINATIONS;++d)fx_canvas.dest[d]=-1;
    /* Sources are endpoints, not copies of sample/generator editors. Ordinary
       tile/card/track membership remains represented by its source bus. */
    for(int i=0;i<w->count;++i) {
        MatrixRow row=w->rows[i];TsSourceRoute raw=mx_route(row,pages,bank);
        int processor=(row.kind==2 || row.kind==4) && ts_matrix_is_processor(row.index);
        if(processor)continue;
        int keep=row.kind==2?(row.index>=TS_MATRIX_FM && row.index<=TS_MATRIX_TRACKER):
            raw.matrix_enabled || raw.mix_enabled;
        if(row.kind==2 && ts_matrix_input_pair(row.index)>=0) {
            keep=1; /* mx_rows already enumerated the physical/named input pairs. */
        }
        if(!keep || (raw.mode==TS_SOURCE_INHERIT && !raw.matrix_enabled))continue;
        FxCanvasNode *n=&fx_canvas.node[fx_canvas.count++];*n=(FxCanvasNode){.row=row,.at=i,.destination=-1};
        TsSourceRoute route=mx_display_route(raw);
        for(int d=0;d<TS_MATRIX_DESTINATIONS;++d)wanted[d]|=route.matrix_gain[d]!=0;
    }
    for(int i=0;i<fx_canvas.count;++i)if(fx_canvas.node[i].row.kind==2 && fx_canvas.node[i].row.index==TS_MATRIX_MOSAIC) {
        FxCanvasNode first=fx_canvas.node[0];fx_canvas.node[0]=fx_canvas.node[i];fx_canvas.node[i]=first;break;
    }
    for(int i=0;i<w->count;++i) {
        MatrixRow row=w->rows[i];
        if((row.kind!=2 && row.kind!=4) || !ts_matrix_is_processor(row.index))continue;
        /* Membership comes from the Matrix inventory, even for isolated chains. */
        for(int d=0;d<TS_MATRIX_DESTINATIONS;++d)
            if(w->controls.row[row.index].matrix_gain[d])wanted[d]=1;
        FxCanvasNode *n=&fx_canvas.node[fx_canvas.count];*n=(FxCanvasNode){.row=row,.at=i,.destination=row.index};
        fx_canvas.dest[row.index]=fx_canvas.count++;
        int sc=ts_matrix_sidechain_for(row.index);if(sc>=0 && ts_matrix_destination_active(&w->controls,sc))fx_canvas.dest[sc]=fx_canvas.count-1;
    }
    for(int d=0;d<TS_MATRIX_DESTINATIONS;++d) {
        if(fx_canvas.dest[d]>=0 || !ts_matrix_destination_active(&w->controls,d))continue;
        if(!ts_matrix_is_output(d) && d<TS_MATRIX_AMBISONICS)continue;
        MatrixRow sink={.kind=5,.index=d};
        int pair=ts_matrix_output_pair(d);
        int named=pair>=0 && w->controls.output_name[pair][0];
        if(!wanted[d] && !named && d!=TS_MATRIX_NODES && !mx_destination_available(d) && !fx_position(sink,0))continue;
        FxCanvasNode *n=&fx_canvas.node[fx_canvas.count];
        *n=(FxCanvasNode){.row={.kind=5,.index=d},.at=-1,.destination=d};
        mx_destination(n->row.name,sizeof(n->row.name),d,1);fx_canvas.dest[d]=fx_canvas.count++;
    }
    /* Seed a new patch from its signal flow once. Keep each module's position
       in the project thereafter, including when the last incident wire goes. */
    int arrange=0;
    for(int i=0;i<fx_canvas.count;++i) {
        FxCanvasNode *n=&fx_canvas.node[i];
        if(n->row.kind<2 || n->row.kind==3)continue;
        if(!fx_position(n->row,0)){arrange=1;break;}
    }
    for(int pass=0;arrange && pass<TS_MATRIX_PROCESSORS+1;++pass) {
        int changed=0;
        for(int i=0;i<fx_canvas.count;++i) {
            FxCanvasNode *n=&fx_canvas.node[i];if(n->at<0)continue;
            TsSourceRoute route=mx_display_route(mx_route(n->row,pages,bank));
            for(int d=0;d<TS_MATRIX_DESTINATIONS;++d)if(route.matrix_gain[d] && fx_canvas.dest[d]>=0) {
                if((n->row.kind==2 || n->row.kind==4) && (w->controls.delayed[n->row.index]&ts_matrix_feedback_bit(d)))continue;
                FxCanvasNode *to=&fx_canvas.node[fx_canvas.dest[d]];
                int rank=SDL_min(TS_MATRIX_PROCESSORS+1,n->rank+1);
                if(to->rank<rank){to->rank=rank;changed=1;}
            }
        }
        if(!changed)break;
    }
    int rows[TS_MATRIX_PROCESSORS+3]={0},source_row=0;
    for(int i=0;i<fx_canvas.count;++i) {
        FxCanvasNode *n=&fx_canvas.node[i];
        if(n->row.kind==0 || n->row.kind==1 || n->row.kind==3) {
            TsFxCanvasPosition *saved=fx_position(n->row,0);
            n->x=saved?saved->x:-260.f;
            n->y=saved?saved->y:source_row*120.f;
            ++source_row;
            continue;
        }
        n->x=n->rank*260.f;
        n->y=rows[n->rank]++*120.f;
        TsFxCanvasPosition *p=fx_position(n->row,0);
        if(!p) {
            p=fx_position(n->row,1);
            if(p)fx_store_initial_position(p,n->x,n->y);
            else {
                /* A project can use every saved position. Give excess nodes
                   deterministic slots that are independent of their wires. */
                n->x=(n->row.kind==5?7:1+n->row.index%6)*260.f;
                n->y=(1+n->row.index/6)*120.f;
            }
        }
        if(p){n->x=p->x;n->y=p->y;}

    }
}
static int fx_chosen(MatrixRow row)
{
    MatrixWindow *w=&matrix_window;
    for(int i=0;i<w->selection_count;++i)if(mx_same(row,w->selection[i]))return 1;
    return 0;
}
static void fx_clear_selection(void)
{matrix_window.selection_count=0;matrix_window.selected=-1;}
static void fx_select(FxCanvasNode *n,int add)
{
    MatrixWindow *w=&matrix_window;
    if(!add)fx_clear_selection();
    int i=0;while(i<w->selection_count && !mx_same(n->row,w->selection[i]))++i;
    if(i==w->selection_count)w->selection[w->selection_count++]=n->row;
    else if(add)w->selection[i]=w->selection[--w->selection_count];
    w->selected=n->at;
}
static int fx_right(void){return fx_canvas.meta?1260:FX_RIGHT;}
static void fx_fit_selection(int selected)
{
    float left=1e9f,top=1e9f,right=-1e9f,bottom=-1e9f;int count=0;
    for(int i=0;i<fx_canvas.count;++i){FxCanvasNode *n=&fx_canvas.node[i];
        if(selected && !fx_chosen(n->row))continue;
        left=fminf(left,n->x);top=fminf(top,n->y);right=fmaxf(right,n->x+FX_NODE_W);bottom=fmaxf(bottom,n->y+FX_NODE_H);++count;}
    fx_canvas.fit=0;if(!count)return;
    TsMatrixLayout *l=matrix_window.layout;float width=FX_RIGHT-20,height=FX_BOTTOM-FX_TOP;
    l->canvas_zoom=SDL_clamp(fminf((width-48)/(right-left),(height-48)/(bottom-top)),.001f,selected?2.f:1.f);
    l->canvas_x=-left+(width/l->canvas_zoom-(right-left))*.5f;
    l->canvas_y=-top+(height/l->canvas_zoom-(bottom-top))*.5f;l->canvas_view=1;
}
static void fx_fit(void){fx_fit_selection(0);}
/* Use Mosaic's palette roles; cables have a dedicated, brighter data color. */
#define FX_THEME(role,fallback) ts_theme_color(&matrix_window.palette,TS_THEME_MOSAIC,role,fallback)
#define FX_BG FX_THEME(TS_COLOR_BACKGROUND,0xff0b0d10u)
#define FX_PANEL FX_THEME(TS_COLOR_PANEL,0xff201f27u)
#define FX_EDGE FX_THEME(TS_COLOR_EDGE,0xff48414du)
#define FX_TEXT FX_THEME(TS_COLOR_TEXT,0xffe0d6e9u)
#define FX_DIM FX_THEME(TS_COLOR_DIM,0xffa79ab3u)
#define FX_FOCUS FX_THEME(TS_COLOR_FOCUS,0xffffc563u)
#define FX_MUTED FX_THEME(TS_COLOR_MUTED,0xff77727bu)
#define FX_CABLE FX_THEME(TS_COLOR_DATA2,0xff81cec8u)
static uint32_t fx_color(int type)
{
    static const int roles[]={TS_COLOR_DATA4,TS_COLOR_DATA3,TS_COLOR_DATA1,TS_COLOR_DATA1,
        TS_COLOR_DATA5,TS_COLOR_DATA3,TS_COLOR_DATA4,TS_COLOR_DATA5,TS_COLOR_DATA3,TS_COLOR_DATA4,TS_COLOR_DATA5};
    static const uint32_t colors[]={0xffc9b3d4u,0xff9ed2b2u,0xffe6c17du,0xffe5b36bu,
        0xffb990c6u,0xff95c6dfu,0xffc294d2u,0xff9fcea7u,0xffb6d38du,0xffd19ebdu,0xffc499c9u};
    type=SDL_clamp(type,0,TS_MATRIX_FX_TYPES-1);
    return FX_THEME(roles[type],colors[type]);
}
static int fx_line_height(FxFontMetrics font)
{return (font.rows[7]*MATRIX_H+mx_pixel_h()-1)/mx_pixel_h()+2;}

/* Wrap at a word boundary when possible. Return the start of the next line. */
static const char *fx_label_line(const char *text,int capacity,char line[128])
{
    int count=(int)strlen(text);
    capacity=SDL_clamp(capacity,0,127);
    if(count>capacity) {
        count=capacity;
        while(count>0 && text[count]!=' ')--count;
        if(!count)count=capacity;
    }
    memcpy(line,text,count);line[count]=0;
    text+=count;
    while(*text==' ')++text;
    return text;
}
static void fx_text_box(SDL_Rect area,SDL_Rect clip,const char *text,uint32_t ink,int scale,int max_lines)
{
    FxFontMetrics font=fx_font(scale);
    int stride=fx_line_height(font);
    int capacity=(mx_px(area.x+area.w)-mx_px(area.x))/font.advance;
    int lines=SDL_min(max_lines,(area.h+2)/stride);
    SDL_Rect visible;
    if(capacity<1 || lines<1 || !SDL_IntersectRect(&area,&clip,&visible))return;
    for(int i=0;i<lines && *text;++i) {
        char line[128];
        text=fx_label_line(text,capacity,line);
        if(i==lines-1 && *text && capacity>=3) {
            size_t keep=SDL_min(strlen(line),(size_t)SDL_min(capacity-3,124));
            memcpy(line+keep,"...",4);
        }
        fx_text_clipped(area.x,area.y+i*stride,line,ink,font,visible);
    }
}
static int fx_name_fits(const char *name,SDL_Rect area,int scale)
{
    FxFontMetrics font=fx_font(scale);
    int capacity=(mx_px(area.x+area.w)-mx_px(area.x))/font.advance;
    int lines=SDL_min(2,(area.h+2)/fx_line_height(font));
    char line[128];
    if(capacity<1 || lines<1)return 0;
    while(lines-->0 && *name)name=fx_label_line(name,capacity,line);
    return !*name;
}
static void fx_panel_text(int x,int y,int width,int height,const char *text,uint32_t ink,int scale,int lines)
{
    SDL_Rect box={x,y,width,height};
    fx_text_box(box,box,text,ink,scale,lines);
}
static void fx_button(int x,int y,int width,const char *text,int active)
{
    mx_rect(x,y,width,28,active?FX_FOCUS:FX_EDGE);mx_rect(x+2,y+2,width-4,24,FX_PANEL);
    FxFontMetrics f=fx_font(2);
    int logical_width=(int)strlen(text)*f.advance*MATRIX_W/mx_pixel_w();
    mx_text(x+(width-logical_width)/2,y+(28-fx_line_height(f)+2)/2,text,active?FX_FOCUS:FX_TEXT,2);
}
static int fx_x(float x){TsMatrixLayout *l=matrix_window.layout;return 20+(int)lrintf((x+l->canvas_x)*l->canvas_zoom);}
static int fx_y(float y){TsMatrixLayout *l=matrix_window.layout;return FX_TOP+(int)lrintf((y+l->canvas_y)*l->canvas_zoom);}
static void fx_rect(int x,int y,int width,int height,uint32_t color)
{
    int r=SDL_min(fx_right(),x+width),b=SDL_min(FX_BOTTOM,y+height);x=SDL_max(20,x);y=SDL_max(FX_TOP,y);
    if(r>x && b>y)mx_rect(x,y,r-x,b-y,color);
}
static void fx_line(int x,int y,int xx,int yy,uint32_t color)
{
    SDL_Rect clip={20,FX_TOP,fx_right()-20,FX_BOTTOM-FX_TOP};
    if(!SDL_IntersectRectAndLine(&clip,&x,&y,&xx,&yy))return;
    int dx=abs(xx-x),dy=-abs(yy-y),sx=x<xx?1:-1,sy=y<yy?1:-1,err=dx+dy;
    for(;;){fx_rect(x,y,2,2,color);if(x==xx && y==yy)break;int e=2*err;if(e>=dy){err+=dy;x+=sx;}if(e<=dx){err+=dx;y+=sy;}}
}
static float fx_segment_distance(float x,float y,float ax,float ay,float bx,float by)
{
    float dx=bx-ax,dy=by-ay,n=dx*dx+dy*dy,t=n?SDL_clamp(((x-ax)*dx+(y-ay)*dy)/n,0,1):0;
    dx=x-ax-t*dx;dy=y-ay-t*dy;return dx*dx+dy*dy;
}
static float fx_cable(int x,int y,int xx,int yy,uint32_t color,int draw,int mx,int my)
{
    float bend=fmaxf(40,fabsf((float)xx-x)*.45f),distance=1e12f;
    int oldx=x,oldy=y;
    if(matrix_window.layout->canvas_straight) {
        if(draw)fx_line(x,y,xx,yy,color);
        distance=fx_segment_distance(mx,my,x,y,xx,yy);
        if(draw){float dx=xx-x,dy=yy-y,len=hypotf(dx,dy);if(len>20){float ax=(x+xx)*.5f,ay=(y+yy)*.5f;dx/=len;dy/=len;
            fx_line((int)(ax-dx*7-dy*4),(int)(ay-dy*7+dx*4),(int)ax,(int)ay,color);
            fx_line((int)(ax-dx*7+dy*4),(int)(ay-dy*7-dx*4),(int)ax,(int)ay,color);}}
        return distance;
    }
    /* Bounded cubic segments; quiet meters, no animated cable particles. */
    for(int i=1;i<=32;++i) {
        float t=i/32.f,u=1-t;
        int nx=(int)lrintf(u*u*u*x+3*u*u*t*(x+bend)+3*u*t*t*(xx-bend)+t*t*t*xx);
        int ny=(int)lrintf(u*u*u*y+3*u*u*t*y+3*u*t*t*yy+t*t*t*yy);
        if(draw)fx_line(oldx,oldy,nx,ny,color);
        distance=fminf(distance,fx_segment_distance(mx,my,oldx,oldy,nx,ny));oldx=nx;oldy=ny;
        if(draw && i==18){fx_line(nx-6,ny-4,nx,ny,color);fx_line(nx-6,ny+4,nx,ny,color);}
    }
    return distance;
}
static void fx_node_rect(int x,int y,int width,int height,const char *name,const char *compact,
    const char *detail,uint32_t color,int selected)
{
    if(x+width<20 || x>fx_right() || y+height<FX_TOP || y>FX_BOTTOM)return;
    fx_rect(x,y,width,height,selected?FX_FOCUS:color);
    fx_rect(x+2,y+2,width-4,height-4,FX_PANEL);
    if(height>14)fx_rect(x+5,y+height-10,width-10,5,color);
    if(selected)fx_rect(x+3,y+3,width-6,3,FX_FOCUS);
    SDL_Rect graph={20,FX_TOP,fx_right()-20,FX_BOTTOM-FX_TOP};
    int show_detail=width>=180 && height>=76 && detail && detail[0];
    SDL_Rect title={x+7,y+7,width-14,height-(show_detail?40:18)};
    int scale=2;
    if(!fx_name_fits(name,title,scale)) {
        if(fx_name_fits(name,title,1))scale=1;
        else {name=compact;if(!fx_name_fits(name,title,scale))scale=1;}
    }
    fx_text_box(title,graph,name,color==FX_MUTED?FX_DIM:FX_TEXT,scale,2);
    if(show_detail)fx_text_box((SDL_Rect){x+7,y+height-30,width-14,19},graph,detail,FX_DIM,1,1);
}
static void fx_chain_cable(int x,int y,int xx,int yy)
{
    if(abs(y-yy)<4) {
        fx_line(x,y,xx,yy,FX_CABLE);int mid=(x+xx)/2,direction=xx>x?1:-1;
        fx_line(mid-direction*6,y-4,mid,y,FX_CABLE);fx_line(mid-direction*6,y+4,mid,y,FX_CABLE);
    } else {
        int side=SDL_min(1244,SDL_max(x,xx)+18);
        fx_line(x,y,side,y,FX_CABLE);fx_line(side,y,side,yy,FX_CABLE);fx_line(side,yy,xx,yy,FX_CABLE);
        int mid=(y+yy)/2;fx_line(side-4,mid-6,side,mid,FX_CABLE);fx_line(side+4,mid-6,side,mid,FX_CABLE);
    }
}
static void fx_meta_paint(TsMatrixEffectControls *c)
{
    TsMetaControls *rack=&c->meta;char label[80];
    snprintf(label,sizeof(label),"FX CANVAS / %.31s",rack->name);mx_text(24,78,label,MX_AMBER,2);
    int previous_x=108,previous_y=210;
    fx_node_rect(26,180,82,60,"IN","IN","",MX_GREEN,0);
    for(int i=0;i<rack->count;++i) {
        int col=i<4?i:7-i,x=146+col*270,y=i<4?174:418;
        int inx=i<4?x:x+FX_NODE_W,outx=i<4?x+FX_NODE_W:x;
        fx_chain_cable(previous_x,previous_y,inx,y+FX_NODE_H/2);
        const TsMetaStage *s=&rack->stage[i];snprintf(label,sizeof(label),"STAGE %d / %s",i+1,s->enabled?"ACTIVE":"BYPASS");
        fx_node_rect(x,y,FX_NODE_W,FX_NODE_H,ts_sister_fx_type_name(s->type),ts_matrix_effect_short_name(s->type),label,s->enabled?fx_color(s->type):FX_MUTED,0);
        previous_x=outx;previous_y=y+FX_NODE_H/2;
    }
    int reverse=rack->count>4 || rack->count==4;
    int out_x=rack->count>4?SDL_max(26,previous_x-130):rack->count==4?1090:previous_x+40;
    int out_y=rack->count==4?300:previous_y-30;
    fx_chain_cable(previous_x,previous_y,reverse?out_x+82:out_x,out_y+30);
    fx_node_rect(out_x,out_y,82,60,"OUT","OUT","",MX_GREEN,0);
    mx_text(26,600,"Double-click a stage to edit its parameters and mappings.",FX_TEXT,2);
    mx_text(26,628,"Patch parallel branches on the outer canvas.",FX_DIM,2);
    for(int i=0;i<4;++i) {
        float v=rack->scene_mode && rack->captured==3?rack->scene[0][i]+(rack->scene[1][i]-rack->scene[0][i])*rack->morph:rack->macro[i];
        snprintf(label,sizeof(label),"%s %.0F%%",rack->label[i],v*100);int x=24+i*310;
        mx_text(x,710,label,MX_TEXT,2);mx_rect(x,746,286,6,MX_DIM);mx_rect(x+(int)(282*v),740,4,18,MX_GREEN);
    }
    mx_button(24,784,212,"RACK EDITOR",0);mx_text(260,790,"Drag or scroll to adjust a macro.",FX_TEXT,2);
}
static void fx_inspector_paint(TsSamplePages *pages,TsInstrument *bank)
{
    MatrixWindow *w=&matrix_window;char label[80];int slot=mx_effect_slot();
    mx_rect(1008,112,252,574,FX_EDGE);mx_rect(1011,115,246,568,FX_PANEL);
    if(slot<0) {
        if(fx_canvas.wire>=0 && fx_canvas.destination>=0 && fx_canvas.dest[fx_canvas.destination]>=0) {
            mx_text(1028,136,"CABLE",FX_FOCUS,2);
            mx_text(1028,194,"FROM",FX_CABLE,2);
            fx_panel_text(1028,221,213,65,fx_canvas.source.name,FX_TEXT,2,3);
            mx_text(1028,302,"TO",FX_CABLE,2);
            fx_panel_text(1028,329,213,65,fx_canvas.node[fx_canvas.dest[fx_canvas.destination]].row.name,FX_TEXT,2,3);
            TsSourceRoute route=mx_display_route(mx_route(fx_canvas.source,pages,bank));
            int gain=route.matrix_gain[fx_canvas.destination];
            if(gain>0) {
                snprintf(label,sizeof(label),"%+.1f dB",20*log10f(gain/1000.f));
                mx_text(1028,414,label,FX_FOCUS,2);
            }
            fx_panel_text(1028,465,213,100,"Scroll to set level. Delete disconnects.",FX_TEXT,2,4);
        } else if(w->selection_count) {
            if(w->selection_count==1)snprintf(label,sizeof(label),"%s",w->selection[0].name);
            else snprintf(label,sizeof(label),"%d NODES",w->selection_count);
            fx_panel_text(1028,136,213,80,label,FX_FOCUS,2,3);
            fx_panel_text(1028,235,213,100,"Z frames this selection.",FX_TEXT,2,3);
        } else {
            mx_text(1028,136,"FX INSPECTOR",FX_FOCUS,2);
            fx_panel_text(1028,197,213,100,"Select a module to inspect it.",FX_TEXT,2,4);
            fx_panel_text(1028,310,213,100,"Double-click a Meta FX to open its chain.",FX_TEXT,2,4);
        }
        return;
    }
    const TsMatrixEffectControls *c=&w->controls.effect[slot];
    fx_panel_text(1028,125,213,47,c->type==TS_MATRIX_FX_META?c->meta.name:ts_matrix_effect_name(c->type),
        c->enabled?fx_color(c->type):FX_MUTED,2,2);
    fx_button(1028,178,208,c->enabled?"ACTIVE / M":"BYPASS / M",c->enabled);
    int count=c->type==TS_MATRIX_FX_META?4:mx_effect_controls(c);
    for(int i=0;i<count;++i){int y=230+i*53;
        float v=c->type==TS_MATRIX_FX_META?(c->meta.scene_mode && c->meta.captured==3?c->meta.scene[0][i]+(c->meta.scene[1][i]-c->meta.scene[0][i])*c->meta.morph:c->meta.macro[i]):mx_effect_value(c,i);
        if(c->type==TS_MATRIX_FX_META)snprintf(label,sizeof(label),"%.20s %.0F%%",c->meta.label[i],v*100);
        else mx_effect_label(label,sizeof(label),c,i);
        fx_panel_text(1028,y,208,38,label,FX_TEXT,2,2);mx_rect(1028,y+39,208,7,FX_EDGE);
        mx_rect(1028,y+39,(int)(208*v),7,fx_color(c->type));mx_rect(1028+(int)(204*v),y+36,4,13,FX_CABLE);
    }
    if(c->type==TS_MATRIX_FX_META)fx_button(1028,480,208,"OPEN CHAIN",0);
    fx_panel_text(1028,625,212,54,"Delete removes this effect.",FX_DIM,2,2);
}
static void fx_popup_open(int x,int y)
{
    fx_canvas.popup=1;fx_canvas.popup_x=SDL_clamp(x,24,FX_RIGHT-278);
    fx_canvas.popup_y=SDL_clamp(y,FX_TOP+4,FX_BOTTOM-(40+(TS_MATRIX_FX_TYPES-1)*30));
    TsMatrixLayout *l=matrix_window.layout;fx_canvas.place_x=(x-20)/l->canvas_zoom-l->canvas_x;
    fx_canvas.place_y=(y-FX_TOP)/l->canvas_zoom-l->canvas_y;matrix_window.drag=0;
}
static void fx_popup_paint(void)
{
    int x=fx_canvas.popup_x,y=fx_canvas.popup_y,h=40+(TS_MATRIX_FX_TYPES-1)*30;
    mx_rect(x,y,274,h,FX_CABLE);mx_rect(x+2,y+2,270,h-4,FX_PANEL);
    mx_text(x+12,y+8,"ADD EFFECT   ESC: BACK",FX_TEXT,2);
    for(int type=1;type<TS_MATRIX_FX_TYPES;++type){int yy=y+32+(type-1)*30;
        mx_rect(x+9,yy+4,5,18,fx_color(type));mx_text(x+24,yy+6,ts_matrix_effect_name(type),FX_TEXT,2);}
}
static void fx_paint_cables(TsSamplePages *pages,TsInstrument *bank)
{
    MatrixWindow *w=&matrix_window;
    int cable_selected=fx_canvas.wire>=0 && fx_canvas.destination>=0;
    int node_selected=0;
    if(w->selection_count==1)for(int i=0;i<fx_canvas.count;++i)
        if(mx_same(fx_canvas.node[i].row,w->selection[0])){node_selected=1;break;}
    int focus=cable_selected || node_selected;
    /* Draw the focused path last so parallel paths cannot cover it. */
    for(int layer=0;layer<=focus;++layer) {
        for(int i=0;i<fx_canvas.count;++i) {
            FxCanvasNode *n=&fx_canvas.node[i];if(n->at<0)continue;
            TsSourceRoute r=mx_display_route(mx_route(n->row,pages,bank));
            for(int d=0;d<TS_MATRIX_DESTINATIONS;++d)if(r.matrix_gain[d] && fx_canvas.dest[d]>=0) {
                FxCanvasNode *to=&fx_canvas.node[fx_canvas.dest[d]];int sc=d!=to->destination;
                int delayed=(n->row.kind==2 || n->row.kind==4) && (w->controls.delayed[n->row.index]&ts_matrix_feedback_bit(d));
                int selected=fx_canvas.wire>=0 && mx_same(n->row,fx_canvas.source) && d==fx_canvas.destination;
                int related=cable_selected?selected:node_selected &&
                    (mx_same(n->row,w->selection[0]) || mx_same(to->row,w->selection[0]));
                if(focus && layer!=related)continue;
                uint32_t color=selected?MX_YELLOW:delayed?MX_AMBER:sc?0xffbc98ccu:FX_CABLE;
                if(focus && !related)color=0xff000000u | (((color&0x00fefefeu)>>1)+((FX_BG&0x00fefefeu)>>1));
                int x=fx_x(n->x+FX_NODE_W),y=fx_y(n->y+FX_NODE_H*.5f),xx=fx_x(to->x),yy=fx_y(to->y+(sc?FX_NODE_H*.8f:FX_NODE_H*.5f));
                fx_cable(x,y,xx,yy,color,1,0,0);
            }
        }
    }
}
static void fx_paint(AudioState *audio,TsSamplePages *pages,TsInstrument *bank)
{
    MatrixWindow *w=&matrix_window;mx_rect(0,0,MATRIX_W,MATRIX_H,FX_BG);char label[100];
    fx_build(pages,bank);if(fx_canvas.fit)fx_fit();
    mx_rect(16,FX_TOP-4,fx_right()-12,FX_BOTTOM-FX_TOP+8,FX_EDGE);
    mx_rect(20,FX_TOP,fx_right()-20,FX_BOTTOM-FX_TOP,FX_BG);
    for(int x=40;x<fx_right();x+=32)for(int y=FX_TOP+16;y<FX_BOTTOM;y+=32)mx_rect(x,y,1,1,FX_EDGE);
    TsMatrixEffectControls *meta=fx_meta();
    if(meta)fx_meta_paint(meta);
    else {
        fx_paint_cables(pages,bank);
        for(int i=0;i<fx_canvas.count;++i) {
            FxCanvasNode *n=&fx_canvas.node[i];int x=fx_x(n->x),y=fx_y(n->y),width=(int)(FX_NODE_W*w->layout->canvas_zoom),height=(int)(FX_NODE_H*w->layout->canvas_zoom);
            int slot=n->row.kind==4?ts_matrix_effect_index(n->row.index):-1;
            const TsMatrixEffectControls *c=slot>=0?&w->controls.effect[slot]:NULL;
            const char *detail=n->at<0?(mx_destination_available(n->destination)?"OUTPUT / RECEIVER":"N/A / PORT NOT OPEN"):c?(c->enabled?(c->type==TS_MATRIX_FX_META?"DOUBLE-CLICK TO ENTER":"ACTIVE / SELECT TO EDIT"):"BYPASS"):"BUS / PATCH ENDPOINT";
            const char *title=c && c->type==TS_MATRIX_FX_META?c->meta.name:n->row.name;
            char compact[48];
            if(c)snprintf(compact,sizeof(compact),"%s %u",ts_matrix_effect_short_name(c->type),(unsigned)c->id);
            else if(n->row.kind==2 && n->row.index==TS_MATRIX_TRACKER)snprintf(compact,sizeof(compact),"TRK BUS");
            else if(n->row.kind==2 && n->row.index==TS_MATRIX_LINK)snprintf(compact,sizeof(compact),"LIVE LINK");
            else snprintf(compact,sizeof(compact),"%s",title);
            fx_node_rect(x,y,width,height,title,compact,detail,c?(c->enabled?fx_color(c->type):FX_MUTED):fx_color(0),fx_chosen(n->row));
            if(n->destination>=0)fx_rect(x-4,y+height/2-4,9,9,FX_CABLE);
            if(n->at>=0)fx_rect(x+width-4,y+height/2-4,9,9,FX_CABLE);
            int sc=n->destination>=0?ts_matrix_sidechain_for(n->destination):-1;
            if(sc>=0 && ts_matrix_destination_active(&w->controls,sc))fx_rect(x-4,y+(int)(height*.8f)-4,9,9,0xffbc98ccu);
            float peak=(n->row.kind==2 || n->row.kind==4)?w->peaks[n->row.index]:0;
            if(peak>0 && x>20 && x+width<1260 && y+height<FX_BOTTOM && y>=FX_TOP)mx_signal(x+8,y+height-8,width-16,3,peak,MX_GREEN);
        }
        if(w->drag==FX_DRAG_WIRE && fx_canvas.wire>=0) {
            for(int i=0;i<fx_canvas.count;++i)if(mx_same(fx_canvas.node[i].row,fx_canvas.source))
                fx_cable(fx_x(fx_canvas.node[i].x+FX_NODE_W),fx_y(fx_canvas.node[i].y+FX_NODE_H*.5f),fx_canvas.mouse_x,fx_canvas.mouse_y,MX_YELLOW,1,0,0);
        }
        if(w->drag==FX_DRAG_BOX){int ax=SDL_min((int)fx_canvas.pointer_x,fx_canvas.mouse_x),ay=SDL_min((int)fx_canvas.pointer_y,fx_canvas.mouse_y);
            int bx=SDL_max((int)fx_canvas.pointer_x,fx_canvas.mouse_x),by=SDL_max((int)fx_canvas.pointer_y,fx_canvas.mouse_y);
            fx_line(ax,ay,bx,ay,FX_FOCUS);fx_line(bx,ay,bx,by,FX_FOCUS);fx_line(bx,by,ax,by,FX_FOCUS);fx_line(ax,by,ax,ay,FX_FOCUS);}
        fx_inspector_paint(pages,bank);
        if(w->effect_open)mx_effect_paint();
        else if(w->compressor_open)mx_compressor_paint();
        else {
            mx_text(24,707,"Double-click empty space: add effect     Drag OUT to IN: connect",FX_TEXT,2);
            mx_text(24,739,"Drag empty space: select     M: bypass     Delete: remove selection",FX_TEXT,2);
            mx_text(24,771,"Z: frame selection     Shift+Z: fit all     Middle-drag: pan",FX_TEXT,2);
        }
        snprintf(label,sizeof(label),w->controls.enabled?"EFFECTS / %d NODES / %.0F%%":"SAVED GRAPH / INACTIVE",fx_canvas.count,w->layout->canvas_zoom*100);mx_text(24,80,label,FX_DIM,2);
    }
    mx_text(22,24,"FX CANVAS",FX_CABLE,3);
    fx_button(292,18,200,meta?"BACK TO FX":"MATRIX F9",0);
    fx_button(502,18,92,"UNDO",0);fx_button(604,18,92,"REDO",0);
    fx_button(706,18,192,"+ EFFECT",0);fx_button(908,18,100,"FIT",0);
    fx_button(1018,18,238,"MOSAIC / CLOSE",0);
    if(!meta){fx_button(660,70,196,w->layout->canvas_straight?"STRAIGHT":"SPLINES",0);
        fx_button(866,70,388,"CLEAR CABLES",0);}
    if(!w->controls.enabled)fx_button(300,70,284,"USE SAVED CHAIN",0);
    if(w->status[0])fx_panel_text(24,meta?814:801,1230,meta?25:38,w->status,FX_FOCUS,2,meta?1:2);
    else mx_text(24,815,"Shift+F9: FX Canvas     F9: Matrix     Esc: back     Ctrl+Z: undo",FX_DIM,2);
    if(fx_canvas.popup)fx_popup_paint();
    (void)audio;
}
static void fx_view(int canvas,TsSamplePages *pages,TsInstrument *bank)
{
    MatrixWindow *w=&matrix_window;w->canvas=canvas;w->drag=0;w->effect_menu=0;fx_canvas.popup=0;fx_canvas.meta=0;fx_canvas.wire=-1;
    if(canvas){w->filter=0;mx_rows(pages,bank);fx_build(pages,bank);if(!w->layout->canvas_view)fx_canvas.fit=1;}
    SDL_SetWindowTitle(w->window,canvas?"TapeSister - FX Canvas (Shift+F9)":"TapeSister - Routing Matrix (F9)");w->dirty=1;
}
#include "main_sdl_fx_canvas_input.inc"
