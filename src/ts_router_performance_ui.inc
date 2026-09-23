static void router_time_label(char *label,size_t size,float seconds)
{ ts_prism_time_label(label,size,seconds); }
static void router_time_field(TsFramebuffer *fb,int x,int y,int w,const char *prefix,float seconds,float maximum)
{
    char time[24],label[48];router_time_label(time,sizeof(time),seconds);
    snprintf(label,sizeof(label),"%s %s",prefix,time);button(fb,x,y,w,label,0);
    float amount=maximum>TS_ROUTER_TIME_MIN?logf(fmaxf(seconds,TS_ROUTER_TIME_MIN)/TS_ROUTER_TIME_MIN)/logf(maximum/TS_ROUTER_TIME_MIN):1;
    rect(fb,x+3,y+20,(int)((w-6)*fminf(1,fmaxf(0,amount))),2,RGB(107,176,182));
}
static const char *router_performance_help(int x,int y)
{
    if(y>=74 && y<124)return "SELECT A ROUTER STATE; STORE CAPTURES BYPASS/SOLO ONLY; RECALL OVERRIDES UNTIL NEXT STEP";
    if(y>=129 && y<152)return "STORE: CURRENT AUDIBLE PARTICIPATION / RECALL: LIVE OVERRIDE / CLEAR: EMPTY SLOT";
    if(y>=162 && y<185) {
        if(x<86)return "PLAY FROM STEP 01; CAPTURE MANUAL BASE STATE; TIMERS CONTINUE";
        if(x<156)return "STOP SEQUENCE; HOLD CURRENT STATE; TEMPORARY TIMERS CONTINUE";
        if(x<230)return "RESET TO STEP 01; KEEP PLAYING IF RUNNING; PRESERVE PRE-SEQUENCE RESTORE POINT";
        if(x<320)return "RESTORE PRE-SEQUENCE PARTICIPATION; CANCEL SEQUENCE AND TIMERS; KEEP ROUTING ORDER";
        if(x<400)return "LOOP ON/OFF; OFF STOPS AFTER THE LAST STEP AND HOLDS ITS STATE";
        return "SEQUENCE LENGTH: CLICK/WHEEL +1; RIGHT-CLICK -1; CHANGES APPLY AT NEXT BOUNDARY";
    }
    if(y>=206 && y<286)return "CLICK: SELECT STEP / WHEEL OR RIGHT-CLICK: STATE / MIDDLE: HOLD / SHIFT-CLICK: END";
    if(y>=292 && y<315) {
        if(x<222)return "STEP STATE: CLICK/WHEEL A-Z / RIGHT-CLICK BACK / MIDDLE: HOLD / EMPTY SLOT: HOLD";
        if(x<414)return "STEP TIME: CLICK TO SET / WHEEL: ADJUST / SHIFT-WHEEL: FINE / RIGHT: 8S / NEXT VISIT";
        return "ASSIGN SELECTED ROUTER STATE TO THIS STEP; RUNNING STEP STAYS FROZEN UNTIL NEXT VISIT";
    }
    if(y>=333 && y<356) {
        if(x<154)return "SELECT TIMER STAGE; EACH STAGE REMEMBERS ITS OWN TIME AND FOR/AFTER MODE";
        if(x<220)return "FOR: TEMPORARY THEN RESTORE / AFTER: WAIT THEN SET BYPASS OR SOLO";
        if(x<362)return "TIMER TIME: CLICK / WHEEL / SHIFT-WHEEL FINE / RIGHT RESET; NEW TRIGGERS ONLY";
        if(x<514)return "TRIGGER BYPASS OR SOLO; RETRIGGER REPLACES THIS STAGE'S TIMER; OTHER TIMERS CONTINUE";
        return "CANCEL SELECTED TIMER AND REMOVE ITS TEMPORARY ACTION; REVEAL UNDERLYING STATE";
    }
    return "ROUTER STATES A-Z / AUDIO-CLOCK TIMING / FIXED 10MS PARTICIPATION CROSSFADE";
}
static void router_performance_render(TsFramebuffer *fb,const TsUiState *ui)
{
    const TsRouterPerformance *p=&ui->config.router_performance;const TsRouterView *v=&ui->router_view;
    const uint32_t teal=RGB(107,176,182),amber=RGB(247,196,91),muted=RGB(123,127,133),dim=RGB(60,65,72);
    char label[100],time[24];rect(fb,10,38,620,344,RGB(22,25,31));rect(fb,10,38,620,2,teal);
    text(fb,22,48,"ROUTER PERFORMANCE",amber,1);
    text(fb,270,48,"PARTICIPATION STATES A-Z",teal,1);button(fb,506,42,112,"< ROUTER",0);
    for(int row=0;row<TS_ROUTER_COUNT;++row) {
        int stage=ui->router_live.order[row],active=ts_router_active(&ui->router_live,stage);
        char marker=ui->router_live.solo==stage+1?'S':!active?'-':!(ui->router_enabled&(1u<<stage))?'O':'+';
        snprintf(label,sizeof(label),"%c %.11s",marker,ts_router_name(stage));
        text(fb,22+row*118,64,label,marker=='S'?amber:marker=='+'?teal:muted,1);
        router_activity(fb,104+row*118,66,ui->router_peaks[stage*2]);
    }
    for(int slot=0;slot<TS_ROUTER_STATES;++slot) {
        int x=22+(slot%13)*45,y=74+(slot/13)*26,occupied=(p->occupied&(1u<<slot))!=0;
        rect(fb,x,y,41,23,slot==ui->router_slot?amber:occupied?teal:dim);
        rect(fb,x+1,y+1,39,21,slot==ui->router_slot?RGB(63,51,30):RGB(27,33,41));
        snprintf(label,sizeof(label),"%c %s",'A'+slot,occupied?"+":"-");text(fb,x+6,y+8,label,occupied?teal:muted,1);
    }
    button(fb,22,129,72,"STORE",0);button(fb,100,129,78,"RECALL",0);button(fb,184,129,66,"CLEAR",0);
    TsRouterState s=p->state[ui->router_slot];
    if(!(p->occupied&(1u<<ui->router_slot)))snprintf(label,sizeof(label),"%c EMPTY / STORE CURRENT ROUTER",'A'+ui->router_slot);
    else if(s.solo)snprintf(label,sizeof(label),"%c SOLO %s",'A'+ui->router_slot,ts_router_name(s.solo-1));
    else snprintf(label,sizeof(label),"%c BYPASS: P%d S%d F%d B%d I%d",'A'+ui->router_slot,
        !!(s.bypass_mask&1),!!(s.bypass_mask&2),!!(s.bypass_mask&4),!!(s.bypass_mask&8),!!(s.bypass_mask&16));
    text(fb,264,137,label,teal,1);
    button(fb,22,162,64,"PLAY",v->running);button(fb,92,162,64,"STOP",0);
    button(fb,162,162,68,"RESET",0);button(fb,236,162,84,"RESTORE",v->restore_valid);
    button(fb,326,162,74,"LOOP",p->loop);snprintf(label,sizeof(label),"LENGTH %02d",p->length);button(fb,406,162,96,label,0);
    text(fb,518,170,v->running?"RUNNING":"STOPPED",v->running?teal:muted,1);
    router_time_label(time,sizeof(time),v->remaining);
    if(v->running)snprintf(label,sizeof(label),"STEP %02d / %c / %s%s",v->step+1,v->state<0?'-':'A'+v->state,time,v->missing?" / EMPTY: HOLD":"");
    else snprintf(label,sizeof(label),"64 STEPS / -: HOLD / X: EMPTY / STOP HOLDS STATE");
    text(fb,22,193,label,teal,1);text(fb,466,193,v->manual_override?"LIVE OVERRIDE":"TRANSITION 10MS",v->manual_override?amber:muted,1);
    for(int cell=0;cell<TS_ROUTER_STEPS;++cell) {
        int x=22+(cell%16)*36,y=206+(cell/16)*20,included=cell<p->length;
        int state=p->step[cell].state,missing=state>=0 && !(p->occupied&(1u<<state));
        int playing=v->running && v->step==cell;
        rect(fb,x,y,33,18,playing?teal:cell==ui->router_step?amber:dim);
        rect(fb,x+1,y+1,31,16,playing?RGB(29,65,70):RGB(23,28,34));
        snprintf(label,sizeof(label),"%02d%c",cell+1,state<0?'-':missing?'X':'A'+state);
        text(fb,x+3,y+5,label,!included?dim:missing?amber:teal,1);
        if(missing && included)rect(fb,x+28,y+2,3,3,amber);
        if(cell==p->length-1)rect(fb,x+1,y+16,31,2,amber);
    }
    int step=ui->router_step;snprintf(label,sizeof(label),"STEP %02d",step+1);text(fb,22,300,label,amber,1);
    snprintf(label,sizeof(label),"STATE %c",p->step[step].state<0?'-':'A'+p->step[step].state);button(fb,110,292,112,label,0);
    router_time_field(fb,230,292,184,"STEP TIME",p->step[step].seconds,p->max_seconds);
    snprintf(label,sizeof(label),"USE SELECTED %c",'A'+ui->router_slot);button(fb,424,292,192,label,0);
    int stage=ui->router_timer_stage;
    if(v->timer_kind[stage]) {
        router_time_label(time,sizeof(time),v->timer_remaining[stage]);
        snprintf(label,sizeof(label),"TIMER: %s %s %s / %s",ts_router_name(stage),v->timer_kind[stage]==TS_ROUTER_TIMER_SOLO?"SOLO":"BYPASS",
            v->timer_after[stage]?"ARMED":"RUNNING",time);
    } else snprintf(label,sizeof(label),"TIMED ACTION / GRAPH: RIGHT-CLICK BYPASS OR SOLO TO TRIGGER");
    text(fb,22,322,label,v->timer_kind[stage]?amber:muted,1);
    button(fb,22,333,132,ts_router_name(stage),0);button(fb,160,333,60,p->timer_after[stage]?"AFTER":"FOR",0);
    router_time_field(fb,226,333,136,"TIME",p->timer_seconds[stage],p->max_seconds);
    button(fb,368,333,70,"BYPASS",v->timer_kind[stage]==TS_ROUTER_TIMER_BYPASS);
    button(fb,444,333,70,"SOLO",v->timer_kind[stage]==TS_ROUTER_TIMER_SOLO);button(fb,520,333,96,"CANCEL",0);
    snprintf(label,sizeof(label),"%.98s",ui->performance_tooltip[0]?ui->performance_tooltip:router_performance_help(-1,-1));
    text(fb,22,366,label,ui->performance_tooltip[0]?amber:muted,1);
}

