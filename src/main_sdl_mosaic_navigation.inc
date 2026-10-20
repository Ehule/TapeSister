/* View navigation never changes card placement or the playback clock. A middle
   press is held until release so grabbing the canvas cannot accidentally seek. */
static void mosaic_fit_all(TsUiState *ui,MosaicController *c)
{
    double right=0;
    for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(ui->mosaic->events[i].id)
        right=fmax(right,ui->mosaic->events[i].x+ui->mosaic->events[i].width);
    ui->mosaic_scale=fmin(1200,300/fmax(.25,ts_mosaic_end(ui->mosaic)));
    ui->mosaic_hscale=fmin(1,(TS_MOSAIC_CANVAS_RIGHT-151)/fmax(1,right));
    ui->mosaic_scroll=ui->mosaic_xscroll=0;ui->mosaic_follow=0;c->zoom_saved=0;
}
static int mosaic_navigation_event(const SDL_Event *event,SDL_AudioDeviceID device,
    TsUiState *ui,MosaicController *c,int x,int y,SDL_Keymod mod)
{
    int key=event->type==SDL_KEYDOWN,down=event->type==SDL_MOUSEBUTTONDOWN;
    int motion=event->type==SDL_MOUSEMOTION,up=event->type==SDL_MOUSEBUTTONUP;
    if(c->pan_active) {
        if(key || (down && event->button.button!=SDL_BUTTON_MIDDLE)) {
            c->pan_active=c->pan_moved=0;
            if(key && event->key.keysym.sym==SDLK_ESCAPE)return 1;
        } else if(motion || (up && event->button.button==SDL_BUTTON_MIDDLE)) {
            if(motion && !(event->motion.state&SDL_BUTTON_MMASK)) {
                c->pan_active=c->pan_moved=0;return 1;
            }
            int dx=x-c->pan_x,dy=y-c->pan_y;
            if(abs(dx)>=4 || abs(dy)>=4)c->pan_moved=1;
            if(c->pan_moved) {
                ui->mosaic_scroll=fmax(0,fmin(1000000,c->pan_scroll-dy/c->pan_scale));
                ui->mosaic_xscroll=fmax(0,fmin(1000000,c->pan_xscroll-dx/c->pan_hscale));
                ui->mosaic_follow=0;c->zoom_saved=0;
            }
            if(up) {
                if(!c->pan_moved) {
                    if(device)SDL_LockAudioDevice(device);
                    ts_mosaic_seek(ui->mosaic,c->pan_scroll+(c->pan_y-66)/c->pan_scale);
                    if(device)SDL_UnlockAudioDevice(device);
                }
                c->pan_active=c->pan_moved=0;
            }
            return 1;
        } else if(event->type==SDL_MOUSEWHEEL)return 1;
    }
    if(down && event->button.button==SDL_BUTTON_MIDDLE && x>=113 && x<631 && y>=66 && y<376) {
        if(c->drag || c->mix_drag || c->volume_drag)return 1;
        c->pan_active=1;c->pan_moved=0;c->pan_x=x;c->pan_y=y;
        c->pan_scroll=ui->mosaic_scroll;c->pan_xscroll=ui->mosaic_xscroll;
        c->pan_scale=ui->mosaic_scale>0?ui->mosaic_scale:24;
        c->pan_hscale=ui->mosaic_hscale>0?ui->mosaic_hscale:1;
        return 1;
    }
    if((motion || down) && !c->drag && !c->mix_drag && !c->volume_drag &&
       x>=10 && x<108 && y>=TS_MOSAIC_VIEW_Y && y<TS_MOSAIC_VIEW_Y+16) {
        int index=(x-10)/20;if((x-10)%20>=18)return 0;
        TsMosaicView *v=&ui->mosaic->views[index];
        if(motion) {
            snprintf(ui->status,sizeof(ui->status),"VIEW %d / CLICK %s / SHIFT SAVE / MIDDLE CLEAR",index+1,v->stored?"RECALL":"SAVE");return 1;
        }
        mosaic_drag_end(ui,c);
        if(event->button.button==SDL_BUTTON_MIDDLE) {
            if(device)SDL_LockAudioDevice(device);
            *v=(TsMosaicView){0};
            if(device)SDL_UnlockAudioDevice(device);
            snprintf(ui->status,sizeof(ui->status),"VIEW %d CLEARED",index+1);
        } else if(event->button.button==SDL_BUTTON_LEFT) {
            if(!v->stored || (mod&KMOD_SHIFT)) {
                if(device)SDL_LockAudioDevice(device);
                *v=(TsMosaicView){1,ui->mosaic_scroll,ui->mosaic_xscroll,ui->mosaic_scale,ui->mosaic_hscale};
                if(device)SDL_UnlockAudioDevice(device);
                snprintf(ui->status,sizeof(ui->status),"VIEW %d SAVED / CLICK TO RECALL",index+1);
            } else {
                ui->mosaic_scroll=v->scroll;ui->mosaic_xscroll=v->xscroll;
                ui->mosaic_scale=v->scale;ui->mosaic_hscale=v->hscale;
                ui->mosaic_follow=0;c->zoom_saved=0;
                snprintf(ui->status,sizeof(ui->status),"VIEW %d RECALLED / F FOLLOW / H PLAYHEAD",index+1);
            }
        }
        return 1;
    }
    if(key && !(mod&(KMOD_CTRL|KMOD_ALT|KMOD_GUI))) {
        SDL_Keycode k=event->key.keysym.sym;
        int page=k==SDLK_PAGEUP || k==SDLK_PAGEDOWN;
        if(k!=SDLK_f && k!=SDLK_h && !page && !(k==SDLK_z && (mod&KMOD_SHIFT)))return 0;
        if(event->key.repeat && !page)return 1;
        if(c->drag || c->mix_drag || c->volume_drag)return 1;
        if(k==SDLK_f) {
            ui->mosaic_follow=!ui->mosaic_follow;
            snprintf(ui->status,sizeof(ui->status),"FOLLOW %s / H SHOW PLAYHEAD",ui->mosaic_follow?"ON":"OFF");
        } else if(k==SDLK_z) {
            mosaic_fit_all(ui,c);snprintf(ui->status,sizeof(ui->status),"WHOLE ARRANGEMENT / Z FIT SELECTION / F FOLLOW");
        } else {
            if(k==SDLK_h) {
                if(device)SDL_LockAudioDevice(device);
                double time=ui->mosaic->time;
                if(device)SDL_UnlockAudioDevice(device);
                ui->mosaic_scroll=fmax(0,time-155/ui->mosaic_scale);
            } else ui->mosaic_scroll=fmax(0,fmin(1000000,ui->mosaic_scroll+(k==SDLK_PAGEUP?-1:1)*280/ui->mosaic_scale));
            ui->mosaic_follow=0;c->zoom_saved=0;
            snprintf(ui->status,sizeof(ui->status),"%s / F RESUME FOLLOW",k==SDLK_h?"PLAYHEAD IN VIEW":"VIEW MOVED");
        }
        return 1;
    }
    return 0;
}
