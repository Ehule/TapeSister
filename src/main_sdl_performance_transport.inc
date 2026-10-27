#include "main_sdl_mandala_editor.inc"
/* Modeless player mixer with a separate live tile bus. Group edits publish at one callback boundary. */
enum { PERF_ARP=1,PERF_TRACKER=2,PERF_MOSAIC=4,PERF_W=960,PERF_H=640 };
enum { PERF_OVERVIEW,PERF_CUES,PERF_INPUTS,PERF_VISUALS,PERF_CLOCKS,PERF_PAGES };
enum { PERF_TAB_STEP=184,PERF_TAB_W=176 };
enum { PERF_ROWS_Y=178,PERF_ROW_STEP=60,PERF_GAIN_X=780,PERF_GAIN_W=112 };
enum { PERF_BUTTON_PLAY=1,PERF_BUTTON_STOP,PERF_BUTTON_HOME,
    PERF_BUTTON_ROW_PLAY,PERF_BUTTON_ROW_HOME=PERF_BUTTON_ROW_PLAY+3,
    PERF_BUTTON_CLOCK=PERF_BUTTON_ROW_HOME+3,
    PERF_BUTTON_VISUAL,PERF_BUTTON_FREEZE,PERF_BUTTON_SEED,
    PERF_BUTTON_EDITOR,PERF_BUTTON_SYMMETRY,PERF_BUTTON_EVOLUTION,
    PERF_BUTTON_INPUT_PREV,PERF_BUTTON_INPUT_NEXT,PERF_BUTTON_TUNER,
    PERF_BUTTON_TUNE_PREV,PERF_BUTTON_TUNE_NEXT,PERF_BUTTON_RENDER,
    PERF_BUTTON_TAB,PERF_BUTTON_CUE=PERF_BUTTON_TAB+PERF_PAGES,PERF_BUTTON_LOOP=PERF_BUTTON_CUE+16,
    PERF_BUTTON_MASTER,PERF_BUTTON_BPM_MINUS,PERF_BUTTON_BPM_PLUS,PERF_BUTTON_CLOCK_SOURCE,
    PERF_BUTTON_CLOCK_ROW=64 };
