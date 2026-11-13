/* Opt-in controller replay for the unrendered Tree of Life compositions.
   All state and dispatch belong to the UI thread. Never run from audio.
   The INI switch enables this fixed scenario, not arbitrary project code. */
#ifndef TS_LIVE_TEST_REVISION
#define TS_LIVE_TEST_REVISION "unknown"
#endif
enum { LT_OFF, LT_ARMED, LT_PENDING, LT_RUNNING, LT_DONE, LT_ABORTED, LT_PREPARING };
enum { LT_CHECK, LT_FX, LT_MATRIX, LT_CLOSE, LT_MIX, LT_GRAIN, LT_MUTE,
       LT_SOLO, LT_BYPASS, LT_TRACKER, LT_MOSAIC, LT_HEALTH, LT_GALLERY, LT_END, LT_SPACE, LT_ESCAPE, LT_SEEK, LT_FOLLOW, LT_ASSERT };
typedef struct { unsigned ms; int action,target; float value; } LiveTestStep;
static struct {
    int state,touched,count,next,profile,mode,profile_failed,angelic;
    double keter_end,profile_from_ms,parked_time;
    unsigned duration_ms;
    Uint64 started,frequency;
    uint32_t generation;
    uint64_t card[TS_GALLERY_LIMIT],project_hash;
    float mixes[TS_MATRIX_EFFECT_CAPACITY],grain;
    LiveTestStep steps[1024];
    unsigned dropped_audio,dropped_events,export_serial;
    int exporting,phase_key;
    char project[TS_BROWSER_PATH_MAX];
} live_test;
static const char *live_test_scenario(void)
{return live_test.angelic?"angelic-noise-live-v1":live_test.mode==3?"tree04-keter-profile-v1":
    live_test.mode>=2?"tree04-keter-full-v1":"tree04-live-v1";}
