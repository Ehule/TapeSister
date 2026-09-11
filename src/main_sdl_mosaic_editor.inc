/* The main window is a view of one event document. Park the sample bank while
   editing; callback voices always read Mosaic's immutable sources. */
typedef struct {
    TsSamplePages *pages;
    TsInstrument *bank;
    TsInstrument *documents[TS_MOSAIC_EVENTS];
    uint64_t document_ids[TS_MOSAIC_EVENTS], document_epochs[TS_MOSAIC_EVENTS];
    uint64_t clipboard_epoch, ui_epoch, drag_epoch;
    const float *bank_data[TS_BANK_SLOT_COUNT];
    uint32_t bank_revision[TS_BANK_SLOT_COUNT], bank_rate[TS_BANK_SLOT_COUNT];
    uint8_t bank_channels[TS_BANK_SLOT_COUNT];
    int bank_page;
    size_t bank_frames[TS_BANK_SLOT_COUNT];
    uint64_t bank_hash[TS_BANK_SLOT_COUNT];
    TsMosaicEvent source_drag;
    uint64_t active, pending_id, pending_revision, pending_epoch;
    const float *seen_data;
    uint32_t seen_revision;
    size_t seen_frames;
    int audio_dirty, resume_exit;
    SDL_Event exit_event;
    int drag, drag_slot, drag_x, drag_y, drag_moved;
    double drag_start, drag_duration, drag_left, drag_width;
    TsMosaicEvent clipboard;
} MosaicController;

static int mosaic_same_audio(const TsSample *a,const TsSample *b)
{
    return a->data && b->data && a->frames==b->frames && a->sample_rate==b->sample_rate && a->channels==b->channels &&
        !memcmp(a->data,b->data,a->frames*a->channels*sizeof(float));
}
static int mosaic_chosen(const TsUiState *ui,int i)
{
    uint64_t id=ui->mosaic->events[i].id;
    return id && (id==ui->mosaic_selected || id==ui->mosaic_selection[i]);
}
static void mosaic_select_only(TsUiState *ui,uint64_t id)
{
    memset(ui->mosaic_selection,0,sizeof(ui->mosaic_selection));ui->mosaic_selected=id;
}
static void mosaic_drag_end(TsUiState *ui,MosaicController *c)
{
    if(c->drag_epoch==ui->mosaic->epoch && c->source_drag.source && c->source_drag.source->pins)
        --c->source_drag.source->pins;
    c->source_drag=(TsMosaicEvent){0};c->drag=0;c->drag_moved=0;
    ui->mosaic_ghost_count=ui->mosaic_box=0;
}
/* Used event versions lead the palette. Original bank audio is retained as a
   separate source when a destructive event edit forks it. No bank is mutated. */
