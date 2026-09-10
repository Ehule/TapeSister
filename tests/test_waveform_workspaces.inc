/* Exercise the same native texture composition used by the application. */
static void render_workspace_frame(SDL_Renderer *renderer, SDL_Texture *base,
    SDL_Texture *textures[2], TsUiWaveformDetail details[2], TsFramebuffer *fb,
    TsUiState *ui, const TsInstrument *instrument, const char *name)
{
    native_waveform_prepare(renderer,details,main_waveform_detail_allowed(ui));
    ts_ui_render(fb,ui,instrument);ts_ui_render_file_recording(fb,ui);
    assert(!SDL_UpdateTexture(base,NULL,fb->pixels,640*sizeof(uint32_t)));
    assert(!SDL_RenderCopy(renderer,base,NULL,NULL));
    native_waveform_copy(renderer,textures,details,fb);
    if(name) {
        int width,height;assert(!SDL_GetRendererOutputSize(renderer,&width,&height));
        uint32_t *pixels=malloc((size_t)width*height*sizeof(uint32_t));assert(pixels);
        assert(!SDL_RenderReadPixels(renderer,NULL,SDL_PIXELFORMAT_ARGB8888,pixels,width*sizeof(uint32_t)));
        native_pixels_shot(name,pixels,width,height);free(pixels);
    }
}

static void benchmark_workspace(SDL_Renderer *renderer, SDL_Texture *base, SDL_Texture *textures[2],
    TsUiWaveformDetail details[2], TsFramebuffer *fb, TsUiState *ui, const TsInstrument *instrument, const char *name)
{
    if(!getenv("TS_TEST_WAVE_BENCH"))return;
    Uint64 start=SDL_GetPerformanceCounter();
    for(int i=0;i<60;++i)render_workspace_frame(renderer,base,textures,details,fb,ui,instrument,NULL);
    printf("%s render_ms=%.3f\n",name,1000.0*(SDL_GetPerformanceCounter()-start)/SDL_GetPerformanceFrequency()/60);
}

