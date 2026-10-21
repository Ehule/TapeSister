/* Ruler gestures preview locally; only release commits a single undo step. */
static void mosaic_loop_status(TsUiState *ui,TsMosaicRange range)
{
    snprintf(ui->status,sizeof(ui->status),"RANGE %.3F - %.3F S / %.3F S / L LOOP",
        range.start,range.end,range.end-range.start);
}
static void mosaic_loop_commit(SDL_AudioDeviceID device,TsUiState *ui,TsMosaicRange range)
{
    TsMosaic *m=ui->mosaic;
    if(!isfinite(range.start) || !isfinite(range.end) || range.start<0 || range.end>1000000 ||
       !((range.start==0 && range.end==0) || range.end>=range.start+.001))return;
    if(device)SDL_LockAudioDevice(device);
    if(range.start!=m->loop_range.start || range.end!=m->loop_range.end) {
        ts_mosaic_checkpoint(m);ts_mosaic_set_loop_range(m,range.start,range.end);
    }
    if(device)SDL_UnlockAudioDevice(device);
    if(range.end>range.start)mosaic_loop_status(ui,range);
    else snprintf(ui->status,sizeof(ui->status),"TIME RANGE CLEARED / REPEAT USES WHOLE ARRANGEMENT");
}
static void mosaic_loop_toggle(SDL_AudioDeviceID device,TsUiState *ui)
{
    if(device)SDL_LockAudioDevice(device);
    ts_mosaic_checkpoint(ui->mosaic);ts_mosaic_set_repeat(ui->mosaic,!ui->mosaic->repeat);
    if(device)SDL_UnlockAudioDevice(device);
    snprintf(ui->status,sizeof(ui->status),"REPEAT %s / %s",ui->mosaic->repeat?"ON":"OFF",
        ui->mosaic->loop_range.end>ui->mosaic->loop_range.start?"SELECTED TIME RANGE":"WHOLE ARRANGEMENT");
}
static TsMosaicRange mosaic_loop_from_cards(const TsUiState *ui)
{
    TsMosaicRange range={1000000,0};
    for(int i=0;i<TS_MOSAIC_EVENTS;++i)if(mosaic_chosen(ui,i)) {
        range.start=fmin(range.start,ui->mosaic->events[i].start);
        range.end=fmax(range.end,ui->mosaic->events[i].start+ui->mosaic->events[i].duration);
    }
    return range.end>range.start?range:(TsMosaicRange){0,0};
}
static void mosaic_loop_close(TsUiState *ui)
{ui->mosaic_loop_open=ui->mosaic_loop_field=0;SDL_StopTextInput();}
static int mosaic_loop_event(const SDL_Event *event,SDL_AudioDeviceID device,
    TsUiState *ui,MosaicController *c,int x,int y,SDL_Keymod mod)
{
    int key=event->type==SDL_KEYDOWN,down=event->type==SDL_MOUSEBUTTONDOWN;
    int up=event->type==SDL_MOUSEBUTTONUP,motion=event->type==SDL_MOUSEMOTION;
    SDL_Keycode k=key?event->key.keysym.sym:0;
    if(ui->mosaic_loop_open) {
        if(c->loop_epoch!=ui->mosaic->epoch){mosaic_loop_close(ui);return 1;}
        if((key && k==SDLK_ESCAPE) || (down && event->button.button==SDL_BUTTON_LEFT &&
           x>=330 && x<470 && y>=246 && y<270)){mosaic_loop_close(ui);return 1;}
        char *field=ui->mosaic_loop_text[ui->mosaic_loop_field-1];
        if(event->type==SDL_TEXTINPUT) {
            size_t n=strlen(field),more=strlen(event->text.text);
            if(n+more<sizeof(ui->mosaic_loop_text[0]))memcpy(field+n,event->text.text,more+1);
        }
        if(key) {
            size_t n=strlen(field);
            if(k==SDLK_BACKSPACE && n)field[n-1]=0;
            if(k==SDLK_a && (mod&KMOD_CTRL))field[0]=0;
            if(k==SDLK_TAB)ui->mosaic_loop_field=3-ui->mosaic_loop_field;
        }
        if(down && event->button.button==SDL_BUTTON_LEFT && y>=173 && y<197) {
            if(x>=140 && x<310)ui->mosaic_loop_field=1;
            if(x>=330 && x<500)ui->mosaic_loop_field=2;
        }
        if((key && !event->key.repeat && (k==SDLK_RETURN || k==SDLK_KP_ENTER)) ||
           (down && event->button.button==SDL_BUTTON_LEFT && x>=170 && x<310 && y>=246 && y<270)) {
            char *tail_a,*tail_b;
            double a=strtod(ui->mosaic_loop_text[0],&tail_a),b=strtod(ui->mosaic_loop_text[1],&tail_b);
            if(tail_a==ui->mosaic_loop_text[0] || tail_b==ui->mosaic_loop_text[1] || *tail_a || *tail_b ||
               !isfinite(a) || !isfinite(b) || a<0 || b>1000000 || b<a+.001)
                snprintf(ui->mosaic_loop_status,sizeof(ui->mosaic_loop_status),"USE 0-1000000 SECONDS; END > START BY 0.001+");
            else {mosaic_loop_commit(device,ui,(TsMosaicRange){a,b});mosaic_loop_close(ui);}
        }
        return key || down || up || motion || event->type==SDL_TEXTINPUT || event->type==SDL_MOUSEWHEEL || event->type==SDL_KEYUP;
    }
    if(ui->mosaic_loop_drag) {
        if(c->loop_epoch!=ui->mosaic->epoch || (key && k==SDLK_ESCAPE) ||
           (down && event->button.button!=SDL_BUTTON_LEFT) ||
           (motion && !(event->motion.state&SDL_BUTTON_LMASK))) {
            ui->mosaic_loop_drag=0;snprintf(ui->status,sizeof(ui->status),"TIME RANGE EDIT CANCELLED");return 1;
        }
        if(motion || (up && event->button.button==SDL_BUTTON_LEFT)) {
            double time=fmax(0,fmin(1000000,ui->mosaic_scroll+(fmax(66,fmin(375,y))-66)/ui->mosaic_scale));
            if(ui->mosaic_loop_drag==1)ui->mosaic_loop_preview=(TsMosaicRange){fmin(time,c->loop_anchor),fmax(time,c->loop_anchor)};
            else if(ui->mosaic_loop_drag==2)ui->mosaic_loop_preview.start=fmin(time,ui->mosaic_loop_preview.end-.001);
            else ui->mosaic_loop_preview.end=fmax(time,ui->mosaic_loop_preview.start+.001);
            mosaic_loop_status(ui,ui->mosaic_loop_preview);
            if(up) {
                if(ui->mosaic_loop_preview.end>=ui->mosaic_loop_preview.start+.001)
                    mosaic_loop_commit(device,ui,ui->mosaic_loop_preview);
                ui->mosaic_loop_drag=0;
            }
            return 1;
        }
        return key || down || up || event->type==SDL_MOUSEWHEEL;
    }
    if(key && k==SDLK_l && !(mod&(KMOD_CTRL|KMOD_ALT|KMOD_GUI))) {
        if(event->key.repeat || c->drag || c->pan_active || c->mix_drag || c->volume_drag)return 1;
        if(mod&KMOD_SHIFT) {
            TsMosaicRange range=mosaic_loop_from_cards(ui);
            if(range.end>range.start)mosaic_loop_commit(device,ui,range);
            else snprintf(ui->status,sizeof(ui->status),"SELECT CARDS FIRST / SHIFT+L SETS THEIR TIME RANGE");
        } else mosaic_loop_toggle(device,ui);
        return 1;
    }
    int ruler=x>=113 && x<151 && y>=66 && y<376;
    if(!ruler || c->drag || c->pan_active || c->mix_drag || c->volume_drag)return 0;
    TsMosaicRange range=ui->mosaic->loop_range;
    double time=ui->mosaic_scroll+(y-66)/ui->mosaic_scale;
    if(motion) {
        snprintf(ui->status,sizeof(ui->status),"RULER: SHIFT DRAG RANGE / DOUBLE CLICK TIMES / L LOOP");return 1;
    }
    if(down && event->button.button==SDL_BUTTON_RIGHT && range.end>range.start &&
       time>=range.start-4/ui->mosaic_scale && time<=range.end+4/ui->mosaic_scale) {
        mosaic_loop_commit(device,ui,(TsMosaicRange){0,0});return 1;
    }
    if(!down || event->button.button!=SDL_BUTTON_LEFT)return 0;
    if(event->button.clicks>=2 && !(mod&KMOD_SHIFT)) {
        if(range.end<=range.start)range=mosaic_loop_from_cards(ui);
        if(range.end<=range.start){range.start=fmax(0,fmin(999999,time));range.end=range.start+1;}
        snprintf(ui->mosaic_loop_text[0],sizeof(ui->mosaic_loop_text[0]),"%.9f",range.start);
        snprintf(ui->mosaic_loop_text[1],sizeof(ui->mosaic_loop_text[1]),"%.9f",range.end);
        ui->mosaic_loop_open=ui->mosaic_loop_field=1;c->loop_epoch=ui->mosaic->epoch;
        snprintf(ui->mosaic_loop_status,sizeof(ui->mosaic_loop_status),"TAB NEXT / CTRL+A REPLACE / ENTER APPLY / ESC CANCEL");
        SDL_StartTextInput();return 1;
    }
    int handle=0;
    if(range.end>range.start) {
        double a=fabs((time-range.start)*ui->mosaic_scale),b=fabs((time-range.end)*ui->mosaic_scale);
        if(fmin(a,b)<=4)handle=a<=b?2:3;
    }
    if((mod&KMOD_SHIFT) || handle) {
        ui->mosaic_loop_drag=(mod&KMOD_SHIFT)?1:handle;c->loop_epoch=ui->mosaic->epoch;
        c->loop_anchor=fmax(0,fmin(1000000,time));
        ui->mosaic_loop_preview=ui->mosaic_loop_drag==1?(TsMosaicRange){c->loop_anchor,c->loop_anchor}:range;
        return 1;
    }
    return 0;
}
