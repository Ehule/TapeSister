/* Application-side ownership: workers own immutable inputs/results. The UI
   owns source/history; callback pointers are detached under the audio lock
   before any of those samples are freed. No CDP work occurs in the callback. */
typedef struct {
    SDL_Thread *thread;
    SDL_atomic_t cancel, done;
    uint64_t generation;
    TsSample input;
    TsPortalRecipe recipe;
    TsCdpRuntime runtime;
    TsCdpRunResult result;
    char error[160];
} PortalWorker;
typedef struct {
    TsSample source;
    TsCdpRunResult history[TS_PORTAL_HISTORY];
    TsPortalRecipe recipes[TS_PORTAL_HISTORY];
    PortalWorker *worker;
    uint64_t generation, source_hash;
    int source_slot, source_page, selection_scope, history_count;
    int quick_apply, library_writable;
    size_t source_first, source_last;
    char library_path[TS_CDP_PATH_MAX];
} PortalController;

static int portal_owns_sample(const PortalController *c,const TsSample *sample)
{
    if(sample==&c->source)return 1;
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
static void portal_invalidate(SDL_AudioDeviceID device,AudioState *audio,
                               TsPortalUi *p,PortalController *c)
{
    portal_stop(device,audio,p,c);portal_cancel(c);
    c->quick_apply=0;
    p->valid=0;p->result=NULL;p->history_selected=-1;
    ts_portal_wave_reset(&p->waves[1],NULL);
}
static void portal_clear_history(PortalController *c,TsPortalUi *p)
{
    for(int i=0;i<TS_PORTAL_HISTORY;++i)ts_cdp_run_result_free(&c->history[i]);
    c->history_count=0;p->history_count=0;p->history_selected=-1;p->valid=0;p->result=NULL;
    ts_portal_wave_reset(&p->waves[1],NULL);
}
static int portal_source(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,
                         TsInstrument *instrument,PortalController *c)
{
    TsPortalUi *p=&ui->portal;
    TsSample next;ts_sample_init(&next);
    if(c->worker) {portal_cancel(c);snprintf(p->message,sizeof(p->message),"CANCELLING; RELOAD AFTER JOB STOPS");return 0;}
    portal_invalidate(device,audio,p,c);portal_clear_history(c,p);
    ts_sample_free(&c->source);p->source=NULL;ts_portal_wave_reset(&p->waves[0],NULL);
    const TsSample *src=&instrument->current;
    if(!src->data || src->frames<2 || src->channels!=1) {
        snprintf(p->message,sizeof(p->message),"CHOOSE A MONO TILE; CURRENT PORTAL FAMILIES REQUIRE MONO");return 0;
    }
    size_t first=0,last=src->frames;
    if(c->selection_scope && instrument->has_selection) {first=instrument->selection_first;last=instrument->selection_last;}
    if(first>=last || last>src->frames || last-first>TS_PORTAL_MAX_FRAMES) {
        snprintf(p->message,sizeof(p->message),"SELECT A SHORTER SOURCE (PORTAL LIMIT: 8 MILLION FRAMES)");return 0;
    }
    next.data=malloc((last-first)*sizeof(float));
    if(!next.data) {snprintf(p->message,sizeof(p->message),"NOT ENOUGH MEMORY FOR SOURCE SNAPSHOT");return 0;}
    memcpy(next.data,src->data+first,(last-first)*sizeof(float));
    next.frames=last-first;next.channels=1;next.sample_rate=src->sample_rate;
    snprintf(next.name,sizeof(next.name),"%s",src->name);
    c->source=next;c->source_first=first;c->source_last=last;
    c->source_hash=ts_sample_hash(src);c->source_slot=instrument->selected_slot;c->source_page=ui->sample_page;
    p->source=&c->source;
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
    ui->portal.dragging_parameter=-1;ui->portal.dragging_wave=-1;
    ui->portal.search_focus=ui->portal.name_focus=0;ui->portal.number_focus=-1;
    ui->portal.manage_open=0;
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
static int portal_worker_main(void *data)
{
    PortalWorker *w=data;TsCdpRunOptions options;ts_cdp_run_options_init(&options);
    options.job_id=w->generation;options.cancel_check=portal_worker_cancel;options.cancel_userdata=w;
    (void)ts_cdp_run_portal(&w->runtime,&w->recipe,&w->input,&options,&w->result,w->error,sizeof(w->error));
    SDL_AtomicSet(&w->done,1);return 0;
}
static void portal_worker_free(PortalWorker *w)
{
    if(!w)return;
    if(w->thread)SDL_WaitThread(w->thread,NULL);
    ts_sample_free(&w->input);ts_cdp_run_result_free(&w->result);free(w);
}
static void portal_preview(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,
                           PortalController *c)
{
    TsPortalUi *p=&ui->portal;
    if(c->worker) {portal_cancel(c);snprintf(p->message,sizeof(p->message),"CANCELLING CDP JOB");return;}
    if(!ts_portal_recipe_validate(&p->recipe,p->message,sizeof(p->message)))return;
    if(!c->source.data) {snprintf(p->message,sizeof(p->message),"LOAD A MONO SOURCE FIRST");return;}
    PortalWorker *w=calloc(1,sizeof(*w));
    if(!w) {snprintf(p->message,sizeof(p->message),"NOT ENOUGH MEMORY FOR RENDER JOB");return;}
    ts_sample_init(&w->input);ts_cdp_run_result_init(&w->result);
    char *base=SDL_GetBasePath();
    int ok=ts_cdp_runtime_discover(&w->runtime,ui->config.cdp_bin_path,base,p->message,sizeof(p->message));
    SDL_free(base);
    if(!ok || !ts_sample_clone(&w->input,&c->source,p->message,sizeof(p->message))) {portal_worker_free(w);return;}
    portal_invalidate(device,audio,p,c);
    w->recipe=p->recipe;w->generation=c->generation;
    w->thread=SDL_CreateThread(portal_worker_main,"TapeSister CDP Portal",w);
    if(!w->thread) {snprintf(p->message,sizeof(p->message),"RENDER THREAD UNAVAILABLE");portal_worker_free(w);return;}
    c->worker=w;p->busy=1;
    snprintf(p->message,sizeof(p->message),"RENDERING %.50s - CANCEL IS AVAILABLE; TILE UNCHANGED",p->recipe.name);
}
static void portal_select_history(SDL_AudioDeviceID device,AudioState *audio,
                                   TsPortalUi *p,PortalController *c,int index)
{
    if(index<0 || index>=c->history_count)return;
    portal_stop(device,audio,p,c);portal_cancel(c);
    p->recipe=c->recipes[index];p->result=&c->history[index].output;
    p->valid=1;p->history_selected=index;p->listen_result=1;
    p->safety=c->history[index].safety;p->peak=c->history[index].peak;
    p->parameter_scroll=0;p->number_focus=-1;
    p->selected_tab=p->selected_slot=-1;
    ts_portal_wave_reset(&p->waves[1],p->result);
}
static void portal_apply(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,
                         TsInstrument *instrument,PortalController *c,int new_tile);
static void portal_poll(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,
                        TsInstrument *instrument,PortalController *c)
{
    TsPortalUi *p=&ui->portal;PortalWorker *w=c->worker;
    if(w && SDL_AtomicGet(&w->done)) {
        SDL_WaitThread(w->thread,NULL);w->thread=NULL;c->worker=NULL;p->busy=0;
        if(p->open && w->generation==c->generation && !SDL_AtomicGet(&w->cancel)) {
            if(w->result.status==TS_CDP_RUN_OK) {
                portal_stop(device,audio,p,c);
                snprintf(w->result.output.name,sizeof(w->result.output.name),"%s",p->recipe.name);
                if(c->source_slot==instrument->selected_slot && c->source_page==ui->sample_page &&
                   c->source_hash==ts_sample_hash(&instrument->current) &&
                   (c->source_first>0 || c->source_last<instrument->current.frames)) {
                    ts_transform_boundary_splice(&w->result.output,&instrument->current,
                                                  c->source_first,c->source_last);
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
                    for(int i=1;i<c->history_count;++i) {c->history[i-1]=c->history[i];c->recipes[i-1]=c->recipes[i];}
                    --c->history_count;ts_cdp_run_result_init(&c->history[c->history_count]);
                }
                int slot=c->history_count++;
                c->history[slot]=w->result;ts_cdp_run_result_init(&w->result);c->recipes[slot]=p->recipe;
                p->history_count=c->history_count;
                for(int i=0;i<c->history_count;++i)snprintf(p->history_names[i],sizeof(p->history_names[i]),"%.23s",c->recipes[i].name);
                int selected_tab=p->selected_tab,selected_slot=p->selected_slot;
                portal_select_history(device,audio,p,c,slot);
                p->selected_tab=selected_tab;p->selected_slot=selected_slot;
                snprintf(p->message,sizeof(p->message),"PREVIEW READY (%s) - A/B TO COMPARE; APPLY CHANGES THE TILE",ts_cdp_safety_name(p->safety));
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
    TsPortalRecipe r=p->recipe;if(pin && p->exact_pin)r.exposed=0;
    if(!ts_portal_recipe_validate(&r,p->message,sizeof(p->message)))return;
    if(pin)next.pins[index]=r;else next.recipes[index]=r;
    if(!ts_portal_library_save(&next,c->library_path,p->message,sizeof(p->message)))return;
    p->library=next;
    p->selected_tab=pin?2:1;p->selected_slot=index;
    snprintf(p->message,sizeof(p->message),pin?"PIN %02d SAVED; OPEN PINS TO REUSE ON ANOTHER TILE":"RECIPE %02d SAVED; SOURCE AUDIO IS NOT PART OF THE RECIPE",index+1);
}
static void portal_apply(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,
                         TsInstrument *instrument,PortalController *c,int new_tile)
{
    TsPortalUi *p=&ui->portal;char error[160];
    if(!p->valid || !p->result || p->busy) {snprintf(p->message,sizeof(p->message),"PREVIEW CURRENT SETTINGS BEFORE APPLYING");return;}
    if(ui->capture_state!=TS_CAPTURE_IDLE || ui->external_record_bank || ui->sister_capture_active) {
        snprintf(p->message,sizeof(p->message),"FINISH CAPTURE BEFORE CHANGING A TILE");return;
    }
    if(!new_tile && (c->source_slot!=instrument->selected_slot || c->source_page!=ui->sample_page ||
        c->source_hash!=ts_sample_hash(&instrument->current))) {
        snprintf(p->message,sizeof(p->message),"SOURCE TILE CHANGED - RELOAD; OLD PREVIEW CANNOT REPLACE IT");return;
    }
    int destination=instrument->selected_slot;
    if(new_tile) {
        for(destination=0;destination<TS_BANK_SLOT_COUNT && instrument->bank[destination].occupied;++destination);
        if(destination>=TS_BANK_SLOT_COUNT) {snprintf(p->message,sizeof(p->message),"BANK FULL; NO TILE OVERWRITTEN");return;}
    }
    /* Allocate before editing so a failed snapshot allocation leaves both the
       tile and its auditionable preview intact. The promoted source is exactly
       the replacement, including when only a main-canvas region was rendered. */
    TsSample promoted;ts_sample_init(&promoted);
    if(!new_tile && !ts_sample_clone(&promoted,p->result,error,sizeof(error))) {
        snprintf(p->message,sizeof(p->message),"APPLY FAILED: %.144s",error);return;
    }
    portal_stop(device,audio,p,c);
    lock_edit(device,audio);
    ts_sister_runtime_prepare_slot_replacement(&audio->sister,destination);
    int ok;
    if(new_tile)ok=ts_instrument_copy_drone_to_new_tile(instrument,p->result,&destination,error,sizeof(error));
    else ok=ts_instrument_apply_rendered_replacement(instrument,p->result,c->source_first,c->source_last,error,sizeof(error));
    unlock_edit(device,audio,ui,instrument);
    if(ok) {
        ui->bank_view_slot=-1;p->valid=0;
        if(!new_tile) {
            portal_invalidate(device,audio,p,c);portal_clear_history(c,p);
            ts_sample_free(&c->source);c->source=promoted;ts_sample_init(&promoted);
            c->source_last=c->source_first+c->source.frames;
            c->source_hash=ts_sample_hash(&instrument->current);
            p->source=&c->source;p->listen_result=0;
            snprintf(p->source_name,sizeof(p->source_name),"TILE %02d %.40s",c->source_slot+1,instrument->current.name);
            ts_portal_wave_reset(&p->waves[0],p->source);
        }
        snprintf(p->message,sizeof(p->message),new_tile?
            "COPIED TO TILE %02d; PORTAL SOURCE UNCHANGED":
            "APPLIED TO TILE %02d; RESULT IS NEW SOURCE; READY FOR NEXT PROCESS; UNDO ON MAIN",instrument->selected_slot+1);
    } else snprintf(p->message,sizeof(p->message),"APPLY FAILED: %.144s",error);
    ts_sample_free(&promoted);
}
static int portal_parameter_at(TsPortalUi *p,int row)
{
    const TsPortalProcess *proc=ts_portal_process_find(p->recipe.process_id);int n=0;
    if(proc)for(unsigned i=0;i<proc->parameter_count;++i) {
        if(p->macro_view && !(p->recipe.exposed&(1u<<i)))continue;
        if(n++==row+p->parameter_scroll)return (int)i;
    }
    return -1;
}
static void portal_parameter_drag(SDL_AudioDeviceID device,AudioState *audio,
                                  TsPortalUi *p,PortalController *c,int index,int x)
{
    const TsPortalProcess *proc=ts_portal_process_find(p->recipe.process_id);
    if(!proc || index<0 || (unsigned)index>=proc->parameter_count)return;
    const TsPortalParam *s=&proc->parameters[index];
    double t=(x-253)/124.0;if(t<0)t=0;if(t>1)t=1;
    double v=s->minimum+t*(s->maximum-s->minimum);
    if(s->maximum>100 && s->minimum>=0)v=s->minimum+expm1(t*log1p(s->maximum-s->minimum));
    if(s->type!=TS_PORTAL_REAL)v=floor(v+0.5);
    if(s->type==TS_PORTAL_ODD_INTEGER)v=2*floor(v/2)+1;
    if(v<s->minimum)v=s->minimum;
    if(v>s->maximum)v=s->maximum;
    if(p->recipe.values[index]!=v) {portal_invalidate(device,audio,p,c);p->recipe.values[index]=v;}
    snprintf(p->message,sizeof(p->message),"%s; %.6g..%.6g; CLICK VALUE TO TYPE",s->help,s->minimum,s->maximum);
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
    p->manage_recipe=p->recipe;
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
    if(pin && p->exact_pin)r.exposed=0;
    if(!ts_portal_library_edit(&p->library,c->library_path,pin,slot,(TsPortalEdit)action,
                               &r,p->manage_name,p->message,sizeof(p->message)))return;
    if(p->selected_tab==pin+1 && p->selected_slot==slot) {
        if(action==TS_PORTAL_RENAME) {
            memset(p->recipe.name,0,sizeof(p->recipe.name));
            memset(p->manage_recipe.name,0,sizeof(p->manage_recipe.name));
            snprintf(p->recipe.name,sizeof(p->recipe.name),"%s",p->manage_name);
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
        ui->show_recipes && x>=114 && x<178 && y>=312 && y<328;
    if(!p->open) {
        if(event->type==SDL_MOUSEBUTTONDOWN && ui->show_recipes && !ui_dialog_open(ui)) {
            if(event->button.button==SDL_BUTTON_LEFT && x>=182 && x<230 && y>=312 && y<328) {
                ui->cdp_user_pins=!ui->cdp_user_pins;
                snprintf(ui->status,sizeof(ui->status),"%s - LEFT APPLY / MIDDLE EDIT",ui->cdp_user_pins?"USER PROCESS PINS":"FACTORY CDP INSTRUMENTS");
                return 1;
            }
            int pin_slot=ts_ui_cdp_slot_from_point(x,y);
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
                stop_all_force(device,audio,ui);p->open=1;p->recipe=r;p->macro_view=1;
                p->selected_tab=2;p->selected_slot=pin;p->pin_slot=pin;
                p->exact_pin=r.exposed==0;p->parameter_scroll=0;c->selection_scope=instrument->has_selection;
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
    if(p->manage_open)return portal_manage_event(event,window,p,c,x,y);
    if(event->type==SDL_DROPFILE) {SDL_free(event->drop.file);snprintf(p->message,sizeof(p->message),"LOAD AUDIO ON MAIN; THEN REOPEN OR RELOAD THE PORTAL");return 1;}
    if(event->type==SDL_TEXTINPUT) {
        char *dst=p->search_focus?p->query:p->name_focus?p->recipe.name:p->number_focus>=0?p->number_text:NULL;
        size_t size=p->search_focus?sizeof(p->query):p->name_focus?sizeof(p->recipe.name):sizeof(p->number_text);
        if(dst)for(const char *s=event->text.text;*s;++s)if((unsigned char)*s>=32 && (unsigned char)*s<127 && *s!='|' && strlen(dst)+1<size) {
            size_t n=strlen(dst);dst[n]=*s;dst[n+1]=0;
        }
        if(p->search_focus)p->scroll=0;
        return 1;
    }
    if(event->type==SDL_KEYDOWN) {
        SDL_Keycode key=event->key.keysym.sym;
        if(p->search_focus || p->name_focus || p->number_focus>=0) {
            char *dst=p->search_focus?p->query:p->name_focus?p->recipe.name:p->number_text;
            if(key==SDLK_BACKSPACE && strlen(dst))dst[strlen(dst)-1]=0;
            if(key==SDLK_RETURN || key==SDLK_ESCAPE) {
                if(key==SDLK_RETURN && p->number_focus>=0) {
                    const TsPortalProcess *proc=ts_portal_process_find(p->recipe.process_id);char *end;
                    int i=p->number_focus;double v=strtod(dst,&end);
                    const TsPortalParam *s=&proc->parameters[i];
                    if(end==dst || *end || !isfinite(v) || v<s->minimum || v>s->maximum || (s->type!=TS_PORTAL_REAL && floor(v)!=v) || (s->type==TS_PORTAL_ODD_INTEGER && fmod(v,2)!=1)) {
                        snprintf(p->message,sizeof(p->message),"VALUE MUST BE %s %.6g..%.6g",s->type==TS_PORTAL_REAL?"A NUMBER":s->type==TS_PORTAL_ODD_INTEGER?"AN ODD INTEGER":"AN INTEGER",s->minimum,s->maximum);return 1;
                    }
                    portal_invalidate(device,audio,p,c);p->recipe.values[i]=v;
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
            TsPortalWave *w=&p->waves[p->dragging_wave];size_t end=ts_portal_wave_frame(w,x-154);
            w->selection_first=end<p->drag_anchor?end:p->drag_anchor;
            w->selection_last=end>p->drag_anchor?end:p->drag_anchor;
            w->has_selection=w->selection_last>w->selection_first;
            w->playhead=w->selection_first;
            (void)portal_sync_loop(device,audio,p,p->dragging_wave,0);
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
            int last=count>12?count-12:0;
            p->scroll-=d;if(p->scroll<0)p->scroll=0;if(p->scroll>last)p->scroll=last;
        }
        else if(x>=154 && x<464 && ((y>=60 && y<122) || (y>=152 && y<214))) {
            int which=y>=152;const TsSample *s=which?p->result:p->source;
            if(SDL_GetModState()&KMOD_ALT)portal_resize_selection(device,audio,ui,which,x,d);
            else if(SDL_GetModState()&KMOD_SHIFT)ts_portal_wave_pan(&p->waves[which],s,d>0?-1:1);
            else ts_portal_wave_zoom(&p->waves[which],s,x-154,d);
        } else if(x>=148 && x<466 && y>=245 && y<327) {
            int i=portal_parameter_at(p,(y-265)/19);
            const TsPortalProcess *proc=ts_portal_process_find(p->recipe.process_id);
            if(y>=265 && i>=0 && x>=253) {
                const TsPortalParam *s=&proc->parameters[i];double step=s->type==TS_PORTAL_REAL?0.001:s->type==TS_PORTAL_ODD_INTEGER?2:1;
                double v=p->recipe.values[i]+d*step;if(v<s->minimum)v=s->minimum;if(v>s->maximum)v=s->maximum;
                portal_invalidate(device,audio,p,c);p->recipe.values[i]=v;
            } else {
                int count=0;
                if(proc)for(unsigned n=0;n<proc->parameter_count;++n)
                    if(!p->macro_view || (p->recipe.exposed&(1u<<n)))++count;
                int last=count>3?count-3:0;
                p->parameter_scroll-=d;if(p->parameter_scroll<0)p->parameter_scroll=0;
                if(p->parameter_scroll>last)p->parameter_scroll=last;
            }
        }
        return 1;
    }
    if(event->type!=SDL_MOUSEBUTTONDOWN)return 0;
    if(event->button.button==SDL_BUTTON_RIGHT) {
        if(x>=154 && x<464 && ((y>=60 && y<122) || (y>=152 && y<214))) {
            int which=y>=152;
            p->waves[which].has_selection=0;p->waves[which].playhead=0;
            if(!portal_sync_loop(device,audio,p,which,0))portal_stop(device,audio,p,c);
        }
        return 1;
    }
    if(event->button.button!=SDL_BUTTON_LEFT)return 1;
    p->search_focus=p->name_focus=0;p->number_focus=-1;
    if(y<32) {
        if(ts_ui_master_limiter_contains(x,y))set_output_limiter_enabled(device,audio,ui,sister,!audio->sister.limiter.enabled);
        else if(ts_ui_master_output_contains(x,y)) {ui->master_output_dragging=1;set_master_output_gain(device,audio,ui,sister,ts_ui_master_output_normalized_from_x(x));}
        else if(x>=165 && x<219)portal_close(device,audio,ui,c);
        else if(x<155 || (x>=224 && x<290)) {if(sister_window_ensure(sister,&ui->config))sister_window_show(sister);}
        else if(x>=295 && x<460) {if(x>=366)c->selection_scope=!c->selection_scope;(void)portal_source(device,audio,ui,instrument,c);}
        return 1;
    }
    if(x>=14 && x<136 && y>=62 && y<78)p->search_focus=1;
    else if(x>=14 && x<136 && y>=83 && y<99) {p->tab=x<54?0:x<96?1:2;p->scroll=0;}
    else if(x>=14 && x<136 && y>=105 && y<121) {p->family=(p->family+1)%(TS_PORTAL_FAMILIES+1);p->scroll=0;}
    else if(x>=14 && x<136 && y>=334 && y<350)portal_manage_open(p);
    else if(x>=14 && x<136 && y>=126 && y<330) {
        TsPortalRecipe r;int slot=ts_portal_filter_slot(p,p->scroll+(y-126)/17,&r);if(slot>=0) {
            portal_invalidate(device,audio,p,c);p->recipe=r;p->parameter_scroll=0;
            p->selected_tab=p->tab;p->selected_slot=slot;if(p->tab==2)p->pin_slot=slot;
            p->macro_view=p->tab==2;p->exact_pin=r.exposed==0;
            snprintf(p->message,sizeof(p->message),"%.60s - ENTER TO PREVIEW; FULL SHOWS ALL PARAMETERS",r.name);
        }
    } else if(x>=154 && x<464 && ((y>=60 && y<122) || (y>=152 && y<214))) {
        int which=y>=152;
        int live=portal_sync_loop(device,audio,p,which,0);
        if(!live)portal_stop(device,audio,p,c);
        p->listen_result=which;p->dragging_wave=which;p->drag_x=x;p->wave_dragged=0;
        p->drag_anchor=ts_portal_wave_frame(&p->waves[which],x-154);
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
        else if(x>=380 && x<464)portal_save(p,c,0);
        else if(x>=478 && x<626)portal_save(p,c,1);
    } else if(y>=360 && y<377) {
        if(x>=64 && x<492)portal_select_history(device,audio,p,c,(x-64)/107);
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
    for(int i=0;i<TS_PORTAL_HISTORY;++i)ts_cdp_run_result_free(&c->history[i]);
}
