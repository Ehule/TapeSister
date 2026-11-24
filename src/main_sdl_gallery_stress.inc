/* Mode 4: generated content coverage, followed by adversarial but supported
   transport. All planning, formatting and UI transactions are control-thread
   work. The Gallery's optional trace counts actual rendered overlap samples. */
enum { LS_FULL,LS_DENSE_CLOSED,LS_DENSE_OPEN,LS_OVERLAP,LS_INTERRUPTED,LS_SHORT,LS_RECOVERY,
       LS_MAX_JOBS=TS_GALLERY_LIMIT*2+5 };
typedef struct {
    TsGallerySequence sequence;
    int stage,movement,profile,transitions,interrupt,interact;
    double seconds,first_fade_at,fade;
} LiveStressJob;
static struct {
    LiveStressJob jobs[LS_MAX_JOBS];
    int jobs_count,current_job,playable,missing_sources,video_only,profile,failed,unsupported_movements,waiting_audio,final_valid;
    int playable_index[TS_GALLERY_LIMIT],full_done[TS_GALLERY_LIMIT];
    double dense_start[TS_GALLERY_LIMIT],dense_end[TS_GALLERY_LIMIT],density[TS_GALLERY_LIMIT];
    double saved_fade[TS_GALLERY_LIMIT],speed[TS_GALLERY_LIMIT],schedule_offset;
    int saved_curve[TS_GALLERY_LIMIT],fade_from_sequence[TS_GALLERY_LIMIT];
    unsigned trace_logged,trace_overflow,planned_transitions,observed_transitions;
    unsigned completed_transitions,interrupted_transitions,job_observed,job_completed;
    int midfade_available,pause_verified,busy_verified,stop_verified,recovery_verified;
    double paused_from,paused_to;
    uint64_t paused_frames,paused_progress,over_budget;
    TsRealtimeDiagnosticsSnapshot initial,stage_initial,final;
} live_stress;
static void live_test_profile_interval(int,double);
static const char *live_stress_name(int stage)
{
    static const char *names[]={"full_pass","dense_closed","dense_open","dense_overlap","interrupted_overlap","short_overlap","recovery"};
    return stage>=0 && stage<=LS_RECOVERY?names[stage]:"idle";
}
static double live_stress_score(const TsMosaic *m,double at)
{
    int solo=0;double score=0;
    for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(m->events[i].id && m->events[i].solo)solo=1;
    for(int i=0;i<TS_MOSAIC_EVENTS;++i) {
        const TsMosaicEvent *e=&m->events[i];
        if(!e->id || !e->source || e->source->video_path[0] || e->muted || (solo && !e->solo))continue;
        if(at>=e->start && at<e->start+e->duration)score+=e->note_count>0?e->note_count:1;
    }
    return score;
}
static int live_test_stress_scan(const TsSamplePages *pages)
{
    const TsMosaicGallery *g=pages->gallery;
    if(!isfinite(g->fade_seconds) || g->fade_seconds<0 || g->fade_seconds>30)return 0;
    for(int p=0;p<g->count;++p) {
        const TsMosaic *m=g->canvas[p].scene;double end=live_test.movement_end[p];
        if(!isfinite(m->speed) || m->speed<.5 || m->speed>2 || end>86400)return 0;
        live_stress.speed[p]=m->speed;live_stress.saved_fade[p]=g->fade_seconds;
        for(int s=0;s<g->sequence.length && s<TS_GALLERY_SEQUENCE_STEPS;++s)
            if(g->sequence.step[s].canvas_id==g->canvas[p].id) {
                const TsGalleryStep *v=&g->sequence.step[s];
                if(!isfinite(v->fade_seconds) || v->fade_seconds<0 || v->fade_seconds>3600 ||
                   v->curve<0 || v->curve>=TS_GALLERY_FADE_CURVES)return 0;
                live_stress.saved_fade[p]=v->fade_seconds;live_stress.saved_curve[p]=v->curve;
                live_stress.fade_from_sequence[p]=1;break;
            }
        double point=0,best=-1;int audio=0,video=0;
        for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(m->events[i].id) {
            const TsMosaicEvent *e=&m->events[i];
            if(!isfinite(e->start) || !isfinite(e->duration) || e->start<0 || e->duration<0 ||
               !isfinite(e->start+e->duration))return 0;
            if(!e->source)++live_stress.missing_sources;
            else if(e->source->video_path[0])++video;else ++audio;
            /* Concurrency only changes at an event boundary. This score is an
               explicit structural heuristic, never a measured CPU maximum. */
            double at=fmin(end,e->start+fmin(e->duration*.5,.000001));
            double score=live_stress_score(m,at);
            if(score>best){best=score;point=at;}
        }
        for(int i=0;i<TS_MOSAIC_MARKERS;++i)if(m->markers[i].stored &&
            isfinite(m->markers[i].time) && m->markers[i].time>=0 && m->markers[i].time<end) {
            double score=live_stress_score(m,m->markers[i].time);
            if(score>best){best=score;point=m->markers[i].time;}
        }
        live_stress.density[p]=fmax(0,best);
        if(video && !audio)++live_stress.video_only;
        if(end>=.001) {
            live_stress.playable_index[live_stress.playable++]=p;
            double span=fmin(end,12*m->speed),start=fmax(0,point-2*m->speed);
            if(start+span>end)start=fmax(0,end-span);
            live_stress.dense_start[p]=start;live_stress.dense_end[p]=fmin(end,start+span);
        } else if(end>0)++live_stress.unsupported_movements;
    }
    return 1;
}
static LiveStressJob *live_stress_job(int stage,int movement)
{
    if(live_stress.jobs_count>=LS_MAX_JOBS){live_test.plan_failed=1;return NULL;}
    LiveStressJob *j=&live_stress.jobs[live_stress.jobs_count++];memset(j,0,sizeof(*j));
    j->stage=stage;j->movement=movement;j->profile=stage!=LS_FULL;
    return j;
}
static void live_stress_step(TsGallerySequence *s,int p,double in,double out,double fade,int repeats,int curve)
{
    if(s->length>=TS_GALLERY_SEQUENCE_STEPS){live_test.plan_failed=1;return;}
    s->step[s->length++]=(TsGalleryStep){.canvas_id=(uint64_t)p+1,
        .in_marker=TS_GALLERY_POINT_TIME,.out_marker=TS_GALLERY_POINT_TIME,
        .in_seconds=in,.out_seconds=out,.fade_seconds=fade,.repeats=repeats,.curve=curve};
    /* p+1 is a temporary index token; resolve to the immutable Gallery identity
       when publishing. It never enters the renderer as an assumed canvas ID. */
    s->first=0;s->last=s->length-1;
}
static void live_stress_measure(LiveStressJob *j)
{
    double total=0;
    for(int i=0;i<j->sequence.length;++i) {
        const TsGalleryStep *s=&j->sequence.step[i];int p=(int)s->canvas_id-1;
        double d=(s->out_seconds-s->in_seconds)/live_stress.speed[p];
        total+=d*s->repeats;
        if(i+1<j->sequence.length) {
            const TsGalleryStep *to=&j->sequence.step[i+1];int q=(int)to->canvas_id-1;
            double fade=fmin(s->fade_seconds,fmin(d,(to->out_seconds-to->in_seconds)/live_stress.speed[q])*.5);
            if(i==0){j->first_fade_at=total-fade;j->fade=fade;}
            total-=fade;if(fade>0)++j->transitions;
        }
    }
    j->seconds=total;
}
static void live_stress_add(uint64_t ms,int action,int target,float value)
{
    if(ms>UINT32_MAX){live_test.plan_failed=1;return;}
    live_test_add((unsigned)ms,action,target,value);
}
static void live_test_stress_plan(void)
{
    live_test.count=0;live_test_add(0,LT_CHECK,0,0);live_test_add(100,LT_BACKSPACE,0,0);
    live_test_add(8000,LT_ASSERT,2,0);uint64_t t=8100;
    for(int p=0;p<live_test.movements;++p){live_stress_add(t,LT_INVENTORY,p,0);t+=100;}
    if(live_stress.playable) {
        LiveStressJob *full=live_stress_job(LS_FULL,-1);
        if(!full)return;
        for(int i=0;i<live_stress.playable;++i) {
            int p=live_stress.playable_index[i];double fade=live_stress.saved_fade[p]>0?live_stress.saved_fade[p]:2;
            live_stress_step(&full->sequence,p,0,live_test.movement_end[p],fade,1,live_stress.saved_curve[p]);
        }
        int first=live_stress.playable_index[0],last=live_stress.playable_index[live_stress.playable-1];
        double last_fade=live_stress.saved_fade[last]>0?live_stress.saved_fade[last]:2;
        double wrap=fmin(live_test.movement_end[first],fmax(4,last_fade*2)*live_stress.speed[first]);
        live_stress_step(&full->sequence,first,0,wrap,0,1,0);live_stress_measure(full);
        for(int n=0;n<live_stress.playable;++n)for(int open=0;open<2;++open) {
            int p=live_stress.playable_index[n];LiveStressJob *j=live_stress_job(open?LS_DENSE_OPEN:LS_DENSE_CLOSED,p);
            if(!j)return;
            double d=(live_stress.dense_end[p]-live_stress.dense_start[p])/live_stress.speed[p];
            int repeats=(int)fmin(999,ceil(12/d));
            live_stress_step(&j->sequence,p,live_stress.dense_start[p],live_stress.dense_end[p],0,repeats,0);
            live_stress_measure(j);
        }
        int heavy=first,other=first;
        for(int n=0;n<live_stress.playable;++n){int p=live_stress.playable_index[n];if(live_stress.density[p]>live_stress.density[heavy])heavy=p;}
        if(live_stress.playable>1) {
            other=live_stress.playable_index[0]==heavy?live_stress.playable_index[1]:live_stress.playable_index[0];
            for(int n=0;n<live_stress.playable;++n){int p=live_stress.playable_index[n];if(p!=heavy && live_stress.density[p]>live_stress.density[other])other=p;}
        }
        for(int interrupted=0;interrupted<2;++interrupted) {
            LiveStressJob *j=live_stress_job(interrupted?LS_INTERRUPTED:LS_OVERLAP,heavy);if(!j)return;
            live_stress_step(&j->sequence,heavy,live_stress.dense_start[heavy],live_stress.dense_end[heavy],4,1,TS_GALLERY_FADE_POWER);
            live_stress_step(&j->sequence,other,live_stress.dense_start[other],live_stress.dense_end[other],0,1,0);
            live_stress_measure(j);j->interact=j->fade>=1;
            if(interrupted && !j->interact){--live_stress.jobs_count;break;}
            j->interrupt=interrupted;live_stress.midfade_available=j->interact;
        }
        LiveStressJob *short_fade=live_stress_job(LS_SHORT,heavy);if(!short_fade)return;
        live_stress_step(&short_fade->sequence,heavy,live_stress.dense_start[heavy],live_stress.dense_end[heavy],.001,1,TS_GALLERY_FADE_SMOOTH);
        live_stress_step(&short_fade->sequence,other,live_stress.dense_start[other],live_stress.dense_end[other],0,1,0);
        live_stress_measure(short_fade);
        LiveStressJob *recovery=live_stress_job(LS_RECOVERY,heavy);if(!recovery)return;
        live_stress_step(&recovery->sequence,heavy,live_stress.dense_start[heavy],live_stress.dense_end[heavy],0,1,0);
        live_stress_measure(recovery);
    }
    for(int i=0;i<live_stress.jobs_count;++i) {
        LiveStressJob *j=&live_stress.jobs[i];
        live_stress.planned_transitions+=(unsigned)j->transitions;
        live_stress_add(t,LT_STRESS_BEGIN,i,0);
        if(j->stage==LS_OVERLAP && j->interact) {
            uint64_t middle=t+(uint64_t)ceil((j->first_fade_at+j->fade*.25)*1000);
            live_stress_add(middle,LT_STRESS_BUSY,i,0);
            live_stress_add(middle+100,LT_STRESS_PAUSE,i,1);
            live_stress_add(middle+600,LT_STRESS_PAUSED,i,0);
            live_stress_add(middle+700,LT_STRESS_PAUSE,i,0);
            j->seconds+=.6;
        }
        if(j->interrupt) {
            uint64_t middle=t+(uint64_t)ceil((j->first_fade_at+j->fade*.5)*1000);
            live_stress_add(middle,LT_STRESS_STOP,i,0);t=middle+100;
        } else {
            double milliseconds=ceil(j->seconds*1000)+100;
            if(!isfinite(milliseconds) || milliseconds>UINT32_MAX){live_test.plan_failed=1;return;}
            t+=(uint64_t)milliseconds;live_stress_add(t,LT_STRESS_WAIT,i,0);t+=100;
        }
        live_stress_add(t,LT_STRESS_END,i,0);
        t+=100;live_stress_add(t,LT_BACKSPACE,0,0);
        t+=2100;live_stress_add(t,LT_ASSERT,2,0);t+=100;
    }
    live_stress_add(t,LT_END,0,0);
    if(t>UINT32_MAX)live_test.plan_failed=1;else live_test.duration_ms=(unsigned)t;
}
static void live_test_stress_log_plan(const TsSamplePages *pages)
{
    ts_session_log_event("live_test_plan","scenario=gallery-stress-v1 testorder=generated jobs=%d playable_movements=%d missing_sources=%d video_only_movements=%d planned_transitions=%u duration_ms=%u density_metric=overlapping_note_slots full_pass_profile=0 stress_profile=1 midfade_interactions=%s trace_capacity=%d",
        live_stress.jobs_count,live_stress.playable,live_stress.missing_sources,live_stress.video_only,
        live_stress.planned_transitions,live_test.duration_ms,live_stress.midfade_available?"available":"unavailable_short_sections",TS_GALLERY_DIAGNOSTIC_TRANSITIONS);
    for(int p=0;p<live_test.movements;++p)ts_session_log_event("live_test_content_plan",
        "movement=%d id=%llu speed=%.6f end_s=%.9f dense_start_s=%.9f dense_end_s=%.9f density=%.3f saved_fade_s=%.6f full_requested_fade_s=%.6f fade_source=%s fade_override=%d coverage=%s",
        p,(unsigned long long)pages->gallery->canvas[p].id,live_stress.speed[p],live_test.movement_end[p],
        live_stress.dense_start[p],live_stress.dense_end[p],live_stress.density[p],live_stress.saved_fade[p],
        live_stress.saved_fade[p]>0?live_stress.saved_fade[p]:2,
        live_stress.fade_from_sequence[p]?"first_saved_sequence_visit":"gallery",live_stress.saved_fade[p]==0,
        live_test.movement_end[p]>=.001?"full_and_stress":"skipped_empty_or_submillisecond");
}
static void live_stress_stage_log(AudioState *a,const LiveStressJob *j,const char *state)
{
    TsRealtimeDiagnosticsSnapshot s={0};ts_realtime_diagnostics_get(&a->realtime_diagnostics,&s);
    ts_session_log_event("live_test_stage",
        "stage=%s state=%s job=%d movement=%d profile=%d callbacks=%llu frames=%llu over_budget=%llu near_budget=%llu start_gaps=%llu planned_seconds=%.6f history=carried comparison=source_positions_only",
        live_stress_name(j->stage),state,live_stress.current_job,j->movement,j->profile,
        (unsigned long long)s.callback_count,(unsigned long long)s.frame_count,
        (unsigned long long)s.deadline_overruns,(unsigned long long)s.near_overruns,
        (unsigned long long)s.delayed_starts,j->seconds);
}
static int live_test_stress_observe(SDL_AudioDeviceID device,AudioState *a,TsSamplePages *pages)
{
    (void)a;TsMosaicGallery *g=pages->gallery;if(!g)return 0;
    /* Bound UI logging too: even a pathological burst must not turn one poll
       into a second telemetry overflow. Any undrained records remain queued. */
    TsGalleryDiagnosticTransition ready[8];unsigned count=0;
    if(device)SDL_LockAudioDevice(device);
    live_stress.trace_overflow=g->diagnostic_overflow;
    while(count<sizeof(ready)/sizeof(*ready) && live_stress.trace_logged<g->diagnostic_count && g->diagnostic[live_stress.trace_logged].retired)
        ready[count++]=g->diagnostic[live_stress.trace_logged++];
    if(device)SDL_UnlockAudioDevice(device);
    const LiveStressJob *j=live_stress.current_job>=0?&live_stress.jobs[live_stress.current_job]:NULL;
    for(unsigned i=0;i<count;++i) {
        const TsGalleryDiagnosticTransition *d=&ready[i];
        ++live_stress.observed_transitions;++live_stress.job_observed;
        if(d->completed){++live_stress.completed_transitions;++live_stress.job_completed;}
        else ++live_stress.interrupted_transitions;
        if(!d->dual_frames || !d->progress_frames)live_stress.failed=1;
        if(j && j->stage==LS_FULL && d->completed && d->from>=0 && d->from<live_test.movements &&
            d->from_end+1e-5>=live_test.movement_end[d->from])live_stress.full_done[d->from]=1;
        ts_session_log_event("live_test_transition",
            "transition=%llu job=%d stage=%s from=%d to=%d from_id=%llu to_id=%llu requested_fade_s=%.9f effective_fade_s=%.9f dual_frames=%llu progress_frames=%llu completed=%d retired_reason=%s from_start_s=%.9f from_end_s=%.9f to_start_s=%.9f to_end_s=%.9f",
            (unsigned long long)d->serial,live_stress.current_job,j?live_stress_name(j->stage):"idle",d->from,d->to,
            (unsigned long long)d->from_id,(unsigned long long)d->to_id,d->requested_fade,d->effective_fade,
            (unsigned long long)d->dual_frames,(unsigned long long)d->progress_frames,d->completed,
            d->completed?"completed":"interrupted",d->from_start,d->from_end,d->to_start,d->to_end);
    }
    TsSessionLogStatus log;ts_session_log_status(&log);
    if(log.state!=TS_SESSION_RECORDING || log.dropped_audio!=live_test.dropped_audio || log.dropped_events!=live_test.dropped_events)live_stress.failed=1;
    return !live_stress.trace_overflow && !live_stress.failed;
}
static int live_test_stress_ready(const LiveTestStep *s,SDL_AudioDeviceID device,AudioState *a,TsSamplePages *pages)
{
    (void)a;int ready=1;TsMosaicGallery *g=pages->gallery;
    if(device)SDL_LockAudioDevice(device);
    if(s->action==LT_STRESS_WAIT)ready=!g->player.running && g->outgoing<0;
    if(s->action==LT_STRESS_BUSY || s->action==LT_STRESS_STOP || (s->action==LT_STRESS_PAUSE && s->value))ready=g->outgoing>=0;
    if(device)SDL_UnlockAudioDevice(device);return ready;
}
static int live_test_stress_action(const LiveTestStep *s,SDL_Window *window,SDL_AudioDeviceID device,
    AudioState *a,TsUiState *ui,TsSamplePages *pages,TsInstrument *bank,MosaicController *mosaic)
{
    (void)window;if(s->target<0 || s->target>=live_stress.jobs_count)return 0;
    LiveStressJob *j=&live_stress.jobs[s->target];TsMosaicGallery *g=pages->gallery;
    if(s->action==LT_STRESS_BEGIN) {
        mosaic_transport(device,a,ui,1);
        if(j->stage==LS_DENSE_CLOSED || j->stage==LS_FULL)matrix_hide();
        else {if(!matrix_show(device,a,ui,pages,bank,-1,-1))return 0;fx_view(1,pages,bank);}
        live_stress.current_job=s->target;live_stress.job_observed=live_stress.job_completed=0;
        live_stress.profile=j->profile;ts_profile_enable(j->profile);if(j->profile)ts_profile_reset();
        live_test.profile_from_ms=1000.0*(SDL_GetPerformanceCounter()-live_test.started)/live_test.frequency;
        if(device)SDL_LockAudioDevice(device);
        g->sequence=j->sequence;
        for(int i=0;i<g->sequence.length;++i) {
            int p=(int)g->sequence.step[i].canvas_id-1;
            g->sequence.step[i].canvas_id=g->canvas[p].id;
        }
        g->diagnostic_count=0;g->diagnostic_active=-1;live_stress.trace_logged=0;
        if(device)SDL_UnlockAudioDevice(device);
        ts_realtime_diagnostics_get(&a->realtime_diagnostics,&live_stress.stage_initial);
        live_stress_stage_log(a,j,"begin");
        return gallery_sequence_start(device,a,ui,bank,mosaic,0);
    }
    if(s->action==LT_STRESS_WAIT) {
        if(device)SDL_LockAudioDevice(device);
        const TsGalleryStep *last=&g->sequence.step[g->sequence.length-1];
        int ok=!g->player.running && g->outgoing<0 && !g->player.error[0] &&
            ts_gallery_active(g)->time+1e-5>=last->out_seconds;
        if(device)SDL_UnlockAudioDevice(device);return ok;
    }
    if(s->action==LT_STRESS_BUSY) {
        char error[128];if(device)SDL_LockAudioDevice(device);
        int before=g->active,outgoing=g->outgoing;
        int rejected=!ts_gallery_sequence_next(g,-1,error,sizeof(error));
        int ok=rejected && outgoing>=0 && before==g->active && outgoing==g->outgoing;
        if(device)SDL_UnlockAudioDevice(device);live_stress.busy_verified=ok;return ok;
    }
    if(s->action==LT_STRESS_PAUSE) {
        if(device)SDL_LockAudioDevice(device);
        ts_gallery_sequence_pause(g,s->value!=0);
        int ok=g->player.running && g->player.paused==(s->value!=0) && g->outgoing>=0;
        ok&=g->diagnostic_active>=0 && (unsigned)g->diagnostic_active<g->diagnostic_count;
        if(s->value && ok) {
            live_stress.paused_from=ts_gallery_outgoing(g)->time;live_stress.paused_to=ts_gallery_active(g)->time;
            live_stress.paused_frames=atomic_load_explicit(&a->realtime_diagnostics.frame_count,memory_order_relaxed);
            live_stress.paused_progress=g->diagnostic[g->diagnostic_active].progress_frames;
        }
        if(device)SDL_UnlockAudioDevice(device);return ok;
    }
    if(s->action==LT_STRESS_PAUSED) {
        if(device)SDL_LockAudioDevice(device);
        uint64_t frames=atomic_load_explicit(&a->realtime_diagnostics.frame_count,memory_order_relaxed);
        int ok=g->player.paused && g->outgoing>=0 && g->diagnostic_active>=0 &&
            (unsigned)g->diagnostic_active<g->diagnostic_count && ts_gallery_outgoing(g)->time==live_stress.paused_from &&
            ts_gallery_active(g)->time==live_stress.paused_to && frames>live_stress.paused_frames &&
            g->diagnostic[g->diagnostic_active].progress_frames==live_stress.paused_progress;
        if(device)SDL_UnlockAudioDevice(device);live_stress.pause_verified=ok;return ok;
    }
    if(s->action==LT_STRESS_STOP) {
        mosaic_transport(device,a,ui,1);
        if(device)SDL_LockAudioDevice(device);
        int ok=a->engine_stopped && g->outgoing<0 && !g->player.running;
        if(device)SDL_UnlockAudioDevice(device);live_stress.stop_verified=ok;return ok;
    }
    if(s->action==LT_STRESS_END) {
        if(!live_test_stress_observe(device,a,pages))return 0;
        TsRealtimeDiagnosticsSnapshot now={0};ts_realtime_diagnostics_get(&a->realtime_diagnostics,&now);
        int ok=now.frame_count>live_stress.stage_initial.frame_count && live_stress.job_observed==(unsigned)j->transitions &&
            live_stress.job_completed==(unsigned)(j->transitions-j->interrupt);
        if(j->stage==LS_FULL)for(int n=0;n<live_stress.playable;++n)ok&=live_stress.full_done[live_stress.playable_index[n]];
        if(j->stage==LS_RECOVERY)live_stress.recovery_verified=ok;
        live_test_profile_interval(500+s->target,1000.0*(SDL_GetPerformanceCounter()-live_test.started)/live_test.frequency);
        live_stress_stage_log(a,j,"end");live_stress.profile=0;ts_profile_enable(0);
        return ok;
    }
    return 0;
}
static int live_test_stress_coverage(SDL_AudioDeviceID device,AudioState *a,int requested_complete)
{
    int full=0;for(int p=0;p<live_test.movements;++p)full+=live_stress.full_done[p]!=0;
    TsRealtimeDiagnosticsSnapshot s=live_stress.final;
    if(!live_stress.final_valid)ts_realtime_diagnostics_get(&a->realtime_diagnostics,&s);
    live_stress.over_budget=s.deadline_overruns-live_stress.initial.deadline_overruns;
    int complete=requested_complete && live_stress.playable && !live_stress.unsupported_movements &&
        !live_stress.failed && !live_stress.trace_overflow && !live_stress.missing_sources &&
        full==live_stress.playable && live_stress.observed_transitions==live_stress.planned_transitions &&
        (!live_stress.playable || live_stress.recovery_verified) &&
        (!live_stress.midfade_available || (live_stress.pause_verified && live_stress.busy_verified && live_stress.stop_verified));
    if(device)SDL_LockAudioDevice(device);
    if(a->gallery)a->gallery->diagnostic_enabled=0;
    if(device)SDL_UnlockAudioDevice(device);
    ts_session_log_event("live_test_coverage",
        "planned_transitions=%u observed_transitions=%u completed_transitions=%u interrupted_transitions=%u full_movements_expected=%d full_movements_completed=%d playable_movements=%d unsupported_movements=%d workload=%s missing_sources=%d trace_overflow=%u coverage_complete=%d midfade_available=%d pause_verified=%d busy_verified=%d stop_verified=%d recovery_verified=%d callbacks=%llu over_budget=%llu near_budget=%llu start_gaps=%llu",
        live_stress.planned_transitions,live_stress.observed_transitions,live_stress.completed_transitions,live_stress.interrupted_transitions,
        live_stress.playable,full,live_stress.playable,live_stress.unsupported_movements,
        live_stress.playable?"content":"no_playback_workload",live_stress.missing_sources,live_stress.trace_overflow,complete,
        live_stress.midfade_available,live_stress.pause_verified,live_stress.busy_verified,live_stress.stop_verified,live_stress.recovery_verified,
        (unsigned long long)(s.callback_count-live_stress.initial.callback_count),(unsigned long long)live_stress.over_budget,
        (unsigned long long)(s.near_overruns-live_stress.initial.near_overruns),
        (unsigned long long)(s.delayed_starts-live_stress.initial.delayed_starts));
    TsSessionLogStatus log;ts_session_log_status(&log);
    if(log.state!=TS_SESSION_RECORDING || log.dropped_audio!=live_test.dropped_audio || log.dropped_events!=live_test.dropped_events)complete=0;
    return complete;
}
