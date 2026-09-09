static void test_instrument_controls(SDL_Window *window,SDL_AudioDeviceID device)
{
    static TsInstrument instrument;static TsUiState ui;static AudioState audio;static SisterWindow sister;
    PortalController c;TransformController transform;char error[160];
    ts_instrument_init(&instrument);ts_ui_init(&ui);ts_sister_runtime_init(&audio.sister);
    ts_note_bank_init(&audio.notes);ts_performance_init(&audio.performance);ts_performance_init(&audio.tile_launchers);ts_capture_init(&audio.capture);
    audio.bank_slot=-1;transform_controller_init(&transform);
    assert(ts_instrument_generate(&instrument,TS_GENERATOR_METALLIC,441,error,sizeof(error)));
    portal_init(&c,&ui.portal);ui.portal.open=1;c.library_writable=1;
    snprintf(c.library_path,sizeof(c.library_path),"portal-instrument-controller.recipes");memset(&ui.portal.library,0,sizeof(ui.portal.library));
    snprintf(ui.config.cdp_bin_path,sizeof(ui.config.cdp_bin_path),"%s",getenv("TS_TEST_CDP_BIN"));
    TsPortalUi *p=&ui.portal;assert(portal_source(device,&audio,&ui,&instrument,&c));
    p->family=TS_PORTAL_INSTRUMENTS+1;
    SDL_Event event={0};event.type=SDL_MOUSEBUTTONDOWN;event.button.button=SDL_BUTTON_LEFT;event.button.windowID=SDL_GetWindowID(window);
#define TOOL_CLICK(X,Y,B) do {event.type=SDL_MOUSEBUTTONDOWN;event.button.windowID=SDL_GetWindowID(window);event.button.button=(B);event.button.x=(X);event.button.y=(Y);assert(portal_event(&event,window,device,&audio,&ui,&instrument,&c,&sister,44100,&transform));}while(0)
    TOOL_CLICK(25,TS_PORTAL_LIST_Y+4+3*17,SDL_BUTTON_LEFT);
    assert(p->chain_active && p->macro_view && !strcmp(p->chain.name,"SLOW BLOOM"));
    portal_preview(device,&audio,&ui,&c);wait_portal(&audio,&ui,&instrument,&c);
    assert(p->valid && p->chain_cached==7);uint64_t hash=ts_sample_hash(p->result),generation=c.generation;
    float *first=c.stages[0].output.data,*second=c.stages[1].output.data;
    TOOL_CLICK(180,303,SDL_BUTTON_RIGHT);assert(p->macro_edit==1 && p->chain_stage==2);
    assert(!keyboard_sustain_allowed(&ui));
    p->auto_preview=1;c.auto_pending=1;c.auto_due=0;portal_auto_poll(device,&audio,&ui,&c,SDL_GetTicks());assert(!c.worker);c.auto_pending=0;
    const char *fields[]={"LEVEL","0.2","0.8"};
    for(int field=0;field<3;++field) {
        event.type=SDL_KEYDOWN;event.key.windowID=SDL_GetWindowID(window);
        event.key.keysym.sym=SDLK_a;event.key.keysym.mod=KMOD_CTRL;
        assert(portal_event(&event,window,device,&audio,&ui,&instrument,&c,&sister,44100,&transform));
        event.type=SDL_TEXTINPUT;event.text.windowID=SDL_GetWindowID(window);
        snprintf(event.text.text,sizeof(event.text.text),"%s",fields[field]);
        assert(portal_event(&event,window,device,&audio,&ui,&instrument,&c,&sister,44100,&transform));
        assert(!strcmp(p->macro_text[field],fields[field]));
        event.type=SDL_KEYDOWN;event.key.windowID=SDL_GetWindowID(window);
        event.key.keysym.sym=SDLK_TAB;event.key.keysym.mod=KMOD_NONE;
        assert(portal_event(&event,window,device,&audio,&ui,&instrument,&c,&sister,44100,&transform));
    }
    assert(p->macro_field==0);
    ui.text_cursor_visible=1;selection_screenshot("TS_TEST_MACRO_EDITOR_SHOT",&ui,&instrument);
    TOOL_CLICK(205,260,SDL_BUTTON_LEFT);
    assert(!p->macro_edit && p->valid && c.generation==generation && !c.auto_pending && ts_sample_hash(p->result)==hash);
    assert(!strcmp(p->chain.stages[2].macros[0].name,"LEVEL"));
    TOOL_CLICK(377,303,SDL_BUTTON_LEFT);assert(p->chain_stage==2 && fabs(p->recipe.values[0]-.8)<1e-12 && !p->valid && p->chain_cached==3);
    event.type=SDL_MOUSEBUTTONUP;event.button.button=SDL_BUTTON_LEFT;portal_event(&event,window,device,&audio,&ui,&instrument,&c,&sister,44100,&transform);
    portal_preview(device,&audio,&ui,&c);assert(c.worker && c.worker->chain_start==2);wait_portal(&audio,&ui,&instrument,&c);
    assert(p->valid && c.stages[0].output.data==first && c.stages[1].output.data==second);
    assert(ts_sample_hash(p->result)!=hash);
    selection_screenshot("TS_TEST_CHAIN_MACROS_SHOT",&ui,&instrument);
    TOOL_CLICK(400,303,SDL_BUTTON_LEFT);assert(p->number_focus==0);
    snprintf(p->number_text,sizeof(p->number_text),"2");event.type=SDL_KEYDOWN;event.key.windowID=SDL_GetWindowID(window);event.key.keysym.sym=SDLK_RETURN;
    portal_event(&event,window,device,&audio,&ui,&instrument,&c,&sister,44100,&transform);
    assert(p->number_focus==0 && p->recipe.values[0]==.8 && p->valid);
    event.key.keysym.sym=SDLK_ESCAPE;portal_event(&event,window,device,&audio,&ui,&instrument,&c,&sister,44100,&transform);
    /* Full CDP view still has its full range; checked macros remain on all stages. */
    TOOL_CLICK(590,225,SDL_BUTTON_LEFT);assert(!p->macro_view && ts_portal_control_count(p)==1);
    TOOL_CLICK(590,225,SDL_BUTTON_LEFT);assert(p->macro_view && ts_portal_control_count(p)==3);
    portal_save(p,&c,0);assert(ui.portal.library.recipes[0].stage_count==3);
    static TsPortalLibrary restored;assert(ts_portal_library_load(&restored,c.library_path,error,sizeof(error)));
    assert(!strcmp(restored.recipes[0].stages[2].macros[0].name,"LEVEL") && restored.recipes[0].stages[2].macros[0].maximum==.8);
    /* Reorder moves the macro with its stage, preserving the parameter binding. */
    TOOL_CLICK(542,70,SDL_BUTTON_LEFT);assert(p->chain_stage==1 && !strcmp(p->recipe.macros[0].name,"LEVEL"));
    TsPortalControl control;assert(ts_portal_control_at(p,1,&control) && control.stage==1 && !strcmp(control.macro->name,"LEVEL"));
    portal_close(device,&audio,&ui,&c);portal_free(&c);remove("portal-instrument-controller.recipes");
    ts_performance_free(&audio.performance);ts_performance_free(&audio.tile_launchers);ts_sister_runtime_free(&audio.sister);ts_capture_free(&audio.capture);ts_instrument_free(&instrument);
#undef TOOL_CLICK
    puts("Chain-wide macros: native editing, ranges, caching, saved metadata and reorder passed");
}
