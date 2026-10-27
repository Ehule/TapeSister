static void project_view_capture(TsProjectView *v,const TsUiState *ui)
{
    memset(v,0,sizeof(*v));v->present=1;
    v->workspace=ui->mosaic_open?TS_PROJECT_VIEW_MOSAIC:ui->tracker_open?TS_PROJECT_VIEW_TRACKER:TS_PROJECT_VIEW_CANVAS;
    v->keyboard=!!ui->show_keyboard;v->ingredients=!!ui->show_ingredients;v->recipes=!!ui->show_recipes;
    v->parent_first=ui->parent_view_first;v->parent_last=ui->parent_view_last;
    v->scroll=ui->mosaic_scroll;v->xscroll=ui->mosaic_xscroll;
    v->scale=ui->mosaic_scale>0?ui->mosaic_scale:24;v->hscale=ui->mosaic_hscale>0?ui->mosaic_hscale:1;
    v->follow=!!ui->mosaic_follow;
    v->pattern_tab=!!ui->mosaic_pattern_tab;v->source_page=ui->mosaic_source_page;
    v->tracker_scroll=ui->tracker_scroll;v->tracker_expanded=!!ui->tracker_expanded;v->tracker_field=ui->tracker_field;
    uint64_t orders[TS_MOSAIC_EVENTS];
    if(ui->mosaic)for(int i=0;i<TS_MOSAIC_EVENTS;++i) {
        uint64_t id=ui->mosaic->events[i].id;
        if(!id || (ui->mosaic_selection[i]!=id && ui->mosaic_selected!=id))continue;
        unsigned at=v->selection_count++;
        uint64_t order=ui->mosaic_selection_order[i]?ui->mosaic_selection_order[i]:UINT64_MAX;
        while(at && orders[at-1]>order){v->selection[at]=v->selection[at-1];orders[at]=orders[at-1];--at;}
        v->selection[at]=id;orders[at]=order;
        if(ui->mosaic_selected==id)v->selected=id;
    }
}
static void project_view_apply(TsUiState *ui,const TsInstrument *instrument,const TsProjectView *v)
{
    ui->mosaic_loaded_view_epoch=0;
    if(!v->present)return;
    ui->mosaic_open=v->workspace==TS_PROJECT_VIEW_MOSAIC;
    ui->tracker_open=v->workspace==TS_PROJECT_VIEW_TRACKER;
    ui->fm_open=ui->portal.open=0;
    ui->show_keyboard=v->keyboard;ui->show_ingredients=v->ingredients;ui->show_recipes=v->recipes;
    if(v->parent_first<v->parent_last && v->parent_last<=instrument->parent.frames) {
        ui->parent_view_first=v->parent_first;ui->parent_view_last=v->parent_last;
    }
    ui->mosaic_scroll=v->scroll;ui->mosaic_xscroll=v->xscroll;ui->mosaic_scale=v->scale;ui->mosaic_hscale=v->hscale;
    ui->mosaic_time=v->time;ui->mosaic_follow=v->follow;
    ui->mosaic_pattern_tab=v->pattern_tab;ui->mosaic_source_page=v->source_page;
    ui->tracker_scroll=v->tracker_scroll;ui->tracker_expanded=v->tracker_expanded;ui->tracker_field=v->tracker_field;
    memset(ui->mosaic_selection,0,sizeof(ui->mosaic_selection));
    memset(ui->mosaic_selection_order,0,sizeof(ui->mosaic_selection_order));
    ui->mosaic_selected=ui->mosaic_selection_serial=0;
    if(ui->mosaic) {
        for(unsigned i=0;i<v->selection_count;++i) {
            TsMosaicEvent *e=ts_mosaic_find(ui->mosaic,v->selection[i]);if(!e)continue;
            int slot=(int)(e-ui->mosaic->events);
            ui->mosaic_selection[slot]=e->id;ui->mosaic_selection_order[slot]=++ui->mosaic_selection_serial;
            if(v->selected==e->id)ui->mosaic_selected=e->id;
        }
        ui->mosaic_loaded_view_epoch=ui->mosaic->epoch;
    }
}
static void project_log_state(const char *event,const char *path,const TsUiState *ui)
{
    int cards=0,muted=0,solo=0;
    if(ui->mosaic)for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(ui->mosaic->events[i].id) {
        ++cards;muted+=!!ui->mosaic->events[i].muted;solo+=!!ui->mosaic->events[i].solo;
    }
    ts_session_log_event(event,"kind=TSR path=%s mosaic_hash=%016llx cards=%d muted=%d solo=%d workspace=%s scroll=%.17g xscroll=%.17g scale=%.17g hscale=%.17g",
        path,(unsigned long long)ts_mosaic_hash(ui->mosaic),cards,muted,solo,
        ui->mosaic_open?"Mosaic":ui->tracker_open?"TrackSister":"Canvas",
        ui->mosaic_scroll,ui->mosaic_xscroll,ui->mosaic_scale,ui->mosaic_hscale);
}
