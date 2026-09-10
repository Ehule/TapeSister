/* Application-side ownership: workers own immutable inputs/results. The UI
   owns source/history; callback pointers are detached under the audio lock
   before any of those samples are freed. No CDP work occurs in the callback. */
typedef struct {
    SDL_Thread *thread;
    SDL_atomic_t cancel, done;
    uint64_t generation;
    TsSample input;
    TsPortalRegion region;
    TsPortalRecipe recipe, chain;
    int chain_start, chain_cleanup_failed;
    size_t original_last;
    TsCdpRunResult stages[TS_PORTAL_CHAIN_STAGES];
    TsPortalRegion stage_regions[TS_PORTAL_CHAIN_STAGES];
    TsCdpRuntime runtime;
    TsCdpRunResult result;
    char error[160];
} PortalWorker;
typedef struct {
    TsSample source;
    TsCdpRunResult stages[TS_PORTAL_CHAIN_STAGES];
    TsPortalRegion stage_regions[TS_PORTAL_CHAIN_STAGES], cache_region;
    TsPortalRecipe cache_recipe;
    TsCdpRunResult history[TS_PORTAL_HISTORY];
    TsPortalRecipe recipes[TS_PORTAL_HISTORY];
    TsPortalRegion regions[TS_PORTAL_HISTORY];
    TsSamplePages *pages;
    ExternalInputState *external_input;
    PortalWorker *worker;
    uint64_t generation, source_hash;
    int source_slot, source_page, selection_scope, history_count;
    int quick_apply, library_writable;
    int auto_pending;
    Uint32 auto_due;
    Uint32 hover_due;
    char hover_help[160], hover_status[160];
    size_t source_first, source_last;
    char library_path[TS_CDP_PATH_MAX];
} PortalController;

#include "main_sdl_portal_help.inc"

/* The scalar editor is mirrored into its selected chain stage only at a
   transition or snapshot boundary. Workers never read mutable UI state. */
static TsPortalRecipe portal_working(TsPortalUi *p)
{
    if(!p->chain_active)return p->recipe;
    ts_portal_step_set(&p->chain.stages[p->chain_stage],&p->recipe);
    return p->chain;
}
static void portal_load_recipe(TsPortalUi *p,const TsPortalRecipe *r)
{
    p->chain_active=r->stage_count!=0;p->chain_stage=0;p->chain_add=0;p->chain_audition=-1;p->chain_cached=0;
    if(p->chain_active){p->chain=*r;ts_portal_step_get(&r->stages[0],&p->recipe);}
    else p->recipe=*r;
}
static int portal_has_macros(const TsPortalRecipe *r)
{
    if(!r->stage_count)return r->exposed!=0;
    for(unsigned i=0;i<r->stage_count;++i)if(r->stages[i].exposed)return 1;
    return 0;
}
static char *portal_recipe_name(TsPortalUi *p)
{return p->chain_active?p->chain.name:p->recipe.name;}
static void portal_cache_clear(PortalController *c,TsPortalUi *p)
{
    for(int i=0;i<TS_PORTAL_CHAIN_STAGES;++i)ts_cdp_run_result_free(&c->stages[i]);
    memset(&c->cache_recipe,0,sizeof(c->cache_recipe));p->chain_cached=0;p->chain_audition=-1;
}
static int portal_owns_sample(const PortalController *c,const TsSample *sample)
{
    if(sample==&c->source)return 1;
    for(int i=0;i<TS_PORTAL_CHAIN_STAGES;++i)if(sample==&c->stages[i].output)return 1;
    for(int i=0;i<TS_PORTAL_HISTORY;++i)if(sample==&c->history[i].output)return 1;
    return 0;
}
/* Called with the audio device locked. Detach even inactive voice pointers
   before moving/freeing history or source storage. */
