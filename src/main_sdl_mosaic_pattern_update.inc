/* Source revisions are checked at workspace boundaries, never in the callback.
   Cached definitions and PCM are shared by all cards choosing that version. */
static int mosaic_pattern_matches(const TsPatternCard *a,const TsPatternCard *b)
{
    return a && b && a->lineage && a->lineage==b->lineage && a->id==b->id;
}
static int mosaic_pattern_index(const MosaicController *c,TsPatternId id)
{
    for(int i=0;i<c->pattern_count;++i)if(c->pattern_ids[i]==id)return i;
    return -1;
}
static TsMosaicSource *mosaic_pattern_get(TsUiState *ui,const TsInstrument *instrument,MosaicController *c,int i)
{
    if(i<0 || i>=c->pattern_count)return NULL;
    if(!c->pattern_sources[i]){
        c->pattern_sources[i]=ts_pattern_source(ui->mosaic,c->pages,instrument,c->pattern_ids[i],ui->status,sizeof(ui->status));
        if(c->pattern_sources[i])++c->pattern_sources[i]->pins;
    }
    return c->pattern_sources[i];
}
static void mosaic_pattern_cache(TsUiState *ui,const TsInstrument *instrument,MosaicController *c)
{
    if(!c->pages || c->pattern_ready)return;
    if(ts_tapehead_frame() && !ts_tapehead_export(&c->pages->tracker,ui->status,sizeof(ui->status)))return;
    for(int i=0;i<c->pattern_count;++i)if(c->pattern_sources[i] && c->pattern_sources[i]->pins)--c->pattern_sources[i]->pins;
    memset(c->pattern_sources,0,sizeof(c->pattern_sources));c->pattern_count=0;
    TsSisterTracker *t=&c->pages->tracker;
    ts_pattern_link_legacy(ui->mosaic,t);
    for(int i=0;i<t->pattern_count;++i)if(t->patterns[i])c->pattern_ids[c->pattern_count++]=t->patterns[i]->id;
    c->pattern_ready=c->pattern_scan=1;
    if(!ui->mosaic->pattern_engine)ui->mosaic->pattern_engine=ts_pattern_engine_create();
    for(int i=0;i<TS_MOSAIC_EVENTS;++i){const TsMosaicEvent *e=&ui->mosaic->events[i];
        if(e->id && e->source->pattern && e->source->pattern->lineage==t->source_id)
            mosaic_pattern_get(ui,instrument,c,mosaic_pattern_index(c,e->source->pattern->id));
    }
    for(TsMosaicSource *s=ui->mosaic->sources;s;s=s->next)if(s->pattern){
        int i=mosaic_pattern_index(c,s->pattern->id);TsMosaicSource *latest=i<0?NULL:c->pattern_sources[i];
        s->pattern->older=latest && mosaic_pattern_matches(s->pattern,latest->pattern) && s->hash!=latest->hash;
        if(latest && mosaic_pattern_matches(s->pattern,latest->pattern) && s->pattern->acknowledged==latest->hash)c->pattern_notice[i]=latest->hash;
    }
}
static void mosaic_pattern_notices(TsUiState *ui,const TsInstrument *instrument,MosaicController *c,int manual)
{
    if(!c->pages || ui->mosaic_pattern_choice || ui_dialog_open(ui) || (!c->pattern_scan && !manual))return;
    mosaic_pattern_cache(ui,instrument,c);
    const TsMosaicEvent *owner=ts_mosaic_find(ui->mosaic,c->pattern_epoch==ui->mosaic->epoch?c->pattern_owner:0);
    const TsMosaicEvent *primary=ts_mosaic_find(ui->mosaic,ui->mosaic_selected);
    int requested=-1;
    if(manual){
        int n=ui->mosaic_source_selected;
        if(manual==1 && primary && primary->source->pattern){
            if(primary->source->pattern->lineage!=c->pages->tracker.source_id || mosaic_pattern_index(c,primary->source->pattern->id)<0){
                snprintf(ui->status,sizeof(ui->status),"SOURCE UNAVAILABLE / SAVED SNAPSHOT STILL PLAYS");return;
            }
            requested=mosaic_pattern_index(c,primary->source->pattern->id);
        }else if(ui->mosaic_pattern_tab && n>=0 && n<ui->mosaic_source_count && ui->mosaic_sources[n].source && ui->mosaic_sources[n].source->pattern)
            requested=mosaic_pattern_index(c,ui->mosaic_sources[n].source->pattern->id);
        owner=NULL;
    }
    for(int pass=0;pass<c->pattern_count;++pass){
        int i=pass;
        /* Editing a card gives that pattern first refusal, then other edits. */
        int preferred=owner && owner->source->pattern?mosaic_pattern_index(c,owner->source->pattern->id):-1;
        if(preferred>=0){if(pass==0)i=preferred;else if(pass<=preferred)i=pass-1;}
        if(manual && requested>=0 && i!=requested)continue;
        TsMosaicSource *source=c->pattern_sources[i];if(!source)continue;
        if(!manual && c->pattern_notice[i]==source->hash)continue;
        int count=0,selected=0;
        for(int j=0;j<TS_MOSAIC_EVENTS;++j){TsMosaicEvent *e=&ui->mosaic->events[j];
            if(e->id && mosaic_pattern_matches(e->source->pattern,source->pattern) && e->source->hash!=source->hash){
                ++count;if(mosaic_chosen(ui,j))++selected;
            }
        }
        if(!count)continue;
        ui->mosaic_pattern_owner=owner && mosaic_pattern_matches(owner->source->pattern,source->pattern) && owner->source->hash!=source->hash?owner->id:0;
        ui->mosaic_pattern_selected=ui->mosaic_pattern_owner?1:selected;
        ui->mosaic_pattern_all=count;ui->mosaic_pattern_number=source->pattern->id;
        ui->mosaic_pattern_version=source->pattern->version;
        c->pattern_prompt=i;ui->mosaic_pattern_choice=1;mosaic_drag_end(ui,c);
        return;
    }
    c->pattern_scan=0;c->pattern_owner=0;
    if(manual)snprintf(ui->status,sizeof(ui->status),"NO LINKED PATTERN CARDS NEED UPDATING");
}
static void mosaic_pattern_resolve(SDL_AudioDeviceID device,TsUiState *ui,MosaicController *c,int choice)
{
    if(!ui->mosaic_pattern_choice || c->ui_epoch!=ui->mosaic->epoch)return;
    int at=c->pattern_prompt;TsMosaicSource *source=c->pattern_sources[at];if(!source)return;
    if(choice==1 && !ui->mosaic_pattern_selected){snprintf(ui->status,sizeof(ui->status),"SELECT MATCHING CARDS OR CHOOSE UPDATE ALL");return;}
    int changed=0;
    if(device)SDL_LockAudioDevice(device);
    if(choice)for(int i=0;i<TS_MOSAIC_EVENTS;++i){TsMosaicEvent *e=&ui->mosaic->events[i];
        int selected=ui->mosaic_pattern_owner?e->id==ui->mosaic_pattern_owner:mosaic_chosen(ui,i);
        if(!e->id || (choice==1 && !selected) || !mosaic_pattern_matches(e->source->pattern,source->pattern) || e->source->hash==source->hash)continue;
        if(!changed)ts_mosaic_checkpoint(ui->mosaic);
        ts_mosaic_update_pattern(ui->mosaic,e,source);++changed;
    }
    if(device)SDL_UnlockAudioDevice(device);
    for(TsMosaicSource *s=ui->mosaic->sources;s;s=s->next)if(mosaic_pattern_matches(s->pattern,source->pattern))s->pattern->acknowledged=source->hash;
    c->pattern_notice[at]=source->hash;ui->mosaic_pattern_choice=0;
    snprintf(ui->status,sizeof(ui->status),choice?"UPDATED %d CARD(S) / PLAYING CARDS SWITCH AT NEXT REPEAT":"EXISTING CARDS KEPT / U: REVIEW SOURCE UPDATES",changed);
}
