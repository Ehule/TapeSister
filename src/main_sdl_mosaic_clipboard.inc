/* A project clipboard owns immutable audio/score generations. Gallery switches,
   source edits and deleting the original canvas cannot invalidate a copied group. */
static TsMosaicSource *mosaic_clipboard_source(TsMosaic *to,const TsMosaicSource *from,const TsMosaic *reuse,char *error,size_t size)
{
    /* Repeated pastes reuse identical destination audio. The clipboard itself
       remains independent of every canvas and keeps the copied generation. */
    for(TsMosaicSource *s=reuse?reuse->sources:NULL;s;s=s->next) {
        if(s->pattern || from->pattern || s->hash!=from->hash)continue;
        if(from->video_path[0]) {
            if(s->video_pool==from->video_pool && !strcmp(s->video_path,from->video_path))return s;
        } else if(!s->video_path[0] && mosaic_same_audio(&s->sample,&from->sample) &&
            ((!s->eq_dry.data && !from->eq_dry.data) ||
             (mosaic_same_audio(&s->eq_dry,&from->eq_dry) && !memcmp(&s->eq,&from->eq,sizeof(s->eq)))))return s;
    }
    if(from->video_path[0])return ts_mosaic_video_source(to,from->video_path,from->video_pool);
    if(!from->pattern)return ts_mosaic_source_eq(to,&from->sample,&from->eq_dry,&from->eq,error,size);
    TsMosaicSource *s=calloc(1,sizeof(*s));if(!s)return NULL;
    TsPatternCard *p=malloc(sizeof(*p));if(!p){free(s);return NULL;}
    *p=*from->pattern;memset(&p->score,0,sizeof(p->score));p->assets=NULL;p->prepared=NULL;
    if(!ts_sister_tracker_clone(&p->score,&from->pattern->score,error,size))goto failed;
    if(p->asset_count) {
        p->assets=calloc((size_t)p->asset_count,sizeof(*p->assets));if(!p->assets)goto failed;
        for(int i=0;i<p->asset_count;++i) {
            p->assets[i]=from->pattern->assets[i];
            p->assets[i].audio=mosaic_clipboard_source(to,p->assets[i].audio,reuse,error,size);
            if(!p->assets[i].audio)goto failed;
        }
    }
    if(!ts_sample_clone(&s->sample,&from->sample,error,size) || !ts_pattern_prepare(p))goto failed;
    s->pattern=p;s->hash=from->hash;s->eq=from->eq;
    memcpy(s->peaks,from->peaks,sizeof(s->peaks));memcpy(s->rms,from->rms,sizeof(s->rms));
    s->next=to->sources;to->sources=s;return s;
failed:
    ts_sample_free(&s->sample);ts_pattern_free(p);free(s);return NULL;
}
static int mosaic_clipboard_clone_sources(TsMosaic *m,const TsMosaic *reuse,char *error,size_t size)
{
    const TsMosaicSource *original[TS_MOSAIC_EVENTS]={0};
    for(int i=0;i<TS_MOSAIC_EVENTS && m->events[i].id;++i) {
        TsMosaicEvent *e=&m->events[i];original[i]=e->source;
        int previous=0;for(;previous<i && original[previous]!=original[i];++previous);
        e->source=previous<i?m->events[previous].source:mosaic_clipboard_source(m,original[i],reuse,error,size);
        if(!e->source)return 0;
    }
    return 1;
}
static void mosaic_clipboard_copy(SDL_AudioDeviceID device,TsUiState *ui,MosaicController *c)
{
    TsMosaic *copy=ts_mosaic_create();char error[160]="NOT ENOUGH MEMORY FOR COPIED CARDS";
    if(!copy){snprintf(ui->status,sizeof(ui->status),"%s",error);return;}
    int count=0;
    if(device)SDL_LockAudioDevice(device);
    for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(mosaic_chosen(ui,i))copy->events[count++]=ui->mosaic->events[i];
    if(device)SDL_UnlockAudioDevice(device);
    if(!count){ts_mosaic_free(copy);snprintf(ui->status,sizeof(ui->status),"SELECT CARDS TO COPY");return;}
    /* Source allocation/preparation stays outside audio exclusion. Only the UI
       can replace/free the immutable originals during this synchronous copy. */
    if(!mosaic_clipboard_clone_sources(copy,NULL,error,sizeof(error))) {
        ts_mosaic_free(copy);snprintf(ui->status,sizeof(ui->status),"COPY FAILED: %.140s",error);return;
    }
    ts_mosaic_free(c->clipboard);c->clipboard=copy;
    snprintf(ui->status,sizeof(ui->status),"COPIED %d CARD%s / SWITCH CANVAS, SET PLAYHEAD, CTRL+V TO PASTE",count,count==1?"":"S");
}
static void mosaic_clipboard_paste(SDL_AudioDeviceID device,TsUiState *ui,MosaicController *c)
{
    if(!c->clipboard){snprintf(ui->status,sizeof(ui->status),"COPY CARDS FIRST / CTRL+C");return;}
    TsMosaic *m=ui->mosaic;int count=0,free_slots=0,patterns=0;
    double first=86400,left=1000000,last=0,right=0;
    for(int i=0;i<TS_MOSAIC_EVENTS;++i) {
        free_slots+=!m->events[i].id;
        const TsMosaicEvent *e=&c->clipboard->events[i];if(!e->id)continue;
        ++count;patterns+=e->source->pattern!=NULL;
        first=fmin(first,e->start);last=fmax(last,e->start);left=fmin(left,e->x);right=fmax(right,e->x);
    }
    if(free_slots<count || !m->next_id || m->next_id>=UINT64_MAX-(uint64_t)count) {
        snprintf(ui->status,sizeof(ui->status),"NOTHING PASTED / NEED %d FREE CARD SLOTS",count);return;
    }
    double dt=fmax(0,ui->mosaic_time)-first,dx=fmax(0,ui->mosaic_xscroll)-left;
    if(last+dt>86400 || right+dx>1000000) {
        snprintf(ui->status,sizeof(ui->status),"NOTHING PASTED / GROUP EXCEEDS CANVAS BOUNDS");return;
    }
    TsMosaic *stage=ts_mosaic_create();char error[160]="NOT ENOUGH MEMORY FOR PASTED CARDS";
    if(!stage){snprintf(ui->status,sizeof(ui->status),"%s",error);return;}
    memcpy(stage->events,c->clipboard->events,sizeof(stage->events));
    if(!mosaic_clipboard_clone_sources(stage,m,error,sizeof(error))) {
        ts_mosaic_free(stage);snprintf(ui->status,sizeof(ui->status),"PASTE FAILED: %.140s",error);return;
    }
    /* Auto-space translates the whole group together; it never changes timing
       or separates cards within the copied passage. */
    for(int pass=0;m->auto_space && pass<TS_MOSAIC_EVENTS;++pass) {
        double shift=0;
        for(int i=0;i<count;++i)for(int j=0;j<TS_MOSAIC_EVENTS;++j) {
            const TsMosaicEvent *a=&stage->events[i],*b=&m->events[j];if(!b->id)continue;
            if(a->start+dt>=b->start+b->duration-1e-9 || b->start>=a->start+dt+a->duration-1e-9)continue;
            if(a->x+dx<b->x+b->width+TS_MOSAIC_GUTTER && b->x<a->x+dx+a->width+TS_MOSAIC_GUTTER)
                shift=fmax(shift,b->x+b->width+TS_MOSAIC_GUTTER-a->x-dx);
        }
        if(!shift)break;dx+=shift;
    }
    if(right+dx>1000000){ts_mosaic_free(stage);snprintf(ui->status,sizeof(ui->status),"NOTHING PASTED / NO ROOM FOR GROUP");return;}
    if(device)SDL_LockAudioDevice(device);
    if(!ts_mosaic_reserve_patterns(m,patterns)) {
        if(device)SDL_UnlockAudioDevice(device);
        ts_mosaic_free(stage);snprintf(ui->status,sizeof(ui->status),"NOTHING PASTED / PATTERN VOICE RESERVATION FAILED");return;
    }
    ts_mosaic_checkpoint(m);
    TsMosaicSource *tail=stage->sources;
    if(tail){while(tail->next)tail=tail->next;tail->next=m->sources;m->sources=stage->sources;stage->sources=NULL;}
    mosaic_select_only(ui,0);
    for(int i=0,j=0;i<TS_MOSAIC_EVENTS && j<count;++i)if(!m->events[i].id) {
        TsMosaicEvent *e=&m->events[i];*e=stage->events[j++];e->id=m->next_id++;e->revision=1;
        e->start+=dt;e->x+=dx;mosaic_select_add(ui,e->id);
    }
    ++m->revision;m->route_discontinuity=1;
    if(device)SDL_UnlockAudioDevice(device);
    ts_mosaic_free(stage);ui->project_modified=1;
    snprintf(ui->status,sizeof(ui->status),"PASTED %d CARD%s AT %.3FS / G TO MOVE GROUP / CTRL+Z TO UNDO",count,count==1?"":"S",first+dt);
}