static void mosaic_sources_refresh(TsUiState *ui,const TsInstrument *instrument,MosaicController *c)
{
    if(c->ui_epoch!=ui->mosaic->epoch) {
        mosaic_drag_end(ui,c);mosaic_select_only(ui,0);
        ui->mosaic_source_selected=-1;ui->mosaic_source_page=0;
        memset(c->bank_data,0,sizeof(c->bank_data));c->ui_epoch=ui->mosaic->epoch;
    }
    if(c->bank_page!=ui->sample_page){memset(c->bank_data,0,sizeof(c->bank_data));c->bank_page=ui->sample_page;}
    const TsSample *selected=ui->mosaic_source_selected>=0 && ui->mosaic_source_selected<ui->mosaic_source_count?
        ui->mosaic_sources[ui->mosaic_source_selected].sample:NULL;
    uint64_t selected_event=ui->mosaic_source_selected>=0 && ui->mosaic_source_selected<ui->mosaic_source_count?
        ui->mosaic_sources[ui->mosaic_source_selected].event:0;
    ui->mosaic_source_count=0;int selection=-1;
    for(int i=0;i<TS_MOSAIC_EVENTS;++i) {
        TsMosaicEvent *e=&ui->mosaic->events[i];if(!e->id)continue;
        int n=0;for(;n<ui->mosaic_source_count;++n)if(ui->mosaic_sources[n].source==e->source)break;
        if(n<ui->mosaic_source_count)continue;
        ui->mosaic_sources[n].sample=&e->source->sample;ui->mosaic_sources[n].source=e->source;
        ui->mosaic_sources[n].event=e->id;ui->mosaic_sources[n].bank_slot=-1;
        if(e->id==selected_event || &e->source->sample==selected)selection=n;
        ++ui->mosaic_source_count;
    }
    for(int i=0;i<TS_BANK_SLOT_COUNT;++i) {
        const TsSample *sample=i==instrument->selected_slot?&instrument->current:&instrument->bank[i].sample;
        if(!sample->data || sample->frames<2)continue;
        if(c->bank_data[i]!=sample->data || c->bank_revision[i]!=sample->visual_revision || c->bank_frames[i]!=sample->frames || c->bank_rate[i]!=sample->sample_rate || c->bank_channels[i]!=sample->channels) {
            c->bank_data[i]=sample->data;c->bank_revision[i]=sample->visual_revision;c->bank_frames[i]=sample->frames;
            c->bank_rate[i]=sample->sample_rate;c->bank_channels[i]=sample->channels;
            c->bank_hash[i]=ts_sample_hash(sample);
        }
        int duplicate=0;
        for(int n=0;n<ui->mosaic_source_count;++n)
            if(ui->mosaic_sources[n].source && ui->mosaic_sources[n].source->hash==c->bank_hash[i]){duplicate=1;break;}
        if(duplicate)continue;
        int n=ui->mosaic_source_count++;
        ui->mosaic_sources[n].sample=sample;ui->mosaic_sources[n].source=NULL;
        ui->mosaic_sources[n].event=0;ui->mosaic_sources[n].bank_slot=i;
        if(sample==selected)selection=n;
    }
    ui->mosaic_source_selected=selection;
    int pages=(ui->mosaic_source_count+15)/16;
    if(ui->mosaic_source_page>=pages)ui->mosaic_source_page=pages>0?pages-1:0;
}
static void mosaic_source_select_event(TsUiState *ui,const TsMosaicEvent *e)
{
    for(int n=0;n<ui->mosaic_source_count;++n)if(ui->mosaic_sources[n].source==e->source) {
        ui->mosaic_source_selected=n;ui->mosaic_source_page=n/16;return;
    }
}

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
    char error[160]={0};
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
    if(ui->mosaic_edit_choice)return 0;
    if(ui->canvas_gesture.active || ui->amplitude_gesture.active || ui->material_macro_gesture.active ||
       ui->stretch_gesture.active || ui->warp_gesture.active || ui->smear_gesture.active || ui->tear_gesture.active)return 1;
    /* Audio identity changes only on a real edit. Selection and tuning are
       lightweight properties; don't hash a long sample every UI frame. */
    int audio_changed=c->seen_data!=instrument->current.data || c->seen_revision!=instrument->current.visual_revision ||
        c->seen_frames!=instrument->current.frames;
    if(audio_changed) {
        c->audio_dirty=!mosaic_same_audio(&instrument->current,&e->source->sample);
        c->seen_data=instrument->current.data;c->seen_revision=instrument->current.visual_revision;
        c->seen_frames=instrument->current.frames;
    }
    /* A working document may contain many edits. Keep its audio and bounds
       together until the user chooses a destination on leaving the editor. */
    if(c->audio_dirty)return 1;
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
static int mosaic_leave(SDL_AudioDeviceID,AudioState *,TsUiState *,TsInstrument *,MosaicController *);
/* One destination decision for the complete editing session. Existing voices
   keep reading immutable audio; Escape resumes editing without losing work. */