static void portal_clear_notes(AudioState *audio,TsPortalUi *p,const PortalController *c)
{
    for(int i=0;i<TS_NOTE_BANK_VOICE_CAPACITY;++i) {
        TsNoteVoice *v=&audio->notes.voices[i];
        if(v->preview && portal_owns_sample(c,v->sample))memset(v,0,sizeof(*v));
    }
    p->note_count=0;
}
static void portal_note_range(TsNoteVoice *v,const TsPortalWave *w,const TsSample *s,int loop,int seek)
{
    v->range_first=w->has_selection?w->selection_first:0;
    v->range_last=w->has_selection?w->selection_last:s->frames;
    if(v->range_first>=v->range_last || v->range_last>s->frames) {v->active=0;return;}
    v->looping=loop;
    v->crossfade_frames=loop?s->sample_rate/200u:0;
    if(v->crossfade_frames>(v->range_last-v->range_first)/2u)
        v->crossfade_frames=(v->range_last-v->range_first)/2u;
    if(seek)v->position=(double)w->playhead;
    if(v->position<(double)v->range_first || v->position>=(double)v->range_last)
        v->position=(double)v->range_first;
}
static void portal_stop(SDL_AudioDeviceID device,AudioState *audio,TsPortalUi *p,
                        PortalController *c)
{
    if(device)SDL_LockAudioDevice(device);
    int owned=portal_owns_sample(c,audio->sample);
    portal_clear_notes(audio,p,c);
    if(owned) {audio->playing=0;audio->sample=NULL;runtime_note_clear(audio);}
    if(device)SDL_UnlockAudioDevice(device);
    p->playing=0;
}
static void portal_cancel(PortalController *c)
{++c->generation;if(c->worker)SDL_AtomicSet(&c->worker->cancel,1);}
static void portal_auto_schedule(const TsPortalUi *p,PortalController *c)
{
    c->auto_pending=p->open && p->auto_preview;
    c->auto_due=SDL_GetTicks()+500u;
}
static void portal_invalidate(SDL_AudioDeviceID device,AudioState *audio,
                               TsPortalUi *p,PortalController *c)
{
    portal_stop(device,audio,p,c);portal_cancel(c);
    c->quick_apply=0;
    p->full_action=0;
    p->valid=0;p->result=NULL;p->history_selected=-1;
    p->chain_audition=-1;
    if(p->chain_active)p->chain_cached&=(1u<<p->chain_stage)-1u;
    ts_portal_wave_reset(&p->waves[1],NULL);
    portal_auto_schedule(p,c);
}
static void portal_clear_history(PortalController *c,TsPortalUi *p)
{
    portal_cache_clear(c,p);
    for(int i=0;i<TS_PORTAL_HISTORY;++i)ts_cdp_run_result_free(&c->history[i]);
    c->history_count=0;p->history_count=0;p->history_selected=-1;p->valid=0;p->result=NULL;
    ts_portal_wave_reset(&p->waves[1],NULL);
}
static int portal_source(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,
                         TsInstrument *instrument,PortalController *c)
{
    TsPortalUi *p=&ui->portal;
    p->load_selection=c->selection_scope;
    TsSample next;ts_sample_init(&next);
    if(c->worker) {portal_cancel(c);snprintf(p->message,sizeof(p->message),"CANCELLING; RELOAD AFTER JOB STOPS");return 0;}
    portal_invalidate(device,audio,p,c);portal_clear_history(c,p);
    ts_sample_free(&c->source);p->source=NULL;ts_portal_wave_reset(&p->waves[0],NULL);
    const TsSample *src=&instrument->current;
    if(!src->data || src->frames<2 || !ts_sample_valid_channels(src->channels)) {
        snprintf(p->message,sizeof(p->message),"CHOOSE A MONO OR STEREO TILE");return 0;
    }
    size_t first=0,last=src->frames;
    if(c->selection_scope && instrument->has_selection) {first=instrument->selection_first;last=instrument->selection_last;}
    if(first>=last || last>src->frames || last-first>TS_PORTAL_MAX_FRAMES) {
        snprintf(p->message,sizeof(p->message),"SELECT A SHORTER SOURCE (PORTAL LIMIT: 8 MILLION FRAMES)");return 0;
    }
    next.data=malloc((last-first)*src->channels*sizeof(float));
    if(!next.data) {snprintf(p->message,sizeof(p->message),"NOT ENOUGH MEMORY FOR SOURCE SNAPSHOT");return 0;}
    memcpy(next.data,src->data+first*src->channels,(last-first)*src->channels*sizeof(float));
    next.frames=last-first;next.channels=src->channels;next.sample_rate=src->sample_rate;
    snprintf(next.name,sizeof(next.name),"%s",src->name);
    c->source=next;c->source_first=first;c->source_last=last;
    c->source_hash=ts_sample_hash(src);c->source_slot=instrument->selected_slot;c->source_page=ui->sample_page;
    p->source=&c->source;
    p->process_selection=0;
    snprintf(p->source_name,sizeof(p->source_name),"TILE %02d %.40s",c->source_slot+1,src->name);
    ts_portal_wave_reset(&p->waves[0],p->source);
    snprintf(p->message,sizeof(p->message),"%s SNAPSHOT READY; DRAG SELECTS AUDITION RANGE; PREVIEW RENDERS SNAPSHOT",
             c->selection_scope && instrument->has_selection?"SELECTION":"WHOLE TILE");
    return 1;
}
static void portal_close(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,
                         PortalController *c)
{
    portal_stop(device,audio,&ui->portal,c);portal_cancel(c);ui->portal.open=0;
    c->auto_pending=0;portal_hover_reset(&ui->portal,c);
    ui->portal.dragging_parameter=-1;ui->portal.dragging_wave=-1;
    ui->portal.search_focus=ui->portal.name_focus=0;ui->portal.number_focus=-1;
    ui->portal.manage_open=0;
    ui->portal.full_action=0;
    ui->master_output_dragging=0;
    SDL_StopTextInput();
    snprintf(ui->status,sizeof(ui->status),"CDP PORTAL CLOSED - SAVED RECIPES AND PINS RETAINED");
}
static void portal_init(PortalController *c,TsPortalUi *p)
{
    char dir[TS_CDP_PATH_MAX];
    memset(c,0,sizeof(*c));ts_sample_init(&c->source);
    for(int i=0;i<TS_PORTAL_HISTORY;++i)ts_cdp_run_result_init(&c->history[i]);
    if(runtime_path_parent(config_file_path(),dir,sizeof(dir)))
        runtime_join_path(c->library_path,sizeof(c->library_path),dir,"cdp-portal.recipes");
    else snprintf(c->library_path,sizeof(c->library_path),"cdp-portal.recipes");
    c->library_writable=ts_portal_library_load(&p->library,c->library_path,p->message,sizeof(p->message));
    if(!c->library_writable)
        fprintf(stderr,"TapeSister CDP Portal: %s\n",p->message);
}
static int portal_worker_cancel(void *data)
{return SDL_AtomicGet(&((PortalWorker*)data)->cancel)!=0;}
static int portal_worker_render(void *data)
{
    PortalWorker *w=data;TsCdpRunOptions options;ts_cdp_run_options_init(&options);
    options.job_id=w->generation;options.cancel_check=portal_worker_cancel;options.cancel_userdata=w;
    /* Unprocessed regions will also enter the result; validate the complete
       snapshot, not just the slice staged to CDP. */
    for(size_t i=0;i<w->input.frames*w->input.channels;++i)if(!isfinite(w->input.data[i])) {
        snprintf(w->error,sizeof(w->error),"SOURCE CONTAINS NONFINITE AUDIO");
        w->result.status=TS_CDP_RUN_FAILED;w->result.finite=0;
        return 0;
    }
    TsSample input=w->input;
    if(w->region.selection) {input.data+=w->region.first*input.channels;input.frames=w->region.last-w->region.first;}
    int ok=ts_cdp_run_portal(&w->runtime,&w->recipe,&input,&options,&w->result,w->error,sizeof(w->error));
    if(ok && w->region.selection && !SDL_AtomicGet(&w->cancel)) {
        TsSample *out=&w->result.output;
        size_t outside=w->input.frames-(w->region.last-w->region.first);
        if(out->frames>TS_PORTAL_MAX_FRAMES-outside) {
            snprintf(w->error,sizeof(w->error),"RESULT PLUS UNPROCESSED AUDIO EXCEEDS PORTAL LIMIT");
            w->result.status=TS_CDP_RUN_FAILED;
        } else {
            size_t total=outside+out->frames;
            float *joined=malloc(total*out->channels*sizeof(float));
            if(!joined) {
                snprintf(w->error,sizeof(w->error),"NOT ENOUGH MEMORY TO ASSEMBLE SELECTION RESULT");
                w->result.status=TS_CDP_RUN_FAILED;
            } else {
                ts_transform_boundary_splice(out,&w->input,w->region.first,w->region.last);
                memcpy(joined,w->input.data,w->region.first*out->channels*sizeof(float));
                memcpy(joined+w->region.first*out->channels,out->data,out->frames*out->channels*sizeof(float));
                w->region.result_last=w->region.first+out->frames;
                memcpy(joined+w->region.result_last*out->channels,w->input.data+w->region.last*out->channels,
                       (w->input.frames-w->region.last)*out->channels*sizeof(float));
                free(out->data);out->data=joined;out->frames=total;
                w->result.peak=ts_sample_peak(out);
                double sum=0;for(size_t i=0;i<total*out->channels;++i)sum+=joined[i];
                w->result.dc_offset=sum/(total*out->channels);
                if(w->result.peak>=.9999f || w->result.clipped_samples)w->result.safety=TS_CDP_SAFETY_HOT;
                else w->result.safety=w->result.peak<.00001f?TS_CDP_SAFETY_SILENT:TS_CDP_SAFETY_SAFE;
            }
        }
    }
    return 0;
}
static int portal_worker_main(void *data)
{
    PortalWorker *w=data;
    if(!w->chain.stage_count)portal_worker_render(w);
    else {
        for(size_t n=0;n<w->input.frames*w->input.channels;++n)if(!isfinite(w->input.data[n])) {
            snprintf(w->error,sizeof(w->error),"SOURCE CONTAINS NONFINITE AUDIO");
            w->result.status=TS_CDP_RUN_FAILED;SDL_AtomicSet(&w->done,1);return 0;
        }
        TsSample original=w->input;

        for(unsigned i=(unsigned)w->chain_start;i<w->chain.stage_count;++i) {
            if(SDL_AtomicGet(&w->cancel)){w->result.status=TS_CDP_RUN_CANCELLED;break;}
            ts_portal_step_get(&w->chain.stages[i],&w->recipe);
            if(w->chain.stages[i].bypass) {
                if(ts_sample_clone(&w->result.output,&w->input,w->error,sizeof(w->error))) {
                    w->result.status=TS_CDP_RUN_OK;w->result.finite=1;
                    w->result.peak=ts_sample_peak(&w->result.output);
                    w->result.safety=w->result.peak>=.9999f?TS_CDP_SAFETY_HOT:w->result.peak<.00001f?TS_CDP_SAFETY_SILENT:TS_CDP_SAFETY_SAFE;
                    w->region.result_last=w->region.last;
                } else w->result.status=TS_CDP_RUN_FAILED;
            } else portal_worker_render(w);
            if(w->result.status!=TS_CDP_RUN_OK) {
                char detail[160];snprintf(detail,sizeof(detail),"%s",w->error);
                snprintf(w->error,sizeof(w->error),"STAGE %u %.30s: %.100s",i+1,w->recipe.name,detail);break;
            }
            if(w->result.cleanup_failed) {
                w->chain_cleanup_failed=1;
                fprintf(stderr,"Portal chain stage %u cleanup: %s\n",i+1,w->result.job_directory);
            }
            w->stages[i]=w->result;ts_cdp_run_result_init(&w->result);
            w->stage_regions[i]=w->region;
            w->stage_regions[i].last=w->original_last;
            w->input=w->stages[i].output;w->region.last=w->region.result_last;
            /* Keep at most 8 million intermediate frames. The newest stage
               always survives; evicted stages can be rendered again. */
            size_t frames=0;for(unsigned j=0;j<=i;++j)frames+=w->stages[j].output.frames;
            for(unsigned j=0;j<i && frames>TS_PORTAL_MAX_FRAMES;++j) {
                frames-=w->stages[j].output.frames;ts_cdp_run_result_free(&w->stages[j]);
            }
        }
        if(w->stages[w->chain.stage_count-1].output.data && !SDL_AtomicGet(&w->cancel)) {
            const TsCdpRunResult *last=&w->stages[w->chain.stage_count-1];
            w->result=*last;ts_sample_init(&w->result.output);
            if(!ts_sample_clone(&w->result.output,&last->output,w->error,sizeof(w->error)))w->result.status=TS_CDP_RUN_FAILED;
        }
        w->input=original;w->region.last=w->original_last;
    }
    SDL_AtomicSet(&w->done,1);return 0;
}
static void portal_worker_free(PortalWorker *w)
{
    if(!w)return;
    if(w->thread)SDL_WaitThread(w->thread,NULL);
    ts_sample_free(&w->input);ts_cdp_run_result_free(&w->result);
    for(int i=0;i<TS_PORTAL_CHAIN_STAGES;++i)ts_cdp_run_result_free(&w->stages[i]);free(w);
}
static void portal_preview(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,
                           PortalController *c)
{
    TsPortalUi *p=&ui->portal;
    c->auto_pending=0; /* Explicit Preview/Cancel consumes any queued update. */
    if(c->worker) {portal_cancel(c);snprintf(p->message,sizeof(p->message),"CANCELLING CDP JOB");return;}
    TsPortalRecipe working=portal_working(p);
    if(!ts_portal_recipe_validate(&working,p->message,sizeof(p->message)))return;
    if(!c->source.data) {snprintf(p->message,sizeof(p->message),"LOAD A SOURCE FIRST");return;}
    if(c->source.channels==2 && !ts_portal_stereo_supported(&working)) {
        snprintf(p->message,sizeof(p->message),"MONO-ONLY PROCESS IN THIS RECIPE; CHOOSE A STEREO-CAPABLE PROCESS");return;
    }
    const TsPortalWave *source_wave=&p->waves[0];
    if(p->process_selection && (!source_wave->has_selection || source_wave->selection_first>=source_wave->selection_last ||
       source_wave->selection_last>c->source.frames)) {
        snprintf(p->message,sizeof(p->message),"DRAW A SOURCE SELECTION OR CHOOSE PROCESS: WHOLE");return;
    }
    PortalWorker *w=calloc(1,sizeof(*w));
    if(!w) {snprintf(p->message,sizeof(p->message),"NOT ENOUGH MEMORY FOR RENDER JOB");return;}
    ts_sample_init(&w->input);ts_cdp_run_result_init(&w->result);
    w->region=(TsPortalRegion){p->process_selection,source_wave->selection_first,source_wave->selection_last,0};
    w->original_last=w->region.last;
    char *base=SDL_GetBasePath();
    int ok=ts_cdp_runtime_discover(&w->runtime,ui->config.cdp_bin_path,base,p->message,sizeof(p->message));
    SDL_free(base);
    const TsSample *start=&c->source;
    if(working.stage_count) {
        unsigned first=0;
        int region_same=w->region.selection==c->cache_region.selection &&
            (!w->region.selection || (w->region.first==c->cache_region.first && w->region.last==c->cache_region.last));
        if(region_same)while(first<working.stage_count && first<c->cache_recipe.stage_count &&
            ts_portal_step_equal(&working.stages[first],&c->cache_recipe.stages[first]))++first;
        if(first==working.stage_count)first=(unsigned)p->chain_stage; /* explicit preview rerolls this stage */
        while(first && !c->stages[first-1].output.data)--first;
        w->chain_start=(int)first;w->chain=working;
        if(first) {start=&c->stages[first-1].output;w->region.last=c->stage_regions[first-1].result_last;}
    }
    if(!ok || !ts_sample_clone(&w->input,start,p->message,sizeof(p->message))) {portal_worker_free(w);return;}
    /* Preserve the original selection's extent for the assembled final result. */
    portal_invalidate(device,audio,p,c);
    c->auto_pending=0;
    w->recipe=p->recipe;w->generation=c->generation;
    w->thread=SDL_CreateThread(portal_worker_main,"TapeSister CDP Portal",w);
    if(!w->thread) {snprintf(p->message,sizeof(p->message),"RENDER THREAD UNAVAILABLE");portal_worker_free(w);return;}
    c->worker=w;p->busy=1;
    snprintf(p->message,sizeof(p->message),"RENDERING %s: %.50s - CANCEL IS AVAILABLE; TILE UNCHANGED",p->process_selection?"SOURCE SELECTION":"WHOLE SOURCE",portal_recipe_name(p));
}
static void portal_select_history(SDL_AudioDeviceID device,AudioState *audio,
                                   TsPortalUi *p,PortalController *c,int index)
{
    if(index<0 || index>=c->history_count)return;
    c->auto_pending=0;
    portal_stop(device,audio,p,c);portal_cancel(c);
    portal_load_recipe(p,&c->recipes[index]);p->result=&c->history[index].output;
    p->valid=1;p->history_selected=index;p->listen_result=1;
    p->safety=c->history[index].safety;p->peak=c->history[index].peak;
    p->parameter_scroll=0;p->number_focus=-1;
    p->selected_tab=p->selected_slot=-1;
    ts_portal_wave_reset(&p->waves[1],p->result);
    p->rendered_region=c->regions[index];p->process_selection=p->rendered_region.selection;
    if(p->chain_active && p->rendered_region.selection==c->cache_region.selection &&
       (!p->process_selection || (p->rendered_region.first==c->cache_region.first && p->rendered_region.last==c->cache_region.last))) {
        for(unsigned i=0;i<p->chain.stage_count && i<c->cache_recipe.stage_count;++i) {
            if(!ts_portal_step_equal(&p->chain.stages[i],&c->cache_recipe.stages[i]))break;
            if(c->stages[i].output.data)p->chain_cached|=1u<<i;
        }
    }
    if(p->process_selection) {
        p->waves[0].has_selection=p->waves[1].has_selection=1;
        p->waves[0].selection_first=p->waves[1].selection_first=p->rendered_region.first;
        p->waves[0].selection_last=p->rendered_region.last;
        p->waves[1].selection_last=p->rendered_region.result_last;
        p->waves[0].playhead=p->waves[1].playhead=p->rendered_region.first;
    }
    snprintf(p->message,sizeof(p->message),"HISTORY %d: %s - READY TO APPLY; AUDITION SELECTIONS REMAIN INDEPENDENT",index+1,
             p->process_selection?"SOURCE SELECTION":"WHOLE SOURCE");
}
/* Selection edits affect the next render without stopping a running loop or
   discarding auditionable audio from the last preview. */
static void portal_selection_changed(TsPortalUi *p,PortalController *c)
{
    if(!p->process_selection)return;
    portal_cancel(c);c->quick_apply=0;p->valid=0;p->full_action=0;p->chain_cached=0;
    portal_auto_schedule(p,c);
    snprintf(p->message,sizeof(p->message),"SOURCE SELECTION CHANGED - PREVIEW AGAIN; AUDITION LOOPS REMAIN INDEPENDENT");
}
static int portal_apply(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,
                         TsInstrument *instrument,PortalController *c,int new_tile);