static struct {SDL_Window *window;SDL_Renderer *renderer;SDL_Texture *image;Uint32 *pixels;
    Uint32 id,painted,flash_until;
    int visible,drag,pressed,flash,input_page,input_drag,tuner_on,page,selected;
    unsigned input_channels,tuner_channel;
    TsAudioChannelRange input_range;
    TsPerformanceInputs *inputs;
    TsInputTuner tuner;
    Uint32 tuner_tick,tuner_signal;
    char input_label[128],input_error[160];} performance_transport;
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
static int performance_command_apply(unsigned selected,int command,unsigned homes,int master,SDL_Window *window,SDL_AudioDeviceID device,AudioState *audio,
    TsUiState *ui,TsSamplePages *pages,const TsInstrument *bank,const TsSample *fm,int rate)
{
    selected&=7u;
    if(!selected && !(command==2 && homes) && !(master && audio->sister.clock_controls.enabled)) {
        snprintf(ui->status,sizeof(ui->status),"PERFORMANCE TRANSPORT: SELECT AT LEAST ONE PLAYER.");return 0;
    }
    if(command<0)command=(performance_running(audio)&selected)?0:1;
    ts_session_log_event("transport_request","selected_mask=%u command=%d",selected,command);
    /* Preflight before changing any participant. Sample copies and tracker
       binding happen outside the transaction's short exclusion. */
    if(command==2 && (homes&PERF_TRACKER) &&
       !embedded_open(window,device,audio,ui,pages,bank,rate))return 0;
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
        if(((selected&PERF_TRACKER) || ((selected&PERF_ARP) && audio->sister.performance_mixer.arp_sync) ||
            ((master || (selected&PERF_ARP)) && audio->sister.clock_controls.enabled && audio->sister.clock_controls.source==TS_CLOCK_TRACKER_SOURCE)) &&
            !embedded_open(window,device,audio,ui,pages,bank,rate))return 0;
    }
    int embedded=ts_tapehead_frame()!=NULL;
    if(embedded)ts_tapehead_host_lock();else if(device)SDL_LockAudioDevice(device);
    int ok=1;
    if(selected&PERF_TRACKER)ok=ts_tapehead_transport_locked(command==2 && !(homes&PERF_TRACKER)?0:command);
    else if(command==2 && (homes&PERF_TRACKER) && ts_tapehead_frame())ok=ts_tapehead_locate_locked(0,0);
    if(ok) {
        if(selected&PERF_ARP) {
            if(command==1){if(!ts_keyboard_sequence_active(&audio->keyboard_sequence))ts_keyboard_sequence_play(&audio->keyboard_sequence);}
            else ts_keyboard_sequence_stop(&audio->keyboard_sequence);
        }
        if(command==2 && (homes&PERF_ARP))ts_keyboard_sequence_reset(&audio->keyboard_sequence);
        if((selected&PERF_MOSAIC) && audio->mosaic) {
            TsMosaic *m=audio->mosaic;
            /* Only Home/Return to Start rewinds, including at the end. */
            m->playing=command==1;
        }
        if(command==2 && (homes&PERF_MOSAIC) && audio->mosaic)ts_mosaic_seek(audio->mosaic,0);
        if(master)ts_master_clock_transport(&audio->sister.master_clock,command);
    }
    keyboard_sequence_ui(audio,ui);
    if(embedded)ts_tapehead_host_unlock();else if(device)SDL_UnlockAudioDevice(device);
    snprintf(ui->status,sizeof(ui->status),ok?"PERFORMANCE GROUP: %s / SHIFT+SPACE TO PLAY OR PAUSE":"GROUP NOT STARTED: TRACKER IS NOT READY.",command==1?"PLAYING":command==2?"AT START":"PAUSED");
    if(ok && master && command==2 && audio->sister.clock_controls.position_custom)
        snprintf(ui->status,sizeof(ui->status),"GROUP PAUSED / HOME FOLLOWERS REWOUND / OTHER POSITIONS RETAINED");
    return ok;
}
static int performance_command_for(unsigned selected,int command,SDL_Window *window,SDL_AudioDeviceID device,AudioState *audio,
    TsUiState *ui,TsSamplePages *pages,const TsInstrument *bank,const TsSample *fm,int rate)
{
    return performance_command_apply(selected,command,selected,0,window,device,audio,ui,pages,bank,fm,rate);
}
static int performance_command(int command,SDL_Window *window,SDL_AudioDeviceID device,AudioState *audio,
    TsUiState *ui,TsSamplePages *pages,const TsInstrument *bank,const TsSample *fm,int rate)
{
    unsigned selected=audio->sister.performance_transport;
    int running=selected?(performance_running(audio)&selected)!=0:audio->sister.master_clock.running;
    int resolved=command<0?(running?0:1):command;
    unsigned homes=ts_master_clock_home_mask(&audio->sister.clock_controls,selected);
    int ok=performance_command_apply(selected,resolved,homes,1,
        window,device,audio,ui,pages,bank,fm,rate);
    if(ok)performance_feedback(resolved==1?PERF_BUTTON_PLAY:resolved==0?PERF_BUTTON_STOP:PERF_BUTTON_HOME,0);
    return ok;
}
static void performance_close(void)
{
    ts_performance_inputs_tuner(performance_transport.inputs,0,0);
    if(performance_transport.image)SDL_DestroyTexture(performance_transport.image);
    free(performance_transport.pixels);
    if(performance_transport.renderer)SDL_DestroyRenderer(performance_transport.renderer);
    if(performance_transport.window)SDL_DestroyWindow(performance_transport.window);
    memset(&performance_transport,0,sizeof(performance_transport));
}
static void performance_show(TsUiState *ui,AudioState *audio)
{
    performance_transport.inputs=audio->performance_inputs;
    performance_palette=ui->palette;
    if(!performance_transport.window) {
        performance_transport.window=SDL_CreateWindow("TapeSister - Control Center",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,PERF_W,PERF_H,SDL_WINDOW_ALLOW_HIGHDPI);
        if(performance_transport.window)performance_transport.renderer=SDL_CreateRenderer(performance_transport.window,-1,SDL_RENDERER_SOFTWARE);
        if(!performance_transport.renderer){performance_close();snprintf(ui->status,sizeof(ui->status),"CANNOT OPEN PERFORMANCE TRANSPORT");return;}
        performance_transport.pixels=calloc((size_t)PERF_W*PERF_H,sizeof(*performance_transport.pixels));
        performance_transport.image=SDL_CreateTexture(performance_transport.renderer,SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_STREAMING,PERF_W,PERF_H);
        if(!performance_transport.pixels || !performance_transport.image ||
           SDL_SetTextureBlendMode(performance_transport.image,SDL_BLENDMODE_NONE)<0 ||
           SDL_SetTextureScaleMode(performance_transport.image,SDL_ScaleModeNearest)<0) {
            performance_close();snprintf(ui->status,sizeof(ui->status),"CANNOT CREATE CONTROL CENTER IMAGE");return;
        }
        performance_transport.id=SDL_GetWindowID(performance_transport.window);
        SDL_RenderSetLogicalSize(performance_transport.renderer,PERF_W,PERF_H);
    }
    SDL_SetWindowAlwaysOnTop(performance_transport.window,audio->sister.performance_mixer.always_on_top?SDL_TRUE:SDL_FALSE);
    performance_transport.visible=1;SDL_ShowWindow(performance_transport.window);SDL_RaiseWindow(performance_transport.window);
}
static void perf_rect(int x,int y,int w,int h,Uint32 color)
{
    if(!performance_transport.pixels || w<=0 || h<=0)return;
    int first=SDL_max(0,x),last=SDL_min(PERF_W,x+w),top=SDL_max(0,y),bottom=SDL_min(PERF_H,y+h);
    if(first>=last || top>=bottom)return;
    for(int row=top;row<bottom;++row)
        SDL_memset4(performance_transport.pixels+(size_t)row*PERF_W+first,color|0xff000000u,(size_t)(last-first));
}
static void perf_text(int x,int y,const char *s,Uint32 color,int scale)
{
    for(;*s;++s,x+=6*scale){const char *g=ts_ui_glyph(*s);for(int r=0;r<7;++r)for(int c=0;c<5;++c)if(g[r*5+c]=='1')perf_rect(x+c*scale,y+r*scale,scale,scale,color);}
}
/* Hinted 8x12 secondary glyphs: uniform two-pixel strokes, one-pixel letter
   gaps. Rasterize at integer coordinates before display/DPI scaling. */