static const char *live_test_action_name(int a)
{
    static const char *names[]={"checkpoint","fx_canvas","routing_matrix","close_matrix",
        "effect_mix","grain_size","mute","solo","bypass","tracker","mosaic",
        "health_window","gallery","finish","space","escape","seek","follow","verify_state"};
    return a>=0 && a<=LT_ASSERT?names[a]:"invalid";
}
static const char *live_test_label(void)
{
    return live_test.state==LT_PREPARING?" [PREPARING DIAGNOSTIC]":
        live_test.state==LT_ARMED?" [LIVE TEST ARMED: MOSAIC PLAY]":
        live_test.state==LT_RUNNING || live_test.state==LT_PENDING?" [LIVE TEST RUNNING: ESC TO ABORT]":
        live_test.state==LT_DONE?" [LIVE TEST COMPLETE: RELOAD TO REPEAT]":
        live_test.state==LT_ABORTED?" [LIVE TEST ABORTED: RELOAD TO REPEAT]":"";
}
static int live_test_save_blocked(void){return live_test.touched;}
static int live_test_slot(const TsMatrixControls *m,int id)
{
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)if(m->effect[i].id==(uint32_t)id && m->effect[i].type)return i;
    return -1;
}
static void live_test_add(unsigned ms,int action,int target,float value)
{
    if(live_test.count<(int)(sizeof(live_test.steps)/sizeof(*live_test.steps)))
        live_test.steps[live_test.count++]=(LiveTestStep){ms,action,target,value};
}
#include "main_sdl_angelic_test.inc"
static void live_test_plan(void)
{
    if(live_test.angelic){live_test_angelic_plan();return;}
    live_test.count=0;
    live_test_add(0,LT_CHECK,0,0);
    if(live_test.mode>=2) {
        /* Preserve the entire buildup, not just a seek into the late section.
           Checkpoints do not edit sound or open windows. */
        for(unsigned ms=30000;ms<live_test.duration_ms;ms+=30000)
            live_test_add(ms,LT_CHECK,(int)(ms/30000),0);
        live_test_add(live_test.duration_ms,LT_END,0,0);return;
    }
    /* Twenty seconds of warmup, followed by the same controls in three scenes.
       Stable effect/card identities are resolved before playback, not from
       whatever happens to be selected when a scheduled action is late. */
    for(int p=0;p<3;++p) {
        unsigned t=20000+60000*p;
        if(p)live_test_add(t,LT_GALLERY,p-1,0);
        live_test_add(t+100,LT_CHECK,p+1,0);
        live_test_add(t+5000,LT_FX,0,0);
        for(int i=0;i<40;++i)live_test_add(t+8000+i*100,LT_MIX,12,.25f+.25f*(i<20?i:39-i)/19.f);
        live_test_add(t+12000,LT_MIX,12,-1);
        for(int i=0;i<20;++i)live_test_add(t+14000+i*100,LT_GRAIN,4,.2f+.4f*(i<10?i:19-i)/9.f);
        live_test_add(t+16000,LT_GRAIN,4,-1);
        live_test_add(t+17000,LT_MUTE,0,1);live_test_add(t+19000,LT_MUTE,0,0);
        live_test_add(t+21000,LT_SOLO,0,1);live_test_add(t+23000,LT_SOLO,0,0);
        live_test_add(t+25000,LT_BYPASS,12,1);live_test_add(t+28000,LT_BYPASS,12,0);
        live_test_add(t+30000,LT_TRACKER,0,0);live_test_add(t+34000,LT_MOSAIC,0,0);
        live_test_add(t+36000,LT_MATRIX,0,0);live_test_add(t+40000,LT_FX,0,0);
        live_test_add(t+44000,LT_HEALTH,0,1);live_test_add(t+46000,LT_HEALTH,0,0);
        live_test_add(t+48000,LT_CLOSE,0,0);live_test_add(t+50000,LT_CHECK,p+4,0);
    }
    live_test_add(200000,LT_GALLERY,2,0);
    live_test_add(208100,LT_CHECK,7,0);
    live_test_add(216000,LT_END,0,0);
}
static int live_test_matches(const TsMatrixControls *m,const TsSamplePages *pages,const char *path)
{
    if(strcmp(path_basename(path),"Tree-of-Life-Live-FX-04.tsr"))return 0;
    TsMosaicGallery *g=pages->gallery;
    if(!g || g->count!=10 || g->active!=2 || g->outgoing>=0)return 0;
    static const char *names[]={"KETER","CHOKHMAH","BINAH"};
    static const int counts[]={53,48,43};
    for(int p=0;p<3;++p) {
        TsMosaic *m=g->canvas[p].scene;int count=0;uint64_t id=0;
        if(!m || !strstr(g->canvas[p].name,names[p]) || m->playing || m->audition_id)return 0;
        for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(m->events[i].id) {
            const TsMosaicEvent *e=&m->events[i];++count;
            if(e->muted || e->solo || e->live)return 0;
            /* The mute/solo exercise begins at most 43 seconds into a scene. */
            if(e->source && !e->source->video_path[0] && e->start<=300 &&
               e->start+e->duration>=345 && (!id || e->id<id))id=e->id;
        }
        if(count!=counts[p] || !id)return 0;
        live_test.card[p]=id;
    }
    int count=0;
    if(!m->enabled)return 0;
    for(int i=0;i<TS_MATRIX_EFFECT_CAPACITY;++i)if(m->effect[i].type) {
        ++count;if(!m->effect[i].enabled)return 0;
        live_test.mixes[i]=m->effect[i].mix;
    }
    int spring=live_test_slot(m,12),grain=live_test_slot(m,4);
    if(count!=14 || spring<0 || grain<0 || m->effect[spring].type!=TS_MATRIX_FX_SUNVOX ||
       m->effect[grain].type!=TS_SISTER_FX_GRAIN ||
       !strstr(ts_sunvox_name(&m->effect[spring].sunvox),"Taneb Spring"))return 0;
    live_test.grain=m->effect[grain].a;
    return 1;
}
static void live_test_loaded(SDL_AudioDeviceID device,AudioState *a,TsUiState *ui,TsSamplePages *pages,const char *path)
{
    if(live_test.state==LT_RUNNING || live_test.state==LT_PENDING) {
        ts_session_log_text("live_test_abort","reason=project_replaced");
        ts_profile_enable(live_test.profile);
    }
    live_test.state=LT_OFF;live_test.next=0;live_test.profile_failed=0;live_test.angelic=0;live_test.phase_key=-1;
    if(!ui->config.diagnostic_live_test)return;
    live_test.angelic=ui->config.diagnostic_live_test==1 && live_test_angelic_matches(&a->sister.matrix.controls,pages,path);
    if(!live_test.angelic && !live_test_matches(&a->sister.matrix.controls,pages,path)) {
        snprintf(ui->status,sizeof(ui->status),"LIVE TEST: OPEN COMPLETE ANGELIC NOISE GALLERY (OPTION 1), OR ORIGINAL TREE OF LIFE 04");
        ts_session_log_text("live_test_rejected","reason=baseline_mismatch");return;
    }
    if(!a->realtime_diagnostics_enabled || !ts_session_log_enabled()) {
        snprintf(ui->status,sizeof(ui->status),"LIVE TEST: AUDIO TELEMETRY AND SESSION RECORDER REQUIRED");return;
    }
    live_test.mode=ui->config.diagnostic_live_test;
    live_test.keter_end=ts_mosaic_end(pages->gallery->canvas[0].scene);
    if(live_test.mode>=2 && (!isfinite(live_test.keter_end) || live_test.keter_end<=0 || live_test.keter_end>3600)) {
        snprintf(ui->status,sizeof(ui->status),"KETER TEST: EXPECTED A MOVEMENT UNDER ONE HOUR");return;
    }
    live_test.duration_ms=live_test.mode>=2?(unsigned)ceil(live_test.keter_end*1000)+8000:216000;
    live_test.project_hash=ui->saved_state_hash;
    snprintf(live_test.project,sizeof(live_test.project),"%s",path);
    live_test_plan();live_test.state=(live_test.angelic || live_test.mode>=2)?LT_PREPARING:LT_ARMED;live_test.touched=1;
    if(device)SDL_LockAudioDevice(device);
    for(int p=0;p<(live_test.angelic?10:3);++p) {
        TsMosaic *m=pages->gallery->canvas[p].scene;
        ts_mosaic_seek(m,(live_test.angelic || live_test.mode>=2)?0:300);m->repeat=0;m->playing=0;
        if(live_test.angelic || live_test.mode>=2){ts_mosaic_set_speed(m,1);m->marker_loop=0;}
        pages->gallery->canvas[p].entry=TS_GALLERY_RESUME;
    }
    pages->gallery->fade_seconds=live_test.mode>=2?0:8;
    if(device)SDL_UnlockAudioDevice(device);
    ui->mosaic_open=1;ui->tracker_open=ui->fm_open=0;
    ui->mosaic_scroll=live_test.mode>=2?0:295;ui->mosaic_scale=4;ui->mosaic_hscale=1;
    ui->project_check_ms=0;
    snprintf(ui->status,sizeof(ui->status),"TREE 04 LIVE TEST ARMED: PRESS MOSAIC PLAY / 3:36 / ESC ABORTS / RELOAD TO REPEAT");
    if(live_test.angelic)snprintf(ui->status,sizeof(ui->status),"PREPARING ANGELIC NOISE TEST / PRESS PLAY WHEN ARMED");
    else if(live_test.mode>=2)snprintf(ui->status,sizeof(ui->status),"PREPARING UNINTERRUPTED KETER TEST / PRESS PLAY WHEN ARMED");
    ts_session_log_event("live_test_armed","scenario=%s steps=%d project_hash=%016llx cards=%llu,%llu,%llu start_s=%d fade_s=%d keter_end_s=%.6f duration_ms=%u",
        live_test_scenario(),live_test.count,(unsigned long long)live_test.project_hash,
        (unsigned long long)live_test.card[0],(unsigned long long)live_test.card[1],(unsigned long long)live_test.card[2],
        (live_test.angelic || live_test.mode>=2)?0:300,live_test.mode>=2?0:8,live_test.keter_end,live_test.duration_ms);
}
static void live_test_transport(TsUiState *ui,int playing)
{
    (void)ui;
    if(live_test.state==LT_ARMED && playing) {
        live_test.profile=ts_profile_enabled();ts_profile_enable(live_test.angelic || live_test.mode==3);
        if(live_test.angelic || live_test.mode==3)ts_profile_reset();
        live_test.profile_from_ms=0;
        live_test.started=SDL_GetPerformanceCounter();live_test.frequency=SDL_GetPerformanceFrequency();
        live_test.state=LT_PENDING;
    }
}
/* UI thread only: one bounded log record per interval, no disk I/O or
   formatting in the callback. Reset measurement counters, never DSP history.
   Epoch publication is asynchronous; a boundary can straddle one callback. */
