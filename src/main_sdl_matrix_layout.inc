/* View positions never become graph/source IDs. Build and filter only on the
   control thread; unknown/new sources append after the saved custom order. */
_Static_assert(MATRIX_CAPACITY==TS_MATRIX_LAYOUT_ROWS,"Matrix layout must hold every source");
static uint64_t mx_row_id(MatrixRow r)
{return r.kind==1 || r.kind==3 || r.kind==4?r.tile:(uint64_t)r.index;}
static int mx_identity_compare(const void *a,const void *b)
{
    const MatrixRow *x=a,*y=b;
    if(x->kind!=y->kind)return x->kind<y->kind?-1:1;
    uint64_t left=mx_row_id(*x),right=mx_row_id(*y);
    return left<right?-1:left>right;
}
static int mx_order_compare(const void *a,const void *b)
{
    const MatrixRow *x=a,*y=b;
    return x->order<y->order?-1:x->order>y->order;
}
static int mx_filter_row(MatrixRow r,int f)
{
    if(!f)return 1;
    if(r.kind==0)return f==1;
    if(r.kind==1)return f==2;
    if(r.kind==3)return f==5;
    if(r.index==TS_MATRIX_TRACKER)return f==1;
    if(ts_matrix_is_processor(r.index))return f==4;
    if(r.index==TS_MATRIX_TILES)return f==2;
    if(r.index==TS_MATRIX_MOSAIC)return f==5;
    return f==3;
}
static void mx_layout_rows(void)
{
    MatrixWindow *w=&matrix_window;const TsMatrixLayout *l=w->layout;
    int saved=l?l->row_count:0;
    for(int i=0;i<w->count;++i)w->rows[i].order=saved+i;
    if(saved) {
        qsort(w->rows,w->count,sizeof(w->rows[0]),mx_identity_compare);
        for(int i=0;i<saved;++i) {
            MatrixRow key={.kind=l->row_kind[i],.tile=l->row_id[i]};
            if(key.kind==0 || key.kind==2)key.index=(int)l->row_id[i];
            MatrixRow *row=bsearch(&key,w->rows,w->count,sizeof(*row),mx_identity_compare);
            if(row)row->order=i;
        }
        qsort(w->rows,w->count,sizeof(w->rows[0]),mx_order_compare);
    }
    w->full_layout.row_count=w->count;
    for(int i=0;i<w->count;++i) {
        w->full_layout.row_kind[i]=(unsigned char)w->rows[i].kind;
        w->full_layout.row_id[i]=mx_row_id(w->rows[i]);
    }
    int count=0;
    for(int i=0;i<w->count;++i)if(mx_filter_row(w->rows[i],w->filter))w->rows[count++]=w->rows[i];
    w->count=count;
}
static void mx_columns(void)
{
    MatrixWindow *w=&matrix_window;w->column_count=0;
    for(int i=0;i<TS_MATRIX_DESTINATIONS;++i) {
        int d=w->layout?w->layout->column[i]:i;
        if(ts_matrix_destination_active(&w->controls,d))w->columns[w->column_count++]=d;
    }
    w->column=SDL_clamp(w->column,0,SDL_max(0,w->column_count-MATRIX_COLS));
    if(!ts_matrix_destination_active(&w->controls,w->destination))w->destination=TS_MATRIX_MAIN;
}
static int mx_column(int position)
{
    return position>=0 && position<matrix_window.column_count?matrix_window.columns[position]:-1;
}
static int mx_column_position(int destination)
{
    for(int i=0;i<matrix_window.column_count;++i)if(mx_column(i)==destination)return i;
    return 0;
}
static void mx_reveal_column(int destination)
{
    MatrixWindow *w=&matrix_window;int at=mx_column_position(destination);
    if(at<w->column)w->column=at;
    if(at>=w->column+MATRIX_COLS)w->column=at-MATRIX_COLS+1;
}
static void mx_focus_rect(int x,int y,int width,int height)
{
    mx_rect(x,y,width,2,MX_YELLOW);mx_rect(x,y+height-2,width,2,MX_YELLOW);
    mx_rect(x,y,2,height,MX_YELLOW);mx_rect(x+width-2,y,2,height,MX_YELLOW);
}
static int mx_layout_key(const SDL_Event *e)
{
    if(e->type!=SDL_KEYDOWN && e->type!=SDL_KEYUP)return 0;
    SDL_Keycode key=e->key.keysym.sym;
    return (e->key.keysym.mod&KMOD_SHIFT) && !(e->key.keysym.mod&(KMOD_CTRL|KMOD_ALT|KMOD_GUI)) &&
        (key==SDLK_UP || key==SDLK_DOWN || key==SDLK_LEFT || key==SDLK_RIGHT);
}
static void mx_layout_move(SDL_Keycode key,TsSamplePages *pages,TsInstrument *bank)
{
    MatrixWindow *w=&matrix_window;TsMatrixLayout *l=w->layout;
    if(!l || !mx_editable())return;
    if(w->layout_focus==1 && (key==SDLK_UP || key==SDLK_DOWN)) {
        mx_rows(pages,bank);
        int at=w->selected,to=at+(key==SDLK_UP?-1:1);
        if(at<0 || to<0 || to>=w->count)return;
        MatrixRow row=w->rows[at],other=w->rows[to];int a=-1,b=-1;
        for(int i=0;i<w->full_layout.row_count;++i) {
            if(w->full_layout.row_kind[i]==row.kind && w->full_layout.row_id[i]==mx_row_id(row))a=i;
            if(w->full_layout.row_kind[i]==other.kind && w->full_layout.row_id[i]==mx_row_id(other))b=i;
        }
        if(a<0 || b<0)return;
        /* Swap adjacent visible rows. Hidden rows retain their relative order. */
        memset(l->row_id,0,sizeof(l->row_id));memset(l->row_kind,0,sizeof(l->row_kind));
        l->row_count=w->full_layout.row_count;
        memcpy(l->row_id,w->full_layout.row_id,l->row_count*sizeof(l->row_id[0]));
        memcpy(l->row_kind,w->full_layout.row_kind,l->row_count*sizeof(l->row_kind[0]));
        l->row_id[a]=mx_row_id(other);l->row_kind[a]=(unsigned char)other.kind;
        l->row_id[b]=mx_row_id(row);l->row_kind[b]=(unsigned char)row.kind;
        mx_rows(pages,bank);
        if(w->selected<w->scroll)w->scroll=w->selected;
        if(w->selected>=w->scroll+MATRIX_VISIBLE)w->scroll=w->selected-MATRIX_VISIBLE+1;
        snprintf(w->status,sizeof(w->status),"MOVED %.32s %s. ROUTING UNCHANGED. CTRL+Z TO UNDO.",row.name,key==SDLK_UP?"UP":"DOWN");
    } else if(w->layout_focus==2 && (key==SDLK_LEFT || key==SDLK_RIGHT)) {
        int at=mx_column_position(w->destination),to=at+(key==SDLK_LEFT?-1:1);
        if(to<0 || to>=w->column_count)return;
        int a=-1,b=-1;
        for(int i=0;i<TS_MATRIX_DESTINATIONS;++i){if(l->column[i]==mx_column(at))a=i;if(l->column[i]==mx_column(to))b=i;}
        if(a<0 || b<0)return;
        unsigned char other=l->column[b];l->column[b]=l->column[a];l->column[a]=other;
        mx_columns();
        mx_reveal_column(w->destination);
        snprintf(w->status,sizeof(w->status),"MOVED DESTINATION %s. ROUTING UNCHANGED. CTRL+Z TO UNDO.",key==SDLK_LEFT?"LEFT":"RIGHT");
    } else snprintf(w->status,sizeof(w->status),"CLICK A ROW LABEL: SHIFT+UP/DOWN. CLICK A COLUMN HEADER: SHIFT+LEFT/RIGHT.");
    w->dirty=1;
}
