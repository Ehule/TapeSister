static int ts_audio_health_show(TsUiState *ui);
/* Embedded tracker workspace: host-level commands only. Upstream owns input. */
static struct { SDL_Window *window; SDL_Renderer *renderer; SDL_Texture *texture;
                SDL_AudioDeviceID *device; int mouse_x,mouse_y; } embedded_host;
static void embedded_lock(void *context) {
    (void)context;if(embedded_host.device && *embedded_host.device)SDL_LockAudioDevice(*embedded_host.device);
}
static void embedded_unlock(void *context) {
    (void)context;if(embedded_host.device && *embedded_host.device)SDL_UnlockAudioDevice(*embedded_host.device);
}
static void embedded_present(void *context,const uint32_t *pixels) {
    (void)context;
    if(!embedded_host.renderer || !embedded_host.texture)return;
    SDL_UpdateTexture(embedded_host.texture,NULL,pixels,632*(int)sizeof(uint32_t));
    SDL_Rect rect={4,0,632,400};
    SDL_RenderCopy(embedded_host.renderer,embedded_host.texture,NULL,&rect);
    SDL_RenderPresent(embedded_host.renderer);
}
static int embedded_preferences_path(char *path,size_t size) {
    char directory[TS_RUNTIME_PATH_MAX];
    return runtime_path_parent(config_file_path(),directory,sizeof(directory))?
        runtime_join_path(path,size,directory,"sistertracker.cfg"):
        snprintf(path,size,"sistertracker.cfg")>0;
}
static int embedded_open(SDL_Window *window,SDL_AudioDeviceID device,AudioState *audio,
                         TsUiState *ui,TsSamplePages *pages,const TsInstrument *instrument,int rate) {
    if(!ts_tapehead_frame()) {
        embedded_host.window=window;
        embedded_host.renderer=SDL_GetRenderer(window);
        if(embedded_host.renderer)embedded_host.texture=SDL_CreateTexture(embedded_host.renderer,
            SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,632,400);
        TsTapeHeadHost host={window,NULL,embedded_lock,embedded_unlock,embedded_present};
        if(!ts_tapehead_init(&host,rate,ui->status,sizeof(ui->status)))return 0;
        char path[TS_RUNTIME_PATH_MAX];
        if(embedded_preferences_path(path,sizeof(path)))ts_tapehead_preferences_load(path,ui->status,sizeof(ui->status));
    }
    if(device)SDL_LockAudioDevice(device);
    ts_tracker_playback_stop(&audio->tracker);
    if(device)SDL_UnlockAudioDevice(device);
    if(!ts_tapehead_sync(pages,instrument,rate,ui->status,sizeof(ui->status)))return 0;
    ts_tapehead_host_lock();
    int ok=ts_tapehead_export(&pages->tracker,ui->status,sizeof(ui->status));
    ts_tapehead_host_unlock();
    if(!ok)return 0;
    ui->tracker_embedded_frame=ts_tapehead_frame();return 1;
}
static void tracker_refresh(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,
                             TsSamplePages *pages,const TsInstrument *instrument,int rate,SisterWindow *sister) {
#ifdef TAPESISTER_LEGACY_TRACKER_FIXTURE
    tracker_native_refresh(device,audio,ui,pages,instrument,rate);return;
#endif
    (void)device;
    if(!ts_tapehead_frame())return;
    if(!ts_tapehead_sync(pages,instrument,rate,ui->status,sizeof(ui->status))) {
        ui->tracker_open=0;ui->tracker_embedded_frame=NULL;return;
    }
    unsigned capturing=atomic_load(&audio->tracker_capture);
    if(sister && ts_performance_recorder_state(&sister->performance_recorder)==TS_PERFORMANCE_FILE_RECORDING)capturing=1;
    ts_tapehead_status(ui->status,capturing);
    ts_tapehead_tick();
    ui->tracker_running=ts_tapehead_running();
    int action=ts_tapehead_action();
    if(action==TS_TH_CANVAS || action==TS_TH_TILES) {
        ts_tapehead_focus_lost();ui->tracker_open=0;
        snprintf(ui->status,sizeof(ui->status),action==TS_TH_TILES?
            "SELECT A TILE ON THE CANVAS - F10 RETURNS TO SISTERTRACKER":"CANVAS - SISTERTRACKER CONTINUES / F10 RETURNS");
    }
    if(action==TS_TH_FX || action==TS_TH_PRISM || action==TS_TH_SISTER || action==TS_TH_FALLOUT) {
        if(sister_window_ensure(sister,&ui->config)) {
            ts_tapehead_focus_lost();
            sister->model.fx_page=action==TS_TH_FX?1:action==TS_TH_PRISM?3:action==TS_TH_FALLOUT?2:0;
            sister->model.fallout_lfo_open=0;
            sister->model.preset_manage_open=sister->model.preset_editing=sister->model.preset_confirmation=0;
            snprintf(sister->model.status,sizeof(sister->model.status),"%s - SISTERTRACKER CONTINUES",
                action==TS_TH_FX?"FX PEDALBOARD":action==TS_TH_PRISM?"PRISM":action==TS_TH_FALLOUT?"FALLOUT":"SISTER MACHINE");
            sister_window_show(sister,&ui->palette);
        } else snprintf(ui->status,sizeof(ui->status),"SISTER WINDOW UNAVAILABLE: %.112s",SDL_GetError());
    }
    if(action==TS_TH_SAVE_PROJECT || action==TS_TH_OPEN_PROJECT) {
        ts_tapehead_focus_lost();
        ui->tracker_open=0; /* Show the normal host load/save page. Transport continues. */
        ui->load_bank_slot=instrument->selected_slot;
        browser_open(ui,action==TS_TH_SAVE_PROJECT?TS_BROWSER_SAVE_RECIPE:TS_BROWSER_LOAD_WAV);
    }
    if(action==TS_TH_CAPTURE && sister) {
        main_file_capture_toggle(audio,ui,sister,(uint32_t)rate);
    }
    if(action==TS_TH_PALETTE_IMPORT || action==TS_TH_PALETTE_EXPORT) {
        ts_tapehead_focus_lost();
        browser_open(ui,action==TS_TH_PALETTE_IMPORT?TS_BROWSER_TRACKER_PALETTE_IMPORT:TS_BROWSER_TRACKER_PALETTE_EXPORT);
    }
    if(action==TS_TH_SAVE_DEFAULTS) {
        char path[TS_RUNTIME_PATH_MAX];
        if(embedded_preferences_path(path,sizeof(path)) && ts_tapehead_preferences_save(path,ui->status,sizeof(ui->status)))
            snprintf(ui->status,sizeof(ui->status),"TRACKER DEFAULTS SAVED");
    }
    if(action==TS_TH_AUDIO)ts_audio_health_show(ui);
    if(action==TS_TH_ROUTER) {
        if(device)SDL_LockAudioDevice(device);router_sync(audio,ui);if(device)SDL_UnlockAudioDevice(device);
        ts_tapehead_focus_lost();ui->router_open=1;ui->master_eq_open=0;
    }
}
static int tracker_event(const SDL_Event *event,SDL_Window *window,SDL_AudioDeviceID device,
    AudioState *audio,TsUiState *ui,TsSamplePages *pages,TsInstrument *instrument,int rate) {
#ifdef TAPESISTER_LEGACY_TRACKER_FIXTURE
    return tracker_native_event(event,window,device,audio,ui,pages,instrument,rate);
#endif
    if(event_window_id(event)!=SDL_GetWindowID(window))return 0;
    if(event->type==SDL_WINDOWEVENT && event->window.event==SDL_WINDOWEVENT_FOCUS_LOST) {
        ts_tapehead_focus_lost();return 0;
    }
    int key=event->type==SDL_KEYDOWN;
    SDL_Keymod mod=key?(SDL_Keymod)event->key.keysym.mod:SDL_GetModState();
    if(key && event->key.keysym.sym==SDLK_F10 && !(mod&(KMOD_CTRL|KMOD_ALT|KMOD_SHIFT|KMOD_GUI))) {
        if(event->key.repeat)return 1;
        if(ui_dialog_open(ui) || ui->mosaic_editing || ui->fm_open || ui->selecting ||
           ui->tape_dragging || ui->dragging_loop_endpoint || ui->wave_pointer_pending)return 1;
        release_mouse_note_before_keyboard_move(device,audio,ui);
        if(ui->tracker_open){ts_tapehead_focus_lost();ui->tracker_open=0;}
        else if(embedded_open(window,device,audio,ui,pages,instrument,rate)) {ui->tracker_open=1;ui->mosaic_open=0;snprintf(ui->status,sizeof(ui->status),"SISTERTRACKER - SELECT TILES ON THE CANVAS");}
        return 1;
    }
    if(event->type==SDL_KEYUP && ts_tapehead_frame()) {
        int owned=ts_tapehead_event(event,embedded_host.mouse_x,embedded_host.mouse_y);
        if(ui->tracker_open && owned)return 1;
    }
    if(!ui->tracker_open || !ui->tracker_embedded_frame || ui_dialog_open(ui) ||
       ui->router_open || ui->master_eq_open || ui->midi_learn_active)return 0;
    if(key && event->key.keysym.sym==SDLK_ESCAPE && !ts_tapehead_config_visible() && !ts_tapehead_interpolation_active()) {
        ts_tapehead_focus_lost();ui->tracker_open=0;return 1;
    }
    if(key && event->key.keysym.sym==SDLK_m && (mod&KMOD_CTRL) && (mod&KMOD_SHIFT))return 0;
    if(key && event->key.keysym.sym==SDLK_TAB && (mod&KMOD_CTRL))return 0;
    if(key && (mod&KMOD_CTRL) && (event->key.keysym.sym==SDLK_s || event->key.keysym.sym==SDLK_o)) {
        ts_tapehead_host_lock();ts_tapehead_export(&pages->tracker,ui->status,sizeof(ui->status));ts_tapehead_host_unlock();
        return 0; /* The host's existing project save/open flow owns these keys. */
    }
    int x=embedded_host.mouse_x,y=embedded_host.mouse_y;
    if(event->type==SDL_MOUSEMOTION)logical_mouse(window,event->motion.x,event->motion.y,&x,&y);
    if(event->type==SDL_MOUSEBUTTONDOWN || event->type==SDL_MOUSEBUTTONUP)logical_mouse(window,event->button.x,event->button.y,&x,&y);
    if(event->type==SDL_MOUSEWHEEL) {int rx,ry;wheel_event_mouse(event,&rx,&ry);logical_mouse(window,rx,ry,&x,&y);}
    if(event->type==SDL_MOUSEMOTION || event->type==SDL_MOUSEBUTTONDOWN ||
       event->type==SDL_MOUSEBUTTONUP || event->type==SDL_MOUSEWHEEL)x-=4;
    embedded_host.mouse_x=x;embedded_host.mouse_y=y;
    int consumed=ts_tapehead_event(event,x,y);
    if(consumed && event->type!=SDL_MOUSEMOTION && event->type!=SDL_KEYUP) {
        ts_tapehead_host_lock();ts_tapehead_export(&pages->tracker,ui->status,sizeof(ui->status));ts_tapehead_host_unlock();
    }
    return consumed;
}
