/* Called only under the playback-device lock. */
static void router_sync(AudioState *audio,TsUiState *ui)
{
    TsRouter *r=&audio->sister.router;
    ui->config.router=ts_router_export(r);ui->router_live=r->controls;
    ui->config.router_performance=r->performance;ui->router_view=ts_router_view(r);
    ts_sister_runtime_publish_router(&audio->sister);
}
static void router_manual(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,int stage,int solo)
{
    if(device)SDL_LockAudioDevice(device);
    ts_router_manual(&audio->sister.router,stage,solo);router_sync(audio,ui);
    if(device)SDL_UnlockAudioDevice(device);
    snprintf(ui->status,sizeof(ui->status),ui->router_view.running?"LIVE OVERRIDE UNTIL NEXT STEP / TIMERS CANCELLED":"MANUAL ROUTER / TIMERS CANCELLED");
}
static float router_time_edit(float current,float maximum,int x,int origin,int width,int wheel,int right)
{
    float result;
    if(wheel)result=current*powf(SDL_GetModState()&KMOD_SHIFT?1.01f:1.1f,(float)wheel);
    else if(right)result=8;
    else result=TS_ROUTER_TIME_MIN*powf(maximum/TS_ROUTER_TIME_MIN,SDL_clamp((float)(x-origin)/(width-1),0,1));
    return SDL_clamp(result,TS_ROUTER_TIME_MIN,maximum);
}
static void router_arm(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,int stage,int kind)
{
    if(device)SDL_LockAudioDevice(device);
    ts_router_timer_start(&audio->sister.router,stage,kind);router_sync(audio,ui);
    if(device)SDL_UnlockAudioDevice(device);
    ui->router_timer_stage=stage;
    snprintf(ui->status,sizeof(ui->status),"%s %s %s %.2FS / RIGHT-CLICK RETRIGGERS / PERFORMANCE: CANCEL",
        ts_router_name(stage),kind==TS_ROUTER_TIMER_SOLO?"SOLO":"BYPASS",
        ui->config.router_performance.timer_after[stage]?"AFTER":"FOR",ui->config.router_performance.timer_seconds[stage]);
}
static int router_performance_mouse(const SDL_Event *e,int x,int y,int wheel,
    SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui)
{
    if(e->type!=SDL_MOUSEBUTTONDOWN && e->type!=SDL_MOUSEWHEEL)return y<382;
    if(y<32){ui->router_open=ui->router_performance_open=0;return 0;}
    if(!wheel && y>=42 && y<65 && x>=506 && x<618){ui->router_performance_open=0;return 1;}
    if(x<22 || x>=616 || y<74 || y>=357)return y<382;
    int right=e->type==SDL_MOUSEBUTTONDOWN && e->button.button==SDL_BUTTON_RIGHT;
    int middle=e->type==SDL_MOUSEBUTTONDOWN && e->button.button==SDL_BUTTON_MIDDLE;
    int change=wheel?wheel:right?-1:1;
    if(e->type==SDL_MOUSEWHEEL && (!wheel || !ts_ui_wheel_guard_accept(&ui->wheel_guard,
        WHEEL_TARGET_SISTER+0x400+(y/24)*20+x/36,SDL_GetTicks())))return 1;
    if(device)SDL_LockAudioDevice(device);
    TsRouter *r=&audio->sister.router;TsRouterPerformance p=r->performance;
    int edit=0;
    if(y<124) {
        int row=(y-74)/26,col=(x-22)/45;
        if(row<2 && col<13 && !wheel)ui->router_slot=row*13+col;
    } else if(y>=129 && y<152 && !wheel) {
        if(x<94){ts_router_store(r,ui->router_slot);snprintf(ui->status,sizeof(ui->status),"ROUTER STATE %c STORED / PARTICIPATION ONLY",'A'+ui->router_slot);}
        else if(x>=100 && x<178){int ok=ts_router_recall(r,ui->router_slot);snprintf(ui->status,sizeof(ui->status),ok?"ROUTER STATE RECALLED / TIMERS CANCELLED / SEQUENCE KEEPS RUNNING":"EMPTY ROUTER STATE / STORE FIRST");}
        else if(x>=184 && x<250){r->performance.occupied&=~(1u<<ui->router_slot);memset(&r->performance.state[ui->router_slot],0,sizeof(TsRouterState));}
    } else if(y>=162 && y<185) {
        if(x<86 && !wheel)ts_router_sequence_play(r);
        else if(x>=92 && x<156 && !wheel)ts_router_sequence_stop(r);
        else if(x>=162 && x<230 && !wheel)ts_router_sequence_reset(r);
        else if(x>=236 && x<320 && !wheel)ts_router_sequence_restore(r);
        else if(x>=326 && x<400 && !wheel){p.loop=!p.loop;edit=1;}
        else if(x>=406 && x<502){p.length=SDL_clamp(p.length+change,1,TS_ROUTER_STEPS);edit=1;}
    } else if(y>=206 && y<286 && x<598) {
        int cell=((y-206)/20)*16+(x-22)/36;
        if(cell<TS_ROUTER_STEPS) {
            ui->router_step=cell;
            if(!wheel && !right && !middle && (SDL_GetModState()&KMOD_SHIFT)){p.length=cell+1;edit=1;}
            else if(wheel || right || middle) {
                p.step[cell].state=middle?-1:(p.step[cell].state+1+change%27+27)%27-1;edit=1;
            }
        }
    } else if(y>=292 && y<315) {
        int step=ui->router_step;
        if(x>=110 && x<222){p.step[step].state=middle?-1:(p.step[step].state+1+change%27+27)%27-1;edit=1;}
        else if(x>=230 && x<414){p.step[step].seconds=router_time_edit(p.step[step].seconds,p.max_seconds,x,230,184,wheel,right);edit=1;}
        else if(x>=424 && x<616 && !wheel){p.step[step].state=ui->router_slot;edit=1;}
    } else if(y>=333 && y<356) {
        int stage=ui->router_timer_stage;
        if(x<154)ui->router_timer_stage=(stage+change%TS_ROUTER_COUNT+TS_ROUTER_COUNT)%TS_ROUTER_COUNT;
        else if(x>=160 && x<220 && !wheel){p.timer_after[stage]=!p.timer_after[stage];edit=1;}
        else if(x>=226 && x<362){p.timer_seconds[stage]=router_time_edit(p.timer_seconds[stage],p.max_seconds,x,226,136,wheel,right);edit=1;}
        else if(x>=368 && x<438 && !wheel)ts_router_timer_start(r,stage,TS_ROUTER_TIMER_BYPASS);
        else if(x>=444 && x<514 && !wheel)ts_router_timer_start(r,stage,TS_ROUTER_TIMER_SOLO);
        else if(x>=520 && !wheel)ts_router_timer_cancel(r,stage);
    }
    if(edit)ts_router_performance_set(r,&p);
    router_sync(audio,ui);
    if(device)SDL_UnlockAudioDevice(device);
    return 1;
}

