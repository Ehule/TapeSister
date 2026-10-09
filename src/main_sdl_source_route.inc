/* Routing is a modeless performance control. Stable tile IDs keep an open
   window from silently retargeting when a page/slot changes. */
typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    TsFramebuffer *framebuffer;
    TsSourceRouteUi model, rendered;
    TsPalette palette;
    TsTileId tile;
    Uint32 id, presented;
    int visible, track, drag, rendered_valid;
} SourceRouteWindow;
static SourceRouteWindow source_route_window;

static TsBankSlot *source_route_tile(TsSamplePages *pages,TsInstrument *instrument,int *slot)
{
    for(int i=0;i<TS_BANK_SLOT_COUNT;++i)if(instrument->bank[i].occupied &&
       instrument->bank[i].tile_id==source_route_window.tile) {
        if(slot)*slot=i;return &instrument->bank[i];
    }
    TsTileLocation loc={0};
    const TsBankSlot *found=ts_sample_pages_find_tile(pages,instrument,source_route_window.tile,&loc);
    if(found && slot)*slot=loc.slot;
    return (TsBankSlot *)found;
}
static void source_route_hide(void)
{
    source_route_window.visible=source_route_window.drag=0;
    if(source_route_window.window)SDL_HideWindow(source_route_window.window);
}
static void source_route_close(void)
{
    if(source_route_window.texture)SDL_DestroyTexture(source_route_window.texture);
    if(source_route_window.renderer)SDL_DestroyRenderer(source_route_window.renderer);
    if(source_route_window.window)SDL_DestroyWindow(source_route_window.window);
    free(source_route_window.framebuffer);memset(&source_route_window,0,sizeof(source_route_window));
}
static int source_route_capture(AudioState *audio,TsSamplePages *pages,TsInstrument *instrument)
{
    SourceRouteWindow *w=&source_route_window;TsSourceRouteUi *m=&w->model;
    m->track=w->track;m->can_rename=0;
    if(w->track>=0) {
        if(w->track>=TS_TRACKER_LANES)return 0;
        m->route=pages->tracker.lanes[w->track].output_route;
        snprintf(m->name,sizeof(m->name),"%.58s",pages->tracker.lanes[w->track].name);
    } else {
        TsBankSlot *slot=source_route_tile(pages,instrument,&m->slot);
        if(!slot)return 0;
        m->route=slot->output_route;
        snprintf(m->name,sizeof(m->name),"%.58s",slot->sample.name);
        m->can_rename=slot==&instrument->bank[m->slot];
    }
    m->array=audio->sister.spatial.controls;m->channels=audio->output_device_channels;
    if(!m->channels)m->channels=2;
    m->available=ts_clean_output_available(&m->array,m->channels,
        audio->sister.insert.separate_send?0:audio->sister.insert.controls.send_pair);
    return 1;
}
static int source_route_show(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,
                             TsSamplePages *pages,TsInstrument *instrument,int track,int slot)
{
    SourceRouteWindow *w=&source_route_window;
    if(track<0 && (slot<0||slot>=TS_BANK_SLOT_COUNT||!instrument->bank[slot].occupied)) {
        snprintf(ui->status,sizeof(ui->status),"CREATE OR LOAD A TILE BEFORE ROUTING IT");return 0;
    }
    if(track<0 && ui->mosaic_editing) {
        snprintf(ui->status,sizeof(ui->status),"MOSAIC CARDS USE THE MAIN ROUTE");return 0;
    }
    w->track=track;w->tile=track<0?instrument->bank[slot].tile_id:0;
    if(!w->window) {
        w->window=SDL_CreateWindow("TapeSister - Source Routing",SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,960,600,SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI);
        if(w->window) {
            SDL_SetWindowMinimumSize(w->window,640,400);
            w->renderer=SDL_CreateRenderer(w->window,-1,SDL_RENDERER_ACCELERATED);
            if(!w->renderer)w->renderer=SDL_CreateRenderer(w->window,-1,SDL_RENDERER_SOFTWARE);
        }
        if(w->renderer)w->texture=SDL_CreateTexture(w->renderer,SDL_PIXELFORMAT_ARGB8888,
            SDL_TEXTUREACCESS_STREAMING,640,400);
        w->framebuffer=calloc(1,sizeof(*w->framebuffer));
        if(!w->window||!w->renderer||!w->texture||!w->framebuffer) {
            snprintf(ui->status,sizeof(ui->status),"ROUTING WINDOW FAILED: %.105s",SDL_GetError());
            source_route_close();return 0;
        }
        w->id=SDL_GetWindowID(w->window);
    }
    if(device)SDL_LockAudioDevice(device);
    int ok=source_route_capture(audio,pages,instrument);
    if(device)SDL_UnlockAudioDevice(device);
    if(!ok)return 0;
    w->visible=1;w->rendered_valid=0;
    SDL_ShowWindow(w->window);SDL_RaiseWindow(w->window);return 1;
}
static void source_route_update_voices(AudioState *audio,TsTileId tile,TsSourceRoute route)
{
    unsigned rate=audio->output_rate>0?(unsigned)audio->output_rate:48000;
    TsPerformanceBank *banks[]={&audio->performance,&audio->tile_launchers,&audio->sister.performance};
    for(int b=0;b<3;++b)for(int i=0;i<banks[b]->render_limit;++i) {
        TsPerformanceVoice *v=&banks[b]->voices[i];
        if(v->active && v->route_tile==tile)ts_source_route_set(&v->output_route,route,rate);
    }
    for(int i=0;i<audio->notes.render_limit;++i) {
        TsNoteVoice *v=&audio->notes.voices[i];
        if(v->active && v->route_tile==tile)ts_source_route_set(&v->output_route,route,rate);
    }
    TsKeyboardSequence *s=&audio->keyboard_sequence;
    if(s->source && !s->source->fm)for(int i=0;i<s->source->count;++i) {
        if(s->source->voices[i].route_tile==tile)ts_source_route_set(&s->source->voices[i].output_route,route,rate);
        if(s->voices[i].route_tile==tile)ts_source_route_set(&s->voices[i].output_route,route,rate);
    }
    if(audio->audition_route_tile==tile)ts_source_route_set(&audio->audition_route,route,rate);
}
static void source_route_commit(SDL_AudioDeviceID device,AudioState *audio,TsSamplePages *pages,
                                 TsInstrument *instrument,TsSourceRoute route)
{
    SourceRouteWindow *w=&source_route_window;
    if(!ts_source_route_valid(&route,w->track>=0))return;
    if(device)SDL_LockAudioDevice(device);
    if(w->track>=0)pages->tracker.lanes[w->track].output_route=route;
    else {
        TsBankSlot *slot=source_route_tile(pages,instrument,NULL);
        if(slot){slot->output_route=route;source_route_update_voices(audio,w->tile,route);}
    }
    source_route_capture(audio,pages,instrument);
    if(device)SDL_UnlockAudioDevice(device);
    ts_tapehead_routes_changed(w->track<0?w->tile:0,route);
}
static void source_route_update(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,
                                 TsSamplePages *pages,TsInstrument *instrument)
{
    SourceRouteWindow *w=&source_route_window;
    if(!w->visible || !window_drawable(w->window) || SDL_GetTicks()-w->presented<33)return;
    if(device)SDL_LockAudioDevice(device);
    int ok=source_route_capture(audio,pages,instrument);
    if(device)SDL_UnlockAudioDevice(device);
    if(!ok){source_route_hide();return;}
    w->presented=SDL_GetTicks();
    if(w->rendered_valid && !memcmp(&w->model,&w->rendered,sizeof(w->model)) &&
       !memcmp(&w->palette,&ui->palette,sizeof(w->palette)))return;
    ts_source_route_ui_render(w->framebuffer,&w->model,&ui->palette);
    SDL_UpdateTexture(w->texture,NULL,w->framebuffer->pixels,640*sizeof(uint32_t));
    SDL_RenderClear(w->renderer);SDL_RenderCopy(w->renderer,w->texture,NULL,NULL);SDL_RenderPresent(w->renderer);
    w->presented=SDL_GetTicks();w->rendered=w->model;w->palette=ui->palette;w->rendered_valid=1;
}
static int source_route_event(SDL_Event *event,SDL_Window *main,SDL_AudioDeviceID device,
                               AudioState *audio,TsUiState *ui,TsSamplePages *pages,TsInstrument *instrument)
{
    SourceRouteWindow *w=&source_route_window;
    if(!w->window || event_window_id(event)!=w->id)return 0;
    if(event->type==SDL_WINDOWEVENT) {
        if(event->window.event==SDL_WINDOWEVENT_CLOSE)source_route_hide();
        if(event->window.event==SDL_WINDOWEVENT_EXPOSED||event->window.event==SDL_WINDOWEVENT_SIZE_CHANGED)w->rendered_valid=0;
        if(event->window.event==SDL_WINDOWEVENT_FOCUS_LOST)w->drag=0;
        return 1;
    }
    if(!w->visible)return 1;
    if(event->type==SDL_KEYDOWN && event->key.keysym.sym==SDLK_ESCAPE){source_route_hide();return 1;}
    if(event->type==SDL_KEYDOWN || event->type==SDL_KEYUP) {
        event->key.windowID=SDL_GetWindowID(main);return 0;
    }
    int x=0,y=0;
    if(event->type==SDL_MOUSEBUTTONDOWN||event->type==SDL_MOUSEBUTTONUP)logical_mouse(w->window,event->button.x,event->button.y,&x,&y);
    else if(event->type==SDL_MOUSEMOTION)logical_mouse(w->window,event->motion.x,event->motion.y,&x,&y);
    else if(event->type==SDL_MOUSEWHEEL){int rx,ry;wheel_event_mouse(event,&rx,&ry);logical_mouse(w->window,rx,ry,&x,&y);}
    else return 1;
    if(event->type==SDL_MOUSEBUTTONUP){w->drag=0;return 1;}
    TsSourceRoute r=w->model.route;int changed=0;
    int click=event->type==SDL_MOUSEBUTTONDOWN;
    int left=click && event->button.button==SDL_BUTTON_LEFT;
    int right=click && event->button.button==SDL_BUTTON_RIGHT;
    int wheel=event->type==SDL_MOUSEWHEEL?(event->wheel.y>0?1:event->wheel.y<0?-1:0):0;
    if(left) {
        if((x>=586&&y<34)||(x>=480&&y>=365)){source_route_hide();return 1;}
        if(y>=365 && x>=18&&x<162) {r=(TsSourceRoute){0};if(w->track>=0)r.mode=TS_SOURCE_INHERIT;changed=1;}
        else if(y>=365&&x>=178&&x<322&&w->model.can_rename) {
            int slot=w->model.slot;source_route_hide();begin_bank_rename(ui,instrument,slot);SDL_RaiseWindow(main);return 1;
        } else if(w->track>=0&&y>=39&&y<67&&x>=444) {r.mode=r.mode==TS_SOURCE_INHERIT?TS_SOURCE_MAIN:TS_SOURCE_INHERIT;changed=1;}
        else if(y>=78&&y<103&&x>=18) {
            r.mode=x<208?TS_SOURCE_MAIN:x<412?TS_SOURCE_PAIR:TS_SOURCE_SPEAKER;
            if(r.speaker>=w->model.array.speakers)r.speaker=0;
            if(r.second>=w->model.array.speakers || r.second==r.speaker)r.second=(r.speaker+1)%w->model.array.speakers;
            changed=1;
        }
    }
    int clean=r.mode==TS_SOURCE_PAIR||r.mode==TS_SOURCE_SPEAKER;
    if(clean && (click||wheel) && y>=144&&y<169&&x>=30&&x<348) {
        int delta=wheel?wheel:right?-1:1;int *destination=x<180?&r.speaker:&r.second;
        if(x<180||r.mode==TS_SOURCE_PAIR) {
            *destination=(*destination+w->model.array.speakers+delta)%w->model.array.speakers;
            if(r.mode==TS_SOURCE_PAIR && r.speaker==r.second)*destination=(*destination+w->model.array.speakers+delta)%w->model.array.speakers;
            changed=1;
        }
    }
    if(r.mode==TS_SOURCE_PAIR) {
        int field=y>=211&&y<239?1:y>=275&&y<301?2:0;
        if(left&&field)w->drag=field;
        if((left&&field)||(event->type==SDL_MOUSEMOTION&&w->drag)) {
            int value=SDL_max(-100,SDL_min(100,(x-32)*200/312-100));
            if(w->drag==1)r.pan=value;else r.width=value;changed=1;
        } else if(field&&(right||wheel)) {
            int *value=field==1?&r.pan:&r.width;
            *value=right?0:SDL_max(-100,SDL_min(100,*value+wheel*((SDL_GetModState()&KMOD_SHIFT)?1:5)));changed=1;
        }
    }
    if(changed)source_route_commit(device,audio,pages,instrument,r);
    return 1;
}
