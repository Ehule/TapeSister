/* Opt-in headless stress probe, not a timing-sensitive CI assertion. Uses the
   real audio callback, pedalboard controller, renderer child and Health reader.
   All trace file I/O happens after the audio device has stopped. */
typedef struct {
    Uint64 at, finished;
    const char *kind;
    int phase;
    unsigned before, after;
} InteractionEvent;
static InteractionEvent interaction_events[8192];
static unsigned interaction_event_count;
static void interaction_note(const char *kind,int phase,Uint64 before,unsigned callbacks)
{
    assert(interaction_event_count<8192);
    interaction_events[interaction_event_count++]=(InteractionEvent){before,SDL_GetPerformanceCounter(),kind,
        phase,callbacks,atomic_load_explicit(&timing_count,memory_order_acquire)};
}
static void interaction_run(const char *prefix,unsigned seconds,int slots,int notes)
{
    assert(seconds>=4 && seconds<=60 && slots>=5 && slots<=20 && notes>=1 && notes<=5);
    for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(a.mosaic->events[i].id) {
        TsMosaicEvent *e=&a.mosaic->events[i];e->note_count=notes;e->gain=.03f/notes;
        for(int j=0;j<notes;++j)e->notes[j]=36+i+j*7;
        ++e->revision;
    }
    static SisterWindow sister;
    ts_sister_ui_model_init(&sister.model,&u.config);sister.model.fx_page=1;
    TsSisterParameters p=a.sister.parameters;p.fx.slot_count=slots;
    p.fx.transition=ts_sister_fx_transition_normalized(2000);
    for(int i=0;i<slots;++i) {
        sister_fx_slot_select_type(&p.fx.slot[i],(TsSisterFxType)(1+i%4));
        p.fx.slot[i].enabled=1;p.fx.slot[i].placement=TS_SISTER_FX_PLACE_POST;p.fx.slot[i].mix=.35f;
    }
    ts_sister_runtime_set_parameters(&a.sister,&p);
    ts_sister_post_fx_sync_controls(&a.sister.post_fx,&p.fx);
    sister.model.parameters=p;
    a.sister.mandala.seed=314159;
    SDL_AudioSpec want={0},got={0};want.freq=48000;want.format=AUDIO_F32SYS;
    want.channels=4;want.samples=512;want.callback=measured_callback;
    ts_real_output=ts_native_open(NULL,0,&want,&got,0);
    assert(ts_real_output && got.freq==48000 && got.samples==512 && got.channels==4);
    ts_output_userdata=&a;
    ts_audio_endpoints_init();ts_audio_endpoint_configure(&ts_output_endpoint,"");
    ts_audio_endpoint_begin_open(&ts_output_endpoint,0);
    ts_audio_endpoint_opened(&ts_output_endpoint,ts_real_output,"Headless dummy output",0,48000,4,512,"F32");
    ts_native_pause(ts_real_output,0);
    TsMandalaPreferences prefs={0};
    /* Repeat combined and baseline phases to expose drift/order effects. */
    const char *names[]={"BASELINE","PEDAL_ONLY","VISUAL_STEADY","VISUAL_CYCLES",
        "COMBINED","FROZEN_PEDAL","COMBINED_REPEAT","BASELINE_REPEAT"};
    unsigned first[8],last[8];
    static char health[8][16384];
    Uint64 origin=SDL_GetPerformanceCounter();
    printf("interaction fixture: slots=%d Mosaic=%d ARP=3 Tracker=4 Prism=8 rate=48000 buffer=512 channels=4 seconds/phase=%u\n",slots,16*notes,seconds);
    fflush(stdout);
    for(int phase=0;phase<8;++phase) {
        /* Reap the previous renderer and settle identical controls before each
           measured phase, without stopping any musical transport. */
        ts_mandala_close();
        Uint32 settle=SDL_GetTicks();
        while(host.shared && SDL_GetTicks()-settle<3000) {
            ts_mandala_poll(&a.sister.mandala,&prefs);SDL_Delay(10);
        }
        assert(!host.shared);
        ts_native_lock(ts_real_output);
        ts_sister_runtime_set_parameters(&a.sister,&p);
        ts_sister_post_fx_sync_controls(&a.sister.post_fx,&p.fx);
        ts_native_unlock(ts_real_output);sister.model.parameters=p;
        settle=SDL_GetTicks();
        while(SDL_GetTicks()-settle<2000)SDL_Delay(10);
        if(phase==5) {
            assert(ts_mandala_open(&a.sister.mandala,&prefs,error,sizeof(error)));
            ts_mandala_freeze(); /* Freeze before ready: isolates an idle graphics process. */
        }
        ts_audio_health_reset(&a);
        first[phase]=atomic_load_explicit(&timing_count,memory_order_acquire);
        Uint32 began=SDL_GetTicks(),next_edit=began,next_visual=began;
        int edits=phase==1 || phase==4 || phase==5 || phase==6;
        int cycles=phase==3 || phase==4 || phase==6;
        unsigned edit=0;
        while(SDL_GetTicks()-began<seconds*1000u) {
            Uint32 now=SDL_GetTicks();
            if((cycles || phase==2) && (Sint32)(now-next_visual)>=0) {
                Uint64 at=SDL_GetPerformanceCounter();unsigned count=atomic_load_explicit(&timing_count,memory_order_acquire);
                if(ts_mandala_active()) {
                    ts_mandala_close();interaction_note("CLOSE",phase,at,count);
                    next_visual=now+3000;
                } else if(!host.shared) {
                    assert(ts_mandala_open(&a.sister.mandala,&prefs,error,sizeof(error)));
                    interaction_note("OPEN",phase,at,count);
                    next_visual=phase==2?began+seconds*1000u:now+3000;
                }
            }
            if(edits && (Sint32)(now-next_edit)>=0) {
                TsSisterUiHit hit={.action=TS_SISTER_UI_ACTION_PARAMETER};
                if(edit%10==9) {
                    hit.index=TS_SISTER_UI_PARAM_FX_TRANSITION;
                    hit.normalized=ts_sister_fx_transition_normalized(edit%20==9?2000:240000);
                } else {
                    hit.index=TS_SISTER_UI_SLOT_PARAMETER(edit%(unsigned)slots,4);
                    hit.normalized=.2f+.6f*((edit*7)%31)/30.f;
                }
                Uint64 at=SDL_GetPerformanceCounter();unsigned count=atomic_load_explicit(&timing_count,memory_order_acquire);
                sister_apply_action(TS_AUDIO_LOGICAL_OUTPUT,&a,&u,&bank,&sister,NULL,NULL,hit,48000,4);
                interaction_note(edit%10==9?"TRANSITION":"MIX",phase,at,count);
                ++edit;next_edit=now+50; /* Twenty real controller edits per second. */
            }
            ts_mandala_follow_effects(&a.sister);ts_mandala_poll(&a.sister.mandala,&prefs);
            SDL_Delay(5);
        }
        last[phase]=atomic_load_explicit(&timing_count,memory_order_acquire);
        report(first[phase],last[phase],names[phase]);
        /* Capture a real Health report, including the UI snapshot. */
        ts_health.visible=1;
        ts_native_lock(ts_real_output);ts_audio_health_capture_setup(&a,&u,SDL_GetTicks());ts_native_unlock(ts_real_output);
        ts_health.visible=0;ts_audio_health_report(&a,health[phase],sizeof(health[phase]));
        TsRealtimeDiagnosticsSnapshot s;ts_realtime_diagnostics_get(&a.realtime_diagnostics,&s);
        printf("health phase=%s callbacks=%llu overruns=%llu near=%llu hold_max_ms=%.6f hold_over=%llu wait_max_ms=%.3f\n",
            names[phase],(unsigned long long)s.callback_count,(unsigned long long)s.deadline_overruns,
            (unsigned long long)s.near_overruns,s.control_worst_microseconds/1000,
            (unsigned long long)s.control_long_holds,s.control_wait_worst_microseconds/1000);
        fflush(stdout);
    }
    ts_native_pause(ts_real_output,1);ts_native_close(ts_real_output);ts_real_output=0;ts_output_userdata=NULL;
    assert(ts_tapehead_running() && a.mosaic->playing && a.keyboard_sequence.running);
    puts("interaction transports: embedded TrackSister, Mosaic and ARP still running");
    ts_mandala_close();ts_mandala_follow_effects(&a.sister);
    char path[1024];
    snprintf(path,sizeof(path),"%s-callbacks.csv",prefix);FILE *f=fopen(path,"w");assert(f);
    fputs("phase,index,start_ms,processing_us,start_interval_us\n",f);
    double scale=1000.0/SDL_GetPerformanceFrequency();
    for(int phase=0;phase<8;++phase)for(unsigned i=first[phase];i<last[phase];++i)
        fprintf(f,"%s,%u,%.6f,%.3f,%.3f\n",names[phase],i,(callback_starts[i]-origin)*scale,timings[i],arrivals[i]);
    fclose(f);
    snprintf(path,sizeof(path),"%s-events.csv",prefix);f=fopen(path,"w");assert(f);
    fputs("phase,action,start_ms,duration_ms,callbacks_before,callbacks_after\n",f);
    for(unsigned i=0;i<interaction_event_count;++i) {
        const InteractionEvent *e=&interaction_events[i];
        fprintf(f,"%s,%s,%.6f,%.6f,%u,%u\n",names[e->phase],e->kind,(e->at-origin)*scale,
            (e->finished-e->at)*scale,e->before,e->after);
    }
    fclose(f);
    snprintf(path,sizeof(path),"%s-health.txt",prefix);f=fopen(path,"w");assert(f);
    for(int phase=0;phase<8;++phase)fprintf(f,"PHASE %s\n%s\n",names[phase],health[phase]);
    fclose(f);
}