/* Only the UI thread launches jobs. Edits coalesce while a cancelled worker
   finishes; incomplete text, held drags and modal choices never start a job. */
static void portal_auto_poll(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,
                              PortalController *c,Uint32 now)
{
    TsPortalUi *p=&ui->portal;
    if(!p->open || !p->auto_preview) {c->auto_pending=0;return;}
    if(!c->auto_pending || c->worker || p->dragging_parameter>=0 || p->dragging_wave>=0 ||
       p->number_focus>=0 || p->macro_edit || p->search_focus || p->name_focus || p->manage_open || p->full_action ||
       !SDL_TICKS_PASSED(now,c->auto_due))return;
    portal_preview(device,audio,ui,c);
}
static void portal_poll(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,
                        TsInstrument *instrument,PortalController *c)
{
    TsPortalUi *p=&ui->portal;PortalWorker *w=c->worker;
    if(w && SDL_AtomicGet(&w->done)) {
        SDL_WaitThread(w->thread,NULL);w->thread=NULL;c->worker=NULL;p->busy=0;
        if(p->open && w->generation==c->generation && !SDL_AtomicGet(&w->cancel)) {
            if(w->result.status==TS_CDP_RUN_OK) {
                portal_stop(device,audio,p,c);
                snprintf(w->result.output.name,sizeof(w->result.output.name),"%s",portal_recipe_name(p));
                if(c->source_slot==instrument->selected_slot && c->source_page==ui->sample_page &&
                   c->source_hash==ts_sample_hash(&instrument->current) &&
                   (c->source_first>0 || c->source_last<instrument->current.frames)) {
                    size_t first=(!w->region.selection || !w->region.first)?c->source_first:0;
                    size_t last=(!w->region.selection || w->region.last==c->source.frames)?c->source_last:instrument->current.frames;
                    ts_transform_boundary_splice(&w->result.output,&instrument->current,
                                                  first,last);
                    float peak=ts_sample_peak(&w->result.output);
                    if(peak>w->result.peak)w->result.peak=peak;
                    if(peak>=0.9999f)w->result.safety=TS_CDP_SAFETY_HOT;
                }
                size_t frames=w->result.output.frames;
                for(int i=0;i<c->history_count;++i)frames+=c->history[i].output.frames;
                /* At most four results / 32 MB of history; source and one
                   worker are separately bounded. Eviction is oldest first. */
                while(c->history_count && (c->history_count==TS_PORTAL_HISTORY || frames>TS_PORTAL_MAX_FRAMES)) {
                    frames-=c->history[0].output.frames;ts_cdp_run_result_free(&c->history[0]);
                    for(int i=1;i<c->history_count;++i) {c->history[i-1]=c->history[i];c->recipes[i-1]=c->recipes[i];c->regions[i-1]=c->regions[i];}
                    --c->history_count;ts_cdp_run_result_init(&c->history[c->history_count]);
                }
                int slot=c->history_count++;
                c->history[slot]=w->result;ts_cdp_run_result_init(&w->result);c->recipes[slot]=portal_working(p);
                c->regions[slot]=w->region;
                p->history_count=c->history_count;
                for(int i=0;i<c->history_count;++i)snprintf(p->history_names[i],sizeof(p->history_names[i]),"%.23s",c->recipes[i].name);
                int selected_tab=p->selected_tab,selected_slot=p->selected_slot,chain_stage=p->chain_stage;
                portal_select_history(device,audio,p,c,slot);
                p->selected_tab=selected_tab;p->selected_slot=selected_slot;
                if(w->chain.stage_count) {
                    for(unsigned i=(unsigned)w->chain_start;i<TS_PORTAL_CHAIN_STAGES;++i) {
                        ts_cdp_run_result_free(&c->stages[i]);
                        c->stages[i]=w->stages[i];ts_cdp_run_result_init(&w->stages[i]);c->stage_regions[i]=w->stage_regions[i];
                    }
                    c->cache_recipe=w->chain;c->cache_region=w->region;
                    size_t frames=0;for(unsigned i=0;i<w->chain.stage_count;++i)frames+=c->stages[i].output.frames;
                    for(unsigned i=0;i+1<w->chain.stage_count && frames>TS_PORTAL_MAX_FRAMES;++i) {
                        frames-=c->stages[i].output.frames;ts_cdp_run_result_free(&c->stages[i]);
                    }
                    p->chain_cached=0;
                    for(unsigned i=0;i<w->chain.stage_count;++i)if(c->stages[i].output.data)p->chain_cached|=1u<<i;
                    p->chain_stage=chain_stage;ts_portal_step_get(&p->chain.stages[chain_stage],&p->recipe);
                }
                snprintf(p->message,sizeof(p->message),"PREVIEW READY (%s) - A/B TO COMPARE; APPLY CHANGES THE TILE",ts_cdp_safety_name(p->safety));
                if(w->chain_cleanup_failed)snprintf(p->message,sizeof(p->message),"CHAIN READY; STAGE TEMP CLEANUP FAILED - SEE DIAGNOSTIC LOG");
                if(c->quick_apply) {c->quick_apply=0;portal_apply(device,audio,ui,instrument,c,0);}
            } else snprintf(p->message,sizeof(p->message),"CDP: %.150s",w->error[0]?w->error:"RENDER FAILED");
            if(w->result.cleanup_failed)fprintf(stderr,"Portal cleanup: %s\n",w->result.job_directory);
            if(p->history_selected>=0 && c->history[p->history_selected].cleanup_failed) {
                fprintf(stderr,"Portal cleanup: %s\n",c->history[p->history_selected].job_directory);
                snprintf(p->message,sizeof(p->message),"RENDER READY; TEMP CLEANUP FAILED - SEE DIAGNOSTIC LOG");
            }
        } else if(p->open)snprintf(p->message,sizeof(p->message),"OLD JOB DISCARDED; PRESS PREVIEW FOR CURRENT SETTINGS");
        portal_worker_free(w);
    }
    portal_auto_poll(device,audio,ui,c,SDL_GetTicks());
    if(p->playing) {
        if(device)SDL_LockAudioDevice(device);
        const TsSample *sample=p->listen_result?p->result:p->source;
        int active=audio->playing && sample && audio->sample==sample;
        size_t position=active?(size_t)audio->position:0;
        p->note_count=0;uint64_t serial=0;
        for(int i=0;i<TS_NOTE_BANK_VOICE_CAPACITY;++i) {
            const TsNoteVoice *v=&audio->notes.voices[i];
            if(v->active && v->preview && sample && v->sample==sample) {
                ++p->note_count;
                if(v->serial>serial) {serial=v->serial;position=(size_t)v->position;}
            }
        }
        active=active || p->note_count>0;
        if(device)SDL_UnlockAudioDevice(device);
        TsPortalWave *wave=&p->waves[p->listen_result];
        p->playing=active;
        wave->playhead=active?position:wave->has_selection?wave->selection_first:0;
    }
}
static void portal_play(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,
                        PortalController *c,int output_rate,int toggle)
{
    TsPortalUi *p=&ui->portal;
    if(toggle && p->playing) {portal_stop(device,audio,p,c);return;}
    const TsSample *s=p->listen_result?p->result:p->source;
    TsPortalWave *w=&p->waves[p->listen_result];
    if(!device || !ts_audio_output_is_available() || output_rate<=0 || !s || !s->data || !s->frames) {
        snprintf(p->message,sizeof(p->message),"NO AUDIO OUTPUT OR NO AUDITIONABLE SAMPLE");return;
    }
    SDL_LockAudioDevice(device);runtime_note_clear(audio);
    audio->sample=s;audio->range_start=w->has_selection?w->selection_first:0;
    audio->range_end=w->has_selection?w->selection_last:s->frames;
    audio->position=w->playhead>=audio->range_start && w->playhead<audio->range_end?w->playhead:audio->range_start;
    audio->pitch=1;audio->source=TS_AUDITION_CURRENT;audio->range=TS_AUDITION_ALL;
    audio->looping=p->loop;audio->loop_mode=TS_LOOP_FORWARD;audio->loop_direction=1;
    audio->crossfade_frames=p->loop?s->sample_rate/200:0;
    if(audio->crossfade_frames*2>audio->range_end-audio->range_start)audio->crossfade_frames=(audio->range_end-audio->range_start)/2;
    audio->bank_slot=-1;audio->step=(double)s->sample_rate/output_rate;
    restart_audio_attack(audio,output_rate,ui->config.voice_attack_ms);audio->playing=1;
    SDL_UnlockAudioDevice(device);p->playing=1;
}
static void portal_ab(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,
                      PortalController *c,int output_rate,int next)
{
    TsPortalUi *p=&ui->portal;int prev=p->listen_result;
    if(prev==next)return;
    const TsSample *a=prev?p->result:p->source,*b=next?p->result:p->source;
    size_t old=p->waves[prev].playhead;
    if(device)SDL_LockAudioDevice(device);
    int was=audio->playing && a && audio->sample==a;
    if(was) {audio->playing=0;audio->sample=NULL;}
    int notes=0;
    for(int i=0;i<TS_NOTE_BANK_VOICE_CAPACITY;++i) {
        TsNoteVoice *v=&audio->notes.voices[i];
        if(!v->active || !v->preview || v->sample!=a)continue;
        if(!b || !b->data || !b->frames || !a->sample_rate || output_rate<=0) {memset(v,0,sizeof(*v));continue;}
        v->position=v->position*b->sample_rate/a->sample_rate;
        v->sample=b;v->step=(double)b->sample_rate/output_rate*v->pitch;
        portal_note_range(v,&p->waves[next],b,p->loop,0);
        notes+=v->active;
    }
    if(device)SDL_UnlockAudioDevice(device);
    p->listen_result=next;p->note_count=notes;p->playing=notes>0;
    /* Match elapsed seconds, never assume equal output duration. */
    if(a && b && a->sample_rate) {
        size_t pos=(size_t)((double)old*b->sample_rate/a->sample_rate);
        p->waves[next].playhead=pos<b->frames?pos:0;
    }
    if(was)portal_play(device,audio,ui,c,output_rate,0);
}
static void portal_note_on(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,
                            int note,int output_rate)
{
    TsPortalUi *p=&ui->portal;TsNoteEvent event;
    const TsSample *s=p->listen_result?p->result:p->source;
    const TsPortalWave *w=&p->waves[p->listen_result];
    const TsTuning unity={TS_KEYBOARD_BASE_NOTE,0.0f};
    if(!device || !ts_audio_output_is_available() || output_rate<=0 || !s || !s->data) {
        snprintf(p->message,sizeof(p->message),"NO AUDIO OUTPUT OR NO AUDITIONABLE SAMPLE");return;
    }
    if(!ts_note_event_qwerty(&event,note,ts_ui_keyboard_base_note(ui)))return;
    SDL_LockAudioDevice(device);
    ts_note_bank_set_attack_ms(&audio->notes,ui->config.voice_attack_ms);
    TsNoteStartResult result=ts_note_bank_start_preview_event(&audio->notes,s,&unity,&event,
        w->has_selection?w->selection_first:0,w->has_selection?w->selection_last:s->frames,p->loop,output_rate);
    if(result==TS_NOTE_STARTED) {audio->playing=0;audio->sample=NULL;p->playing=1;}
    SDL_UnlockAudioDevice(device);
    char name[8];
    snprintf(p->message,sizeof(p->message),result==TS_NOTE_STARTED?"%s %s - C4 IS ORIGINAL PITCH; F1-F8 OCTAVE; SPACE STOPS ALL":
        result==TS_NOTE_LIMIT_REACHED?"CHORD LIMIT: 5 NOTES":"NOTE COULD NOT START",
        p->listen_result?"RESULT":"SOURCE",ts_midi_note_name(event.midi_note,name,sizeof(name)));
}
static void portal_save(TsPortalUi *p,PortalController *c,int pin)
{
    if(!c->library_writable) {
        snprintf(p->message,sizeof(p->message),"SAVE BLOCKED: EXISTING RECIPE FILE COULD NOT BE READ; BACK IT UP / REPAIR AND RESTART");return;
    }
    TsPortalLibrary next=p->library;
    int index=p->pin_slot;
    if(!pin) {for(index=0;index<TS_PORTAL_SLOTS && next.recipes[index].process_id[0];++index);}
    if(index>=TS_PORTAL_SLOTS) {snprintf(p->message,sizeof(p->message),"SAVED RECIPE LIBRARY IS FULL (32); NO EXISTING RECIPE CHANGED");return;}
    if(pin && next.pins[index].process_id[0]) {snprintf(p->message,sizeof(p->message),"PIN SLOT OCCUPIED - USE MANAGE TOOLS TO UPDATE OR REPLACE IT");return;}
    TsPortalRecipe r=portal_working(p);if(pin && p->exact_pin)ts_portal_recipe_exact(&r);
    if(!ts_portal_recipe_validate(&r,p->message,sizeof(p->message)))return;
    if(pin)next.pins[index]=r;else next.recipes[index]=r;
    if(!ts_portal_library_save(&next,c->library_path,p->message,sizeof(p->message)))return;
    p->library=next;
    p->selected_tab=pin?2:1;p->selected_slot=index;
    snprintf(p->message,sizeof(p->message),pin?"PIN %02d SAVED; OPEN PINS TO REUSE ON ANOTHER TILE":"RECIPE %02d SAVED; SOURCE AUDIO IS NOT PART OF THE RECIPE",index+1);
}
static int portal_apply(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,
                         TsInstrument *instrument,PortalController *c,int new_tile)
{
    TsPortalUi *p=&ui->portal;char error[160];
    int promote=new_tile!=1;
    if(p->chain_active && p->chain_audition>=0 && p->valid && p->history_selected>=0) {
        portal_stop(device,audio,p,c);p->result=&c->history[p->history_selected].output;
        p->chain_audition=-1;ts_portal_wave_reset(&p->waves[1],p->result);
    }
    TsPortalRegion region=p->rendered_region;
    if(!p->valid || !p->result || p->busy) {snprintf(p->message,sizeof(p->message),"PREVIEW CURRENT SETTINGS BEFORE APPLYING");return 0;}
    if(ui->capture_state!=TS_CAPTURE_IDLE || ui->external_record_bank || ui->sister_capture_active) {
        snprintf(p->message,sizeof(p->message),"FINISH CAPTURE BEFORE CHANGING A TILE");return 0;
    }
    if(!new_tile && (c->source_slot!=instrument->selected_slot || c->source_page!=ui->sample_page ||
        c->source_hash!=ts_sample_hash(&instrument->current))) {
        snprintf(p->message,sizeof(p->message),"SOURCE TILE CHANGED - RELOAD; OLD PREVIEW CANNOT REPLACE IT");return 0;
    }
    int destination=instrument->selected_slot;
    if(new_tile) {
        for(destination=0;destination<TS_BANK_SLOT_COUNT && instrument->bank[destination].occupied;++destination);
        if(destination>=TS_BANK_SLOT_COUNT) {snprintf(p->message,sizeof(p->message),"PAGE FULL - CREATE A NEW SAMPLE PAGE OR CANCEL");p->full_action=new_tile;return 0;}
    }
    /* Allocate before editing so a failed snapshot allocation leaves both the
       tile and its auditionable preview intact. The promoted source is exactly
       the replacement, including when only a main-canvas region was rendered. */
    TsSample promoted;ts_sample_init(&promoted);
    if(promote && !ts_sample_clone(&promoted,p->result,error,sizeof(error))) {
        snprintf(p->message,sizeof(p->message),"APPLY FAILED: %.144s",error);return 0;
    }
    portal_stop(device,audio,p,c);
    lock_edit(device,audio);
    ts_sister_runtime_prepare_slot_replacement(&audio->sister,destination);
    int ok;
    if(new_tile && !instrument->bank[instrument->selected_slot].occupied) {
        /* A newly created page has no occupied tile for the Drone copy helper.
           Import the already rendered audio directly into its empty slot. */
        ok=ts_instrument_select_bank(instrument,destination,error,sizeof(error)) &&
           ts_instrument_import_sample(instrument,p->result,1,0,p->result->frames,TS_LOOP_FORWARD,error,sizeof(error));
    } else if(new_tile)ok=ts_instrument_copy_drone_to_new_tile(instrument,p->result,&destination,error,sizeof(error));
    else ok=ts_instrument_apply_rendered_replacement(instrument,p->result,c->source_first,c->source_last,error,sizeof(error));
    unlock_edit(device,audio,ui,instrument);
    if(ok) {
        ui->bank_view_slot=-1;p->valid=0;
        if(promote) {
            portal_invalidate(device,audio,p,c);portal_clear_history(c,p);
            ts_sample_free(&c->source);c->source=promoted;ts_sample_init(&promoted);
            if(new_tile) {c->source_first=0;c->source_slot=instrument->selected_slot;c->source_page=ui->sample_page;}
            c->source_last=c->source_first+c->source.frames;
            c->source_hash=ts_sample_hash(&instrument->current);
            p->source=&c->source;p->listen_result=0;
            snprintf(p->source_name,sizeof(p->source_name),"TILE %02d %.40s",c->source_slot+1,instrument->current.name);
            ts_portal_wave_reset(&p->waves[0],p->source);
            if(region.selection) {
                p->waves[0].has_selection=1;p->waves[0].selection_first=region.first;
                p->waves[0].selection_last=region.result_last;p->waves[0].playhead=region.first;
            }
        }
        p->full_action=0;
        snprintf(p->message,sizeof(p->message),new_tile==2?
            "NEW TILE %02d IS NOW THE SOURCE; READY FOR NEXT PROCESS":new_tile?
            "COPIED TO TILE %02d; PORTAL SOURCE UNCHANGED":
            "APPLIED TO TILE %02d; RESULT IS NEW SOURCE; READY FOR NEXT PROCESS; UNDO ON MAIN",instrument->selected_slot+1);
    } else snprintf(p->message,sizeof(p->message),"APPLY FAILED: %.144s",error);
    ts_sample_free(&promoted);return ok;
}
static void portal_continue_page(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,
                                  TsInstrument *instrument,PortalController *c,TransformController *transform)
{
    TsPortalUi *p=&ui->portal;int action=p->full_action;
    if(!action || !p->valid || p->busy) {p->full_action=0;return;}
    if(ui->capture_state!=TS_CAPTURE_IDLE || ui->external_record_bank || ui->sister_capture_active) {
        snprintf(p->message,sizeof(p->message),"FINISH CAPTURE BEFORE CREATING A PAGE");return;
    }
    if(!c->pages || !c->pages->active_live) {
        snprintf(p->message,sizeof(p->message),"NEW PAGE IS ONLY AVAILABLE IN THE SAMPLE BANK");return;
    }
    size_t previous=ts_sample_pages_active(c->pages);
    portal_stop(device,audio,p,c);
    if(!append_sample_page(device,c->external_input,audio,ui,instrument,c->pages,transform)) {
        snprintf(p->message,sizeof(p->message),"%.159s",ui->status);return;
    }
    if(!portal_apply(device,audio,ui,instrument,c,action)) {
        /* A failed tile copy must not strand the user on an empty new page. */
        lock_edit(device,audio);
        (void)ts_sample_pages_remove_last_and_switch(c->pages,instrument,previous,NULL,0);
        (void)ts_sister_runtime_set_page(&audio->sister,previous,instrument);
        ui->sample_page=(int)previous;ui->sample_page_count=(int)ts_sample_pages_count(c->pages);
        unlock_edit(device,audio,ui,instrument);
    }
}
static int portal_parameter_at(TsPortalUi *p,int row)
{
    TsPortalControl control;
    if(!ts_portal_control_at(p,row+p->parameter_scroll,&control))return -1;
    if(control.stage>=0 && control.stage!=p->chain_stage) {
        (void)portal_working(p);p->chain_stage=control.stage;
        ts_portal_step_get(&p->chain.stages[p->chain_stage],&p->recipe);
    }
    return control.index;
}
static void portal_parameter_drag(SDL_AudioDeviceID device,AudioState *audio,
                                  TsPortalUi *p,PortalController *c,int index,int x)
{
    const TsPortalProcess *proc=ts_portal_process_find(p->recipe.process_id);
    if(!proc || index<0 || (unsigned)index>=proc->parameter_count)return;
    const TsPortalParam *s=&proc->parameters[index];
    double t=(x-253)/124.0;if(t<0)t=0;if(t>1)t=1;
    double lo,hi;ts_portal_control_bounds(&p->recipe,(unsigned)index,p->macro_view,&lo,&hi);
    double v=lo+t*(hi-lo);
    if(hi>100 && lo>=0)v=lo+expm1(t*log1p(hi-lo));
    v=ts_portal_parameter_quantize(proc,(unsigned)index,v);
    if(p->recipe.values[index]!=v) {portal_invalidate(device,audio,p,c);p->recipe.values[index]=v;}
    snprintf(p->message,sizeof(p->message),"%s; %.6g..%.6g; CLICK VALUE TO TYPE",s->help,lo,hi);
}
static void portal_macro_open(TsPortalUi *p,int index)
{
    const TsPortalProcess *proc=ts_portal_process_find(p->recipe.process_id);
    const TsPortalMacro *m=&p->recipe.macros[index];double lo,hi;
    ts_portal_control_bounds(&p->recipe,(unsigned)index,1,&lo,&hi);
    p->macro_edit=index+1;p->macro_field=0;p->number_focus=-1;p->search_focus=p->name_focus=0;
    snprintf(p->macro_text[0],sizeof(p->macro_text[0]),"%s",m->configured?m->name:proc->parameters[index].label);
    snprintf(p->macro_text[1],sizeof(p->macro_text[1]),"%.9g",lo);
    snprintf(p->macro_text[2],sizeof(p->macro_text[2]),"%.9g",hi);
    snprintf(p->message,sizeof(p->message),"NAME AND RANGE FOR THIS CONTROL; CDP INPUT-DEPENDENT LIMITS STILL APPLY");
}
static int portal_macro_event(const SDL_Event *event,TsPortalUi *p,int x,int y)
{
    char *text=p->macro_text[p->macro_field];int apply=0;
    if(event->type==SDL_TEXTINPUT) {
        for(const char *s=event->text.text;*s;++s)if(*s>=32 && *s<127 && *s!='|' && strlen(text)+1<32) {
            size_t n=strlen(text);text[n]=*s;text[n+1]=0;
        }
    } else if(event->type==SDL_KEYDOWN) {
        SDL_Keycode key=event->key.keysym.sym;
        if(key==SDLK_ESCAPE)p->macro_edit=0;
        else if(key==SDLK_BACKSPACE && text[0])text[strlen(text)-1]=0;
        else if(key==SDLK_TAB)p->macro_field=(p->macro_field+((event->key.keysym.mod&KMOD_SHIFT)?2:1))%3;
        else if(key==SDLK_RETURN || key==SDLK_KP_ENTER)apply=1;
        else if((event->key.keysym.mod&KMOD_CTRL) && key==SDLK_a)text[0]=0;
    } else if(event->type==SDL_MOUSEBUTTONDOWN && event->button.button==SDL_BUTTON_LEFT) {
        if(x>=268 && x<466 && y>=157 && y<231)p->macro_field=(y-157)/25;
        if(y>=250 && y<272) {
            if(x>=178 && x<268)apply=1;
            else if(x>=277 && x<367) {
                memset(&p->recipe.macros[p->macro_edit-1],0,sizeof(TsPortalMacro));
                (void)portal_working(p);p->macro_edit=0;
                snprintf(p->message,sizeof(p->message),"MACRO RESTORED TO CDP NAME AND FULL RANGE; SAVE TO KEEP");
            } else if(x>=376 && x<466)p->macro_edit=0;
        }
    } else if(event->type==SDL_DROPFILE)SDL_free(event->drop.file);
    if(apply) {
        char *a,*b;double lo=strtod(p->macro_text[1],&a),hi=strtod(p->macro_text[2],&b);
        if(a==p->macro_text[1] || *a || b==p->macro_text[2] || *b) {
            snprintf(p->message,sizeof(p->message),"TYPE NUMERIC MINIMUM AND MAXIMUM");return 1;
        }
        if(ts_portal_macro_set(&p->recipe,(unsigned)p->macro_edit-1,p->macro_text[0],lo,hi,p->message,sizeof(p->message))) {
            (void)portal_working(p);p->macro_edit=0;
            snprintf(p->message,sizeof(p->message),"MACRO NAME AND RANGE UPDATED; SAVE AS / UPDATE TOOL TO KEEP");
        }
    }
    return event->type==SDL_WINDOWEVENT?0:1;
}
static int portal_sync_loop(SDL_AudioDeviceID device,AudioState *audio,TsPortalUi *p,
                            int which,int seek)
{
    TsPortalWave *w=&p->waves[which];
    int live=sync_preview_loop(device,audio,which?p->result:p->source,p->loop,
        w->has_selection,w->selection_first,w->selection_last,&w->playhead,seek);
    const TsSample *s=which?p->result:p->source;
    size_t requested=w->playhead,display=requested;uint64_t serial=0;
    if(device)SDL_LockAudioDevice(device);
    if(p->loop && s)for(int i=0;i<TS_NOTE_BANK_VOICE_CAPACITY;++i) {
        TsNoteVoice *v=&audio->notes.voices[i];
        if(v->active && v->preview && v->sample==s && v->looping) {
            w->playhead=requested;portal_note_range(v,w,s,1,seek);
            if(v->active) {live=1;if(v->serial>serial) {serial=v->serial;display=(size_t)v->position;}}
        }
    }
    if(device)SDL_UnlockAudioDevice(device);
    w->playhead=display;
    if(live)p->playing=1;
    return live;
}
static void portal_resize_selection(SDL_AudioDeviceID device,AudioState *audio,
                                     TsUiState *ui,int which,int x,int wheel)
{
    TsPortalUi *p=&ui->portal;TsPortalWave *w=&p->waves[which];
    if(resize_preview_selection(which?p->result:p->source,w->has_selection,
        ts_portal_wave_frame(w,x-154),wheel,ui->config.rotate_wheel_coarse,
        &w->selection_first,&w->selection_last,p->message,sizeof(p->message))) {
        w->playhead=w->selection_first;
        (void)portal_sync_loop(device,audio,p,which,0);
    }
}
static void portal_manage_select(TsPortalUi *p,int slot)
{
    p->manage_slot=slot;p->manage_action=0;p->manage_name_focus=0;
    const TsPortalRecipe *r=p->manage_tab?&p->library.pins[slot]:&p->library.recipes[slot];
    snprintf(p->manage_name,sizeof(p->manage_name),"%s",r->name);
}
static void portal_manage_open(TsPortalUi *p)
{
    p->manage_open=1;p->manage_tab=p->selected_tab>=1?p->selected_tab-1:p->tab==2;
    int slot=p->selected_tab==p->manage_tab+1 && p->selected_slot>=0?p->selected_slot:p->manage_tab?p->pin_slot:0;
    portal_manage_select(p,slot);p->manage_scroll=(slot/8)*8;
    p->manage_recipe=portal_working(p);
    p->search_focus=p->name_focus=0;p->number_focus=-1;
    p->dragging_parameter=p->dragging_wave=-1;
    snprintf(p->message,sizeof(p->message),"SELECT A NUMBERED SLOT; UPDATE USES CURRENT SETTINGS; REPLACE CAN CHANGE PROCESS");
}
static void portal_manage_commit(TsPortalUi *p,PortalController *c)
{
    if(!c->library_writable) {
        snprintf(p->message,sizeof(p->message),"LIBRARY UNREADABLE: REPAIR THE EXISTING RECIPE FILE BEFORE EDITING");return;
    }
    int action=p->manage_action,slot=p->manage_slot,pin=p->manage_tab;
    TsPortalRecipe r=p->manage_recipe;
    if(pin && p->exact_pin)ts_portal_recipe_exact(&r);
    if(!ts_portal_library_edit(&p->library,c->library_path,pin,slot,(TsPortalEdit)action,
                               &r,p->manage_name,p->message,sizeof(p->message)))return;
    if(p->selected_tab==pin+1 && p->selected_slot==slot) {
        if(action==TS_PORTAL_RENAME) {
            memset(portal_recipe_name(p),0,sizeof(p->recipe.name));
            memset(p->manage_recipe.name,0,sizeof(p->manage_recipe.name));
            snprintf(portal_recipe_name(p),sizeof(p->recipe.name),"%s",p->manage_name);
            snprintf(p->manage_recipe.name,sizeof(p->manage_recipe.name),"%s",p->manage_name);
        }
        else if(action==TS_PORTAL_REMOVE)p->selected_tab=p->selected_slot=-1;
    }
    if(pin)p->pin_slot=slot;
    p->scroll=0;
    portal_manage_select(p,slot);
    snprintf(p->message,sizeof(p->message),"%s %02d %s - SAVED",pin?"PIN":"RECIPE",slot+1,
        action==TS_PORTAL_RENAME?"RENAMED":action==TS_PORTAL_UPDATE?"UPDATED":action==TS_PORTAL_REPLACE?"REPLACED":"REMOVED");
}
static int portal_manage_event(SDL_Event *e,SDL_Window *window,TsPortalUi *p,PortalController *c,int x,int y)
{
    if(e->type==SDL_DROPFILE) {SDL_free(e->drop.file);return 1;}
    if(e->type==SDL_TEXTINPUT && p->manage_name_focus) {
        for(const char *s=e->text.text;*s;++s)if((unsigned char)*s>=32 && (unsigned char)*s<127 && *s!='|') {
            size_t n=strlen(p->manage_name);if(n+1<sizeof(p->manage_name)) {p->manage_name[n]=*s;p->manage_name[n+1]=0;}
        }
    } else if(e->type==SDL_KEYDOWN) {
        SDL_Keycode key=e->key.keysym.sym;
        if(key==SDLK_ESCAPE) {
            if(p->manage_action || p->manage_name_focus)portal_manage_select(p,p->manage_slot);
            else p->manage_open=0;
        } else if(p->manage_name_focus && key==SDLK_BACKSPACE && p->manage_name[0])
            p->manage_name[strlen(p->manage_name)-1]=0;
        else if(p->manage_name_focus && key==SDLK_a && (e->key.keysym.mod&KMOD_CTRL))p->manage_name[0]=0;
        else if(!e->key.repeat && key==SDLK_RETURN && p->manage_name_focus) {
            p->manage_name_focus=0;p->manage_action=TS_PORTAL_RENAME;
        }
    } else if(e->type==SDL_MOUSEWHEEL && !p->manage_action) {
        int rx,ry;SDL_GetMouseState(&rx,&ry);logical_mouse(window,rx,ry,&x,&y);
        if(x>=94 && x<295 && y>=107 && y<260) {
            int d=e->wheel.direction==SDL_MOUSEWHEEL_FLIPPED?-e->wheel.y:e->wheel.y;
            p->manage_scroll-=d;if(p->manage_scroll<0)p->manage_scroll=0;
            if(p->manage_scroll>TS_PORTAL_SLOTS-8)p->manage_scroll=TS_PORTAL_SLOTS-8;
        }
    } else if(e->type==SDL_MOUSEBUTTONDOWN && e->button.button==SDL_BUTTON_LEFT) {
        if(x>=472 && x<542 && y>=62 && y<78) {p->manage_open=0;return 1;}
        if(p->manage_action) {
            if(y>=300 && y<316 && x>=310 && x<420)portal_manage_commit(p,c);
            else if(y>=300 && y<316 && x>=430 && x<540)portal_manage_select(p,p->manage_slot);
            return 1;
        }
        if(y>=86 && y<102 && x>=94 && x<294) {
            p->manage_tab=x>=196;p->manage_scroll=0;portal_manage_select(p,0);
        } else if(y>=109 && y<253 && x>=94 && x<294)portal_manage_select(p,p->manage_scroll+(y-109)/18);
        else if(y>=258 && y<274 && x>=94 && x<294) {
            p->manage_scroll+=x<194?-8:8;
            if(p->manage_scroll<0)p->manage_scroll=0;
            if(p->manage_scroll>24)p->manage_scroll=24;
        } else if(y>=178 && y<194 && x>=310 && x<542)p->manage_name_focus=1;
        else if(y>=205 && y<243 && x>=310 && x<542) {
            int action=y<223?(x<426?TS_PORTAL_RENAME:TS_PORTAL_UPDATE):(x<426?TS_PORTAL_REPLACE:TS_PORTAL_REMOVE);
            const TsPortalRecipe *r=p->manage_tab?&p->library.pins[p->manage_slot]:&p->library.recipes[p->manage_slot];
            if(action!=TS_PORTAL_REPLACE && !r->process_id[0]) {
                snprintf(p->message,sizeof(p->message),"CHOOSE AN OCCUPIED SLOT; REPLACE CAN FILL AN EMPTY SLOT");return 1;
            }
            if(action==TS_PORTAL_UPDATE && strcmp(r->process_id,p->manage_recipe.process_id)) {
                snprintf(p->message,sizeof(p->message),"DIFFERENT PROCESS: USE REPLACE INSTEAD");return 1;
            }
            p->manage_action=action;p->manage_name_focus=0;
        }
    }
    return e->type==SDL_WINDOWEVENT?0:1;
}
static void portal_chain_select(SDL_AudioDeviceID device,AudioState *audio,TsPortalUi *p,PortalController *c,int index)
{
    (void)portal_working(p);portal_stop(device,audio,p,c);
    p->chain_stage=index;p->chain_add=0;p->parameter_scroll=0;p->number_focus=-1;
    ts_portal_step_get(&p->chain.stages[index],&p->recipe);
    p->chain_audition=index;p->listen_result=1;
    p->result=(p->chain_cached&(1u<<index))?&c->stages[index].output:NULL;
    if(p->result){p->peak=c->stages[index].peak;p->safety=c->stages[index].safety;}
    ts_portal_wave_reset(&p->waves[1],p->result);
    if(p->result && c->stage_regions[index].selection) {
        p->waves[1].has_selection=1;p->waves[1].selection_first=c->stage_regions[index].first;
        p->waves[1].selection_last=c->stage_regions[index].result_last;
        p->waves[1].playhead=p->waves[1].selection_first;
    }
    snprintf(p->message,sizeof(p->message),p->result?
        "STAGE %d OUTPUT - PLAY / QWERTY TO AUDITION; APPLY ALWAYS USES THE FINAL CHAIN":
        "STAGE %d - PREVIEW TO RENDER; PICK A PROCESS TO REPLACE THIS STAGE",index+1);
}
static int portal_chain_click(SDL_AudioDeviceID device,AudioState *audio,TsPortalUi *p,PortalController *c,int x,int y)
{
    if(x>=8 && x<62 && y>=360 && y<377) {
        (void)portal_working(p);portal_invalidate(device,audio,p,c);portal_clear_history(c,p);
        p->chain_active=!p->chain_active;p->chain_add=0;p->chain_stage=0;
        if(p->chain_active) {
            memset(&p->chain,0,sizeof(p->chain));p->chain.stage_count=1;p->chain.version=1;
            snprintf(p->chain.process_id,sizeof(p->chain.process_id),"@chain");
            snprintf(p->chain.name,sizeof(p->chain.name),"MY CHAIN");
            ts_portal_step_set(&p->chain.stages[0],&p->recipe);
        }
        snprintf(p->message,sizeof(p->message),p->chain_active?
            "CHAIN: ADD THEN PICK A PROCESS; ROWS AUDITION STAGES; APPLY USES FINAL OUTPUT":
            "SINGLE PROCESS MODE - SELECTED STAGE KEPT; SAVED CHAINS ARE RETAINED");return 1;
    }
    if(!p->chain_active)return 0;
    if(x>=478 && x<626 && y>=63 && y<79) {
        (void)portal_working(p);
        if(x<526) {
            if(p->chain.stage_count==TS_PORTAL_CHAIN_STAGES)snprintf(p->message,sizeof(p->message),"CHAIN FULL: MAXIMUM EIGHT STAGES");
            else {p->chain_add=!p->chain_add;p->tab=0;p->scroll=0;p->family=0;p->query[0]=0;
                snprintf(p->message,sizeof(p->message),p->chain_add?"PICK A PROCESS IN THE LIBRARY TO INSERT AFTER STAGE %d":"ADD CANCELLED; LIBRARY NOW REPLACES THE SELECTED STAGE",p->chain_stage+1);}
        } else {
            int to=p->chain_stage+(x<576?-1:1);
            if(to>=0 && to<(int)p->chain.stage_count) {
                TsPortalStep temp=p->chain.stages[to];p->chain.stages[to]=p->chain.stages[p->chain_stage];p->chain.stages[p->chain_stage]=temp;
                int first=to<p->chain_stage?to:p->chain_stage;
                portal_invalidate(device,audio,p,c);p->chain_cached&=(1u<<first)-1;
                p->chain_stage=to;ts_portal_step_get(&p->chain.stages[to],&p->recipe);
            }
        }
        return 1;
    }
    if(x>=478 && x<626 && y>=84 && y<196) {
        int i=(y-84)/14;if(i>=(int)p->chain.stage_count)return 1;
        portal_chain_select(device,audio,p,c,i);
        if(x>=602) {portal_invalidate(device,audio,p,c);p->chain.stages[i].bypass=!p->chain.stages[i].bypass;}
        return 1;
    }
    if(x>=478 && x<626 && y>=201 && y<217) {
        if(x<550) {
            if(p->chain.stage_count==1)snprintf(p->message,sizeof(p->message),"KEEP ONE STAGE; SINGLE RETURNS TO ONE-PROCESS MODE");
            else {
                (void)portal_working(p);portal_invalidate(device,audio,p,c);
                for(unsigned i=(unsigned)p->chain_stage;i+1<p->chain.stage_count;++i)p->chain.stages[i]=p->chain.stages[i+1];
                memset(&p->chain.stages[--p->chain.stage_count],0,sizeof(TsPortalStep));
                if(p->chain_stage>=(int)p->chain.stage_count)--p->chain_stage;
                ts_portal_step_get(&p->chain.stages[p->chain_stage],&p->recipe);
            }
        } else {
            const TsPortalProcess *proc=ts_portal_process_find(p->recipe.process_id);
            snprintf(p->message,sizeof(p->message),"%s",p->macro_view && p->chain.description[0]?p->chain.description:proc?proc->description:"");
        }
        return 1;
    }
    if(x>=501 && x<626 && y>=360 && y<377) {
        if(p->valid && p->history_selected>=0) {
            portal_stop(device,audio,p,c);p->chain_audition=-1;p->result=&c->history[p->history_selected].output;
            p->peak=c->history[p->history_selected].peak;p->safety=c->history[p->history_selected].safety;
            p->listen_result=1;ts_portal_wave_reset(&p->waves[1],p->result);
            if(p->rendered_region.selection) {
                p->waves[1].has_selection=1;p->waves[1].selection_first=p->rendered_region.first;
                p->waves[1].selection_last=p->rendered_region.result_last;
            }
            snprintf(p->message,sizeof(p->message),"FINAL CHAIN OUTPUT - READY TO APPLY OR SAVE TO A NEW TILE");
        } else snprintf(p->message,sizeof(p->message),"PREVIEW THE CURRENT CHAIN TO HEAR ITS FINAL OUTPUT");
        return 1;
    }
    return 0;
}
static int portal_open_factory(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,
    TsInstrument *instrument,PortalController *c,TransformController *transform,int index)
{
    const TsCdpRecipe *f=index>=0?ts_cdp_factory_recipe_at((size_t)index):NULL;
    if(!f || ui_dialog_open(ui))return 0;
    if(ui->canvas_gesture.active || ui->amplitude_gesture.active || ui->material_macro_gesture.active ||
       ui->stretch_gesture.active || ui->warp_gesture.active || ui->smear_gesture.active || ui->tear_gesture.active) {
        snprintf(ui->status,sizeof(ui->status),"FINISH THE CURRENT EDIT GESTURE BEFORE OPENING CDP PORTAL");return 0;
    }
    if(ui->capture_state!=TS_CAPTURE_IDLE || ui->external_record_bank || ui->sister_capture_active || c->worker || transform->worker) {
        snprintf(ui->status,sizeof(ui->status),"FINISH CAPTURE / RENDER BEFORE EDITING A FACTORY INSTRUMENT");return 0;
    }
    TsCdpRecipeValues values=ui->cdp_presets[index];
    values.tuning_hz=(float)ts_tuning_frequency(&instrument->audible_tuning);
    TsPortalRecipe recipe;ts_portal_factory_recipe(f,&values,&recipe);
    TsPortalUi *p=&ui->portal;
    stop_all_force(device,audio,ui);portal_load_recipe(p,&recipe);p->open=1;
    p->tab=0;p->family=TS_PORTAL_FACTORY+1;p->query[0]=0;p->channel_filter=TS_PORTAL_CHANNEL_ANY;p->scroll=index>=TS_PORTAL_LIST_ROWS?index-TS_PORTAL_LIST_ROWS+1:0;
    p->selected_tab=0;p->selected_slot=(int)ts_portal_process_count()+index;
    p->parameter_scroll=0;p->macro_view=0;p->exact_pin=0;
    c->selection_scope=instrument->has_selection;SDL_StartTextInput();
    int ok=portal_source(device,audio,ui,instrument,c);
    if(ok)snprintf(p->message,sizeof(p->message),"%s FACTORY SETTINGS - PREVIEW / SAVE BANK / SAVE AS / CHAIN",f->display_name);
    return ok;
}
static void portal_save_factory(TsUiState *ui)
{
    TsPortalUi *p=&ui->portal;const TsCdpRecipe *f=ts_portal_factory_find(p->recipe.process_id);
    if(p->chain_active || !f || !ts_portal_recipe_validate(&p->recipe,p->message,sizeof(p->message)))return;
    TsCdpRecipeValues values;ts_portal_factory_values(&p->recipe,&values);
    (void)save_cdp_preset_values(ui,ts_cdp_recipe_index_for_id(f->id),&values,p->message,sizeof(p->message));
}
/* Mouse and arrow selection share invalidation, chain insertion and note cleanup. */
static void portal_select_row(SDL_AudioDeviceID device,AudioState *audio,TsPortalUi *p,PortalController *c,int row)
{
    TsPortalRecipe r;int slot=ts_portal_filter_slot(p,row,&r);if(slot>=0) {
        if(p->chain_active && p->chain_add && r.stage_count) {
            snprintf(p->message,sizeof(p->message),"PICK A SINGLE PROCESS FOR THIS STAGE; CANCEL ADD TO LOAD A SAVED CHAIN");return;
        }
        portal_invalidate(device,audio,p,c);
        if(p->chain_active && !r.stage_count && (p->tab==0 || p->chain_add)) {
            (void)portal_working(p);
            if(p->chain_add && p->chain.stage_count<TS_PORTAL_CHAIN_STAGES) {
                unsigned at=(unsigned)++p->chain_stage;
                for(unsigned n=p->chain.stage_count;n>at;--n)p->chain.stages[n]=p->chain.stages[n-1];
                ++p->chain.stage_count;memset(&p->chain.stages[at],0,sizeof(TsPortalStep));
            }
            p->recipe=r;ts_portal_step_set(&p->chain.stages[p->chain_stage],&r);p->chain_add=0;
        } else portal_load_recipe(p,&r);
        p->parameter_scroll=0;
        p->selected_tab=p->tab;p->selected_slot=slot;if(p->tab==2)p->pin_slot=slot;
        p->macro_view=p->tab==2 || r.stage_count;p->exact_pin=!portal_has_macros(&r);
        if(r.description[0])snprintf(p->message,sizeof(p->message),"%s",r.description);
        else snprintf(p->message,sizeof(p->message),"%.60s - ENTER TO PREVIEW; FULL SHOWS ALL PARAMETERS",r.name);
    }
}
static void portal_library_move(SDL_AudioDeviceID device,AudioState *audio,TsPortalUi *p,PortalController *c,int direction,int from_search)
{
    TsPortalRecipe r;int count=0,current=-1,slot;
    while((slot=ts_portal_filter_slot(p,count,&r))>=0) {
        if(!from_search && p->selected_tab==p->tab && p->selected_slot==slot)current=count;
        ++count;
    }
    if(!count)return;
    int next=current<0?(direction>0?p->scroll:p->scroll+TS_PORTAL_LIST_ROWS-1):current+direction;
    if(next<0)next=0;
    if(next>=count)next=count-1;
    if(next!=current)portal_select_row(device,audio,p,c,next);
    if(next<p->scroll)p->scroll=next;
    if(next>=p->scroll+TS_PORTAL_LIST_ROWS)p->scroll=next-TS_PORTAL_LIST_ROWS+1;
}
static size_t portal_frame_from_pointer(const TsPortalWave *w, SDL_Window *window, int raw_x)
{
    int width,height;SDL_GetWindowSize(window,&width,&height);if(width<=0)return w->first;
    long double t=(((long double)raw_x+0.5L)*TS_UI_WIDTH/width-154)/TS_PORTAL_WAVE_COLUMNS;
    if(t<0)t=0;if(t>1)t=1;
    return w->first+(size_t)floorl(t*(w->last-w->first)+0.5L);
}

