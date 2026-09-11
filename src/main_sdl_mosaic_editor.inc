/* The main window is a view of one event document. Park the sample bank while
   editing; callback voices always read Mosaic's immutable sources. */
typedef struct {
    TsSamplePages *pages;
    TsInstrument *bank;
    TsInstrument *documents[TS_MOSAIC_EVENTS];
    uint64_t document_ids[TS_MOSAIC_EVENTS], document_epochs[TS_MOSAIC_EVENTS];
    uint64_t clipboard_epoch;
    uint64_t active;
    const float *seen_data;
    uint32_t seen_revision;
    size_t seen_frames;
    int drag, drag_slot, drag_x, drag_y;
    double drag_start, drag_duration, drag_left, drag_width;
    TsMosaicEvent clipboard;
} MosaicController;

static int mosaic_document_index(MosaicController *c,uint64_t id)
{
    for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(c->document_ids[i]==id)return i;
    return -1;
}
static TsInstrument *mosaic_document(MosaicController *c,TsUiState *ui,
                                     TsInstrument *current,uint64_t id)
{
    if(!c || !ts_mosaic_find(ui->mosaic,id))return NULL;
    if(c->active==id)return current;
    int i=mosaic_document_index(c,id);return i>=0?c->documents[i]:NULL;
}
static int mosaic_publish(SDL_AudioDeviceID device,TsUiState *ui,
                            TsInstrument *doc,TsMosaicEvent *e,int refresh_audio)
{
    if(!e || !doc->current.data)return 0;
    char error[160];
    TsMosaicSource *source=refresh_audio?ts_mosaic_source(ui->mosaic,&doc->current,error,sizeof(error)):e->source;
    if(!source){snprintf(ui->status,sizeof(ui->status),"EVENT AUDIO NOT UPDATED: %.120s",error);return 0;}
    size_t first=doc->has_loop?doc->loop_first:doc->has_selection?doc->selection_first:0;
    size_t last=doc->has_loop?doc->loop_last:doc->has_selection?doc->selection_last:doc->current.frames;
    if(last>source->sample.frames)last=source->sample.frames;
    if(first>=last){first=0;last=source->sample.frames;}
    size_t crossfade=doc->has_loop?(size_t)llround(doc->loop_crossfade_ms*doc->current.sample_rate/1000.0):0;
    if(crossfade>(last-first)/2)crossfade=(last-first)/2;
    int changed=e->source!=source || e->first!=first || e->last!=last || e->crossfade!=crossfade ||
        strncmp(e->name,doc->current.name,63) || e->mode!=doc->loop_mode || e->tuning.root_note!=doc->tuning.root_note ||
        e->tuning.fine_tune_cents!=doc->tuning.fine_tune_cents;
    if(changed) {
        if(device)SDL_LockAudioDevice(device);
        snprintf(e->name,sizeof(e->name),"%.63s",doc->current.name);
        e->source=source;e->first=first;e->last=last;e->crossfade=crossfade;
        e->mode=doc->loop_mode;e->tuning=doc->tuning;++e->revision;++ui->mosaic->revision;
        if(device)SDL_UnlockAudioDevice(device);
        ts_mosaic_collect(ui->mosaic);
    }
    return 1;
}
static int mosaic_commit(SDL_AudioDeviceID device,TsUiState *ui,
                           TsInstrument *instrument,MosaicController *c)
{
    TsMosaicEvent *e=ts_mosaic_find(ui->mosaic,c->active);if(!e)return 1;
    if(ui->canvas_gesture.active || ui->amplitude_gesture.active || ui->material_macro_gesture.active ||
       ui->stretch_gesture.active || ui->warp_gesture.active || ui->smear_gesture.active || ui->tear_gesture.active)return 1;
    /* Audio identity changes only on a real edit. Selection and tuning are
       lightweight properties; don't hash a long sample every UI frame. */
    int audio_changed=c->seen_data!=instrument->current.data || c->seen_revision!=instrument->current.visual_revision ||
        c->seen_frames!=instrument->current.frames;
    size_t first=instrument->has_loop?instrument->loop_first:instrument->has_selection?instrument->selection_first:0;
    size_t last=instrument->has_loop?instrument->loop_last:instrument->has_selection?instrument->selection_last:instrument->current.frames;
    if(audio_changed || strncmp(e->name,instrument->current.name,63) || e->first!=first || e->last!=last || e->mode!=instrument->loop_mode ||
       e->crossfade!=(instrument->has_loop?(size_t)llround(instrument->loop_crossfade_ms*instrument->current.sample_rate/1000.0):0) ||
       memcmp(&e->tuning,&instrument->tuning,sizeof(e->tuning))) {
        if(mosaic_publish(device,ui,instrument,e,audio_changed)) {
            c->seen_data=instrument->current.data;c->seen_revision=instrument->current.visual_revision;
            c->seen_frames=instrument->current.frames;
        } else return 0;
    }
    return 1;
}
static int mosaic_leave(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,
                           TsInstrument *instrument,MosaicController *c)
{
    if(!c->active)return 1;
    /* Never carry a gesture whose snapshot belongs to this event into the
       restored sample bank, including a window-close during a held drag. */
    if(ui->canvas_gesture.active)end_canvas_gesture(NULL,device,audio,ui,instrument,1);
    if(ui->amplitude_gesture.active)end_amplitude_draw(device,audio,ui,instrument,1);
    if(ui->material_macro_gesture.active)end_material_macro_gesture(device,audio,ui,instrument,1);
    if(ui->stretch_gesture.active)end_stretch_gesture(device,audio,ui,instrument,1);
    if(ui->warp_gesture.active)end_warp_gesture(device,audio,ui,instrument,1);
    if(ui->smear_gesture.active)end_smear_gesture(device,audio,ui,instrument,1);
    if(ui->tear_gesture.active)end_tear_gesture(device,audio,ui,instrument,1);
    if(!mosaic_commit(device,ui,instrument,c))return 0;
    int i=mosaic_document_index(c,c->active);
    stop_all_force(device,audio,ui);
    if(device)SDL_LockAudioDevice(device);
    *c->documents[i]=*instrument;*instrument=*c->bank;ts_instrument_init(c->bank);
    if(c->pages)c->pages->mosaic_bank=NULL;
    c->active=ui->mosaic_editing=0;ui->mosaic_open=1;ui->bank_view_slot=-1;
    runtime_note_sync(audio,instrument,audio->output_rate);
    if(device)SDL_UnlockAudioDevice(device);
    ui->staged_notes=0;ui->audition_source=TS_AUDITION_CURRENT;return 1;
}
static int mosaic_enter(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,
                           TsInstrument *instrument,MosaicController *c,uint64_t id)
{
    TsMosaicEvent *e=ts_mosaic_find(ui->mosaic,id);if(!e)return 0;
    if(c->active && !mosaic_leave(device,audio,ui,instrument,c))return 0;
    int i=mosaic_document_index(c,id);
    if(i<0)for(i=0;i<TS_MOSAIC_EVENTS;++i)if(!ts_mosaic_find(ui->mosaic,c->document_ids[i]))break;
    if(i==TS_MOSAIC_EVENTS)return 0;
    char error[160];
    if(!c->bank){c->bank=malloc(sizeof(*c->bank));if(!c->bank)return 0;ts_instrument_init(c->bank);}
    if(!c->documents[i]){c->documents[i]=malloc(sizeof(TsInstrument));if(!c->documents[i])return 0;ts_instrument_init(c->documents[i]);}
    TsInstrument *doc=c->documents[i];
    if(c->document_ids[i]!=id || c->document_epochs[i]!=ui->mosaic->epoch || ts_sample_hash(&doc->current)!=e->source->hash) {
        ts_instrument_free(doc);ts_instrument_init(doc);
        if(!ts_instrument_import_sample(doc,&e->source->sample,1,e->first,e->last,e->mode,error,sizeof(error))) {
            snprintf(ui->status,sizeof(ui->status),"EVENT EDITOR: %.130s",error);return 0;
        }
    }
    snprintf(doc->current.name,sizeof(doc->current.name),"%s",e->name);
    doc->has_loop=1;doc->loop_first=e->first;doc->loop_last=e->last;
    doc->loop_mode=e->mode;doc->loop_crossfade_ms=(float)(1000.0*e->crossfade/e->source->sample.sample_rate);doc->tuning=e->tuning;
    c->document_ids[i]=id;c->document_epochs[i]=ui->mosaic->epoch;
    stop_all_force(device,audio,ui);
    if(device)SDL_LockAudioDevice(device);
    *c->bank=*instrument;*instrument=*doc;ts_instrument_init(doc);
    if(c->pages)c->pages->mosaic_bank=c->bank;
    c->active=ui->mosaic_editing=id;ui->mosaic_open=0;
    runtime_note_sync(audio,instrument,audio->output_rate);
    if(device)SDL_UnlockAudioDevice(device);
    c->seen_data=instrument->current.data;c->seen_revision=instrument->current.visual_revision;c->seen_frames=instrument->current.frames;
    ui->bank_view_slot=-1;ui->show_keyboard=1;ui->show_recipes=ui->show_ingredients=0;
    ui->audition_source=TS_AUDITION_CURRENT;
    snprintf(ui->status,sizeof(ui->status),"EVENT %llu - CLICK KEYS TO SET CHORD; MOSAIC RETURNS TO ARRANGEMENT",(unsigned long long)id);
    return 1;
}
static void mosaic_controller_free(MosaicController *c)
{
    for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(c->documents[i]){ts_instrument_free(c->documents[i]);free(c->documents[i]);}
    if(c->bank){ts_instrument_free(c->bank);free(c->bank);}memset(c,0,sizeof(*c));
}
