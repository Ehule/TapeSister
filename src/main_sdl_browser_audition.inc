/* Browser audition has its own immutable buffer and voice. It never changes
   the current tile or the retained import-preview document. Decode stays off
   the UI/audio threads; cancellation cannot publish an obsolete selection. */
typedef struct { ImportController loader; } BrowserAudition;

static void browser_audition_stop(SDL_AudioDeviceID device,AudioState *audio,
                                  TsUiState *ui,BrowserAudition *c,int discard)
{
    if(device)SDL_LockAudioDevice(device);
    memset(&audio->browser_voice,0,sizeof(audio->browser_voice));
    if(device)SDL_UnlockAudioDevice(device);
    discard_import_worker(&c->loader);
    ui->browser.preview_loading=ui->browser.preview_playing=0;
    if(discard) {
        ts_audio_import_free(&c->loader.decoded);
        c->loader.path[0]=0;
    }
}

static void browser_audition_play(SDL_AudioDeviceID device,AudioState *audio,
                                  TsUiState *ui,BrowserAudition *c,int rate)
{
    const TsSample *sample=&c->loader.decoded.sample;
    if(!device || !ts_audio_output_is_available() || rate<=0 || !sample->data || !sample->frames) {
        snprintf(ui->browser.message,sizeof(ui->browser.message),"NO AUDIO OUTPUT");return;
    }
    TsNoteVoice v={0};v.sample=sample;v.range_last=sample->frames;
    v.step=(double)sample->sample_rate/rate;v.pitch=1;v.gain=1;v.direction=1;
    v.attack_frames=ts_audition_attack_frames(rate,ui->config.voice_attack_ms);v.active=1;
    SDL_LockAudioDevice(device);audio->browser_voice=v;SDL_UnlockAudioDevice(device);
    ui->browser.preview_playing=1;
    snprintf(ui->browser.message,sizeof(ui->browser.message),"PLAYING - SPACE STOPS");
}

static void browser_audition_poll(SDL_AudioDeviceID device,AudioState *audio,
                                  TsUiState *ui,BrowserAudition *c,int rate)
{
    if(!c->loader.path[0])return;
    char selected[TS_BROWSER_PATH_MAX];
    if(ui->browser.mode!=TS_BROWSER_LOAD_WAV || ui->file_busy ||
       !ts_browser_selected_path(&ui->browser,selected,sizeof(selected)) ||
       strcmp(selected,c->loader.path)) {
        browser_audition_stop(device,audio,ui,c,1);return;
    }
    ImportWorker *w=c->loader.worker;
    if(w && SDL_AtomicGet(&w->done)) {
        SDL_WaitThread(w->thread,NULL);c->loader.worker=NULL;ui->browser.preview_loading=0;
        if(w->ok && w->automatic) {
            c->loader.decoded=w->decoded;ts_audio_import_init(&w->decoded);
            browser_audition_play(device,audio,ui,c,rate);
        } else {
            snprintf(ui->browser.message,sizeof(ui->browser.message),"PREVIEW FAILED: %.130s",
                     w->error[0]?w->error:"USE IMPORT PREVIEW FOR RAW DATA");
            c->loader.path[0]=0;
        }
        ts_audio_import_free(&w->decoded);free(w);
    }
    if(ui->browser.preview_playing) {
        if(device)SDL_LockAudioDevice(device);
        int playing=audio->browser_voice.active;
        if(device)SDL_UnlockAudioDevice(device);
        if(!playing) {
            ui->browser.preview_playing=0;
            snprintf(ui->browser.message,sizeof(ui->browser.message),"FINISHED - SPACE REPLAYS");
        }
    }
}

static int browser_audition_event(const SDL_Event *event,SDL_Window *window,
    SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,BrowserAudition *c,int rate)
{
    if(event->type==SDL_WINDOWEVENT && event->window.windowID==SDL_GetWindowID(window) &&
       (event->window.event==SDL_WINDOWEVENT_FOCUS_LOST || event->window.event==SDL_WINDOWEVENT_CLOSE)) {
        browser_audition_stop(device,audio,ui,c,1);return 0;
    }
    if(event->type!=SDL_KEYDOWN || event->key.windowID!=SDL_GetWindowID(window) ||
       ui->browser.mode!=TS_BROWSER_LOAD_WAV || ui->browser.filename_focus ||
       ui->browser.creating_directory || ui->file_busy || ui->exit_confirm_open ||
       ui->project_overwrite_confirm_open ||
       (event->key.keysym.mod&(KMOD_CTRL|KMOD_ALT|KMOD_GUI|KMOD_SHIFT)))return 0;
    if(event->key.keysym.sym==SDLK_ESCAPE && (c->loader.worker || ui->browser.preview_playing)) {
        browser_audition_stop(device,audio,ui,c,1);
        snprintf(ui->browser.message,sizeof(ui->browser.message),"STOPPED - ESC CLOSES");return 1;
    }
    if(event->key.keysym.sym!=SDLK_SPACE)return 0;
    if(event->key.repeat)return 1;
    char path[TS_BROWSER_PATH_MAX];
    if(!ts_browser_selected_path(&ui->browser,path,sizeof(path))) {
        browser_audition_stop(device,audio,ui,c,1);
        snprintf(ui->browser.message,sizeof(ui->browser.message),"SELECT AN AUDIO FILE");return 1;
    }
    if(ts_audio_import_detect_kind(path)==TS_AUDIO_IMPORT_UNKNOWN) {
        browser_audition_stop(device,audio,ui,c,1);
        snprintf(ui->browser.message,sizeof(ui->browser.message),"RAW: OPEN IMPORT PREVIEW");return 1;
    }
    if(!strcmp(path,c->loader.path)) {
        if(c->loader.worker || ui->browser.preview_playing) {
            browser_audition_stop(device,audio,ui,c,c->loader.worker!=NULL);
            snprintf(ui->browser.message,sizeof(ui->browser.message),"STOPPED - SPACE REPLAYS");
        } else browser_audition_play(device,audio,ui,c,rate);
        return 1;
    }
    browser_audition_stop(device,audio,ui,c,1);
    ImportWorker *w=calloc(1,sizeof(*w));
    if(!w){snprintf(ui->browser.message,sizeof(ui->browser.message),"PREVIEW: OUT OF MEMORY");return 1;}
    snprintf(w->path,sizeof(w->path),"%s",path);ts_audio_import_init(&w->decoded);
    w->thread=SDL_CreateThread(import_worker_main,"TapeSister Browser",w);
    if(!w->thread){free(w);snprintf(ui->browser.message,sizeof(ui->browser.message),"COULD NOT START PREVIEW DECODER");return 1;}
    c->loader.worker=w;snprintf(c->loader.path,sizeof(c->loader.path),"%s",path);
    ui->browser.preview_loading=1;
    snprintf(ui->browser.message,sizeof(ui->browser.message),"LOADING - SPACE CANCELS");return 1;
}
