/* Mode 4: generated content coverage, followed by adversarial but supported
   transport. All planning, formatting and UI transactions are control-thread
   work. The Gallery's optional trace counts actual rendered overlap samples. */
enum { LS_FULL,LS_DENSE_CLOSED,LS_DENSE_OPEN,LS_OVERLAP,LS_INTERRUPTED,LS_SHORT,LS_RECOVERY,LS_ALIGNED,
       LS_MAX_JOBS=TS_GALLERY_LIMIT*2+6 };
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
    double density_start[TS_GALLERY_LIMIT],density_end[TS_GALLERY_LIMIT],density_point[TS_GALLERY_LIMIT];
    int alignment_available,alignment_verified,alignment_job,alignment_from,alignment_to;
    const char *alignment_reason,*alignment_method;
    double alignment_from_point,alignment_to_point,alignment_phase;
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
    static const char *names[]={"full_pass","dense_closed","dense_open","dense_overlap","interrupted_overlap","short_overlap","recovery","aligned_overlap"};
    return stage>=0 && stage<=LS_ALIGNED?names[stage]:"idle";
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
static int live_stress_boundary_compare(const void *a,const void *b)
{
    double x=*(const double *)a,y=*(const double *)b;return (x>y)-(x<y);
}
/* Density is constant between source boundaries. Keep the earliest contiguous
   maximum plateau, including adjoining equal-score intervals, and use its
   midpoint rather than an onset epsilon. All work is on the control thread. */
