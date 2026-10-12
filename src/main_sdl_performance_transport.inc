/* Modeless three-channel mixer. Group edits publish at one callback boundary. */
enum { PERF_ARP=1,PERF_TRACKER=2,PERF_MOSAIC=4,PERF_W=600,PERF_H=386 };
static struct {SDL_Window *window;SDL_Renderer *renderer;Uint32 id,painted;int visible,drag;} performance_transport;
static unsigned performance_running(AudioState *audio)
{
    return (ts_keyboard_sequence_active(&audio->keyboard_sequence)?PERF_ARP:0)|
        (ts_tapehead_running()?PERF_TRACKER:0)|(audio->mosaic && audio->mosaic->playing?PERF_MOSAIC:0);
}
static int performance_command_for(unsigned selected,int command,int restart,SDL_Window *window,SDL_AudioDeviceID device,AudioState *audio,
    TsUiState *ui,TsSamplePages *pages,const TsInstrument *bank,const TsSample *fm,int rate)
{
    selected&=7u;
    if(!selected){snprintf(ui->status,sizeof(ui->status),"PERFORMANCE TRANSPORT: SELECT AT LEAST ONE PLAYER.");return 0;}
    if(command<0)command=(performance_running(audio)&selected)?0:1;
    /* Preflight before changing any participant. Sample copies and tracker
       binding happen outside the transaction's short exclusion. */
    if(command==1) {
        if((selected&PERF_MOSAIC) && (!audio->mosaic || ts_mosaic_end(audio->mosaic)<=0)) {
            snprintf(ui->status,sizeof(ui->status),"GROUP NOT STARTED: MOSAIC HAS NO CARDS.");return 0;
        }
        if((selected&PERF_ARP) && !ts_keyboard_sequence_active(&audio->keyboard_sequence)) {
            if(!audio->keyboard_sequence.settings.count ||
               (device && !keyboard_sequence_prepare(device,audio,ui,bank,fm,rate,1)) || !audio->keyboard_sequence.source) {
                snprintf(ui->status,sizeof(ui->status),"GROUP NOT STARTED: ARP NEEDS NOTES AND A PLAYABLE SOURCE.");return 0;
            }
        }
        if(((selected&PERF_TRACKER) || ((selected&PERF_ARP) && audio->sister.performance_mixer.arp_sync)) && !embedded_open(window,device,audio,ui,pages,bank,rate))return 0;
    }
    int embedded=ts_tapehead_frame()!=NULL;
    if(embedded)ts_tapehead_host_lock();else if(device)SDL_LockAudioDevice(device);
    int ok=1;
    if(selected&PERF_TRACKER) {
        if(command==1 && restart)ok=ts_tapehead_transport_locked(2);
        if(ok)ok=ts_tapehead_transport_locked(command);
    }
    if(ok) {
        if(selected&PERF_ARP) {
            if(command==1){if(restart)ts_keyboard_sequence_stop(&audio->keyboard_sequence);if(!ts_keyboard_sequence_active(&audio->keyboard_sequence))ts_keyboard_sequence_play(&audio->keyboard_sequence);}
            else ts_keyboard_sequence_stop(&audio->keyboard_sequence);
        }
        if((selected&PERF_MOSAIC) && audio->mosaic) {
            TsMosaic *m=audio->mosaic;
            if(command==2 || (command==1 && (restart || m->time>=ts_mosaic_end(m))))ts_mosaic_seek(m,0);
            m->playing=command==1;
        }
    }
    keyboard_sequence_ui(audio,ui);
    if(embedded)ts_tapehead_host_unlock();else if(device)SDL_UnlockAudioDevice(device);
    snprintf(ui->status,sizeof(ui->status),ok?"PERFORMANCE GROUP: %s / SHIFT+SPACE TO PLAY OR STOP":"GROUP NOT STARTED: TRACKER IS NOT READY.",command==1?"PLAYING":command==2?"AT START":"STOPPED");
    return ok;
}
static int performance_command(int command,SDL_Window *window,SDL_AudioDeviceID device,AudioState *audio,
    TsUiState *ui,TsSamplePages *pages,const TsInstrument *bank,const TsSample *fm,int rate)
{
    return performance_command_for(audio->sister.performance_transport,command,audio->sister.performance_mixer.restart,
        window,device,audio,ui,pages,bank,fm,rate);
}
static void performance_close(void)
{
    if(performance_transport.renderer)SDL_DestroyRenderer(performance_transport.renderer);
    if(performance_transport.window)SDL_DestroyWindow(performance_transport.window);
    memset(&performance_transport,0,sizeof(performance_transport));
}
static void performance_show(TsUiState *ui,AudioState *audio)
{
    if(!performance_transport.window) {
        performance_transport.window=SDL_CreateWindow("TapeSister - Performance Transport",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,PERF_W,PERF_H,SDL_WINDOW_ALLOW_HIGHDPI);
        if(performance_transport.window)performance_transport.renderer=SDL_CreateRenderer(performance_transport.window,-1,SDL_RENDERER_SOFTWARE);
        if(!performance_transport.renderer){performance_close();snprintf(ui->status,sizeof(ui->status),"CANNOT OPEN PERFORMANCE TRANSPORT");return;}
        performance_transport.id=SDL_GetWindowID(performance_transport.window);
        SDL_RenderSetLogicalSize(performance_transport.renderer,PERF_W,PERF_H);
    }
    SDL_SetWindowAlwaysOnTop(performance_transport.window,audio->sister.performance_mixer.always_on_top?SDL_TRUE:SDL_FALSE);
    performance_transport.visible=1;SDL_ShowWindow(performance_transport.window);SDL_RaiseWindow(performance_transport.window);
}
static void perf_rect(int x,int y,int w,int h,Uint32 color)
{
    SDL_SetRenderDrawColor(performance_transport.renderer,(color>>16)&255,(color>>8)&255,color&255,255);
    SDL_Rect r={x,y,w,h};SDL_RenderFillRect(performance_transport.renderer,&r);
}
static void perf_text(int x,int y,const char *s,Uint32 color,int scale)
{
    for(;*s;++s,x+=6*scale){const char *g=ts_ui_glyph(*s);for(int r=0;r<7;++r)for(int c=0;c<5;++c)if(g[r*5+c]=='1')perf_rect(x+c*scale,y+r*scale,scale,scale,color);}
}
static void performance_update(SDL_AudioDeviceID device,AudioState *audio)
{
    if(!performance_transport.visible || !window_drawable(performance_transport.window) || SDL_GetTicks()-performance_transport.painted<50)return;
    if(device)SDL_LockAudioDevice(device);
    unsigned selected=audio->sister.performance_transport,running=performance_running(audio);
    TsPerformanceMixer m=audio->sister.performance_mixer;
    if(device)SDL_UnlockAudioDevice(device);
    int top=(SDL_GetWindowFlags(performance_transport.window)&SDL_WINDOW_ALWAYS_ON_TOP)!=0;
    if(top!=(int)m.always_on_top)SDL_SetWindowAlwaysOnTop(performance_transport.window,m.always_on_top?SDL_TRUE:SDL_FALSE);
    perf_rect(0,0,PERF_W,PERF_H,0x151c20);perf_text(22,20,"PERFORMANCE TRANSPORT",0x8addb4,2);
    perf_rect(422,20,12,12,m.always_on_top?0x8addb4:0x47605d);perf_text(444,23,"ALWAYS ON TOP",0xdbe8e3,1);
    perf_text(22,49,"CHECK PLAYERS FOR GROUP CONTROL",0x94aaa7,1);perf_text(352,49,"CHANNEL GAIN",0x94aaa7,1);
    const char *names[]={"ARP","TRACKSISTER","MOSAIC"};
    char line[80];
    for(int i=0;i<3;++i) {
        int y=76+i*54;unsigned bit=1u<<i;
        perf_rect(20,y,560,46,0x202b30);perf_rect(28,y+15,12,12,selected&bit?0x8addb4:0x47605d);
        perf_text(49,y+9,names[i],0xdbe8e3,2);
        perf_text(50,y+30,running&bit?"PLAYING":"STOPPED",running&bit?0x8addb4:0x94aaa7,1);
        perf_rect(206,y+8,76,30,0x365746);perf_text(220,y+20,running&bit?"STOP":"PLAY",0xdbe8e3,1);
        perf_rect(290,y+8,48,30,0x304047);perf_text(302,y+20,"HOME",0xdbe8e3,1);
        int position=352+(int)lrintf(m.gain[i]*74);
        perf_rect(352,y+20,148,6,0x47605d);perf_rect(352,y+20,position-352,6,0x8addb4);
        perf_rect(425,y+16,2,14,0xb6aa73);perf_rect(position-3,y+15,6,16,0xdbe8e3);
        if(m.gain[i]>0)snprintf(line,sizeof(line),"%+.1f DB",20*log10f(m.gain[i]));else snprintf(line,sizeof(line),"SILENT");
        perf_text(513,y+20,line,0xdbe8e3,1);
    }
    perf_rect(20,244,262,30,0x304047);perf_text(32,255,m.restart?"GROUP PLAY: RESTART TOGETHER":"GROUP PLAY: RESUME",0xdbe8e3,1);
    const char *clocks[]={"ARP CLOCK: LOCAL","ARP CLOCK: TRACKER 1/4","ARP CLOCK: TRACKER 1/8","ARP CLOCK: TRACKER 1/16"};
    perf_rect(294,244,286,30,0x304047);perf_text(306,255,clocks[m.arp_sync%4],0xdbe8e3,1);
    perf_text(22,285,"MOSAIC SHARES GROUP START; ITS TAPE SPEED AND PITCH STAY LOCAL.",0x94aaa7,1);
    const char *buttons[]={"PLAY GROUP","STOP GROUP","RETURN TO START"};
    for(int i=0;i<3;++i){int x=20+i*190;perf_rect(x,310,180,34,0x365746);perf_text(x+10,323,buttons[i],0xdbe8e3,1);}
    perf_text(22,358,"SHIFT+SPACE: GROUP   CTRL+F11: OPEN   SPACE: ACTIVE WORKSPACE",0x94aaa7,1);
    perf_text(22,376,"GAIN: DRAG / WHEEL   DOUBLE-CLICK OR RIGHT-CLICK: 0 DB",0x94aaa7,1);
    SDL_RenderPresent(performance_transport.renderer);performance_transport.painted=SDL_GetTicks();
}
static void performance_gain(SDL_AudioDeviceID device,AudioState *audio,int channel,float gain)
{
    if(channel<0 || channel>2)return;
    if(device)SDL_LockAudioDevice(device);
    audio->sister.performance_mixer.gain[channel]=fmaxf(0,fminf(2,gain));
    if(device)SDL_UnlockAudioDevice(device);
    performance_transport.painted=SDL_GetTicks()-100;
}
static int performance_event(SDL_Event *e,SDL_Window *window,SDL_AudioDeviceID device,AudioState *audio,
    TsUiState *ui,TsSamplePages *pages,const TsInstrument *bank,const TsSample *fm,int rate)
{
    int own=performance_transport.window && event_window_id(e)==performance_transport.id;
    int key=e->type==SDL_KEYDOWN,mod=key?e->key.keysym.mod:0;
    int shortcut=key && e->key.keysym.sym==SDLK_SPACE && (mod&KMOD_SHIFT) && !(mod&(KMOD_ALT|KMOD_GUI));
    int openkey=key && e->key.keysym.sym==SDLK_F11 && (mod&KMOD_CTRL) && !(mod&(KMOD_ALT|KMOD_GUI|KMOD_SHIFT));
    if(!own && !sister_performance_keys_allowed(ui)) {
        if(ui->config_open && e->type==SDL_MOUSEBUTTONDOWN && e->button.button==SDL_BUTTON_LEFT && event_window_id(e)==SDL_GetWindowID(window)) {
            int x,y;logical_mouse(window,e->button.x,e->button.y,&x,&y);
            if(x>=356 && x<460 && y>=TS_CONFIG_ACTION_Y && y<TS_CONFIG_ACTION_Y+18){ui->config_open=0;performance_show(ui,audio);return 1;}
        }
        return 0;
    }
    if((shortcut || openkey) && !ui->midi_learn_active && matrix_window.rename<0 &&
       (!ui->tracker_open || ts_tapehead_transport_keys_allowed())) {
        if(!e->key.repeat) {
            if(openkey || (mod&KMOD_CTRL))performance_show(ui,audio);
            else performance_command(-1,window,device,audio,ui,pages,bank,fm,rate);
        }
        return 1;
    }
    if(!own)return 0;
    if((e->type==SDL_WINDOWEVENT && e->window.event==SDL_WINDOWEVENT_FOCUS_LOST) || e->type==SDL_MOUSEBUTTONUP) {
        performance_transport.drag=0;SDL_CaptureMouse(SDL_FALSE);
    }
    if((e->type==SDL_WINDOWEVENT && e->window.event==SDL_WINDOWEVENT_CLOSE) || (key && e->key.keysym.sym==SDLK_ESCAPE)) {
        performance_transport.drag=0;SDL_CaptureMouse(SDL_FALSE);
        performance_transport.visible=0;SDL_HideWindow(performance_transport.window);return 1;
    }
    if(e->type==SDL_MOUSEMOTION && performance_transport.drag)
        performance_gain(device,audio,performance_transport.drag-1,(e->motion.x-352)/74.f);
    if(e->type==SDL_MOUSEWHEEL) {
        int x,y;wheel_event_mouse(e,&x,&y);
        int row=(y-76)/54;
        if(x>=346 && x<580 && y>=76 && row<3 && (y-76)%54<46) {
            float delta=e->wheel.preciseY?e->wheel.preciseY:e->wheel.y;
            if(e->wheel.direction==SDL_MOUSEWHEEL_FLIPPED)delta=-delta;
            performance_gain(device,audio,row,audio->sister.performance_mixer.gain[row]+delta*.02f);
        }
    }
    if(e->type==SDL_MOUSEBUTTONDOWN) {
        int x=e->button.x,y=e->button.y,left=e->button.button==SDL_BUTTON_LEFT;
        int row=(y-76)/54;
        if(x>=346 && x<580 && y>=76 && row<3 && (y-76)%54<46) {
            if(e->button.button==SDL_BUTTON_RIGHT || (left && e->button.clicks>=2))performance_gain(device,audio,row,1);
            else if(left) {performance_transport.drag=row+1;SDL_CaptureMouse(SDL_TRUE);performance_gain(device,audio,row,(x-352)/74.f);}
        } else if(left && x>=20 && x<338 && y>=76 && row<3 && (y-76)%54<46) {
            if(x>=206)performance_command_for(1u<<row,x>=290?2:-1,0,window,device,audio,ui,pages,bank,fm,rate);
            else {if(device)SDL_LockAudioDevice(device);audio->sister.performance_transport^=1u<<row;if(device)SDL_UnlockAudioDevice(device);}
        } else if(left && x>=422 && x<580 && y>=16 && y<38) {
            if(device)SDL_LockAudioDevice(device);audio->sister.performance_mixer.always_on_top^=1;if(device)SDL_UnlockAudioDevice(device);
            SDL_SetWindowAlwaysOnTop(performance_transport.window,audio->sister.performance_mixer.always_on_top?SDL_TRUE:SDL_FALSE);
        } else if(left && x>=20 && x<580 && y>=244 && y<274) {
            if(x>=294 && !ts_tapehead_frame() && !embedded_open(window,device,audio,ui,pages,bank,rate))return 1;
            if(device)SDL_LockAudioDevice(device);
            if(x<282)audio->sister.performance_mixer.restart^=1;
            else if(x>=294)audio->sister.performance_mixer.arp_sync=(audio->sister.performance_mixer.arp_sync+1)%4;
            if(device)SDL_UnlockAudioDevice(device);
        } else if(left && y>=310 && y<344 && x>=20 && x<580 && (x-20)%190<180)
            performance_command((int[]){1,0,2}[(x-20)/190],window,device,audio,ui,pages,bank,fm,rate);
        performance_transport.painted=SDL_GetTicks()-100;
    }
    return 1;
}