static void live_test_profile_interval(int phase,double elapsed_ms)
{
    if((!live_test.angelic && live_test.mode!=3) || phase==0)return;
    char text[16384];
    int n=snprintf(text,sizeof(text),
        "scenario=%s from_ms=%.3f to_ms=%.3f phase=%d profile=1\n",
        live_test_scenario(),live_test.profile_from_ms,elapsed_ms,phase);
    if(n>0 && (size_t)n<sizeof(text)) {
        ts_profile_report(text+n,sizeof(text)-(size_t)n);
        if(!ts_session_log_text("live_test_profile",text))live_test.profile_failed=1;
    }
    live_test.profile_from_ms=elapsed_ms;ts_profile_reset();
}
static void live_test_checkpoint(AudioState *a,int phase)
{
    double elapsed=1000.0*(SDL_GetPerformanceCounter()-live_test.started)/live_test.frequency;
    live_test_profile_interval(phase,elapsed);
    TsRealtimeDiagnosticsSnapshot s={0};ts_realtime_diagnostics_get(&a->realtime_diagnostics,&s);
    ts_session_log_event("live_test_checkpoint",
        "phase=%d elapsed_ms=%.3f callbacks=%llu frames=%llu over_budget=%llu near_budget=%llu max_ms=%.6f generation=%u",
        phase,elapsed,
        (unsigned long long)s.callback_count,(unsigned long long)s.frame_count,
        (unsigned long long)s.deadline_overruns,(unsigned long long)s.near_overruns,s.worst_microseconds/1000,a->audio_health_generation);
}
static void live_test_finish(SDL_AudioDeviceID device,AudioState *a,TsUiState *ui,int complete,const char *reason)
{
    if(live_test.state!=LT_RUNNING && live_test.state!=LT_PENDING)return;
    live_test_checkpoint(a,complete?8:-1);
    if(live_test.profile_failed){complete=0;reason="profile_log_unavailable";}
    live_test.state=complete?LT_DONE:LT_ABORTED; /* Stop cannot rearm the run. */
    mosaic_transport(device,a,ui,1);ts_profile_enable(live_test.profile);
    ts_session_log_event(complete?"live_test_complete":"live_test_abort",
        "scenario=%s completed=%d total=%d reason=%s",live_test_scenario(),live_test.next,live_test.count,reason);
    TsSessionLogStatus log;ts_session_log_status(&log);live_test.export_serial=log.export_serial;
    int saved=ts_session_log_export_project(live_test.project,0);live_test.exporting=saved;
    snprintf(ui->status,sizeof(ui->status),"LIVE TEST %s / LOG %s / RELOAD PROJECT TO REPEAT",
        complete?"COMPLETE":"ABORTED",saved?"EXPORT QUEUED":"EXPORT FAILED; USE F12 SAVE SESSION");
    ui->project_check_ms=0;
}
static SDL_Event live_test_key(SDL_Window *window,SDL_Keycode key,SDL_Keymod mod)
{
    SDL_Event e;SDL_zero(e);e.type=SDL_KEYDOWN;e.key.windowID=SDL_GetWindowID(window);
    e.key.keysym.sym=key;e.key.keysym.mod=mod;return e;
}
static void live_test_phase(SDL_AudioDeviceID device,AudioState *a,TsUiState *ui,TsSamplePages *pages)
{
    if(!live_test.angelic)return;
    if(device)SDL_LockAudioDevice(device);
    int phase=a->engine_stopped?3:!ui->mosaic->playing?2:pages->gallery->outgoing>=0?1:0;
    int movement=pages->gallery->active,canvas=matrix_window.visible && matrix_window.canvas;
    if(device)SDL_UnlockAudioDevice(device);
    int key=phase+4*canvas+8*movement;
    if(key==live_test.phase_key)return;
    live_test.phase_key=key;
    static const char *names[]={"playing","transition","paused_tails","stopped"};
    ts_session_log_event("live_test_phase","mode=%s fx_canvas=%d movement=%d settle_ms=2000",names[phase],canvas,movement);
}
static int live_test_action(const LiveTestStep *s,SDL_Window *window,SDL_AudioDeviceID device,
    AudioState *a,TsUiState *ui,TsSamplePages *pages,TsInstrument *bank,MosaicController *mosaic,
    PortalController *portal,SisterWindow *sister,TransformController *transform)
{
    if(s->action==LT_SPACE || s->action==LT_ESCAPE) {
        ui->mosaic_open=1;ui->tracker_open=0;
        SDL_Event e=live_test_key(window,s->action==LT_SPACE?SDLK_SPACE:SDLK_ESCAPE,KMOD_NONE);
        if(!mosaic_event(&e,window,device,a,ui,bank,mosaic,portal,sister,transform,a->output_rate))return 0;
        if(device)SDL_LockAudioDevice(device);
        live_test.parked_time=ui->mosaic->time;
        int ok=s->action==LT_SPACE?(!a->engine_stopped && ui->mosaic->playing==s->target):
            (a->engine_stopped && !ui->mosaic->playing && (!s->target || ui->mosaic->time==0));
        if(device)SDL_UnlockAudioDevice(device);return ok;
    }
    if(s->action==LT_ASSERT) {
        double positions[TS_GALLERY_LIMIT]={0};int playing[TS_GALLERY_LIMIT]={0};unsigned sleeping;int stopped,outgoing,active,ok=1;
        if(device)SDL_LockAudioDevice(device);
        stopped=a->engine_stopped;sleeping=a->sister.matrix_effects.suspended;
        active=pages->gallery->active;outgoing=pages->gallery->outgoing;
        for(int c=0;c<pages->gallery->count;++c){positions[c]=pages->gallery->canvas[c].scene->time;playing[c]=pages->gallery->canvas[c].scene->playing;}
        if(device)SDL_UnlockAudioDevice(device);
        if(s->target==0 || s->target==4)ok=!stopped && playing[active];
        else ok=!playing[active] && stopped==(s->target!=1) && fabs(positions[active]-live_test.parked_time)<1e-8;
        if(s->target==3)ok&=positions[active]==0;
        if(s->target==4){ok&=outgoing<0;for(int c=0;c<pages->gallery->count;++c)if(c!=active)ok&=!playing[c];}
        ts_session_log_event("live_test_state","expected=%d success=%d active=%d outgoing=%d engine_stopped=%d suspended_mask=%u position=%.9f fx_canvas=%d",
            s->target,ok,active,outgoing,stopped,sleeping,positions[active],matrix_window.visible && matrix_window.canvas);
        live_test_checkpoint(a,100+live_test.next);return ok;
    }
    if(s->action==LT_FOLLOW){ui->mosaic_follow=s->target;return 1;}
    if(s->action==LT_SEEK) {
        ui->mosaic_open=1;ui->tracker_open=0;
        double destination=ts_mosaic_end(ui->mosaic)*s->value;
        if(s->target) {
            int nearest=-1;double distance=1e30;
            for(int i=0;i<TS_MOSAIC_MARKERS;++i)if(ui->mosaic->markers[i].stored && fabs(ui->mosaic->markers[i].time-destination)<distance){nearest=i;distance=fabs(ui->mosaic->markers[i].time-destination);}
            if(nearest<0)return 0;mosaic_marker_jump(device,ui,nearest);return 1;
        }
        ui->mosaic_scroll=fmax(0,destination-20/ui->mosaic_scale);
        SDL_Event e;SDL_zero(e);e.type=SDL_MOUSEBUTTONDOWN;e.button.button=SDL_BUTTON_MIDDLE;
        if(!mosaic_navigation_event(&e,device,ui,mosaic,140,86,KMOD_NONE))return 0;
        e.type=SDL_MOUSEBUTTONUP;return mosaic_navigation_event(&e,device,ui,mosaic,140,86,KMOD_NONE);
    }
    if(s->action==LT_CHECK){live_test_checkpoint(a,s->target);return 1;}
    if(s->action==LT_CLOSE){matrix_hide();return !matrix_window.visible;}
    if(s->action==LT_HEALTH){if(s->value)return ts_audio_health_show(ui);ts_audio_health_close();return 1;}
    if(s->action==LT_TRACKER || s->action==LT_MOSAIC) {
        int wanted=s->action==LT_TRACKER;
        if(ui->tracker_open!=wanted) {
            SDL_Event e=live_test_key(window,SDLK_F10,KMOD_NONE);
            if(!tracker_event(&e,window,device,a,ui,pages,bank,a->output_rate))return 0;
        }
        if(!wanted)ui->mosaic_open=1;
        return ui->tracker_open==wanted;
    }
    if(s->action==LT_GALLERY) {
        /* Exercise the actual Gallery selection/Enter controller, including
           its publication, UI cache retirement and crossfade checks. */
        SDL_Event e=live_test_key(window,SDLK_g,KMOD_CTRL);
        ui->mosaic_open=1;
        if(!ui->mosaic_gallery_open && !mosaic_gallery_event(&e,device,a,ui,bank,mosaic,0,0,KMOD_CTRL))return 0;
        ui->mosaic_gallery_selected=s->target;
        e=live_test_key(window,SDLK_RETURN,KMOD_NONE);
        if(!mosaic_gallery_event(&e,device,a,ui,bank,mosaic,0,0,KMOD_NONE))return 0;
        return pages->gallery->active==s->target && !ui->mosaic_gallery_open;
    }
    if(s->action==LT_MUTE || s->action==LT_SOLO) {
        int p=pages->gallery->active;if(p<0 || p>=(live_test.angelic?10:3))return 0;
        uint64_t id=live_test.card[p];
        TsMosaicEvent *card=ts_mosaic_find(ui->mosaic,id);if(!card)return 0;
        int wanted=s->value!=0,old=s->action==LT_MUTE?card->muted:card->solo;
        mosaic_select_only(ui,id);ui->mosaic_open=1;
        if(old!=wanted) {
            SDL_Event e=live_test_key(window,s->action==LT_MUTE?SDLK_m:SDLK_s,s->action==LT_MUTE?KMOD_NONE:KMOD_ALT);
            if(!mosaic_event(&e,window,device,a,ui,bank,mosaic,portal,sister,transform,a->output_rate))return 0;
        }
        return (s->action==LT_MUTE?card->muted:card->solo)==wanted;
    }
    if(!matrix_window.visible && !matrix_show(device,a,ui,pages,bank,-1,-1))return 0;
    if(s->action==LT_FX || s->action==LT_MATRIX) {
        fx_view(s->action==LT_FX,pages,bank);return matrix_window.canvas==(s->action==LT_FX);
    }
    int slot=live_test_slot(&matrix_window.controls,s->target);if(slot<0)return 0;
    matrix_window.effect_open=s->target;fx_inspector_build();
    if(s->action==LT_BYPASS) {
        int enabled=s->value==0;
        if(matrix_window.controls.effect[slot].enabled!=enabled)fx_inspector_toggle(device,a,ui);
        return ui->config.matrix.effect[slot].enabled==enabled;
    }
    float value=s->value<0?(s->action==LT_MIX?live_test.mixes[slot]:live_test.grain):s->value;
    int position=0;
    if(s->action==LT_MIX)while(position<mx_effect_controls(&matrix_window.controls.effect[slot]) &&
        mx_effect_control_id(&matrix_window.controls.effect[slot],position)!=3)++position;
    FxInspectorControl c={.kind=FX_CONTROL_EFFECT,.index=slot,.field=position};
    fx_inspector_set(&c,value,0,0,device,a,ui);
    float actual=s->action==LT_MIX?ui->config.matrix.effect[slot].mix:ui->config.matrix.effect[slot].a;
    return fabsf(actual-value)<1e-5f;
}
static int live_test_input(const SDL_Event *e,SDL_AudioDeviceID device,AudioState *a,TsUiState *ui)
{
    if(live_test.state!=LT_RUNNING && live_test.state!=LT_PENDING)return 0;
    /* Key releases/motion/focus events are normal consequences of opening
       windows. Actual player edits invalidate a controlled run. */
    int escape=e->type==SDL_KEYDOWN && e->key.keysym.sym==SDLK_ESCAPE;
    if(escape || e->type==SDL_KEYDOWN || e->type==SDL_MOUSEBUTTONDOWN || e->type==SDL_MOUSEWHEEL ||
       e->type==SDL_TEXTINPUT || e->type==SDL_DROPFILE || e->type==SDL_QUIT ||
       (midi_wake_event && e->type==midi_wake_event) ||
       (e->type==SDL_WINDOWEVENT && (e->window.event==SDL_WINDOWEVENT_CLOSE || e->window.event==SDL_WINDOWEVENT_MINIMIZED))) {
        live_test_finish(device,a,ui,0,escape?"escape":"manual_input");
        return escape;
    }
    return 0;
}
static void live_test_poll(SDL_Window *window,SDL_AudioDeviceID device,AudioState *a,TsUiState *ui,
    TsSamplePages *pages,TsInstrument *bank,MosaicController *mosaic,PortalController *portal,
    SisterWindow *sister,TransformController *transform,Uint64 now)
{
    if(live_test.state==LT_PREPARING) {
        /* Use the normal Gallery publication and cache-retirement controller
           while stopped, before arming Play or starting the measurement. */
        mosaic_transport(device,a,ui,1);
        if(!mosaic_gallery_enter(device,a,ui,bank,mosaic,0)) {
            live_test.state=LT_ABORTED;
            snprintf(ui->status,sizeof(ui->status),"KETER TEST COULD NOT SELECT CANVAS / RELOAD TO RETRY");return;
        }
        ui->mosaic_scroll=0;ui->mosaic_scale=4;ui->mosaic_hscale=1;
        live_test.state=LT_ARMED;
        if(live_test.angelic)snprintf(ui->status,sizeof(ui->status),"ANGELIC NOISE TEST ARMED: MOSAIC PLAY / 13:12 / ESC ABORTS / PROFILE ON");
        else snprintf(ui->status,sizeof(ui->status),"KETER %s TEST ARMED: MOSAIC PLAY / %u:%02u / ESC ABORTS",
            live_test.mode==3?"PROFILE":"FULL",live_test.duration_ms/60000,(live_test.duration_ms/1000)%60);
        return;
    }
    if(live_test.exporting && (live_test.state==LT_DONE || live_test.state==LT_ABORTED)) {
        TsSessionLogStatus log;ts_session_log_status(&log);
        if(log.export_serial!=live_test.export_serial && log.export_result!=1) {
            live_test.exporting=0;
            snprintf(ui->status,sizeof(ui->status),"LIVE TEST %s / %s / RELOAD PROJECT TO REPEAT",
                live_test.state==LT_DONE?"COMPLETE":"ABORTED",
                log.export_result==2?"LOG SAVED IN PROJECT logs":"EXPORT FAILED; RECOVERY LOG KEPT (F12)");
        }
    }
    if(live_test.state!=LT_PENDING && live_test.state!=LT_RUNNING)return;
    if(live_test.state==LT_PENDING) {
        live_test.generation=a->audio_health_generation;
        TsSessionLogStatus log;ts_session_log_status(&log);
        live_test.dropped_audio=log.dropped_audio;live_test.dropped_events=log.dropped_events;
        live_test.state=LT_RUNNING;
        TsRealtimeDiagnosticsSnapshot initial={0};ts_realtime_diagnostics_get(&a->realtime_diagnostics,&initial);
        ts_session_log_event("live_test_start",
            "scenario=%s steps=%d project_hash=%016llx start_ticks=%llu frequency=%llu backend=%s rate=%u frames=%u channels=%u workers=%u profile=%d revision=%s build=%s %s",
            live_test_scenario(),live_test.count,(unsigned long long)live_test.project_hash,
            (unsigned long long)live_test.started,(unsigned long long)live_test.frequency,
            ts_native_current_driver()?ts_native_current_driver():"none",
            (unsigned)a->output_rate,initial.device_buffer_frames,(unsigned)a->output_device_channels,
            ts_sunvox_workers_configured(),ts_profile_enabled(),TS_LIVE_TEST_REVISION,__DATE__,__TIME__);
    }
    if((live_test.angelic || live_test.mode==3) && !ts_profile_enabled()) {
        live_test_finish(device,a,ui,0,"profile_unavailable");return;
    }
    if(a->audio_health_generation!=live_test.generation || strcmp(ui->project_path,live_test.project)) {
        live_test_finish(device,a,ui,0,"device_or_project_changed");return;
    }
    if(live_test.next>=live_test.count)return;
    const LiveTestStep *s=&live_test.steps[live_test.next];
    double elapsed=1000.0*(now-live_test.started)/live_test.frequency;
    if(elapsed<s->ms)return;
    TsSessionLogStatus log;ts_session_log_status(&log);
    if(log.state!=TS_SESSION_RECORDING || log.dropped_audio!=live_test.dropped_audio || log.dropped_events!=live_test.dropped_events) {
        live_test_finish(device,a,ui,0,"incomplete_telemetry");return;
    }
    uint64_t frames=atomic_load_explicit(&a->realtime_diagnostics.frame_count,memory_order_relaxed);
    ts_session_log_event("live_test_action_begin",
        "index=%d action=%s target=%d value=%.6f planned_ms=%u actual_ms=%.3f late_ms=%.3f audio_frames=%llu",
        live_test.next,live_test_action_name(s->action),s->target,s->value,s->ms,elapsed,elapsed-s->ms,(unsigned long long)frames);
    Uint64 begin=SDL_GetPerformanceCounter();
    int ok=s->action==LT_END || live_test_action(s,window,device,a,ui,pages,bank,mosaic,portal,sister,transform);
    double duration=1000.0*(SDL_GetPerformanceCounter()-begin)/live_test.frequency;
    ts_session_log_event("live_test_action_end","index=%d success=%d duration_ms=%.3f gallery=%d",
        live_test.next,ok,duration,pages->gallery?pages->gallery->active:-1);
    if(ok && s->action!=LT_END)live_test_phase(device,a,ui,pages);
    ++live_test.next;
    if(live_test.profile_failed){live_test_finish(device,a,ui,0,"profile_log_unavailable");return;}
    if(!ok){live_test_finish(device,a,ui,0,"action_failed");return;}
    if(s->action==LT_END){live_test_finish(device,a,ui,1,"finished");return;}
    /* Do not silently move the schedule to hide stalls. One action per UI
       iteration prevents a late burst from starving paint/service entirely. */
}