static int router_midi_command(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,const char *target)
{
    if(strncmp(target,"router.",7))return 0;
    int stage=-1,kind=0;char action[16],extra;
    if(device)SDL_LockAudioDevice(device);
    TsRouter *r=&audio->sister.router;int handled=1;
    if(sscanf(target,"router.stage.%d.%15[a-z]%c",&stage,action,&extra)==2 && stage>=0 && stage<TS_ROUTER_COUNT) {
        if(!strcmp(action,"bypass"))ts_router_manual(r,stage,0);
        else if(!strcmp(action,"solo"))ts_router_manual(r,stage,1);
        else handled=0;
    } else if(!strncmp(target,"router.state.",13) && target[13]>='A' && target[13]<='Z' && !strcmp(target+14,".recall")) {
        ts_router_recall(r,target[13]-'A');
    } else if(sscanf(target,"router.timer.%d.%15[a-z]%c",&stage,action,&extra)==2 && stage>=0 && stage<TS_ROUTER_COUNT) {
        if(!strcmp(action,"bypass"))kind=TS_ROUTER_TIMER_BYPASS;
        else if(!strcmp(action,"solo"))kind=TS_ROUTER_TIMER_SOLO;
        else if(!strcmp(action,"cancel"))ts_router_timer_cancel(r,stage);
        else handled=0;
        if(kind)ts_router_timer_start(r,stage,kind);
    } else if(!strcmp(target,"router.play"))ts_router_sequence_play(r);
    else if(!strcmp(target,"router.stop"))ts_router_sequence_stop(r);
    else if(!strcmp(target,"router.reset"))ts_router_sequence_reset(r);
    else if(!strcmp(target,"router.restore"))ts_router_sequence_restore(r);
    else if(!strcmp(target,"router.loop"))r->performance.loop=!r->performance.loop;
    else handled=0;
    if(handled)router_sync(audio,ui);
    if(device)SDL_UnlockAudioDevice(device);
    return handled;
}
