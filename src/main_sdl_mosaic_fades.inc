/* Direct card fades/pan own only that card. Footer controls retain group editing.
   Drag kinds 7/8/9 share the normal gesture cancellation/focus lifecycle. */
static void mosaic_card_pan_status(TsUiState *ui,float pan)
{
    char value[24];
    if(pan==0)snprintf(value,sizeof(value),"CENTER");
    else snprintf(value,sizeof(value),"%c %.0F%%",pan<0?'L':'R',fabsf(pan)*100);
    snprintf(ui->status,sizeof(ui->status),"PAN %s / DRAG LEFT-RIGHT / RIGHT CLICK CENTER / ESC CANCEL",value);
}
static void mosaic_fade_set(SDL_AudioDeviceID device,TsUiState *ui,MosaicController *c,double value)
{
    TsMosaicEvent *e=ts_mosaic_find(ui->mosaic,c->card_mix_event);if(!e)return;
    if(c->drag==9) {
        float pan=(float)fmax(-1,fmin(1,value));
        if(fabs(value)*c->card_pan_span<=1+1e-6)pan=0;
        if(fabsf(pan-e->pan)>1e-7f) {
            if(device)SDL_LockAudioDevice(device);
            if(!c->drag_moved)ts_mosaic_checkpoint(ui->mosaic);
            e->pan=pan;++ui->mosaic->revision;c->drag_moved=1;
            if(device)SDL_UnlockAudioDevice(device);
        }
        mosaic_card_pan_status(ui,e->pan);return;
    }
    int part=c->drag==7?1:2;
    double in=c->fade_in,out=c->fade_out;
    if(part==1)in=fmax(0,fmin(e->duration-out,value));
    else out=fmax(0,fmin(e->duration-in,value));
    if(fabs(in-e->fade_in)>1e-9 || fabs(out-e->fade_out)>1e-9) {
        if(device)SDL_LockAudioDevice(device);
        if(!c->drag_moved)ts_mosaic_checkpoint(ui->mosaic);
        e->fade_in=in;e->fade_out=out;++ui->mosaic->revision;c->drag_moved=1;
        if(device)SDL_UnlockAudioDevice(device);
    }
    snprintf(ui->status,sizeof(ui->status),"FADE %s %.3FS / DRAG %s / RIGHT CLICK RESET / ESC CANCEL",
        part==1?"IN":"OUT",part==1?in:out,part==1?"DOWN":"UP");
}
static int mosaic_fades_event(const SDL_Event *event,SDL_AudioDeviceID device,TsUiState *ui,
                              MosaicController *c,int x,int y,SDL_Keymod mod)
{
    int motion=event->type==SDL_MOUSEMOTION,down=event->type==SDL_MOUSEBUTTONDOWN;
    if(c->drag>=7 && c->drag<=9) {
        if(motion) {
            int distance=c->drag==9?x-c->drag_x:y-c->drag_y;
            double delta=distance/(c->drag==9?c->card_pan_span:ui->mosaic_scale);
            if(c->drag_moved || abs(distance)>=2)
                mosaic_fade_set(device,ui,c,c->drag==9?c->card_pan+delta:
                    c->drag==7?c->fade_in+delta:c->fade_out-delta);
            return 1;
        }
        if(event->type==SDL_MOUSEBUTTONUP && event->button.button==SDL_BUTTON_LEFT) {
            mosaic_drag_end(ui,c);return 1;
        }
        if(event->type==SDL_KEYDOWN) {
            SDL_Keycode key=event->key.keysym.sym;
            if(key==SDLK_LSHIFT || key==SDLK_RSHIFT || key==SDLK_LALT || key==SDLK_RALT ||
               key==SDLK_LCTRL || key==SDLK_RCTRL || key==SDLK_LGUI || key==SDLK_RGUI)return 1;
            if(key==SDLK_ESCAPE && c->drag_moved) {
                if(device)SDL_LockAudioDevice(device);ts_mosaic_undo(ui->mosaic,0);
                if(device)SDL_UnlockAudioDevice(device);
            }
            mosaic_drag_end(ui,c);if(key==SDLK_ESCAPE)return 1;
        } else if(down || event->type==SDL_MOUSEWHEEL)return 1;
    }
    if(motion){ui->mosaic_card_mix_event=0;ui->mosaic_card_mix_part=0;}
    if((!motion && !down) || c->drag || c->mix_drag || c->volume_drag || c->pan_active ||
       ui->mosaic_loop_drag || (mod&(KMOD_SHIFT|KMOD_CTRL|KMOD_ALT|KMOD_GUI)) ||
       x<151 || x>=ts_ui_mosaic_canvas_right(ui) || y<66 || y>=TS_MOSAIC_CANVAS_BOTTOM)return 0;
    TsMosaicEvent *e=mosaic_card_at(ui,x,y);
    if(e && !e->source->video_path[0]) {
        for(int part=1;part<=3;++part) {
            double hx,hy,span=0;
            int visible=part==3?ts_ui_mosaic_pan_handle(ui,e,&hx,&hy,&span):ts_ui_mosaic_fade_handle(ui,e,part,&hx,&hy);
            if(!visible || fabs(x-hx)>4 || fabs(y-hy)>4)continue;
            if(down && event->button.button!=SDL_BUTTON_LEFT && event->button.button!=SDL_BUTTON_RIGHT)return 0;
            double in,out;ts_mosaic_event_fades(e,&in,&out);
            if(down) {
                mosaic_drag_end(ui,c);mosaic_picks_clear(ui,c);mosaic_select_only(ui,e->id);
                mosaic_source_select_event(ui,e);
                c->card_mix_event=e->id;c->fade_in=in;c->fade_out=out;c->drag_y=y;c->drag_x=x;c->drag=part+6;
                c->card_pan=e->pan;c->card_pan_span=span;
            }
            ui->mosaic_card_mix_event=e->id;ui->mosaic_card_mix_part=part;
            if(part==3)mosaic_card_pan_status(ui,e->pan);
            else snprintf(ui->status,sizeof(ui->status),"FADE %s %.3FS / DRAG %s / RIGHT CLICK RESET",
                part==1?"IN":"OUT",part==1?in:out,part==1?"DOWN":"UP");
            if(down && (event->button.button==SDL_BUTTON_RIGHT || event->button.clicks>=2)) {
                mosaic_fade_set(device,ui,c,0);mosaic_drag_end(ui,c);
            }
            return 1;
        }
        return 0; /* A covered card's handle must not steal the front card. */
    }
    return 0;
}
