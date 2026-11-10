static void mosaic_gallery_snapshot(TsUiState *,const TsMosaicGallery *);
/* Metadata browsing is cheap. Actual audio decoding is deferred until one
   canvas is chosen and the busy frame has been presented. No disk I/O or
   destruction takes place under the output exclusion. */
static void mosaic_canvas_queue_import(TsUiState *ui,PendingFileOperation *pending)
{
    if(!ui->mosaic_canvas_import_pending || pending->active)return;
    *pending=(PendingFileOperation){.mode=TS_BROWSER_IMPORT_CANVAS,.active=1};
    snprintf(pending->path,sizeof(pending->path),"%s",ui->mosaic_canvas_import_path);
    ui->file_busy=1;ui->file_busy_phase=2;
    snprintf(ui->file_busy_label,sizeof(ui->file_busy_label),"IMPORTING CANVAS");
}
static int mosaic_canvas_file_operation(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,TsInstrument *bank,MosaicController *c,
    TsSamplePages *pages,const PendingFileOperation *pending,char *error,size_t n)
{
    int ok=0;TsMosaicGallery *g=pages->gallery;
    if(!g){snprintf(error,n,"Open Mosaic Gallery first");goto done;}
    if(pending->mode==TS_BROWSER_EXPORT_CANVAS) {
        int selected=ui->mosaic_gallery_selected;
        if(selected<0 || selected>=g->count){snprintf(error,n,"Canvas is no longer available");goto done;}
        TsSisterProjectState *state=malloc(sizeof(*state));
        if(!state){snprintf(error,n,"Not enough memory for canvas export");goto done;}
        ts_sister_project_state_init(state,48000);state->matrix=audio->sister.matrix.controls;state->matrix_layout=audio->sister.matrix_layout;
        ok=ts_sample_pages_export_canvas_with_state(&g->canvas[selected],state,pending->path,error,n);free(state);
        if(ok)snprintf(ui->status,sizeof(ui->status),"EXPORTED CANVAS / AUDIO, PATTERNS AND EFFECT ROUTING");
    } else if(!ui->mosaic_canvas_import_pending) {
        TsMosaicGalleryCatalog catalog;
        ok=ts_sample_pages_canvas_catalog(pending->path,&catalog,error,n);
        if(ok) {
            snprintf(ui->mosaic_canvas_import_path,sizeof(ui->mosaic_canvas_import_path),"%s",pending->path);
            ui->mosaic_canvas_import_count=catalog.count;ui->mosaic_canvas_import_selected=catalog.active;
            for(int i=0;i<catalog.count;++i)memcpy(ui->mosaic_canvas_import_names[i],catalog.canvas[i].name,48);
            ui->mosaic_canvas_import_replace=ui->mosaic_canvas_import_confirm=0;
            int target=ui->mosaic_gallery_selected;
            ui->mosaic_canvas_import_target=target>=0 && target<g->count?g->canvas[target].id:0;
            snprintf(ui->status,sizeof(ui->status),"CHOOSE SOURCE / ADD TO END OR REPLACE SELECTED / LINKED FX INCLUDED");
        }
    } else {
        TsMosaicCanvas imported={0};TsMosaic *retired=NULL;
        TsCanvasRoutingImport *routing=NULL;TsMatrixEffectPlan plan={0};int staged=0;
        int target=-1,replacing=ui->mosaic_canvas_import_replace;
        for(int i=0;i<g->count;++i)if(g->canvas[i].id==ui->mosaic_canvas_import_target)target=i;
        if(replacing && target<0){snprintf(error,n,"Selected destination canvas is no longer available");goto done;}
        if(!replacing && g->count>=TS_GALLERY_LIMIT){snprintf(error,n,"Gallery full: select Replace selected");goto done;}
        if(matrix_window.original_active){snprintf(error,n,"Select EDITED in FX Canvas before importing");goto done;}
        routing=malloc(sizeof(*routing));
        if(!routing){snprintf(error,n,"Not enough memory for canvas import");goto done;}
        routing->matrix=audio->sister.matrix.controls;routing->layout=audio->sister.matrix_layout;
        if(replacing) {
            unsigned exclusive=ts_sample_pages_canvas_exclusive_effects(pages,bank,g->canvas[target].scene,&routing->matrix);
            for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)if(exclusive&(1u<<i)) {
                uint32_t id=routing->matrix.effect[i].id;int count=0;
                for(int p=0;p<routing->layout.canvas_count;++p) {
                    TsFxCanvasPosition position=routing->layout.canvas[p];
                    if(position.kind!=4 || position.id!=id)routing->layout.canvas[count++]=position;
                }
                routing->layout.canvas_count=count;ts_matrix_effect_remove(&routing->matrix,i);
            }
        }
        if(!ts_sample_pages_load_canvas_routed(pending->path,ui->mosaic_canvas_import_selected,
            &routing->matrix,&routing->layout,g->next_id,
            replacing?ui->mosaic_canvas_import_target:0,&imported,routing,error,n))goto import_done;
        if(!mx_stage_effects(device,audio,&routing->matrix,&plan)){snprintf(error,n,"Could not prepare linked effects: %.100s",matrix_window.status);goto import_done;}
        staged=1;
        int active=replacing && target==g->active;
        if(active && !c){snprintf(error,n,"Active canvas editor is unavailable");goto import_done;}
        if(active && !runtime_note_preserve_workspace(device,audio,ui)){snprintf(error,n,"Could not preserve held voices");goto import_done;}
        if(device)SDL_LockAudioDevice(device);
        ok=replacing?ts_gallery_replace_prepared(g,target,imported.scene,&retired,error,n):ts_gallery_append_prepared(g,imported.scene,error,n);
        if(ok) {
            if(!replacing)target=g->count-1;
            TsMosaicCanvas *to=&g->canvas[target];uint64_t id=to->id;
            *to=imported;to->id=id;ui->mosaic_gallery_selected=target;
            ts_matrix_effects_commit(&audio->sister.matrix_effects,&routing->matrix,(unsigned)audio->output_rate,&plan);
            ts_matrix_set_live(&audio->sister.matrix,&routing->matrix,(unsigned)audio->output_rate);
            ui->config.matrix=audio->sister.matrix.controls;
            if(active) {
                audio->mosaic=pages->mosaic=imported.scene;
                audio->mosaic_keyboard_sample=NULL;
            }
            mosaic_gallery_snapshot(ui,g);
        }
        if(device)SDL_UnlockAudioDevice(device);
        if(ok) {
            if(active) {
                /* ui->mosaic still owns the old pins until cleanup finishes. */
                mosaic_drag_end(ui,c);mosaic_picks_clear(ui,c);
                if(c->clipboard_epoch==ui->mosaic->epoch && c->clipboard.source && c->clipboard.source->pins)--c->clipboard.source->pins;
                for(int i=0;i<256;++i)if(c->pattern_sources[i] && c->pattern_sources[i]->pins)--c->pattern_sources[i]->pins;
                runtime_mosaic_sources_clear_selection(audio);
                mosaic_controller_free(c);c->pages=pages;ui->mosaic=imported.scene;
                ui->mosaic_live_key_valid=ui->mosaic_source_keys=ui->mosaic_source_key_count=0;
                mosaic_select_only(ui,0);ui->mosaic_source_selected=-1;ui->mosaic_live_status[0]=0;
                ui->mosaic_scroll=imported.scroll;ui->mosaic_xscroll=imported.xscroll;
                ui->mosaic_scale=imported.scale;ui->mosaic_hscale=imported.hscale;
                ui->mosaic_loaded_view_epoch=imported.scene->epoch;mosaic_sources_refresh(ui,bank,c);
            }
            audio->sister.matrix_layout=routing->layout;
            matrix_history_clear();matrix_window.controls=audio->sister.matrix.controls;
            matrix_window.gallery=g;matrix_window.mosaic=pages->mosaic;matrix_window.layout=&audio->sister.matrix_layout;
            mx_rows(pages,bank);matrix_window.dirty=1;
            ui->mosaic_canvas_import_count=0;
            snprintf(ui->status,sizeof(ui->status),"%s %.47s / AUDIO + %d LINKED FX",replacing?"REPLACED WITH":"IMPORTED",imported.name,routing->effects);
        }
import_done:
        if(staged)ts_matrix_effects_discard(&plan);
        if(!ok)ts_mosaic_free(imported.scene);
        ts_mosaic_free(retired);free(routing);
    }
done:
    ui->mosaic_canvas_import_pending=0;
    if(!ok)snprintf(ui->status,sizeof(ui->status),"CANVAS TRANSFER FAILED: %.130s",error);
    snprintf(ui->mosaic_gallery_status,sizeof(ui->mosaic_gallery_status),"%s",ui->status);
    /* A failed file choice stays in the browser. A failed chosen-page decode
       keeps its selection list so the user can cancel without changing audio. */
    if(ok || ui->browser.mode==TS_BROWSER_CLOSED)ui->mosaic_gallery_open=1;
    return ok;
}
