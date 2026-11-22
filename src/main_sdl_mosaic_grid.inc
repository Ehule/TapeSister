static void mosaic_grid_status(TsUiState *ui)
{
    TsMosaicGrid g=ui->mosaic->grid;
    snprintf(ui->status,sizeof(ui->status),g.enabled?"ALT SNAP: GRID 1/%d %s / %.4GS / RIGHT-CLICK GRID: SETTINGS":"ALT SNAP: CARDS / GRID OFF / RIGHT-CLICK GRID: SETTINGS",
        g.division,g.unit==0?"SECOND":g.unit==1?"BEAT":"4 BEATS",ts_mosaic_grid_step(g,ui->mosaic_grid_quarter));
}
/* Quantize deliberate pointer seeks only; playback and authored marker/sequence
   destinations keep their exact timing. Publish the new cursor immediately. */
static void mosaic_grid_seek(SDL_AudioDeviceID device,TsUiState *ui,double time)
{
    if(device)SDL_LockAudioDevice(device);
    if(ui->mosaic->grid.enabled)time=ts_mosaic_grid_snap(ui->mosaic->grid,ui->mosaic_grid_quarter,time);
    ts_mosaic_seek(ui->mosaic,time);ui->mosaic_time=ui->mosaic->time;
    if(device)SDL_UnlockAudioDevice(device);
}
static int mosaic_grid_event(const SDL_Event *e,TsUiState *ui,MosaicController *c,int x,int y)
{
    if(!ui->mosaic_open || !ui->mosaic)return 0;
    int key=e->type==SDL_KEYDOWN,down=e->type==SDL_MOUSEBUTTONDOWN;
    if(ui->mosaic_grid_open) {
        int apply=key && !e->key.repeat && (e->key.keysym.sym==SDLK_RETURN || e->key.keysym.sym==SDLK_KP_ENTER);
        int cancel=key && e->key.keysym.sym==SDLK_ESCAPE;
        if(down && e->button.button==SDL_BUTTON_LEFT) {
            for(int i=0;i<3;++i)if(x>=136+i*124 && x<248+i*124 && y>=150 && y<174)ui->mosaic_grid_edit.unit=i;
            static const int divisions[]={1,2,3,4,6,8,12,16,24,32,48,64};
            for(int i=0;i<12;++i)if(x>=136+(i%6)*62 && x<190+(i%6)*62 && y>=202+(i/6)*30 && y<226+(i/6)*30)
                ui->mosaic_grid_edit.division=divisions[i];
            if(y>=278 && y<302){apply=x>=136 && x<306;cancel=x>=330 && x<500;}
        }
        if(apply){ui->mosaic->grid=ui->mosaic_grid_edit;mosaic_grid_status(ui);}
        if(apply || cancel)ui->mosaic_grid_open=0;
        return key || down || e->type==SDL_KEYUP || e->type==SDL_MOUSEBUTTONUP ||
            e->type==SDL_MOUSEMOTION || e->type==SDL_MOUSEWHEEL || e->type==SDL_TEXTINPUT;
    }
    if(!down || ui_dialog_open(ui) || c->active || c->drag || c->mix_drag || c->volume_drag || c->pan_active ||
       x<TS_MOSAIC_GRID_X || x>=TS_MOSAIC_GRID_X+TS_MOSAIC_GRID_W || y<TS_MOSAIC_GRID_Y || y>=TS_MOSAIC_GRID_Y+TS_MOSAIC_GRID_H)return 0;
    if(e->button.button==SDL_BUTTON_LEFT){ui->mosaic->grid.enabled^=1;mosaic_grid_status(ui);}
    if(e->button.button==SDL_BUTTON_RIGHT){ui->mosaic_grid_edit=ui->mosaic->grid;ui->mosaic_grid_open=1;}
    return 1;
}
static int mosaic_pitch_wheel(const SDL_Event *event,SDL_AudioDeviceID device,TsUiState *ui,MosaicController *c,int x,int y,SDL_Keymod mod)
{
    if(event->type!=SDL_MOUSEWHEEL || !(mod&KMOD_CTRL) || !(mod&KMOD_SHIFT) || (mod&(KMOD_ALT|KMOD_GUI)) ||
       ui_dialog_open(ui) || x<151 || x>=ts_ui_mosaic_canvas_right(ui) || y<66 || y>=TS_MOSAIC_CANVAS_BOTTOM)return 0;
    if(c->drag || c->mix_drag || c->volume_drag || c->pan_active)return 1;
    int delta=wheel_event_detents(event,0),low=-127,high=127,count=0;
    if(!delta)return 1; /* SDL accumulates fractional wheel motion into detents. */
    for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(mosaic_chosen(ui,i)) {
        const TsMosaicEvent *e=&ui->mosaic->events[i];if(e->source->video_path[0])continue;
        ++count;for(int n=0;n<e->note_count;++n){low=SDL_max(low,-e->notes[n]);high=SDL_min(high,127-e->notes[n]);}
    }
    /* Clamp the whole selection together to preserve every interval. */
    delta=SDL_clamp(delta,low,high);
    if(!count){snprintf(ui->status,sizeof(ui->status),"SELECT AUDIO OR PATTERN CARDS / CTRL+SHIFT+WHEEL: TRANSPOSE");return 1;}
    if(!delta){snprintf(ui->status,sizeof(ui->status),"NOTE LIMIT / SELECTED INTERVALS PRESERVED");return 1;}
    if(device)SDL_LockAudioDevice(device);
    ts_mosaic_checkpoint(ui->mosaic);
    for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(mosaic_chosen(ui,i)) {
        TsMosaicEvent *e=&ui->mosaic->events[i];if(e->source->video_path[0])continue;
        for(int n=0;n<e->note_count;++n)e->notes[n]+=delta;
        ++e->revision;
    }
    ++ui->mosaic->revision;
    if(device)SDL_UnlockAudioDevice(device);
    snprintf(ui->status,sizeof(ui->status),"TRANSPOSE %d CARD%s %+d SEMITONE%s / CTRL+Z: UNDO",count,count==1?"":"S",delta,abs(delta)==1?"":"S");
    return 1;
}
