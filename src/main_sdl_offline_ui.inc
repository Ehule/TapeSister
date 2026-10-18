enum { OFFLINE_W=620, OFFLINE_H=390 };
static struct {
    SDL_Window *window;SDL_Renderer *renderer;Uint32 id,painted;
    int visible,field,select_all,bits,choosing;
    unsigned sources,rate;
    char duration[32],tail[32],message[192];
    TsPalette palette;
} offline_ui;
static void offline_rect(int x,int y,int w,int h,Uint32 color)
{
    SDL_SetRenderDrawColor(offline_ui.renderer,color>>16&255,color>>8&255,color&255,255);
    SDL_Rect r={x,y,w,h};SDL_RenderFillRect(offline_ui.renderer,&r);
}
static void offline_text(int x,int y,const char *s,Uint32 color,int scale)
{
    for(;*s;++s,x+=6*scale){const char *g=ts_ui_glyph(*s);for(int r=0;r<7;++r)for(int c=0;c<5;++c)
        if(g[r*5+c]=='1')offline_rect(x+c*scale,y+r*scale,scale,scale,color);}
}
static void offline_show(TsUiState *ui,AudioState *audio)
{
    if(!offline_ui.window) {
        offline_ui.window=SDL_CreateWindow("TapeSister - Offline Render",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,OFFLINE_W,OFFLINE_H,SDL_WINDOW_ALLOW_HIGHDPI);
        if(offline_ui.window)offline_ui.renderer=SDL_CreateRenderer(offline_ui.window,-1,SDL_RENDERER_SOFTWARE);
        if(!offline_ui.renderer){if(offline_ui.window)SDL_DestroyWindow(offline_ui.window);offline_ui.window=NULL;snprintf(ui->status,sizeof(ui->status),"CANNOT OPEN OFFLINE RENDER");return;}
        offline_ui.id=SDL_GetWindowID(offline_ui.window);SDL_RenderSetLogicalSize(offline_ui.renderer,OFFLINE_W,OFFLINE_H);
        snprintf(offline_ui.duration,sizeof(offline_ui.duration),"24:00");snprintf(offline_ui.tail,sizeof(offline_ui.tail),"10");offline_ui.bits=24;
    }
    if(!offline_job.active) {
        offline_ui.sources=audio->sister.performance_transport&6u;
        if(!offline_ui.sources)offline_ui.sources=audio->mosaic && ts_mosaic_end(audio->mosaic)>0?OFFLINE_MOSAIC:OFFLINE_TRACKER;
        offline_ui.rate=audio->output_rate>0?(unsigned)audio->output_rate:48000;
    }
    offline_ui.palette=ui->palette;offline_ui.visible=1;offline_ui.painted=SDL_GetTicks()-100;
    SDL_ShowWindow(offline_ui.window);SDL_RaiseWindow(offline_ui.window);
}
static int offline_time(const char *text,double *seconds)
{
    double total=0;int parts=0;const char *p=text;
    do {
        char *end=NULL;if(!isdigit((unsigned char)*p))return 0;
        double value=strtod(p,&end);if(end==p || !isfinite(value) || value<0)return 0;
        if(parts && value>=60)return 0;
        total=total*60+value;if(++parts>3)return 0;
        if(!*end){*seconds=total;return 1;}
        if(*end!=':' || (strchr(p,'.') && strchr(p,'.')<end))return 0;
        p=end+1;
    }while(*p);
    return 0;
}
static int offline_start(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,TsInstrument *bank,
    TsSamplePages *pages,TsInstrument *parked,int record_active,const char *path,int overwrite)
{
    OfflineOptions o={0};TsSisterProjectState *state=malloc(sizeof(*state));int ok=0;
    if(!state){snprintf(ui->status,sizeof(ui->status),"Cannot allocate render snapshot.");return 0;}
    o.rate=audio->output_rate>0?(unsigned)audio->output_rate:48000;o.bits=offline_ui.bits;o.sources=offline_ui.sources;
    o.channels=SDL_max(2u,audio->output_device_channels);o.overwrite=overwrite;
    if(!offline_time(offline_ui.duration,&o.seconds) || !offline_time(offline_ui.tail,&o.tail)) {
        snprintf(ui->status,sizeof(ui->status),"Use seconds or minutes:seconds for duration and tail.");goto done;
    }
    if(!offline_valid(&o,ui->status,sizeof(ui->status)))goto done;
    if(ui->mosaic_editing){snprintf(ui->status,sizeof(ui->status),"Return to Mosaic and apply card edits before rendering.");goto done;}
    if(audio->capture.state!=TS_CAPTURE_IDLE){snprintf(ui->status,sizeof(ui->status),"Finish or cancel capture before rendering.");goto done;}
    if((o.sources&OFFLINE_MOSAIC) && (!audio->mosaic || ts_mosaic_end(audio->mosaic)<=0)) {
        snprintf(ui->status,sizeof(ui->status),"Mosaic has no cards. Select TrackSister or add cards.");goto done;
    }
    if(device)SDL_LockAudioDevice(device);
    ts_sister_project_state_capture(state,&audio->sister,ts_sample_pages_count(pages),NULL);
    state->keyboard_sequence=ts_keyboard_sequence_export(&audio->keyboard_sequence);
    o.sister_enabled=audio->sister.enabled;o.sister_rolling=audio->sister.rolling;
    o.program_gain=audio->mixer.program_gain;o.master_gain=audio->mixer.master_gain;
    o.output_gain=audio->sister.master_output_gain.target;
    o.limiter_enabled=audio->sister.limiter.enabled;o.limiter_ceiling=audio->sister.limiter.ceiling_db;
    o.limiter_lookahead=audio->sister.limiter.lookahead_ms;o.limiter_release=audio->sister.limiter.release_ms;
    o.eq_run=audio->sister.master_eq.morph_running;o.router_run=audio->sister.router.transport.running;
    o.prism_run=audio->sister.prism.matrix.running;
    for(int i=0;i<TS_ROUTER_COUNT;++i)o.router_timer[i]=audio->sister.router.transport.timer[i].kind;
    o.spatial_morph=audio->sister.spatial.morph_active;o.spatial_rise=audio->sister.spatial.rise_active;
    o.speakers=audio->speakers;o.output_range=audio->output_range;
    o.fx_snapshot=1;memcpy(o.fx_slot,audio->sister.post_fx.slot,sizeof(o.fx_slot));o.fx_master=audio->sister.post_fx.master_engage;
    if(device)SDL_UnlockAudioDevice(device);
    /* Hardware cannot return samples faster than real time. Do not silently
       substitute a different effects path for an enabled external Insert. */
    if(!state->matrix.enabled && state->insert.send_pair && !(state->router.bypass_mask&(1u<<TS_ROUTER_INSERT))) {
        snprintf(ui->status,sizeof(ui->status),"Bypass the external Insert before offline rendering; hardware needs real-time capture.");goto done;
    }
    snprintf(o.output,sizeof(o.output),"%s",path);
    if(!offline_job_prepare(&o,ui->status,sizeof(ui->status)))goto done;
    if(!ts_tapehead_export(&pages->tracker,ui->status,sizeof(ui->status)) ||
        !ts_sample_pages_save_project(pages,record_active?NULL:bank,record_active?bank:parked,state,
            o.project,ui->status,sizeof(ui->status))) {offline_job_release();goto done;}
    ok=offline_job_launch(ui->status,sizeof(ui->status));
    if(ok) {
        snprintf(ui->status,sizeof(ui->status),"OFFLINE RENDER STARTED / F11 > RENDER OFFLINE FOR PROGRESS");
        *offline_ui.message=0;offline_ui.visible=1;offline_ui.choosing=0;
        SDL_ShowWindow(offline_ui.window);SDL_RaiseWindow(offline_ui.window);
    }
done:
    free(state);return ok;
}
static void offline_update(void)
{
    offline_job_poll();
    if(!offline_ui.visible || !window_drawable(offline_ui.window) || SDL_GetTicks()-offline_ui.painted<50)return;
    Uint32 bg=ts_theme_color(&offline_ui.palette,TS_THEME_TRANSPORT,TS_COLOR_PANEL,0xff151c20u);
    Uint32 ink=ts_theme_color(&offline_ui.palette,TS_THEME_TRANSPORT,TS_COLOR_TEXT,0xffdbe8e3u);
    Uint32 dim=ts_theme_color(&offline_ui.palette,TS_THEME_TRANSPORT,TS_COLOR_DIM,0xff94aaa7u);
    Uint32 edge=ts_theme_color(&offline_ui.palette,TS_THEME_TRANSPORT,TS_COLOR_EDGE,0xff304047u);
    Uint32 accent=ts_theme_color(&offline_ui.palette,TS_THEME_TRANSPORT,TS_COLOR_SELECTION,0xff8addb4u);
    offline_rect(0,0,OFFLINE_W,OFFLINE_H,bg);offline_text(22,20,"OFFLINE RENDER",accent,2);
    offline_text(22,49,"STEREO WAV / CURRENT MIX AND EFFECTS",dim,1);
    const char *names[]={"MOSAIC","TRACKSISTER"};unsigned bits[]={4,2};
    for(int i=0;i<2;++i) {
        int x=22+i*230;offline_rect(x,78,16,16,offline_ui.sources&bits[i]?accent:edge);
        offline_text(x+26,79,names[i],ink,2);
    }
    offline_text(22,116,"DURATION (M:SS OR SECONDS)",dim,1);offline_text(332,116,"EFFECTS TAIL (SECONDS)",dim,1);
    const char *values[]={offline_ui.duration,offline_ui.tail};
    for(int i=0;i<2;++i) {
        int x=i?332:22;offline_rect(x,133,i?266:286,32,edge);offline_text(x+10,142,values[i],ink,2);
        if(offline_ui.field==i+1)offline_rect(x,163,i?266:286,2,accent);
    }
    offline_rect(22,183,286,32,edge);offline_text(34,195,offline_ui.bits==24?"WAV: 24-BIT PCM":"WAV: 32-BIT FLOAT",ink,1);
    char line[128];snprintf(line,sizeof(line),"%u HZ / CURRENT SESSION RATE",offline_ui.rate);offline_text(332,195,line,dim,1);
    offline_text(22,233,"STARTS BOTH SELECTED PLAYERS AT ZERO; RETAINS LOOP SETTINGS.",dim,1);
    offline_text(22,250,"ACTIVE EQ / PRISM / ROUTER TIMERS RESTART; FX RAMPS CONTINUE.",dim,1);
    offline_text(22,267,"STARTS WITH EMPTY EFFECT TAILS. LIVE INPUT / MIDI / ARP OMITTED.",dim,1);
    offline_rect(22,289,576,10,edge);
    OfflineProgress *p=&offline_job.progress;
    if(p->total)offline_rect(22,289,(int)(576.0*fmin(1,(double)p->frames/p->total)),10,accent);
    if(offline_job.active || p->total) {
        snprintf(line,sizeof(line),"%s  %.1F / %.1F SECONDS",offline_job.cancelling?"CANCELLING":p->state==2?"COMPLETE":p->state==3?"CANCELLED":p->state<0?"FAILED":"RENDER",(double)p->frames/SDL_max(1u,offline_job.options.rate),(double)p->total/SDL_max(1u,offline_job.options.rate));
        offline_text(22,309,line,ink,1);
    }
    const char *message=*offline_ui.message?offline_ui.message:p->message;
    /* Two readable lines; the full error also remains in the main status. */
    char short_message[97];snprintf(short_message,sizeof(short_message),"%.96s",message);offline_text(22,326,short_message,dim,1);
    offline_rect(22,349,280,28,accent);offline_text(34,359,offline_job.active?"CANCEL RENDER":"CHOOSE FILE AND RENDER...",bg,1);
    offline_rect(318,349,280,28,edge);offline_text(330,359,offline_job.active?"HIDE (CONTINUE RENDERING)":"CLOSE",ink,1);
    SDL_RenderPresent(offline_ui.renderer);offline_ui.painted=SDL_GetTicks();
}
static void offline_hide(void)
{
    offline_ui.visible=0;offline_ui.field=0;SDL_StopTextInput();SDL_HideWindow(offline_ui.window);
}
static int offline_event(SDL_Event *e,SDL_Window *main_window,TsUiState *ui)
{
    if(!offline_ui.window || event_window_id(e)!=offline_ui.id)return 0;
    if(e->type==SDL_WINDOWEVENT) {
        if(e->window.event==SDL_WINDOWEVENT_CLOSE)offline_hide();
        if(e->window.event==SDL_WINDOWEVENT_FOCUS_LOST)SDL_StopTextInput();
        if(e->window.event==SDL_WINDOWEVENT_FOCUS_GAINED && offline_ui.field)SDL_StartTextInput();
    }
    if(e->type==SDL_KEYDOWN && e->key.keysym.sym==SDLK_ESCAPE){offline_hide();return 1;}
    if(e->type==SDL_MOUSEBUTTONDOWN && e->button.button==SDL_BUTTON_LEFT) {
        int x,y;
        float fx,fy;SDL_RenderWindowToLogical(offline_ui.renderer,e->button.x,e->button.y,&fx,&fy);x=(int)fx;y=(int)fy;
        offline_ui.field=0;SDL_StopTextInput();
        if(y>=349 && y<377) {
            if(x>=318 && x<598)offline_hide();
            if(x>=22 && x<302) {
                if(offline_job.active)offline_job_cancel();
                else {
                    OfflineOptions o={.rate=offline_ui.rate,.bits=offline_ui.bits,.sources=offline_ui.sources,.channels=2};
                    if(!offline_time(offline_ui.duration,&o.seconds) || !offline_time(offline_ui.tail,&o.tail))
                        snprintf(offline_ui.message,sizeof(offline_ui.message),"Enter seconds or minutes:seconds.");
                    else if(offline_valid(&o,offline_ui.message,sizeof(offline_ui.message))) {
                        offline_hide();offline_ui.choosing=1;
                        browser_open(ui,TS_BROWSER_RENDER_WAV);ts_browser_set_filename(&ui->browser,"tapesister-render.wav");
                        SDL_ShowWindow(main_window);SDL_RaiseWindow(main_window);
                    }
                }
            }
        } else if(!offline_job.active) {
            if(y>=72 && y<103){if(x>=22 && x<220)offline_ui.sources^=4;if(x>=252 && x<500)offline_ui.sources^=2;}
            if(y>=133 && y<166 && x>=22 && x<598){offline_ui.field=x<320?1:2;offline_ui.select_all=1;SDL_StartTextInput();}
            if(y>=183 && y<215 && x>=22 && x<308)offline_ui.bits=offline_ui.bits==24?32:24;
        }
    }
    if(offline_ui.field && !offline_job.active) {
        char *value=offline_ui.field==1?offline_ui.duration:offline_ui.tail;
        if(e->type==SDL_TEXTINPUT) {
            const char *t=e->text.text;int valid=*t!=0;for(const char *s=t;*s;++s)if(!isdigit((unsigned char)*s) && *s!=':' && *s!='.')valid=0;
            if(valid){if(offline_ui.select_all)*value=0;offline_ui.select_all=0;if(strlen(value)+strlen(t)<32)strcat(value,t);}
        }
        if(e->type==SDL_KEYDOWN) {
            SDL_Keycode key=e->key.keysym.sym;
            if(key==SDLK_a && (e->key.keysym.mod&KMOD_CTRL))offline_ui.select_all=1;
            if(key==SDLK_BACKSPACE || key==SDLK_DELETE){if(offline_ui.select_all)*value=0;else if(*value)value[strlen(value)-1]=0;offline_ui.select_all=0;}
            if(key==SDLK_TAB){offline_ui.field=offline_ui.field==1?2:1;offline_ui.select_all=1;}
            if(key==SDLK_RETURN){offline_ui.field=0;SDL_StopTextInput();}
        }
    }
    offline_ui.painted=SDL_GetTicks()-100;return 1;
}
static void offline_close(void)
{
    offline_job_shutdown();
    if(offline_ui.renderer)SDL_DestroyRenderer(offline_ui.renderer);
    if(offline_ui.window)SDL_DestroyWindow(offline_ui.window);
    memset(&offline_ui,0,sizeof(offline_ui));
}
