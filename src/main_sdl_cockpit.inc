/* UI-only control center. No callback hooks, score checkpoints, or consuming
   Matrix meter reads. Runtime flags use the existing short device exclusion. */
typedef struct {
    TsTapeHeadPerformance tracker;
    TsMasterClock master_clock;
    unsigned selected,running;
    double time,speed;
    int repeat,cue_loop,cue,slot,note,notes,held,synth,live_loops;
    TsMosaicRange range;
} PerformanceStatus;
static struct {
    const TsMosaic *owner;
    uint64_t epoch,revision;
    Uint32 updated;
    int cards,patterns,muted,solo,here;
    double end;
} performance_arrangement;
static unsigned performance_popcount(uint32_t bits)
{unsigned n=0;for(;bits;bits&=bits-1)++n;return n;}
static void performance_time(char *out,size_t size,double seconds)
{
    if(!isfinite(seconds) || seconds<0)seconds=0;
    seconds=fmin(seconds,5999999.999);unsigned minutes=(unsigned)(seconds/60);
    snprintf(out,size,"%02u:%06.3f",minutes,seconds-minutes*60.0);
}
static PerformanceStatus performance_status(SDL_AudioDeviceID device,AudioState *a)
{
    PerformanceStatus s={0};
    if(ts_tapehead_frame())ts_tapehead_host_lock();else if(device)SDL_LockAudioDevice(device);
    ts_tapehead_performance_locked(&s.tracker);
    s.master_clock=a->sister.master_clock;
    s.selected=a->sister.performance_transport;s.running=performance_running(a);
    if(a->mosaic) {
        const TsMosaic *m=a->mosaic;s.live_loops=ts_mosaic_live_count(m);s.time=m->time;s.speed=m->speed;
        s.repeat=m->repeat;s.cue_loop=m->marker_loop;s.cue=m->marker_active;s.range=m->loop_range;
    }
    s.slot=a->keyboard_sequence.active_slot;s.note=a->keyboard_sequence.current_note;
    for(int i=0;i<TS_NOTE_BANK_VOICE_CAPACITY;++i)if(a->notes.voices[i].active) {
        ++s.notes;s.held+=a->notes.voices[i].latched!=0;s.synth+=a->notes.voices[i].synth!=0;
    }
    for(int i=0;i<a->mosaic_source_performance.render_limit;++i)if(a->mosaic_source_performance.voices[i].active) {
        ++s.notes;s.held+=a->mosaic_source_performance.voices[i].latched!=0;
    }
    if(ts_tapehead_frame())ts_tapehead_host_unlock();else if(device)SDL_UnlockAudioDevice(device);
    return s;
}
static void performance_arrangement_refresh(const TsMosaic *m,double time)
{
    Uint32 now=SDL_GetTicks();
    if(!m){memset(&performance_arrangement,0,sizeof(performance_arrangement));return;}
    if(performance_arrangement.owner==m && performance_arrangement.epoch==m->epoch &&
       now-performance_arrangement.updated<250)return;
    /* Event definitions and source ownership are control-thread data. Audio
       changes private voices/scheduling only; never traverse them here. Format
       this metadata outside the audio lock, at most four times per second. */
    performance_arrangement.owner=m;performance_arrangement.epoch=m->epoch;
    performance_arrangement.revision=m->revision;performance_arrangement.updated=now;
    performance_arrangement.cards=performance_arrangement.patterns=performance_arrangement.muted=0;
    performance_arrangement.solo=performance_arrangement.here=0;performance_arrangement.end=0;
    for(int i=0;i<TS_MOSAIC_EVENTS;++i) {
        const TsMosaicEvent *e=&m->events[i];if(!e->id)continue;
        ++performance_arrangement.cards;performance_arrangement.patterns+=e->source->pattern!=NULL;
        performance_arrangement.muted+=e->muted!=0;performance_arrangement.solo+=e->solo!=0;
        performance_arrangement.here+=e->start<=time && time<e->start+e->duration;
        performance_arrangement.end=fmax(performance_arrangement.end,e->start+e->duration);
    }
}
static TsMosaicRange performance_cue_range(const TsMosaic *m,int active)
{
    if(!m || active<1 || active>TS_MOSAIC_MARKERS || !m->markers[active-1].stored)return (TsMosaicRange){0,0};
    double start=m->markers[active-1].time,end=INFINITY;
    for(int i=0;i<TS_MOSAIC_MARKERS;++i)if(m->markers[i].stored && m->markers[i].time>start && m->markers[i].time<end)end=m->markers[i].time;
    if(!isfinite(end))end=performance_arrangement.end;
    return end>start?(TsMosaicRange){start,end}:(TsMosaicRange){0,0};
}
static void performance_loop_label(const PerformanceStatus *s,const TsMosaic *m,char *out,size_t size)
{
    TsMosaicRange range=s->cue_loop?performance_cue_range(m,s->cue):s->range;
    char first[24],last[24];performance_time(first,sizeof(first),range.start);performance_time(last,sizeof(last),range.end);
    if(s->cue_loop && range.end<=range.start)snprintf(out,size,"CUE LOOP ARMED / NO ACTIVE RANGE");
    else if(s->cue_loop || (s->repeat && range.end>range.start))
        snprintf(out,size,"%s LOOP %s - %s",s->cue_loop?"CUE":"RANGE",first,last);
    else snprintf(out,size,"%s",s->repeat?"WHOLE ARRANGEMENT REPEAT":"LOOP OFF / CONTINUE FORWARD");
}
static void performance_page_set(AudioState *a,int page)
{
    if(page<0 || page>=PERF_PAGES)return;
    performance_transport.page=page;performance_transport.drag=performance_transport.input_drag=0;
    SDL_CaptureMouse(SDL_FALSE);
    if(page!=PERF_INPUTS)ts_performance_inputs_tuner(a->performance_inputs,0,0);
    performance_transport.painted=SDL_GetTicks()-100;
}
static void performance_overview(AudioState *a,const TsPerformanceMixer *m,const PerformanceStatus *s,
    const TsSisterRoutingSnapshot *r,Uint32 text,Uint32 dim,Uint32 accent,Uint32 edge)
{
    const int channel[]={2,1,0,3};const char *names[]={"MOSAIC","TRACKSISTER","ARP","LIVE TILES"};
    char line[128],position[32];
    perf_small(28,150,"PLAYER",dim);perf_small(204,150,"GROUP",dim);perf_small(282,150,"STATE / POSITION",dim);
    perf_small(464,150,"MUTE / SOLO",dim);perf_small(620,150,"TRANSPORT",dim);perf_small(780,150,"PRE-ROUTE TRIM",dim);
    for(int row=0;row<4;++row) {
        int ch=channel[row],y=PERF_ROWS_Y+row*PERF_ROW_STEP;unsigned bit=1u<<ch;
        perf_rect(20,y,920,54,edge);if(performance_transport.selected==row)perf_rect(20,y,3,54,accent);
        perf_text(28,y+10,names[row],text,2);
        if(ch<3) {
            perf_rect(206,y+12,12,12,s->selected&bit?accent:dim);perf_small(226,y+12,s->selected&bit?"IN":"OUT",text);
            perf_text(282,y+8,s->running&bit?"PLAYING":"STOPPED",s->running&bit?accent:dim,2);
            perf_button(620,y+10,76,32,s->running&bit?(ch?"PAUSE":"STOP"):"PLAY",PERF_BUTTON_ROW_PLAY+ch,(s->running&bit)!=0,1);
            perf_button(704,y+10,56,32,"HOME",PERF_BUTTON_ROW_HOME+ch,0,0);
        } else {
            perf_small(208,y+13,"--",dim);perf_text(282,y+8,"MANUAL",text,2);perf_small(620,y+20,"QWERTY / MIDI",dim);
        }
        if(ch==2) {
            performance_time(position,sizeof(position),s->time);snprintf(line,sizeof(line),"%s / %.2fX",position,s->speed);
            perf_small(282,y+34,line,dim);snprintf(line,sizeof(line),"M %d / S %d",performance_arrangement.muted,performance_arrangement.solo);
            perf_small(464,y+10,line,text);snprintf(line,sizeof(line),s->live_loops?"%d LIVE LOOPS":"%d CARDS HERE",s->live_loops?s->live_loops:performance_arrangement.here);perf_small(464,y+34,line,dim);
        } else if(ch==1) {
            if(s->tracker.ready)snprintf(line,sizeof(line),"P%02d / ROW %03d",s->tracker.pattern,s->tracker.row);else snprintf(line,sizeof(line),"NOT OPEN");
            perf_small(282,y+34,line,dim);snprintf(line,sizeof(line),"SCORE M %u",performance_popcount(s->tracker.score_mutes));perf_small(464,y+10,line,text);
            if(s->tracker.performance_solo>=0)snprintf(line,sizeof(line),"PERF SOLO %02d",s->tracker.performance_solo+1);
            else snprintf(line,sizeof(line),"PERF M %u",performance_popcount(s->tracker.performance_mutes));perf_small(464,y+34,line,dim);
        } else if(ch==0) {
            snprintf(line,sizeof(line),"SLOT %02d / NOTE %d",s->slot+1,s->note);perf_small(282,y+34,line,dim);
            perf_small(464,y+10,"NOTE SEQUENCE",text);perf_small(464,y+34,"OWN VOICES",dim);
        } else {
            snprintf(line,sizeof(line),"KEY VOICES %d",s->notes);perf_small(282,y+34,line,dim);
            snprintf(line,sizeof(line),"LATCH %d / FM %d",s->held,s->synth);perf_small(464,y+10,line,text);perf_small(464,y+34,"+ TILE LAUNCHERS",dim);
        }
        int x=PERF_GAIN_X+(int)lrintf(m->gain[ch]*PERF_GAIN_W/2);
        perf_rect(PERF_GAIN_X,y+20,PERF_GAIN_W,5,dim);perf_rect(PERF_GAIN_X,y+20,x-PERF_GAIN_X,5,accent);
        perf_rect(PERF_GAIN_X+PERF_GAIN_W/2,y+16,1,14,text);perf_rect(x-2,y+15,5,16,text);
        if(m->gain[ch]>0)snprintf(line,sizeof(line),"%+.1f",20*log10f(m->gain[ch]));else snprintf(line,sizeof(line),"-INF");
        perf_small(898,y+18,line,text);if(m->gain[ch]==0)perf_small(780,y+37,"TRIM SILENT",accent);
    }
    if(performance_transport.selected==0) {
        const TsMosaic *mosaic=a->mosaic;
        if(mosaic && s->cue>0 && s->cue<=16 && mosaic->markers[s->cue-1].stored)
            snprintf(line,sizeof(line),"TRIGGERED CUE %02d / %.32s",s->cue,mosaic->markers[s->cue-1].name);
        else snprintf(line,sizeof(line),"NO TRIGGERED CUE / %d CARDS / %d PATTERNS",performance_arrangement.cards,performance_arrangement.patterns);
        perf_small(28,426,line,accent);performance_loop_label(s,mosaic,line,sizeof(line));perf_small(28,447,line,text);
    } else if(performance_transport.selected==1) {
        snprintf(line,sizeof(line),"TRACKER ORDER %02d / %d BPM (SCORE %d) / %d TICKS / BLOCK LOOP %s",s->tracker.order,s->tracker.bpm,s->tracker.score_bpm,s->tracker.ticks_per_row,s->tracker.block_loop?"ON":"OFF");perf_small(28,426,line,accent);
        perf_small(28,447,"SCORE MUTES AND LIVE PERFORMANCE MUTES ARE SEPARATE",text);
    } else if(performance_transport.selected==2) {
        const char *clocks[]={"LOCAL NOTE INTERVALS","TRACKSISTER 1/4","TRACKSISTER 1/8","TRACKSISTER 1/16"};
        if(a->sister.clock_controls.enabled && (a->sister.clock_controls.tempo_mask&TS_CLOCK_ARP))
            snprintf(line,sizeof(line),"ARP CLOCK: MASTER 1/%u / SLOT TIMER AND LFO: LOCAL",4u<<a->sister.clock_controls.arp_division);
        else snprintf(line,sizeof(line),"ARP CLOCK: %s / SLOT TIMER AND LFO: LOCAL",clocks[m->arp_sync%4]);
        perf_small(28,426,line,accent);
        perf_button(28,447,300,30,"LOCAL / TRACKER CLOCK",PERF_BUTTON_CLOCK,m->arp_sync!=0,0);
    } else {
        perf_small(28,426,"MANUAL NOTES / LATCHES / FM / TILE LAUNCHERS",accent);
        perf_small(28,447,"INDEPENDENT OF GROUP PAUSE / INPUT MONITOR IS A SEPARATE FEED",text);
    }
    snprintf(line,sizeof(line),"SISTER %s / %s / MONITOR %s",r->enabled?"ON":"OFF",r->held?"HOLD":r->rolling?"ROLLING":"STOPPED",r->monitor_enabled?"ON":"OFF");perf_small(28,488,line,text);
    snprintf(line,sizeof(line),"PRISM %s / ROUTER SEQ %s / INSERT %s",a->sister.parameters.prism.enabled?"ON":"OFF",r->router_view.running?"RUN":"STOP",r->insert_inputs && r->insert_outputs?"READY":"UNAVAILABLE");perf_small(28,510,line,dim);
    if(r->fx_transition_active || r->fx_master_transition_active)perf_small(620,488,"PEDALBOARD FADING",accent);
    else perf_small(620,488,a->sister.parameters.fx.enabled?"PEDALBOARD ON":"PEDALBOARD OFF",dim);
    perf_small(620,510,a->sister.parameters.fx.fallout.enabled?"FALLOUT ON":"FALLOUT OFF",dim);
}
static void performance_cues(AudioState *a,const PerformanceStatus *s,Uint32 text,Uint32 dim)
{
    const TsMosaic *m=a->mosaic;char line[128],position[32];performance_time(position,sizeof(position),s->time);
    snprintf(line,sizeof(line),"MOSAIC CUES / IMMEDIATE SEEK / %s",position);perf_small(28,154,line,text);
    for(int i=0;i<16;++i) {
        int x=20+(i%8)*116,y=184+(i/8)*82,stored=m && m->markers[i].stored;
        snprintf(line,sizeof(line),"%02d %.7s",i+1,stored?m->markers[i].name:"EMPTY");
        perf_button(x,y,108,68,line,PERF_BUTTON_CUE+i,s->cue==i+1,stored);
        if(stored){performance_time(position,sizeof(position),m->markers[i].time);perf_small(x+8,y+47,position,dim);}
    }
    perf_button(28,370,250,34,s->cue_loop?"CUE LOOP ON":"CUE LOOP OFF",PERF_BUTTON_LOOP,s->cue_loop,0);
    performance_loop_label(s,m,line,sizeof(line));perf_small(28,424,line,text);
    perf_small(28,454,"LOOP RELEASE CONTINUES FORWARD / CUES DO NOT MOVE OTHER PLAYERS",dim);
    perf_small(28,484,"MOSAIC TIME AND TAPE SPEED REMAIN INDEPENDENT",dim);
}
static void performance_visuals(AudioState *a,Uint32 dim)
{
    const TsMandalaConfig *v=&a->sister.mandala;char line[64];
    perf_button(20,180,180,34,ts_mandala_active()?"CLOSE VISUALIZER":"VISUALIZE",PERF_BUTTON_VISUAL,ts_mandala_active(),1);
    perf_button(210,180,180,34,ts_mandala_frozen()?"RESUME VISUAL":"FREEZE VISUAL",PERF_BUTTON_FREEZE,ts_mandala_active() && ts_mandala_frozen(),0);
    perf_button(400,180,180,34,"NEW SEED",PERF_BUTTON_SEED,0,0);
    perf_button(20,232,180,34,"EDIT VISUALIZER",PERF_BUTTON_EDITOR,mandala_editor.visible,0);
    snprintf(line,sizeof(line),"SYMMETRY %u",v->symmetry);perf_button(210,232,180,34,line,PERF_BUTTON_SYMMETRY,0,0);
    snprintf(line,sizeof(line),"EVOLUTION %u:%02u",v->cycle_seconds/60,v->cycle_seconds%60);perf_button(400,232,180,34,line,PERF_BUTTON_EVOLUTION,0,0);
    perf_small(28,310,"VISUAL WINDOW: F FULLSCREEN / D DISPLAY / Q QUALITY / ESC EXIT",dim);
    perf_small(28,340,"FREEZE AFFECTS THE VISUAL / AUDIO TRANSPORT REMAINS INDEPENDENT",dim);
}
static void performance_update(SDL_AudioDeviceID device,AudioState *audio)
{
    ts_mandala_follow_effects(&audio->sister);
    if(!performance_transport.visible || !window_drawable(performance_transport.window) || SDL_GetTicks()-performance_transport.painted<50)return;
    PerformanceStatus s=performance_status(device,audio);
    TsPerformanceMixer m=audio->sister.performance_mixer; /* UI-owned project controls. */
    if(performance_transport.page==PERF_OVERVIEW || performance_transport.page==PERF_CUES)
        performance_arrangement_refresh(audio->mosaic,s.time);
    TsSisterRoutingSnapshot r={0};ts_sister_runtime_get_snapshot(&audio->sister,&r);
    TsRealtimeDiagnosticsSnapshot d={0};ts_realtime_diagnostics_get(&audio->realtime_diagnostics,&d);
    Uint32 text=ts_theme_color(&performance_palette,TS_THEME_TRANSPORT,TS_COLOR_TEXT,0xffdbe8e3u);
    Uint32 dim=ts_theme_color(&performance_palette,TS_THEME_TRANSPORT,TS_COLOR_DIM,0xff94aaa7u);
    Uint32 accent=ts_theme_color(&performance_palette,TS_THEME_TRANSPORT,TS_COLOR_DATA4,0xff8addb4u);
    Uint32 edge=ts_theme_color(&performance_palette,TS_THEME_TRANSPORT,TS_COLOR_EDGE,0xff202b30u);
    perf_rect(0,0,PERF_W,PERF_H,ts_theme_color(&performance_palette,TS_THEME_TRANSPORT,TS_COLOR_PANEL,0xff151c20u));
    perf_text(22,20,"TAPESISTER / CONTROL CENTER",accent,2);
    perf_rect(786,20,12,12,m.always_on_top?accent:dim);perf_small(808,20,"ON TOP",text);
    const char *commands[]={"PLAY GROUP","PAUSE GROUP","RETURN TO START"};
    for(int i=0;i<3;++i)perf_button(20+i*190,54,180,34,commands[i],PERF_BUTTON_PLAY+i,0,1);
    char line[160];snprintf(line,sizeof(line),"%u JOINED / %u RUNNING",performance_popcount(s.selected),performance_popcount(s.running&s.selected));perf_small(606,64,line,dim);
    const char *tabs[]={"OVERVIEW","CUES","INPUTS / TUNER","VISUALS","CLOCK ROUTING"};
    for(int i=0;i<PERF_PAGES;++i)perf_button(20+i*PERF_TAB_STEP,104,PERF_TAB_W,30,tabs[i],PERF_BUTTON_TAB+i,performance_transport.page==i,0);
    const TsMasterClockControls *clock=&audio->sister.clock_controls;
    if(clock->enabled) {
        double bpm=s.master_clock.quarter_seconds>0?60/s.master_clock.quarter_seconds:clock->bpm;
        snprintf(line,sizeof(line),"MASTER %.2f BPM / %s / MOSAIC TIME REMAINS FREE",bpm,s.master_clock.running?"RUNNING":"PAUSED");perf_small(28,38,line,dim);
    }
    if(performance_transport.page==PERF_OVERVIEW)performance_overview(audio,&m,&s,&r,text,dim,accent,edge);
    else if(performance_transport.page==PERF_CUES)performance_cues(audio,&s,text,dim);
    else if(performance_transport.page==PERF_INPUTS)performance_inputs_paint(&m);
    else if(performance_transport.page==PERF_VISUALS)performance_visuals(audio,dim);
    else performance_clock_paint(clock,&s.master_clock,&s.tracker,s.selected,m.arp_sync,text,dim,accent,edge);
    perf_rect(20,542,920,1,dim);
    for(int ch=0;ch<2;++ch) {
        float level=isfinite(r.output_level[ch])?fmaxf(0,r.output_level[ch]):0;
        float db=level>0?20*log10f(level):-100;
        perf_rect(20,555+ch*14,140,6,edge);perf_rect(20,555+ch*14,(int)(140*fmaxf(0,fminf(1,(db+60)/60))),6,accent);
    }
    snprintf(line,sizeof(line),"MAIN TAP / LIMITER %s / %s",r.limiter_enabled?"ON":"OFF",audio->sister.matrix.controls.enabled?(ts_matrix_main_clip(&audio->sister.matrix.controls)?"MATRIX LEGACY CLIP":"MATRIX FLOAT"):"LEGACY ROUTER");perf_small(178,550,line,text);
    Uint64 frequency=SDL_GetPerformanceFrequency(),now=SDL_GetPerformanceCounter();
    int active=d.completed_ticks && frequency && now>=d.completed_ticks && now-d.completed_ticks<frequency/2;
    if(d.callback_count)snprintf(line,sizeof(line),"%u HZ / %u FRAMES / CALLBACKS %s / LOAD %.1f%% / OVERRUNS %llu",d.sample_rate,d.device_buffer_frames,active?"ACTIVE":"NOT RECENT",d.recent_load_percent,(unsigned long long)d.deadline_overruns);
    else snprintf(line,sizeof(line),"%d HZ / WAITING FOR CALLBACK TIMING DATA",audio->output_rate);
    perf_small(178,572,line,dim);
    perf_button(20,604,160,28,"F9 ROUTING",100,0,0);perf_button(190,604,160,28,"F12 HEALTH",101,0,0);
    perf_button(360,604,180,28,offline_job.active?"RENDER PROGRESS":"RENDER OFFLINE",PERF_BUTTON_RENDER,offline_job.active,0);
    snprintf(line,sizeof(line),"LIVE LINK %s / REC %s",r.live_link_available?"ON":"OFF",audio->sister_file_recorder && ts_performance_recorder_state(audio->sister_file_recorder)==TS_PERFORMANCE_FILE_RECORDING?"ON":"OFF");perf_small(560,612,line,dim);
    SDL_SetWindowAlwaysOnTop(performance_transport.window,m.always_on_top?SDL_TRUE:SDL_FALSE);
    perf_present();performance_transport.painted=SDL_GetTicks();
}