static int portal_event(SDL_Event *event,SDL_Window *window,SDL_AudioDeviceID device,
                         AudioState *audio,TsUiState *ui,TsInstrument *instrument,
                         PortalController *c,SisterWindow *sister,int output_rate,
                         TransformController *transform)
{
    TsPortalUi *p=&ui->portal;int x=0,y=0;
    if(event->type==SDL_QUIT || (event->type==SDL_WINDOWEVENT && event->window.event==SDL_WINDOWEVENT_CLOSE)) {
        if(p->open)portal_close(device,audio,ui,c);
        return 0;
    }
    if(event_window_id(event)!=SDL_GetWindowID(window))return 0;
    if(event->type==SDL_MOUSEBUTTONDOWN || event->type==SDL_MOUSEBUTTONUP)
        logical_mouse(window,event->button.x,event->button.y,&x,&y);
    if(event->type==SDL_MOUSEMOTION)logical_mouse(window,event->motion.x,event->motion.y,&x,&y);
    int shortcut=event->type==SDL_KEYDOWN && !event->key.repeat && event->key.keysym.sym==SDLK_p &&
        (event->key.keysym.mod & KMOD_CTRL) && (event->key.keysym.mod & KMOD_SHIFT);
    int open_button=event->type==SDL_MOUSEBUTTONDOWN && event->button.button==SDL_BUTTON_LEFT &&
        ((x>=464 && x<492 && y>=4 && y<30) ||
         (ui->show_recipes && x>=114 && x<178 && y>=312 && y<328));
    if(!p->open) {
        if(event->type==SDL_MOUSEBUTTONDOWN && ui->show_recipes && !ui_dialog_open(ui)) {
            if(event->button.button==SDL_BUTTON_LEFT && x>=182 && x<230 && y>=312 && y<328) {
                ui->cdp_user_pins=!ui->cdp_user_pins;
                snprintf(ui->status,sizeof(ui->status),"%s - LEFT APPLY / MIDDLE EDIT",ui->cdp_user_pins?"USER PROCESS PINS":"FACTORY CDP INSTRUMENTS");
                return 1;
            }
            int pin_slot=ts_ui_cdp_slot_from_point(x,y);
            if(!ui->cdp_user_pins && pin_slot>=0 && event->button.button==SDL_BUTTON_MIDDLE) {
                int index=ts_cdp_catalog_index_for_slot(&ui->cdp_catalog,(size_t)ui->cdp_page,(size_t)pin_slot);
                (void)portal_open_factory(device,audio,ui,instrument,c,transform,index);return 1;
            }
            if(ui->cdp_user_pins && pin_slot>=0) {
                if(event->button.button!=SDL_BUTTON_LEFT && event->button.button!=SDL_BUTTON_MIDDLE)return 1;
                int pin=ui->cdp_page*16+pin_slot;
                TsPortalRecipe r=p->library.pins[pin];
                if(!r.process_id[0]) {snprintf(ui->status,sizeof(ui->status),"EMPTY PROCESS PIN - CREATE ONE IN CDP PORTAL");return 1;}
                if(ui->canvas_gesture.active || ui->amplitude_gesture.active ||
                   ui->material_macro_gesture.active || ui->stretch_gesture.active ||
                   ui->warp_gesture.active || ui->smear_gesture.active || ui->tear_gesture.active) {
                    snprintf(ui->status,sizeof(ui->status),"FINISH THE CURRENT EDIT GESTURE BEFORE USING A PROCESS PIN");return 1;
                }
                if(ui->capture_state!=TS_CAPTURE_IDLE || ui->external_record_bank || ui->sister_capture_active || transform->worker || c->worker) {
                    snprintf(ui->status,sizeof(ui->status),"FINISH CAPTURE / RENDER BEFORE USING A PROCESS PIN");return 1;
                }
                stop_all_force(device,audio,ui);p->open=1;portal_load_recipe(p,&r);p->macro_view=1;
                p->selected_tab=2;p->selected_slot=pin;p->pin_slot=pin;
                p->exact_pin=!portal_has_macros(&r);p->parameter_scroll=0;c->selection_scope=instrument->has_selection;
                SDL_StartTextInput();
                if(portal_source(device,audio,ui,instrument,c) && event->button.button==SDL_BUTTON_LEFT) {
                    portal_preview(device,audio,ui,c);c->quick_apply=c->worker!=NULL;
                }
                return 1;
            }
        }
        if((shortcut || open_button) && !ui_dialog_open(ui) && !ui->canvas_gesture.active &&
           !ui->amplitude_gesture.active && !ui->material_macro_gesture.active &&
           !ui->stretch_gesture.active && !ui->warp_gesture.active && !ui->smear_gesture.active && !ui->tear_gesture.active) {
            if(ui->capture_state!=TS_CAPTURE_IDLE || ui->external_record_bank || ui->sister_capture_active || transform->worker || c->worker) {
                snprintf(ui->status,sizeof(ui->status),"FINISH CAPTURE OR CURRENT TRANSFORM BEFORE OPENING CDP PORTAL");return 1;
            }
            stop_all_force(device,audio,ui);p->open=1;c->selection_scope=instrument->has_selection;
            SDL_StartTextInput();
            (void)portal_source(device,audio,ui,instrument,c);return 1;
        }
        return 0;
    }
    if(event->type==SDL_KEYDOWN || event->type==SDL_MOUSEBUTTONDOWN || event->type==SDL_MOUSEWHEEL ||
       (event->type==SDL_WINDOWEVENT && (event->window.event==SDL_WINDOWEVENT_FOCUS_LOST || event->window.event==SDL_WINDOWEVENT_LEAVE)))
        portal_hover_reset(p,c);
    /* Releases must reach held notes even after focus moves into a text field
       or the manager, and are keyed by physical note key rather than octave. */
    if(event->type==SDL_KEYUP && note_for_key(event->key.keysym.sym)>=0) {
        if(device)SDL_LockAudioDevice(device);
        ts_note_bank_release(&audio->notes,note_for_key(event->key.keysym.sym));
        if(device)SDL_UnlockAudioDevice(device);
        return 1;
    }
    if(event->type==SDL_WINDOWEVENT && event->window.event==SDL_WINDOWEVENT_FOCUS_LOST) {
        if(device)SDL_LockAudioDevice(device);
        portal_clear_notes(audio,p,c);
        if(device)SDL_UnlockAudioDevice(device);
    }
    if(p->full_action) {
        if(event->type==SDL_DROPFILE)SDL_free(event->drop.file);
        if(event->type==SDL_KEYDOWN && event->key.keysym.sym==SDLK_ESCAPE)p->full_action=0;
        if(event->type==SDL_MOUSEBUTTONDOWN && event->button.button==SDL_BUTTON_LEFT && y>=217 && y<239) {
            if(x>=192 && x<310)portal_continue_page(device,audio,ui,instrument,c,transform);
            else if(x>=330 && x<448)p->full_action=0;
        }
        return event->type==SDL_WINDOWEVENT?0:1;
    }
    if(p->macro_edit)return portal_macro_event(event,p,x,y);
    if(p->manage_open)return portal_manage_event(event,window,p,c,x,y);
    if(event->type==SDL_DROPFILE) {SDL_free(event->drop.file);snprintf(p->message,sizeof(p->message),"LOAD AUDIO ON MAIN; THEN REOPEN OR RELOAD THE PORTAL");return 1;}
    if(event->type==SDL_TEXTINPUT) {
        char *dst=p->search_focus?p->query:p->name_focus?portal_recipe_name(p):p->number_focus>=0?p->number_text:NULL;
        size_t size=p->search_focus?sizeof(p->query):p->name_focus?sizeof(p->recipe.name):sizeof(p->number_text);
        if(dst)for(const char *s=event->text.text;*s;++s)if((unsigned char)*s>=32 && (unsigned char)*s<127 && *s!='|' && strlen(dst)+1<size) {
            size_t n=strlen(dst);dst[n]=*s;dst[n+1]=0;
        }
        if(p->search_focus)p->scroll=0;
        return 1;
    }
    if(event->type==SDL_KEYDOWN) {
        SDL_Keycode key=event->key.keysym.sym;
        if(!p->name_focus && p->number_focus<0 && !(event->key.keysym.mod&(KMOD_CTRL|KMOD_ALT|KMOD_GUI)) &&
           (key==SDLK_UP || key==SDLK_DOWN || (!p->search_focus && (key==SDLK_LEFT || key==SDLK_RIGHT)))) {
            if(key==SDLK_UP || key==SDLK_DOWN) {
                int from_search=p->search_focus;p->search_focus=0;
                portal_library_move(device,audio,p,c,key==SDLK_DOWN?1:-1,from_search);
            } else {
                p->family=(p->family+(key==SDLK_RIGHT?1:TS_PORTAL_FAMILIES))%(TS_PORTAL_FAMILIES+1);
                p->scroll=0;
            }
            return 1;
        }
        if(p->search_focus || p->name_focus || p->number_focus>=0) {
            char *dst=p->search_focus?p->query:p->name_focus?portal_recipe_name(p):p->number_text;
            if(key==SDLK_a && (event->key.keysym.mod&KMOD_CTRL))dst[0]=0;
            if(key==SDLK_BACKSPACE && strlen(dst))dst[strlen(dst)-1]=0;
            if(key==SDLK_RETURN || key==SDLK_ESCAPE) {
                if(key==SDLK_RETURN && p->number_focus>=0) {
                    const TsPortalProcess *proc=ts_portal_process_find(p->recipe.process_id);char *end;
                    int i=p->number_focus;double v=strtod(dst,&end);
                    const TsPortalParam *s=&proc->parameters[i];
                    double lo,hi;ts_portal_control_bounds(&p->recipe,(unsigned)i,p->macro_view,&lo,&hi);
                    const TsCdpRecipe *factory=ts_portal_factory_find(proc->id);
                    if(factory && i<(int)factory->control_count)v=(double)(float)v;
                    if(s->type==TS_PORTAL_ENUMERATED && ts_portal_parameter_quantize(proc,(unsigned)i,v)!=v) {
                        snprintf(p->message,sizeof(p->message),"CHOOSE A NAMED MODE; WHEEL THE VALUE TO CYCLE THE AVAILABLE CHOICES");return 1;
                    }
                    if(end==dst || *end || !isfinite(v) || v<lo || v>hi || (s->type!=TS_PORTAL_REAL && floor(v)!=v) || (s->type==TS_PORTAL_ODD_INTEGER && fmod(v,2)!=1)) {
                        snprintf(p->message,sizeof(p->message),"VALUE MUST BE %s %.6g..%.6g",s->type==TS_PORTAL_REAL?"A NUMBER":s->type==TS_PORTAL_ODD_INTEGER?"AN ODD INTEGER":"AN INTEGER",lo,hi);return 1;
                    }
                    v=ts_portal_parameter_quantize(proc,(unsigned)i,v);
                    if(p->recipe.values[i]!=v) {portal_invalidate(device,audio,p,c);p->recipe.values[i]=v;}
                }
                p->search_focus=p->name_focus=0;p->number_focus=-1;
            }
            if(p->search_focus)p->scroll=0;
            return 1;
        }
        if(event->key.repeat)return 1;
        if(key==SDLK_ESCAPE || shortcut)portal_close(device,audio,ui,c);
        else if(key==SDLK_SPACE)portal_play(device,audio,ui,c,output_rate,1);
        else if(key==SDLK_RETURN)portal_preview(device,audio,ui,c);
        else if(key==SDLK_TAB)portal_ab(device,audio,ui,c,output_rate,!p->listen_result);
        else if(!(event->key.keysym.mod&(KMOD_CTRL|KMOD_ALT|KMOD_GUI))) {
            if(key>=SDLK_F1 && key<=SDLK_F8) {
                int octave=ts_ui_keyboard_set_octave(ui,(int)(key-SDLK_F1));
                snprintf(p->message,sizeof(p->message),"QWERTY OCTAVE %d - C4 IS ORIGINAL PITCH; HELD NOTES KEEP THEIR PITCH",octave);
            } else if(note_for_key(key)>=0)portal_note_on(device,audio,ui,note_for_key(key),output_rate);
        }
        return 1;
    }
    if(event->type==SDL_KEYUP)return 1;
    if(event->type==SDL_MOUSEMOTION) {
        if(ui->master_output_dragging)set_master_output_gain(device,audio,ui,sister,ts_ui_master_output_normalized_from_x(x));
        else if(p->dragging_parameter>=0)portal_parameter_drag(device,audio,p,c,p->dragging_parameter,x);
        else if(p->dragging_wave>=0 && (p->wave_dragged || abs(x-p->drag_x)>=2)) {
            p->wave_dragged=1;
            TsPortalWave *w=&p->waves[p->dragging_wave];size_t end=portal_frame_from_pointer(w,window,event->motion.x);
            w->selection_first=end<p->drag_anchor?end:p->drag_anchor;
            w->selection_last=end>p->drag_anchor?end:p->drag_anchor;
            w->has_selection=w->selection_last>w->selection_first;
            w->playhead=w->selection_first;
            (void)portal_sync_loop(device,audio,p,p->dragging_wave,0);
            if(p->dragging_wave==0)portal_selection_changed(p,c);
        }
        return 1;
    }
    if(event->type==SDL_MOUSEBUTTONUP) {
        if(event->button.button==SDL_BUTTON_LEFT && p->dragging_wave>=0 && !p->wave_dragged) {
            p->waves[p->dragging_wave].playhead=p->drag_anchor;
            (void)portal_sync_loop(device,audio,p,p->dragging_wave,1);
        }
        p->dragging_parameter=-1;p->dragging_wave=-1;ui->master_output_dragging=0;return 1;
    }
    if(event->type==SDL_WINDOWEVENT && event->window.event==SDL_WINDOWEVENT_FOCUS_LOST) {
        p->dragging_parameter=-1;p->dragging_wave=-1;ui->master_output_dragging=0;return 0;
    }
    if(event->type==SDL_MOUSEWHEEL) {
        int rx,ry;SDL_GetMouseState(&rx,&ry);logical_mouse(window,rx,ry,&x,&y);
        int d=event->wheel.direction==SDL_MOUSEWHEEL_FLIPPED?-event->wheel.y:event->wheel.y;
        if(!d)return 1;
        if(x<144) {
            TsPortalRecipe row;int count=0;
            while(ts_portal_filter(p,count,&row))++count;
            int last=count>TS_PORTAL_LIST_ROWS?count-TS_PORTAL_LIST_ROWS:0;
            p->scroll-=d;if(p->scroll<0)p->scroll=0;if(p->scroll>last)p->scroll=last;
        }
        else if(x>=154 && x<464 && ((y>=60 && y<122) || (y>=152 && y<214))) {
            int which=y>=152;const TsSample *s=which?p->result:p->source;
            if(SDL_GetModState()&KMOD_ALT) {portal_resize_selection(device,audio,ui,which,x,d);if(!which)portal_selection_changed(p,c);}
            else if(SDL_GetModState()&KMOD_SHIFT)ts_portal_wave_pan(&p->waves[which],s,d>0?-1:1);
            else ts_portal_wave_zoom(&p->waves[which],s,x-154,d);
        } else if(x>=148 && x<466 && y>=245 && y<327) {
            int i=(y>=265 && y<322 && x>=253)?portal_parameter_at(p,(y-265)/19):-1;
            const TsPortalProcess *proc=ts_portal_process_find(p->recipe.process_id);
            if(y>=265 && i>=0 && x>=253) {
                double lo,hi;ts_portal_control_bounds(&p->recipe,(unsigned)i,p->macro_view,&lo,&hi);
                double v=ts_portal_parameter_nudge(proc,(unsigned)i,p->recipe.values[i],d);
                v=fmax(lo,fmin(hi,v));
                if(p->recipe.values[i]!=v) {portal_invalidate(device,audio,p,c);p->recipe.values[i]=v;}
            } else {
                int count=ts_portal_control_count(p);
                int last=count>3?count-3:0;
                p->parameter_scroll-=d;if(p->parameter_scroll<0)p->parameter_scroll=0;
                if(p->parameter_scroll>last)p->parameter_scroll=last;
            }
        }
        return 1;
    }
    if(event->type!=SDL_MOUSEBUTTONDOWN)return 0;
    if(event->button.button==SDL_BUTTON_RIGHT) {
        if(x>=154 && x<253 && y>=265 && y<322) {
            int i=portal_parameter_at(p,(y-265)/19);if(i>=0)portal_macro_open(p,i);return 1;
        }
        if(x>=154 && x<464 && ((y>=60 && y<122) || (y>=152 && y<214))) {
            int which=y>=152;
            p->waves[which].has_selection=0;p->waves[which].playhead=0;
            if(!portal_sync_loop(device,audio,p,which,0))portal_stop(device,audio,p,c);
            if(!which)portal_selection_changed(p,c);
        }
        return 1;
    }
    if(event->button.button!=SDL_BUTTON_LEFT)return 1;
    if(x>=478 && x<544 && y>=308 && y<325) {
        p->auto_preview=!p->auto_preview;portal_auto_schedule(p,c);
        snprintf(p->message,sizeof(p->message),"AUTO PREVIEW %s - WAITS 500 MS AFTER EDITS; PREVIEW BUTTON CAN CANCEL",p->auto_preview?"ON":"OFF");
        return 1;
    }
    p->search_focus=p->name_focus=0;p->number_focus=-1;
    if(portal_chain_click(device,audio,p,c,x,y))return 1;
    if(y<32) {
        if(ts_ui_master_limiter_contains(x,y))set_output_limiter_enabled(device,audio,ui,sister,!audio->sister.limiter.enabled);
        else if(ts_ui_master_output_contains(x,y)) {ui->master_output_dragging=1;set_master_output_gain(device,audio,ui,sister,ts_ui_master_output_normalized_from_x(x));}
        else if(x>=165 && x<219)portal_close(device,audio,ui,c);
        else if(x<155 || (x>=224 && x<290)) {if(sister_window_ensure(sister,&ui->config))sister_window_show(sister);}
        else if(x>=295 && x<460) {if(x>=366)c->selection_scope=!c->selection_scope;(void)portal_source(device,audio,ui,instrument,c);}
        return 1;
    }
    if(x>=14 && x<136 && y>=62 && y<78) {p->search_focus=1;p->query[0]=0;p->scroll=0;}
    else if(x>=14 && x<136 && y>=83 && y<99) {p->tab=x<54?0:x<96?1:2;p->scroll=0;}
    else if(x>=14 && x<136 && y>=105 && y<121) {p->family=(p->family+1)%(TS_PORTAL_FAMILIES+1);p->scroll=0;}
    else if(x>=14 && x<136 && y>=126 && y<142) {p->channel_filter=(p->channel_filter+1)%TS_PORTAL_CHANNEL_FILTERS;p->scroll=0;}
    else if(x>=14 && x<136 && y>=334 && y<350) {if(x<82)portal_manage_open(p);else portal_save(p,c,0);}
    else if(x>=14 && x<136 && y>=TS_PORTAL_LIST_Y && y<TS_PORTAL_LIST_Y+TS_PORTAL_LIST_ROWS*TS_PORTAL_LIST_ROW_H) {
        portal_select_row(device,audio,p,c,p->scroll+(y-TS_PORTAL_LIST_Y)/TS_PORTAL_LIST_ROW_H);
    } else if(x>=154 && x<464 && ((y>=60 && y<122) || (y>=152 && y<214))) {
        int which=y>=152;
        int live=portal_sync_loop(device,audio,p,which,0);
        if(!live)portal_stop(device,audio,p,c);
        p->listen_result=which;p->dragging_wave=which;p->drag_x=x;p->wave_dragged=0;
        p->drag_anchor=portal_frame_from_pointer(&p->waves[which],window,event->button.x);
        if(!live)p->waves[which].playhead=p->drag_anchor;
    } else if(y>=223 && y<239 && x>=150 && x<464) {
        if(x<199)portal_play(device,audio,ui,c,output_rate,1);
        else if(x<331)portal_ab(device,audio,ui,c,output_rate,x>=269);
        else if(x<377) {
            p->loop=!p->loop;
            const TsSample *s=p->listen_result?p->result:p->source;
            if(device)SDL_LockAudioDevice(device);
            int transport=s && audio->playing && audio->sample==s;
            for(int i=0;i<TS_NOTE_BANK_VOICE_CAPACITY;++i) {
                TsNoteVoice *v=&audio->notes.voices[i];
                if(s && v->active && v->preview && v->sample==s)portal_note_range(v,&p->waves[p->listen_result],s,p->loop,0);
            }
            if(device)SDL_UnlockAudioDevice(device);
            if(transport)portal_play(device,audio,ui,c,output_rate,0);
        }
        else if(x<421)for(int i=0;i<2;++i) {TsPortalWave *w=&p->waves[i];const TsSample *s=i?p->result:p->source;w->first=0;w->last=s?s->frames:0;ts_portal_wave_refresh(w,s);}
        else portal_ab(device,audio,ui,c,output_rate,!p->listen_result);
    } else if(y>=247 && y<263 && x>=310 && x<434) {
        portal_invalidate(device,audio,p,c);p->process_selection=!p->process_selection;
        snprintf(p->message,sizeof(p->message),p->process_selection?
            "PROCESS SOURCE SELECTION - DRAG ON SOURCE, THEN PREVIEW; OUTSIDE AUDIO IS KEPT":
            "PROCESS WHOLE SOURCE - WAVEFORM SELECTIONS ARE AUDITION ONLY");
    } else if(y>=265 && y<323 && x>=253 && x<464) {
        int i=portal_parameter_at(p,(y-265)/19);
        if(i>=0) {
            if(x>=440) {p->recipe.exposed^=1u<<i;}
            else if(x>=387) {p->number_focus=i;snprintf(p->number_text,sizeof(p->number_text),"%.9g",p->recipe.values[i]);}
            else {p->dragging_parameter=i;portal_parameter_drag(device,audio,p,c,i,x);}
        }
    } else if(x>=478 && x<626 && y>=220 && y<236) {
        if(x<550) {portal_invalidate(device,audio,p,c);ts_portal_recipe_default(&p->recipe,ts_portal_process_find(p->recipe.process_id));}
        else {p->macro_view=!p->macro_view;p->parameter_scroll=0;}
    } else if(x>=478 && x<626 && y>=242 && y<258)p->name_focus=1;
    else if(x>=478 && x<626 && y>=263 && y<279)p->exact_pin=!p->exact_pin;
    else if(y>=287 && y<303 && x>=478 && x<626) {p->pin_slot=(p->pin_slot+(x<502?31:1))%32;}
    else if(y>=332 && y<355) {
        if(x>=150 && x<226)portal_preview(device,audio,ui,c);
        else if(x>=230 && x<294)portal_apply(device,audio,ui,instrument,c,0);
        else if(x>=298 && x<376)portal_apply(device,audio,ui,instrument,c,1);
        else if(x>=380 && x<464)portal_apply(device,audio,ui,instrument,c,2);
        else if(x>=478 && x<626)portal_save(p,c,1);
    } else if(y>=360 && y<377) {
        if(x>=64 && x<492)portal_select_history(device,audio,p,c,(x-64)/107);
        else if(x>=501 && x<626 && ts_portal_factory_find(p->recipe.process_id))portal_save_factory(ui);
        else if(x>=501 && x<626) {
            portal_close(device,audio,ui,c);
            snprintf(ui->status,sizeof(ui->status),"CTRL+Z UNDOES THE LAST TILE EDIT; CTRL+SHIFT+P REOPENS PORTAL");
        }
    }
    return 1;
}
static void portal_free(PortalController *c)
{
    portal_cancel(c);portal_worker_free(c->worker);c->worker=NULL;
    ts_sample_free(&c->source);
    for(int i=0;i<TS_PORTAL_CHAIN_STAGES;++i)ts_cdp_run_result_free(&c->stages[i]);
    for(int i=0;i<TS_PORTAL_HISTORY;++i)ts_cdp_run_result_free(&c->history[i]);
}
