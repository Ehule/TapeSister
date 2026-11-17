/* Non-nested presentation groups. Membership is explicit, never inferred from
   overlap. Small rugs paint last and win background/title hit testing. */
static TsFxCanvasGroup *fx_group(uint32_t id)
{return ts_matrix_layout_group(matrix_window.layout,id);}
static SDL_Rect fx_group_rect(const TsFxCanvasGroup *g)
{return (SDL_Rect){fx_x(g->x),fx_y(g->y),(int)ceilf(g->w*matrix_window.layout->canvas_zoom),(int)ceilf(g->h*matrix_window.layout->canvas_zoom)};}
static int fx_group_visible(uint32_t id)
{
    if(!fx_canvas.movement && !fx_canvas.active_view)return 1;
    for(int i=0;i<fx_canvas.count;++i){TsFxCanvasPosition *p=fx_position(fx_canvas.node[i].row,0);if(p && p->group==id)return 1;}
    for(int i=0;i<fx_label_limit();++i)if(fx_label_visible(i) && fx_label_at(i)->group==id)return 1;
    return 0;
}
static uint32_t fx_group_hit(int x,int y,int border)
{
    TsMatrixLayout *l=matrix_window.layout;TsFxCanvasGroup *best=NULL;
    for(int i=0;i<l->group_count;++i){TsFxCanvasGroup *g=&l->groups[i];if(!fx_group_visible(g->id))continue;
        SDL_Rect r=fx_group_rect(g);if(x<r.x || y<r.y || x>=r.x+r.w || y>=r.y+r.h)continue;
        if(!best || g->w*g->h<=best->w*best->h)best=g;}
    if(!best)return 0;SDL_Rect r=fx_group_rect(best);
    if(border && x>=r.x+7 && x<r.x+r.w-7 && y>=r.y+24 && y<r.y+r.h-7)return 0;
    return best->id;
}
static void fx_group_blend(SDL_Rect r,uint32_t color,float opacity)
{
    int a=(int)lroundf(opacity*255);if(!a)return;
    int left=mx_px(SDL_max(20,r.x)),right=mx_px(SDL_min(fx_right(),r.x+r.w));
    int top=mx_py(SDL_max(FX_TOP,r.y)),bottom=mx_py(SDL_min(FX_BOTTOM,r.y+r.h));
    for(int y=top;y<bottom;++y)for(int x=left;x<right;++x){uint32_t *p=&matrix_window.pixels[(size_t)y*mx_pixel_w()+x],v=*p;
        unsigned red=(((color>>16)&255)*a+((v>>16)&255)*(255-a)+127)/255;
        unsigned green=(((color>>8)&255)*a+((v>>8)&255)*(255-a)+127)/255;
        unsigned blue=((color&255)*a+(v&255)*(255-a)+127)/255;*p=0xff000000u|(red<<16)|(green<<8)|blue;}
}
static void fx_groups_paint(void)
{
    TsMatrixLayout *l=matrix_window.layout;int order[TS_FX_GROUP_LIMIT];
    for(int i=0;i<l->group_count;++i){order[i]=i;for(int j=i;j>0 && l->groups[order[j-1]].w*l->groups[order[j-1]].h<l->groups[order[j]].w*l->groups[order[j]].h;--j){int t=order[j];order[j]=order[j-1];order[j-1]=t;}}
    for(int i=0;i<l->group_count;++i){TsFxCanvasGroup *g=&l->groups[order[i]];if(!fx_group_visible(g->id))continue;
        SDL_Rect r=fx_group_rect(g);if(r.x+r.w<20 || r.x>fx_right() || r.y+r.h<FX_TOP || r.y>FX_BOTTOM)continue;
        fx_group_blend(r,g->color,g->opacity);uint32_t ink=g->id==fx_canvas.group_selected?FX_FOCUS:g->color;
        fx_rect(r.x,r.y,r.w,2,ink);fx_rect(r.x,r.y+r.h-2,r.w,2,ink);fx_rect(r.x,r.y,2,r.h,ink);fx_rect(r.x+r.w-2,r.y,2,r.h,ink);
    }
}
/* The cable raster includes everything beneath its antialiased strokes.
   Keep that background's exact inputs in the cache key, even with no cables. */
