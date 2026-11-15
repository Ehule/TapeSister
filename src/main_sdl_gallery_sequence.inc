/* Section editing is a UI transaction; audio only sees the published plan. */
static int gallery_sequence_time(const char *text,double *value)
{
    char extra;double minutes,seconds;
    if(strchr(text,':')) {
        if(sscanf(text,"%lf:%lf %c",&minutes,&seconds,&extra)!=2 || !isfinite(minutes) || minutes<0 ||
           floor(minutes)!=minutes || !isfinite(seconds) || seconds<0 || seconds>=60)return 0;
        *value=minutes*60+seconds;
    } else {
        char *end;*value=strtod(text,&end);if(end==text || *end)return 0;
    }
    return isfinite(*value) && *value>=0 && *value<=86400;
}
static void gallery_sequence_follow(SDL_AudioDeviceID device,AudioState *a,TsUiState *ui,TsInstrument *bank,MosaicController *c)
{
    TsMosaicGallery *g=c->pages?c->pages->gallery:NULL;
    if(!g || c->active || c->drag || c->mix_drag || c->volume_drag || c->pan_active ||
       ui->mosaic_grab || ui->mosaic_edit_choice || ui->mosaic_pattern_choice || ui->mosaic_recording || ui->mosaic_live_recording)return;
    if(device)SDL_LockAudioDevice(device);
    TsMosaic *next=ts_gallery_active(g);
    int changed=next!=ui->mosaic;
    if(device)SDL_UnlockAudioDevice(device);
    if(!changed)return;
    TsSamplePages *pages=c->pages;
    for(int i=0;i<g->count;++i)if(g->canvas[i].scene==ui->mosaic) {
        g->canvas[i].scroll=ui->mosaic_scroll;g->canvas[i].xscroll=ui->mosaic_xscroll;
        g->canvas[i].scale=ui->mosaic_scale;g->canvas[i].hscale=ui->mosaic_hscale;
    }
    mosaic_drag_end(ui,c);mosaic_picks_clear(ui,c);
    if(c->clipboard_epoch==ui->mosaic->epoch && c->clipboard.source && c->clipboard.source->pins)--c->clipboard.source->pins;
    for(int i=0;i<256;++i)if(c->pattern_sources[i] && c->pattern_sources[i]->pins)--c->pattern_sources[i]->pins;
    if(device)SDL_LockAudioDevice(device);
    TsMosaicCanvas to=g->canvas[g->active];
    ui->mosaic=pages->mosaic=a->mosaic=to.scene;ui->mosaic_gallery=a->gallery=g;
    a->mosaic_keyboard_sample=NULL;ui->mosaic_live_key_valid=0;
    ui->mosaic_source_keys=ui->mosaic_source_key_count=0;
    if(device)SDL_UnlockAudioDevice(device);
    runtime_mosaic_sources_clear_selection(a);mosaic_controller_free(c);c->pages=pages;
    mosaic_select_only(ui,0);ui->mosaic_source_selected=-1;ui->mosaic_live_status[0]=0;
    ui->mosaic_scroll=to.scroll;ui->mosaic_xscroll=to.xscroll;
    ui->mosaic_scale=to.scale>0?to.scale:24;ui->mosaic_hscale=to.hscale>0?to.hscale:1;
    ui->mosaic_loaded_view_epoch=ui->mosaic->epoch;mosaic_sources_refresh(ui,bank,c);
    matrix_window.dirty=1;
}
static int gallery_sequence_start(SDL_AudioDeviceID device,AudioState *a,TsUiState *ui,TsInstrument *bank,MosaicController *c,int step)
{
    TsMosaicGallery *g=c->pages->gallery;
    if(mosaic_gallery_busy(a,ui,c))return 0;
    if(g->outgoing>=0){snprintf(ui->mosaic_gallery_status,sizeof(ui->mosaic_gallery_status),"WAIT FOR THE CURRENT CROSSFADE");return 0;}
    if(!runtime_note_preserve_workspace(device,a,ui))return 0;
    if(device)SDL_LockAudioDevice(device);
    int ok=ts_gallery_sequence_prepare(g,ui->mosaic_gallery_status,sizeof(ui->mosaic_gallery_status)) &&
        ts_gallery_sequence_play(g,step,ui->mosaic_gallery_status,sizeof(ui->mosaic_gallery_status));
    if(ok){a->gallery=g;a->mosaic=ts_gallery_active(g);a->engine_stopped=ui->mosaic_engine_stopped=0;}
    mosaic_gallery_snapshot(ui,g);
    if(device)SDL_UnlockAudioDevice(device);
    if(ok)gallery_sequence_follow(device,a,ui,bank,c);
    return ok;
}
static int gallery_sequence_event(const SDL_Event *event,SDL_AudioDeviceID device,AudioState *a,TsUiState *ui,
    TsInstrument *bank,MosaicController *c,int x,int y,SDL_Keymod mod)
{
    TsMosaicGallery *g=c->pages->gallery;int key=event->type==SDL_KEYDOWN;
    int click=event->type==SDL_MOUSEBUTTONDOWN,wheel=event->type==SDL_MOUSEWHEEL;
    SDL_Keycode k=key?event->key.keysym.sym:0;
    int right=click && event->button.button==SDL_BUTTON_RIGHT;
    int change=wheel?(event->wheel.direction==SDL_MOUSEWHEEL_FLIPPED?-event->wheel.y:event->wheel.y):right?-1:1;
    if(ui->gallery_sequence_edit) {
        size_t length=strlen(ui->gallery_sequence_text);
        if(event->type==SDL_TEXTINPUT)for(const char *p=event->text.text;*p && length<30;++p)
            if((*p>='0' && *p<='9') || *p==':' || *p=='.')ui->gallery_sequence_text[length++]=*p;
        ui->gallery_sequence_text[length]=0;
        if(key && k==SDLK_a && (mod&KMOD_CTRL))ui->gallery_sequence_text[0]=0;
        if(key && k==SDLK_BACKSPACE && length)ui->gallery_sequence_text[length-1]=0;
        if(key && (k==SDLK_RETURN || k==SDLK_KP_ENTER)) {
            double value;
            if(!gallery_sequence_time(ui->gallery_sequence_text,&value) || (ui->gallery_sequence_edit==3 && value>3600)) {
                snprintf(ui->mosaic_gallery_status,sizeof(ui->mosaic_gallery_status),"ENTER M:SS OR SECONDS / FADE MAX 3600S");return 1;
            }
            if(device)SDL_LockAudioDevice(device);
            TsGalleryStep *p=&g->sequence.step[ui->gallery_sequence_selected];
            if(ui->gallery_sequence_edit==1){p->in_marker=TS_GALLERY_POINT_TIME;p->in_seconds=value;}
            if(ui->gallery_sequence_edit==2){p->out_marker=TS_GALLERY_POINT_TIME;p->out_seconds=value;}
            if(ui->gallery_sequence_edit==3)p->fade_seconds=value;
            mosaic_gallery_snapshot(ui,g);
            if(device)SDL_UnlockAudioDevice(device);
            ui->project_modified=1;ui->gallery_sequence_edit=0;SDL_StopTextInput();
        }
        if(key && k==SDLK_ESCAPE){ui->gallery_sequence_edit=0;SDL_StopTextInput();}
        return 1;
    }
    if((key && k==SDLK_ESCAPE) || (click && x>=16 && x<98 && y>=16 && y<40)) {
        ui->gallery_sequence_open=0;return 1;
    }
    if(key && event->key.repeat)return 1;
    if(key && (k==SDLK_LEFT || k==SDLK_RIGHT || k==SDLK_UP || k==SDLK_DOWN)) {
        int delta=k==SDLK_LEFT?-1:k==SDLK_RIGHT?1:k==SDLK_UP?-4:4;
        ui->gallery_sequence_selected=SDL_clamp(ui->gallery_sequence_selected+delta,0,g->sequence.length-1);
        ui->gallery_sequence_page=ui->gallery_sequence_selected/16;return 1;
    }
    if(click && x>=16 && x<624 && y>=84 && y<228) {
        int cell=(y-84)/36*4+(x-16)/152,index=ui->gallery_sequence_page*16+cell;
        if((x-16)%152<146 && (y-84)%36<32 && index<g->sequence.length)ui->gallery_sequence_selected=index;
        return 1;
    }
    int play=(key && k==SDLK_SPACE) || (click && !right && x>=16 && x<74 && y>=48 && y<72);
    int launch=click && !right && x>=440 && x<618 && y>=316 && y<340;
    if((play || launch) && !g->player.running) {
        gallery_sequence_start(device,a,ui,bank,c,ui->gallery_sequence_selected);return 1;
    }
    if(right && ((y>=254 && y<278 && x>=220 && x<618) || (y>=284 && y<308 && x>=136 && x<280))) {
        ui->gallery_sequence_edit=y>=284?3:x<416?1:2;
        const TsGalleryStep *p=&g->sequence.step[ui->gallery_sequence_selected];
        double value=ui->gallery_sequence_edit==3?p->fade_seconds:ui->gallery_sequence_edit==1?
            ui->gallery_step_start[ui->gallery_sequence_selected]:ui->gallery_step_end[ui->gallery_sequence_selected];
        snprintf(ui->gallery_sequence_text,sizeof(ui->gallery_sequence_text),"%.3f",value);SDL_StartTextInput();return 1;
    }
    if(!click && !wheel && !play && !(key && (k==SDLK_RETURN || k==SDLK_KP_ENTER)))return 1;
    if(wheel && !change)return 1;
    if(device)SDL_LockAudioDevice(device);
    TsGallerySequence *s=&g->sequence;int selected=SDL_clamp(ui->gallery_sequence_selected,0,s->length-1);
    TsGalleryStep *p=&s->step[selected];int modified=0;
    if(play)ts_gallery_sequence_pause(g,!g->player.paused);
    else if(click && y>=48 && y<72) {
        if(x>=80 && x<138)ts_gallery_sequence_stop(g);
        else if(x>=144 && x<202){s->loop=!s->loop;modified=1;}
        else if(x>=208 && x<286){s->first=SDL_clamp(s->first+change,0,s->last);modified=1;}
        else if(x>=292 && x<370){s->last=SDL_clamp(s->last+change,s->first,s->length-1);modified=1;}
        else if(x>=468 && x<502)ui->gallery_sequence_page=SDL_max(0,ui->gallery_sequence_page-1);
        else if(x>=584 && x<618)ui->gallery_sequence_page=SDL_min(3,ui->gallery_sequence_page+1);
    } else if(wheel && y>=48 && y<72) {
        if(x>=208 && x<286){s->first=SDL_clamp(s->first+change,0,s->last);modified=1;}
        else if(x>=292 && x<370){s->last=SDL_clamp(s->last+change,s->first,s->length-1);modified=1;}
    } else if((click || wheel) && y>=254 && y<278) {
        if(x>=16 && x<212) {
            int canvas=0;for(int i=0;i<g->count;++i)if(g->canvas[i].id==p->canvas_id)canvas=i;
            canvas=(canvas+change%g->count+g->count)%g->count;p->canvas_id=g->canvas[canvas].id;modified=1;
        } else if(x>=220 && x<416){p->in_marker=((p->in_marker+1+change%17+17)%17)-1;modified=1;}
        else if(x>=424 && x<618){p->out_marker=((p->out_marker+2+change%18+18)%18)-2;modified=1;}
    } else if((click || wheel) && y>=284 && y<308) {
        if(x>=16 && x<128){p->repeats=SDL_clamp(p->repeats+change,1,999);modified=1;}
        else if(x>=136 && x<280){p->fade_seconds=fmax(0,fmin(3600,p->fade_seconds+change*((mod&KMOD_SHIFT)?.01:.25)));modified=1;}
        else if(x>=288 && x<448){p->curve=(p->curve+change%TS_GALLERY_FADE_CURVES+TS_GALLERY_FADE_CURVES)%TS_GALLERY_FADE_CURVES;modified=1;}
        else if(click && x>=456 && x<532 && g->player.running)g->player.queued=selected;
        else if(click && x>=540 && x<618)ts_gallery_sequence_next(g,-1,ui->mosaic_gallery_status,sizeof(ui->mosaic_gallery_status));
    } else if(key && (k==SDLK_RETURN || k==SDLK_KP_ENTER)) {
        if(g->player.running)g->player.queued=selected;
    } else if(launch)ts_gallery_sequence_next(g,selected,ui->mosaic_gallery_status,sizeof(ui->mosaic_gallery_status));
    else if(click && y>=316 && y<340 && x>=16 && x<432) {
        if(x<160) {
            TsGalleryStep added=x>=92?*p:(TsGalleryStep){.canvas_id=g->canvas[g->active].id,.in_marker=-1,.out_marker=-2,.fade_seconds=2,.repeats=1};
            int at=x>=92?selected+1:s->length;
            if(ts_gallery_sequence_insert(g,at,&added)){ui->gallery_sequence_selected=at;modified=1;}
        } else if(x>=168 && x<236 && s->length>1) {
            if(ts_gallery_sequence_remove(g,selected)){ui->gallery_sequence_selected=SDL_min(selected,s->length-1);modified=1;}
        } else if(x>=244) {
            int to=selected+(x<334?-1:1);
            if(ts_gallery_sequence_move(g,selected,to)){ui->gallery_sequence_selected=to;modified=1;}
        }
        ui->gallery_sequence_page=ui->gallery_sequence_selected/16;
    }
    if(modified){ui->project_modified=1;g->player.error[0]=0;snprintf(ui->mosaic_gallery_status,sizeof(ui->mosaic_gallery_status),"STEP EDITED / PLAYING SECTION KEPT / APPLIES ON NEXT VISIT");}
    if(g->player.running){a->mosaic=ts_gallery_active(g);a->engine_stopped=ui->mosaic_engine_stopped=0;}
    mosaic_gallery_snapshot(ui,g);
    if(device)SDL_UnlockAudioDevice(device);
    gallery_sequence_follow(device,a,ui,bank,c);return 1;
}
