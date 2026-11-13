/* Exact control-thread snapshots. Meters are not graph inputs; project recall,
   external routing edits and undo are, even without a local mouse event. */
static struct {
    int valid,count;
    uint32_t revealed[TS_MATRIX_EFFECT_CAPACITY],effect_open;
    MatrixRow selection[MATRIX_CAPACITY+TS_MATRIX_DESTINATIONS];int selection_count;
    MatrixRow rows[MATRIX_CAPACITY];
    TsSourceRoute routes[MATRIX_CAPACITY];
    TsMatrixControls controls;
    TsRouterControls legacy;
    TsSpatialControls array;
    TsSpeakerSetup speakers;
    TsMatrixLayout layout;
    TsAudioChannelRange output_range;
    unsigned outputs;
    uint64_t builds,movement;
} fx_graph_cache;
static int fx_graph_changed(TsSamplePages *pages,TsInstrument *bank)
{
    MatrixWindow *w=&matrix_window;
    int changed=!fx_graph_cache.valid || fx_graph_cache.count!=w->count || fx_graph_cache.movement!=fx_canvas.movement;
    fx_graph_cache.movement=fx_canvas.movement;
    if(fx_canvas.movement) {
        changed|=memcmp(fx_graph_cache.revealed,fx_canvas.revealed,sizeof(fx_canvas.revealed))!=0;
        memcpy(fx_graph_cache.revealed,fx_canvas.revealed,sizeof(fx_canvas.revealed));
        changed|=fx_graph_cache.effect_open!=w->effect_open || fx_graph_cache.selection_count!=w->selection_count;
        changed|=memcmp(fx_graph_cache.selection,w->selection,w->selection_count*sizeof(w->selection[0]))!=0;
        memcpy(fx_graph_cache.selection,w->selection,w->selection_count*sizeof(w->selection[0]));
        fx_graph_cache.selection_count=w->selection_count;fx_graph_cache.effect_open=w->effect_open;
    }
#define FX_KEY(field) do { \
    changed|=memcmp(&fx_graph_cache.field,&w->field,sizeof(w->field))!=0; \
    memcpy(&fx_graph_cache.field,&w->field,sizeof(w->field)); \
} while(0)
    FX_KEY(controls);FX_KEY(legacy);FX_KEY(array);FX_KEY(speakers);
    FX_KEY(output_range);FX_KEY(outputs);
#undef FX_KEY
    /* Compare used view positions, not the unused capacity for sixteen canvases. */
    size_t layout_bytes=offsetof(TsMatrixLayout,canvas)+(size_t)w->layout->canvas_count*sizeof(w->layout->canvas[0]);
    changed|=memcmp(&fx_graph_cache.layout,w->layout,layout_bytes)!=0;
    memcpy(&fx_graph_cache.layout,w->layout,layout_bytes);
    changed|=memcmp(fx_graph_cache.rows,w->rows,w->count*sizeof(w->rows[0]))!=0;
    memcpy(fx_graph_cache.rows,w->rows,w->count*sizeof(w->rows[0]));
    for(int i=0;i<w->count;++i) {
        TsSourceRoute route=mx_route(w->rows[i],pages,bank);
        changed|=memcmp(&route,&fx_graph_cache.routes[i],sizeof(route))!=0;
        fx_graph_cache.routes[i]=route;
    }
    fx_graph_cache.count=w->count;fx_graph_cache.valid=1;
    return changed;
}