static void fx_groups_cache_background(void)
{
    static struct { TsFxCanvasGroup groups[TS_FX_GROUP_LIMIT];int count;
        uint32_t selected,focus;float x,y,zoom; } previous,current;
    memset(&current,0,sizeof(current));TsMatrixLayout *l=matrix_window.layout;
    current.selected=fx_canvas.group_selected;current.focus=FX_FOCUS;
    current.x=l->canvas_x;current.y=l->canvas_y;current.zoom=l->canvas_zoom;
    for(int i=0;i<l->group_count;++i)if(fx_group_visible(l->groups[i].id))
        current.groups[current.count++]=l->groups[i];
    if(memcmp(&current,&previous,sizeof(current)))fx_cable_cache.valid=0;
    previous=current;
}
/* Keep the handle legible when a card from another rug overlaps its title. */
static void fx_groups_titles(void)
{
    TsMatrixLayout *l=matrix_window.layout;
    for(int i=0;i<l->group_count;++i){TsFxCanvasGroup *g=&l->groups[i];if(!fx_group_visible(g->id))continue;
        SDL_Rect r=fx_group_rect(g);if(fx_group_hit(r.x+8,r.y+12,0)!=g->id)continue;
        const char *name=g->name[0]?g->name:"GROUP";int width=SDL_min(r.w-16,(int)strlen(name)*12+12);if(width<16)continue;
        fx_rect(r.x+5,r.y+3,width+6,22,FX_PANEL);
        SDL_Rect title={r.x+8,r.y+6,width,18},clip={20,FX_TOP,fx_right()-20,FX_BOTTOM-FX_TOP};
        fx_text_box_scaled(title,clip,name,g->id==fx_canvas.group_selected?FX_FOCUS:g->color,1,1,1);
    }
}
static void fx_group_bounds(float x,float y,float w,float h,float *left,float *top,float *right,float *bottom)
{*left=fminf(*left,x);*top=fminf(*top,y);*right=fmaxf(*right,x+w);*bottom=fmaxf(*bottom,y+h);}
static void fx_group_create(void)
{
    MatrixWindow *w=&matrix_window;TsMatrixLayout *l=w->layout;if(!mx_editable() || !w->pending)return;
    if(l->group_count>=TS_FX_GROUP_LIMIT){snprintf(w->status,sizeof(w->status),"GROUP LIMIT REACHED (64).");return;}
    float left=1e9f,top=1e9f,right=-1e9f,bottom=-1e9f;int members=0;uint32_t id=1;
    for(int i=0;i<l->group_count;++i)if(l->groups[i].id>=id)id=l->groups[i].id+1;if(!id)return;
    for(int i=0;i<fx_canvas.count;++i){FxCanvasNode *n=&fx_canvas.node[i];if(!fx_chosen(n->row))continue;
        TsFxCanvasPosition *p=fx_position(n->row,1);if(!p){*l=w->pending->layout;return;}p->x=n->x;p->y=n->y;p->group=id;
        fx_group_bounds(n->x,n->y,fx_node_w(n),fx_node_h(n),&left,&top,&right,&bottom);++members;}
    for(int i=0;i<fx_label_limit();++i)if(fx_label_chosen(i) && fx_label_visible(i)){
        TsFxCanvasLabel *p=fx_label_at(i);SDL_Rect r=fx_text_object_rect(i);p->group=id;
        fx_group_bounds((r.x-20)/l->canvas_zoom-l->canvas_x,(r.y-FX_TOP)/l->canvas_zoom-l->canvas_y,r.w/l->canvas_zoom,r.h/l->canvas_zoom,&left,&top,&right,&bottom);++members;}
    if(!members){snprintf(w->status,sizeof(w->status),"SELECT CARDS AND/OR TEXT, THEN CTRL+G.");return;}
    TsFxCanvasGroup *g=&l->groups[l->group_count++];*g=(TsFxCanvasGroup){.id=id,.color=0xffb29acdu,.x=left-48,.y=top-64,.w=right-left+96,.h=bottom-top+112,.opacity=.12f};
    snprintf(g->name,sizeof(g->name),"GROUP %u",id);fx_clear_selection();fx_clear_wires();fx_canvas.group_selected=id;w->effect_open=w->compressor_open=0;
    snprintf(w->status,sizeof(w->status),"GROUPED %d OBJECTS / DRAG RUG: MOVE ALL / ALT+G: UNGROUP / RIGHT-CLICK: ACTIONS / F2: NAME, COLOR, OPACITY",members);
}
static void fx_group_ungroup(void)
{
    TsMatrixLayout *l=matrix_window.layout;uint32_t ids[TS_FX_GROUP_LIMIT];int count=0;
    for(int i=0;i<l->group_count;++i){uint32_t id=l->groups[i].id;int chosen=id==fx_canvas.group_selected;
        for(int j=0;!chosen && j<fx_canvas.count;++j){TsFxCanvasPosition *p=fx_position(fx_canvas.node[j].row,0);chosen=p && p->group==id && fx_chosen(fx_canvas.node[j].row);}
        for(int j=0;!chosen && j<fx_label_limit();++j){TsFxCanvasLabel *p=fx_label_at(j);chosen=p && p->group==id && fx_label_chosen(j);}
        if(chosen)ids[count++]=id;}
    for(int k=0;k<count;++k){uint32_t id=ids[k];
        for(int i=0;i<l->canvas_count;++i)if(l->canvas[i].group==id)l->canvas[i].group=0;
        for(int i=0;i<fx_label_limit();++i){TsFxCanvasLabel *p=fx_label_at(i);if(p && p->group==id)p->group=0;}
        for(int i=0;i<l->group_count;++i)if(l->groups[i].id==id){memmove(l->groups+i,l->groups+i+1,(--l->group_count-i)*sizeof(l->groups[0]));memset(l->groups+l->group_count,0,sizeof(l->groups[0]));break;}}
    fx_canvas.group_selected=0;snprintf(matrix_window.status,sizeof(matrix_window.status),"UNGROUPED %d RUGS / OBJECT POSITIONS RETAINED / CTRL+Z: UNDO",count);
}
static void fx_groups_clamp(void)
{
    TsMatrixLayout *l=matrix_window.layout;
    for(int i=0;i<l->canvas_count;++i){TsFxCanvasPosition *p=&l->canvas[i];TsFxCanvasGroup *g=fx_group(p->group);if(!g)continue;
        float maxscale=fminf((g->w-16)/FX_NODE_W,(g->h-40)/FX_NODE_H),scale=p->scale?p->scale:1;
        if(scale>maxscale)p->scale=scale=fmaxf(.25f,maxscale);
        p->x=SDL_clamp(p->x,g->x+8,g->x+g->w-8-FX_NODE_W*scale);p->y=SDL_clamp(p->y,g->y+32,g->y+g->h-8-FX_NODE_H*scale);}
    for(int i=0;i<fx_label_limit();++i){TsFxCanvasLabel *p=fx_label_at(i);if(!p)continue;TsFxCanvasGroup *g=fx_group(p->group);if(!g)continue;
        SDL_Rect r=fx_text_object_rect(i);float z=l->canvas_zoom,width=r.w/z,height=r.h/z;
        float fit=fminf(1,fminf((g->w-16)/width,(g->h-40)/height));if(fit<1){p->scale=fmaxf(.25f,p->scale*fit);r=fx_text_object_rect(i);width=r.w/z;height=r.h/z;}
        float x=(r.x-20)/z-l->canvas_x,y=(r.y-FX_TOP)/z-l->canvas_y;
        p->x+=SDL_clamp(x,g->x+8,g->x+g->w-8-width)-x;p->y+=SDL_clamp(y,g->y+32,g->y+g->h-8-height)-y;}
}
static void fx_group_begin(uint32_t id,int x,int y)
{
    if(!fx_group(id) || !mx_editable() || !matrix_window.pending)return;
    fx_clear_selection();fx_clear_wires();fx_canvas.group_selected=id;matrix_window.effect_open=matrix_window.compressor_open=0;
    fx_canvas.pointer_x=x;fx_canvas.pointer_y=y;matrix_window.drag=FX_DRAG_GROUP;SDL_CaptureMouse(SDL_TRUE);
}
static int fx_group_drag(SDL_Event *e,int x,int y)
{
    MatrixWindow *w=&matrix_window;if(w->drag!=FX_DRAG_GROUP)return 0;
    if(e->type==SDL_KEYDOWN && e->key.keysym.sym==SDLK_ESCAPE){fx_cancel_gesture();return 1;}
    if(e->type==SDL_MOUSEMOTION || e->type==SDL_MOUSEBUTTONUP){
        TsFxCanvasGroup *g=fx_group(fx_canvas.group_selected),*old=w->pending?ts_matrix_layout_group(&w->pending->layout,fx_canvas.group_selected):NULL;
        if(g && old){float dx=(x-fx_canvas.pointer_x)/w->layout->canvas_zoom,dy=(y-fx_canvas.pointer_y)/w->layout->canvas_zoom;
            if(w->layout->canvas_snap && !(SDL_GetModState()&KMOD_ALT)){dx=fx_snap(old->x+dx)-old->x;dy=fx_snap(old->y+dy)-old->y;}
            dx=SDL_clamp(dx,-100000.f-old->x,100000.f-old->x);dy=SDL_clamp(dy,-100000.f-old->y,100000.f-old->y);g->x=old->x+dx;g->y=old->y+dy;
            for(int i=0;i<w->layout->canvas_count;++i){TsFxCanvasPosition *p=&w->layout->canvas[i];if(p->group==g->id){p->x=w->pending->layout.canvas[i].x+dx;p->y=w->pending->layout.canvas[i].y+dy;}}
            for(int i=0;i<w->layout->label_count;++i){TsFxCanvasLabel *p=&w->layout->labels[i];if(p->group==g->id){p->x=w->pending->layout.labels[i].x+dx;p->y=w->pending->layout.labels[i].y+dy;}}
            for(int i=0;i<w->layout->heading_count;++i){TsFxCanvasLabel *p=&w->layout->headings[i].label;if(p->group==g->id){p->x=w->pending->layout.headings[i].label.x+dx;p->y=w->pending->layout.headings[i].label.y+dy;}}
        }
        if(e->type==SDL_MOUSEBUTTONUP){w->drag=0;SDL_CaptureMouse(SDL_FALSE);}return 1;
    }
    return 1;
}
