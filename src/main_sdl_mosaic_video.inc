static int mosaic_video_path(const char *path)
{
    const char *extension=strrchr(path,'.');if(!extension)return 0;
    const char *types[]={".mp4",".m4v",".mov",".mkv",".webm",".avi",".mpg",".mpeg",".ts",".gif"};
    for(unsigned i=0;i<sizeof(types)/sizeof(*types);++i)if(!SDL_strcasecmp(extension,types[i]))return 1;
    return 0;
}
static void mosaic_video_open(TsUiState *ui,MosaicController *c,uint64_t id)
{
    mosaic_drag_end(ui,c);mosaic_picks_clear(ui,c);ui->mosaic_video_id=id;ui->mosaic_video_epoch=ui->mosaic->epoch;
    ui->mosaic_video_time=ui->mosaic_time;ui->mosaic_video_x=ui->mosaic_xscroll;
    ui->mosaic_video_open=1;
}
static int mosaic_video_event(const SDL_Event *event,SDL_AudioDeviceID device,AudioState *audio,
                              TsUiState *ui,const TsInstrument *instrument,MosaicController *c,int x,int y)
{
    if(!ui->mosaic_open || ui->browser.mode!=TS_BROWSER_CLOSED)return 0;
    if(c->ui_epoch!=ui->mosaic->epoch)mosaic_sources_refresh(ui,instrument,c);
    int down=event->type==SDL_MOUSEBUTTONDOWN,key=event->type==SDL_KEYDOWN,wheel=event->type==SDL_MOUSEWHEEL;
    if(ui->mosaic_video_open) {
        if(ui->mosaic_video_epoch!=ui->mosaic->epoch){ui->mosaic_video_open=0;return 1;}
        if(key){if(event->key.keysym.sym==SDLK_ESCAPE || event->key.keysym.sym==SDLK_RETURN)ui->mosaic_video_open=0;return 1;}
        if(!down && !wheel)return event->type==SDL_TEXTINPUT || event->type==SDL_MOUSEBUTTONUP;
        TsMosaicEvent *e=ts_mosaic_find(ui->mosaic,ui->mosaic_video_id);
        if(down && y>=110 && y<126 && x>=106 && x<534) {
            ui->mosaic_video_open=0;browser_open(ui,x<314?TS_BROWSER_MOSAIC_VIDEO:TS_BROWSER_MOSAIC_VIDEO_POOL);return 1;
        }
        if(down && y>=308 && y<324) {
            if(x>=414 && x<534)ui->mosaic_video_open=0;
            else if(x>=106 && x<226 && e){if(device)SDL_LockAudioDevice(device);ts_mosaic_checkpoint(ui->mosaic);ts_mosaic_delete(ui->mosaic,e->id);if(device)SDL_UnlockAudioDevice(device);ui->mosaic_video_open=0;}
            return 1;
        }
        if(down && y>=259 && y<275 && x>=106 && x<534) {
            if(x<314){ui->mosaic_video_open=0;mandala_editor_video_loaded(ui,&audio->sister.mandala);}
            else if(e){if(device)SDL_LockAudioDevice(device);ui->mosaic->video_id=e->id;++ui->mosaic->video_serial;if(device)SDL_UnlockAudioDevice(device);
                if(!ts_mandala_active()){TsMandalaPreferences p={ui->config.visual_quality,ui->config.visual_display,ui->config.visual_fullscreen};ts_mandala_open(&audio->sister.mandala,&p,ui->status,sizeof(ui->status));}}
            return 1;
        }
        if(!e)return 1;
        int change=(y>=157 && y<173 && down) || (y>=188 && y<204);
        if(change && x>=106 && x<534) {
            int delta=wheel?event->wheel.y:down && event->button.button==SDL_BUTTON_RIGHT?-1:1;
            if(wheel && event->wheel.direction==SDL_MOUSEWHEEL_FLIPPED)delta=-delta;
            if(device)SDL_LockAudioDevice(device);ts_mosaic_checkpoint(ui->mosaic);
            if(y<173){if(x<314)e->video_random_start^=1;else e->video_bw^=1;}
            else if(x<314)e->video_transition=(e->video_transition+3+(delta<0?-1:1))%3;
            else e->video_fade=fmax(0,fmin(30,e->video_fade+delta*.25));
            ++e->revision;if(device)SDL_UnlockAudioDevice(device);
        }
        return 1;
    }
    if(ts_ui_foreground_panel_open(ui))return 0;
    if(key && !event->key.repeat && event->key.keysym.sym==SDLK_v &&
       (event->key.keysym.mod&(KMOD_CTRL|KMOD_SHIFT))==(KMOD_CTRL|KMOD_SHIFT) && !(event->key.keysym.mod&(KMOD_ALT|KMOD_GUI))){mosaic_video_open(ui,c,0);return 1;}
    if(down && event->button.button==SDL_BUTTON_RIGHT && x>=151 && x<ts_ui_mosaic_canvas_right(ui) && y>=66 && y<TS_MOSAIC_CANVAS_BOTTOM) {
        double time=ui->mosaic_scroll+(y-66)/ui->mosaic_scale,left=ui->mosaic_xscroll+(x-151)/(ui->mosaic_hscale>0?ui->mosaic_hscale:1);
        const TsMosaicEvent *primary=ts_mosaic_find(ui->mosaic,ui->mosaic_selected);
        for(int pass=TS_MOSAIC_EVENTS;pass>=0;--pass) {
            int i=pass==TS_MOSAIC_EVENTS?(primary?(int)(primary-ui->mosaic->events):-1):pass;
            if(i<0 || (pass!=TS_MOSAIC_EVENTS && primary==&ui->mosaic->events[i]))continue;
            const TsMosaicEvent *e=&ui->mosaic->events[i];
            if(!e->id || time<e->start || time>=e->start+e->duration || left<e->x || left>=e->x+e->width)continue;
            if(!e->source->video_path[0])return 0;
            mosaic_select_only(ui,e->id);mosaic_video_open(ui,c,e->id);return 1;
        }
        mosaic_video_open(ui,c,0);ui->mosaic_video_time=time;ui->mosaic_video_x=left;return 1;
    }
    return 0;
}
static int mosaic_video_drop(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,MosaicController *c,const char *path,SDL_Window *window)
{
    struct stat info;if(stat(path,&info))return 0;
    int pool=S_ISDIR(info.st_mode);if(!pool && !mosaic_video_path(path))return 0;
    if(ts_ui_foreground_panel_open(ui)){snprintf(ui->status,sizeof(ui->status),"CLOSE THE OPEN PANEL BEFORE ADDING VIDEO");return 1;}
    TsMosaicSource *source=ts_mosaic_video_source(ui->mosaic,path,pool);if(!source)return 1;
    int px,py,x,y;SDL_GetMouseState(&px,&py);logical_mouse(window,px,py,&x,&y);
    double time=ui->mosaic_time,left=ui->mosaic_xscroll;
    if(x>=151 && x<ts_ui_mosaic_canvas_right(ui) && y>=66 && y<TS_MOSAIC_CANVAS_BOTTOM){time=ui->mosaic_scroll+(y-66)/ui->mosaic_scale;left+=(x-151)/(ui->mosaic_hscale>0?ui->mosaic_hscale:1);}
    if(device)SDL_LockAudioDevice(device);ts_mosaic_checkpoint(ui->mosaic);TsMosaicEvent *e=ts_mosaic_add(ui->mosaic,source,time,left);
    if(e){e->duration=1.5;e->width=120;e->video_fade=2;mosaic_select_only(ui,e->id);}
    if(device)SDL_UnlockAudioDevice(device);
    if(e){audio->sister.mandala.video_enabled=1;mosaic_video_open(ui,c,e->id);}
    return 1;
}
static void mosaic_video_follow(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui)
{
    static uint64_t epoch,serial,command;static int was_active;
    if(!ts_mandala_active()){was_active=0;return;}
    TsMosaic *m=audio->gallery?ts_gallery_active(audio->gallery):audio->mosaic;
    if(!m)return;
    TsVisualCue cue={0};uint64_t next_epoch,next_serial;int changed,active=ts_mandala_active();
    if(device)SDL_LockAudioDevice(device);
    next_epoch=m->epoch;next_serial=m->video_serial;changed=epoch!=next_epoch || serial!=next_serial || (active && !was_active);
    if(changed) {
        const TsMosaicEvent *e=ts_mosaic_find(m,m->video_id);
        if(!e || !e->source->video_path[0])e=ts_mosaic_video_at(m,m->time);
        if(e){snprintf(cue.path,sizeof(cue.path),"%s",e->source->video_path);cue.pool=e->source->video_pool;cue.random_start=e->video_random_start;
            cue.transition=e->video_transition;cue.fade=e->video_fade;cue.monochrome=e->video_bw;}
    }
    if(device)SDL_UnlockAudioDevice(device);
    if(changed){epoch=next_epoch;serial=next_serial;cue.serial=++command;ts_mandala_video_cue(&cue);}
    was_active=active;(void)ui;
}
