/* A spatial view of the Matrix's exact routes. Only the existing Matrix
   transactions may change audio; positions, pan and zoom are view metadata. */
enum { FX_NODE_W=210, FX_NODE_H=86, FX_TOP=116, FX_BOTTOM=682 };
typedef struct { MatrixRow row; int at,destination,rank; float x,y; } FxCanvasNode;
static struct {
    FxCanvasNode node[MATRIX_CAPACITY+TS_MATRIX_DESTINATIONS];
    int count,dest[TS_MATRIX_DESTINATIONS],all,fit,wire,destination,mouse_x,mouse_y;
    MatrixRow source,moving;
    float origin_x,origin_y,pointer_x,pointer_y;
    uint32_t meta;
} fx_canvas={.wire=-1,.destination=-1};
static void fx_context_reset(void)
{fx_canvas.meta=0;fx_canvas.wire=fx_canvas.destination=-1;fx_canvas.fit=0;}
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
    if(l->canvas_count==TS_FX_CANVAS_POSITIONS) {
        /* Reclaim positions of deleted instances before reusing a view slot. */
        for(int i=l->canvas_count-1;i>=0;--i)if(l->canvas[i].kind==4) {
            int alive=0;for(int j=0;j<TS_MATRIX_EFFECT_CAPACITY;++j)alive|=matrix_window.controls.effect[j].id==l->canvas[i].id;
            if(!alive)l->canvas[i]=l->canvas[--l->canvas_count];
        }
    }
    if(l->canvas_count==TS_FX_CANVAS_POSITIONS)return NULL;
    TsFxCanvasPosition *p=&l->canvas[l->canvas_count++];memset(p,0,sizeof(*p));p->kind=kind;p->id=id;return p;
}
static void fx_build(TsSamplePages *pages,TsInstrument *bank)
{
    MatrixWindow *w=&matrix_window;int wanted[TS_MATRIX_DESTINATIONS]={0};
    if(w->filter){w->filter=0;mx_rows(pages,bank);}
    fx_canvas.count=0;for(int d=0;d<TS_MATRIX_DESTINATIONS;++d)fx_canvas.dest[d]=-1;
    wanted[TS_MATRIX_MAIN]=1;
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)if(w->controls.effect[i].id)wanted[TS_MATRIX_EFFECT_BASE+i]=1;
    /* Sources are endpoints, not copies of sample/generator editors. Ordinary
       tile/card/track membership remains represented by its source bus. */
    for(int i=0;i<w->count;++i) {
        MatrixRow row=w->rows[i];TsSourceRoute raw=mx_route(row,pages,bank);
        int processor=(row.kind==2 || row.kind==4) && ts_matrix_is_processor(row.index);
        if(processor)continue;
        int keep=row.kind==2?(row.index>=TS_MATRIX_FM && row.index<=TS_MATRIX_TRACKER):
            raw.matrix_enabled || raw.mix_enabled;
        if(row.kind==2 && ts_matrix_input_pair(row.index)>=0) {
            keep=fx_canvas.all;
            for(int d=0;d<TS_MATRIX_DESTINATIONS;++d)keep|=raw.matrix_gain[d]!=0;
        }
        if(!keep || (raw.mode==TS_SOURCE_INHERIT && !raw.matrix_enabled))continue;
        FxCanvasNode *n=&fx_canvas.node[fx_canvas.count++];*n=(FxCanvasNode){.row=row,.at=i,.destination=-1};
        TsSourceRoute route=mx_display_route(raw);
        for(int d=0;d<TS_MATRIX_DESTINATIONS;++d)wanted[d]|=route.matrix_gain[d]!=0;
    }
    for(int i=0;i<fx_canvas.count;++i)if(fx_canvas.node[i].row.kind==2 && fx_canvas.node[i].row.index==TS_MATRIX_MOSAIC) {
        FxCanvasNode first=fx_canvas.node[0];fx_canvas.node[0]=fx_canvas.node[i];fx_canvas.node[i]=first;break;
    }
    for(int k=0;k<TS_MATRIX_PROCESSORS;++k) {
        int p=ts_matrix_processor(k),sc=ts_matrix_sidechain_for(p);
        if(sc>=0 && wanted[sc])wanted[p]=1;
    }
    for(int pass=0;pass<TS_MATRIX_PROCESSORS;++pass)for(int k=0;k<TS_MATRIX_PROCESSORS;++k) {
        int p=ts_matrix_processor(k),sc=ts_matrix_sidechain_for(p);
        if(sc>=0 && wanted[sc] && ts_matrix_destination_active(&w->controls,sc))wanted[p]=1;
        if(!wanted[p] && !fx_canvas.all)continue;
        for(int d=0;d<TS_MATRIX_DESTINATIONS;++d)wanted[d]|=w->controls.row[p].matrix_gain[d]!=0;
    }
    for(int i=0;i<w->count;++i) {
        MatrixRow row=w->rows[i];
        if((row.kind!=2 && row.kind!=4) || !ts_matrix_is_processor(row.index))continue;
        if(!wanted[row.index] && !fx_canvas.all)continue;
        FxCanvasNode *n=&fx_canvas.node[fx_canvas.count];*n=(FxCanvasNode){.row=row,.at=i,.destination=row.index};
        fx_canvas.dest[row.index]=fx_canvas.count++;
        int sc=ts_matrix_sidechain_for(row.index);if(sc>=0 && ts_matrix_destination_active(&w->controls,sc))fx_canvas.dest[sc]=fx_canvas.count-1;
    }
    for(int d=0;d<TS_MATRIX_DESTINATIONS;++d)if(fx_canvas.dest[d]<0 && wanted[d] &&
        ts_matrix_destination_active(&w->controls,d)) {
        FxCanvasNode *n=&fx_canvas.node[fx_canvas.count];
        *n=(FxCanvasNode){.row={.kind=5,.index=d},.at=-1,.destination=d};
        mx_destination(n->row.name,sizeof(n->row.name),d,1);fx_canvas.dest[d]=fx_canvas.count++;
    }
    for(int pass=0;pass<TS_MATRIX_PROCESSORS+1;++pass) {
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
    int rows[TS_MATRIX_PROCESSORS+3]={0};
    for(int i=0;i<fx_canvas.count;++i) {
        FxCanvasNode *n=&fx_canvas.node[i];n->x=n->rank*260.f;n->y=rows[n->rank]++*120.f;
        TsFxCanvasPosition *p=fx_position(n->row,0);if(p){n->x=p->x;n->y=p->y;}
    }
}
static void fx_fit(void)
{
    if(!fx_canvas.count)return;
    float left=1e9f,top=1e9f,right=-1e9f,bottom=-1e9f;
    for(int i=0;i<fx_canvas.count;++i){FxCanvasNode *n=&fx_canvas.node[i];left=fminf(left,n->x);top=fminf(top,n->y);right=fmaxf(right,n->x+FX_NODE_W);bottom=fmaxf(bottom,n->y+FX_NODE_H);}
    TsMatrixLayout *l=matrix_window.layout;
    l->canvas_zoom=SDL_clamp(fminf(1190/(right-left+40),(FX_BOTTOM-FX_TOP-50)/(bottom-top+30)),.2f,1.f);
    l->canvas_x=-left+((1240/l->canvas_zoom)-(right-left))*.5f;
    l->canvas_y=-top+(((FX_BOTTOM-FX_TOP)/l->canvas_zoom)-(bottom-top))*.5f;
    l->canvas_view=1;
    fx_canvas.fit=0;
}
static int fx_x(float x){TsMatrixLayout *l=matrix_window.layout;return 20+(int)lrintf((x+l->canvas_x)*l->canvas_zoom);}
static int fx_y(float y){TsMatrixLayout *l=matrix_window.layout;return FX_TOP+(int)lrintf((y+l->canvas_y)*l->canvas_zoom);}
static void fx_rect(int x,int y,int width,int height,uint32_t color)
{
    int r=SDL_min(1260,x+width),b=SDL_min(FX_BOTTOM,y+height);x=SDL_max(20,x);y=SDL_max(FX_TOP,y);
    if(r>x && b>y)mx_rect(x,y,r-x,b-y,color);
}
static void fx_line(int x,int y,int xx,int yy,uint32_t color)
{
    SDL_Rect clip={20,FX_TOP,1240,FX_BOTTOM-FX_TOP};
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
    if(abs(xx-x)>240 || xx<x) {
        /* Long direct routes travel in the margin, rather than appearing to
           plug into intervening nodes when they happen to share a row. */
        int lane=(abs(y+yy)/7)%5;
        int route_y=xx<x?FX_BOTTOM-14-lane*7:FX_TOP+16+lane*7;
        int px[6]={x,x+12,x+12,xx-12,xx-12,xx};
        int py[6]={y,y,route_y,route_y,yy,yy};
        for(int i=1;i<6;++i){if(draw)fx_line(px[i-1],py[i-1],px[i],py[i],color);
            distance=fminf(distance,fx_segment_distance(mx,my,px[i-1],py[i-1],px[i],py[i]));}
        if(draw){fx_line(xx-6,yy-4,xx,yy,color);fx_line(xx-6,yy+4,xx,yy,color);}return distance;
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
static void fx_node_rect(int x,int y,int width,int height,const char *name,const char *detail,uint32_t color,int selected)
{
    if(x+width<20 || x>1260 || y+height<FX_TOP || y>FX_BOTTOM)return;
    fx_rect(x,y,width,height,color);fx_rect(x+2,y+2,width-4,height-4,MX_PANEL);
    if(selected)fx_rect(x+3,y+3,width-6,3,MX_YELLOW);
    int scale=width>=110?2:1,chars=SDL_max(0,(width-20)/(6*scale));char label[64];
    snprintf(label,sizeof(label),"%.*s",SDL_min(chars,60),name);
    if(y+12>=FX_TOP && y+12+7*scale<FX_BOTTOM && x+10>=20 && x+10+6*scale*(int)strlen(label)<1260)
        mx_text(x+10,y+12,label,color,scale);
    snprintf(label,sizeof(label),"%.*s",SDL_min(60,SDL_max(0,(width-20)/6)),detail);
    if(height>60 && y+height-23>=FX_TOP && y+height-16<FX_BOTTOM && x+10>=20 && x+width<1261)
        mx_text(x+10,y+height-23,label,MX_DIM,1);
}
static void fx_chain_cable(int x,int y,int xx,int yy)
{
    if(abs(y-yy)<4) {
        fx_line(x,y,xx,yy,MX_GREEN);int mid=(x+xx)/2,direction=xx>x?1:-1;
        fx_line(mid-direction*6,y-4,mid,y,MX_GREEN);fx_line(mid-direction*6,y+4,mid,y,MX_GREEN);
    } else {
        int side=SDL_min(1244,SDL_max(x,xx)+18);
        fx_line(x,y,side,y,MX_GREEN);fx_line(side,y,side,yy,MX_GREEN);fx_line(side,yy,xx,yy,MX_GREEN);
        int mid=(y+yy)/2;fx_line(side-4,mid-6,side,mid,MX_GREEN);fx_line(side+4,mid-6,side,mid,MX_GREEN);
    }
}
static void fx_meta_paint(TsMatrixEffectControls *c)
{
    TsMetaControls *rack=&c->meta;char label[80];
    snprintf(label,sizeof(label),"FX CANVAS / %.31s",rack->name);mx_text(24,78,label,MX_AMBER,2);
    int previous_x=108,previous_y=210;
    fx_node_rect(26,180,82,60,"IN","",MX_GREEN,0);
    for(int i=0;i<rack->count;++i) {
        int col=i<4?i:7-i,x=146+col*270,y=i<4?174:418;
        int inx=i<4?x:x+FX_NODE_W,outx=i<4?x+FX_NODE_W:x;
        fx_chain_cable(previous_x,previous_y,inx,y+FX_NODE_H/2);
        const TsMetaStage *s=&rack->stage[i];snprintf(label,sizeof(label),"STAGE %d / %s",i+1,s->enabled?"ACTIVE":"BYPASS");
        fx_node_rect(x,y,FX_NODE_W,FX_NODE_H,ts_sister_fx_type_name(s->type),label,s->enabled?MX_GREEN:MX_DIM,0);
        previous_x=outx;previous_y=y+FX_NODE_H/2;
    }
    int reverse=rack->count>4 || rack->count==4;
    int out_x=rack->count>4?SDL_max(26,previous_x-130):rack->count==4?1090:previous_x+40;
    int out_y=rack->count==4?300:previous_y-30;
    fx_chain_cable(previous_x,previous_y,reverse?out_x+82:out_x,out_y+30);
    fx_node_rect(out_x,out_y,82,60,"OUT","",MX_GREEN,0);
    mx_text(26,600,"DOUBLE-CLICK A STAGE: FULL RACK EDITOR / PARAMETERS, ORDER, BYPASS, MAPPINGS AND FILES",MX_DIM,1);
    mx_text(26,622,"THIS RACK IS SERIAL. PARALLEL BRANCHES ARE PATCHED ON THE OUTER FX CANVAS.",MX_DIM,1);
    for(int i=0;i<4;++i) {
        float v=rack->scene_mode && rack->captured==3?rack->scene[0][i]+(rack->scene[1][i]-rack->scene[0][i])*rack->morph:rack->macro[i];
        snprintf(label,sizeof(label),"%s %.0F%%",rack->label[i],v*100);int x=24+i*310;
        mx_text(x,710,label,MX_TEXT,2);mx_rect(x,746,286,6,MX_DIM);mx_rect(x+(int)(282*v),740,4,18,MX_GREEN);
    }
    mx_button(24,784,212,"RACK EDITOR",0);mx_text(260,794,"FOUR MACROS / DRAG OR WHEEL / AUDIO CONTINUES",MX_DIM,1);
}
static void fx_paint(AudioState *audio,TsSamplePages *pages,TsInstrument *bank)
{
    MatrixWindow *w=&matrix_window;mx_rect(0,0,MATRIX_W,MATRIX_H,MX_BG);char label[100];
    fx_build(pages,bank);if(fx_canvas.fit)fx_fit();
    mx_rect(20,FX_TOP,1240,FX_BOTTOM-FX_TOP,0xff10171bu);
    for(int x=40;x<1260;x+=32)for(int y=FX_TOP+16;y<FX_BOTTOM;y+=32)mx_rect(x,y,1,1,0xff293238u);
    TsMatrixEffectControls *meta=fx_meta();
    if(meta)fx_meta_paint(meta);
    else {
        for(int i=0;i<fx_canvas.count;++i) {
            FxCanvasNode *n=&fx_canvas.node[i];if(n->at<0)continue;
            TsSourceRoute r=mx_display_route(mx_route(n->row,pages,bank));
            for(int d=0;d<TS_MATRIX_DESTINATIONS;++d)if(r.matrix_gain[d] && fx_canvas.dest[d]>=0) {
                FxCanvasNode *to=&fx_canvas.node[fx_canvas.dest[d]];int sc=d!=to->destination;
                int delayed=(n->row.kind==2 || n->row.kind==4) && (w->controls.delayed[n->row.index]&ts_matrix_feedback_bit(d));
                int selected=fx_canvas.wire>=0 && mx_same(n->row,fx_canvas.source) && d==fx_canvas.destination;
                uint32_t color=selected?MX_YELLOW:delayed?MX_AMBER:sc?0xffbc98ccu:MX_GREEN;
                int x=fx_x(n->x+FX_NODE_W),y=fx_y(n->y+FX_NODE_H*.5f),xx=fx_x(to->x),yy=fx_y(to->y+(sc?FX_NODE_H*.8f:FX_NODE_H*.5f));
                fx_cable(x,y,xx,yy,color,1,0,0);
                if(selected){snprintf(label,sizeof(label),"%+.1F DB%s",20*log10f(r.matrix_gain[d]/1000.f),sc?" / SC":delayed?" / DELAYED":"");
                    if(y>FX_TOP+20 && y<FX_BOTTOM-20 && x>20 && x<1100)mx_text(x+8,y-16,label,MX_YELLOW,1);}
            }
        }
        for(int i=0;i<fx_canvas.count;++i) {
            FxCanvasNode *n=&fx_canvas.node[i];int x=fx_x(n->x),y=fx_y(n->y),width=(int)(FX_NODE_W*w->layout->canvas_zoom),height=(int)(FX_NODE_H*w->layout->canvas_zoom);
            int slot=n->row.kind==4?ts_matrix_effect_index(n->row.index):-1;
            const TsMatrixEffectControls *c=slot>=0?&w->controls.effect[slot]:NULL;
            const char *detail=n->at<0?(mx_destination_available(n->destination)?"OUTPUT / RECEIVER":"N/A / PORT NOT OPEN"):c?(c->enabled?(c->type==TS_MATRIX_FX_META?"DOUBLE-CLICK TO ENTER":"ACTIVE / SELECT TO EDIT"):"BYPASS"):"BUS / PATCH ENDPOINT";
            const char *title=c && c->type==TS_MATRIX_FX_META?c->meta.name:n->row.name;
            char compact[32];
            if(c && c->type!=TS_MATRIX_FX_META && width<180){snprintf(compact,sizeof(compact),"%s %u",ts_matrix_effect_short_name(c->type),(unsigned)c->id);title=compact;}
            fx_node_rect(x,y,width,height,title,detail,c && c->type==TS_MATRIX_FX_META?MX_AMBER:MX_GREEN,n->at>=0 && mx_chosen(n->at));
            if(n->destination>=0)fx_rect(x-4,y+height/2-4,9,9,MX_GREEN);
            if(n->at>=0)fx_rect(x+width-4,y+height/2-4,9,9,MX_GREEN);
            int sc=n->destination>=0?ts_matrix_sidechain_for(n->destination):-1;
            if(sc>=0 && ts_matrix_destination_active(&w->controls,sc))fx_rect(x-4,y+(int)(height*.8f)-4,9,9,0xffbc98ccu);
            float peak=(n->row.kind==2 || n->row.kind==4)?w->peaks[n->row.index]:0;
            if(peak>0 && x>20 && x+width<1260 && y+height<FX_BOTTOM && y>=FX_TOP)mx_signal(x+8,y+height-8,width-16,3,peak,MX_GREEN);
        }
        if(w->drag==41 && fx_canvas.wire>=0) {
            for(int i=0;i<fx_canvas.count;++i)if(mx_same(fx_canvas.node[i].row,fx_canvas.source))
                fx_cable(fx_x(fx_canvas.node[i].x+FX_NODE_W),fx_y(fx_canvas.node[i].y+FX_NODE_H*.5f),fx_canvas.mouse_x,fx_canvas.mouse_y,MX_YELLOW,1,0,0);
        }
        if(w->effect_menu || w->effect_open)mx_effect_paint();
        else if(w->compressor_open)mx_compressor_paint();
        else {mx_text(24,709,"DRAG OUT TO IN: CONNECT / SELECT CABLE: WHEEL LEVEL, DELETE DISCONNECTS",MX_TEXT,1);
            mx_text(24,735,"DRAG NODE: ARRANGE / MIDDLE DRAG: PAN / WHEEL: ZOOM / DOUBLE-CLICK META FX: ENTER",MX_DIM,1);
            mx_text(24,763,"SOURCES ENTER THROUGH BUSES; EXPLICIT TILE / TRACK / CARD ROUTES APPEAR AS ENDPOINTS.",MX_DIM,1);}
        snprintf(label,sizeof(label),w->controls.enabled?"EFFECTS / %d NODES / %.0F%%":"SAVED GRAPH / INACTIVE",fx_canvas.count,w->layout->canvas_zoom*100);mx_text(24,80,label,MX_DIM,2);
    }
    mx_text(22,24,"FX CANVAS",MX_GREEN,3);
    mx_button(292,18,200,meta?"BACK TO FX":"MATRIX F9",0);
    mx_button(502,18,92,"UNDO",0);mx_button(604,18,92,"REDO",0);
    mx_button(706,18,192,"+ EFFECT",0);mx_button(908,18,100,"FIT",0);
    mx_button(1018,18,238,"MOSAIC / CLOSE",0);
    if(!meta)mx_button(940,70,314,fx_canvas.all?"ALL ENDPOINTS":"CONNECTED / BUSES",fx_canvas.all);
    if(!w->controls.enabled)mx_button(620,70,284,"USE SAVED CHAIN",0);
    if(w->status[0])mx_text(24,816,w->status,MX_AMBER,1);
    else mx_text(24,816,"SHIFT+F9: FX CANVAS / F9: MATRIX / ESC: BACK OR CLOSE / CTRL+Z: UNDO",MX_DIM,1);
    (void)audio;
}
static void fx_view(int canvas,TsSamplePages *pages,TsInstrument *bank)
{
    MatrixWindow *w=&matrix_window;w->canvas=canvas;w->drag=0;w->effect_menu=0;fx_canvas.meta=0;fx_canvas.wire=-1;
    if(canvas){w->filter=0;mx_rows(pages,bank);fx_build(pages,bank);if(!w->layout->canvas_view)fx_canvas.fit=1;}
    SDL_SetWindowTitle(w->window,canvas?"TapeSister - FX Canvas (Shift+F9)":"TapeSister - Routing Matrix (F9)");w->dirty=1;
}
static int fx_event(SDL_Event *e,SDL_Window *main,SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,TsSamplePages *pages,TsInstrument *bank,int x,int y,int wheel)
{
    MatrixWindow *w=&matrix_window;TsMatrixLayout *l=w->layout;
    if(e->type==SDL_KEYDOWN) {
        if(e->key.keysym.sym==SDLK_ESCAPE){if(fx_canvas.meta)fx_canvas.meta=0;else if(w->effect_menu || w->effect_open)w->effect_menu=w->effect_open=0;else matrix_hide();return 1;}
        if(e->key.keysym.sym==SDLK_DELETE && fx_canvas.wire>=0) {
            int at=fx_source_at(fx_canvas.source);if(at>=0 && fx_canvas.destination>=0){mx_select(at,0);w->destination=fx_canvas.destination;mx_gain(device,audio,ui,pages,bank,0);}fx_canvas.wire=-1;return 1;
        }
        if(e->key.keysym.sym==SDLK_HOME){fx_build(pages,bank);fx_fit();return 1;}
        e->key.windowID=SDL_GetWindowID(main);return 0;
    }
    if(e->type==SDL_KEYUP){e->key.windowID=SDL_GetWindowID(main);return 0;}
    int click=e->type==SDL_MOUSEBUTTONDOWN,left=click && e->button.button==SDL_BUTTON_LEFT;
    if(left && y>=18 && y<46) {
        if(x>=292 && x<492){if(fx_canvas.meta)fx_canvas.meta=0;else fx_view(0,pages,bank);}
        else if(x>=502 && x<696)mx_undo(device,audio,ui,pages,bank,x>=604);
        else if(x>=706 && x<898){fx_canvas.meta=0;w->effect_menu=!w->effect_menu;w->effect_open=0;}
        else if(x>=908 && x<1008){fx_build(pages,bank);fx_fit();}
        else if(x>=1018){ui->mosaic_open=1;matrix_hide();SDL_RaiseWindow(main);}
        return 1;
    }
    if(left && y>=70 && y<98) {
        if(!w->controls.enabled && x>=620 && x<904)mx_activate(device,audio,ui,pages,bank);
        else if(x>=940){fx_canvas.all=!fx_canvas.all;fx_canvas.fit=1;}return 1;
    }
    TsMatrixEffectControls *meta=fx_meta();
    if(meta) {
        if(left && y>=784 && y<812 && x<236){meta_fx_open(meta->id);return 1;}
        if(left && e->button.clicks>=2)for(int i=0;i<meta->meta.count;++i) {
            int nx=146+(i<4?i:7-i)*270,ny=i<4?174:418;
            if(x>=nx && x<nx+FX_NODE_W && y>=ny && y<ny+FX_NODE_H){meta_fx_stage_open(meta->id,i);return 1;}
        }
        if((left || wheel) && y>=700 && y<770 && mx_editable()) {
            int i=(x-24)/310;if(x>=24 && i>=0 && i<4) {
                float v=meta->meta.scene_mode && meta->meta.captured==3?meta->meta.scene[0][i]+(meta->meta.scene[1][i]-meta->meta.scene[0][i])*meta->meta.morph:meta->meta.macro[i];
                ts_meta_macro_set(&meta->meta,i,wheel?v+wheel*((SDL_GetModState()&KMOD_SHIFT)?.002f:.02f):(x-24-i*310)/286.f);
                mx_commit_controls(device,audio,ui);if(left)w->drag=50+i;
            }return 1;
        }
        if(e->type==SDL_MOUSEMOTION && w->drag>=50 && w->drag<54 && mx_editable()) {
            int i=w->drag-50;ts_meta_macro_set(&meta->meta,i,(x-24-i*310)/286.f);mx_commit_controls(device,audio,ui);return 1;
        }
        if(e->type==SDL_MOUSEBUTTONUP)w->drag=0;
        return 1;
    }
    if((w->effect_menu || w->effect_open) && (y>=692 || (w->drag>=20 && w->drag<40))){mx_effect_event(e,device,audio,ui,pages,bank,x,y,wheel);return 1;}
    if(w->compressor_open && (y>=692 || (w->drag>=10 && w->drag<20))){mx_compressor_event(e,device,audio,ui,x,y,wheel);return 1;}
    fx_build(pages,bank);
    if(e->type==SDL_MOUSEBUTTONUP) {
        if(w->drag==41 && fx_canvas.wire>=0) {
            int at=fx_source_at(fx_canvas.source);
            for(int i=0;i<fx_canvas.count;++i) {
                FxCanvasNode *n=&fx_canvas.node[i];int xx=fx_x(n->x),yy=fx_y(n->y+FX_NODE_H*.5f);
                int dest=n->destination;
                int sc=dest>=0?ts_matrix_sidechain_for(dest):-1;
                if(sc>=0 && ts_matrix_destination_active(&w->controls,sc) && abs(y-fx_y(n->y+FX_NODE_H*.8f))<13){yy=fx_y(n->y+FX_NODE_H*.8f);dest=sc;}
                if(at>=0 && dest>=0 && abs(x-xx)<18 && abs(y-yy)<18){mx_select(at,0);w->destination=dest;mx_gain(device,audio,ui,pages,bank,1000);break;}
            }
        }
        w->drag=0;return 1;
    }
    if(e->type==SDL_MOUSEMOTION && w->drag>=40) {
        if(w->drag==40) {
            TsFxCanvasPosition *p=fx_position(fx_canvas.moving,0);
            if(p){p->x=SDL_clamp(fx_canvas.origin_x+(x-fx_canvas.pointer_x)/l->canvas_zoom,-100000.f,100000.f);p->y=SDL_clamp(fx_canvas.origin_y+(y-fx_canvas.pointer_y)/l->canvas_zoom,-100000.f,100000.f);}
        } else if(w->drag==42) {
            l->canvas_x=SDL_clamp(fx_canvas.origin_x+(x-fx_canvas.pointer_x)/l->canvas_zoom,-100000.f,100000.f);
            l->canvas_y=SDL_clamp(fx_canvas.origin_y+(y-fx_canvas.pointer_y)/l->canvas_zoom,-100000.f,100000.f);
        }
        fx_canvas.mouse_x=x;fx_canvas.mouse_y=y;return 1;
    }
    if(y<FX_TOP || y>=FX_BOTTOM)return 1;
    if(click && e->button.button==SDL_BUTTON_MIDDLE) {
        w->drag=42;fx_canvas.origin_x=l->canvas_x;fx_canvas.origin_y=l->canvas_y;fx_canvas.pointer_x=x;fx_canvas.pointer_y=y;return 1;
    }
    if(wheel) {
        if(fx_canvas.wire>=0 && fx_canvas.destination>=0) {
            int at=fx_source_at(fx_canvas.source);
            if(at>=0){mx_select(at,0);w->destination=fx_canvas.destination;int gain=mx_display_route(mx_route(w->rows[at],pages,bank)).matrix_gain[w->destination];
                if(gain)mx_gain(device,audio,ui,pages,bank,(int)lrintf(gain*powf(10,wheel*((SDL_GetModState()&KMOD_SHIFT)?.005f:.05f))));}
        } else {
            float old=l->canvas_zoom,next=SDL_clamp(old*powf(1.12f,wheel),.2f,2.f);
            l->canvas_x+=(x-20)*(1/next-1/old);l->canvas_y+=(y-FX_TOP)*(1/next-1/old);l->canvas_zoom=next;
        }return 1;
    }
    if(!left)return 1;
    for(int i=fx_canvas.count-1;i>=0;--i) {
        FxCanvasNode *n=&fx_canvas.node[i];int xx=fx_x(n->x),yy=fx_y(n->y),ww=(int)(FX_NODE_W*l->canvas_zoom),hh=(int)(FX_NODE_H*l->canvas_zoom);
        if(n->at>=0 && abs(x-(xx+ww))<16 && abs(y-(yy+hh/2))<16){fx_canvas.source=n->row;fx_canvas.wire=i;fx_canvas.destination=-1;w->drag=41;fx_canvas.mouse_x=x;fx_canvas.mouse_y=y;return 1;}
        if(x<xx || x>xx+ww || y<yy || y>yy+hh)continue;
        fx_canvas.wire=-1;
        if(n->at>=0){mx_select(n->at,0);w->effect_open=n->row.kind==4?(uint32_t)n->row.tile:0;w->effect_menu=0;
            w->compressor_open=n->row.kind==2 && n->row.index==TS_MATRIX_COMPRESSOR;}
        if(e->button.clicks>=2 && n->row.kind==4) {
            TsMatrixEffectControls *c=&w->controls.effect[ts_matrix_effect_index(n->row.index)];
            if(c->type==TS_MATRIX_FX_META){fx_canvas.meta=c->id;w->effect_open=0;return 1;}
            if(c->type==TS_MATRIX_FX_FILTER){character_filter_matrix_open(c->id);return 1;}
        }
        if(mx_editable()) {
            TsFxCanvasPosition *p=fx_position(n->row,1);
            if(p){p->x=n->x;p->y=n->y;fx_canvas.moving=n->row;fx_canvas.origin_x=p->x;fx_canvas.origin_y=p->y;fx_canvas.pointer_x=x;fx_canvas.pointer_y=y;w->drag=40;}
            else snprintf(w->status,sizeof(w->status),"SAVED POSITION LIMIT REACHED. EXISTING ROUTES ARE UNCHANGED.");
        }return 1;
    }
    float nearest=100;int found=-1,destination=-1;
    for(int i=0;i<fx_canvas.count;++i) {
        FxCanvasNode *n=&fx_canvas.node[i];if(n->at<0)continue;TsSourceRoute r=mx_display_route(mx_route(n->row,pages,bank));
        for(int d=0;d<TS_MATRIX_DESTINATIONS;++d)if(r.matrix_gain[d] && fx_canvas.dest[d]>=0) {
            FxCanvasNode *to=&fx_canvas.node[fx_canvas.dest[d]];
            float distance=fx_cable(fx_x(n->x+FX_NODE_W),fx_y(n->y+FX_NODE_H*.5f),fx_x(to->x),fx_y(to->y+(d==to->destination?FX_NODE_H*.5f:FX_NODE_H*.8f)),0,0,x,y);
            if(distance<nearest){nearest=distance;found=i;destination=d;}
        }
    }
    fx_canvas.wire=found;fx_canvas.destination=destination;
    if(found>=0){fx_canvas.source=fx_canvas.node[found].row;snprintf(w->status,sizeof(w->status),"CABLE SELECTED / WHEEL: LEVEL / DELETE: DISCONNECT / CLICK EMPTY SPACE: RELEASE");}
    else {w->effect_open=0;w->status[0]=0;}
    return 1;
}