static void live_stress_density(const TsMosaic *m,int p,double end)
{
    double boundary[TS_MOSAIC_EVENTS*2+2];int count=0;
    boundary[count++]=0;boundary[count++]=end;
    for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(m->events[i].id) {
        const TsMosaicEvent *e=&m->events[i];
        boundary[count++]=fmin(end,e->start);boundary[count++]=fmin(end,e->start+e->duration);
    }
    qsort(boundary,(size_t)count,sizeof(boundary[0]),live_stress_boundary_compare);
    double best=-1,start=0,stop=0;
    for(int i=1;i<count;++i) {
        double a=boundary[i-1],b=boundary[i];if(b<=a)continue;
        double score=live_stress_score(m,a+(b-a)*.5);
        if(score>best){best=score;start=a;stop=b;}
        else if(score==best && a==stop)stop=b;
    }
    live_stress.density[p]=fmax(0,best);
    live_stress.density_start[p]=start;live_stress.density_end[p]=stop;
    live_stress.density_point[p]=start+(stop-start)*.5;
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
        int audio=0,video=0;
        for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(m->events[i].id) {
            const TsMosaicEvent *e=&m->events[i];
            if(!isfinite(e->start) || !isfinite(e->duration) || e->start<0 || e->duration<0 ||
               !isfinite(e->start+e->duration))return 0;
            if(!e->source)++live_stress.missing_sources;
            else if(e->source->video_path[0])++video;else ++audio;
        }
        live_stress_density(m,p,end);
        if(video && !audio)++live_stress.video_only;
        if(end>=.001) {
            live_stress.playable_index[live_stress.playable++]=p;
            double span=fmin(end,12*m->speed),start=fmax(0,live_stress.density_point[p]-2*m->speed);
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
static void live_stress_aligned_plan(int from,int to)
{
    double sa=live_stress.speed[from],sb=live_stress.speed[to];
    double a=live_stress.dense_start[from],b=live_stress.dense_end[from];
    double c=live_stress.dense_start[to],d=live_stress.dense_end[to];
    double fade=fmin(4,fmin((b-a)/sa,(d-c)/sb)*.5);
    double low=fmax(0,fmax((live_stress.density_start[from]-(b-fade*sa))/sa,
                          (live_stress.density_start[to]-c)/sb));
    double high=fmin(fade,fmin((live_stress.density_end[from]-(b-fade*sa))/sa,
                              (live_stress.density_end[to]-c)/sb));
    double phase=0,pa=live_stress.density_point[from],pb=live_stress.density_point[to];
    const char *method="existing_density_overlap";
    live_stress.alignment_reason="native_section_limits";
    /* A common interval is stronger than separately finding a peak on each
       side. It also keeps a flat 1ms scene eligible despite section limits. */
    if(high>low && b>=a+.001 && d>=c+.001) {
        phase=low+(high-low)*.5;pa=b-fade*sa+phase*sa;pb=c+phase*sb;
    } else {
        method="peak_midpoints";
        double qa=pa/sa,ra=(live_test.movement_end[from]-pa)/sa;
        double qb=pb/sb,rb=(live_test.movement_end[to]-pb)/sb;
        /* Each native section must last >=2F. These bounds are the intersection
           of their legal common peak phases, including both timeline edges. */
        fade=fmin(4,fmin(fmin(qa,rb),fmin(fmin((qa+ra)*.5,(qb+rb)*.5),fmin(ra+qb,(qa+rb)/3))));
        for(int attempt=0;attempt<2;++attempt) {
            low=fmax(0,fmax(fade-ra,2*fade-rb));high=fmin(fade,fmin(qb,qa-fade));
            if(!(fade>0) || low>high+1e-10)return;
            phase=fmax(low,fmin(fade*.5,high));
            if(phase>0 && phase<fade)break;
            /* Keep both peak points inside the fade, not at an entry/retire
               boundary whose endpoint can fall between rendered samples. */
            fade*=.99;
        }
        if(!(phase>0 && phase<fade))return;
        a=fmax(0,pa-(fade+phase)*sa);b=fmin(live_test.movement_end[from],pa+(fade-phase)*sa);
        c=fmax(0,pb-phase*sb);d=fmin(live_test.movement_end[to],pb+(2*fade-phase)*sb);
        if(b<a+.001 || d<c+.001)return;
    }
    LiveStressJob *j=live_stress_job(LS_ALIGNED,from);if(!j)return;
    live_stress_step(&j->sequence,from,a,b,fade,1,TS_GALLERY_FADE_POWER);
    live_stress_step(&j->sequence,to,c,d,0,1,0);live_stress_measure(j);
    double actual_phase=(pa-(b-j->fade*sa))/sa,incoming_phase=(pb-c)/sb;
    if(!(j->fade>0 && actual_phase>=-1e-10 && actual_phase<j->fade &&
         incoming_phase>=-1e-10 && incoming_phase<j->fade && fabs(actual_phase-incoming_phase)<1e-8)) {
        --live_stress.jobs_count;return;
    }
    live_stress.alignment_available=1;live_stress.alignment_job=live_stress.jobs_count-1;
    live_stress.alignment_from=from;live_stress.alignment_to=to;
    live_stress.alignment_from_point=pa;live_stress.alignment_to_point=pb;
    live_stress.alignment_phase=fmax(0,actual_phase);
    live_stress.alignment_method=method;live_stress.alignment_reason="available";
}
static void live_stress_add(uint64_t ms,int action,int target,float value)
{
    if(ms>UINT32_MAX){live_test.plan_failed=1;return;}
    live_test_add((unsigned)ms,action,target,value);
}
static void live_test_stress_plan(void)
{
    live_stress.alignment_job=-1;live_stress.alignment_reason="no_playback_workload";
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
        live_stress_aligned_plan(heavy,other);
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
    ts_session_log_event("live_test_plan","scenario=gallery-stress-v1 testorder=generated jobs=%d playable_movements=%d missing_sources=%d video_only_movements=%d planned_transitions=%u duration_ms=%u density_metric=overlapping_note_slots full_pass_profile=0 stress_profile=1 midfade_interactions=%s trace_capacity=%d alignment_available=%d alignment_reason=%s",
        live_stress.jobs_count,live_stress.playable,live_stress.missing_sources,live_stress.video_only,
        live_stress.planned_transitions,live_test.duration_ms,live_stress.midfade_available?"available":"unavailable_short_sections",TS_GALLERY_DIAGNOSTIC_TRANSITIONS,
        live_stress.alignment_available,live_stress.alignment_reason);
    for(int p=0;p<live_test.movements;++p)ts_session_log_event("live_test_content_plan",
        "movement=%d id=%llu speed=%.6f end_s=%.9f dense_start_s=%.9f dense_end_s=%.9f density=%.3f density_start_s=%.17g density_end_s=%.17g density_point_s=%.17g saved_fade_s=%.6f full_requested_fade_s=%.6f fade_source=%s fade_override=%d coverage=%s",
        p,(unsigned long long)pages->gallery->canvas[p].id,live_stress.speed[p],live_test.movement_end[p],
        live_stress.dense_start[p],live_stress.dense_end[p],live_stress.density[p],
        live_stress.density_start[p],live_stress.density_end[p],live_stress.density_point[p],live_stress.saved_fade[p],
        live_stress.saved_fade[p]>0?live_stress.saved_fade[p]:2,
        live_stress.fade_from_sequence[p]?"first_saved_sequence_visit":"gallery",live_stress.saved_fade[p]==0,
        live_test.movement_end[p]>=.001?"full_and_stress":"skipped_empty_or_submillisecond");
}
static void live_stress_alignment_log(const AudioState *a)
{
    const LiveStressJob *j=&live_stress.jobs[live_stress.alignment_job];
    int p=live_stress.alignment_from,q=live_stress.alignment_to;
    const TsGalleryStep *from=&j->sequence.step[0],*to=&j->sequence.step[1];
    ts_session_log_event("live_test_alignment",
        "job=%d method=%s from=%d to=%d from_id=%llu to_id=%llu from_point_s=%.17g to_point_s=%.17g from_density_start_s=%.17g from_density_end_s=%.17g to_density_start_s=%.17g to_density_end_s=%.17g from_in_s=%.17g from_out_s=%.17g to_in_s=%.17g to_out_s=%.17g from_speed=%.17g to_speed=%.17g fade_s=%.17g phase_s=%.17g",
        live_stress.alignment_job,live_stress.alignment_method,p,q,
        (unsigned long long)a->gallery->canvas[p].id,(unsigned long long)a->gallery->canvas[q].id,
        live_stress.alignment_from_point,live_stress.alignment_to_point,
        live_stress.density_start[p],live_stress.density_end[p],live_stress.density_start[q],live_stress.density_end[q],
        from->in_seconds,from->out_seconds,to->in_seconds,to->out_seconds,
        live_stress.speed[p],live_stress.speed[q],j->fade,live_stress.alignment_phase);
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
static int live_stress_density_frame(const TsGalleryDiagnosticTransition *d,int rate,uint64_t k)
{
    if(k>=d->progress_frames)return 0;
    int p=live_stress.alignment_from,q=live_stress.alignment_to;
    double from=d->from_start+(double)k*live_stress.speed[p]/rate;
    double to=d->to_start+(double)k*live_stress.speed[q]/rate;
    return from>=live_stress.density_start[p] && from<live_stress.density_end[p] &&
        to>=live_stress.density_start[q] && to<live_stress.density_end[q];
}
/* Aligned jobs have constant native speed and no pauses. Frame k renders at
   trace.start + k*speed/rate on each side. Count their common density interval
   on that discrete grid; a phase tolerance alone cannot prove sub-frame peaks.
   Correct ceil roundoff with bounded checks of actual source positions. Exact
   inclusive-start samples remain valid. All work stays on the UI thread. */
static uint64_t live_stress_common_density_frames(const TsGalleryDiagnosticTransition *d,int rate,uint64_t *witness)
{
    *witness=UINT64_MAX;
    if(rate<=0 || !d->progress_frames)return 0;
    int p=live_stress.alignment_from,q=live_stress.alignment_to;
    double sa=live_stress.speed[p],sb=live_stress.speed[q];
    double low=fmax((live_stress.density_start[p]-d->from_start)*rate/sa,
                    (live_stress.density_start[q]-d->to_start)*rate/sb);
    double high=fmin((live_stress.density_end[p]-d->from_start)*rate/sa,
                     (live_stress.density_end[q]-d->to_start)*rate/sb);
    uint64_t frames=d->progress_frames;
    uint64_t first=(uint64_t)fmin((double)frames,fmax(0,ceil(low)));
    uint64_t end=(uint64_t)fmin((double)frames,fmax(0,ceil(high)));
    for(int i=0;i<2 && first>0 && live_stress_density_frame(d,rate,first-1);++i)--first;
    for(int i=0;i<2 && first<frames && !live_stress_density_frame(d,rate,first);++i)++first;
    for(int i=0;i<2 && end<frames && live_stress_density_frame(d,rate,end);++i)++end;
    for(int i=0;i<2 && end>first && !live_stress_density_frame(d,rate,end-1);++i)--end;
    if(end<=first || !live_stress_density_frame(d,rate,first) || !live_stress_density_frame(d,rate,end-1))return 0;
    uint64_t k=first+(end-first)/2;
    if(!live_stress_density_frame(d,rate,k))return 0;
    *witness=k;return end-first;
}
static int live_test_stress_observe(SDL_AudioDeviceID device,AudioState *a,TsSamplePages *pages)
{
    TsMosaicGallery *g=pages->gallery;if(!g)return 0;
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
        uint64_t common_density_frames=0,density_witness_frame=UINT64_MAX;
        ++live_stress.observed_transitions;++live_stress.job_observed;
        if(d->completed){++live_stress.completed_transitions;++live_stress.job_completed;}
        else ++live_stress.interrupted_transitions;
        if(!d->dual_frames || !d->progress_frames)live_stress.failed=1;
        if(j && j->stage==LS_FULL && d->completed && d->from>=0 && d->from<live_test.movements &&
            d->from_end+1e-5>=live_test.movement_end[d->from])live_stress.full_done[d->from]=1;
        if(j && j->stage==LS_ALIGNED) {
            int p=live_stress.alignment_from,q=live_stress.alignment_to;
            double sa=live_stress.speed[p],sb=live_stress.speed[q];
            double pa=live_stress.alignment_from_point,pb=live_stress.alignment_to_point;
            double tolerance=a->output_rate>0?1.0/a->output_rate+1e-9:0;
            double phase_a=(pa-d->from_start)/sa,phase_b=(pb-d->to_start)/sb;
            common_density_frames=live_stress_common_density_frames(d,a->output_rate,&density_witness_frame);
            int ok=a->output_rate>0 && d->completed && d->from==p && d->to==q &&
                d->from_id==g->canvas[p].id && d->to_id==g->canvas[q].id &&
                d->effective_fade<=j->fade+1e-8 && d->effective_fade+tolerance>=j->fade &&
                phase_a>=-tolerance && phase_b>=-tolerance &&
                pa<=d->from_end+sa*tolerance && pb<=d->to_end+sb*tolerance &&
                fabs(phase_a-phase_b)<=tolerance && fabs(phase_a-live_stress.alignment_phase)<=tolerance && common_density_frames>0;
            live_stress.alignment_verified=ok;
            if(!ok){live_stress.failed=1;live_stress.alignment_reason=common_density_frames?"rendered_alignment_mismatch":"no_common_density_sample";}
        }
        ts_session_log_event("live_test_transition",
            "transition=%llu job=%d stage=%s from=%d to=%d from_id=%llu to_id=%llu requested_fade_s=%.17g effective_fade_s=%.17g dual_frames=%llu progress_frames=%llu completed=%d retired_reason=%s from_start_s=%.17g from_end_s=%.17g to_start_s=%.17g to_end_s=%.17g common_density_frames=%llu density_witness_frame=%lld",
            (unsigned long long)d->serial,live_stress.current_job,j?live_stress_name(j->stage):"idle",d->from,d->to,
            (unsigned long long)d->from_id,(unsigned long long)d->to_id,d->requested_fade,d->effective_fade,
            (unsigned long long)d->dual_frames,(unsigned long long)d->progress_frames,d->completed,
            d->completed?"completed":"interrupted",d->from_start,d->from_end,d->to_start,d->to_end,(unsigned long long)common_density_frames,
            density_witness_frame==UINT64_MAX?-1:(long long)density_witness_frame);
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
        if(j->stage==LS_ALIGNED)live_stress_alignment_log(a);
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
        (!live_stress.alignment_available || live_stress.alignment_verified) &&
        (!live_stress.playable || live_stress.recovery_verified) &&
        (!live_stress.midfade_available || (live_stress.pause_verified && live_stress.busy_verified && live_stress.stop_verified));
    if(device)SDL_LockAudioDevice(device);
    if(a->gallery)a->gallery->diagnostic_enabled=0;
    if(device)SDL_UnlockAudioDevice(device);
    ts_session_log_event("live_test_coverage",
        "planned_transitions=%u observed_transitions=%u completed_transitions=%u interrupted_transitions=%u full_movements_expected=%d full_movements_completed=%d playable_movements=%d unsupported_movements=%d workload=%s missing_sources=%d trace_overflow=%u coverage_complete=%d midfade_available=%d pause_verified=%d busy_verified=%d stop_verified=%d recovery_verified=%d alignment_available=%d alignment_verified=%d alignment_reason=%s callbacks=%llu over_budget=%llu near_budget=%llu start_gaps=%llu",
        live_stress.planned_transitions,live_stress.observed_transitions,live_stress.completed_transitions,live_stress.interrupted_transitions,
        live_stress.playable,full,live_stress.playable,live_stress.unsupported_movements,
        live_stress.playable?"content":"no_playback_workload",live_stress.missing_sources,live_stress.trace_overflow,complete,
        live_stress.midfade_available,live_stress.pause_verified,live_stress.busy_verified,live_stress.stop_verified,live_stress.recovery_verified,
        live_stress.alignment_available,live_stress.alignment_verified,live_stress.alignment_reason,
        (unsigned long long)(s.callback_count-live_stress.initial.callback_count),(unsigned long long)live_stress.over_budget,
        (unsigned long long)(s.near_overruns-live_stress.initial.near_overruns),
        (unsigned long long)(s.delayed_starts-live_stress.initial.delayed_starts));
    TsSessionLogStatus log;ts_session_log_status(&log);
    if(log.state!=TS_SESSION_RECORDING || log.dropped_audio!=live_test.dropped_audio || log.dropped_events!=live_test.dropped_events)complete=0;
    return complete;
}
