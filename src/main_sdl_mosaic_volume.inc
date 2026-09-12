/* A drag edits normalized arrangement time; one stroke is one undo step. */
static int mosaic_volume_action_at(int x,int y)
{
    if(y>=68 && y<84) {
        if(x>=TS_MOSAIC_ENV_X && x<TS_MOSAIC_ENV_X+15)return TS_MOSAIC_ENV_RESET;
        if(x>=TS_MOSAIC_ENV_X+23 && x<TS_MOSAIC_ENV_X+38)return TS_MOSAIC_ENV_MATCH_START;
        if(x>=TS_MOSAIC_ENV_RIGHT-15 && x<TS_MOSAIC_ENV_RIGHT)return TS_MOSAIC_ENV_SMOOTH;
    }
    if(y>=359 && y<375) {
        if(x>=TS_MOSAIC_ENV_X && x<TS_MOSAIC_ENV_X+15)return TS_MOSAIC_ENV_RAMP;
        if(x>=TS_MOSAIC_ENV_RIGHT-15 && x<TS_MOSAIC_ENV_RIGHT)return TS_MOSAIC_ENV_MATCH_END;
    }
    return -1;
}
static void mosaic_volume_point(SDL_AudioDeviceID device,TsUiState *ui,MosaicController *c,int x,int y)
{
    double position=fmax(0,fmin(1,(ui->mosaic_scroll+(y-66)/ui->mosaic_scale)/c->volume_end));
    float value=(float)fmax(0,fmin(1,(double)(x-TS_MOSAIC_ENV_X-1)/(TS_MOSAIC_ENV_RIGHT-TS_MOSAIC_ENV_X-2)));
    if(device)SDL_LockAudioDevice(device);
    ts_mosaic_volume_draw(ui->mosaic,c->volume_position,c->volume_value,position,value);
    if(device)SDL_UnlockAudioDevice(device);
    c->volume_position=position;c->volume_value=value;
    snprintf(ui->status,sizeof(ui->status),"MOSAIC VOLUME %.0f%% AT %.2fS",100*value,position*c->volume_end);
}
static int mosaic_volume_event(const SDL_Event *event,SDL_AudioDeviceID device,TsUiState *ui,MosaicController *c,int x,int y)
{
    if(c->volume_epoch!=ui->mosaic->epoch)c->volume_drag=0;
    if(c->volume_drag) {
        if(event->type==SDL_MOUSEMOTION){mosaic_volume_point(device,ui,c,x,y);return 1;}
        if(event->type==SDL_MOUSEBUTTONUP){c->volume_drag=0;return 1;}
        if(event->type==SDL_MOUSEWHEEL)return 1;
        if(event->type==SDL_KEYDOWN && event->key.keysym.sym==SDLK_ESCAPE) {
            if(device)SDL_LockAudioDevice(device);ts_mosaic_undo(ui->mosaic,0);if(device)SDL_UnlockAudioDevice(device);
            c->volume_drag=0;return 1;
        }
        if(event->type==SDL_KEYDOWN)c->volume_drag=0;
    }
    int inside=x>=TS_MOSAIC_ENV_X && x<=TS_MOSAIC_ENV_RIGHT && y>=66 && y<376;
    if(!inside)return 0;
    int action=mosaic_volume_action_at(x,y);
    static const char *names[]={"RESET VOLUME", "SMOOTH VOLUME", "START MATCHES END", "END MATCHES START", "RAMP BETWEEN ENDPOINTS"};
    if(event->type==SDL_MOUSEMOTION && !c->drag && !c->mix_drag) {
        snprintf(ui->status,sizeof(ui->status),"%s",action>=0?names[action]:"DRAW VOLUME - LEFT QUIET, RIGHT FULL");return 1;
    }
    if(event->type!=SDL_MOUSEBUTTONDOWN || c->drag || c->mix_drag)return 0;
    if(event->button.button!=SDL_BUTTON_LEFT)return 1;
    double end=ts_mosaic_end(ui->mosaic);
    if(end<=0 && action<0){snprintf(ui->status,sizeof(ui->status),"ADD AN EVENT BEFORE DRAWING THE VOLUME ENVELOPE");return 1;}
    if(device)SDL_LockAudioDevice(device);
    ts_mosaic_checkpoint(ui->mosaic);
    if(action>=0)ts_mosaic_volume_action(ui->mosaic,(TsMosaicEnvelopeAction)action);
    if(device)SDL_UnlockAudioDevice(device);
    if(action>=0){snprintf(ui->status,sizeof(ui->status),"%s",names[action]);return 1;}
    c->volume_drag=1;c->volume_epoch=ui->mosaic->epoch;c->volume_end=end;
    c->volume_position=fmax(0,fmin(1,(ui->mosaic_scroll+(y-66)/ui->mosaic_scale)/end));
    c->volume_value=(float)fmax(0,fmin(1,(double)(x-TS_MOSAIC_ENV_X-1)/(TS_MOSAIC_ENV_RIGHT-TS_MOSAIC_ENV_X-2)));
    mosaic_volume_point(device,ui,c,x,y);return 1;
}
