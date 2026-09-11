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
    assert(CREATE_EVENT() && !c.worker && !ui.cdp_create);
    uint64_t fresh=ts_sample_hash(&instrument.current);assert(fresh!=before);
    assert(CREATE_EVENT() && !c.worker);fresh=ts_sample_hash(&instrument.current);
    assert(ts_sample_hash(&c.create_original)==fresh);
    /* Known safe ring-modulation dice isolate lifecycle from spectral failures. */
    uint64_t lifecycle_root=1;
    for(;;++lifecycle_root) {
        TsFmSeedSequence probe;ts_fm_seed_sequence_init(&probe,lifecycle_root);
        if(ts_fm_seed_sequence_next(&probe)%12==1)break;
    }
    ts_fm_seed_sequence_init(&c.create_dice,lifecycle_root);
    e.button.button=SDL_BUTTON_RIGHT;assert(CREATE_EVENT() && c.worker && c.create_roll);
    assert(ts_sample_hash(&instrument.current)==fresh && ts_sample_hash(&c.source)==fresh);
    wait_portal(&audio,&ui,&instrument,&c);
    assert(!strncmp(ui.status,"CREATED",7) && !ui.portal.open && c.create_ready);
    uint64_t variation=ts_sample_hash(&instrument.current);assert(variation!=fresh && !ui.portal.valid);
    create_native_shot("TS_TEST_CREATE_SHOT",&ui,&instrument);
    TsPortalRecipe rolled=portal_working(&ui.portal);
    assert(strstr(rolled.description,"CREATE seed") && ts_portal_recipe_validate(&rolled,error,sizeof(error)));
    e.button.button=SDL_BUTTON_LEFT;e.button.x=475;e.button.y=15;
    assert(CREATE_EVENT() && ui.portal.open && !c.worker);
    assert(!memcmp(&rolled,&ui.portal.recipe,sizeof(rolled)));portal_close(device,&audio,&ui,&c);
    /* Reroll starts from the SAME clean source, never the processed variation. */
    e.button.x=100;e.button.y=214;e.button.button=SDL_BUTTON_RIGHT;
    ts_fm_seed_sequence_init(&c.create_dice,lifecycle_root);
    assert(CREATE_EVENT() && c.worker);
    assert(ts_sample_hash(&instrument.current)==fresh && ts_sample_hash(&c.worker->input)==fresh);
    wait_portal(&audio,&ui,&instrument,&c);assert(ts_sample_hash(&instrument.current)==variation);
    e.button.button=SDL_BUTTON_MIDDLE;assert(CREATE_EVENT());
    assert(!ui.cdp_create && ts_sample_hash(&instrument.current)==fresh);
    /* Middle cancel after worker completion but BEFORE publication cannot leak a result. */
    e.button.button=SDL_BUTTON_RIGHT;ts_fm_seed_sequence_init(&c.create_dice,lifecycle_root);
    assert(CREATE_EVENT() && c.worker);
    SDL_WaitThread(c.worker->thread,NULL);c.worker->thread=NULL;
    e.button.button=SDL_BUTTON_MIDDLE;assert(CREATE_EVENT());wait_portal(&audio,&ui,&instrument,&c);
    assert(!ui.cdp_create && ts_sample_hash(&instrument.current)==fresh && !c.create_retry);
    /* Repeated right clicks queue one new roll; replacing FM cancels that queue. */
    e.button.button=SDL_BUTTON_RIGHT;assert(CREATE_EVENT() && c.worker);
    assert(CREATE_EVENT() && c.create_retry);
    wait_portal(&audio,&ui,&instrument,&c);assert(!c.create_retry);
    assert(ts_sample_hash(&c.create_original)==fresh);
    assert(CREATE_EVENT() && c.worker);assert(CREATE_EVENT() && c.create_retry);
    e.button.button=SDL_BUTTON_LEFT;assert(CREATE_EVENT());
    uint64_t next=ts_sample_hash(&instrument.current);assert(next!=fresh);
    wait_portal(&audio,&ui,&instrument,&c);assert(ts_sample_hash(&instrument.current)==next && !c.create_retry);
    /* A completed variation must not be restored over a later manual edit or sibling. */
    e.button.button=SDL_BUTTON_RIGHT;ts_fm_seed_sequence_init(&c.create_dice,lifecycle_root);
    assert(CREATE_EVENT());wait_portal(&audio,&ui,&instrument,&c);
    instrument.current.data[100]+=.01f;++instrument.current.visual_revision;
    uint64_t edited=ts_sample_hash(&instrument.current);
    e.button.button=SDL_BUTTON_MIDDLE;assert(CREATE_EVENT());assert(ts_sample_hash(&instrument.current)==edited);
    assert(strstr(ui.status,"CURRENT EDITS KEPT"));
    int page=ui.sample_page;ui.sample_page++;assert(CREATE_EVENT());
    assert(ts_sample_hash(&instrument.current)==edited);ui.sample_page=page;
    /* A native gesture blocks CDP apply; stale audio is never overwritten. */
    e.button.button=SDL_BUTTON_LEFT;assert(CREATE_EVENT());fresh=ts_sample_hash(&instrument.current);
    e.button.button=SDL_BUTTON_RIGHT;ts_fm_seed_sequence_init(&c.create_dice,lifecycle_root);
    assert(CREATE_EVENT());ui.amplitude_gesture.active=1;
    wait_portal(&audio,&ui,&instrument,&c);ui.amplitude_gesture.active=0;
    assert(ts_sample_hash(&instrument.current)==fresh && strstr(ui.status,"FINISH CURRENT EDIT"));
    /* Selection CREATE preserves the whole source, including both outside regions. */
    size_t frames=instrument.current.frames,first=frames/4,last=frames*3/4;
    float prefix[100],suffix[100];memcpy(prefix,instrument.current.data,sizeof(prefix));
    memcpy(suffix,instrument.current.data+frames-100,sizeof(suffix));
    instrument.has_selection=1;instrument.selection_first=first;instrument.selection_last=last;
    e.button.button=SDL_BUTTON_LEFT;assert(CREATE_EVENT());fresh=ts_sample_hash(&instrument.current);
    e.button.button=SDL_BUTTON_RIGHT;ts_fm_seed_sequence_init(&c.create_dice,lifecycle_root);
    assert(CREATE_EVENT() && c.worker && c.source_first==first && c.source_last==last);
    wait_portal(&audio,&ui,&instrument,&c);
    assert(!memcmp(prefix,instrument.current.data,sizeof(prefix)));
    assert(!memcmp(suffix,instrument.current.data+instrument.current.frames-100,sizeof(suffix)));
    e.button.button=SDL_BUTTON_MIDDLE;assert(CREATE_EVENT());
    assert(ts_sample_hash(&instrument.current)==fresh && instrument.has_selection && instrument.selection_first==first && instrument.selection_last==last);
    instrument.selection_last=instrument.selection_first+10;
    e.button.button=SDL_BUTTON_LEFT;assert(CREATE_EVENT());e.button.button=SDL_BUTTON_RIGHT;
    assert(CREATE_EVENT() && !c.worker && strstr(ui.status,"100 MS"));
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
    puts("CREATE: independent FM/CDP dice, reroll, exact restore, cancellation, stale result, stamp and retained recipe checks passed");
}
