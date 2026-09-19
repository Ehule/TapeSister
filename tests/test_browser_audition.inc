static void test_browser_audition(SDL_Window *window,SDL_AudioDeviceID device,
    AudioState *audio,TsUiState *ui,TsInstrument *instrument,const TsSample *sample)
{
    const char *name="Drone-Mix.wav";char error[160];
    assert(ts_sample_save_wav32f(sample,name,error,sizeof(error)));
    uint64_t original=ts_sample_hash(&instrument->current);
    BrowserAudition c={0};ui->portal.open=0;ui->import_preview_open=0;ui->file_busy=0;
    ts_sister_runtime_disable(&audio->sister);
    ts_browser_init(&ui->browser);ui->browser.mode=TS_BROWSER_LOAD_WAV;
    snprintf(ui->browser.directory,sizeof(ui->browser.directory),".");
    snprintf(ui->browser.entries[0].name,sizeof(ui->browser.entries[0].name),"%s",name);
    ui->browser.entry_count=1;ui->browser.selected=0;ui->browser.filename_focus=0;
    SDL_Event e={0};e.type=SDL_KEYDOWN;e.key.windowID=SDL_GetWindowID(window);e.key.keysym.sym=SDLK_SPACE;
#define BROWSER_KEY() browser_audition_event(&e,window,device,audio,ui,&c,44100)
    assert(BROWSER_KEY() && ui->browser.preview_loading && !ui->file_busy);
    e.key.repeat=1;assert(BROWSER_KEY() && c.loader.worker);e.key.repeat=0;
    for(int n=0;n<5000 && c.loader.worker;++n) {
        SDL_Delay(1);browser_audition_poll(device,audio,ui,&c,44100);
    }
    assert(!c.loader.worker && ui->browser.preview_playing && !ui->import_preview_open);
    assert(ui->browser.mode==TS_BROWSER_LOAD_WAV && ts_sample_hash(&instrument->current)==original);
    float block[512];double energy=0;
    for(int n=0;n<8;++n) {
        audio_callback(audio,(Uint8*)block,sizeof(block));
        for(int i=0;i<512;++i){assert(isfinite(block[i]));energy+=fabsf(block[i]);}
    }
    assert(energy>.01);
    const char *shot=getenv("TS_TEST_BROWSER_AUDITION_PPM");
    if(shot){static TsFramebuffer fb;ts_ui_render(&fb,ui,instrument);assert(ts_ui_write_ppm(&fb,shot));}
    assert(BROWSER_KEY() && !audio->browser_voice.active && !ui->browser.preview_playing);
    assert(BROWSER_KEY() && ui->browser.preview_playing && !c.loader.worker); /* Replay the cache. */
    ui->browser.filename_focus=1;assert(!BROWSER_KEY());ui->browser.filename_focus=0;
    ui->browser.creating_directory=1;assert(!BROWSER_KEY());ui->browser.creating_directory=0;
    e.key.keysym.mod=KMOD_CTRL;assert(!BROWSER_KEY());e.key.keysym.mod=0;
    /* Selection changes cancel playback and retire its buffer before replacement. */
    ui->browser.selected=-1;browser_audition_poll(device,audio,ui,&c,44100);
    assert(!audio->browser_voice.sample && !c.loader.decoded.sample.data);
    ui->browser.selected=0;
    assert(BROWSER_KEY() && c.loader.worker);
    assert(BROWSER_KEY() && !c.loader.worker && !ui->browser.preview_loading);
    assert(!ui->import_preview_open);
    assert(BROWSER_KEY());ui->browser.mode=TS_BROWSER_CLOSED;
    browser_audition_poll(device,audio,ui,&c,44100);
    assert(!c.loader.worker && !audio->browser_voice.active);
    ui->browser.mode=TS_BROWSER_LOAD_WAV;assert(BROWSER_KEY());
    e.type=SDL_WINDOWEVENT;e.window.windowID=SDL_GetWindowID(window);e.window.event=SDL_WINDOWEVENT_FOCUS_LOST;
    assert(!BROWSER_KEY() && !c.loader.worker && !audio->browser_voice.active);
    FILE *raw=fopen("test-browser-raw.bin","wb");assert(raw);fputs("not audio",raw);fclose(raw);
    snprintf(ui->browser.entries[0].name,sizeof(ui->browser.entries[0].name),"test-browser-raw.bin");
    e.type=SDL_KEYDOWN;e.key.windowID=SDL_GetWindowID(window);e.key.keysym.sym=SDLK_SPACE;e.key.keysym.mod=0;
    assert(BROWSER_KEY() && !c.loader.worker && strstr(ui->browser.message,"RAW"));
    browser_audition_stop(device,audio,ui,&c,1);ts_browser_close(&ui->browser);
    assert(ts_sample_hash(&instrument->current)==original);
    remove(name);remove("test-browser-raw.bin");
#undef BROWSER_KEY
}
