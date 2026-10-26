/* Timeline cues share the ordinary seek path. Metadata never runs in audio. */
static double mosaic_marker_playhead(SDL_AudioDeviceID device,TsUiState *ui)
{
    if(device)SDL_LockAudioDevice(device);
    double time=ui->mosaic->time;
    if(device)SDL_UnlockAudioDevice(device);
    return fmax(0,fmin(1000000,isfinite(time)?time:0));
}
static void mosaic_marker_commit(SDL_AudioDeviceID device,TsUiState *ui,int index,TsMosaicMarker value)
{
    if(device)SDL_LockAudioDevice(device);
    int ok=ts_mosaic_set_marker(ui->mosaic,index,&value);
    if(device)SDL_UnlockAudioDevice(device);
    if(ok)ui->mosaic_marker_selected=value.stored?index+1:0;
}
static void mosaic_marker_add(SDL_AudioDeviceID device,TsUiState *ui)
{
    for(int i=0;i<TS_MOSAIC_MARKERS;++i)if(!ui->mosaic->markers[i].stored) {
        TsMosaicMarker value={.stored=1,.time=mosaic_marker_playhead(device,ui)};
        mosaic_marker_commit(device,ui,i,value);
        snprintf(ui->status,sizeof(ui->status),"MARKER %02d AT %.3FS / CLICK JUMP / RIGHT EDIT",i+1,value.time);return;
    }
    snprintf(ui->status,sizeof(ui->status),"ALL 16 MARKERS USED / RIGHT CLICK TO EDIT OR DELETE");
}
static void mosaic_marker_jump(SDL_AudioDeviceID device,TsUiState *ui,int index)
{
    const TsMosaicMarker *v=&ui->mosaic->markers[index];
    if(!v->stored){snprintf(ui->status,sizeof(ui->status),"EMPTY MARKER %02d / SHIFT CLICK SET / RIGHT EDIT",index+1);return;}
    if(device)SDL_LockAudioDevice(device);
    ts_mosaic_seek(ui->mosaic,v->time);ui->mosaic_time=ui->mosaic->time;
    if(device)SDL_UnlockAudioDevice(device);
    ui->mosaic_marker_selected=index+1;
    snprintf(ui->status,sizeof(ui->status),"MARKER %02d / %.3FS / %.31s",index+1,v->time,v->name);
}
static void mosaic_marker_close(TsUiState *ui)
{ui->mosaic_marker_open=0;SDL_StopTextInput();}
static void mosaic_marker_edit(SDL_AudioDeviceID device,TsUiState *ui,MosaicController *c,int index)
{
    const TsMosaicMarker *v=&ui->mosaic->markers[index];
    ui->mosaic_marker_open=index+1;ui->mosaic_marker_field=0;c->marker_epoch=ui->mosaic->epoch;
    snprintf(ui->mosaic_marker_name,sizeof(ui->mosaic_marker_name),"%s",v->name);
    snprintf(ui->mosaic_marker_time,sizeof(ui->mosaic_marker_time),"%.9f",v->stored?v->time:mosaic_marker_playhead(device,ui));
    snprintf(ui->mosaic_marker_status,sizeof(ui->mosaic_marker_status),"TAB NEXT / CTRL+A REPLACE / ENTER APPLY / ESC CANCEL");
    SDL_StartTextInput();
}
static int mosaic_marker_event(const SDL_Event *event,SDL_AudioDeviceID device,
    TsUiState *ui,MosaicController *c,int x,int y,SDL_Keymod mod)
{
    int key=event->type==SDL_KEYDOWN,down=event->type==SDL_MOUSEBUTTONDOWN;
    int up=event->type==SDL_MOUSEBUTTONUP,motion=event->type==SDL_MOUSEMOTION;
    int left=down && event->button.button==SDL_BUTTON_LEFT;
    SDL_Keycode k=key?event->key.keysym.sym:0;
    if(ui->mosaic_marker_open) {
        if(c->marker_epoch!=ui->mosaic->epoch){mosaic_marker_close(ui);return 1;}
        int apply=(key && !event->key.repeat && (k==SDLK_RETURN || k==SDLK_KP_ENTER));
        int cancel=key && k==SDLK_ESCAPE,remove=0;
        if(left && y>=244 && y<268){apply|=x>=160 && x<256;remove=x>=272 && x<368;cancel|=x>=384 && x<480;}
        if(cancel){mosaic_marker_close(ui);return 1;}
        if(remove){mosaic_marker_commit(device,ui,ui->mosaic_marker_open-1,(TsMosaicMarker){0});mosaic_marker_close(ui);return 1;}
        if(left && x>=160 && x<480 && y>=160 && y<180)ui->mosaic_marker_field=0;
        if(left && x>=160 && x<330 && y>=201 && y<221)ui->mosaic_marker_field=1;
        if(left && x>=340 && x<480 && y>=201 && y<221)
            snprintf(ui->mosaic_marker_time,sizeof(ui->mosaic_marker_time),"%.9f",mosaic_marker_playhead(device,ui));
        char *field=ui->mosaic_marker_field?ui->mosaic_marker_time:ui->mosaic_marker_name;
        size_t n=strlen(field);
        if(event->type==SDL_TEXTINPUT)for(const unsigned char *p=(const unsigned char *)event->text.text;*p && n<31;++p)
            if(*p>=32 && *p<=126){field[n++]=(char)*p;field[n]=0;}
        if(key) {
            if(k==SDLK_BACKSPACE && n)field[n-1]=0;
            if(k==SDLK_a && (mod&KMOD_CTRL))field[0]=0;
            if(k==SDLK_TAB)ui->mosaic_marker_field=!ui->mosaic_marker_field;
        }
        if(apply) {
            char *end;double time=strtod(ui->mosaic_marker_time,&end);
            if(end==ui->mosaic_marker_time || *end || !isfinite(time) || time<0 || time>1000000)
                snprintf(ui->mosaic_marker_status,sizeof(ui->mosaic_marker_status),"TIME MUST BE 0-1000000 SECONDS");
            else {
                TsMosaicMarker value={.stored=1,.time=time};snprintf(value.name,sizeof(value.name),"%s",ui->mosaic_marker_name);
                mosaic_marker_commit(device,ui,ui->mosaic_marker_open-1,value);mosaic_marker_close(ui);
            }
        }
        return key || down || up || motion || event->type==SDL_TEXTINPUT || event->type==SDL_MOUSEWHEEL || event->type==SDL_KEYUP;
    }
    if(ui->mosaic_views_open) {
        if(key && k==SDLK_ESCAPE){ui->mosaic_views_open=0;return 1;}
        if(left && !(x>=200 && x<440 && y>=160 && y<250)){ui->mosaic_views_open=0;return 1;}
        if(down && x>=220 && x<420 && y>=204 && y<224) {
            int index=(x-220)/40;TsMosaicView *v=&ui->mosaic->views[index];
            if(event->button.button==SDL_BUTTON_MIDDLE)*v=(TsMosaicView){0};
            else if(left) {
                if(!v->stored || (mod&KMOD_SHIFT))*v=(TsMosaicView){1,ui->mosaic_scroll,ui->mosaic_xscroll,ui->mosaic_scale,ui->mosaic_hscale};
                else {ui->mosaic_scroll=v->scroll;ui->mosaic_xscroll=v->xscroll;ui->mosaic_scale=v->scale;ui->mosaic_hscale=v->hscale;ui->mosaic_follow=0;c->zoom_saved=0;}
            }
        }
        return key || down || up || motion || event->type==SDL_TEXTINPUT || event->type==SDL_MOUSEWHEEL || event->type==SDL_KEYUP;
    }
    if(ui->mosaic_marker_drag) {
        if(c->marker_epoch!=ui->mosaic->epoch || (key && k==SDLK_ESCAPE) ||
           (down && event->button.button!=SDL_BUTTON_LEFT) || (motion && !(event->motion.state&SDL_BUTTON_LMASK))) {
            ui->mosaic_marker_drag=0;return 1;
        }
        if(motion || (up && event->button.button==SDL_BUTTON_LEFT)) {
            if(abs(y-c->marker_y)>=3)c->marker_moved=1;
            if(c->marker_moved)ui->mosaic_marker_preview=fmax(0,fmin(1000000,c->marker_time+(y-c->marker_y)/c->marker_scale));
            if(up) {
                int index=ui->mosaic_marker_drag-1;
                if(c->marker_moved){TsMosaicMarker v=ui->mosaic->markers[index];v.time=ui->mosaic_marker_preview;mosaic_marker_commit(device,ui,index,v);}
                else mosaic_marker_jump(device,ui,index);
                ui->mosaic_marker_drag=0;
            }
            return 1;
        }
        return key || down || up || event->type==SDL_MOUSEWHEEL;
    }
    if(c->drag || c->pan_active || c->mix_drag || c->volume_drag || ui->mosaic_loop_drag)return 0;
    if(key && k==SDLK_m && !(mod&(KMOD_CTRL|KMOD_ALT|KMOD_GUI|KMOD_SHIFT))) {
        if(!event->key.repeat)mosaic_marker_add(device,ui);return 1;
    }
    if(left && y>=TS_MOSAIC_MARKER_Y && y<TS_MOSAIC_MARKER_Y+16 && x>=10 && x<108) {
        if(x<60){ui->mosaic_views_open=1;mosaic_drag_end(ui,c);}
        else mosaic_marker_add(device,ui);
        return 1;
    }
    int slot=-1,flag=0;
    if(x>=TS_MOSAIC_MARKER_X && x<TS_MOSAIC_MARKER_X+TS_MOSAIC_MARKERS*TS_MOSAIC_MARKER_STEP &&
       y>=TS_MOSAIC_MARKER_Y && y<TS_MOSAIC_MARKER_Y+16)slot=(x-TS_MOSAIC_MARKER_X)/TS_MOSAIC_MARKER_STEP;
    else if(!(mod&KMOD_SHIFT) && x>=127 && x<151 && y>=66 && y<TS_MOSAIC_CANVAS_BOTTOM) {
        for(int i=TS_MOSAIC_MARKERS-1;i>=0;--i) {
            int top=ts_ui_mosaic_marker_y(ui,i);
            if(top>=0 && y>=top && y<top+10){slot=i;flag=1;break;}
        }
    }
    if(slot<0)return 0;
    if(motion) {
        const TsMosaicMarker *v=&ui->mosaic->markers[slot];
        snprintf(ui->status,sizeof(ui->status),"%02d %s %.3FS / %.24s / RIGHT EDIT",slot+1,v->stored?"JUMP":"EMPTY",v->time,v->name);return 1;
    }
    if(down && event->button.button==SDL_BUTTON_RIGHT){mosaic_marker_edit(device,ui,c,slot);return 1;}
    if(left) {
        if(mod&KMOD_SHIFT) {
            TsMosaicMarker v=ui->mosaic->markers[slot];v.stored=1;v.time=mosaic_marker_playhead(device,ui);mosaic_marker_commit(device,ui,slot,v);
        } else if(flag) {
            ui->mosaic_marker_drag=slot+1;ui->mosaic_marker_preview=c->marker_time=ui->mosaic->markers[slot].time;
            c->marker_epoch=ui->mosaic->epoch;c->marker_y=y;c->marker_moved=0;c->marker_scale=ui->mosaic_scale>0?ui->mosaic_scale:24;
        } else mosaic_marker_jump(device,ui,slot);
    }
    return down || up;
}
