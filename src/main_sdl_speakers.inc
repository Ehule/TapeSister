/* Global physical setup; the separate spatial window owns project array geometry. */
typedef struct {
    SDL_Window *window;SDL_Renderer *renderer;SDL_Texture *texture;TsFramebuffer *fb;
    Uint32 id,presented,blink;int visible;
    TsSpeakersUi model;
} SpeakersWindow;
static SpeakersWindow speakers_window;
static void speakers_close(void)
{
    SpeakersWindow *w=&speakers_window;
    if(w->texture)SDL_DestroyTexture(w->texture);
    if(w->renderer)SDL_DestroyRenderer(w->renderer);
    if(w->window)SDL_DestroyWindow(w->window);
    free(w->fb);memset(w,0,sizeof(*w));
}
static int speakers_show(AudioState *audio,TsUiState *ui)
{
    SpeakersWindow *w=&speakers_window;
    if(!w->window) {
        w->window=SDL_CreateWindow("TapeSister - Speaker Setup",SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,960,600,SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI);
        if(w->window){SDL_SetWindowMinimumSize(w->window,640,400);w->renderer=SDL_CreateRenderer(w->window,-1,SDL_RENDERER_SOFTWARE);}
        if(w->renderer)w->texture=SDL_CreateTexture(w->renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,640,400);
        w->fb=calloc(1,sizeof(*w->fb));
        if(!w->window || !w->renderer || !w->texture || !w->fb){speakers_close();snprintf(ui->status,sizeof(ui->status),"SPEAKER SETUP WINDOW FAILED");return 0;}
        w->id=SDL_GetWindowID(w->window);
    }
    w->visible=1;w->model.setup=ui->config.speakers;w->model.channels=audio->output_device_channels;
    SDL_ShowWindow(w->window);SDL_RaiseWindow(w->window);return 1;
}
static void speakers_stop(SDL_AudioDeviceID device,AudioState *audio)
{
    if(device)SDL_LockAudioDevice(device);audio->speaker_test_remaining=0;if(device)SDL_UnlockAudioDevice(device);
}
static void speakers_hide(SDL_AudioDeviceID device,AudioState *audio)
{
    speakers_stop(device,audio);speakers_window.visible=speakers_window.model.editing=0;
    if(speakers_window.window)SDL_HideWindow(speakers_window.window);SDL_StopTextInput();
}
static void speakers_update(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui)
{
    SpeakersWindow *w=&speakers_window;
    if(!w->visible || !window_drawable(w->window) || SDL_GetTicks()-w->presented<33)return;
    w->model.setup=ui->config.speakers;w->model.channels=audio->output_device_channels;
    if(device)SDL_LockAudioDevice(device);
    memcpy(w->model.peak,audio->speaker_peak,sizeof(w->model.peak));w->model.test=audio->speaker_test_remaining>0;
    if(device)SDL_UnlockAudioDevice(device);
    w->model.caret=((SDL_GetTicks()-w->blink)/500)%2==0;
    ts_speakers_ui_render(w->fb,&w->model,&ui->palette);
    SDL_UpdateTexture(w->texture,NULL,w->fb->pixels,640*sizeof(uint32_t));SDL_RenderClear(w->renderer);
    SDL_RenderCopy(w->renderer,w->texture,NULL,NULL);SDL_RenderPresent(w->renderer);w->presented=SDL_GetTicks();
}
static int speakers_commit(SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui,const TsSpeakerSetup *setup)
{
    if(!ts_speakers_valid(setup)) {snprintf(speakers_window.model.message,sizeof(speakers_window.model.message),"UNCHANGED: EACH ACTIVE SPEAKER NEEDS A DIFFERENT HARDWARE OUTPUT");return 0;}
    if(device)SDL_LockAudioDevice(device);
    audio->speakers=ui->config.speakers=*setup;audio->sister.spatial.hop=0;audio->speaker_test_remaining=0;
    if(device)SDL_UnlockAudioDevice(device);
    speakers_window.model.setup=*setup;
    snprintf(speakers_window.model.message,sizeof(speakers_window.model.message),"SETUP APPLIED / SAVE CONFIG TO KEEP THIS HARDWARE SETUP");return 1;
}
static int speakers_event(SDL_Event *e,SDL_Window *main,SDL_AudioDeviceID device,AudioState *audio,TsUiState *ui)
{
    SpeakersWindow *w=&speakers_window;Uint32 id=event_window_id(e);
    if(ui->config_open && id==SDL_GetWindowID(main) && e->type==SDL_MOUSEBUTTONDOWN && e->button.button==SDL_BUTTON_LEFT) {
        int x,y;logical_mouse(main,e->button.x,e->button.y,&x,&y);
        if(x>=300 && x<410 && y>=42 && y<56){speakers_show(audio,ui);return 1;}
    }
    if(!w->window || id!=w->id)return 0;
    if(e->type==SDL_WINDOWEVENT) {
        if(e->window.event==SDL_WINDOWEVENT_CLOSE)speakers_hide(device,audio);
        if(e->window.event==SDL_WINDOWEVENT_FOCUS_LOST)speakers_stop(device,audio);
        return 1;
    }
    if(w->model.editing) {
        char *name=w->model.edit;
        if(e->type==SDL_TEXTINPUT)for(const char *p=e->text.text;*p;++p) {
            size_t n=strlen(name);if(n<TS_SPEAKER_NAME-1 && *p>=32 && *p<=126 && *p!='='){name[n]=*p;name[n+1]=0;}
        }
        if(e->type==SDL_KEYDOWN) {
            if(e->key.keysym.sym==SDLK_BACKSPACE){size_t n=strlen(name);if(n)name[n-1]=0;}
            if(e->key.keysym.sym==SDLK_RETURN) {
                TsSpeakerSetup next=ui->config.speakers;
                snprintf(next.name[w->model.selected],TS_SPEAKER_NAME,"%s",name);
                if(speakers_commit(device,audio,ui,&next)){w->model.editing=0;SDL_StopTextInput();}
            }
            if(e->key.keysym.sym==SDLK_ESCAPE){w->model.editing=0;SDL_StopTextInput();}
        }
        w->blink=SDL_GetTicks();return 1;
    }
    if(e->type==SDL_KEYDOWN && e->key.keysym.sym==SDLK_ESCAPE){speakers_hide(device,audio);return 1;}
    if(e->type==SDL_KEYDOWN || e->type==SDL_KEYUP){e->key.windowID=SDL_GetWindowID(main);return 0;}
    int x,y,delta=1;
    if(e->type==SDL_MOUSEBUTTONDOWN){logical_mouse(w->window,e->button.x,e->button.y,&x,&y);delta=e->button.button==SDL_BUTTON_RIGHT?-1:1;}
    else if(e->type==SDL_MOUSEWHEEL){int rx,ry;wheel_event_mouse(e,&rx,&ry);logical_mouse(w->window,rx,ry,&x,&y);delta=wheel_event_detents(e,0);}
    else return 1;
    TsSpeakerSetup next=ui->config.speakers;
    if(y>=9 && y<31) {
        if(x>=586){speakers_hide(device,audio);return 1;}
        if(x>=492){speakers_stop(device,audio);return 1;}
        if(x>=374 && x<484) {
            int channel=ts_speakers_channel(&next,w->model.selected,audio->output_device_channels);
            if(channel<0)snprintf(w->model.message,sizeof(w->model.message),"THIS SPEAKER OUTPUT IS UNAVAILABLE");
            else if(!audio->sister.matrix.controls.enabled)snprintf(w->model.message,sizeof(w->model.message),"LEGACY PROJECT: USE ARRAY TEST IN THE AMBISONICS WINDOW");
            else {if(device)SDL_LockAudioDevice(device);audio->speaker_test_channel=channel;audio->speaker_test_remaining=audio->output_rate;audio->speaker_test_gain=0;if(device)SDL_UnlockAudioDevice(device);}
            return 1;
        }
        if(x>=210 && x<366){next.count=SDL_clamp(next.count+(e->type==SDL_MOUSEWHEEL?delta:x<288?-1:1),1,16);speakers_commit(device,audio,ui,&next);}
        return 1;
    }
    if(y>=76 && y<332) {
        int col=x>=328,index=col*8+(y-76)/32,local=x-(14+col*314);
        if(index<16 && local>=0 && local<296) {
            w->model.selected=index;
            if(local>=24 && local<198 && e->type==SDL_MOUSEBUTTONDOWN) {
                snprintf(w->model.edit,sizeof(w->model.edit),"%s",next.name[index]);w->model.editing=1;w->blink=SDL_GetTicks();SDL_StartTextInput();
            } else if(local>=206) {
                next.output[index]=SDL_clamp(next.output[index]+delta,0,63);speakers_commit(device,audio,ui,&next);
            }
        }
    }
    return 1;
}