static void test_workspace_waveforms(void)
{
    static TsInstrument instrument;static TsUiState ui;static TsFramebuffer fb;
    static AudioState audio;static SisterWindow sister;static PortalController portal;
    static TransformController transform;
    TsSample source,result,fm;char error[160];
    ts_instrument_init(&instrument);ts_ui_init(&ui);ts_sample_init(&source);ts_sample_init(&result);ts_sample_init(&fm);
    ts_note_bank_init(&audio.notes);ts_sister_runtime_init(&audio.sister);
    source.channels=result.channels=2;source.frames=result.frames=16384;source.sample_rate=result.sample_rate=48000;
    source.data=malloc(source.frames*2*sizeof(float));result.data=malloc(result.frames*2*sizeof(float));assert(source.data && result.data);
    for(size_t i=0;i<source.frames;++i) {
        source.data[2*i]=.35f*sinf(i*.04f)+.12f*sinf(i*.071f)+.04f*sinf(i*.41f);
        source.data[2*i+1]=.3f*sinf(i*.032f+.7f)+.13f*sinf(i*.067f)+.03f*sinf(i*.43f);
        result.data[2*i]=source.data[2*i]*.65f;result.data[2*i+1]=source.data[2*i+1]*.65f;
    }
    uint64_t hash=ts_sample_hash(&source);
    assert(ts_instrument_import_sample(&instrument,&source,0,0,source.frames,TS_LOOP_FORWARD,error,sizeof(error)));
    SDL_Window *window=SDL_CreateWindow("Workspace detail",0,0,2560,1600,SDL_WINDOW_HIDDEN);assert(window);
    SDL_Renderer *renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);assert(renderer);
    SDL_Texture *base=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,640,400);assert(base);
    SDL_Texture *textures[2]={0};TsUiWaveformDetail details[2]={0};
    portal_init(&portal,&ui.portal);ui.portal.open=1;ui.portal.source=&source;ui.portal.result=&result;ui.portal.valid=1;
    snprintf(ui.portal.source_name,sizeof(ui.portal.source_name),"STEREO TEST");
    ts_portal_recipe_default(&ui.portal.recipe,ts_portal_process_find("modify.loudness.1"));
    ui.portal.recipe.values[0]=.65;
    for(size_t i=0;i<result.frames*2;++i)if(fabsf(result.data[i])>ui.portal.peak)ui.portal.peak=fabsf(result.data[i]);
    for(int i=0;i<2;++i) {
        TsPortalWave *w=&ui.portal.waves[i];ts_portal_wave_reset(w,i?&result:&source);
        w->first=1536;w->last=3584;w->has_selection=1;w->selection_first=i?2300:1900;w->selection_last=i?3050:2650;
        w->playhead=w->selection_first;ts_portal_wave_refresh(w,i?&result:&source);
    }
    render_workspace_frame(renderer,base,textures,details,&fb,&ui,&instrument,"portal-native-detail");
    benchmark_workspace(renderer,base,textures,details,&fb,&ui,&instrument,"portal");
    assert(details[0].valid && details[1].valid && details[0].width==TS_PORTAL_WAVE_COLUMNS*4 && details[1].width==TS_PORTAL_WAVE_COLUMNS*4);
    assert(details[0].analysis_count==1 && details[1].analysis_count==1);
    size_t bins=0;for(int x=1;x<TS_PORTAL_WAVE_COLUMNS*4;++x)bins+=details[0].columns[x].first!=details[0].columns[x-1].first;
    assert(bins>1200); /* Real additional audio bins, beyond the old 310. */
    ui.portal.waves[0].playhead++;ui.portal.waves[1].selection_first++;
    render_workspace_frame(renderer,base,textures,details,&fb,&ui,&instrument,NULL);
    assert(details[0].analysis_count==1 && details[1].analysis_count==1);
    /* A replacement published at the same address and length must invalidate. */
    result.data[4000]=-.91f;ts_portal_wave_reset(&ui.portal.waves[1],&result);
    render_workspace_frame(renderer,base,textures,details,&fb,&ui,&instrument,NULL);
    assert(details[0].analysis_count==1 && details[1].analysis_count==2);
    result.frames/=2;ts_portal_wave_reset(&ui.portal.waves[1],&result);
    render_workspace_frame(renderer,base,textures,details,&fb,&ui,&instrument,NULL);
    assert(details[1].last==result.frames && details[1].analysis_count==3);
    result.channels=1;ts_portal_wave_refresh(&ui.portal.waves[1],&result);
    render_workspace_frame(renderer,base,textures,details,&fb,&ui,&instrument,NULL);
    assert(details[1].source_channels==1 && details[1].analysis_count==4);
    ui.portal.result=NULL;
    render_workspace_frame(renderer,base,textures,details,&fb,&ui,&instrument,NULL);
    assert(details[0].valid && !details[1].valid); /* No old result texture. */
    ui.portal.manage_open=1;
    render_workspace_frame(renderer,base,textures,details,&fb,&ui,&instrument,NULL);
    assert(!details[0].valid && !details[1].valid);ui.portal.manage_open=0;
    /* Adjacent physical pixels in the same old UI column select different frames. */
    assert(800/4==801/4);
    assert(portal_frame_from_pointer(&ui.portal.waves[0],window,801)>
           portal_frame_from_pointer(&ui.portal.waves[0],window,800));
    SDL_Event e={0};e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=SDL_GetWindowID(window);
    e.button.button=SDL_BUTTON_LEFT;e.button.x=801;e.button.y=300;
    assert(portal_event(&e,window,0,&audio,&ui,&instrument,&portal,&sister,48000,&transform));
    assert(ui.portal.drag_anchor==portal_frame_from_pointer(&ui.portal.waves[0],window,801));
    e.type=SDL_MOUSEMOTION;e.motion.windowID=SDL_GetWindowID(window);e.motion.x=850;e.motion.y=300;
    assert(portal_event(&e,window,0,&audio,&ui,&instrument,&portal,&sister,48000,&transform));
    assert(ui.portal.waves[0].selection_last==portal_frame_from_pointer(&ui.portal.waves[0],window,850));
    ui.portal.open=0;
    begin_fm_workspace(0,&audio,&ui,&instrument,&fm);
    render_workspace_frame(renderer,base,textures,details,&fb,&ui,&instrument,"fm-native-detail");
    benchmark_workspace(renderer,base,textures,details,&fb,&ui,&instrument,"fm");
    assert(details[0].valid && !details[1].valid && details[0].width==2384);
    uint64_t analysis=details[0].analysis_count;
    uint64_t coarse=ts_ui_waveform_cache_rebuild_count(TS_UI_WAVEFORM_FM);
    ui.active_notes=1;
    render_workspace_frame(renderer,base,textures,details,&fb,&ui,&instrument,NULL);
    assert(details[0].analysis_count==analysis && ts_ui_waveform_cache_rebuild_count(TS_UI_WAVEFORM_FM)==coarse);
    assert(render_fm_workspace(0,&audio,&ui,&instrument,&fm));
    render_workspace_frame(renderer,base,textures,details,&fb,&ui,&instrument,NULL);
    assert(details[0].analysis_count==analysis+1);
    ui.fm_bank_choice_open=1;
    render_workspace_frame(renderer,base,textures,details,&fb,&ui,&instrument,NULL);
    assert(!details[0].valid);ui.fm_bank_choice_open=0;
    close_fm_workspace(0,&audio,&ui,&fm);
    assert(ts_sample_hash(&source)==hash);
    /* Tab opens, hides and reopens fullscreen Sister, also over an FM/Portal workspace. */
    ui.config.sister_window_maximized=1;
    e=(SDL_Event){0};e.type=SDL_KEYDOWN;e.key.keysym.sym=SDLK_TAB;e.key.windowID=SDL_GetWindowID(window);
    assert(workspace_tab_event(&e,window,&sister,&ui));
    assert(sister.model.visible && !(SDL_GetWindowFlags(sister.window)&SDL_WINDOW_HIDDEN));
    for(int i=0;i<4;++i) {
        ui.portal.open=1; /* The other window's modal owner cannot trap Sister's Tab. */
        e.key.windowID=sister.window_id;
        assert(workspace_tab_event(&e,window,&sister,&ui));
        assert(!sister.model.visible && (SDL_GetWindowFlags(sister.window)&SDL_WINDOW_HIDDEN));
        ui.portal.open=0;ui.fm_open=1;e.key.windowID=SDL_GetWindowID(window);
        assert(workspace_tab_event(&e,window,&sister,&ui));
        assert(sister.model.visible && (SDL_GetWindowFlags(sister.window)&SDL_WINDOW_FULLSCREEN_DESKTOP));
    }
    e.key.windowID=sister.window_id;e.key.repeat=1;assert(workspace_tab_event(&e,window,&sister,&ui));assert(sister.model.visible);
    e.key.repeat=0;e.key.keysym.mod=KMOD_CTRL;assert(!workspace_tab_event(&e,window,&sister,&ui));
    e.key.keysym.mod=0;sister.model.preset_manage_open=1;assert(!workspace_tab_event(&e,window,&sister,&ui));sister.model.preset_manage_open=0;
    /* Live snapshot: each narrow bin contributes; never skip peaks when reducing it. */
    sister_window_fullscreen(&sister,0);SDL_SetWindowSize(sister.window,2560,1600);
    ts_sister_ui_model_init(&sister.model,&ui.config);sister.model.routing.enabled=1;sister.model.waveform.channels=2;
    sister.model.waveform.valid_bins=TS_SISTER_WAVE_BIN_COUNT;
    for(int i=0;i<TS_SISTER_WAVE_BIN_COUNT;++i) {
        float env=.15f+.3f*(.5f+.5f*sinf(i*.007f));
        float ripple=.08f*sinf(i*.53f);
        sister.model.waveform.bins[i]=(TsSisterWaveBin){-env+ripple,env+ripple,-env*.7f-ripple,env*.8f-ripple};
    }
    sister.model.engine.write_normalized=.7f;sister.model.engine.head_normalized[0]=.31f;
    sister.model.engine.head_normalized[1]=.53f;sister.model.engine.head_normalized[2]=.84f;
    native_waveform_prepare(sister.renderer,sister.waveform_details,1);
    ts_sister_ui_render(&sister.framebuffer,&sister.model,&ui.palette);
    assert(!SDL_UpdateTexture(sister.texture,NULL,sister.framebuffer.pixels,640*sizeof(uint32_t)));
    assert(!SDL_RenderCopy(sister.renderer,sister.texture,NULL,NULL));
    native_waveform_copy(sister.renderer,sister.waveform_textures,sister.waveform_details,&sister.framebuffer);
    assert(sister.waveform_details[0].valid && sister.waveform_details[1].valid);
    uint32_t *pixels=malloc(2560u*1600u*sizeof(uint32_t));assert(pixels);
    assert(!SDL_RenderReadPixels(sister.renderer,NULL,SDL_PIXELFORMAT_ARGB8888,pixels,2560*sizeof(uint32_t)));
    native_pixels_shot("sister-native-detail",pixels,2560,1600);free(pixels);
    if(getenv("TS_TEST_WAVE_BENCH"))for(int enabled=0;enabled<2;++enabled) {
        Uint64 start=SDL_GetPerformanceCounter();
        for(int i=0;i<60;++i) {
            native_waveform_prepare(sister.renderer,sister.waveform_details,enabled);
            ts_sister_ui_render(&sister.framebuffer,&sister.model,&ui.palette);
            SDL_UpdateTexture(sister.texture,NULL,sister.framebuffer.pixels,640*sizeof(uint32_t));
            SDL_RenderCopy(sister.renderer,sister.texture,NULL,NULL);
            native_waveform_copy(sister.renderer,sister.waveform_textures,sister.waveform_details,&sister.framebuffer);
        }
        printf("sister detail=%d render_ms=%.3f\n",enabled,1000.0*(SDL_GetPerformanceCounter()-start)/SDL_GetPerformanceFrequency()/60);
    }
    for(int i=0;i<2;++i) {
        ts_ui_waveform_detail_free(&details[i]);if(textures[i])SDL_DestroyTexture(textures[i]);
        ts_ui_waveform_detail_free(&sister.waveform_details[i]);if(sister.waveform_textures[i])SDL_DestroyTexture(sister.waveform_textures[i]);
    }
    SDL_DestroyTexture(sister.texture);SDL_DestroyRenderer(sister.renderer);SDL_DestroyWindow(sister.window);memset(&sister,0,sizeof(sister));
    SDL_DestroyTexture(base);SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);
    ts_sample_free(&source);ts_sample_free(&result);ts_sample_free(&fm);ts_instrument_free(&instrument);
    ts_sister_runtime_free(&audio.sister);
    puts("Portal/FM native detail, live Sister detail and fullscreen Tab checks passed");
}