/* Stable IDs, never visual row numbers, form persistent MIDI targets. */
static int router_midi_target(const TsUiState *ui,int x,int y,char *target,size_t size)
{
    if(ui->insert_open) {
        if(y>=326 && y<349 && x>=22 && x<198)
            snprintf(target,size,"router.stage.%d.%s",TS_ROUTER_INSERT,x<114?"bypass":"solo");
    } else if(!ui->router_performance_open) {
        if(y>=108 && y<318 && x>=414 && x<600)
            snprintf(target,size,"router.stage.%d.%s",ui->config.router.order[(y-108)/42],x<502?"bypass":"solo");
    } else {
        if(y>=74 && y<124 && x>=22 && x<607) {
            int slot=((y-74)/26)*13+(x-22)/45;
            snprintf(target,size,"router.state.%c.recall",'A'+slot);
        } else if(y>=162 && y<185) {
            const char *action=x>=22 && x<86?"play":x>=92 && x<156?"stop":x>=162 && x<230?"reset":
                x>=236 && x<320?"restore":x>=326 && x<400?"loop":NULL;
            if(action)snprintf(target,size,"router.%s",action);
        } else if(y>=333 && y<356) {
            const char *action=x>=368 && x<438?"bypass":x>=444 && x<514?"solo":x>=520 && x<616?"cancel":NULL;
            if(action)snprintf(target,size,"router.timer.%d.%s",ui->router_timer_stage,action);
        } else if(y>=129 && y<152 && x>=100 && x<178)snprintf(target,size,"router.state.%c.recall",'A'+ui->router_slot);
    }
    return target[0]!=0;
}
static void router_learn_button(TsFramebuffer *fb,const TsUiState *ui,int x,int y,int w)
{
    char target[TS_MIDI_TARGET_ID_MAX]={0};
    if(router_midi_target(ui,x+4,y+4,target,sizeof(target)))
        midi_learn_tint(fb,x,y,w,23,midi_learn_target_state(&ui->config.midi_map,ui->midi_learn_pending,target));
}
static void router_learn_overlay(TsFramebuffer *fb,const TsUiState *ui)
{
    if(!ui->midi_learn_active)return;
    if(ui->insert_open) {
        router_learn_button(fb,ui,22,326,92);router_learn_button(fb,ui,128,326,70);
    } else if(ui->router_performance_open) {
        for(int i=0;i<26;++i)router_learn_button(fb,ui,22+(i%13)*45,74+(i/13)*26,41);
        const int buttons[][3]={{100,129,78},{22,162,64},{92,162,64},{162,162,68},{236,162,84},{326,162,74},
            {368,333,70},{444,333,70},{520,333,96}};
        for(size_t i=0;i<sizeof(buttons)/sizeof(*buttons);++i)router_learn_button(fb,ui,buttons[i][0],buttons[i][1],buttons[i][2]);
    } else for(int row=0;row<TS_ROUTER_COUNT;++row) {
        router_learn_button(fb,ui,414,116+42*row,88);router_learn_button(fb,ui,512,116+42*row,88);
    }
}
