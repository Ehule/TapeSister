/* During a save the controller admits only cancellation, Close and window
   events. Captures/edit workers must finish first. PCM/history/source ownership
   stays with the pinned document until join; only metadata is copied. Audio
   playback may mutate its own runtime, never these immutable sample buffers. */
typedef struct {
    SDL_Thread *thread;
    SDL_mutex *mutex;
    SDL_atomic_t done,cancel;
    TsSamplePages pages;
    TsInstrument *active,*record;
    TsSisterProjectState state;
    TsSisterRuntime *sister;
    TsKeyboardSequence sequence;
    TsSaveControl control;
    TsMosaic *scene[TS_GALLERY_LIMIT];
    int scene_count,ok,close_requested;
    uint64_t saved_hash,saved_ui_hash;
    Uint32 last_progress,last_journal;
    char path[TS_BROWSER_PATH_MAX],error[1024],message[160],asset[160],journal_path[2048];
    char logged_stage[64],logged_asset[2048];
    FILE *journal;
    struct {const void *data;size_t frames;unsigned rate,channels;uint64_t hash;} hashes[256];
    unsigned hash_count,hash_next;
} ProjectSaveWorker;
static ProjectSaveWorker *project_save_worker;

static int project_save_cancelled(void *data)
{return SDL_AtomicGet(&((ProjectSaveWorker *)data)->cancel)!=0;}
static int project_save_cached_hash(void *data,const void *pcm,size_t frames,
    unsigned rate,unsigned channels,uint64_t *hash,int insert)
{
    ProjectSaveWorker *w=data;
    for(unsigned i=0;i<w->hash_count;++i)if(w->hashes[i].data==pcm && w->hashes[i].frames==frames &&
        w->hashes[i].rate==rate && w->hashes[i].channels==channels) {
        if(insert)w->hashes[i].hash=*hash;else *hash=w->hashes[i].hash;return 1;
    }
    if(!insert)return 0;
    unsigned i=w->hash_count<256?w->hash_count++:w->hash_next++%256;
    w->hashes[i].data=pcm;w->hashes[i].frames=frames;w->hashes[i].rate=rate;
    w->hashes[i].channels=channels;w->hashes[i].hash=*hash;return 1;
}
static void project_save_progress(void *data,const char *stage,const char *asset,
    uint64_t bytes,size_t completed,size_t total)
{
    ProjectSaveWorker *w=data;Uint32 now=SDL_GetTicks();
    int changed=strcmp(stage,w->logged_stage)!=0 || strcmp(asset,w->logged_asset)!=0;
    if(!changed && (Uint32)(now-w->last_progress)<100u)return;
    w->last_progress=now;
    SDL_LockMutex(w->mutex);
    snprintf(w->message,sizeof(w->message),"%s / %zu OF %zu BANK ASSETS / %.1F MIB WRITTEN",
        stage,completed,total,(double)bytes/1048576.0);
    snprintf(w->asset,sizeof(w->asset),"%.150s",path_basename(asset));
    SDL_UnlockMutex(w->mutex);
    if(w->journal && (changed || (Uint32)(now-w->last_journal)>=1000u)) {
        fprintf(w->journal,"%u\t%s\t%zu/%zu\t%llu\t%s\n",now,stage,completed,total,(unsigned long long)bytes,asset);
        fflush(w->journal);
        w->last_journal=now;
    }
    if(changed)ts_session_log_event("project_save_stage","stage=%s assets=%zu/%zu bytes=%llu asset=%s",stage,completed,total,(unsigned long long)bytes,asset);
    snprintf(w->logged_stage,sizeof(w->logged_stage),"%s",stage);
    snprintf(w->logged_asset,sizeof(w->logged_asset),"%s",asset);
}
static void project_save_snapshot_scene(TsMosaic *out,const TsMosaic *in)
{
    /* Save-only projection: no audio-owned voices/schedules or 32 Undo canvases. */
    memcpy(out->events,in->events,sizeof(out->events));
    memcpy(out->markers,in->markers,sizeof(out->markers));
    memcpy(out->views,in->views,sizeof(out->views));
    memcpy(out->volume,in->volume,sizeof(out->volume));
    out->repeat=in->repeat;out->gain=in->gain;out->speed=in->speed;
    out->auto_space=in->auto_space;out->loop_range=in->loop_range;
    out->live_capture=in->live_capture;out->live_envelope=in->live_envelope;
}
static void project_save_worker_free(ProjectSaveWorker *w)
{
    if(!w)return;
    if(w->pages.pages)for(size_t i=0;i<w->pages.page_count;++i)free(w->pages.pages[i]);
    free(w->pages.pages);free(w->active);free(w->record);free(w->sister);
    free(w->pages.gallery);
    for(int i=0;i<w->scene_count;++i)free(w->scene[i]);
    if(w->journal)fclose(w->journal);
    if(w->mutex)SDL_DestroyMutex(w->mutex);
    free(w);
}
static FILE *project_save_open_journal(const char *path,char *destination,size_t size)
{
    char *health=SDL_GetPrefPath("TapeSister","Health");FILE *journal=NULL;
    destination[0]=0;
    if(health) {
        snprintf(destination,size,"%sproject-save-%llu.log",health,(unsigned long long)SDL_GetPerformanceCounter());
        SDL_free(health);journal=fopen(destination,"wbx");
        if(journal){fprintf(journal,"START\t%s\n",path);fflush(journal);}
        else destination[0]=0;
    }
    return journal;
}
static int project_save_main(void *data)
{
    ProjectSaveWorker *w=data;
    /* Compute the saved-state identity before commit, with progress and bounded
       cancellation. Cache only this pinned generation; the native writer can
       reuse these quantized hashes while still verifying every output byte. */
    TsSaveControl *previous=ts_save_io_bind(&w->control);
    if(ts_save_io_stage("Fingerprinting project state",w->path)) {
        w->saved_hash=paged_project_state_hash(&w->pages,w->active,w->record,w->sister,&w->sequence);
        w->saved_ui_hash=paged_project_fingerprint(&w->pages,w->active,w->record,w->sister,&w->sequence,1);
    }
    ts_save_io_bind(previous);
    if(w->control.was_cancelled) {
        snprintf(w->error,sizeof(w->error),"Save cancelled before serialization; previous project kept");
        project_save_progress(w,"Cancelled",w->path,0,0,0);
    }else {
        ts_session_log_project_save_begin();
        w->ok=ts_sample_pages_save_project_controlled(&w->pages,w->active,w->record,&w->state,
            w->path,&w->control,w->error,sizeof(w->error));
        ts_session_log_project_save_end();
    }
    ts_session_log_event(w->ok?"project_save_success":w->control.was_cancelled?"project_save_cancelled":"project_save_failure",
        "path=%s error=%s recovery=%s journal=%s",w->path,w->error,w->control.recovery,w->journal_path);
    if(w->journal){fprintf(w->journal,"RESULT\t%s\t%s\nRECOVERY\t%s\n",w->ok?"SUCCESS":w->control.was_cancelled?"CANCELLED":"FAILED",w->error,w->control.recovery);fflush(w->journal);}
    SDL_AtomicSet(&w->done,1);return 0;
}
static int project_save_start(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,
    TsSamplePages *pages,const TsInstrument *active,const TsInstrument *record,
    const TsSisterProjectState *state,const char *path,char *error,size_t size)
{
    char journal_path[2048];FILE *journal=project_save_open_journal(path,journal_path,sizeof(journal_path));
    ProjectSaveWorker *w=calloc(1,sizeof(*w));if(!w)goto failed;
    w->journal=journal;snprintf(w->journal_path,sizeof(w->journal_path),"%s",journal_path);
    w->mutex=SDL_CreateMutex();w->pages=*pages;w->pages.pages=NULL;w->pages.gallery=NULL;
    w->pages.pages=calloc(pages->page_count,sizeof(*pages->pages));
    w->active=active?malloc(sizeof(*active)):NULL;w->record=record?malloc(sizeof(*record)):NULL;
    w->sister=malloc(sizeof(*w->sister));
    if(!w->mutex || !w->pages.pages || (active && !w->active) || (record && !w->record) || !w->sister)goto failed;
    /* Commit destination pages before the brief audio-excluded metadata copy. */
    memset(w->sister,0,sizeof(*w->sister));
    for(size_t i=0;i<pages->page_count;++i)if(pages->pages[i]) {
        w->pages.pages[i]=malloc(sizeof(TsInstrument));if(!w->pages.pages[i])goto failed;
        *w->pages.pages[i]=*pages->pages[i];
    }
    if(active)*w->active=*active;if(record)*w->record=*record;
    if(pages->gallery) {
        w->pages.gallery=malloc(sizeof(*pages->gallery));if(!w->pages.gallery)goto failed;
        *w->pages.gallery=*pages->gallery;w->scene_count=pages->gallery->count;
    }else if(pages->mosaic)w->scene_count=1;
    for(int i=0;i<w->scene_count;++i) {
        w->scene[i]=calloc(1,sizeof(TsMosaic));if(!w->scene[i])goto failed;
        project_save_snapshot_scene(w->scene[i],pages->gallery?pages->gallery->canvas[i].scene:pages->mosaic);
        if(w->pages.gallery)w->pages.gallery->canvas[i].scene=w->scene[i];
    }
    w->pages.mosaic=w->scene_count?w->scene[pages->gallery?pages->gallery->active:0]:NULL;
    if(w->pages.gallery)w->pages.gallery->root=w->pages.mosaic;
    if(device)SDL_LockAudioDevice(device);
    *w->sister=audio->sister;w->sequence=audio->keyboard_sequence;
    if(device)SDL_UnlockAudioDevice(device);
    w->state=*state;
    snprintf(w->path,sizeof(w->path),"%s",path);
    w->control.userdata=w;w->control.cancelled=project_save_cancelled;w->control.progress=project_save_progress;
    w->control.sample_hash=project_save_cached_hash;
    w->thread=SDL_CreateThread(project_save_main,"TapeSister Save",w);if(!w->thread)goto failed;
    project_save_worker=w;
    ui->file_busy=1;
    snprintf(ui->file_busy_label,sizeof(ui->file_busy_label),"SAVING");
    snprintf(ui->file_busy_detail,sizeof(ui->file_busy_detail),"PREPARING PORTABLE PROJECT");
    snprintf(ui->file_busy_asset,sizeof(ui->file_busy_asset),"%.150s",path_basename(path));
    return 1;
failed:
    if(journal){fprintf(journal,"RESULT\tFAILED\tCould not allocate metadata snapshot / start save worker\n");fflush(journal);if(!w)fclose(journal);}
    ts_session_log_event("project_save_failure","path=%s stage=snapshot error=allocation/thread startup journal=%s",path,journal_path);
    project_save_worker_free(w);
    snprintf(error,size,"Could not allocate metadata snapshot / start save worker");return 0;
}
static void project_save_cancel(int close_requested)
{
    ProjectSaveWorker *w=project_save_worker;if(!w)return;
    if(close_requested)w->close_requested=1;
    SDL_AtomicSet(&w->cancel,1);
    ts_session_log_event("project_save_shutdown",close_requested?"Close requested; cancelling before commit":"Cancel requested");
}
static int project_save_event(const SDL_Event *event,SDL_Window *window,
    SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui)
{
    if(!project_save_worker)return 0;
    /* Hardware lifecycle remains live, and releasing a held note must never
       become a stuck voice just because a document save owns editing. */
    if(event->type==SDL_AUDIODEVICEADDED || event->type==SDL_AUDIODEVICEREMOVED)return 0;
    if(event->type==SDL_KEYUP && note_for_key(event->key.keysym.sym)>=0)
        release_note(device,audio,ui,note_for_key(event->key.keysym.sym));
    if(event->type==SDL_MOUSEBUTTONUP && ui->mouse_note>=0) {
        release_note(device,audio,ui,ui->mouse_note);ui->mouse_note=-1;
    }
    int close=event->type==SDL_QUIT || (event->type==SDL_WINDOWEVENT && event->window.event==SDL_WINDOWEVENT_CLOSE && event->window.windowID==SDL_GetWindowID(window));
    int cancel=event->type==SDL_KEYDOWN && event->key.keysym.sym==SDLK_ESCAPE;
    if(event->type==SDL_MOUSEBUTTONDOWN && event->button.button==SDL_BUTTON_LEFT && event->button.windowID==SDL_GetWindowID(window)) {
        float x,y;SDL_Renderer *renderer=SDL_GetRenderer(window);
        if(renderer){SDL_RenderWindowToLogical(renderer,event->button.x,event->button.y,&x,&y);cancel=x>=44 && x<596 && y>=240 && y<270;}
    }
    if(close || cancel) {
        project_save_cancel(close);
        snprintf(ui->file_busy_detail,sizeof(ui->file_busy_detail),"CANCELLING / FINISHING SAFE COMMIT");
    }
    if(event->type==SDL_DROPFILE)SDL_free(event->drop.file);
    return 1;
}
