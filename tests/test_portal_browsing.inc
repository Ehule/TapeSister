/* Library navigation and intentional stage replacement use the real SDL
   handler and CDP workers, including publication while the user changes mode. */
static void browsing_screenshot(const char *name,TsUiState *ui,const TsInstrument *instrument)
{
    const char *dir=getenv("TS_TEST_PORTAL_BROWSE_SHOTS");if(!dir || !*dir)return;
    SDL_Window *window=SDL_CreateWindow("Portal browsing",0,0,1920,1200,SDL_WINDOW_HIDDEN);assert(window);
    SDL_Renderer *renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);assert(renderer);
    SDL_Texture *base=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,640,400);assert(base);
    SDL_Texture *textures[2]={0};TsUiWaveformDetail details[2]={0};static TsFramebuffer fb;
    native_waveform_prepare(renderer,details,main_waveform_detail_allowed(ui));
    ts_ui_render(&fb,ui,instrument);ts_ui_render_file_recording(&fb,ui);
    assert(!SDL_UpdateTexture(base,NULL,fb.pixels,640*sizeof(uint32_t)));
    assert(!SDL_RenderCopy(renderer,base,NULL,NULL));native_waveform_copy(renderer,textures,details,&fb);
    uint32_t *pixels=malloc(1920*1200*sizeof(uint32_t));assert(pixels);
    assert(!SDL_RenderReadPixels(renderer,NULL,SDL_PIXELFORMAT_ARGB8888,pixels,1920*sizeof(uint32_t)));
    char path[TS_CDP_PATH_MAX];snprintf(path,sizeof(path),"%s/%s.ppm",dir,name);
    FILE *file=fopen(path,"wb");assert(file);fprintf(file,"P6\n1920 1200\n255\n");
    for(size_t i=0;i<1920*1200;++i) {
        unsigned char rgb[3]={pixels[i]>>16,pixels[i]>>8,pixels[i]};assert(fwrite(rgb,1,3,file)==3);
    }
    assert(!fclose(file));free(pixels);
    for(int i=0;i<2;++i){SDL_DestroyTexture(textures[i]);ts_ui_waveform_detail_free(&details[i]);}
    SDL_DestroyTexture(base);SDL_DestroyRenderer(renderer);SDL_DestroyWindow(window);
}
static void test_portal_browsing(SDL_Window *window,SDL_AudioDeviceID device)
{
    static TsInstrument instrument;static TsUiState ui;static AudioState audio;static SisterWindow sister;
    PortalController c;TransformController transform;char error[160];
    ts_instrument_init(&instrument);ts_ui_init(&ui);ts_sister_runtime_init(&audio.sister);
    ts_note_bank_init(&audio.notes);ts_performance_init(&audio.performance);ts_performance_init(&audio.tile_launchers);
    ts_capture_init(&audio.capture);audio.bank_slot=-1;transform_controller_init(&transform);
    assert(ts_instrument_generate(&instrument,TS_GENERATOR_METALLIC,441,error,sizeof(error)));
    portal_init(&c,&ui.portal);TsPortalUi *p=&ui.portal;p->open=1;c.library_writable=1;
    snprintf(c.library_path,sizeof(c.library_path),"portal-browsing-controller.recipes");
    memset(&p->library,0,sizeof(p->library));
    snprintf(ui.config.cdp_bin_path,sizeof(ui.config.cdp_bin_path),"%s",getenv("TS_TEST_CDP_BIN"));
    assert(portal_source(device,&audio,&ui,&instrument,&c));
    uint64_t source_hash=ts_sample_hash(p->source),tile_hash=ts_sample_hash(&instrument.current);
    SDL_Event e;
#define BROWSE_EVENT() assert(portal_event(&e,window,device,&audio,&ui,&instrument,&c,&sister,44100,&transform))
#define BROWSE_CLICK(X,Y) do {memset(&e,0,sizeof(e));e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=SDL_GetWindowID(window);e.button.button=SDL_BUTTON_LEFT;e.button.x=(X);e.button.y=(Y);BROWSE_EVENT();}while(0)
#define BROWSE_KEY(K) do {memset(&e,0,sizeof(e));e.type=SDL_KEYDOWN;e.key.windowID=SDL_GetWindowID(window);e.key.keysym.sym=(K);BROWSE_EVENT();}while(0)
#define BROWSE_PICK(T,Q) do {p->tab=(T);p->family=0;p->channel_filter=TS_PORTAL_CHANNEL_ANY;p->scroll=0;snprintf(p->query,sizeof(p->query),"%s",(Q));BROWSE_CLICK(25,TS_PORTAL_LIST_Y+4);}while(0)
    /* Every starter can be browsed in either direction without accumulating
       stages, including after selecting a different stage or tweaking it. */
    const char *starters[]={"DUST HALO","DARK GLASS","BROKEN SIGNAL","SLOW BLOOM"};
    for(int i=0;i<4;++i) {
        BROWSE_PICK(0,starters[i]);assert(p->chain_active && !p->chain_edit && p->chain.stage_count==3);
        BROWSE_CLICK(490,116);assert(p->chain_stage==2 && !p->chain_edit);
        p->recipe.values[0]=.4; /* A parameter edit must not arm library replacement. */
        BROWSE_PICK(0,"factory.reform");
        assert(!p->chain_active && !p->chain_edit && !p->chain.stage_count && !strcmp(p->recipe.process_id,"factory.reform"));
        BROWSE_PICK(0,starters[i]);BROWSE_PICK(0,"modify.loudness.1");
        assert(!p->chain_active && !p->chain.stage_count && !strcmp(p->recipe.process_id,"modify.loudness.1"));
    }
    BROWSE_PICK(0,"DUST HALO");p->query[0]=0;
    BROWSE_KEY(SDLK_DOWN);assert(p->chain_active && !strcmp(p->chain.name,"DARK GLASS"));
    BROWSE_KEY(SDLK_UP);assert(!strcmp(p->chain.name,"DUST HALO"));
    BROWSE_KEY(SDLK_UP);assert(!p->chain_active && !strcmp(p->recipe.process_id,"factory.granulate"));
    BROWSE_KEY(SDLK_DOWN);assert(p->chain_active && !p->chain_edit && p->chain.stage_count==3);

    /* SAVE and PINS follow the same whole-item rule. In EDIT, single saved
       recipes replace only the selected stage with their full metadata. */
    TsPortalRecipe starter,gain;assert(ts_portal_instrument_recipe(2,&starter));
    ts_portal_recipe_default(&gain,ts_portal_process_find("modify.loudness.1"));gain.values[0]=.375;
    snprintf(gain.name,sizeof(gain.name),"MY GAIN");
    assert(ts_portal_macro_set(&gain,0,"LEVEL",.2,.8,error,sizeof(error)));
    p->library.recipes[3]=p->library.pins[6]=starter;
    p->library.recipes[9]=p->library.pins[12]=gain;
    for(int tab=1;tab<=2;++tab) {
        BROWSE_PICK(tab,"BROKEN SIGNAL");assert(p->chain_active && !p->chain_edit);
        BROWSE_PICK(tab,"MY GAIN");assert(!p->chain_active && p->recipe.values[0]==.375);
        BROWSE_PICK(tab,"BROKEN SIGNAL");BROWSE_CLICK(490,102);assert(p->chain_stage==1);
        BROWSE_CLICK(590,207);assert(p->chain_edit);
        TsPortalRecipe before=portal_working(p);BROWSE_PICK(tab,"MY GAIN");
        TsPortalRecipe after=portal_working(p);
        assert(p->chain_edit && after.stage_count==3 && p->chain_stage==1);
        assert(!memcmp(&before.stages[0],&after.stages[0],sizeof(TsPortalStep)));
        assert(!memcmp(&before.stages[2],&after.stages[2],sizeof(TsPortalStep)));
        assert(!strcmp(p->recipe.macros[0].name,"LEVEL") && p->recipe.values[0]==.375);
        uint64_t generation=c.generation;BROWSE_PICK(tab,"BROKEN SIGNAL");
        assert(c.generation==generation && p->chain_edit && p->recipe.values[0]==.375);
        assert(strstr(p->message,"TURN EDIT OFF"));
        BROWSE_CLICK(590,207);BROWSE_PICK(tab,"BROKEN SIGNAL");
        assert(!p->chain_edit && !memcmp(&p->chain,&starter,sizeof(starter)));
    }
    assert(!memcmp(&p->library.recipes[3],&starter,sizeof(starter)));
    assert(!memcmp(&p->library.pins[6],&starter,sizeof(starter)));

    /* Browse a completed chain. EDIT is navigation state only: valid audio,
       audition notes, source/result selections, caches and auto timer survive. */
    BROWSE_PICK(0,"BROKEN SIGNAL");p->query[0]=0;p->scroll=156;
    portal_preview(device,&audio,&ui,&c);assert(c.worker);
    BROWSE_CLICK(590,207);assert(p->chain_edit); /* Changed while worker is running. */
    wait_portal(&audio,&ui,&instrument,&c);assert(p->valid && p->chain_edit && p->chain_cached==7);
    p->auto_preview=1;c.auto_pending=0;
    p->waves[0].has_selection=1;p->waves[0].selection_first=100;p->waves[0].selection_last=2000;
    p->waves[1].has_selection=1;p->waves[1].selection_first=200;p->waves[1].selection_last=1700;
    p->listen_result=1;portal_note_on(device,&audio,&ui,0,44100);
    portal_poll(device,&audio,&ui,&instrument,&c); /* UI note count updates on polling. */
    int notes=p->note_count;assert(notes>0);
    const TsSample *result=p->result;uint64_t result_hash=ts_sample_hash(result),generation=c.generation;
    unsigned cached=p->chain_cached;Uint32 due=c.auto_due;
    BROWSE_CLICK(590,207);assert(!p->chain_edit);
    assert(c.generation==generation && !c.auto_pending && c.auto_due==due);
    assert(p->valid && p->result==result && ts_sample_hash(p->result)==result_hash && p->chain_cached==cached && p->note_count==notes);
    assert(p->waves[0].selection_first==100 && p->waves[1].selection_first==200);
    browsing_screenshot("portal-browse-chain",&ui,&instrument);
    BROWSE_CLICK(590,207);assert(p->chain_edit);
    browsing_screenshot("portal-edit-chain",&ui,&instrument);
    /* A raw library arrow replaces the chosen stage only while EDIT is on. */
    BROWSE_CLICK(490,116);assert(p->chain_stage==2);
    BROWSE_PICK(0,"modify.loudness.1");assert(p->chain_active && p->chain_edit && p->chain.stage_count==3);
    p->query[0]=0;BROWSE_KEY(SDLK_DOWN);assert(p->chain_stage==2 && p->chain.stage_count==3 && !strcmp(p->recipe.process_id,"modify.loudness.2"));
    for(int i=0;i<TS_NOTE_BANK_VOICE_CAPACITY;++i)assert(!audio.notes.voices[i].sample);
    /* ADD still inserts just once in browse mode. Cancel preserves the mode;
       turning EDIT off always cancels a pending insertion. */
    BROWSE_CLICK(490,70);assert(p->chain_add);BROWSE_CLICK(590,207);assert(!p->chain_add && !p->chain_edit);
    BROWSE_CLICK(490,70);BROWSE_CLICK(490,70);assert(!p->chain_add && !p->chain_edit && strstr(p->message,"LOADS WHOLE"));
    BROWSE_CLICK(490,70);BROWSE_PICK(0,"DUST HALO");assert(p->chain_add && p->chain.stage_count==3);
    BROWSE_PICK(0,"modify.loudness.1");assert(p->chain.stage_count==4 && !p->chain_add && !p->chain_edit);
    BROWSE_PICK(0,"modify.loudness.1");assert(!p->chain_active);

    /* Old chain work must never overwrite a newer single process; only the
       last auto-preview may publish, with output matching a direct CDP run. */
    BROWSE_PICK(0,"BROKEN SIGNAL");portal_preview(device,&audio,&ui,&c);assert(c.worker);
    PortalWorker *old=c.worker;BROWSE_PICK(0,"modify.loudness.1");
    assert(SDL_AtomicGet(&old->cancel) && c.auto_pending && !p->valid && !p->chain_active);
    c.auto_due=SDL_GetTicks()+60000;wait_portal(&audio,&ui,&instrument,&c);
    assert(!p->valid && !p->chain_active);
    portal_auto_poll(device,&audio,&ui,&c,c.auto_due);assert(c.worker);
    wait_portal(&audio,&ui,&instrument,&c);assert(p->valid && !p->chain_active);
    TsCdpRuntime runtime;TsCdpRunResult direct;TsCdpRunOptions options;
    ts_cdp_runtime_init(&runtime);ts_cdp_run_result_init(&direct);ts_cdp_run_options_init(&options);
    assert(ts_cdp_runtime_discover(&runtime,getenv("TS_TEST_CDP_BIN"),NULL,error,sizeof(error)));
    assert(ts_cdp_run_portal(&runtime,&p->recipe,&c.source,&options,&direct,error,sizeof(error)));
    assert(ts_sample_hash(&direct.output)==ts_sample_hash(p->result));ts_cdp_run_result_free(&direct);
    browsing_screenshot("portal-browse-single",&ui,&instrument);

    /* Creating a chain deliberately starts editing. Completion preserves a
       pending ADD; explicitly recalling history returns to whole-item browsing. */
    BROWSE_CLICK(25,365);assert(p->chain_edit && p->chain_active && p->chain.stage_count==1);
    portal_preview(device,&audio,&ui,&c);assert(c.worker);
    BROWSE_CLICK(490,70);assert(p->chain_add);
    wait_portal(&audio,&ui,&instrument,&c);assert(p->chain_edit && p->chain_add && p->valid);
    portal_save(p,&c,0);
    static TsPortalLibrary restored;assert(ts_portal_library_load(&restored,c.library_path,error,sizeof(error)));
    assert(restored.recipes[0].stage_count==1);
    BROWSE_CLICK(64+p->history_selected*107+5,366);
    assert(p->valid && p->chain_active && !p->chain_edit && !p->chain_add);
    BROWSE_PICK(0,"factory.reform");assert(!p->chain_active);
    assert(ts_sample_hash(p->source)==source_hash && ts_sample_hash(&instrument.current)==tile_hash);
    portal_close(device,&audio,&ui,&c);portal_free(&c);remove(c.library_path);
    ts_instrument_free(&instrument);ts_sister_runtime_free(&audio.sister);
    ts_performance_free(&audio.performance);ts_performance_free(&audio.tile_launchers);ts_capture_free(&audio.capture);
#undef BROWSE_EVENT
#undef BROWSE_CLICK
#undef BROWSE_KEY
#undef BROWSE_PICK
    puts("Portal browsing: whole items, explicit editing, saved/pinned recipes, live mode changes and stale-worker rejection passed");
}
