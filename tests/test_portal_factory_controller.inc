static void test_factory_workflow(SDL_Window *window,SDL_AudioDeviceID device)
{
    static TsInstrument instrument;static TsUiState ui;static AudioState audio;
    static SisterWindow sister;PortalController c;TransformController transform;
    char error[160],old_config[TS_RUNTIME_PATH_MAX];
    snprintf(old_config,sizeof(old_config),"%s",runtime_config_path);
    snprintf(runtime_config_path,sizeof(runtime_config_path),"portal-factory-controller.ini");
    ts_instrument_init(&instrument);ts_ui_init(&ui);
    ts_sister_runtime_init(&audio.sister);ts_note_bank_init(&audio.notes);
    ts_performance_init(&audio.performance);ts_performance_init(&audio.tile_launchers);
    ts_capture_init(&audio.capture);audio.bank_slot=-1;transform_controller_init(&transform);
    assert(ts_instrument_generate(&instrument,TS_GENERATOR_METALLIC,999,error,sizeof(error)));
    portal_init(&c,&ui.portal);TsPortalUi *p=&ui.portal;
    snprintf(ui.config.cdp_bin_path,sizeof(ui.config.cdp_bin_path),"%s",getenv("TS_TEST_CDP_BIN"));
    /* A filtered bank's first tile must open its actual factory identity. */
    int enabled[TS_CDP_FACTORY_RECIPE_COUNT]={0};int index=ts_cdp_recipe_index_for_id("reform");
    enabled[index]=1;ts_cdp_catalog_view_build(&ui.cdp_catalog,enabled,TS_CDP_FACTORY_RECIPE_COUNT);
    ui.cdp_presets[index].controls[0]=4;ui.show_recipes=1;ui.cdp_user_pins=0;ui.cdp_page=0;
    instrument.has_selection=1;instrument.selection_first=4000;instrument.selection_last=20000;
    SDL_Event click={0};click.type=SDL_MOUSEBUTTONDOWN;click.button.button=SDL_BUTTON_MIDDLE;
    click.button.windowID=SDL_GetWindowID(window);click.button.x=20;click.button.y=340;
#define FACTORY_EVENT() assert(portal_event(&click,window,device,&audio,&ui,&instrument,&c,&sister,44100,&transform))
    FACTORY_EVENT();assert(p->open && !ui.transform_open && !c.worker);
    assert(!strcmp(p->recipe.process_id,"factory.reform") && p->recipe.values[0]==4);
    assert(p->source->frames==16000 && c.selection_scope && p->family==TS_PORTAL_FACTORY+1);
    /* Named choices remain valid, and save changes only the selected bank slot. */
    p->recipe.values[0]=6;click.button.button=SDL_BUTTON_LEFT;click.button.x=550;click.button.y=365;
    FACTORY_EVENT();assert(ui.cdp_presets[index].controls[0]==6 && ui.config.cdp_factory_overridden[index]);
    TsConfig disk;ts_config_init(&disk);assert(ts_config_load(&disk,runtime_config_path,error,sizeof(error)));
    assert(disk.cdp_factory_controls[index][0]==6);
    for(int n=0;n<TS_CDP_FACTORY_RECIPE_COUNT;++n)if(n!=index)assert(!disk.cdp_factory_overridden[n]);
    static TsConfig before;before=ui.config;
    snprintf(runtime_config_path,sizeof(runtime_config_path),"missing-factory-directory/preset.ini");
    p->recipe.values[0]=7;FACTORY_EVENT();assert(ui.cdp_presets[index].controls[0]==6 && !memcmp(&before,&ui.config,sizeof(before)));
    snprintf(runtime_config_path,sizeof(runtime_config_path),"portal-factory-controller.ini");
    portal_preview(device,&audio,&ui,&c);wait_portal(&audio,&ui,&instrument,&c);assert(p->valid);
    selection_screenshot("TS_TEST_PORTAL_FACTORY_SCREENSHOT",&ui,&instrument);
    /* The exact same factory stage can be followed by an ordinary Portal stage. */
    click.button.x=25;FACTORY_EVENT();assert(p->chain_active && p->chain.stage_count==1);
    click.button.x=490;click.button.y=70;FACTORY_EVENT();assert(p->chain_add);
    p->family=0;snprintf(p->query,sizeof(p->query),"LINEAR GAIN");click.button.x=25;click.button.y=130;FACTORY_EVENT();
    assert(p->chain.stage_count==2 && !strcmp(p->recipe.process_id,"modify.loudness.1"));p->recipe.values[0]=.5;
    portal_preview(device,&audio,&ui,&c);wait_portal(&audio,&ui,&instrument,&c);assert(p->valid);
    assert(fabs(ts_sample_peak(p->result)-.5*ts_sample_peak(&c.stages[0].output))<.0001);
    const float *first=c.stages[0].output.data;
    portal_invalidate(device,&audio,p,&c);p->recipe.values[0]=.25;
    portal_preview(device,&audio,&ui,&c);assert(c.worker->chain_start==1);
    wait_portal(&audio,&ui,&instrument,&c);assert(p->valid && c.stages[0].output.data==first);
    uint64_t original=ts_sample_hash(&instrument.current);
    assert(portal_apply(device,&audio,&ui,&instrument,&c,0));assert(ts_sample_hash(&instrument.current)!=original);
    assert(ts_instrument_undo(&instrument,error,sizeof(error)) && ts_sample_hash(&instrument.current)==original);
    portal_close(device,&audio,&ui,&c);portal_free(&c);ts_instrument_free(&instrument);
    ts_sister_runtime_free(&audio.sister);ts_performance_free(&audio.performance);
    ts_performance_free(&audio.tile_launchers);ts_capture_free(&audio.capture);
    remove("portal-factory-controller.ini");snprintf(runtime_config_path,sizeof(runtime_config_path),"%s",old_config);
#undef FACTORY_EVENT
}
