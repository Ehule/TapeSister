/* UI-owned bounded history. Stable IDs keep deleted/reordered cards and tiles
   from redirecting old edits onto unrelated objects. No DSP histories are copied. */
typedef struct { MatrixRow row;TsSourceRoute route; } MatrixSavedRoute;
struct MatrixSnapshot {
    TsMatrixControls controls;TsRouterControls router;TsSpatialControls spatial;
    TsCompressorControls compressor;
    TsInsertControls insert;uint16_t sources;int count;
    TsMatrixLayout layout;
    MatrixSavedRoute routes[];
};
static void mx_refresh_tracker(TsSamplePages *pages,TsInstrument *bank)
{ts_tapehead_refresh_routes(pages,bank);}
static MatrixSnapshot *mx_snapshot(SDL_AudioDeviceID device,AudioState *audio,TsSamplePages *pages,TsInstrument *bank)
{
    size_t count=TS_TRACKER_LANES+TS_MOSAIC_EVENTS;
    for(size_t p=0;p<ts_sample_pages_count(pages);++p) {
        const TsInstrument *page=ts_sample_pages_page(pages,bank,p);if(!page)continue;
        for(int i=0;i<TS_BANK_SLOT_COUNT;++i)count+=page->bank[i].occupied!=0;
    }
    MatrixSnapshot *s=calloc(1,sizeof(*s)+count*sizeof(s->routes[0]));if(!s)return NULL;
    if(device)SDL_LockAudioDevice(device);
    s->controls=audio->sister.matrix.controls;s->router=audio->sister.router.controls;s->spatial=audio->sister.spatial.controls;
    s->compressor=audio->sister.compressor.controls;
    s->layout=audio->sister.matrix_layout;
    s->insert=audio->sister.insert.controls;s->sources=audio->sister.source_switches;
    if(device)SDL_UnlockAudioDevice(device);
    for(int i=0;i<TS_TRACKER_LANES;++i)s->routes[s->count++]=(MatrixSavedRoute){.row={.kind=0,.index=i},.route=pages->tracker.lanes[i].output_route};
    for(size_t p=0;p<ts_sample_pages_count(pages);++p) {
        const TsInstrument *page=ts_sample_pages_page(pages,bank,p);if(!page)continue;
        for(int i=0;i<TS_BANK_SLOT_COUNT;++i)if(page->bank[i].occupied)
            s->routes[s->count++]=(MatrixSavedRoute){.row={.kind=1,.tile=page->bank[i].tile_id},.route=page->bank[i].output_route};
    }
    TsMosaic *m=matrix_window.mosaic;
    if(m)for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(m->events[i].id)
        s->routes[s->count++]=(MatrixSavedRoute){.row={.kind=3,.tile=m->events[i].id},.route=m->events[i].output_route};
    return s;
}
static int mx_equal(const MatrixSnapshot *a,const MatrixSnapshot *b)
{
    return a && b && a->count==b->count && !memcmp(a,b,sizeof(*a)+a->count*sizeof(a->routes[0]));
}
static int mx_apply(const MatrixSnapshot *s,SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,TsSamplePages *pages,TsInstrument *bank)
{
    if(!s)return 0;MatrixWindow *w=&matrix_window;
    TsMatrixControls controls=s->controls;
    if(controls.next_effect_id<audio->sister.matrix.controls.next_effect_id)controls.next_effect_id=audio->sister.matrix.controls.next_effect_id;
    TsMatrixEffectPlan plan;if(!mx_stage_effects(device,audio,&controls,&plan))return 0;
    audio->sister.matrix_layout=s->layout;
    if(device)SDL_LockAudioDevice(device);
    for(int i=0;i<s->count;++i) {
        MatrixSavedRoute v=s->routes[i];
        if(v.row.kind==0)pages->tracker.lanes[v.row.index].output_route=v.route;
        else if(v.row.kind==1) {
            TsBankSlot *tile=mx_tile(pages,bank,v.row.tile);
            if(tile){tile->output_route=v.route;source_route_update_voices(audio,v.row.tile,v.route);}
        } else {TsMosaicEvent *e=ts_mosaic_find(w->mosaic,v.row.tile);if(e)e->output_route=v.route;}
    }
    ts_matrix_effects_commit(&audio->sister.matrix_effects,&controls,(unsigned)audio->output_rate,&plan);
    ts_matrix_set_live(&audio->sister.matrix,&controls,(unsigned)audio->output_rate);
    ts_compressor_set(&audio->sister.compressor,&s->compressor);
    w->compressor=ui->config.compressor=s->compressor;
    ts_sister_runtime_set_router(&audio->sister,&s->router);
    ts_spatial_recall(&audio->sister.spatial,&s->spatial);
    ts_sister_runtime_set_insert(&audio->sister,&s->insert);
    ts_sister_runtime_set_sources(&audio->sister,s->sources);
    w->controls=ui->config.matrix=controls;w->legacy=ui->config.router=s->router;
    w->array=ui->config.spatial=s->spatial;ui->config.insert=s->insert;
    if(device)SDL_UnlockAudioDevice(device);
    mx_refresh_tracker(pages,bank);mx_rows(pages,bank);mx_reveal_column(w->destination);w->dirty=1;return 1;
}
static void matrix_history_clear(void)
{
    MatrixWindow *w=&matrix_window;
    for(int i=0;i<w->history_count;++i)free(w->history[i]);
    free(w->pending);free(w->original);free(w->edited);
    w->pending=w->original=w->edited=NULL;w->history_count=w->history_cursor=0;
    w->original_active=w->selection_count=w->drag=w->layout_focus=0;w->effect_open=0;w->effect_menu=0;
}
static void mx_push(MatrixSnapshot *s)
{
    MatrixWindow *w=&matrix_window;
    for(int i=w->history_cursor;i<w->history_count;++i)free(w->history[i]);
    w->history_count=w->history_cursor;
    if(w->history_count==32){free(w->history[0]);memmove(w->history,w->history+1,31*sizeof(*w->history));--w->history_count;}
    w->history[w->history_count++]=s;w->history_cursor=w->history_count;
}
static void mx_original(SDL_AudioDeviceID device,AudioState *audio,TsSamplePages *pages,TsInstrument *bank)
{
    if(!matrix_window.original)matrix_window.original=mx_snapshot(device,audio,pages,bank);
}
static void mx_undo(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,TsSamplePages *pages,TsInstrument *bank,int redo)
{
    MatrixWindow *w=&matrix_window;w->history_skip=1;w->drag=0;
    if(w->original_active){snprintf(w->status,sizeof(w->status),"SELECT EDITED TO USE UNDO/REDO.");return;}
    int at=redo?w->history_cursor:w->history_cursor-1;if(at<0 || at>=w->history_count)return;
    MatrixSnapshot *now=mx_snapshot(device,audio,pages,bank);if(!now)return;
    MatrixSnapshot *saved=w->history[at];
    if(saved->count==now->count && !memcmp(saved,now,offsetof(MatrixSnapshot,layout)) &&
       !memcmp(saved->routes,now->routes,now->count*sizeof(now->routes[0]))) {
        /* Undoing a view edit must not reapply controls or touch DSP history. */
        audio->sister.matrix_layout=saved->layout;mx_rows(pages,bank);mx_reveal_column(w->destination);
        if(w->layout_focus==1 && w->selected>=0)w->scroll=SDL_clamp(w->selected-MATRIX_VISIBLE+1,0,SDL_max(0,w->count-MATRIX_VISIBLE));
        w->dirty=1;
    } else if(!mx_apply(saved,device,audio,ui,pages,bank)){free(now);return;}
    free(saved);w->history[at]=now;
    w->history_cursor+=redo?1:-1;snprintf(w->status,sizeof(w->status),"%s MATRIX EDIT (%d / %d)",redo?"REDID":"UNDID",w->history_cursor,w->history_count);
}
static void mx_ab(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,TsSamplePages *pages,TsInstrument *bank)
{
    MatrixWindow *w=&matrix_window;w->history_skip=1;w->drag=0;
    if(!w->original){snprintf(w->status,sizeof(w->status),"ORIGINAL IS CAPTURED WHEN YOU FIRST EDIT ROUTING IN THIS PROJECT.");return;}
    if(w->original_active) {if(!mx_apply(w->edited,device,audio,ui,pages,bank))return;w->original_active=0;}
    else {free(w->edited);w->edited=mx_snapshot(device,audio,pages,bank);if(!w->edited)return;if(!mx_apply(w->original,device,audio,ui,pages,bank))return;w->original_active=1;}
    snprintf(w->status,sizeof(w->status),"AUDITIONING %s - A/B PRESERVES THE EDITED ROUTING AND UNDO HISTORY.",w->original_active?"ORIGINAL":"EDITED");
}
static void mx_restore(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,TsSamplePages *pages,TsInstrument *bank)
{
    MatrixWindow *w=&matrix_window;w->history_skip=1;
    if(!w->original)return;
    const SDL_MessageBoxButtonData buttons[]={{SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT,0,"Cancel"},{SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT,1,"Restore original"}};
    SDL_MessageBoxData dialog={SDL_MESSAGEBOX_WARNING,w->window,"Restore original routing?",
        "Replace edited routing with the original? You can undo this restore. Audio and arrangement edits are kept.",2,buttons,NULL};
    int choice=0;if(SDL_ShowMessageBox(&dialog,&choice)<0 || choice!=1)return;
    if(w->original_active){if(!mx_apply(w->edited,device,audio,ui,pages,bank))return;w->original_active=0;}
    MatrixSnapshot *now=mx_snapshot(device,audio,pages,bank);if(!now)return;
    if(!mx_apply(w->original,device,audio,ui,pages,bank)){free(now);return;}mx_push(now);
    free(w->edited);w->edited=NULL;snprintf(w->status,sizeof(w->status),"ORIGINAL ROUTING RESTORED - CTRL+Z TO UNDO.");
}
