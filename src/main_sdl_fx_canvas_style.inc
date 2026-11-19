/* Cable appearance follows both endpoint identities, including sidechains. */
static TsFxCableStyle fx_wire_style_key(MatrixRow row,int destination)
{return (TsFxCableStyle){mx_row_id(row),row.canvas,row.kind,destination,fx_wire_target_id(destination),0};}
static int fx_layout_wire_style_at(const TsMatrixLayout *l,TsFxCableStyle key)
{
    for(int i=0;i<l->cable_count;++i) {
        const TsFxCableStyle *p=&l->cables[i];
        if(p->source==key.source && p->canvas==key.canvas && p->kind==key.kind && p->target==key.target &&
           (key.target?(p->destination>=TS_MATRIX_EFFECT_SC_BASE)==(key.destination>=TS_MATRIX_EFFECT_SC_BASE):p->destination==key.destination))return i;
    }
    return -1;
}
static int fx_wire_style_at(TsFxCableStyle key)
{return fx_layout_wire_style_at(matrix_window.layout,key);}
static void fx_wire_style_remove(TsMatrixLayout *l,int at)
{
    if(at<0)return;
    memmove(l->cables+at,l->cables+at+1,(size_t)(--l->cable_count-at)*sizeof(*l->cables));
    memset(l->cables+l->cable_count,0,sizeof(*l->cables));
}
static uint32_t fx_wire_color(MatrixRow row,int destination)
{int at=fx_wire_style_at(fx_wire_style_key(row,destination));return at<0?fx_source_color(row):matrix_window.layout->cables[at].color;}
static void fx_wire_set_color(FxCanvasWire wire,uint32_t color)
{
    TsMatrixLayout *l=matrix_window.layout;TsFxCableStyle key=fx_wire_style_key(wire.source,wire.destination);
    key.target=wire.target_id;int at=fx_wire_style_at(key);
    if(!color) {
        fx_wire_style_remove(l,at);
    } else {
        if(at<0)at=l->cable_count++;
        key.color=color;l->cables[at]=key;
    }
}
