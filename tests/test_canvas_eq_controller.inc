static void wait_canvas_eq(AudioState *sound,TsUiState *view,TsInstrument *doc)
{
    Uint32 start=SDL_GetTicks();
    while(canvas_eq.thread && SDL_GetTicks()-start<20000){SDL_Delay(1);canvas_eq_poll(0,sound,view,doc);}
    assert(!canvas_eq.thread);
}
static void test_canvas_eq_controller(SDL_Window *window)
{
    AudioState *sound=calloc(1,sizeof(*sound));TsUiState *view=calloc(1,sizeof(*view));TsInstrument *doc=malloc(sizeof(*doc));assert(sound && view && doc);
    ts_ui_init(view);ts_instrument_init(doc);ts_note_bank_init(&sound->notes);ts_sister_runtime_init(&sound->sister);
    ts_performance_init(&sound->performance);ts_performance_init(&sound->tile_launchers);sound->output_rate=48000;
    char error[160];assert(ts_instrument_generate(doc,TS_GENERATOR_TONAL,719,error,sizeof(error)));
    uint64_t before=ts_sample_hash(&doc->current);TsMasterEqControls master=view->config.master_eq;
    canvas_eq_open(0,sound,view,doc);wait_canvas_eq(sound,view,doc);assert(view->canvas_eq_open && canvas_eq.dry.data);
    ts_master_eq_preset(&view->canvas_eq,TS_EQ_PRESET_WARM);view->canvas_eq.band[2].gain_db=-4;
    canvas_eq_preview(0,sound,view);assert(sound->playing && sound->canvas_eq_preview);
    SDL_Event e={0};e.type=SDL_MOUSEBUTTONDOWN;e.button.windowID=SDL_GetWindowID(window);e.button.button=SDL_BUTTON_LEFT;e.button.x=320;e.button.y=155;
    assert(canvas_eq_event(&e,window,0,sound,view,&sister,doc));
    assert(!memcmp(&master,&view->config.master_eq,sizeof(master)));
    const char *directory=getenv("TS_NEXT_SCREENSHOTS");
    if(directory) {
        char path[1024];static TsFramebuffer fb;snprintf(path,sizeof(path),"%s/canvas-eq.ppm",directory);
        ts_ui_render(&fb,view,doc);assert(ts_ui_write_ppm(&fb,path));
        view->mosaic_editing=1;snprintf(path,sizeof(path),"%s/mosaic-instance-eq.ppm",directory);
        ts_ui_render(&fb,view,doc);assert(ts_ui_write_ppm(&fb,path));view->mosaic_editing=0;
    }
    TsMasterEqControls selected=view->canvas_eq;canvas_eq_apply(0,sound,view);wait_canvas_eq(sound,view,doc);
    assert(!view->canvas_eq_open && !sound->canvas_eq_preview && !sound->playing && ts_sample_hash(&doc->current)!=before);
    assert(!memcmp(&selected,&doc->tile_eq,sizeof(selected)) && doc->undo_count>0);
    assert(ts_instrument_undo(doc,error,sizeof(error)) && ts_sample_hash(&doc->current)==before);
    canvas_eq_open(0,sound,view,doc);wait_canvas_eq(sound,view,doc);view->canvas_eq.band[1].gain_db=12;
    e.type=SDL_KEYDOWN;e.key.windowID=SDL_GetWindowID(window);e.key.keysym.sym=SDLK_ESCAPE;
    assert(canvas_eq_event(&e,window,0,sound,view,&sister,doc));assert(!view->canvas_eq_open && ts_sample_hash(&doc->current)==before);
    canvas_eq_open(0,sound,view,doc);wait_canvas_eq(sound,view,doc);view->canvas_eq.band[1].gain_db=8;
    doc->bank[doc->selected_slot].locked=1; /* An external control changes the owner during rendering. */
    canvas_eq_apply(0,sound,view);wait_canvas_eq(sound,view,doc);
    assert(!view->canvas_eq_open && ts_sample_hash(&doc->current)==before && doc->bank[doc->selected_slot].locked);
    assert(strstr(view->status,"TILE CHANGED"));
    ts_instrument_free(doc);free(doc);ts_sister_runtime_free(&sound->sister);ts_performance_free(&sound->performance);ts_performance_free(&sound->tile_launchers);free(sound);free(view);
}
