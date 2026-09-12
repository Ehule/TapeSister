/* Arrangement controls live on the main thread; the callback smooths level,
   balance and speed without changing a voice's identity or phase. */
static int mosaic_control_hit(int x,int y)
{
    if(y>=56 && y<66 && x>=TS_MOSAIC_SPEED_X-4 && x<TS_MOSAIC_SPEED_X+TS_MOSAIC_SPEED_W+3)return 5;
    if(y<TS_MOSAIC_MIX_Y || y>=400)return 0;
    for(int i=0;i<4;++i)if(x>=TS_MOSAIC_MIX_X+i*TS_MOSAIC_MIX_STEP && x<TS_MOSAIC_MIX_X+i*TS_MOSAIC_MIX_STEP+TS_MOSAIC_MIX_W)return i+1;
    return 0;
}
static double mosaic_control_value(const TsMosaicEvent *e,int field)
{
    return field==1?e->gain:field==2?e->pan:field==3?e->fade_in:e->fade_out;
}
static double mosaic_control_point(TsUiState *ui,int field,int x)
{
    int left=field==5?TS_MOSAIC_SPEED_X:TS_MOSAIC_MIX_X+(field-1)*TS_MOSAIC_MIX_STEP+2;
    int width=field==5?TS_MOSAIC_SPEED_W:TS_MOSAIC_MIX_W-4;
    double amount=fmax(0,fmin(1,(double)(x-left)/(width-1)));
    if(field<=2 || field==5)if(fabs(amount-.5)<.014)amount=.5;
    if(field==5)return exp2(amount*2-1);
    if(field==1)return amount*2;
    if(field==2)return amount*2-1;
    TsMosaicEvent *e=ts_mosaic_find(ui->mosaic,ui->mosaic_selected);
    return e?amount*amount*e->duration:0;
}
static int mosaic_control_set(SDL_AudioDeviceID device,TsUiState *ui,MosaicController *c,int field,double value)
{
    if(!isfinite(value))return 0;
    int changed=0;TsMosaic *m=ui->mosaic;
    if(device)SDL_LockAudioDevice(device);
    if(field==5) {
        value=fmax(.5,fmin(2,value));changed=fabs(m->speed-value)>1e-10;
        if(changed) {
            if(!c->mix_changed)ts_mosaic_checkpoint(m);
            ts_mosaic_set_speed(m,value);
        }
    } else {
        for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(mosaic_chosen(ui,i)) {
            TsMosaicEvent *e=&m->events[i];
            double v=field==1?fmax(0,fmin(2,value)):field==2?fmax(-1,fmin(1,value)):fmax(0,fmin(e->duration,value));
            if(fabs(mosaic_control_value(e,field)-v)<1e-7)continue;
            if(!changed && !c->mix_changed)ts_mosaic_checkpoint(m);
            changed=1;
            if(field==1)e->gain=(float)v;
            else if(field==2)e->pan=(float)v;
            else if(field==3)e->fade_in=v;
            else e->fade_out=v;
        }
        if(changed)++m->revision;
    }
    if(device)SDL_UnlockAudioDevice(device);
    c->mix_changed|=changed;
    if(field==5)snprintf(ui->status,sizeof(ui->status),"MOSAIC SPEED %.3fX (%+.2f ST) - RIGHT CLICK RESETS",m->speed,12*log2(m->speed));
    else snprintf(ui->status,sizeof(ui->status),"EVENT %s - DRAG / WHEEL; SHIFT+WHEEL FINE; RIGHT CLICK RESETS",
        field==1?"LEVEL":field==2?"PAN":field==3?"FADE IN":"FADE OUT");
    return changed;
}
static void mosaic_controls_end(MosaicController *c)
{c->mix_drag=c->mix_changed=0;}
static int mosaic_controls_event(const SDL_Event *event,SDL_Window *window,SDL_AudioDeviceID device,
                                  TsUiState *ui,MosaicController *c,int x,int y,SDL_Keymod mod)
{
    if(c->mix_epoch!=ui->mosaic->epoch){mosaic_controls_end(c);c->mix_epoch=ui->mosaic->epoch;}
    if(event->type==SDL_WINDOWEVENT && event->window.event==SDL_WINDOWEVENT_FOCUS_LOST)mosaic_controls_end(c);
    if(c->mix_drag) {
        if(event->type==SDL_MOUSEMOTION) {
            mosaic_control_set(device,ui,c,c->mix_drag,mosaic_control_point(ui,c->mix_drag,x));return 1;
        }
        if(event->type==SDL_MOUSEBUTTONUP){mosaic_controls_end(c);return 1;}
        if(event->type==SDL_KEYDOWN && event->key.keysym.sym==SDLK_ESCAPE) {
            if(c->mix_changed) {
                if(device)SDL_LockAudioDevice(device);ts_mosaic_undo(ui->mosaic,0);if(device)SDL_UnlockAudioDevice(device);
            }
            mosaic_controls_end(c);return 1;
        }
        if(event->type==SDL_KEYDOWN)mosaic_controls_end(c);
    }
    int wheel=event->type==SDL_MOUSEWHEEL,down=event->type==SDL_MOUSEBUTTONDOWN;
    if(!wheel && !down)return 0;
    if(wheel){int mx,my;SDL_GetMouseState(&mx,&my);logical_mouse(window,mx,my,&x,&y);}
    int field=mosaic_control_hit(x,y);
    if(!field)return 0;
    TsMosaicEvent *selected=ts_mosaic_find(ui->mosaic,ui->mosaic_selected);
    if(field!=5 && !selected)return 1;
    mosaic_drag_end(ui,c);c->mix_changed=0;c->mix_epoch=ui->mosaic->epoch;
    if(wheel) {
        double delta=event->wheel.y*((mod&KMOD_SHIFT)?.1:1),value;
        if(field==5)value=ui->mosaic->speed*exp2(delta/12);
        else {
            value=mosaic_control_value(selected,field);
            if(field==1)value=value<=0 && delta<=0?0:fmax(.001,value)*pow(10,delta/20);
            else value+=delta*(field==2?.05:.1);
        }
        mosaic_control_set(device,ui,c,field,value);mosaic_controls_end(c);return 1;
    }
    if(event->button.button==SDL_BUTTON_RIGHT || (event->button.button==SDL_BUTTON_LEFT && event->button.clicks>=2)) {
        mosaic_control_set(device,ui,c,field,field==1 || field==5?1:0);mosaic_controls_end(c);return 1;
    }
    if(event->button.button==SDL_BUTTON_LEFT) {
        c->mix_drag=field;
        mosaic_control_set(device,ui,c,field,mosaic_control_point(ui,field,x));return 1;
    }
    return 1;
}
