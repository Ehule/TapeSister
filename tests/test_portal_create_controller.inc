static void create_native_shot(const char *variable,TsUiState *ui,const TsInstrument *instrument)
{
    const char *path=getenv(variable);if(!path)return;
    SDL_Window *w=SDL_CreateWindow("TapeSister",0,0,1920,1200,SDL_WINDOW_HIDDEN);assert(w);
    SDL_Renderer *renderer=SDL_CreateRenderer(w,-1,SDL_RENDERER_SOFTWARE);assert(renderer);
    SDL_Texture *base=SDL_CreateTexture(renderer,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STREAMING,640,400);assert(base);
    SDL_Texture *textures[2]={0};TsUiWaveformDetail details[2]={0};static TsFramebuffer fb;
    native_waveform_prepare(renderer,details,main_waveform_detail_allowed(ui));
    ts_ui_render(&fb,ui,instrument);ts_ui_render_file_recording(&fb,ui);
    assert(!SDL_UpdateTexture(base,NULL,fb.pixels,640*sizeof(uint32_t)));
    assert(!SDL_RenderCopy(renderer,base,NULL,NULL));native_waveform_copy(renderer,textures,details,&fb);
    uint32_t *pixels=malloc(1920u*1200u*sizeof(uint32_t));assert(pixels);
    assert(!SDL_RenderReadPixels(renderer,NULL,SDL_PIXELFORMAT_ARGB8888,pixels,1920*sizeof(uint32_t)));
    FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n1920 1200\n255\n");
    for(size_t i=0;i<1920u*1200u;++i) {
        unsigned char rgb[3]={(unsigned char)(pixels[i]>>16),(unsigned char)(pixels[i]>>8),(unsigned char)pixels[i]};
        assert(fwrite(rgb,1,3,f)==3);
    }
    assert(!fclose(f));free(pixels);
    for(int i=0;i<2;++i){ts_ui_waveform_detail_free(&details[i]);SDL_DestroyTexture(textures[i]);}
    SDL_DestroyTexture(base);SDL_DestroyRenderer(renderer);SDL_DestroyWindow(w);
}
static void test_portal_create(SDL_Window *window,SDL_AudioDeviceID device)
{
    static TsInstrument instrument;static TsUiState ui;static AudioState audio;
    static SisterWindow sister;PortalController c;TransformController transform;TsFmSeedSequence seeds;
    char error[160];ts_instrument_init(&instrument);ts_ui_init(&ui);
    ts_sister_runtime_init(&audio.sister);ts_note_bank_init(&audio.notes);
    ts_performance_init(&audio.performance);ts_performance_init(&audio.tile_launchers);
    ts_capture_init(&audio.capture);audio.bank_slot=-1;
    transform_controller_init(&transform);ts_fm_seed_sequence_init(&seeds,0x43524541);
    assert(ts_instrument_generate(&instrument,TS_GENERATOR_METALLIC,777,error,sizeof(error)));
    portal_init(&c,&ui.portal);c.create_seeds=&seeds;
    snprintf(ui.config.cdp_bin_path,sizeof(ui.config.cdp_bin_path),"%s",getenv("TS_TEST_CDP_BIN"));
    SDL_Event e={0};e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=SDL_GetWindowID(window);
    e.button.x=100;e.button.y=214;e.button.button=SDL_BUTTON_LEFT;
#define CREATE_EVENT() portal_event(&e,window,device,&audio,&ui,&instrument,&c,&sister,44100,&transform)
    uint64_t before=ts_sample_hash(&instrument.current);
    assert(!CREATE_EVENT()); /* Normal CREATE remains owned by the main controller. */
    e.button.button=SDL_BUTTON_RIGHT;assert(CREATE_EVENT());assert(ui.cdp_create && !c.worker);
    assert(ts_sample_hash(&instrument.current)==before);
    e.button.button=SDL_BUTTON_LEFT;
    instrument.has_selection=1;instrument.selection_first=100;instrument.selection_last=200;
    assert(CREATE_EVENT());assert(!c.worker && strstr(ui.status,"100 MS"));
    assert(ts_sample_hash(&instrument.current)==before);instrument.has_selection=0;
    ui.amplitude_gesture.active=1;assert(CREATE_EVENT());assert(!c.worker);ui.amplitude_gesture.active=0;
    assert(CREATE_EVENT());assert(c.worker && c.create_roll && ui.cdp_creating && !ui.portal.open);
    uint64_t fresh=ts_sample_hash(&instrument.current);assert(fresh!=before);
    assert(CREATE_EVENT());wait_portal(&audio,&ui,&instrument,&c);
    assert(!ui.cdp_creating && !c.create_roll && ts_sample_hash(&instrument.current)==fresh);
    assert(strstr(ui.status,"CANCELLED"));
    assert(CREATE_EVENT());assert(c.worker);uint64_t second_fresh=ts_sample_hash(&instrument.current);
    wait_portal(&audio,&ui,&instrument,&c);
    if(strncmp(ui.status,"CREATED",7))fprintf(stderr,"CREATE completion: %s\n",ui.status);
    assert(!strncmp(ui.status,"CREATED",7) && !ui.portal.open && c.create_ready);
    assert(ts_sample_hash(&instrument.current)!=second_fresh && !ui.portal.valid);
    create_native_shot("TS_TEST_CREATE_SHOT",&ui,&instrument);
    TsPortalRecipe rolled=portal_working(&ui.portal);
    assert(strstr(rolled.description,"CREATE seed") && ts_portal_recipe_validate(&rolled,error,sizeof(error)));
    e.button.x=475;e.button.y=15;assert(CREATE_EVENT());assert(ui.portal.open && !c.worker);
    assert(!memcmp(&rolled,&ui.portal.recipe,sizeof(rolled)));
    portal_close(device,&audio,&ui,&c);
    assert(ts_instrument_undo(&instrument,error,sizeof(error)) && ts_sample_hash(&instrument.current)==second_fresh);
    /* Use a known ring-modulation roll to isolate asynchronous lifecycle
       checks from source-dependent spectral failures or hot-result review. */
    uint64_t lifecycle_root=1;
    for(;;++lifecycle_root) {
        TsFmSeedSequence probe;ts_fm_seed_sequence_init(&probe,lifecycle_root);
        if(ts_fm_seed_sequence_next(&probe)%12==1)break;
    }
    ts_fm_seed_sequence_init(&seeds,lifecycle_root);
    e.button.x=100;e.button.y=214;assert(CREATE_EVENT());assert(c.worker);
    assert(!strcmp(ui.portal.recipe.process_id,"modify.radical.5"));
    instrument.current.data[100]+=.01f;uint64_t edited=ts_sample_hash(&instrument.current);
    wait_portal(&audio,&ui,&instrument,&c);
    if(!strstr(ui.status,"NOT APPLIED"))fprintf(stderr,"CREATE stale: %s\n",ui.status);
    assert(ts_sample_hash(&instrument.current)==edited && strstr(ui.status,"NOT APPLIED"));
    ts_fm_seed_sequence_init(&seeds,lifecycle_root);
    assert(CREATE_EVENT());assert(c.worker);fresh=ts_sample_hash(&instrument.current);
    ui.amplitude_gesture.active=1;wait_portal(&audio,&ui,&instrument,&c);ui.amplitude_gesture.active=0;
    assert(ts_sample_hash(&instrument.current)==fresh && strstr(ui.status,"FINISH CURRENT EDIT"));
    /* Selection CREATE keeps both outside regions through generation and CDP. */
    size_t frames=instrument.current.frames,first=frames/4,last=frames*3/4;
    float prefix[100],suffix[100];memcpy(prefix,instrument.current.data,sizeof(prefix));
    memcpy(suffix,instrument.current.data+frames-100,sizeof(suffix));
    instrument.has_selection=1;instrument.selection_first=first;instrument.selection_last=last;
    ts_fm_seed_sequence_init(&seeds,lifecycle_root);
    assert(CREATE_EVENT());assert(c.worker && c.source_first==first && c.source_last==last);
    wait_portal(&audio,&ui,&instrument,&c);
    assert(!memcmp(prefix,instrument.current.data,sizeof(prefix)));
    assert(!memcmp(suffix,instrument.current.data+instrument.current.frames-100,sizeof(suffix)));
    /* Render an original instrument in the actual Portal, with named macros. */
    ui.portal.open=1;c.selection_scope=0;assert(portal_source(device,&audio,&ui,&instrument,&c));
    assert(ts_portal_instrument_recipe(4,&rolled));portal_load_recipe(&ui.portal,&rolled);ui.portal.macro_view=1;
    portal_preview(device,&audio,&ui,&c);wait_portal(&audio,&ui,&instrument,&c);assert(ui.portal.valid);
    snprintf(ui.portal.query,sizeof(ui.portal.query),"comet");
    create_native_shot("TS_TEST_EXPANSION_SHOT",&ui,&instrument);
    portal_close(device,&audio,&ui,&c);portal_free(&c);
    ts_performance_free(&audio.performance);ts_performance_free(&audio.tile_launchers);
    ts_sister_runtime_free(&audio.sister);ts_capture_free(&audio.capture);ts_instrument_free(&instrument);
#undef CREATE_EVENT
    puts("CREATE + CDP: mode, cancellation, undo, stale result, edit gesture, stamp and retained recipe checks passed");
}
