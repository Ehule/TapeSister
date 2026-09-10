/* Actual controller coverage: chains use the same worker, notes and tile edits. */
static void test_chain_workflow(SDL_Window *window,SDL_AudioDeviceID device)
{
    static TsInstrument instrument;static TsUiState ui;static AudioState audio;
    static SisterWindow sister;PortalController c;TransformController transform;
    char error[160];ts_instrument_init(&instrument);ts_ui_init(&ui);
    ts_sister_runtime_init(&audio.sister);ts_note_bank_init(&audio.notes);
    ts_performance_init(&audio.performance);ts_performance_init(&audio.tile_launchers);
    ts_capture_init(&audio.capture);audio.bank_slot=-1;transform_controller_init(&transform);
    assert(ts_instrument_generate(&instrument,TS_GENERATOR_METALLIC,777,error,sizeof(error)));
    portal_init(&c,&ui.portal);ui.portal.open=1;
    snprintf(c.library_path,sizeof(c.library_path),"portal-chain-controller.recipes");c.library_writable=1;
    memset(&ui.portal.library,0,sizeof(ui.portal.library));
    snprintf(ui.config.cdp_bin_path,sizeof(ui.config.cdp_bin_path),"%s",getenv("TS_TEST_CDP_BIN"));
    TsPortalUi *p=&ui.portal;assert(portal_source(device,&audio,&ui,&instrument,&c));
    ts_portal_recipe_default(&p->recipe,ts_portal_process_find("extend.scramble.1"));
    SDL_Event click={0};click.type=SDL_MOUSEBUTTONDOWN;click.button.button=SDL_BUTTON_LEFT;
    click.button.windowID=SDL_GetWindowID(window);
#define CHAIN_CLICK(X,Y) do {click.button.x=(X);click.button.y=(Y);assert(portal_event(&click,window,device,&audio,&ui,&instrument,&c,&sister,44100,&transform));}while(0)
    CHAIN_CLICK(25,365);assert(p->chain_active && p->chain.stage_count==1);
    CHAIN_CLICK(490,70);assert(p->chain_add);
    snprintf(p->query,sizeof(p->query),"SPECTRAL BLUR");CHAIN_CLICK(25,TS_PORTAL_LIST_Y+4);
    assert(p->chain.stage_count==2 && p->chain_stage==1 && !strcmp(p->recipe.process_id,"blur.blur"));
    CHAIN_CLICK(490,70);snprintf(p->query,sizeof(p->query),"LINEAR GAIN");CHAIN_CLICK(25,TS_PORTAL_LIST_Y+4);
    assert(p->chain.stage_count==3 && p->chain_stage==2);
    p->recipe.values[0]=.5;
    portal_preview(device,&audio,&ui,&c);wait_portal(&audio,&ui,&instrument,&c);
    assert(p->valid && p->chain_cached==7 && p->chain.stage_count==3);
    assert(p->result->frames==c.stages[2].output.frames);
    TsPortalRecipe full=portal_working(p);TsCdpRuntime runtime;TsCdpRunResult direct;TsCdpRunOptions options;
    ts_cdp_runtime_init(&runtime);ts_cdp_run_result_init(&direct);ts_cdp_run_options_init(&options);
    assert(ts_cdp_runtime_discover(&runtime,getenv("TS_TEST_CDP_BIN"),NULL,error,sizeof(error)));
    assert(ts_cdp_run_portal(&runtime,&full,&c.source,&options,&direct,error,sizeof(error)));
    assert(ts_sample_hash(&direct.output)==ts_sample_hash(p->result));ts_cdp_run_result_free(&direct);
    uint64_t initial=ts_sample_hash(&instrument.current),first=ts_sample_hash(&c.stages[0].output);
    const float *first_data=c.stages[0].output.data,*blur_data=c.stages[1].output.data;
    /* Edit only the final stage: earlier random and spectral renders survive. */
    portal_invalidate(device,&audio,p,&c);p->recipe.values[0]=.25;
    portal_preview(device,&audio,&ui,&c);assert(c.worker && c.worker->chain_start==2);
    wait_portal(&audio,&ui,&instrument,&c);assert(p->valid && c.stages[0].output.data==first_data && c.stages[1].output.data==blur_data);
    assert(ts_sample_hash(&c.stages[0].output)==first);
    assert(fabs(ts_sample_peak(p->result)-ts_sample_peak(&c.stages[1].output)*.25)<.0001);
    snprintf(p->chain.name,sizeof(p->chain.name),"GHOST MACHINE");
    selection_screenshot("TS_TEST_PORTAL_CHAIN_SCREENSHOT",&ui,&instrument);
    /* Each intermediate is playable; Apply still targets the final chain. */
    CHAIN_CLICK(490,88);assert(p->chain_stage==0 && p->chain_audition==0 && p->result==&c.stages[0].output && p->valid);
    portal_note_on(device,&audio,&ui,0,44100);portal_note_on(device,&audio,&ui,4,44100);
    int notes=0;for(int i=0;i<TS_NOTE_BANK_VOICE_CAPACITY;++i)if(audio.notes.voices[i].active)++notes;assert(notes==2);
    CHAIN_CLICK(520,365);assert(p->chain_audition==-1 && p->result==&c.history[p->history_selected].output);
    for(int i=0;i<TS_NOTE_BANK_VOICE_CAPACITY;++i)assert(!audio.notes.voices[i].sample);
    uint64_t final=ts_sample_hash(p->result);
    CHAIN_CLICK(490,88);assert(portal_apply(device,&audio,&ui,&instrument,&c,1));
    assert(ts_sample_hash(&instrument.current)==final && ts_sample_hash(p->source)==initial);
    /* Save the entire chain with stage macros; exact pins clear all masks. */
    snprintf(p->chain.name,sizeof(p->chain.name),"GHOST MACHINE");p->exact_pin=0;
    p->recipe.exposed=1;portal_save(p,&c,0);assert(p->library.recipes[0].stage_count==3);
    portal_save(p,&c,1);assert(p->library.pins[0].stages[0].exposed==1);
    p->pin_slot=1;p->exact_pin=1;portal_save(p,&c,1);
    for(unsigned i=0;i<3;++i)assert(!p->library.pins[1].stages[i].exposed);
    static TsPortalLibrary saved;assert(ts_portal_library_load(&saved,c.library_path,error,sizeof(error)));
    assert(!memcmp(&saved,&p->library,sizeof(saved)));
    portal_manage_open(p);p->manage_tab=0;portal_manage_select(p,0);
    snprintf(p->manage_name,sizeof(p->manage_name),"GHOST CHAIN");p->manage_action=TS_PORTAL_RENAME;
    portal_manage_commit(p,&c);assert(!strcmp(p->library.recipes[0].name,"GHOST CHAIN"));p->manage_open=0;
    /* Reorder, remove and bypass through the visible controls. */
    CHAIN_CLICK(580,70);assert(p->chain_stage==1 && !strcmp(p->chain.stages[0].process_id,"blur.blur"));
    CHAIN_CLICK(540,70);assert(p->chain_stage==0 && !strcmp(p->chain.stages[0].process_id,"extend.scramble.1"));
    CHAIN_CLICK(610,88);assert(p->chain.stages[0].bypass && !p->valid);
    CHAIN_CLICK(490,205);assert(p->chain.stage_count==2 && !strcmp(p->recipe.process_id,"blur.blur"));
    /* Backward-compatible collection load restores full chains from the browser. */
    CHAIN_CLICK(590,205);assert(!p->chain_edit);
    p->tab=1;p->family=0;p->query[0]=0;p->scroll=0;CHAIN_CLICK(25,TS_PORTAL_LIST_Y+4);
    assert(p->chain_active && p->chain.stage_count==3 && !strcmp(p->chain.name,"GHOST CHAIN"));
    /* Selection follows duration-changing stages while surrounding audio stays exact. */
    portal_invalidate(device,&audio,p,&c);portal_clear_history(&c,p);
    p->chain.stage_count=2;p->chain_stage=0;
    ts_portal_recipe_default(&p->recipe,ts_portal_process_find("modify.speed.1"));p->recipe.values[0]=.5;
    ts_portal_step_set(&p->chain.stages[0],&p->recipe);p->chain.stages[0].bypass=0;
    TsPortalRecipe gain;ts_portal_recipe_default(&gain,ts_portal_process_find("modify.loudness.1"));gain.values[0]=.5;
    ts_portal_step_set(&p->chain.stages[1],&gain);p->chain.stages[1].bypass=0;
    p->process_selection=1;p->waves[0].has_selection=1;p->waves[0].selection_first=4000;p->waves[0].selection_last=20000;
    portal_preview(device,&audio,&ui,&c);wait_portal(&audio,&ui,&instrument,&c);
    assert(p->valid && p->rendered_region.last==20000 && p->rendered_region.result_last>35000);
    size_t end=p->rendered_region.result_last;
    assert(!memcmp(p->result->data,c.source.data,4000*sizeof(float)));
    assert(!memcmp(p->result->data+end,c.source.data+20000,(c.source.frames-20000)*sizeof(float)));
    CHAIN_CLICK(490,102);portal_invalidate(device,&audio,p,&c);p->recipe.values[0]=.25;
    portal_preview(device,&audio,&ui,&c);assert(c.worker->chain_start==1);wait_portal(&audio,&ui,&instrument,&c);
    assert(p->valid && p->rendered_region.last==20000 && p->rendered_region.result_last==end);
    assert(!memcmp(p->result->data+end,c.source.data+20000,(c.source.frames-20000)*sizeof(float)));
    /* Stale tile protection remains in force after New Tile changed selection. */
    assert(!portal_apply(device,&audio,&ui,&instrument,&c,0));
    assert(ts_instrument_select_bank(&instrument,c.source_slot,error,sizeof(error)));
    uint64_t replacement=ts_sample_hash(p->result);
    assert(portal_apply(device,&audio,&ui,&instrument,&c,0));assert(ts_sample_hash(p->source)==replacement);
    assert(ts_instrument_undo(&instrument,error,sizeof(error)));assert(ts_sample_hash(&instrument.current)==initial);
    /* Failure in a later stage never publishes a partial result or edits a tile. */
    assert(portal_source(device,&audio,&ui,&instrument,&c));p->process_selection=0;
    p->chain_stage=1;ts_portal_recipe_default(&p->recipe,ts_portal_process_find("sfedit.cut.1"));p->recipe.values[1]=30;
    portal_preview(device,&audio,&ui,&c);wait_portal(&audio,&ui,&instrument,&c);
    assert(!p->valid && strstr(p->message,"STAGE 2") && ts_sample_hash(&instrument.current)==initial);
    /* Cancelled generations cannot install intermediate storage. */
    ts_portal_recipe_default(&p->recipe,ts_portal_process_find("blur.blur"));
    portal_preview(device,&audio,&ui,&c);assert(c.worker);portal_cancel(&c);
    wait_portal(&audio,&ui,&instrument,&c);assert(!p->valid);
    portal_preview(device,&audio,&ui,&c);wait_portal(&audio,&ui,&instrument,&c);assert(p->valid);
    /* Main-bank pins open the whole chain and quick-apply its final output. */
    portal_close(device,&audio,&ui,&c);ui.show_recipes=1;ui.cdp_user_pins=1;ui.cdp_page=0;
    click.button.button=SDL_BUTTON_MIDDLE;CHAIN_CLICK(20,340);
    assert(p->open && p->chain_active && p->macro_view && !p->exact_pin && p->chain.stage_count==3 && !c.worker);
    portal_close(device,&audio,&ui,&c);click.button.button=SDL_BUTTON_LEFT;CHAIN_CLICK(20,340);
    assert(c.worker && c.quick_apply);wait_portal(&audio,&ui,&instrument,&c);
    assert(!p->valid && strstr(p->message,"APPLIED") && ts_sample_hash(&instrument.current)!=initial);
    assert(ts_instrument_undo(&instrument,error,sizeof(error)) && ts_sample_hash(&instrument.current)==initial);
    /* Eight bypass stages are a true pass-through, and intermediate storage
       remains bounded even when not every stage can stay cached. */
    portal_stop(device,&audio,p,&c);portal_clear_history(&c,p);
    ts_sample_free(&c.source);c.source.frames=2000000;c.source.channels=1;c.source.sample_rate=44100;
    c.source.data=malloc(c.source.frames*sizeof(float));assert(c.source.data);
    for(size_t n=0;n<c.source.frames;++n)c.source.data[n]=.1f*sinf((float)n*.1f);
    p->source=&c.source;ts_portal_wave_reset(&p->waves[0],p->source);
    p->chain.stage_count=8;p->chain_stage=0;p->process_selection=0;
    ts_portal_recipe_default(&p->recipe,ts_portal_process_find("modify.loudness.1"));
    for(unsigned i=0;i<8;++i){ts_portal_step_set(&p->chain.stages[i],&p->recipe);p->chain.stages[i].bypass=1;}
    CHAIN_CLICK(490,70);assert(!p->chain_add && strstr(p->message,"MAXIMUM EIGHT"));
    portal_preview(device,&audio,&ui,&c);wait_portal(&audio,&ui,&instrument,&c);
    assert(p->valid && ts_sample_hash(p->result)==ts_sample_hash(p->source));
    size_t frames=0;for(unsigned i=0;i<8;++i)frames+=c.stages[i].output.frames;
    assert(frames<=TS_PORTAL_MAX_FRAMES && !c.stages[0].output.data && c.stages[7].output.data);
    CHAIN_CLICK(490,88);assert(!p->result && p->chain_audition==0);
    CHAIN_CLICK(520,365);assert(p->result && p->chain_audition==-1);
    /* Input selection invalidation cannot reuse caches from another region. */
    p->process_selection=1;p->waves[0].has_selection=1;p->waves[0].selection_first=1000;p->waves[0].selection_last=50000;
    portal_selection_changed(p,&c);portal_preview(device,&audio,&ui,&c);assert(c.worker && c.worker->chain_start==0);
    portal_cancel(&c);wait_portal(&audio,&ui,&instrument,&c);assert(!p->valid);
    portal_close(device,&audio,&ui,&c);portal_free(&c);assert(remove(c.library_path)==0);
    ts_instrument_free(&instrument);ts_sister_runtime_free(&audio.sister);
    ts_performance_free(&audio.performance);ts_performance_free(&audio.tile_launchers);ts_capture_free(&audio.capture);
    puts("Portal chain rendering, pins, selection, cancellation and cache checks passed");
#undef CHAIN_CLICK
}