static int mosaic_resolve_edit(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,
                               TsInstrument *instrument,MosaicController *c,int choice)
{
    if(choice==0 && ui->mosaic_edit_choice) {
        ui->mosaic_edit_choice=0;c->pending_id=0;
        snprintf(ui->status,sizeof(ui->status),"KEEP EDITING - ALL CHANGES RETAINED");return 1;
    }
    TsMosaicEvent *owner=ts_mosaic_find(ui->mosaic,c->pending_id);
    if(!ui->mosaic_edit_choice || !owner || c->pending_id!=c->active ||
       c->pending_epoch!=ui->mosaic->epoch || c->pending_revision!=owner->revision) {
        snprintf(ui->status,sizeof(ui->status),"EDIT OWNER CHANGED - RESULT KEPT IN THE EDITOR");return 0;
    }
    char error[160]={0};
    TsMosaicSource *source=ts_mosaic_source(ui->mosaic,&instrument->current,error,sizeof(error));
    if(!source){snprintf(ui->status,sizeof(ui->status),"EVENT AUDIO NOT UPDATED: %.120s",error);return 0;}
    ++source->pins;
    TsInstrument *original_doc=NULL;int new_index=-1;
    if(choice==1) {
        int free_slot=0;for(int i=0;i<TS_MOSAIC_EVENTS;++i)free_slot+=!ui->mosaic->events[i].id;
        if(!free_slot){--source->pins;snprintf(ui->status,sizeof(ui->status),"MOSAIC IS FULL - UPDATE TILE OR KEEP EDITING");return 0;}
        for(int i=0;i<TS_MOSAIC_EVENTS;++i)
            if(c->document_epochs[i]!=ui->mosaic->epoch || !ts_mosaic_find(ui->mosaic,c->document_ids[i])){new_index=i;break;}
        original_doc=new_index>=0?malloc(sizeof(*original_doc)):NULL;
        if(!original_doc){--source->pins;snprintf(ui->status,sizeof(ui->status),"NOT ENOUGH MEMORY FOR A NEW EVENT DOCUMENT");return 0;}
        ts_instrument_init(original_doc);
        if(!ts_instrument_import_sample(original_doc,&owner->source->sample,1,owner->first,owner->last,owner->mode,error,sizeof(error))) {
            ts_instrument_free(original_doc);free(original_doc);--source->pins;
            snprintf(ui->status,sizeof(ui->status),"COULD NOT KEEP ORIGINAL EVENT: %.110s",error);return 0;
        }
        original_doc->tuning=owner->tuning;
        original_doc->loop_crossfade_ms=(float)(1000.0*owner->crossfade/owner->source->sample.sample_rate);
        snprintf(original_doc->current.name,sizeof(original_doc->current.name),"%s",owner->name);
    }
    if(device)SDL_LockAudioDevice(device);ts_mosaic_checkpoint(ui->mosaic);
    TsMosaicEvent *target=choice==1?ts_mosaic_copy(ui->mosaic,owner->id,owner->start,owner->x+owner->width+TS_MOSAIC_GUTTER):owner;
    int muted=target?target->muted:0;
    if(target && choice==1)target->muted=1;
    if(device)SDL_UnlockAudioDevice(device);
    if(!target || !mosaic_publish(device,ui,instrument,target,1)) {
        if(original_doc){ts_instrument_free(original_doc);free(original_doc);}
        if(target && choice==1){if(device)SDL_LockAudioDevice(device);ts_mosaic_delete(ui->mosaic,target->id);if(device)SDL_UnlockAudioDevice(device);}
        --source->pins;return 0;
    }
    if(device)SDL_LockAudioDevice(device);target->muted=muted;if(device)SDL_UnlockAudioDevice(device);
    if(choice==1) {
        /* Keep the original ID's document for any outstanding CDP owner.
           The empty parked container follows the currently open variation. */
        int i=mosaic_document_index(c,c->active);
        if(c->documents[new_index]){ts_instrument_free(c->documents[new_index]);free(c->documents[new_index]);}
        c->documents[new_index]=c->documents[i];c->documents[i]=original_doc;
        c->document_ids[new_index]=target->id;c->document_epochs[new_index]=ui->mosaic->epoch;
        c->active=ui->mosaic_editing=target->id;mosaic_select_only(ui,target->id);
    }
    --source->pins;
    c->seen_data=instrument->current.data;c->seen_revision=instrument->current.visual_revision;c->seen_frames=instrument->current.frames;
    ui->mosaic_edit_choice=0;c->pending_id=0;c->audio_dirty=0;
    snprintf(ui->status,sizeof(ui->status),choice==1?"NEW EVENT CREATED BESIDE THE ORIGINAL":"EVENT UPDATED - OTHER EVENTS UNCHANGED");
    ts_mosaic_collect(ui->mosaic);return mosaic_leave(device,audio,ui,instrument,c);
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
    if(c->audio_dirty) {
        TsMosaicEvent *owner=ts_mosaic_find(ui->mosaic,c->active);if(!owner)return 0;
        ui->mosaic_edit_choice=1;c->pending_id=owner->id;c->pending_revision=owner->revision;c->pending_epoch=ui->mosaic->epoch;
        snprintf(ui->status,sizeof(ui->status),"RETURN TO MOSAIC - NEW TILE, UPDATE TILE, OR KEEP EDITING");return 0;
    }
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
    char error[160]={0};
    if(!c->bank){c->bank=malloc(sizeof(*c->bank));if(!c->bank)return 0;ts_instrument_init(c->bank);}
    if(!c->documents[i]){c->documents[i]=malloc(sizeof(TsInstrument));if(!c->documents[i])return 0;ts_instrument_init(c->documents[i]);}
    TsInstrument *doc=c->documents[i];
    if(c->document_ids[i]!=id || c->document_epochs[i]!=ui->mosaic->epoch || !mosaic_same_audio(&doc->current,&e->source->sample)) {
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
    c->active=ui->mosaic_editing=id;ui->mosaic_open=0;c->audio_dirty=0;
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
