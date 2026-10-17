#include "main_sdl_mandala_editor.inc"
/* Modeless player mixer with a separate live tile bus. Group edits publish at one callback boundary. */
enum { PERF_ARP=1,PERF_TRACKER=2,PERF_MOSAIC=4,PERF_W=600,PERF_H=548 };
enum { PERF_BUTTON_PLAY=1,PERF_BUTTON_STOP,PERF_BUTTON_HOME,
    PERF_BUTTON_ROW_PLAY,PERF_BUTTON_ROW_HOME=PERF_BUTTON_ROW_PLAY+3,
    PERF_BUTTON_CLOCK=PERF_BUTTON_ROW_HOME+3,
    PERF_BUTTON_VISUAL,PERF_BUTTON_FREEZE,PERF_BUTTON_SEED,
    PERF_BUTTON_EDITOR,PERF_BUTTON_SYMMETRY,PERF_BUTTON_EVOLUTION };
static struct {SDL_Window *window;SDL_Renderer *renderer;Uint32 id,painted,flash_until;
    int visible,drag,pressed,flash;} performance_transport;
static TsPalette performance_palette;
static void performance_feedback(int button,int held)
{
    performance_transport.flash=button;
    performance_transport.flash_until=SDL_GetTicks()+180;
    if(held)performance_transport.pressed=button;
    performance_transport.painted=SDL_GetTicks()-100;
}
static unsigned performance_running(AudioState *audio)
{
    return (ts_keyboard_sequence_active(&audio->keyboard_sequence)?PERF_ARP:0)|
        (ts_tapehead_running()?PERF_TRACKER:0)|(audio->mosaic && audio->mosaic->playing?PERF_MOSAIC:0);
}
static int performance_command_for(unsigned selected,int command,SDL_Window *window,SDL_AudioDeviceID device,AudioState *audio,
    TsUiState *ui,TsSamplePages *pages,const TsInstrument *bank,const TsSample *fm,int rate)
{
    selected&=7u;
    if(!selected){snprintf(ui->status,sizeof(ui->status),"PERFORMANCE TRANSPORT: SELECT AT LEAST ONE PLAYER.");return 0;}
    if(command<0)command=(performance_running(audio)&selected)?0:1;
    ts_session_log_event("transport_request","selected_mask=%u command=%d",selected,command);
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
    if(selected&PERF_TRACKER)ok=ts_tapehead_transport_locked(command);
    if(ok) {
        if(selected&PERF_ARP) {
            if(command==1){if(!ts_keyboard_sequence_active(&audio->keyboard_sequence))ts_keyboard_sequence_play(&audio->keyboard_sequence);}
            else ts_keyboard_sequence_stop(&audio->keyboard_sequence);
        }
        if((selected&PERF_MOSAIC) && audio->mosaic) {
            TsMosaic *m=audio->mosaic;
            /* Only Home/Return to Start rewinds, including at the end. */
            if(command==2)ts_mosaic_seek(m,0);
            m->playing=command==1;
        }
    }
    keyboard_sequence_ui(audio,ui);
    if(embedded)ts_tapehead_host_unlock();else if(device)SDL_UnlockAudioDevice(device);
    snprintf(ui->status,sizeof(ui->status),ok?"PERFORMANCE GROUP: %s / SHIFT+SPACE TO PLAY OR PAUSE":"GROUP NOT STARTED: TRACKER IS NOT READY.",command==1?"PLAYING":command==2?"AT START":"PAUSED");
    return ok;
}
static int performance_command(int command,SDL_Window *window,SDL_AudioDeviceID device,AudioState *audio,
    TsUiState *ui,TsSamplePages *pages,const TsInstrument *bank,const TsSample *fm,int rate)
{
    int resolved=command<0?(performance_running(audio)&audio->sister.performance_transport?0:1):command;
    int ok=performance_command_for(audio->sister.performance_transport,resolved,
        window,device,audio,ui,pages,bank,fm,rate);
    if(ok)performance_feedback(resolved==1?PERF_BUTTON_PLAY:resolved==0?PERF_BUTTON_STOP:PERF_BUTTON_HOME,0);
    return ok;
}
static void performance_close(void)
{
    if(performance_transport.renderer)SDL_DestroyRenderer(performance_transport.renderer);
    if(performance_transport.window)SDL_DestroyWindow(performance_transport.window);
    memset(&performance_transport,0,sizeof(performance_transport));
}
static void performance_show(TsUiState *ui,AudioState *audio)
{
    performance_palette=ui->palette;
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
static void perf_button(int x,int y,int width,int height,const char *label,int button,int active,int primary)
{
    int pressed=performance_transport.pressed==button ||
        (performance_transport.flash==button && !SDL_TICKS_PASSED(SDL_GetTicks(),performance_transport.flash_until));
    int lit=active || pressed;
    Uint32 background=ts_theme_color(&performance_palette,TS_THEME_TRANSPORT,
        lit?TS_COLOR_SELECTION:primary?TS_COLOR_GRID:TS_COLOR_EDGE,lit?0xff8addb4u:0xff304047u);
    Uint32 foreground=ts_theme_color(&performance_palette,TS_THEME_TRANSPORT,
        lit?TS_COLOR_SELECTEDTEXT:TS_COLOR_TEXT,lit?0xff151c20u:0xffdbe8e3u);
    perf_rect(x,y,width,height,background);
    if(lit)perf_rect(x,y,width,2,ts_theme_color(&performance_palette,TS_THEME_TRANSPORT,
        pressed?TS_COLOR_FOCUS:TS_COLOR_ACTIVITY,0xfff0c982u));
    perf_text(x+12,y+(height-7)/2,label,foreground,1);
}
static void performance_update(SDL_AudioDeviceID device,AudioState *audio)
{
    /* Runs even when this dialog is hidden, outside its existing device lock. */
    ts_mandala_follow_effects(&audio->sister);
    if(!performance_transport.visible || !window_drawable(performance_transport.window) || SDL_GetTicks()-performance_transport.painted<50)return;
    if(device)SDL_LockAudioDevice(device);
    unsigned selected=audio->sister.performance_transport,running=performance_running(audio);
    TsPerformanceMixer m=audio->sister.performance_mixer;
    if(device)SDL_UnlockAudioDevice(device);
    int top=(SDL_GetWindowFlags(performance_transport.window)&SDL_WINDOW_ALWAYS_ON_TOP)!=0;
    if(top!=(int)m.always_on_top)SDL_SetWindowAlwaysOnTop(performance_transport.window,m.always_on_top?SDL_TRUE:SDL_FALSE);
    perf_rect(0,0,PERF_W,PERF_H,ts_theme_color(&performance_palette,TS_THEME_TRANSPORT,TS_COLOR_PANEL,0xff151c20u));perf_text(22,20,"PERFORMANCE TRANSPORT",ts_theme_color(&performance_palette,TS_THEME_TRANSPORT,TS_COLOR_DATA4,0xff8addb4u),2);
    perf_rect(422,20,12,12,m.always_on_top?ts_theme_color(&performance_palette,TS_THEME_TRANSPORT,TS_COLOR_DATA4,0xff8addb4u):ts_theme_color(&performance_palette,TS_THEME_TRANSPORT,TS_COLOR_GRID,0xff47605du));perf_text(444,23,"ALWAYS ON TOP",ts_theme_color(&performance_palette,TS_THEME_TRANSPORT,TS_COLOR_TEXT,0xffdbe8e3u),1);
    perf_text(22,49,"CHECK PLAYERS FOR GROUP CONTROL",ts_theme_color(&performance_palette,TS_THEME_TRANSPORT,TS_COLOR_DIM,0xff94aaa7u),1);perf_text(352,49,"CHANNEL GAIN",ts_theme_color(&performance_palette,TS_THEME_TRANSPORT,TS_COLOR_DIM,0xff94aaa7u),1);
    const char *names[]={"ARP","TRACKSISTER","MOSAIC","LIVE TILES"};
    char line[80];
    for(int i=0;i<4;++i) {
        int y=76+i*54;unsigned bit=1u<<i;
        perf_rect(20,y,560,46,ts_theme_color(&performance_palette,TS_THEME_TRANSPORT,TS_COLOR_EDGE,0xff202b30u));if(i<3)perf_rect(28,y+15,12,12,selected&bit?ts_theme_color(&performance_palette,TS_THEME_TRANSPORT,TS_COLOR_DATA4,0xff8addb4u):ts_theme_color(&performance_palette,TS_THEME_TRANSPORT,TS_COLOR_GRID,0xff47605du));
        perf_text(49,y+9,names[i],ts_theme_color(&performance_palette,TS_THEME_TRANSPORT,TS_COLOR_TEXT,0xffdbe8e3u),2);
        perf_text(50,y+30,i==3?"+ LAUNCHERS":running&bit?"PLAYING":"STOPPED",running&bit?ts_theme_color(&performance_palette,TS_THEME_TRANSPORT,TS_COLOR_DATA4,0xff8addb4u):ts_theme_color(&performance_palette,TS_THEME_TRANSPORT,TS_COLOR_DIM,0xff94aaa7u),1);
        if(i<3) {
        perf_button(206,y+8,76,30,running&bit?(i?"PAUSE":"STOP"):"PLAY",PERF_BUTTON_ROW_PLAY+i,(running&bit)!=0,1);
        perf_button(290,y+8,48,30,"HOME",PERF_BUTTON_ROW_HOME+i,0,0);
        } else perf_text(214,y+20,"QWERTY / MIDI",ts_theme_color(&performance_palette,TS_THEME_TRANSPORT,TS_COLOR_DIM,0xff94aaa7u),1);
        int position=352+(int)lrintf(m.gain[i]*74);
        perf_rect(352,y+20,148,6,ts_theme_color(&performance_palette,TS_THEME_TRANSPORT,TS_COLOR_GRID,0xff47605du));perf_rect(352,y+20,position-352,6,ts_theme_color(&performance_palette,TS_THEME_TRANSPORT,TS_COLOR_DATA4,0xff8addb4u));
        perf_rect(425,y+16,2,14,ts_theme_color(&performance_palette,TS_THEME_TRANSPORT,TS_COLOR_DATA1,0xffb6aa73u));perf_rect(position-3,y+15,6,16,ts_theme_color(&performance_palette,TS_THEME_TRANSPORT,TS_COLOR_TEXT,0xffdbe8e3u));
        if(m.gain[i]>0)snprintf(line,sizeof(line),"%+.1f DB",20*log10f(m.gain[i]));else snprintf(line,sizeof(line),"SILENT");
        perf_text(513,y+20,line,ts_theme_color(&performance_palette,TS_THEME_TRANSPORT,TS_COLOR_TEXT,0xffdbe8e3u),1);
    }
    perf_text(32,309,"PLAY RESUMES / HOME REWINDS",ts_theme_color(&performance_palette,TS_THEME_TRANSPORT,TS_COLOR_DIM,0xff94aaa7u),1);
    const char *clocks[]={"ARP CLOCK: LOCAL","ARP CLOCK: TRACKER 1/4","ARP CLOCK: TRACKER 1/8","ARP CLOCK: TRACKER 1/16"};
    perf_button(294,298,286,30,clocks[m.arp_sync%4],PERF_BUTTON_CLOCK,m.arp_sync!=0,0);
    perf_text(22,339,"MOSAIC SHARES GROUP START; ITS TAPE SPEED AND PITCH STAY LOCAL.",ts_theme_color(&performance_palette,TS_THEME_TRANSPORT,TS_COLOR_DIM,0xff94aaa7u),1);
    const char *buttons[]={"PLAY GROUP","PAUSE GROUP","RETURN TO START"};
    for(int i=0;i<3;++i)perf_button(20+i*190,364,180,34,buttons[i],PERF_BUTTON_PLAY+i,
        selected && (i==0?(running&selected)!=0:i==1?(running&selected)==0:0),1);
    perf_text(22,412,"SHIFT+SPACE: GROUP   F11: OPEN   ALT+F11: FULLSCREEN",ts_theme_color(&performance_palette,TS_THEME_TRANSPORT,TS_COLOR_DIM,0xff94aaa7u),1);
    perf_text(22,430,"GAIN: DRAG / WHEEL   DOUBLE-CLICK OR RIGHT-CLICK: 0 DB",ts_theme_color(&performance_palette,TS_THEME_TRANSPORT,TS_COLOR_DIM,0xff94aaa7u),1);
    perf_button(20,454,180,32,ts_mandala_active()?"CLOSE VISUALIZER":"VISUALIZE",PERF_BUTTON_VISUAL,ts_mandala_active(),1);
    perf_button(210,454,180,32,ts_mandala_frozen()?"RESUME VISUAL":"FREEZE VISUAL",PERF_BUTTON_FREEZE,ts_mandala_active() && ts_mandala_frozen(),0);
    perf_button(400,454,180,32,"NEW SEED",PERF_BUTTON_SEED,0,0);
    const TsMandalaConfig *v=&audio->sister.mandala;
    snprintf(line,sizeof(line),"EDIT VISUALIZER");
    perf_button(20,494,180,28,line,PERF_BUTTON_EDITOR,mandala_editor.visible,0);
    snprintf(line,sizeof(line),"SYMMETRY: %u",v->symmetry);perf_button(210,494,180,28,line,PERF_BUTTON_SYMMETRY,0,0);
    snprintf(line,sizeof(line),"EVOLUTION: %u:%02u",v->cycle_seconds/60,v->cycle_seconds%60);perf_button(400,494,180,28,line,PERF_BUTTON_EVOLUTION,0,0);
    perf_text(22,534,"IN VISUAL WINDOW: F FULLSCREEN / D DISPLAY / Q QUALITY / ESC EXIT",ts_theme_color(&performance_palette,TS_THEME_TRANSPORT,TS_COLOR_DIM,0xff94aaa7u),1);
    SDL_RenderPresent(performance_transport.renderer);performance_transport.painted=SDL_GetTicks();
}
static void performance_gain(SDL_AudioDeviceID device,AudioState *audio,int channel,float gain)
{
    if(channel<0 || channel>3)return;
    if(device)SDL_LockAudioDevice(device);
    audio->sister.performance_mixer.gain[channel]=fmaxf(0,fminf(2,gain));
    if(device)SDL_UnlockAudioDevice(device);
    performance_transport.painted=SDL_GetTicks()-100;
}
static int performance_event(SDL_Event *e,SDL_Window *window,SDL_AudioDeviceID device,AudioState *audio,
    TsUiState *ui,TsSamplePages *pages,const TsInstrument *bank,const TsSample *fm,int rate)
{
    int own=performance_transport.window && event_window_id(e)==performance_transport.id;
    if(e->type==SDL_MOUSEBUTTONUP && e->button.button==SDL_BUTTON_LEFT && performance_transport.pressed) {
        /* A short click remains visible even if down/up arrive in one batch. */
        performance_transport.pressed=0;performance_transport.flash_until=SDL_GetTicks()+180;
        performance_transport.painted=SDL_GetTicks()-100;
    }
    if(own && e->type==SDL_WINDOWEVENT && (e->window.event==SDL_WINDOWEVENT_FOCUS_LOST ||
       e->window.event==SDL_WINDOWEVENT_HIDDEN || e->window.event==SDL_WINDOWEVENT_MINIMIZED)) {
        performance_transport.pressed=performance_transport.flash=0;
        performance_transport.painted=SDL_GetTicks()-100;
    }
    int key=e->type==SDL_KEYDOWN,mod=key?e->key.keysym.mod:0;
    int shortcut=key && e->key.keysym.sym==SDLK_SPACE && (mod&KMOD_SHIFT) && !(mod&(KMOD_ALT|KMOD_GUI));
    int openkey=key && e->key.keysym.sym==SDLK_F11 && !(mod&(KMOD_CTRL|KMOD_ALT|KMOD_GUI|KMOD_SHIFT));
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
        performance_transport.pressed=performance_transport.flash=0;
        performance_transport.visible=0;SDL_HideWindow(performance_transport.window);return 1;
    }
    if(e->type==SDL_MOUSEMOTION && performance_transport.drag)
        performance_gain(device,audio,performance_transport.drag-1,(e->motion.x-352)/74.f);
    if(e->type==SDL_MOUSEWHEEL) {
        int x,y;wheel_event_mouse(e,&x,&y);
        int row=(y-76)/54;
        if(x>=346 && x<580 && y>=76 && row<4 && (y-76)%54<46) {
            float delta=e->wheel.preciseY?e->wheel.preciseY:e->wheel.y;
            if(e->wheel.direction==SDL_MOUSEWHEEL_FLIPPED)delta=-delta;
            performance_gain(device,audio,row,audio->sister.performance_mixer.gain[row]+delta*.02f);
        }
    }
    if(e->type==SDL_MOUSEBUTTONDOWN) {
        int x=e->button.x,y=e->button.y,left=e->button.button==SDL_BUTTON_LEFT;
        if(left && y>=454 && y<522 && x>=20 && x<580) {
            int col=(x-20)/190;
            if((x-20)%190>=180)return 1;
            TsMandalaConfig *v=&audio->sister.mandala;
            if(y<486) {
                performance_feedback(PERF_BUTTON_VISUAL+col,1);
                if(col==0) {
                    if(ts_mandala_active())ts_mandala_close();
                    else {
                        TsMandalaPreferences prefs={ui->config.visual_quality,ui->config.visual_display,ui->config.visual_fullscreen};
                        char error[160];
                        if(!ts_mandala_open(v,&prefs,error,sizeof(error)))snprintf(ui->status,sizeof(ui->status),"VISUALIZER: %.140s",error);
                    }
                } else if(col==1)ts_mandala_freeze();
                else ts_mandala_reseed(v);
            } else if(y>=494) {
                performance_feedback(PERF_BUTTON_EDITOR+col,1);
                if(col==0)mandala_editor_show(ui,v);
                else if(col==1)v->symmetry=v->symmetry>=12?4:v->symmetry+2;
                else v->cycle_seconds=v->cycle_seconds>=960?60:v->cycle_seconds*2;
            }
            performance_transport.painted=SDL_GetTicks()-100;
            return 1;
        }
        int row=(y-76)/54;
        if(x>=346 && x<580 && y>=76 && row<4 && (y-76)%54<46) {
            if(e->button.button==SDL_BUTTON_RIGHT || (left && e->button.clicks>=2))performance_gain(device,audio,row,1);
            else if(left) {performance_transport.drag=row+1;SDL_CaptureMouse(SDL_TRUE);performance_gain(device,audio,row,(x-352)/74.f);}
        } else if(left && x>=20 && x<338 && y>=76 && row<3 && (y-76)%54<46) {
            if(x>=206) {
                performance_feedback((x>=290?PERF_BUTTON_ROW_HOME:PERF_BUTTON_ROW_PLAY)+row,1);
                performance_command_for(1u<<row,x>=290?2:-1,window,device,audio,ui,pages,bank,fm,rate);
            }
            else {if(device)SDL_LockAudioDevice(device);audio->sister.performance_transport^=1u<<row;if(device)SDL_UnlockAudioDevice(device);}
        } else if(left && x>=422 && x<580 && y>=16 && y<38) {
            if(device)SDL_LockAudioDevice(device);audio->sister.performance_mixer.always_on_top^=1;if(device)SDL_UnlockAudioDevice(device);
            SDL_SetWindowAlwaysOnTop(performance_transport.window,audio->sister.performance_mixer.always_on_top?SDL_TRUE:SDL_FALSE);
        } else if(left && x>=294 && x<580 && y>=298 && y<328) {
            performance_feedback(PERF_BUTTON_CLOCK,1);
            if(!ts_tapehead_frame() && !embedded_open(window,device,audio,ui,pages,bank,rate))return 1;
            if(device)SDL_LockAudioDevice(device);
            audio->sister.performance_mixer.arp_sync=(audio->sister.performance_mixer.arp_sync+1)%4;
            if(device)SDL_UnlockAudioDevice(device);
        } else if(left && y>=364 && y<398 && x>=20 && x<580 && (x-20)%190<180) {
            performance_feedback(PERF_BUTTON_PLAY+(x-20)/190,1);
            performance_command((int[]){1,0,2}[(x-20)/190],window,device,audio,ui,pages,bank,fm,rate);
        }
        performance_transport.painted=SDL_GetTicks()-100;
    }
    return 1;
}
