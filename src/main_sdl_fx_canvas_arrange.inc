/* Presentation-only arrangement; natural names put "Effect 2" before "Effect 10". */
static int fx_natural_compare(const char *a,const char *b)
{
    while(*a && *b) {
        if(*a>='0' && *a<='9' && *b>='0' && *b<='9') {
            const char *ae=a,*be=b;while(*ae>='0' && *ae<='9')++ae;while(*be>='0' && *be<='9')++be;
            while(a<ae && *a=='0')++a;while(b<be && *b=='0')++b;
            if(ae-a!=be-b)return ae-a<be-b?-1:1;
            int n=memcmp(a,b,(size_t)(ae-a));if(n)return n;
            a=ae;b=be;continue;
        }
        int ac=SDL_tolower((unsigned char)*a),bc=SDL_tolower((unsigned char)*b);
        if(ac!=bc)return ac<bc?-1:1;
        ++a;++b;
    }
    return !!*a-!!*b;
}
static const char *fx_node_title(const FxCanvasNode *n)
{
    TsFxCanvasPosition *p=fx_position(n->row,0);if(p && p->name[0])return p->name;
    const TsMatrixEffectControls *c=n->row.kind==4?&matrix_window.controls.effect[ts_matrix_effect_index(n->row.index)]:NULL;
    return c && c->type==TS_MATRIX_FX_SUNVOX?ts_sunvox_name(&c->sunvox):
        c && c->type==TS_MATRIX_FX_META?c->meta.name:n->row.name;
}
static int fx_arrange_compare(const FxCanvasNode *a,const FxCanvasNode *b,int type)
{
    if(type) {
        /* Sources, buses, added effects (by type), then outputs. */
        int ak=a->row.kind==4?100+matrix_window.controls.effect[ts_matrix_effect_index(a->row.index)].type:a->row.kind==5?200:a->row.kind;
        int bk=b->row.kind==4?100+matrix_window.controls.effect[ts_matrix_effect_index(b->row.index)].type:b->row.kind==5?200:b->row.kind;
        if(ak!=bk)return ak<bk?-1:1;
    }
    int n=fx_natural_compare(fx_node_title(a),fx_node_title(b));if(n)return n;
    if(a->row.canvas!=b->row.canvas)return a->row.canvas<b->row.canvas?-1:1;
    uint64_t ai=mx_row_id(a->row),bi=mx_row_id(b->row);return ai<bi?-1:ai>bi;
}
static int fx_object_selected(const FxCanvasNode *n)
{
    TsFxCanvasPosition *p=fx_position(n->row,0);
    return fx_chosen(n->row) || (fx_canvas.group_selected && p && p->group==fx_canvas.group_selected);
}
static void fx_arrange(int type,TsSamplePages *pages,TsInstrument *bank)
{
    MatrixWindow *w=&matrix_window;if(!mx_editable())return;
    fx_build(pages,bank);
    FxCanvasNode **nodes=malloc((size_t)fx_canvas.count*sizeof(*nodes));if(!nodes)return;
    int count=0;float x=1e9f,y=1e9f,width=FX_NODE_W,height=FX_NODE_H;
    for(int i=0;i<fx_canvas.count;++i)if(fx_object_selected(&fx_canvas.node[i])) {
        FxCanvasNode *n=&fx_canvas.node[i];nodes[count++]=n;
        x=fminf(x,n->x);y=fminf(y,n->y);width=fmaxf(width,fx_node_w(n));height=fmaxf(height,fx_node_h(n));
    }
    if(count<2){free(nodes);snprintf(w->status,sizeof(w->status),"SELECT TWO OR MORE CARDS TO ARRANGE.");return;}
    for(int i=1;i<count;++i) {FxCanvasNode *n=nodes[i];int j=i;
        while(j && fx_arrange_compare(nodes[j-1],n,type)>0){nodes[j]=nodes[j-1];--j;}nodes[j]=n;}
    int columns=(int)ceilf(sqrtf((float)count));
    float dx=ceilf((width+46)/FX_GRID)*FX_GRID,dy=ceilf((height+34)/FX_GRID)*FX_GRID;
    x=roundf(x/FX_GRID)*FX_GRID;y=roundf(y/FX_GRID)*FX_GRID;
    for(int i=0;i<count;++i)if(!fx_position(nodes[i]->row,1)) {
        *w->layout=w->pending->layout;free(nodes);snprintf(w->status,sizeof(w->status),"UNCHANGED: CANVAS POSITION STORAGE FULL.");return;
    }
    for(int i=0;i<count;++i){TsFxCanvasPosition *p=fx_position(nodes[i]->row,0);p->x=x+(i%columns)*dx;p->y=y+(i/columns)*dy;
        TsFxCanvasGroup *g=ts_matrix_layout_group(w->layout,p->group);
        if(g){float left=fminf(g->x,p->x-32),top=fminf(g->y,p->y-48);
            g->w=fmaxf(g->x+g->w,p->x+fx_node_w(nodes[i])+32)-left;
            g->h=fmaxf(g->y+g->h,p->y+fx_node_h(nodes[i])+32)-top;g->x=left;g->y=top;}
    }
    free(nodes);
    if(!ts_matrix_layout_valid(w->layout)) {
        *w->layout=w->pending->layout;snprintf(w->status,sizeof(w->status),"UNCHANGED: ARRANGEMENT WOULD EXCEED CANVAS BOUNDS.");return;
    }
    snprintf(w->status,sizeof(w->status),"ARRANGED %d CARDS BY %s / CTRL+Z: UNDO",count,type?"TYPE, THEN NAME":"NAME");
}
