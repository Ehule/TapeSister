/* Movement headings use the existing text editor and gestures, with separate
   storage so migration never consumes the user's 64 free-text slots. */
static TsFxCanvasLabel *fx_label_at(int index)
{
    TsMatrixLayout *l=matrix_window.layout;
    if(index>=0 && index<l->label_count)return &l->labels[index];
    index-=TS_FX_LABEL_LIMIT;
    return index>=0 && index<l->heading_count?&l->headings[index].label:NULL;
}
static int fx_label_limit(void)
{return TS_FX_LABEL_LIMIT+matrix_window.layout->heading_count;}
static int fx_heading_member(const FxCanvasNode *n,uint64_t canvas)
{return canvas?n->row.kind==3 && n->row.canvas==canvas:n->row.kind==0 || n->row.kind==1;}
static int fx_label_visible(int index)
{
    TsFxCanvasLabel *p=fx_label_at(index);if(!p)return 0;
    if(index<TS_FX_LABEL_LIMIT)return 1;
    if(!p->text[0])return 0;
    uint64_t canvas=matrix_window.layout->headings[index-TS_FX_LABEL_LIMIT].canvas;
    for(int i=0;i<fx_canvas.count;++i)if(fx_heading_member(&fx_canvas.node[i],canvas))return 1;
    return 0;
}
static void fx_migrate_headings(void)
{
    TsMatrixLayout *l=matrix_window.layout;TsMosaicGallery *g=matrix_window.gallery;
    if(!g)return;
    /* Forget removed movements; surviving identities retain their edited text,
       location, size and deletion even after Gallery reordering or renaming. */
    for(int i=0;i<l->heading_count;) {
        uint64_t id=l->headings[i].canvas;int alive=!id;
        for(int c=0;c<g->count;++c)alive|=g->canvas[c].id==id;
        if(alive){++i;continue;}
        memmove(l->headings+i,l->headings+i+1,(size_t)(--l->heading_count-i)*sizeof(l->headings[0]));
        memset(l->headings+l->heading_count,0,sizeof(l->headings[0]));
        fx_canvas.label_selected=-1;
    }
    /* Run against the complete graph before applying movement/playing filters. */
    for(int c=-1;c<g->count;++c) {
        uint64_t id=c<0?0:g->canvas[c].id;int found=0;
        for(int i=0;i<l->heading_count;++i)found|=l->headings[i].canvas==id;
        if(found)continue;
        float left=1e9f,top=1e9f;int count=0;
        for(int i=0;i<fx_canvas.count;++i) {
            FxCanvasNode *n=&fx_canvas.node[i];if(!fx_heading_member(n,id))continue;
            left=fminf(left,n->x);top=fminf(top,n->y);++count;
        }
        if(!count || l->heading_count==TS_FX_HEADING_LIMIT)continue;
        TsFxCanvasHeading *h=&l->headings[l->heading_count++];memset(h,0,sizeof(*h));
        h->canvas=id;h->label.x=left;h->label.y=top;h->label.scale=1;
        const char *name=c<0?"OTHER SOURCES":g->canvas[c].name;
        /* Gallery names and existing text objects share printable ASCII. */
        for(int i=0;name[i] && i<TS_FX_LABEL_TEXT-1;++i)
            h->label.text[i]=(unsigned char)name[i]>=32 && (unsigned char)name[i]<=126?name[i]:'?';
    }
}
