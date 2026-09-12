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
static double mosaic_volume_position(const TsUiState *ui,int drag,double end,int y)
{
    if(drag==2)return 0;
    if(drag==3)return 1;
    y=y<TS_MOSAIC_ENV_TOP+TS_MOSAIC_ENV_PAD?TS_MOSAIC_ENV_TOP+TS_MOSAIC_ENV_PAD:
      y>TS_MOSAIC_ENV_BOTTOM-TS_MOSAIC_ENV_PAD?TS_MOSAIC_ENV_BOTTOM-TS_MOSAIC_ENV_PAD:y;
    return fmax(0,fmin(1,ts_ui_mosaic_volume_time(ui,y)/end));
}
static float mosaic_volume_value(int x)
{
    return (float)fmax(0,fmin(1,(double)(x-TS_MOSAIC_ENV_X-TS_MOSAIC_ENV_PAD)/
        (TS_MOSAIC_ENV_RIGHT-TS_MOSAIC_ENV_X-2*TS_MOSAIC_ENV_PAD)));
}
static int mosaic_volume_endpoint(const TsUiState *ui,double end,int y)
{
    double first=ts_ui_mosaic_volume_y(ui,0),last=ts_ui_mosaic_volume_y(ui,end);
    int top=TS_MOSAIC_ENV_TOP+TS_MOSAIC_ENV_PAD,bottom=TS_MOSAIC_ENV_BOTTOM-TS_MOSAIC_ENV_PAD;
    if(first>=top && first<=bottom && fabs(y-first)<=4 && fabs(y-first)<=fabs(y-last))return 2;
    if(last>=top && last<=bottom && fabs(y-last)<=4)return 3;
    return 1;
}
static void mosaic_volume_point(SDL_AudioDeviceID device,TsUiState *ui,MosaicController *c,int x,int y)
{
    double position=mosaic_volume_position(ui,c->volume_drag,c->volume_end,y);
    float value=mosaic_volume_value(x);
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
        if(event->type==SDL_MOUSEMOTION){
            /* Small sideways handle drags stay on the endpoint; drawing into
               the body continues a normal stroke from that endpoint. */
            if(c->volume_drag>=2 && y>TS_MOSAIC_ENV_TOP && y<TS_MOSAIC_ENV_BOTTOM &&
               fabs(y-ts_ui_mosaic_volume_y(ui,c->volume_drag==2?0:c->volume_end))>8)c->volume_drag=1;
            mosaic_volume_point(device,ui,c,x,y);return 1;
        }
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
    int drawing=y>=TS_MOSAIC_ENV_TOP && y<=TS_MOSAIC_ENV_BOTTOM;
    static const char *names[]={"RESET VOLUME", "SMOOTH VOLUME", "START MATCHES END", "END MATCHES START", "RAMP BETWEEN ENDPOINTS"};
    if(event->type==SDL_MOUSEMOTION && !c->drag && !c->mix_drag) {
        int endpoint=drawing?mosaic_volume_endpoint(ui,ts_mosaic_end(ui->mosaic),y):1;
        snprintf(ui->status,sizeof(ui->status),"%s",action>=0?names[action]:!drawing?"VOLUME ENVELOPE":
            endpoint==2?"DRAG START LEVEL - LEFT QUIET, RIGHT FULL":endpoint==3?"DRAG END LEVEL - LEFT QUIET, RIGHT FULL":
            "DRAW VOLUME - LEFT QUIET, RIGHT FULL");return 1;
    }
    if(event->type!=SDL_MOUSEBUTTONDOWN || c->drag || c->mix_drag)return 0;
    if(event->button.button!=SDL_BUTTON_LEFT)return 1;
    if(action<0 && !drawing)return 1;
    double end=ts_mosaic_end(ui->mosaic);
    if(end<=0 && action<0){snprintf(ui->status,sizeof(ui->status),"ADD AN EVENT BEFORE DRAWING THE VOLUME ENVELOPE");return 1;}
    if(device)SDL_LockAudioDevice(device);
    ts_mosaic_checkpoint(ui->mosaic);
    if(action>=0)ts_mosaic_volume_action(ui->mosaic,(TsMosaicEnvelopeAction)action);
    if(device)SDL_UnlockAudioDevice(device);
    if(action>=0){
        const char *message=names[action];
        if(ui->mosaic->repeat && (action==TS_MOSAIC_ENV_MATCH_START || action==TS_MOSAIC_ENV_MATCH_END))
            message="REPEAT ALREADY LINKS START AND END LEVELS";
        if(action==TS_MOSAIC_ENV_RAMP && ui->mosaic->volume[0]==ui->mosaic->volume[TS_MOSAIC_ENVELOPE_POINTS-1])
            message=ui->mosaic->repeat?"FLAT RAMP - REPEAT LINKS THE ENDPOINTS":"FLAT RAMP - START AND END LEVELS ARE EQUAL";
        snprintf(ui->status,sizeof(ui->status),"%s",message);return 1;
    }
    c->volume_drag=mosaic_volume_endpoint(ui,end,y);c->volume_epoch=ui->mosaic->epoch;c->volume_end=end;
    c->volume_position=mosaic_volume_position(ui,c->volume_drag,end,y);
    c->volume_value=mosaic_volume_value(x);
    mosaic_volume_point(device,ui,c,x,y);return 1;
}
