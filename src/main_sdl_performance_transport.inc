/* Modeless transport selection is project state. Clock ownership remains local:
   all selected players start at the next shared output callback boundary. */
enum { PERF_ARP=1,PERF_TRACKER=2,PERF_MOSAIC=4,PERF_W=520,PERF_H=300 };
static struct {SDL_Window *window;SDL_Renderer *renderer;Uint32 id,painted;int visible;} performance_transport;
static unsigned performance_running(AudioState *audio)
{
    return (ts_keyboard_sequence_active(&audio->keyboard_sequence)?PERF_ARP:0)|
        (ts_tapehead_running()?PERF_TRACKER:0)|(audio->mosaic && audio->mosaic->playing?PERF_MOSAIC:0);
}
static int performance_command(int command,SDL_Window *window,SDL_AudioDeviceID device,AudioState *audio,
    TsUiState *ui,TsSamplePages *pages,const TsInstrument *bank,const TsSample *fm,int rate)
{
    unsigned selected=audio->sister.performance_transport&7u;
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
        if((selected&PERF_TRACKER) && !embedded_open(window,device,audio,ui,pages,bank,rate))return 0;
    }
    int embedded=ts_tapehead_frame()!=NULL;
    if(embedded)ts_tapehead_host_lock();else if(device)SDL_LockAudioDevice(device);
    int ok=1;
    if(selected&PERF_TRACKER)ok=ts_tapehead_transport_locked(command);
    if(ok) {
        if(selected&PERF_ARP) {
            if(command==1){if(!ts_keyboard_sequence_active(&audio->keyboard_sequence))ts_keyboard_sequence_play(&audio->keyboard_sequence);}
            else ts_keyboard_sequence_stop(&audio->keyboard_sequence);
        }
        if((selected&PERF_MOSAIC) && audio->mosaic) {
            TsMosaic *m=audio->mosaic;
            if(command==2 || (command==1 && m->time>=ts_mosaic_end(m)))ts_mosaic_seek(m,0);
            m->playing=command==1;
        }
    }
    keyboard_sequence_ui(audio,ui);
    if(embedded)ts_tapehead_host_unlock();else if(device)SDL_UnlockAudioDevice(device);
    snprintf(ui->status,sizeof(ui->status),ok?"PERFORMANCE GROUP: %s / SHIFT+SPACE TO PLAY OR STOP":"GROUP NOT STARTED: TRACKER IS NOT READY.",command==1?"PLAYING":command==2?"AT START":"STOPPED");
    return ok;
}
static void performance_close(void)
{
    if(performance_transport.renderer)SDL_DestroyRenderer(performance_transport.renderer);
    if(performance_transport.window)SDL_DestroyWindow(performance_transport.window);
    memset(&performance_transport,0,sizeof(performance_transport));
}
static void performance_show(TsUiState *ui)
{
    if(!performance_transport.window) {
        performance_transport.window=SDL_CreateWindow("TapeSister - Performance Transport",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,PERF_W,PERF_H,SDL_WINDOW_ALLOW_HIGHDPI);
        if(performance_transport.window)performance_transport.renderer=SDL_CreateRenderer(performance_transport.window,-1,SDL_RENDERER_SOFTWARE);
        if(!performance_transport.renderer){performance_close();snprintf(ui->status,sizeof(ui->status),"CANNOT OPEN PERFORMANCE TRANSPORT");return;}
        performance_transport.id=SDL_GetWindowID(performance_transport.window);
        SDL_RenderSetLogicalSize(performance_transport.renderer,PERF_W,PERF_H);
    }
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
    if(device)SDL_UnlockAudioDevice(device);
    perf_rect(0,0,PERF_W,PERF_H,0x151c20);perf_text(22,20,"PERFORMANCE TRANSPORT",0x8addb4,2);
    perf_text(22,48,"SELECT THE PLAYERS CONTROLLED BY SHIFT+SPACE",0x94aaa7,1);
    const char *names[]={"ARP","SISTERTRACKER","MOSAIC"};
    for(int i=0;i<3;++i) {
        int y=76+i*38;perf_rect(20,y,480,30,0x202b30);perf_rect(30,y+8,14,14,selected&(1u<<i)?0x8addb4:0x47605d);
        perf_text(57,y+8,names[i],0xdbe8e3,2);perf_text(385,y+11,running&(1u<<i)?"PLAYING":"STOPPED",running&(1u<<i)?0x8addb4:0x94aaa7,1);
    }
    const char *buttons[]={"PLAY","STOP","RETURN TO START"};
    for(int i=0;i<3;++i){int x=i==2?264:20+i*122;perf_rect(x,204,i==2?236:112,34,0x365746);perf_text(x+12,214,buttons[i],0xdbe8e3,2);}
    perf_text(22,255,"SPACE CONTROLS THE ACTIVE WORKSPACE.",0x94aaa7,1);
    perf_text(22,275,"SHARED START/STOP; TEMPO AND TAPE SPEED STAY LOCAL.",0x94aaa7,1);
    SDL_RenderPresent(performance_transport.renderer);performance_transport.painted=SDL_GetTicks();
}
static int performance_event(SDL_Event *e,SDL_Window *window,SDL_AudioDeviceID device,AudioState *audio,
    TsUiState *ui,TsSamplePages *pages,const TsInstrument *bank,const TsSample *fm,int rate)
{
    int own=performance_transport.window && event_window_id(e)==performance_transport.id;
    int key=e->type==SDL_KEYDOWN,mod=key?e->key.keysym.mod:0;
    int shortcut=key && e->key.keysym.sym==SDLK_SPACE && (mod&KMOD_SHIFT) && !(mod&(KMOD_ALT|KMOD_GUI));
    if(!own && !sister_performance_keys_allowed(ui)) {
        if(ui->config_open && e->type==SDL_MOUSEBUTTONDOWN && e->button.button==SDL_BUTTON_LEFT && event_window_id(e)==SDL_GetWindowID(window)) {
            int x,y;logical_mouse(window,e->button.x,e->button.y,&x,&y);
            if(x>=356 && x<460 && y>=TS_CONFIG_ACTION_Y && y<TS_CONFIG_ACTION_Y+18){ui->config_open=0;performance_show(ui);return 1;}
        }
        return 0;
    }
    if(shortcut && !ui->midi_learn_active && matrix_window.rename<0 &&
       (!ui->tracker_open || ts_tapehead_transport_keys_allowed())) {
        if(!e->key.repeat) {
            if(mod&KMOD_CTRL)performance_show(ui);
            else if(audio->sister.performance_transport==PERF_ARP)
                keyboard_sequence_toggle_playback(device,audio,ui,bank,fm,rate);
            else performance_command(-1,window,device,audio,ui,pages,bank,fm,rate);
        }
        return 1;
    }
    if(!own)return 0;
    if((e->type==SDL_WINDOWEVENT && e->window.event==SDL_WINDOWEVENT_CLOSE) || (key && e->key.keysym.sym==SDLK_ESCAPE)) {
        performance_transport.visible=0;SDL_HideWindow(performance_transport.window);return 1;
    }
    if(e->type==SDL_MOUSEBUTTONDOWN && e->button.button==SDL_BUTTON_LEFT) {
        int x=e->button.x,y=e->button.y;
        if(x>=20 && x<500 && y>=76 && y<182) {
            int row=(y-76)/38;if(row<3){if(device)SDL_LockAudioDevice(device);audio->sister.performance_transport^=1u<<row;if(device)SDL_UnlockAudioDevice(device);}
        }
        if(y>=204 && y<238 && x>=20 && x<500)performance_command(x<132?1:x<254?0:2,window,device,audio,ui,pages,bank,fm,rate);
    }
    return 1;
}
