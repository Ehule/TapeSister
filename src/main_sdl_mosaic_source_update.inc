/* Prepare replacement banks without touching live state. All matching slots
   use the same audio identity that Mosaic uses to share its immutable sources. */
typedef struct MosaicSharedUpdate {
    TsInstrument *bank, *replacement, *preserved_editor;
    struct MosaicSharedUpdate *next;
} MosaicSharedUpdate;

static void mosaic_shared_update_free(MosaicSharedUpdate *updates)
{
    while(updates) {
        MosaicSharedUpdate *next=updates->next;
        ts_instrument_free(updates->replacement);free(updates->replacement);
        if(updates->preserved_editor){ts_instrument_free(updates->preserved_editor);free(updates->preserved_editor);}
        free(updates);updates=next;
    }
}
static int mosaic_stage_shared_update(MosaicController *c,const TsInstrument *doc,
                                      const TsMosaicSource *source,MosaicSharedUpdate **updates,
                                      int *first_page,int *first_slot,char *error,size_t size)
{
    int banks=c->pages?(int)ts_sample_pages_count(c->pages):1;
    *updates=NULL;
    for(int p=0;p<banks;++p) {
        TsInstrument *bank=c->pages?ts_sample_pages_page_mut(c->pages,c->bank,(size_t)p):c->bank;
        if(!bank)continue;
        unsigned matches=0;
        for(int n=0;n<TS_BANK_SLOT_COUNT;++n) {
            const TsSample *sample=n==bank->selected_slot?&bank->current:&bank->bank[n].sample;
            if(!mosaic_same_audio(sample,&source->sample))continue;
            if(bank->bank[n].locked) {
                snprintf(error,size,"Bank %02d tile %02d is locked; unlock it before Update All",p+1,n+1);goto failed;
            }
            matches|=1u<<n;
        }
        if(!matches)continue;
        MosaicSharedUpdate *update=calloc(1,sizeof(*update));
        if(!update){snprintf(error,size,"Not enough memory to update the shared source");goto failed;}
        update->replacement=malloc(sizeof(*update->replacement));
        if(!update->replacement){free(update);snprintf(error,size,"Not enough memory to update the source bank");goto failed;}
        ts_instrument_init(update->replacement);update->bank=bank;
        update->next=*updates;*updates=update;
        if(!ts_instrument_clone(update->replacement,bank,error,size))goto failed;
        if(!ts_instrument_sync_selected(update->replacement,error,size))goto failed;
        int selected=bank->selected_slot;
        if(!(matches&(1u<<selected))) {
            /* Keep the unrelated live editor exactly, including its generator,
               view and undo state; loading a bank slot would reconstruct it. */
            update->preserved_editor=calloc(1,sizeof(TsInstrument));
            if(!update->preserved_editor){snprintf(error,size,"Not enough memory to preserve the selected editor");goto failed;}
            memcpy(update->preserved_editor,update->replacement,offsetof(TsInstrument,bank));
            ts_sample_init(&update->replacement->current);ts_sample_init(&update->replacement->parent);
        }
        for(int n=0;n<TS_BANK_SLOT_COUNT;++n)if(matches&(1u<<n)) {
            if(*first_page<0){*first_page=p;*first_slot=n;}
            TsInstrument *staged=update->replacement;
            /* Import replaces only this slot; the original bank is still parked. */
            staged->selected_slot=n;
            if(!ts_instrument_import_sample(staged,&doc->current,doc->has_loop,doc->loop_first,
                                           doc->loop_last,doc->loop_mode,error,size))goto failed;
            staged->tuning=staged->audible_tuning=doc->tuning;staged->loop_crossfade_ms=doc->loop_crossfade_ms;
            if(!ts_instrument_sync_selected(staged,error,size))goto failed;
        }
        if(update->preserved_editor) {
            ts_sample_free(&update->replacement->current);ts_sample_free(&update->replacement->parent);
            memcpy(update->replacement,update->preserved_editor,offsetof(TsInstrument,bank));
            ts_sample_init(&update->preserved_editor->current);ts_sample_init(&update->preserved_editor->parent);
        } else if(!ts_instrument_select_bank(update->replacement,selected,error,size))goto failed;
    }
    return 1;
failed:
    mosaic_shared_update_free(*updates);*updates=NULL;return 0;
}
static void mosaic_shared_update_publish(MosaicSharedUpdate *updates)
{
    /* Allocation-free exchange under the audio lock. Old bank storage is freed
       after the main editor has returned and stopped its temporary voices. */
    for(;updates;updates=updates->next) {
        unsigned char temporary[1024];
        for(size_t at=0;at<sizeof(TsInstrument);at+=sizeof(temporary)) {
            size_t size=sizeof(TsInstrument)-at;if(size>sizeof(temporary))size=sizeof(temporary);
            unsigned char *a=(unsigned char *)updates->bank+at,*b=(unsigned char *)updates->replacement+at;
            memcpy(temporary,a,size);memcpy(a,b,size);memcpy(b,temporary,size);
        }
    }
}
static void mosaic_replace_shared_events(TsMosaic *m,const TsMosaicSource *before,
                                        TsMosaicSource *after,uint64_t owner)
{
    for(int i=0;i<TS_MOSAIC_EVENTS;++i) {
        TsMosaicEvent *e=&m->events[i];if(!e->id || e->id==owner || e->source!=before)continue;
        /* Preserve each card's region proportionally when the source length
           changes. Notes, timing, loop mode and mix settings remain its own. */
        double ratio=(double)after->sample.frames/before->sample.frames;
        e->first=(size_t)llround(e->first*ratio);e->last=(size_t)llround(e->last*ratio);
        if(e->last>after->sample.frames)e->last=after->sample.frames;
        if(e->first>=e->last){e->first=0;e->last=after->sample.frames;}
        e->crossfade=(size_t)llround(e->crossfade*ratio);
        if(e->crossfade>(e->last-e->first)/2)e->crossfade=(e->last-e->first)/2;
        if(!strncmp(e->name,before->sample.name,sizeof(e->name)))snprintf(e->name,sizeof(e->name),"%.63s",after->sample.name);
        e->source=after;++e->revision;
    }
    ++m->revision;
}
