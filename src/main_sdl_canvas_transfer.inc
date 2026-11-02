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
static int mosaic_canvas_file_operation(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,
    TsSamplePages *pages,const PendingFileOperation *pending,char *error,size_t n)
{
    int ok=0;TsMosaicGallery *g=pages->gallery;
    if(!g){snprintf(error,n,"Open Mosaic Gallery first");goto done;}
    if(pending->mode==TS_BROWSER_EXPORT_CANVAS) {
        int selected=ui->mosaic_gallery_selected;
        if(selected<0 || selected>=g->count){snprintf(error,n,"Canvas is no longer available");goto done;}
        ok=ts_sample_pages_export_canvas(&g->canvas[selected],pending->path,error,n);
        if(ok)snprintf(ui->status,sizeof(ui->status),"EXPORTED CANVAS / TSR BUNDLE / SHARED EFFECTS EXCLUDED");
    } else if(!ui->mosaic_canvas_import_pending) {
        TsMosaicGalleryCatalog catalog;
        ok=ts_sample_pages_canvas_catalog(pending->path,&catalog,error,n);
        if(ok) {
            snprintf(ui->mosaic_canvas_import_path,sizeof(ui->mosaic_canvas_import_path),"%s",pending->path);
            ui->mosaic_canvas_import_count=catalog.count;ui->mosaic_canvas_import_selected=catalog.active;
            for(int i=0;i<catalog.count;++i)memcpy(ui->mosaic_canvas_import_names[i],catalog.canvas[i].name,48);
            snprintf(ui->status,sizeof(ui->status),"CHOOSE A CANVAS / ADDS A PAGE / DESTINATION EFFECTS ARE SHARED");
        }
    } else {
        TsMosaicCanvas imported={0};
        if(g->count>=TS_GALLERY_LIMIT){snprintf(error,n,"Gallery limit: 16 canvases");goto done;}
        if(!ts_sample_pages_load_canvas(pending->path,ui->mosaic_canvas_import_selected,&imported,error,n))goto done;
        if(device)SDL_LockAudioDevice(device);
        ok=ts_gallery_append_prepared(g,imported.scene,error,n);
        if(ok) {
            TsMosaicCanvas *to=&g->canvas[g->count-1];uint64_t id=to->id;
            *to=imported;to->id=id;ui->mosaic_gallery_selected=g->count-1;
            mosaic_gallery_snapshot(ui,g);
        }
        if(device)SDL_UnlockAudioDevice(device);
        if(!ok)ts_mosaic_free(imported.scene);
        else {
            ui->mosaic_canvas_import_count=0;
            snprintf(ui->status,sizeof(ui->status),"IMPORTED %.47s / SELECT ENTER TO TURN THE PAGE",imported.name);
        }
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
