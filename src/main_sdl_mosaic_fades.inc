/* Direct handles keep the opening selection and opening values for the entire
   gesture. Alt changes its interpretation without accumulating rounding error. */
static void mosaic_card_pan_status(TsUiState *ui,float pan)
{
    char value[24];
    if(pan==0)snprintf(value,sizeof(value),"CENTER");
    else snprintf(value,sizeof(value),"%c %.0F%%",pan<0?'L':'R',fabsf(pan)*100);
    snprintf(ui->status,sizeof(ui->status),"PAN %s / GROUP RELATIVE / ALT: CENTER + MATCH / ESC CANCEL",value);
}
static void mosaic_fade_set(SDL_AudioDeviceID device,TsUiState *ui,MosaicController *c,double delta,int equal)
{
    int pan=c->drag==9,part=c->drag==7?1:2,count=ui->mosaic_ghost_count;
    if(!count)return;
    double common=delta;
    if(equal && !pan) {
        double limit=86400;
        for(int i=0;i<count;++i) {
            const TsMosaicEvent *o=&ui->mosaic_ghosts[i];double in,out;ts_mosaic_event_fades(o,&in,&out);
            limit=fmin(limit,o->duration-(part==1?out:in));
        }
        common=fmax(0,fmin(limit,common));
    }
    int changed=0;
    double ins[TS_MOSAIC_EVENTS],outs[TS_MOSAIC_EVENTS];float pans[TS_MOSAIC_EVENTS];
    TsMosaicEvent *targets[TS_MOSAIC_EVENTS];
    for(int i=0;i<count;++i) {
        const TsMosaicEvent *o=&ui->mosaic_ghosts[i];TsMosaicEvent *e=ts_mosaic_find(ui->mosaic,o->id);
        if(!e)return;targets[i]=e;
        ts_mosaic_event_fades(o,&ins[i],&outs[i]);pans[i]=o->pan;
        if(pan) {
            double value=(equal?0:o->pan)+delta;
            pans[i]=(float)fmax(-1,fmin(1,value));
            if(fabs(value)*c->card_pan_span<=1+1e-6)pans[i]=0;
            changed|=fabsf(pans[i]-e->pan)>1e-7f;
        } else {
            if(part==1)ins[i]=equal?common:fmax(0,fmin(o->duration-outs[i],ins[i]+delta));
            else outs[i]=equal?common:fmax(0,fmin(o->duration-ins[i],outs[i]+delta));
            changed|=fabs(ins[i]-e->fade_in)>1e-9 || fabs(outs[i]-e->fade_out)>1e-9;
        }
    }
    if(changed) {
        if(device)SDL_LockAudioDevice(device);
        if(!c->drag_moved)ts_mosaic_checkpoint(ui->mosaic);
        for(int i=0;i<count;++i) {
            TsMosaicEvent *e=targets[i];
            if(pan)e->pan=pans[i];else {e->fade_in=ins[i];e->fade_out=outs[i];}
        }
        ++ui->mosaic->revision;c->drag_moved=1;
        if(device)SDL_UnlockAudioDevice(device);
    }
    TsMosaicEvent *e=ts_mosaic_find(ui->mosaic,c->card_mix_event);
    if(!e)return;
    if(pan)mosaic_card_pan_status(ui,e->pan);
    else snprintf(ui->status,sizeof(ui->status),"FADE %s %.3FS / %d CARDS / %s / ALT MATCH / ESC CANCEL",
        part==1?"IN":"OUT",part==1?e->fade_in:e->fade_out,count,equal?"FROM ZERO":"RELATIVE");
}
static void mosaic_fade_motion(SDL_AudioDeviceID device,TsUiState *ui,MosaicController *c,int x,int y,SDL_Keymod mod,int force)
{
    c->drag_pointer_x=x;c->drag_pointer_y=y;
    int distance=c->drag==9?x-c->drag_x:y-c->drag_y;
    double delta=distance/(c->drag==9?c->card_pan_span:ui->mosaic_scale);
    if(c->drag==8)delta=-delta;
    if(force || c->drag_moved || abs(distance)>=2)mosaic_fade_set(device,ui,c,delta,(mod&KMOD_ALT)!=0);
}
static int mosaic_fades_event(const SDL_Event *event,SDL_AudioDeviceID device,TsUiState *ui,
                              MosaicController *c,int x,int y,SDL_Keymod mod)
{
    int motion=event->type==SDL_MOUSEMOTION,down=event->type==SDL_MOUSEBUTTONDOWN;
    if(c->drag>=7 && c->drag<=9) {
        if(motion){mosaic_fade_motion(device,ui,c,x,y,mod,0);return 1;}
        if((event->type==SDL_KEYDOWN || event->type==SDL_KEYUP) &&
           (event->key.keysym.sym==SDLK_LALT || event->key.keysym.sym==SDLK_RALT)) {
            mosaic_fade_motion(device,ui,c,c->drag_pointer_x,c->drag_pointer_y,(SDL_Keymod)event->key.keysym.mod,1);return 1;
        }
        if(event->type==SDL_MOUSEBUTTONUP && event->button.button==SDL_BUTTON_LEFT) {
            mosaic_drag_end(ui,c);return 1;
        }
        if(event->type==SDL_KEYDOWN) {
            SDL_Keycode key=event->key.keysym.sym;
            if(key==SDLK_LSHIFT || key==SDLK_RSHIFT || key==SDLK_LCTRL || key==SDLK_RCTRL || key==SDLK_LGUI || key==SDLK_RGUI)return 1;
            if(key==SDLK_ESCAPE && c->drag_moved) {
                if(device)SDL_LockAudioDevice(device);ts_mosaic_undo(ui->mosaic,0);
                if(device)SDL_UnlockAudioDevice(device);
            }
            mosaic_drag_end(ui,c);if(key==SDLK_ESCAPE)return 1;
        } else if(down || event->type==SDL_MOUSEWHEEL)return 1;
    }
    if(motion){ui->mosaic_card_mix_event=0;ui->mosaic_card_mix_part=ui->mosaic_card_mix_group=0;ui->mosaic_resize_event=0;ui->mosaic_resize_part=0;}
    if((!motion && !down) || c->drag || c->mix_drag || c->volume_drag || c->pan_active ||
       ui->mosaic_loop_drag || (mod&(KMOD_SHIFT|KMOD_CTRL|KMOD_GUI)) ||
       x<151 || x>=ts_ui_mosaic_canvas_right(ui) || y<66 || y>=TS_MOSAIC_CANVAS_BOTTOM)return 0;
    TsMosaicEvent *e=mosaic_card_at(ui,x,y);
    int grip=e?mosaic_resize_handle_at(ui,e,x,y):0;
    if(grip) {
        ui->mosaic_resize_event=e->id;ui->mosaic_resize_part=grip==6?1:2;
        if(motion) {
            snprintf(ui->status,sizeof(ui->status),"%s RESIZE GRIP / DRAG / S SCALE / T TOP B BOTTOM C CENTER",grip==6?"TOP":"BOTTOM");return 1;
        }
        return 0;
    }
    if(e && !e->source->video_path[0]) {
        for(int part=1;part<=3;++part) {
            double hx,hy,span=0;
            int visible=part==3?ts_ui_mosaic_pan_handle(ui,e,&hx,&hy,&span):ts_ui_mosaic_fade_handle(ui,e,part,&hx,&hy);
            if(!visible || fabs(x-hx)>4 || fabs(y-hy)>4)continue;
            if(down && event->button.button!=SDL_BUTTON_LEFT && event->button.button!=SDL_BUTTON_RIGHT)return 0;
            int selected=0;for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(ui->mosaic->events[i].id==e->id){selected=mosaic_chosen(ui,i);break;}
            double in,out;ts_mosaic_event_fades(e,&in,&out);
            if(down) {
                mosaic_drag_end(ui,c);mosaic_picks_clear(ui,c);
                if(!selected)mosaic_select_only(ui,e->id);else mosaic_selection_repair(ui);
                mosaic_source_select_event(ui,e);
                int total=mosaic_selected_events(ui,ui->mosaic_ghosts),audio_count=0;
                for(int i=0;i<total;++i)if(!ui->mosaic_ghosts[i].source->video_path[0])
                    ui->mosaic_ghosts[audio_count++]=ui->mosaic_ghosts[i];
                ui->mosaic_ghost_count=audio_count;
                c->card_mix_event=e->id;c->fade_in=in;c->fade_out=out;c->drag_y=y;c->drag_x=x;c->drag=part+6;
                c->drag_pointer_x=x;c->drag_pointer_y=y;c->drag_epoch=ui->mosaic->epoch;
                c->card_pan=e->pan;c->card_pan_span=span;
            }
            ui->mosaic_card_mix_event=e->id;ui->mosaic_card_mix_part=part;
            ui->mosaic_card_mix_group=down?ui->mosaic_ghost_count>1:selected;
            if(part==3)mosaic_card_pan_status(ui,e->pan);
            else snprintf(ui->status,sizeof(ui->status),"FADE %s %.3FS / GROUP RELATIVE / ALT: ZERO + MATCH",
                part==1?"IN":"OUT",part==1?in:out);
            if(down && (event->button.button==SDL_BUTTON_RIGHT || event->button.clicks>=2)) {
                mosaic_fade_set(device,ui,c,0,1);mosaic_drag_end(ui,c);
            } else if(down && (mod&KMOD_ALT))mosaic_fade_set(device,ui,c,0,1);
            return 1;
        }
        return 0; /* A covered card's handle must not steal the front card. */
    }
    return 0;
}