static void perf_small(int x,int y,const char *s,Uint32 color)
{
    static const unsigned column[]={0,1,3,5,6},row[]={0,2,3,5,7,8,10};
    for(;*s;++s,x+=9) {
        const char *g=ts_ui_glyph(*s);
        for(int r=0;r<7;++r)for(int c=0;c<5;++c)if(g[r*5+c]=='1')
            perf_rect(x+(int)column[c],y+(int)row[r],2,2,color);
    }
}
static void perf_present(void)
{
    /* Scaling many tiny SDL rectangles can drop or thin individual strokes.
       Upload a complete native image, then scale it once like the main UI. */
    SDL_Renderer *r=performance_transport.renderer;Uint32 background=performance_transport.pixels[0];
    SDL_UpdateTexture(performance_transport.image,NULL,performance_transport.pixels,PERF_W*sizeof(Uint32));
    SDL_SetRenderDrawColor(r,(background>>16)&255,(background>>8)&255,background&255,255);
    SDL_RenderClear(r);SDL_Rect destination={0,0,PERF_W,PERF_H};
    SDL_RenderCopy(r,performance_transport.image,NULL,&destination);SDL_RenderPresent(r);
}
static void perf_pointer(const SDL_Event *e,int *x,int *y)
{
    int px,py;
    if(e->type==SDL_MOUSEWHEEL)wheel_event_mouse(e,&px,&py);
    else if(e->type==SDL_MOUSEMOTION){px=e->motion.x;py=e->motion.y;}
    else {px=e->button.x;py=e->button.y;}
    float lx,ly;SDL_RenderWindowToLogical(performance_transport.renderer,px,py,&lx,&ly);
    *x=(int)floorf(lx);*y=(int)floorf(ly);
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
    perf_small(x+8,y+(height-12)/2,label,foreground);
}
#include "main_sdl_performance_inputs.inc"
#include "main_sdl_clock_routing.inc"
#include "main_sdl_cockpit.inc"
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
        performance_transport.drag=performance_transport.input_drag=0;SDL_CaptureMouse(SDL_FALSE);
    }
    if((e->type==SDL_WINDOWEVENT && e->window.event==SDL_WINDOWEVENT_CLOSE) || (key && e->key.keysym.sym==SDLK_ESCAPE)) {
        performance_transport.drag=performance_transport.input_drag=0;SDL_CaptureMouse(SDL_FALSE);
        performance_transport.pressed=performance_transport.flash=performance_transport.tuner_on=0;
        ts_performance_inputs_tuner(audio->performance_inputs,0,0);
        performance_transport.visible=0;SDL_HideWindow(performance_transport.window);return 1;
    }
    if(key && !(mod&(KMOD_CTRL|KMOD_ALT|KMOD_GUI))) {
        if(e->key.keysym.sym==SDLK_TAB) {
            if(!e->key.repeat)performance_page_set(audio,(performance_transport.page+((mod&KMOD_SHIFT)?PERF_PAGES-1:1))%PERF_PAGES);
            return 1;
        }
        if(e->key.keysym.sym==SDLK_F9 && !e->key.repeat) {
            SDL_Event request=*e;request.key.windowID=SDL_GetWindowID(window);SDL_PushEvent(&request);return 1;
        }
        if(e->key.keysym.sym==SDLK_F12)return ts_audio_health_event(e,window,audio,ui);
    }
    if(performance_inputs_event(e,device,audio))return 1;
    if(performance_clock_event(e,window,device,audio,ui,pages,bank,rate))return 1;
    if(e->type!=SDL_MOUSEBUTTONDOWN && e->type!=SDL_MOUSEMOTION && e->type!=SDL_MOUSEWHEEL)return 1;
    int x,y;perf_pointer(e,&x,&y);
    if(e->type==SDL_MOUSEMOTION) {
        if(performance_transport.drag)performance_gain(device,audio,performance_transport.drag-1,(x-PERF_GAIN_X)*2.f/PERF_GAIN_W);
        return 1;
    }
    const int channels[]={2,1,0,3};int row=(y-PERF_ROWS_Y)/PERF_ROW_STEP;
    int row_hit=performance_transport.page==PERF_OVERVIEW && y>=PERF_ROWS_Y && row<4 && (y-PERF_ROWS_Y)%PERF_ROW_STEP<54;
    if(e->type==SDL_MOUSEWHEEL) {
        if(row_hit && x>=PERF_GAIN_X-8 && x<940) {
            float delta=e->wheel.preciseY?e->wheel.preciseY:e->wheel.y;if(e->wheel.direction==SDL_MOUSEWHEEL_FLIPPED)delta=-delta;
            int ch=channels[row];performance_gain(device,audio,ch,audio->sister.performance_mixer.gain[ch]+delta*.02f);
        }
        return 1;
    }
    int left=e->button.button==SDL_BUTTON_LEFT;
    if(left && y>=104 && y<134 && x>=20 && x<20+PERF_PAGES*PERF_TAB_STEP && (x-20)%PERF_TAB_STEP<PERF_TAB_W) {
        performance_page_set(audio,(x-20)/PERF_TAB_STEP);return 1;
    }
    if(left && y>=54 && y<88 && x>=20 && x<590 && (x-20)%190<180) {
        int command=(int[]){1,0,2}[(x-20)/190];performance_feedback(PERF_BUTTON_PLAY+(x-20)/190,1);
        performance_command(command,window,device,audio,ui,pages,bank,fm,rate);return 1;
    }
    if(left && x>=786 && x<940 && y>=16 && y<38) {
        if(device)SDL_LockAudioDevice(device);audio->sister.performance_mixer.always_on_top^=1;if(device)SDL_UnlockAudioDevice(device);
        SDL_SetWindowAlwaysOnTop(performance_transport.window,audio->sister.performance_mixer.always_on_top?SDL_TRUE:SDL_FALSE);
    } else if(left && y>=604 && y<632) {
        if(x>=20 && x<180) {
            SDL_Event request={0};request.type=SDL_KEYDOWN;request.key.windowID=SDL_GetWindowID(window);request.key.keysym.sym=SDLK_F9;
            request.key.state=SDL_PRESSED;
            SDL_PushEvent(&request);
        }
        else if(x>=190 && x<350) {
            SDL_Event health={0};health.type=SDL_KEYDOWN;health.key.windowID=performance_transport.id;health.key.keysym.sym=SDLK_F12;
            ts_audio_health_event(&health,window,audio,ui);
        } else if(x>=360 && x<540){performance_feedback(PERF_BUTTON_RENDER,1);offline_show(ui,audio);}
    } else if(row_hit) {
        int ch=channels[row];
        if(x>=PERF_GAIN_X-8 && x<940) {
            if(e->button.button==SDL_BUTTON_RIGHT || (left && e->button.clicks>=2))performance_gain(device,audio,ch,1);
            else if(left){performance_transport.drag=ch+1;SDL_CaptureMouse(SDL_TRUE);performance_gain(device,audio,ch,(x-PERF_GAIN_X)*2.f/PERF_GAIN_W);}
        } else if(left && ch<3 && x>=620 && x<760) {
            performance_feedback((x>=704?PERF_BUTTON_ROW_HOME:PERF_BUTTON_ROW_PLAY)+ch,1);
            performance_command_for(1u<<ch,x>=704?2:-1,window,device,audio,ui,pages,bank,fm,rate);
        } else if(left && ch<3 && x>=200 && x<270) {
            if(device)SDL_LockAudioDevice(device);audio->sister.performance_transport^=1u<<ch;if(device)SDL_UnlockAudioDevice(device);
        } else if(left && x>=20 && x<200)performance_transport.selected=row;
    } else if(left && performance_transport.page==PERF_OVERVIEW && performance_transport.selected==2 && x>=28 && x<328 && y>=447 && y<477) {
        if(!ts_tapehead_frame() && !embedded_open(window,device,audio,ui,pages,bank,rate))return 1;
        if(device)SDL_LockAudioDevice(device);
        audio->sister.clock_controls.tempo_mask&=~TS_CLOCK_ARP;
        audio->sister.performance_mixer.arp_sync=(audio->sister.performance_mixer.arp_sync+1)%4;
        if(device)SDL_UnlockAudioDevice(device);
        performance_feedback(PERF_BUTTON_CLOCK,1);
    } else if(left && performance_transport.page==PERF_CUES && audio->mosaic) {
        int cue=-1;
        if(y>=184 && y<348 && x>=20 && x<940 && (x-20)%116<108 && (y-184)%82<68)cue=(y-184)/82*8+(x-20)/116;
        if(cue>=0 && cue<16 && audio->mosaic->markers[cue].stored) {
            if(device)SDL_LockAudioDevice(device);ts_mosaic_trigger_marker(audio->mosaic,cue);if(device)SDL_UnlockAudioDevice(device);
            performance_feedback(PERF_BUTTON_CUE+cue,1);
        } else if(x>=28 && x<278 && y>=370 && y<404) {
            if(device)SDL_LockAudioDevice(device);ts_mosaic_set_marker_loop(audio->mosaic,!audio->mosaic->marker_loop);if(device)SDL_UnlockAudioDevice(device);
            performance_feedback(PERF_BUTTON_LOOP,1);
        }
    } else if(left && performance_transport.page==PERF_VISUALS && x>=20 && x<580 && (x-20)%190<180 &&
        ((y>=180 && y<214) || (y>=232 && y<266))) {
        int col=(x-20)/190;TsMandalaConfig *v=&audio->sister.mandala;
        if(y<214) {
            performance_feedback(PERF_BUTTON_VISUAL+col,1);
            if(col==0) {
                if(ts_mandala_active())ts_mandala_close();else {
                    TsMandalaPreferences prefs={ui->config.visual_quality,ui->config.visual_display,ui->config.visual_fullscreen};char error[160];
                    if(!ts_mandala_open(v,&prefs,error,sizeof(error)))snprintf(ui->status,sizeof(ui->status),"VISUALIZER: %.140s",error);
                }
            } else if(col==1)ts_mandala_freeze();else ts_mandala_reseed(v);
        } else {
            performance_feedback(PERF_BUTTON_EDITOR+col,1);
            if(col==0)mandala_editor_show(ui,v);else if(col==1)v->symmetry=v->symmetry>=12?4:v->symmetry+2;else v->cycle_seconds=v->cycle_seconds>=960?60:v->cycle_seconds*2;
        }
    }
    performance_transport.painted=SDL_GetTicks()-100;return 1;
}
